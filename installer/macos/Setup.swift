import AppKit
import SwiftUI
import CryptoKit
import CoreAudio

@_silgen_name("ab_setup_action") private func setupAction(_ ready:Int32,_ installed:Int32,_ restart:Int32) -> Int32
@_silgen_name("ab_setup_installer_result") private func setupResult(_ result:Int32) -> Int32
@_silgen_name("ab_setup_next") private func setupNext(_ verified:Int32,_ app:Int32,_ a:Int32,_ ap:Int32,_ b:Int32,_ bp:Int32,_ restart:Int32) -> Int32

struct PayloadFile: Decodable { let path: String; let sha256: String }
struct SetupManifest: Decodable { let files: [PayloadFile] }
struct Component: Sendable {
    var ready = false
    var present = false
    var detail = "Not checked yet"
}
struct SetupInventory: Sendable {
    var first = Component(), second = Component()
    var appPresent = false, appCurrent = false
    var boot = "unknown"
}
// Blocking system work belongs here, away from the UI actor.
enum SetupSystem {
    static func run(_ path:String,_ args:[String]) throws -> (Int32,String) {
        let task=Process();task.executableURL=URL(fileURLWithPath:path);task.arguments=args
        let pipe=Pipe();task.standardOutput=pipe;task.standardError=pipe
        try task.run();let data=pipe.fileHandleForReading.readDataToEndOfFile();task.waitUntilExit()
        return (task.terminationStatus,String(decoding:data,as:UTF8.self))
    }
    static func verify(_ resources:URL) throws {
        let manifest=try JSONDecoder().decode(SetupManifest.self,from:Data(contentsOf:resources.appendingPathComponent("manifest.json")))
        for file in manifest.files {
            guard !file.path.hasPrefix("/"), !file.path.split(separator:"/").contains("..") else {throw NSError(domain:"Unsafe package path",code:1)}
            let data=try Data(contentsOf:resources.appendingPathComponent(file.path))
            let digest=SHA256.hash(data:data).map{String(format:"%02x",$0)}.joined()
            guard digest==file.sha256 else {throw NSError(domain:"Package integrity check failed: \(file.path)",code:1)}
        }
        for file in ["BlackHole2ch-0.7.1.pkg","BlackHole16ch-0.7.1.pkg"] {
            let result=try run("/usr/sbin/pkgutil",["--check-signature",resources.appendingPathComponent("drivers/"+file).path])
            guard result.0==0,result.1.contains("Q5C99V536K"),result.1.contains("signed by a developer certificate issued by Apple") else {throw NSError(domain:"Vendor signature check failed: \(file)",code:1)}
        }
        guard try run("/usr/bin/codesign",["--verify","--deep","--strict",resources.appendingPathComponent("San Audisco.app").path]).0==0 else {throw NSError(domain:"San Audisco app signature check failed",code:1)}
    }
    static func active(_ uid:String)->Bool {
        var address=AudioObjectPropertyAddress(mSelector:kAudioHardwarePropertyDevices,mScope:kAudioObjectPropertyScopeGlobal,mElement:kAudioObjectPropertyElementMain)
        var size:UInt32=0
        guard AudioObjectGetPropertyDataSize(AudioObjectID(kAudioObjectSystemObject),&address,0,nil,&size)==noErr else{return false}
        var devices=[AudioObjectID](repeating:0,count:Int(size)/MemoryLayout<AudioObjectID>.size)
        guard AudioObjectGetPropertyData(AudioObjectID(kAudioObjectSystemObject),&address,0,nil,&size,&devices)==noErr else{return false}
        for device in devices {
            var property=AudioObjectPropertyAddress(mSelector:kAudioDevicePropertyDeviceUID,mScope:kAudioObjectPropertyScopeGlobal,mElement:kAudioObjectPropertyElementMain)
            var value:Unmanaged<CFString>?;var bytes=UInt32(MemoryLayout<Unmanaged<CFString>?>.size)
            if AudioObjectGetPropertyData(device,&property,0,nil,&bytes,&value)==noErr,value?.takeRetainedValue() as String? == uid{return true}
        }
        return false
    }
    static func component(_ channels:Int)->Component {
        let path="/Library/Audio/Plug-Ins/HAL/BlackHole\(channels)ch.driver"
        guard FileManager.default.fileExists(atPath:path) else{return Component(detail:"BlackHole \(channels)ch is not installed.")}
        guard let info=NSDictionary(contentsOfFile:path+"/Contents/Info.plist"),let version=info["CFBundleShortVersionString"] as? String else{return Component(present:true,detail:"BlackHole \(channels)ch exists but its version cannot be read. Review the installation.")}
        guard version.compare("0.7.1",options:.numeric) != .orderedAscending else{return Component(present:true,detail:"BlackHole \(channels)ch \(version) is older than the supported version. Existing files will be preserved.")}
        let ready=active("BlackHole\(channels)ch_UID")
        return Component(ready:ready,present:true,detail:"BlackHole \(channels)ch \(version): "+(ready ? "audio device available.":"installed, but audio device unavailable. Restart or review the installation."))
    }
    static func inventory(_ destination:URL,_ resources:URL)->SetupInventory {
        var result=SetupInventory();result.first=component(2);result.second=component(16)
        result.boot=(try? run("/usr/sbin/sysctl",["-n","kern.boottime"]).1) ?? "unknown"
        result.appPresent=["SanAudisco","bridge-engine","bridge-agent"].allSatisfy{FileManager.default.isExecutableFile(atPath:destination.appendingPathComponent("Contents/MacOS/"+$0).path)}
        let installed=Bundle(url:destination)?.infoDictionary,packaged=Bundle(url:resources.appendingPathComponent("San Audisco.app"))?.infoDictionary
        result.appCurrent=result.appPresent && installed?["CFBundleVersion"] as? String == packaged?["CFBundleVersion"] as? String && installed?["CFBundleShortVersionString"] as? String == packaged?["CFBundleShortVersionString"] as? String
        return result
    }
    static func installApp(_ resources:URL,_ destination:URL,_ state:URL) throws {
        guard try run("/usr/bin/pgrep",["-x","bridge-agent"]).0 != 0 else {throw NSError(domain:"Quit San Audisco from its menu before updating. Your current audio will stay running until you quit.",code:1)}
        let fm=FileManager.default
        try fm.createDirectory(at:destination.deletingLastPathComponent(),withIntermediateDirectories:true)
        let temporary=destination.deletingLastPathComponent().appendingPathComponent("San Audisco-install-"+UUID().uuidString+".app")
        try fm.copyItem(at:resources.appendingPathComponent("San Audisco.app"),to:temporary)
        defer{try? fm.removeItem(at:temporary)}
        guard try run("/usr/bin/codesign",["--verify","--deep","--strict",temporary.path]).0==0 else{throw NSError(domain:"Copied app failed signature verification; existing app retained.",code:1)}
        if fm.fileExists(atPath:destination.path) {
            let backups=state.deletingLastPathComponent().appendingPathComponent("app-backups")
            try fm.createDirectory(at:backups,withIntermediateDirectories:true)
            let backup=backups.appendingPathComponent("San Audisco-"+UUID().uuidString+".app")
            try fm.moveItem(at:destination,to:backup)
            do{try fm.moveItem(at:temporary,to:destination)}catch{
                let installError=error
                do{try fm.moveItem(at:backup,to:destination)}catch{throw NSError(domain:"Update failed. The previous app remains at \(backup.path). Restore it before opening San Audisco.",code:1)}
                throw installError
            }
        }else{try fm.moveItem(at:temporary,to:destination)}
        let legacy=destination.deletingLastPathComponent().appendingPathComponent("AudioBridge.app")
        if let bundle=Bundle(url:legacy),bundle.bundleIdentifier=="local.audiobridge.desktop"{try fm.trashItem(at:legacy,resultingItemURL:nil)}
    }
}

@MainActor final class SetupModel:ObservableObject {
    @Published var inventory=SetupInventory()
    @Published var message="Connect two computers and listen through one output. Setup will check this Mac and guide you through anything it needs."
    @Published var busy=false
    @Published var verified=false
    @Published var started=false
    @Published var restartPending=false
    @Published var installerOpen=false
    private var awaitingComponent:Int?
    private var installationBoot="",awaitingBoot=""
    private var loadedState=false
    let resources=Bundle.main.resourceURL!
    let destination=FileManager.default.homeDirectoryForCurrentUser.appendingPathComponent("Applications/San Audisco.app")
    let state=FileManager.default.urls(for:.applicationSupportDirectory,in:.userDomainMask)[0].appendingPathComponent("AudioBridge/setup-v2.json")
    var next:Int32 {setupNext(verified ? 1:0,inventory.appCurrent ? 1:0,inventory.first.ready ? 1:0,inventory.first.present ? 1:0,inventory.second.ready ? 1:0,inventory.second.present ? 1:0,restartPending ? 1:0)}
    var primaryTitle:String {
        if !started{return "Check this Mac"};if busy{return "Working…"};if installerOpen{return "Finish the Apple Installer"}
        switch next{case 1:return inventory.appPresent ? "Update San Audisco":"Install San Audisco";case 2:return "Install playback component";case 3:return "Install microphone component";case 4,5:return "Check again";case 6:return "Open San Audisco";default:return "Retry checks"}
    }
    var phase:Int { !started ? 0 : !verified ? 1 : next==6 ? 3:2 }
    func apply(_ result:SetupInventory) {
        inventory=result
        if !loadedState {
            if let data=try? Data(contentsOf:state),let old=try? JSONSerialization.jsonObject(with:data) as? [String:Any]{installationBoot=old["installation_boot"] as? String ?? "";let component=old["awaiting_component"] as? Int;awaitingComponent=(component==2 || component==16) ? component:nil;awaitingBoot=old["awaiting_boot"] as? String ?? ""}
            loadedState=true
        }
        if let channels=awaitingComponent,(channels==2 ? result.first.present:result.second.present){installationBoot=awaitingBoot.isEmpty ? result.boot:awaitingBoot;awaitingComponent=nil;awaitingBoot=""}
        if !installationBoot.isEmpty && result.boot != "unknown" && installationBoot != result.boot{installationBoot=""}
        restartPending = !installationBoot.isEmpty
        installerOpen=NSWorkspace.shared.runningApplications.contains{$0.bundleIdentifier=="com.apple.installer"}
        saveState()
    }
    func check() {
        guard !busy else{return};started=true;busy=true;message="Checking the package and audio components…"
        let resources=resources,destination=destination,needsVerification = !verified
        Task {
            let result=await Task.detached{()->(SetupInventory,String?) in
                do{if needsVerification{try SetupSystem.verify(resources)};return (SetupSystem.inventory(destination,resources),nil)}catch{return (SetupSystem.inventory(destination,resources),error.localizedDescription)}
            }.value
            verified=result.1==nil;apply(result.0);busy=false
            if let error=result.1{message=error}else{describeNext()}
            saveState()
        }
    }
    func describeNext() {
        if installerOpen{message="Finish or cancel the Apple Installer, then choose Check again.";return}
        switch next {
        case 1:message="The audio components already on this Mac will be kept. Install the app to continue."
        case 2,3:message="One audio component is missing. Its original installer will open and may ask for your Mac password."
        case 4:message="An existing audio component needs attention. Expand Details to see what to check; setup will preserve it."
        case 5:message="Restart your Mac when convenient, then reopen this setup. Your progress is saved."
        case 6:message="This Mac is ready. Open San Audisco to create or join a room, choose your hubs, and run Autoconfigure."
        default:message="The package must pass its checks before anything is installed."
        }
    }
    func saveState() {
        do{
            try FileManager.default.createDirectory(at:state.deletingLastPathComponent(),withIntermediateDirectories:true)
            let object:[String:Any]=["schema":1,"checked_at":ISO8601DateFormatter().string(from:Date()),"blackhole2_ready":inventory.first.ready,"blackhole16_ready":inventory.second.ready,"app_installed":inventory.appPresent,"status":message,"automatic_restart":false,"restart_pending":restartPending,"installation_boot":installationBoot,"awaiting_component":awaitingComponent ?? 0,"awaiting_boot":awaitingBoot]
            try JSONSerialization.data(withJSONObject:object,options:.prettyPrinted).write(to:state,options:.atomic)
        }catch{message += " Could not save setup progress: \(error.localizedDescription)"}
    }
    func primary() {
        guard !busy else{return}
        if !started{check();return}
        switch next{case 1:installApp();case 2:installDriver(2);case 3:installDriver(16);case 6:NSWorkspace.shared.open(destination);default:check()}
    }
    func installDriver(_ channels:Int) {
        guard verified else{return}
        if NSWorkspace.shared.runningApplications.contains(where:{$0.bundleIdentifier=="com.apple.installer"}){installerOpen=true;describeNext();return}
        // Revalidate immediately at the mutation boundary, including partial files.
        let component=channels==2 ? inventory.first:inventory.second
        let exists=FileManager.default.fileExists(atPath:"/Library/Audio/Plug-Ins/HAL/BlackHole\(channels)ch.driver")
        guard setupAction(component.ready ? 1:0,exists ? 1:0,0)==1 else{check();return}
        awaitingComponent=channels;awaitingBoot=inventory.boot;saveState()
        let file=resources.appendingPathComponent("drivers/BlackHole\(channels)ch-0.7.1.pkg")
        if setupResult(NSWorkspace.shared.open(file) ? 0:2)==2{awaitingComponent=nil;message="Apple Installer could not open. No installation was confirmed."}
        else{installerOpen=true;message="Follow the Apple Installer. Defer a restart until both components are installed, then choose Check again."}
        saveState()
    }
    func installApp() {
        guard verified,!busy else{return}
        guard !NSWorkspace.shared.runningApplications.contains(where:{$0.bundleIdentifier=="local.audiobridge.desktop"}) else{message="Quit San Audisco from its app menu before updating. Your audio will stay running until you quit.";return}
        busy=true;message="Installing San Audisco. Your rooms and audio settings will be kept."
        let resources=resources,destination=destination,state=state
        Task {
            let error=await Task.detached{()->String? in do{try SetupSystem.installApp(resources,destination,state);return nil}catch{return error.localizedDescription}}.value
            let result=await Task.detached{SetupSystem.inventory(destination,resources)}.value
            apply(result);busy=false
            if let error=error{message=error}else{describeNext()};saveState()
        }
    }
    func installerFinished() {
        guard installerOpen,!NSWorkspace.shared.runningApplications.contains(where:{$0.bundleIdentifier=="com.apple.installer"}) else{return}
        installerOpen=false;check()
    }
}
struct SetupView:View {
    @ObservedObject var model:SetupModel
    @State private var details=false
    private func badge(_ ready:Bool,_ present:Bool)->String {if !model.started{return "Not checked"};if model.busy{return "Checking…"};if model.restartPending&&present{return "Restart required"};return ready ? "Ready":present ? "Needs attention":"Missing"}
    private func card(_ title:String,_ subtitle:String,_ symbol:String,_ status:String)->some View {
        HStack(spacing:14){Image(systemName:symbol).font(.title2).foregroundColor(.secondary).frame(width:28);VStack(alignment:.leading,spacing:4){Text(title).font(.headline);Text(subtitle).font(.caption).foregroundColor(.secondary)};Spacer();Label(status,systemImage:status=="Ready" ? "checkmark.circle.fill":status=="Missing" ? "circle.dashed":"info.circle").font(.callout).foregroundColor(status=="Ready" ? .green:.secondary)}.padding(16).background(Color.primary.opacity(0.035),in:RoundedRectangle(cornerRadius:12)).accessibilityElement(children:.combine)
    }
    var body:some View {
        VStack(alignment:.leading,spacing:22){
            HStack(spacing:16){if let icon=NSImage(contentsOf:model.resources.appendingPathComponent("SanAudisco.icns")){Image(nsImage:icon).resizable().frame(width:72,height:72).clipShape(RoundedRectangle(cornerRadius:14)).accessibilityHidden(true)};VStack(alignment:.leading,spacing:5){Text("San Audisco").font(.largeTitle).fontWeight(.semibold);Text("One place for your computers’ audio.").foregroundColor(.secondary)}}
            HStack{ForEach(Array(["Welcome","Check","Set up","Ready"].enumerated()),id:\.offset){index,label in HStack(spacing:6){Image(systemName:index<model.phase ? "checkmark.circle.fill":"\(index+1).circle\(index==model.phase ? ".fill":"")");Text(label)}.font(.callout).foregroundColor(index==model.phase ? .accentColor:.secondary);if index<3{Spacer();Image(systemName:"chevron.right").font(.caption).foregroundColor(.secondary);Spacer()}}}.accessibilityElement(children:.ignore).accessibilityLabel("Setup step \(model.phase+1) of 4")
            ScrollView{VStack(alignment:.leading,spacing:12){
                if !model.started {Text("Let’s get this Mac ready").font(.title2).fontWeight(.semibold);Text("San Audisco combines audio from two computers and shares your chosen microphone. Existing compatible audio components are kept.").foregroundColor(.secondary).fixedSize(horizontal:false,vertical:true).padding(.bottom,8)}
                card("San Audisco app","Your rooms and settings stay with you.","app.connected.to.app.below.fill",!model.started ? "Not checked":model.busy ? "Checking…":model.inventory.appCurrent ? "Ready":model.inventory.appPresent ? "Update available":"Missing")
                card("Computer audio","Brings this computer’s sound into the room.","speaker.wave.2",badge(model.inventory.first.ready,model.inventory.first.present))
                card("Shared microphone","Makes your chosen microphone available to voice apps.","mic",badge(model.inventory.second.ready,model.inventory.second.present))
                DisclosureGroup("Details",isExpanded:$details){VStack(alignment:.leading,spacing:10){Text(model.inventory.first.detail);Text(model.inventory.second.detail);Text("App location: \(model.destination.path)");Text("BlackHole by Existential Audio. Original vendor packages and notices are included unchanged. Installer and microphone permission prompts remain necessary.");HStack{Button("Audio MIDI Setup"){NSWorkspace.shared.open(URL(fileURLWithPath:"/System/Applications/Utilities/Audio MIDI Setup.app"))};Button("Setup log"){NSWorkspace.shared.activateFileViewerSelecting([model.state])}}}.font(.caption).foregroundColor(.secondary).textSelection(.enabled).padding(.top,8)}.padding(.top,8)
            }}
            Divider()
            HStack(alignment:.top,spacing:12){if model.busy{ProgressView().controlSize(.small)}else{Image(systemName:model.next==6 && model.started ? "checkmark.circle.fill":"info.circle").foregroundColor(model.next==6 && model.started ? .green:.secondary)};Text(model.message).font(.callout).fixedSize(horizontal:false,vertical:true).textSelection(.enabled);Spacer()}
            HStack{Text("Setup never restarts your Mac.").font(.caption).foregroundColor(.secondary);Spacer();if model.started && (model.installerOpen || (model.next>0 && model.next<4) || model.next==6){Button("Check again"){model.check()}.disabled(model.busy)};Button(model.primaryTitle){model.primary()}.buttonStyle(.borderedProminent).disabled(model.busy || model.installerOpen).keyboardShortcut(.defaultAction)}
        }.padding(28).frame(minWidth:650,idealWidth:700,minHeight:650,idealHeight:720)
        .onReceive(NSWorkspace.shared.notificationCenter.publisher(for:NSWorkspace.didTerminateApplicationNotification)){_ in model.installerFinished()}
    }
}
@MainActor final class SetupDelegate:NSObject,NSApplicationDelegate {
    weak var model:SetupModel?
    func applicationShouldTerminate(_ sender:NSApplication)->NSApplication.TerminateReply {
        guard model?.busy==true else{return .terminateNow}
        let alert=NSAlert();alert.messageText="Setup is working";alert.informativeText="Wait for the current check or installation to finish before quitting.";alert.addButton(withTitle:"OK");alert.runModal();return .terminateCancel
    }
}
@main struct SetupApp:App {
    @NSApplicationDelegateAdaptor(SetupDelegate.self) private var delegate
    @StateObject private var model=SetupModel()
    var body:some Scene {Window("San Audisco Setup",id:"setup"){SetupView(model:model).onAppear{delegate.model=model}}}
}
