import SwiftUI
import AppKit
import AVFoundation
import ServiceManagement
import Darwin

struct AudioEndpoint: Identifiable {
 let id: String; let name: String; let inputs: Int; let outputs: Int
 init(_ value: [String: Any]) {
  id=value["id"] as? String ?? ""; name=value["name"] as? String ?? id
  inputs=(value["inputs"] as? NSNumber)?.intValue ?? 0
  outputs=(value["outputs"] as? NSNumber)?.intValue ?? 0
 }
}
struct RoomDevice: Identifiable {
 let id: String; let name: String; let platform: String; let online: Bool
 let audio: [AudioEndpoint]; let headphones: String; let microphone: String
 let playback: String; let micOutput: String; let setup: String; let diagnostics:[String:Any]
 init(_ value: [String: Any]) {
  id=value["id"] as? String ?? ""; name=value["name"] as? String ?? id
  platform=value["platform"] as? String ?? ""; online=value["online"] as? Bool ?? false
  audio=(value["audio_devices"] as? [[String:Any]] ?? []).map(AudioEndpoint.init)
  headphones=value["headphones_output"] as? String ?? ""; microphone=value["physical_microphone"] as? String ?? ""
  playback=value["playback_input"] as? String ?? ""; micOutput=value["microphone_output"] as? String ?? ""
  setup=value["setup_status"] as? String ?? "Checking audio components…"
  diagnostics=value["diagnostics"] as? [String:Any] ?? [:]
 }
 var roleValue: Int { platform.lowercased().hasPrefix("win") ? 0 : 1 }
}
struct NearbyRoom: Identifiable {
 let id: String; let name: String; let address: String
 init(_ value: [String:Any]) {
  id=value["id"] as? String ?? value["address"] as? String ?? UUID().uuidString
  name=value["name"] as? String ?? "Nearby room"; address=value["address"] as? String ?? ""
 }
}

// The GUI is only a client. The per-user agent owns audio and survives window close.
final class BridgeModel: ObservableObject {
 @Published var devices: [RoomDevice]=[]
 @Published var nearby: [NearbyRoom]=[]
 @Published var localID=""
 @Published var roomID=""
 @Published var deviceName=""
 @Published var invite=""
 @Published var expires=0
 @Published var joinCode=""
 @Published var joinAddress=""
 var selectedRoomID=""
 var selectedRoomAddress=""
 @Published var paired=false
 @Published var connected=false
 @Published var running=false
 @Published var wantedRunning=false
 @Published var available=false
 @Published var busy=false
 @Published var permissionPending=false
 @Published var status="Connecting to AudioBridge…"
 @Published var error=""
 @Published var muteReason=""
 @Published var diagnosticMessage=""
 @Published var diagnostics: [String:Any]=[:]
 @Published var settings: [String:Double]=["primary":0,"mic_source":1,"mute":1,"deafen":0,"master":1,"balance":0,"win_volume":1,"mac_volume":1]
 @Published var startAtLogin=false
 private let requests=DispatchQueue(label:"AudioBridge.agent.requests")
 private var timer: Timer?
 private var polling=false
 private var permissionRequestID: UUID?
 private var pendingControls: [String:DispatchWorkItem]=[:]
 var executable: URL { Bundle.main.bundleURL.appendingPathComponent("Contents/MacOS/bridge-agent") }

 func open() {
  guard timer == nil else {return}
  reconnectAgent()
  startAtLogin=SMAppService.mainApp.status == .enabled
  timer=Timer.scheduledTimer(withTimeInterval:1.0,repeats:true) {[weak self] _ in self?.poll()}
 }
 func reconnectAgent() {
  busy=true;error=""
  requests.async { [weak self] in
   guard let self=self else{return}
   do {let result=try Self.invoke(self.executable,arguments:["ensure"],request:nil)
    if result["ok"] as? Bool == false {throw NSError(domain:"AudioBridge",code:6,userInfo:[NSLocalizedDescriptionKey:result["error"] as? String ?? "Background agent could not start."])}
    let response=try Self.invoke(self.executable,arguments:["request"],request:["command":"status"])
    DispatchQueue.main.async {self.busy=false;if let state=response["state"] as? [String:Any]{self.apply(state)};self.available=response["ok"] as? Bool == true;if !self.available{self.error=response["error"] as? String ?? "Background agent unavailable"}}
   } catch {DispatchQueue.main.async {self.busy=false;self.available=false;self.error=error.localizedDescription;self.status="Background agent unavailable"}}
  }
 }
 deinit {timer?.invalidate()}

 private static func invoke(_ executable:URL,arguments:[String],request:[String:Any]?) throws -> [String:Any] {
  let p=Process(); p.executableURL=executable;p.arguments=arguments
  let input=Pipe(),output=Pipe();p.standardInput=input;p.standardOutput=output;p.standardError=FileHandle.nullDevice
  let lock=NSLock();var bytes=Data();var oversized=false
  let drained=DispatchSemaphore(value:0)
  DispatchQueue.global(qos:.utility).async {
   defer {drained.signal()}
   while let data=try? output.fileHandleForReading.read(upToCount:8192),!data.isEmpty {
    lock.lock();if bytes.count+data.count<=1_048_576 {bytes.append(data)}else{oversized=true};lock.unlock()
   }
  }
  let finished=DispatchSemaphore(value:0);p.terminationHandler={_ in finished.signal()}
  do {try p.run()}catch{try? output.fileHandleForWriting.close();throw error}
  try? output.fileHandleForWriting.close()
  if let request=request {var data=try JSONSerialization.data(withJSONObject:request);guard data.count<=60_000 else{p.terminate();throw NSError(domain:"AudioBridge",code:5,userInfo:[NSLocalizedDescriptionKey:"The requested change is too large."])};data.append(10);try input.fileHandleForWriting.write(contentsOf:data)}
  try? input.fileHandleForWriting.close()
  if finished.wait(timeout:.now()+15) == .timedOut {
   p.terminate();if finished.wait(timeout:.now()+1) == .timedOut {kill(p.processIdentifier,SIGKILL);_ = finished.wait(timeout:.now()+1)}
   throw NSError(domain:"AudioBridge",code:1,userInfo:[NSLocalizedDescriptionKey:"AudioBridge did not respond. Open the app again to reconnect; audio may still be running."])
  }
  guard drained.wait(timeout:.now()+2) == .success else {throw NSError(domain:"AudioBridge",code:4,userInfo:[NSLocalizedDescriptionKey:"The agent response stream did not close."])}
  lock.lock();let result=bytes;let tooLarge=oversized;lock.unlock()
  guard !tooLarge else {throw NSError(domain:"AudioBridge",code:2,userInfo:[NSLocalizedDescriptionKey:"AudioBridge returned an oversized response."])}
  if arguments.first == "ensure" && p.terminationStatus==0 {return [:]}
  guard let object=(try? JSONSerialization.jsonObject(with:result)) as? [String:Any] else {
   throw NSError(domain:"AudioBridge",code:3,userInfo:[NSLocalizedDescriptionKey:"Could not read the background agent response."])
  }
  return object
 }
 func send(_ request:[String:Any],quiet:Bool=false,completion:(()->Void)?=nil) {
  if !quiet {busy=true;error=""}
  requests.async { [weak self] in
   guard let self=self else{return}
   do {
    let response=try Self.invoke(self.executable,arguments:["request"],request:request)
    DispatchQueue.main.async {
     self.polling=false;if !quiet {self.busy=false}
     if let state=response["state"] as? [String:Any] {self.apply(state)}
     if response["ok"] as? Bool == false {self.error=response["error"] as? String ?? "AudioBridge could not apply the change."}
     else {self.available=true;completion?()}
    }
   }catch {DispatchQueue.main.async{self.polling=false;if !quiet{self.busy=false};self.available=false;self.error=error.localizedDescription}}
  }
 }
 func poll() {guard !polling && !busy else{return};if available && !NSApp.windows.contains(where:{$0.isVisible && $0.canBecomeMain}){return};polling=true;send(["command":"status"],quiet:true)}
 func apply(_ state:[String:Any]) {
  localID=state["device_id"] as? String ?? localID
  if deviceName.isEmpty {deviceName=state["name"] as? String ?? Host.current().localizedName ?? "Mac"}
  roomID=state["room_id"] as? String ?? "";invite=state["invite_code"] as? String ?? ""
  expires=(state["invite_expires_seconds"] as? NSNumber)?.intValue ?? 0
  paired=state["paired"] as? Bool ?? false;connected=state["connected"] as? Bool ?? false;running=state["running"] as? Bool ?? false;wantedRunning=state["wanted_running"] as? Bool ?? running
  status=state["status"] as? String ?? (connected ? "Room connected" : "Waiting for the other computer")
  muteReason=state["mute_reason"] as? String ?? ""
  devices=(state["devices"] as? [[String:Any]] ?? []).map(RoomDevice.init)
  nearby=(state["discovered"] as? [[String:Any]] ?? []).map(NearbyRoom.init)
  if let incoming=state["settings"] as? [String:Any] {
   for (key,value) in incoming {if pendingControls[key] == nil,let number=value as? NSNumber{settings[key]=number.doubleValue}}
  }
  diagnostics=state["diagnostics"] as? [String:Any] ?? [:]
  diagnosticMessage=diagnostics["message"] as? String ?? ""
 }
 func control(_ key:String,_ value:Double,debounce:Bool=false) {
  settings[key]=value
  pendingControls[key]?.cancel()
  if debounce {
   let work=DispatchWorkItem{[weak self] in guard let self=self else{return};self.pendingControls[key]=nil;self.send(["command":"control","key":key,"value":value])}
   pendingControls[key]=work;DispatchQueue.main.asyncAfter(deadline:.now()+0.15,execute:work)
  } else {pendingControls[key]=nil;send(["command":"control","key":key,"value":value])}
 }
 func number(_ key:String,default fallback:Double=0)->Double {settings[key] ?? fallback}
 func scalar(_ key:String,default fallback:Double=0)->Binding<Double> {Binding(get:{self.number(key,default:fallback)},set:{self.control(key,$0,debounce:true)})}
 func toggle(_ key:String)->Binding<Bool> {Binding(get:{self.number(key) != 0},set:{self.control(key,$0 ? 1:0)})}
 func role(_ key:String)->Binding<Int> {Binding(get:{Int(self.number(key))},set:{self.control(key,Double($0))})}
 var formattedInvite:String {invite.count==8 ? String(invite.prefix(4))+"-"+String(invite.suffix(4)) : invite}
 func setLogin(_ enabled:Bool) {do{if enabled{try SMAppService.mainApp.register()}else{try SMAppService.mainApp.unregister()};startAtLogin=SMAppService.mainApp.status == .enabled;if enabled && !startAtLogin{error="Approve AudioBridge in System Settings → General → Login Items."}}catch{self.error=error.localizedDescription;startAtLogin=SMAppService.mainApp.status == .enabled}}
 func level(_ device:RoomDevice,_ key:String)->Double {let source=device.id==localID ? diagnostics : device.diagnostics;let value=(source[key] as? NSNumber)?.doubleValue ?? 0;return value.isFinite ? min(1,max(0,value)):0}
 func create(){send(["command":"create","name":deviceName])}
 func join(){let code=joinCode.uppercased().filter{$0 != "-" && !$0.isWhitespace};guard code.count==8 else{error="Enter the eight letters and numbers shown on the other computer.";return};let address=joinAddress.trimmingCharacters(in:.whitespacesAndNewlines);var request:[String:Any]=["command":"join","code":code,"address":address];if !selectedRoomID.isEmpty && address==selectedRoomAddress{request["room_id"]=selectedRoomID};send(request)}
 func start() {
  guard !permissionPending else{return};let requestID=UUID();permissionRequestID=requestID
  switch AVCaptureDevice.authorizationStatus(for:.audio) {
  case .authorized: finishPermission(true,id:requestID)
  case .notDetermined:
   permissionPending=true;error="";status="Waiting for macOS microphone permission…"
   AVCaptureDevice.requestAccess(for:.audio){[weak self] granted in DispatchQueue.main.async{self?.finishPermission(granted,id:requestID)}}
  case .denied,.restricted:
   permissionRequestID=nil;error="Allow AudioBridge in System Settings → Privacy & Security → Microphone, then start audio again. Virtual cable capture also needs this permission."
  @unknown default:permissionRequestID=nil;error="Microphone permission is unavailable on this Mac."
  }
 }
 private func finishPermission(_ granted:Bool,id:UUID){guard permissionRequestID==id else{return};permissionRequestID=nil;permissionPending=false;guard granted else{error="Microphone access was not granted. Enable AudioBridge in System Settings → Privacy & Security → Microphone.";return};send(["command":"start"])}
 func cancelPermission(){permissionRequestID=nil;permissionPending=false;status="Start cancelled"}
 func quit(){cancelPermission();send(["command":"quit"],completion:{NSApp.terminate(nil)})}
}

struct DeviceRouteView:View {
 @ObservedObject var model:BridgeModel
 let device:RoomDevice
 @State private var headphones=""
 @State private var microphone=""
 @State private var playback=""
 @State private var micOutput=""
 @State private var dirty=false
 private func select(_ title:String,_ value:Binding<String>,input:Bool)->some View {
  Picker(title,selection:value){Text("Not selected").tag("");ForEach(device.audio.filter{input ? $0.inputs>0 : $0.outputs>0}){Text($0.name).tag($0.id)}}
 }
 private func edit(_ value:Binding<String>)->Binding<String>{Binding(get:{value.wrappedValue},set:{value.wrappedValue=$0;dirty=true})}
 private func load(){headphones=device.headphones;microphone=device.microphone;playback=device.playback;micOutput=device.micOutput}
 var body:some View {GroupBox{
  VStack(alignment:.leading,spacing:10){
   HStack{Text(device.name).font(.headline);Text(device.id==model.localID ? "This Mac" : device.platform).foregroundColor(.secondary);Spacer();Text(device.online ? "Online" : "Offline").foregroundColor(device.online ? .green : .secondary)}
   Text(device.setup).font(.caption)
   select("Headphones",edit($headphones),input:false)
   select("Physical microphone",edit($microphone),input:true)
   DisclosureGroup("Advanced audio routes"){
    select("Playback cable capture",edit($playback),input:true)
    select("Shared microphone output",edit($micOutput),input:false)
   }
   HStack{Text(device.roleValue==Int(model.number("primary")) ? "Hub · headphones connected here" : "Satellite · sends audio to Hub").font(.caption).foregroundColor(.secondary);Spacer();Button("Apply devices"){model.send(["command":"configure","device_id":device.id,"headphones_output":headphones,"physical_microphone":microphone,"playback_input":playback,"microphone_output":micOutput]);dirty=false}.disabled(!dirty || !device.online || model.busy)}
  }.padding(8)
 }.onAppear{load()}.onChange(of:device.headphones+device.microphone+device.playback+device.micOutput){_ in if !dirty{load()}}}
}

struct ContentView:View {
 @EnvironmentObject var m:BridgeModel
 @State private var page=0
 @State private var forget=false
 private var controlsDisabled:Bool {!m.connected || m.busy}
 private func mixRow(_ label:String,prefix:String)->some View {
  HStack{Text(label).frame(width:140,alignment:.leading);Slider(value:m.scalar(prefix+"_volume",default:1),in:0...1);Text("\(Int(m.number(prefix+"_volume",default:1)*100))%").monospacedDigit().frame(width:42);Toggle("Mute mic",isOn:m.toggle(prefix+"_mute"));Toggle("Deafen",isOn:m.toggle(prefix+"_deafen"))}
 }
 private func platformName(_ prefix:String)->String {m.devices.first{$0.roleValue==(prefix=="win" ? 0:1)}?.name ?? (prefix=="win" ? "Windows":"Mac")}
 var body:some View {VStack(alignment:.leading,spacing:16){
  HStack{Text("AudioBridge").font(.largeTitle).bold();Spacer();Circle().fill(m.connected ? Color.green:Color.gray).frame(width:9,height:9);Text(m.connected ? "Connected":(m.paired ? "Reconnecting":"Not paired"))}
  Picker("View",selection:$page){Text("Room").tag(0);Text("Devices").tag(1);Text("Mixer").tag(2);Text("Checks").tag(3)}.pickerStyle(.segmented)
  ScrollView{VStack(alignment:.leading,spacing:16){
   if page==0 {
    GroupBox("This computer") {HStack{TextField("Device name",text:$m.deviceName);Button("Save name"){m.send(["command":"configure","device_id":m.localID,"name":m.deviceName])}.disabled(m.busy || m.localID.isEmpty)}.padding(8)}
    if !m.paired {
     GroupBox("Connect your two computers") {VStack(alignment:.leading,spacing:12){
      Text("Use the same local network. Create a room here, or enter the code from your other computer.")
      Button("Create room"){m.create()}.disabled(m.busy || !m.available)
      if !m.invite.isEmpty {HStack{Text(m.formattedInvite).font(.system(size:30,weight:.semibold,design:.monospaced)).textSelection(.enabled);Button("Copy code"){NSPasteboard.general.clearContents();NSPasteboard.general.setString(m.formattedInvite,forType:.string)}};Text("Invitation expires in \(m.expires) seconds. It can pair one computer.").font(.caption);Button("Cancel invitation"){m.send(["command":"forget"])}.disabled(m.busy)}
      Divider()
      HStack{TextField("Room code, e.g. K7MP-4X9R",text:$m.joinCode);Button("Join room"){m.join()}.disabled(m.busy || !m.available)}
      DisclosureGroup("Nearby rooms and manual connection"){
       HStack{TextField("Other computer’s address (optional)",text:$m.joinAddress);Button("Find rooms"){m.send(["command":"discover"])}}
       ForEach(m.nearby){room in Button("\(room.name) · \(room.address)"){m.joinAddress=room.address;m.selectedRoomID=room.id;m.selectedRoomAddress=room.address}.buttonStyle(.link)}
       Text("Discovery does not share the invitation code. If your network blocks discovery, enter the other computer’s address.").font(.caption).foregroundColor(.secondary)
      }
     }.padding(8)}
    } else {
     GroupBox("Your room"){VStack(alignment:.leading,spacing:10){ForEach(m.devices){device in HStack{Text(device.name);Spacer();Text(device.roleValue==Int(m.number("primary")) ? "Hub":"Satellite");Text(device.online ? "Online":"Offline").foregroundColor(.secondary)}};Text("Both apps control this room. Your pairing is remembered.").font(.caption);Button("Forget this pairing…",role:.destructive){forget=true}.disabled(m.busy)}.padding(8)}
    }
    Toggle("Start AudioBridge at login",isOn:Binding(get:{m.startAtLogin},set:{m.setLogin($0)})).disabled(m.busy || !m.available)
    Text("Closing this window keeps audio running. Use Quit AudioBridge to stop the background agent.").font(.caption).foregroundColor(.secondary)
   } else if page==1 {
    HStack{Text("Choose devices on either computer from here.");Spacer();Button("Refresh devices"){m.send(["command":"diagnose"])}}
    ForEach(m.devices){DeviceRouteView(model:m,device:$0)}
    Text("Connect AirPods to the Hub in Bluetooth settings. Changing the Hub here does not move their Bluetooth connection.").font(.caption).foregroundColor(.secondary)
   } else if page==2 {
    HStack{Picker("Hub",selection:m.role("primary")){Text(platformName("win")).tag(0);Text(platformName("mac")).tag(1)};Picker("Mic source",selection:m.role("mic_source")){Text(platformName("win")).tag(0);Text(platformName("mac")).tag(1)}}.disabled(controlsDisabled)
    GroupBox("Mix") {VStack(spacing:16){mixRow(platformName("win"),prefix:"win");mixRow(platformName("mac"),prefix:"mac");HStack{Text(platformName("win"));Slider(value:m.scalar("balance"),in:-1...1);Text(platformName("mac"))};HStack{Text("Overall volume");Slider(value:m.scalar("master",default:1),in:0...1)}}.padding(8)}.disabled(controlsDisabled)
    HStack{Toggle("Mute microphone everywhere",isOn:m.toggle("mute"));Spacer();Toggle("Deafen both",isOn:m.toggle("deafen"))}.disabled(controlsDisabled)
    if m.number("mute",default:1) == 0 && (m.diagnostics["muted"] as? Bool == true) {Text("Microphone is blocked by audio readiness. Open Checks and resolve the inactive route before speaking.").foregroundColor(.orange)}
    if m.number("mute",default:1) != 0 {Text(m.muteReason.isEmpty ? "Microphone is muted. Unmute when you are ready to speak.":m.muteReason).foregroundColor(.orange);Button("Unmute microphone"){m.control("mute",0)}.disabled(controlsDisabled)}
   } else {
    HStack{Button("Check setup"){m.send(["command":"diagnose"])};Button("Reconnect background agent"){m.reconnectAgent()}}.disabled(m.busy)
    if !m.running && AVCaptureDevice.authorizationStatus(for:.audio) != .authorized {Button("Allow microphone access and start audio"){m.start()}.disabled(m.busy || !m.connected)}
    ForEach(m.devices){d in GroupBox(d.name){VStack(alignment:.leading,spacing:10){
     Text(d.setup)
     ForEach(["playback_level","microphone_level","microphone_delivery_level"],id:\.self){key in
      let level=m.level(d,key)
      HStack{Text(["playback_level":"Application audio","microphone_level":"Physical microphone","microphone_delivery_level":"Shared microphone"][key] ?? key).frame(width:160,alignment:.leading);ProgressView(value:level>0 ? min(1,max(0,(20*log10(level)+60)/60)):0).frame(minWidth:100);Text(level>0 ? String(format:"%.0f dB",20*log10(level)):"Silent").monospacedDigit().frame(width:65,alignment:.trailing)}
     }
     Button("Play test sound from \(d.name)"){m.send(["command":"test_tone","device_id":d.id])}.disabled(m.busy || !m.running || !d.online)
    }.padding(8)}}
    Text("Test sounds play for two seconds through the Hub’s headphones. Signal meters show activity; confirm that you can actually hear the sound.").font(.caption).foregroundColor(.secondary)
    if !m.diagnosticMessage.isEmpty {Text(m.diagnosticMessage).textSelection(.enabled)}
    ForEach(["playback_ready","mic_sink_ready","headphones_ready","microphone_ready"],id:\.self){key in if let ready=m.diagnostics[key] as? Bool {HStack{Image(systemName:ready ? "checkmark.circle.fill":"circle").foregroundColor(ready ? .green:.secondary);Text(["playback_ready":"Local playback capture","mic_sink_ready":"Shared microphone output","headphones_ready":"Headphone output","microphone_ready":"Physical microphone"][key] ?? key);Spacer();Text(ready ? "Ready":"Not active").foregroundColor(.secondary)}}}
    Divider();Text("Discord on Mac").font(.headline);Text("Speaker: BlackHole 2ch\nMicrophone: BlackHole 16ch")
    Text("Deadlock on Windows").font(.headline);Text("Output: CABLE Input (VB-Audio Virtual Cable)\nMicrophone: Hi-Fi Cable Output (VB-Audio Hi-Fi Cable)")
    Text("An inactive device may be expected when the other computer owns that role. No microphone recordings are saved by these checks.").font(.caption).foregroundColor(.secondary)
   }
  }.padding(.vertical,8)}
  Divider()
  if !m.error.isEmpty {Text(m.error).font(.callout).foregroundColor(.red).textSelection(.enabled)}
  HStack{VStack(alignment:.leading){Text(m.permissionPending ? "Waiting for microphone permission…":m.status).font(.caption);if !m.muteReason.isEmpty{Text(m.muteReason).font(.caption).foregroundColor(.orange)}};Spacer();if m.busy{ProgressView().controlSize(.small)};Button(m.permissionPending ? "Cancel start":((m.running || m.wantedRunning) ? "Stop audio":"Start audio")){if m.permissionPending{m.cancelPermission()}else if m.running || m.wantedRunning{m.send(["command":"stop"])}else{m.start()}}.disabled(m.busy || (!m.paired && !m.running)).keyboardShortcut(.defaultAction)}
 }.padding(24).frame(minWidth:820,minHeight:650).onAppear{m.open()}.alert("Forget this pairing?",isPresented:$forget){Button("Cancel",role:.cancel){};Button("Forget",role:.destructive){m.send(["command":"forget"])}}message:{Text("Audio will stop. You will need a new room code to connect these computers again.")}}
}
final class AppDelegate:NSObject,NSApplicationDelegate {
 func applicationDidFinishLaunching(_ notification:Notification){
  let loginEvent=NSAppleEventManager.shared().currentAppleEvent?.paramDescriptor(forKeyword:keyAEPropData)?.enumCodeValue == keyAELaunchedAsLogInItem
  if ProcessInfo.processInfo.arguments.contains("--background") || loginEvent {DispatchQueue.main.async{NSApp.windows.forEach{$0.orderOut(nil)}}}
 }
 func applicationShouldTerminateAfterLastWindowClosed(_ sender:NSApplication)->Bool {false}
 func applicationShouldHandleReopen(_ sender:NSApplication,hasVisibleWindows:Bool)->Bool {if !hasVisibleWindows{sender.windows.first(where:{$0.canBecomeMain})?.makeKeyAndOrderFront(nil)};return true}
}
struct MenuControls:View {
 @EnvironmentObject var model:BridgeModel
 @Environment(\.openWindow) private var openWindow
 var body:some View {Button("Open AudioBridge"){openWindow(id:"main");NSApp.activate(ignoringOtherApps:true);model.poll()};Divider();Button("Quit AudioBridge"){model.quit()}}
}
@main struct AudioBridgeApp:App {
 @NSApplicationDelegateAdaptor(AppDelegate.self) private var appDelegate
 @StateObject private var model=BridgeModel()
 var body:some Scene {
  WindowGroup("AudioBridge",id:"main"){ContentView().environmentObject(model)}.commands{CommandGroup(replacing:.appTermination){Button("Quit AudioBridge"){model.quit()}.keyboardShortcut("q")}}
  MenuBarExtra("AudioBridge",systemImage:"waveform"){MenuControls().environmentObject(model)}
 }
}
