import AppKit
import SwiftUI
import CryptoKit
import CoreAudio

@_silgen_name("ab_setup_action") private func setupAction(_ ready:Int32,_ installed:Int32,_ restart:Int32) -> Int32
@_silgen_name("ab_setup_installer_result") private func setupResult(_ result:Int32) -> Int32

struct PayloadFile: Decodable { let path: String; let sha256: String }
struct SetupManifest: Decodable { let files: [PayloadFile] }

@MainActor final class SetupModel: ObservableObject {
    @Published var rows = [String]()
    @Published var message = "Check the components, then install only what is missing."
    @Published var busy = false
    @Published var verified = false
    @Published var ready2 = false
    @Published var ready16 = false
    @Published var installedApp = false
    @Published var restartPending = false
    private var awaitingComponent:Int?
    private var installationBoot = ""
    private var awaitingBoot = ""
    private var loadedState = false
    let resources = Bundle.main.resourceURL!
    let destination = FileManager.default.homeDirectoryForCurrentUser.appendingPathComponent("Applications/San Audisco.app")
    let state = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0].appendingPathComponent("AudioBridge/setup-v2.json")

    func run(_ path: String, _ args: [String]) throws -> (Int32, String) {
        let task = Process(); task.executableURL = URL(fileURLWithPath:path); task.arguments = args
        let pipe = Pipe(); task.standardOutput = pipe; task.standardError = pipe
        try task.run(); let data = pipe.fileHandleForReading.readDataToEndOfFile(); task.waitUntilExit()
        return (task.terminationStatus, String(decoding:data,as:UTF8.self))
    }
    func verify() {
        busy = true
        do {
            let manifest = try JSONDecoder().decode(SetupManifest.self, from: Data(contentsOf: resources.appendingPathComponent("manifest.json")))
            for file in manifest.files {
                guard !file.path.hasPrefix("/"), !file.path.split(separator:"/").contains("..") else { throw NSError(domain:"Unsafe package path",code:1) }
                let data = try Data(contentsOf:resources.appendingPathComponent(file.path))
                let digest = SHA256.hash(data:data).map{String(format:"%02x",$0)}.joined()
                guard digest == file.sha256 else { throw NSError(domain:"Package integrity check failed: \(file.path)",code:1) }
            }
            for file in ["BlackHole2ch-0.7.1.pkg","BlackHole16ch-0.7.1.pkg"] {
                let result = try run("/usr/sbin/pkgutil",["--check-signature",resources.appendingPathComponent("drivers/"+file).path])
                guard result.0 == 0, result.1.contains("Q5C99V536K"), result.1.contains("signed by a developer certificate issued by Apple") else { throw NSError(domain:"Vendor signature check failed: \(file)",code:1) }
            }
            let app = resources.appendingPathComponent("San Audisco.app")
            guard try run("/usr/bin/codesign",["--verify","--deep","--strict",app.path]).0 == 0 else { throw NSError(domain:"San Audisco app signature check failed",code:1) }
            verified = true; message = "Package integrity verified. Existing compatible drivers will be kept."
        } catch { verified = false; message = error.localizedDescription }
        busy = false; refresh()
    }
    func active(_ uid:String) -> Bool {
        var address = AudioObjectPropertyAddress(mSelector:kAudioHardwarePropertyDevices,mScope:kAudioObjectPropertyScopeGlobal,mElement:kAudioObjectPropertyElementMain)
        var size:UInt32 = 0
        guard AudioObjectGetPropertyDataSize(AudioObjectID(kAudioObjectSystemObject),&address,0,nil,&size) == noErr else {return false}
        var devices = [AudioObjectID](repeating:0,count:Int(size)/MemoryLayout<AudioObjectID>.size)
        guard AudioObjectGetPropertyData(AudioObjectID(kAudioObjectSystemObject),&address,0,nil,&size,&devices) == noErr else {return false}
        for device in devices {
            var property = AudioObjectPropertyAddress(mSelector:kAudioDevicePropertyDeviceUID,mScope:kAudioObjectPropertyScopeGlobal,mElement:kAudioObjectPropertyElementMain)
            var value:Unmanaged<CFString>?; var bytes = UInt32(MemoryLayout<Unmanaged<CFString>?>.size)
            if AudioObjectGetPropertyData(device,&property,0,nil,&bytes,&value) == noErr, value?.takeRetainedValue() as String? == uid { return true }
        }
        return false
    }
    func inventory(_ channels:Int) -> (Bool,String) {
        let path = "/Library/Audio/Plug-Ins/HAL/BlackHole\(channels)ch.driver/Contents/Info.plist"
        guard let info = NSDictionary(contentsOfFile:path), let version = info["CFBundleShortVersionString"] as? String else { return (false,"BlackHole \(channels)ch: not installed") }
        guard version.compare("0.7.1", options:.numeric) != .orderedAscending else { return (false,"BlackHole \(channels)ch: older version \(version); review before replacing") }
        let present = active("BlackHole\(channels)ch_UID")
        return (present,"BlackHole \(channels)ch \(version): " + (present ? "ready — keep installed component" : "installed; restart may be needed to activate audio"))
    }
    func refresh() {
        let currentBoot = (try? run("/usr/sbin/sysctl",["-n","kern.boottime"]).1) ?? "unknown"
        if !loadedState {
            if let data = try? Data(contentsOf:state), let old = try? JSONSerialization.jsonObject(with:data) as? [String:Any] {installationBoot = old["installation_boot"] as? String ?? "";let oldComponent = old["awaiting_component"] as? Int;awaitingComponent = (oldComponent == 2 || oldComponent == 16) ? oldComponent : nil;awaitingBoot = old["awaiting_boot"] as? String ?? ""}
            loadedState = true
        }
        if let channels = awaitingComponent, FileManager.default.fileExists(atPath:"/Library/Audio/Plug-Ins/HAL/BlackHole\(channels)ch.driver") {installationBoot = awaitingBoot.isEmpty ? currentBoot : awaitingBoot;awaitingComponent = nil;awaitingBoot = ""}
        if !installationBoot.isEmpty && currentBoot != "unknown" && installationBoot != currentBoot {installationBoot = ""}
        restartPending = !installationBoot.isEmpty
        let two = inventory(2), sixteen = inventory(16)
        ready2 = two.0; ready16 = sixteen.0
        installedApp = ["SanAudisco","bridge-engine","bridge-agent"].allSatisfy{FileManager.default.isExecutableFile(atPath:destination.appendingPathComponent("Contents/MacOS/"+$0).path)}
        rows = [two.1,sixteen.1,installedApp ? "San Audisco: installed in ~/Applications" : "San Audisco: ready to install in ~/Applications"]
        if restartPending {rows.append("A driver was installed this boot. Restart when convenient, then reopen setup.")}
        saveState()
    }
    func saveState() {
        do {
            try FileManager.default.createDirectory(at:state.deletingLastPathComponent(),withIntermediateDirectories:true)
            let object:[String:Any] = ["schema":1,"checked_at":ISO8601DateFormatter().string(from:Date()),"blackhole2_ready":ready2,"blackhole16_ready":ready16,"app_installed":installedApp,"status":message,"automatic_restart":false,"restart_pending":restartPending,"installation_boot":installationBoot,"awaiting_component":awaitingComponent ?? 0,"awaiting_boot":awaitingBoot]
            try JSONSerialization.data(withJSONObject:object,options:.prettyPrinted).write(to:state,options:.atomic)
        } catch { message += " Could not save setup progress: \(error.localizedDescription)" }
    }
    func installDriver(_ channels:Int) {
        guard verified else {return}
        refresh()
        if NSWorkspace.shared.runningApplications.contains(where:{$0.bundleIdentifier == "com.apple.installer"}) {message = "Finish or cancel the open Apple Installer before starting the next component.";saveState();return}
        let existing = "/Library/Audio/Plug-Ins/HAL/BlackHole\(channels)ch.driver"
        let action = setupAction((channels == 2 ? ready2 : ready16) ? 1 : 0, FileManager.default.fileExists(atPath:existing) ? 1 : 0, 0)
        if action == 0 {refresh();return}
        if action != 1 {
            message = "BlackHole \(channels)ch already exists. Setup will not replace it automatically. Check again after a restart, or review its version in Audio MIDI Setup.";saveState();return
        }
        awaitingComponent = channels
        awaitingBoot = (try? run("/usr/sbin/sysctl",["-n","kern.boottime"]).1) ?? "unknown"
        let file = resources.appendingPathComponent("drivers/BlackHole\(channels)ch-0.7.1.pkg")
        message = "The original BlackHole installer is opening. Follow its prompts; defer any restart until both components are installed. Then choose Check again."
        saveState()
        if setupResult(NSWorkspace.shared.open(file) ? 0 : 2) == 2 {awaitingComponent = nil;message = "Could not open Apple Installer. No installation was confirmed.";saveState()}
    }
    func installApp() {
        guard verified else {return}
        busy = true
        do {
            let running = NSWorkspace.shared.runningApplications.contains{ $0.bundleIdentifier == "local.audiobridge.desktop" }
            let agent = try run("/usr/bin/pgrep",["-x","bridge-agent"]).0 == 0
            guard !running && !agent else { throw NSError(domain:"Quit San Audisco and its background agent before updating. Setup will not stop live audio.",code:1) }
            let fm = FileManager.default
            try fm.createDirectory(at:destination.deletingLastPathComponent(),withIntermediateDirectories:true)
            let temporary = destination.deletingLastPathComponent().appendingPathComponent("San Audisco-install-"+UUID().uuidString+".app")
            try fm.copyItem(at:resources.appendingPathComponent("San Audisco.app"),to:temporary)
            defer {try? fm.removeItem(at:temporary)}
            guard try run("/usr/bin/codesign",["--verify","--deep","--strict",temporary.path]).0 == 0 else {throw NSError(domain:"Copied app failed signature verification; existing app retained.",code:1)}
            if fm.fileExists(atPath:destination.path) {
                let backups = state.deletingLastPathComponent().appendingPathComponent("app-backups")
                try fm.createDirectory(at:backups,withIntermediateDirectories:true)
                let backup = backups.appendingPathComponent("San Audisco-"+UUID().uuidString+".app")
                try fm.moveItem(at:destination,to:backup)
                do {try fm.moveItem(at:temporary,to:destination)} catch {
                    let installError = error
                    do {try fm.moveItem(at:backup,to:destination)} catch {throw NSError(domain:"Update failed and the previous app could not be restored automatically. It remains at \(backup.path). Restore it before launching San Audisco.",code:1)}
                    throw installError
                }
            } else {try fm.moveItem(at:temporary,to:destination)}
            let legacy = destination.deletingLastPathComponent().appendingPathComponent("AudioBridge.app")
            if let bundle = Bundle(url:legacy), bundle.bundleIdentifier == "local.audiobridge.desktop" {
                try fm.trashItem(at:legacy,resultingItemURL:nil)
            }
            message = "San Audisco installed. Open it when both audio components are ready. Existing settings and drivers were retained."
        } catch {message = error.localizedDescription}
        busy = false;refresh()
    }
}
struct SetupView:View {
    @StateObject var model = SetupModel()
    var body:some View {
        VStack(alignment:.leading,spacing:18) {
            Text("San Audisco Setup").font(.largeTitle)
            Text("Private build · macOS · Existing audio drivers are preserved")
            ForEach(model.rows,id:\.self){Text($0)}
            HStack {
                Button("Install BlackHole 2ch"){model.installDriver(2)}.disabled(!model.verified || model.ready2 || model.busy)
                Button("Install BlackHole 16ch"){model.installDriver(16)}.disabled(!model.verified || model.ready16 || model.busy)
            }
            HStack {
                Button("Install / update San Audisco"){model.installApp()}.disabled(!model.verified || model.busy)
                Button("Check again"){model.refresh()}.disabled(model.busy)
                Button("Open San Audisco"){NSWorkspace.shared.open(model.destination)}.disabled(!model.installedApp || !model.ready2 || !model.ready16 || model.restartPending || model.busy)
            }
            Text(model.message).textSelection(.enabled).fixedSize(horizontal:false,vertical:true)
            Text("BlackHole by Existential Audio. Vendor packages and notices are included unchanged. OS installer and microphone permission prompts remain necessary. Setup never restarts your Mac.").font(.caption).foregroundColor(.secondary)
            Button("Open setup state"){NSWorkspace.shared.activateFileViewerSelecting([model.state])}
        }.padding(28).frame(width:660).onAppear{model.verify()}
    }
}
@main struct SetupApp:App {var body:some Scene {Window("San Audisco Setup",id:"setup"){SetupView()}}}
