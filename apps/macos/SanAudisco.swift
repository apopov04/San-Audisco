import SwiftUI
import AppKit
import AVFoundation
import ServiceManagement
import Darwin

struct AudioEndpoint: Identifiable {
 let id: String; let name: String; let inputs: Int; let outputs: Int
 var isVirtual:Bool {let n=name.lowercased();return n.contains("blackhole") || n.contains("cable") || n.contains("streaming")}
 init(_ value: [String: Any]) {
  id=value["id"] as? String ?? ""; name=value["name"] as? String ?? id
  inputs=(value["inputs"] as? NSNumber)?.intValue ?? 0
  outputs=(value["outputs"] as? NSNumber)?.intValue ?? 0
 }
}
struct RoomDevice: Identifiable {
 let appPlaybackOutput:String;let appMicrophoneInput:String
 let id: String; let name: String; let online: Bool
 let slot:Int; let kind:String; let kindOverride:String; let mix:[String:Double]
 let audio: [AudioEndpoint]; let headphones: String; let microphone: String
 let playback: String; let micOutput: String; let setup: String; let diagnostics:[String:Any]
 init(_ value: [String: Any]) {
  id=value["id"] as? String ?? ""; name=value["name"] as? String ?? id
  appPlaybackOutput=value["app_playback_output"] as? String ?? "";appMicrophoneInput=value["app_microphone_input"] as? String ?? ""
  online=value["online"] as? Bool ?? false
  slot=(value["slot"] as? NSNumber)?.intValue ?? -1
  kind=value["kind"] as? String ?? "unknown";kindOverride=value["kind_override"] as? String ?? "auto"
  var decoded:[String:Double]=[:]
  for (key,v) in value["mix"] as? [String:Any] ?? [:] {if let number=v as? NSNumber{decoded[key]=number.doubleValue}}
  mix=decoded
  audio=(value["audio_devices"] as? [[String:Any]] ?? []).map(AudioEndpoint.init)
  headphones=value["headphones_output"] as? String ?? ""; microphone=value["physical_microphone"] as? String ?? ""
  playback=value["playback_input"] as? String ?? ""; micOutput=value["microphone_output"] as? String ?? ""
  setup=value["setup_status"] as? String ?? "Checking audio components…"
  diagnostics=value["diagnostics"] as? [String:Any] ?? [:]
 }
 var iconName:String {kind == "laptop" ? "laptopcomputer":kind == "server" ? "server.rack":"desktopcomputer"}
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
 @Published var outputHubID=""
 @Published var inputHubID=""
 @Published var deviceKindChoice="auto"
 @Published var deviceMix:[String:[String:Double]]=[:]
 @Published var deviceName=""
 @Published var invite=""
 @Published var expires=0
 @Published var joinCode=""
 @Published var joinAddress=""
 var selectedRoomID=""
 var selectedRoomAddress=""
 @Published var paired=false
 @Published var connected=false
 @Published var compatible=false
 @Published var sharedReady=false
 @Published var controlsReady=false
 @Published var handoverActive=false
 @Published var running=false
 @Published var wantedRunning=false
 @Published var available=false
 @Published var busy=false
 @Published var permissionPending=false
 @Published var status="Connecting to San Audisco…"
 @Published var error=""
 @Published var muteReason=""
 @Published var diagnosticMessage=""
 @Published var diagnostics: [String:Any]=[:]
 @Published var settings: [String:Double]=["mute":1,"deafen":0,"master":1,"balance":0]
 @Published var startAtLogin=false
 private let requests=DispatchQueue(label:"San Audisco.agent.requests")
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
    if result["ok"] as? Bool == false {throw NSError(domain:"San Audisco",code:6,userInfo:[NSLocalizedDescriptionKey:result["error"] as? String ?? "Background agent could not start."])}
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
  if let request=request {var data=try JSONSerialization.data(withJSONObject:request);guard data.count<=60_000 else{p.terminate();throw NSError(domain:"San Audisco",code:5,userInfo:[NSLocalizedDescriptionKey:"The requested change is too large."])};data.append(10);try input.fileHandleForWriting.write(contentsOf:data)}
  try? input.fileHandleForWriting.close()
  if finished.wait(timeout:.now()+15) == .timedOut {
   p.terminate();if finished.wait(timeout:.now()+1) == .timedOut {kill(p.processIdentifier,SIGKILL);_ = finished.wait(timeout:.now()+1)}
   throw NSError(domain:"San Audisco",code:1,userInfo:[NSLocalizedDescriptionKey:"San Audisco did not respond. Open the app again to reconnect; audio may still be running."])
  }
  guard drained.wait(timeout:.now()+2) == .success else {throw NSError(domain:"San Audisco",code:4,userInfo:[NSLocalizedDescriptionKey:"The agent response stream did not close."])}
  lock.lock();let result=bytes;let tooLarge=oversized;lock.unlock()
  guard !tooLarge else {throw NSError(domain:"San Audisco",code:2,userInfo:[NSLocalizedDescriptionKey:"San Audisco returned an oversized response."])}
  if arguments.first == "ensure" && p.terminationStatus==0 {return [:]}
  guard let object=(try? JSONSerialization.jsonObject(with:result)) as? [String:Any] else {
   throw NSError(domain:"San Audisco",code:3,userInfo:[NSLocalizedDescriptionKey:"Could not read the background agent response."])
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
     if response["ok"] as? Bool == false {self.error=response["error"] as? String ?? "San Audisco could not apply the change."}
     else {self.available=true;completion?()}
    }
   }catch {DispatchQueue.main.async{self.polling=false;if !quiet{self.busy=false};self.available=false;self.error=error.localizedDescription}}
  }
 }
 func poll() {guard !polling && !busy else{return};if available && !NSApp.windows.contains(where:{$0.isVisible && $0.canBecomeMain}){return};polling=true;send(["command":"status"],quiet:true)}
 func apply(_ state:[String:Any]) {
  localID=state["device_id"] as? String ?? localID
  if deviceName.isEmpty {deviceName=state["name"] as? String ?? Host.current().localizedName ?? "Mac"}
  invite=state["invite_code"] as? String ?? ""
  expires=(state["invite_expires_seconds"] as? NSNumber)?.intValue ?? 0
  paired=state["paired"] as? Bool ?? false;connected=state["connected"] as? Bool ?? false;running=state["running"] as? Bool ?? false;wantedRunning=state["wanted_running"] as? Bool ?? running
  status=state["status"] as? String ?? (connected ? "Connected" : "Waiting for the other computer")
  compatible=state["compatible"] as? Bool ?? false;sharedReady=state["shared_ready"] as? Bool ?? false;controlsReady=state["controls_ready"] as? Bool ?? false;handoverActive=state["handover_active"] as? Bool ?? false
  muteReason=state["mute_reason"] as? String ?? ""
  devices=(state["devices"] as? [[String:Any]] ?? []).map(RoomDevice.init).sorted{a,b in a.slot == b.slot ? a.id < b.id:a.slot < b.slot}
  outputHubID=state["hub_device_id"] as? String ?? "";inputHubID=state["mic_device_id"] as? String ?? ""
  deviceKindChoice=state["device_kind_override"] as? String ?? devices.first{$0.id==localID}?.kindOverride ?? "auto"
  for d in devices {for (key,value) in d.mix {let commandKey=key == "mic_muted" ? "mute":key == "deafened" ? "deafen":key == "silenced" ? "silence":key;if pendingControls["device:\(d.id):\(commandKey)"] == nil {deviceMix[d.id,default:[:]][commandKey]=value}}}
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
 func setRole(input:Bool,deviceID:String) {send(["command":"set_role","role":input ? "input":"output","device_id":deviceID])}
 func deviceControl(_ deviceID:String,_ key:String,_ value:Double,debounce:Bool=false) {
  deviceMix[deviceID,default:[:]][key]=value
  let token="device:\(deviceID):\(key)";pendingControls[token]?.cancel()
  let request:[String:Any]=["command":"control","device_id":deviceID,"key":key,"value":value]
  if debounce {let work=DispatchWorkItem{[weak self] in guard let self=self else{return};self.pendingControls[token]=nil;self.send(request)};pendingControls[token]=work;DispatchQueue.main.asyncAfter(deadline:.now()+0.15,execute:work)}else{pendingControls[token]=nil;send(request)}
 }
 func deviceNumber(_ deviceID:String,_ key:String,default fallback:Double=0)->Double {deviceMix[deviceID]?[key] ?? fallback}
 func deviceScalar(_ deviceID:String,_ key:String,default fallback:Double=0)->Binding<Double> {Binding(get:{self.deviceNumber(deviceID,key,default:fallback)},set:{self.deviceControl(deviceID,key,$0,debounce:true)})}
 var canStart:Bool {available && connected && compatible && sharedReady && !handoverActive}
 func number(_ key:String,default fallback:Double=0)->Double {settings[key] ?? fallback}
 func scalar(_ key:String,default fallback:Double=0)->Binding<Double> {Binding(get:{self.number(key,default:fallback)},set:{self.control(key,$0,debounce:true)})}
 var formattedInvite:String {invite.count==8 ? String(invite.prefix(4))+"-"+String(invite.suffix(4)) : invite}
 func setLogin(_ enabled:Bool) {do{if enabled{try SMAppService.mainApp.register()}else{try SMAppService.mainApp.unregister()};startAtLogin=SMAppService.mainApp.status == .enabled;if enabled && !startAtLogin{error="Approve San Audisco in System Settings → General → Login Items."}}catch{self.error=error.localizedDescription;startAtLogin=SMAppService.mainApp.status == .enabled}}
 func deviceLabel(_ device:RoomDevice)->String {let matches=devices.filter{$0.name==device.name};guard matches.count>1 else{return device.name};var count=4;while count<device.id.count && matches.filter({$0.id.prefix(count)==device.id.prefix(count)}).count>1{count+=1};return "\(device.name) · \(device.id.prefix(count))"}
 func isOnline(_ device:RoomDevice)->Bool {available && device.online && (device.id==localID || connected)}
 func level(_ device:RoomDevice,_ key:String)->Double {let source=device.id==localID ? diagnostics : device.diagnostics;let value=(source[key] as? NSNumber)?.doubleValue ?? 0;return value.isFinite ? min(1,max(0,value)):0}
 func create(){var request:[String:Any]=["command":"create"];if deviceName != devices.first(where:{$0.id==localID})?.name{request["name"]=deviceName};send(request)}
 func join(){let code=joinCode.uppercased().filter{$0 != "-" && !$0.isWhitespace};guard code.count==8 else{error="Enter the eight letters and numbers shown on the other computer.";return};let address=joinAddress.trimmingCharacters(in:.whitespacesAndNewlines);var request:[String:Any]=["command":"join","code":code,"address":address];if !selectedRoomID.isEmpty && address==selectedRoomAddress{request["room_id"]=selectedRoomID};send(request)}
 func start() {
  guard !permissionPending else{return};let requestID=UUID();permissionRequestID=requestID
  switch AVCaptureDevice.authorizationStatus(for:.audio) {
  case .authorized: finishPermission(true,id:requestID)
  case .notDetermined:
   permissionPending=true;error="";status="Waiting for macOS microphone permission…"
   AVCaptureDevice.requestAccess(for:.audio){[weak self] granted in DispatchQueue.main.async{self?.finishPermission(granted,id:requestID)}}
  case .denied,.restricted:
   permissionRequestID=nil;error="Allow San Audisco in System Settings → Privacy & Security → Microphone, then start audio again. Virtual cable capture also needs this permission."
  @unknown default:permissionRequestID=nil;error="Microphone permission is unavailable on this Mac."
  }
 }
 private func finishPermission(_ granted:Bool,id:UUID){guard permissionRequestID==id else{return};permissionRequestID=nil;permissionPending=false;guard granted else{error="Microphone access was not granted. Enable San Audisco in System Settings → Privacy & Security → Microphone.";return};send(["command":"start"])}
 func cancelPermission(){permissionRequestID=nil;permissionPending=false;status="Start cancelled"}
 func quit(){cancelPermission();send(["command":"quit"],completion:{NSApp.terminate(nil)})}
}

// Native pop-up sizing must not depend on the longest device name.
final class BoundedAudioPopUp:NSPopUpButton {
 override var intrinsicContentSize:NSSize {NSSize(width:NSView.noIntrinsicMetric,height:super.intrinsicContentSize.height)}
}
struct AudioDevicePicker:NSViewRepresentable {
 let title:String
 let options:[(id:String,name:String)]
 @Binding var selection:String
 @Environment(\.isEnabled) private var enabled
 final class Coordinator:NSObject {
  var owner:AudioDevicePicker
  init(_ owner:AudioDevicePicker){self.owner=owner}
  @objc func changed(_ sender:NSPopUpButton){owner.selection=sender.selectedItem?.representedObject as? String ?? ""}
 }
 func makeCoordinator()->Coordinator {Coordinator(self)}
 func makeNSView(context:Context)->BoundedAudioPopUp {
  let view=BoundedAudioPopUp(frame:.zero,pullsDown:false)
  view.target=context.coordinator;view.action=#selector(Coordinator.changed(_:))
  view.cell?.lineBreakMode = .byTruncatingTail;view.cell?.usesSingleLineMode=true
  view.setContentHuggingPriority(.defaultLow,for:.horizontal)
  view.setContentCompressionResistancePriority(.defaultLow,for:.horizontal)
  return view
 }
 func updateNSView(_ view:BoundedAudioPopUp,context:Context){
  context.coordinator.owner=self
  if view.itemArray.map({$0.representedObject as? String ?? ""}) != options.map(\.id) || view.itemTitles != options.map(\.name) {
   view.removeAllItems();for option in options{view.addItem(withTitle:option.name);view.lastItem?.representedObject=option.id}
  }
  view.selectItem(at:options.firstIndex(where:{$0.id==selection}) ?? 0)
  view.isEnabled=enabled;view.setAccessibilityLabel(title)
  view.toolTip=options.first(where:{$0.id==selection})?.name
 }
}

struct AudioControlIcon:View {
 let headphones:Bool
 var blocked=false
 var crowned=false
 var body:some View {
  VStack(spacing:0){
   if crowned {Image(systemName:"crown.fill").font(.system(size:9,weight:.semibold))}
   Image(systemName:headphones ? "headphones":(blocked ? "mic.slash":"mic"))
    .font(.system(size:16,weight:.medium)).frame(width:22,height:22)
    .overlay{if headphones && blocked{Rectangle().frame(width:25,height:2).rotationEffect(.degrees(-45))}}
  }.frame(width:20,height:36).accessibilityHidden(true)
 }
}

struct DeviceRouteView:View {
 @ObservedObject var model:BridgeModel
 let device:RoomDevice
 private var online:Bool {model.isOnline(device)}
 private var controlsDisabled:Bool {!online || model.busy}
 private func route(_ key:String,_ current:String)->Binding<String> {Binding(get:{current},set:{model.send(["command":"configure","device_id":device.id,key:$0])})}
 private func select(_ title:String,key:String,current:String,input:Bool,virtual:Bool=false)->some View {
  let choices=device.audio.filter{(input ? $0.inputs>0 : $0.outputs>0) && $0.isVirtual==virtual}
  let missing = !current.isEmpty && !choices.contains(where:{$0.id==current})
  let options:[(id:String,name:String)]=[("","Choose \(title.lowercased())…")]+(missing ? [(current,"Saved device · unavailable")]:[])+choices.map{($0.id,$0.name)}
  return VStack(alignment:.leading,spacing:5){
   Text(title).font(.subheadline).foregroundColor(.secondary)
   AudioDevicePicker(title:"\(model.deviceLabel(device)) \(title.lowercased())",options:options,selection:route(key,current)).frame(height:26).disabled(controlsDisabled)
  }.frame(minWidth:0,maxWidth:.infinity,alignment:.leading)
 }
 private func audioButton(deafen:Bool)->some View {
  let key=deafen ? "deafen":"mute"
  let inherited=model.number("deafen") != 0 || (!deafen && (model.number("mute",default:1) != 0 || model.deviceNumber(device.id,"deafen") != 0))
  let active=inherited || model.deviceNumber(device.id,key) != 0
  let action=deafen ? (active ? "Undeafen":"Deafen"):(active ? "Unmute microphone on":"Mute microphone on")
  let hint=inherited ? (model.number("deafen") != 0 ? "Deafen is on in Global audio. Turn it off there first.":model.number("mute",default:1) != 0 ? "Microphone is muted in Global audio. Unmute it there first.":"This computer is deafened. Undeafen it first.") : "\(action) \(model.deviceLabel(device))"
  return Button{model.deviceControl(device.id,key,active ? 0:1)}label:{
   AudioControlIcon(headphones:deafen,blocked:active).foregroundColor(active ? .red:.primary)
  }.buttonStyle(.bordered).help(hint).disabled(inherited)
   .accessibilityLabel(inherited ? "\(deafen ? "Deafened":"Microphone muted on") \(device.name). \(hint)":"\(action) \(model.deviceLabel(device))").accessibilityValue(active ? "On":"Off")

 }

 var body:some View {VStack(alignment:.leading,spacing:14){
  HStack(alignment:.top,spacing:12){
   select("Input",key:"physical_microphone",current:device.microphone,input:true).frame(minWidth:0,maxWidth:.infinity)
   select("Output",key:"headphones_output",current:device.headphones,input:false).frame(minWidth:0,maxWidth:.infinity)
  }
  Divider()
  VStack(alignment:.leading,spacing:6){HStack{Text("Audio from this computer");Spacer();Text("\(Int(model.deviceNumber(device.id,"volume",default:1)*100))%").monospacedDigit()};Slider(value:model.deviceScalar(device.id,"volume",default:1),in:0...1).accessibilityLabel("\(model.deviceLabel(device)) listening volume")}.disabled(controlsDisabled || !model.controlsReady)
  HStack(spacing:8){audioButton(deafen:false);audioButton(deafen:true);Spacer()}.disabled(controlsDisabled || !model.controlsReady)
  if online && device.setup != "Ready" {Text(device.setup).font(.caption).foregroundColor(.secondary).fixedSize(horizontal:false,vertical:true)}
  DisclosureGroup("Advanced audio routes") {VStack(alignment:.leading,spacing:12){select("App audio cable",key:"playback_input",current:device.playback,input:true,virtual:true);select("Microphone delivery cable",key:"microphone_output",current:device.micOutput,input:false,virtual:true)}.padding(.top,8)}.font(.caption)
 }.frame(maxWidth:.infinity,alignment:.topLeading)}
}

struct SetupBrandHeader:View {
 let title:String;let subtitle:String
 var body:some View {HStack(spacing:12){
  Image(nsImage:NSWorkspace.shared.icon(forFile:Bundle.main.bundlePath)).resizable().interpolation(.high).frame(width:48,height:48).accessibilityHidden(true)
  VStack(alignment:.leading,spacing:4){Text(title).font(.title2).bold();Text(subtitle).font(.caption).foregroundColor(.secondary)}
 }}
}

struct RoomSettingsView:View {
 @ObservedObject var m:BridgeModel
 @Environment(\.dismiss) private var dismiss
 @State private var forget=false
 var body:some View {VStack(alignment:.leading,spacing:20){
  HStack{SetupBrandHeader(title:m.paired ? "Room settings":"Connect your computers",subtitle:"San Audisco · one room, two computers");Spacer();Button("Done"){dismiss()}}
  ScrollView{VStack(alignment:.leading,spacing:20){
   Text("This computer").font(.headline)
   HStack{TextField("This computer’s name",text:$m.deviceName);Button("Save name"){m.send(["command":"configure","device_id":m.localID,"name":m.deviceName])}.disabled(m.busy || m.localID.isEmpty)}
   HStack{Text("Computer icon");Picker("Computer icon",selection:Binding(get:{m.deviceKindChoice},set:{m.deviceKindChoice=$0;m.send(["command":"device_profile","kind":$0])})){Text("Automatic").tag("auto");Label("Laptop",systemImage:"laptopcomputer").tag("laptop");Label("Desktop",systemImage:"desktopcomputer").tag("desktop");Label("Studio / server",systemImage:"server.rack").tag("server")}.labelsHidden()}.disabled(m.busy || !m.available)
   if !m.paired {
    Divider();Text("1 · Connect the computers").font(.headline)
    Text("Open San Audisco on both computers using the same Wi-Fi or local network. Create a room on one, then enter its code on the other.").foregroundColor(.secondary)
    if m.invite.isEmpty {Button("Create a room"){m.create()}.disabled(m.busy || !m.available)}else{
     Text("Enter this code on your other computer").font(.headline)
     HStack{Text(m.formattedInvite).font(.system(size:32,weight:.semibold,design:.monospaced)).textSelection(.enabled);Button("Copy"){NSPasteboard.general.clearContents();NSPasteboard.general.setString(m.formattedInvite,forType:.string)}}
     HStack{Text("Expires in \(m.expires) seconds").font(.caption).foregroundColor(.secondary);Spacer();Button("Cancel invitation"){m.send(["command":"cancel_invite"])}}
    }
    Divider();Text("Already have a code?").font(.headline)
    HStack{TextField("Room code",text:$m.joinCode);Button("Join room"){m.join()}.disabled(m.busy || !m.available)}
    DisclosureGroup("Connection options") {VStack(alignment:.leading,spacing:10){HStack{TextField("Other computer’s address (optional)",text:$m.joinAddress);Button("Find rooms"){m.send(["command":"discover"])}};ForEach(m.nearby){room in Button(room.name){m.joinAddress=room.address;m.selectedRoomID=room.id;m.selectedRoomAddress=room.address}.buttonStyle(.link)}}.padding(.top,10)}
   }else{
    ForEach(m.devices){d in HStack{Image(systemName:d.iconName);Text(m.deviceLabel(d));Spacer();Text(d.id==m.localID ? "This computer":(m.connected ? "Connected":"Offline")).foregroundColor(.secondary)}}
    Text("Pairing is remembered. Both apps control the same room.").foregroundColor(.secondary)
    GroupBox("Finish audio setup"){VStack(alignment:.leading,spacing:10){
     Label("Choose an Output Hub for your headphones.",systemImage:"headphones")
     Label("Choose an Input Hub for your microphone.",systemImage:"mic")
     Text("Use the crowned buttons above the computers in the main window. Then Autoconfigure sets up audio routing on both devices.").font(.caption).foregroundColor(.secondary)
     Button("Autoconfigure both computers"){m.send(["command":"autoconfigure"])}.disabled(m.busy || !m.connected)
    }.padding(6)}
    DisclosureGroup("Mix balance") {VStack(alignment:.leading,spacing:8){HStack{Text(m.devices.first{$0.slot==0}?.name ?? "First computer").lineLimit(1);Slider(value:m.scalar("balance"),in:-1...1).accessibilityLabel("Balance between the two computers");Text(m.devices.first{$0.slot==1}?.name ?? "Second computer").lineLimit(1)};Button("Center balance"){m.control("balance",0)}}.padding(.top,8)}.disabled(m.busy || !m.controlsReady)
    Button("Forget this pairing…",role:.destructive){forget=true}
   }
   Divider();Toggle("Start San Audisco at login",isOn:Binding(get:{m.startAtLogin},set:{m.setLogin($0)})).disabled(m.busy || !m.available)
   Text("Closing the window keeps audio running. Quit San Audisco from its menu-bar icon to stop the shared audio session.").font(.caption).foregroundColor(.secondary)
   if !m.error.isEmpty{Text(m.error).foregroundColor(.red).textSelection(.enabled)}
  }}
 }.padding(24).frame(width:540,height:520).alert("Forget this pairing?",isPresented:$forget){Button("Cancel",role:.cancel){};Button("Forget",role:.destructive){m.send(["command":"forget"])}}message:{Text("Audio will stop on this computer. Forget the pairing on the other computer as well before creating a new room.")}}
}

struct AudioChecksView:View {
 @ObservedObject var m:BridgeModel
 @Environment(\.dismiss) private var dismiss
 private var linked:Bool {m.available && m.connected && m.compatible && m.sharedReady && !m.handoverActive}
 private var routesReady:Bool {m.devices.count==2 && m.devices.allSatisfy{m.isOnline($0) && $0.setup=="Ready"}}
 private var selectedHubs:Bool {m.devices.contains{$0.id==m.outputHubID && !$0.headphones.isEmpty} && m.devices.contains{$0.id==m.inputHubID && !$0.microphone.isEmpty}}
 private func checklist(_ title:String,_ detail:String,ready:Bool)->some View {
  HStack(alignment:.top,spacing:10){Image(systemName:ready ? "checkmark.circle.fill":"circle").foregroundColor(ready ? .green:.secondary).font(.title3);VStack(alignment:.leading,spacing:3){Text(title).font(.subheadline).bold();Text(detail).font(.caption).foregroundColor(.secondary).fixedSize(horizontal:false,vertical:true)};Spacer()}.accessibilityElement(children:.combine)
 }
 private var checklistView:some View {GroupBox("Setup checklist"){VStack(alignment:.leading,spacing:14){
  checklist("Connect both computers",m.handoverActive ? "Switching audio hub…":!m.connected ? "Open Room settings to create or join a room.":!m.compatible ? "Update San Audisco on the other computer.":m.sharedReady ? "Both computers share the same room.":"Waiting for room settings to synchronize.",ready:linked)
  checklist("Choose Input and Output Hubs",selectedHubs ? "Your selected microphone supplies both computers. Headphones play from the Output Hub.":"Select input and output devices, then use the crowned buttons on the main screen.",ready:selectedHubs)
  checklist("Configure audio routes",routesReady ? "Audio components and selected routes are ready.":"Run Autoconfigure. If a component is missing, reopen San Audisco Setup on that computer.",ready:routesReady)
  checklist("Start and test audio",m.running ? "Play a test sound below and check the microphone meters.":"Start audio when both computers are ready. macOS may ask for microphone access.",ready:m.running && linked)
 }.padding(8)}}
 private func deviceChecks(_ d:RoomDevice)->some View {
  GroupBox {VStack(alignment:.leading,spacing:12){
   HStack{Label(m.deviceLabel(d),systemImage:d.iconName).font(.headline);Spacer();Text(m.isOnline(d) ? (d.setup=="Ready" ? "Ready":"Needs attention"):"Offline").font(.caption).foregroundColor(m.isOnline(d) && d.setup=="Ready" ? .green:.secondary)}
   if d.setup != "Ready" {Text(d.setup).font(.caption).foregroundColor(.secondary).fixedSize(horizontal:false,vertical:true)}
   ForEach(["playback_level","microphone_level","microphone_delivery_level"],id:\.self){key in
    let level=m.level(d,key)
    HStack{Text(["playback_level":"App audio","microphone_level":"Microphone","microphone_delivery_level":"Voice sent to apps"][key] ?? key).font(.caption).frame(width:120,alignment:.leading);ProgressView(value:level>0 ? min(1,max(0,(20*log10(level)+60)/60)):0).accessibilityLabel("\(m.deviceLabel(d)) \(key)");Text(level>0 ? String(format:"%.0f dB",20*log10(level)):"Silent").font(.caption).monospacedDigit().frame(width:55,alignment:.trailing)}
   }
   Button("Play test sound"){m.send(["command":"test_tone","device_id":d.id])}.disabled(m.busy || !m.running || !m.isOnline(d)).help("Play a two-second test sound from \(m.deviceLabel(d)) through the Output Hub.")
  }.padding(8)}
 }
 var body:some View {VStack(alignment:.leading,spacing:18){
  HStack{SetupBrandHeader(title:"Audio checks",subtitle:"San Audisco · get both computers ready");Spacer();Button("Done"){dismiss()}}
  ScrollView{VStack(alignment:.leading,spacing:18){
   checklistView
   HStack{Button("Autoconfigure"){m.send(["command":"autoconfigure"])}.disabled(m.busy || !m.available);Button("Refresh"){m.send(["command":"diagnose"])}.disabled(m.busy || !m.available);Spacer();if !m.running {Button(m.permissionPending ? "Cancel start":"Start audio"){if m.permissionPending{m.cancelPermission()}else{m.start()}}.buttonStyle(.borderedProminent).disabled(m.busy || !linked)}}
   if !m.error.isEmpty{Label(m.error,systemImage:"exclamationmark.triangle").foregroundColor(.red).textSelection(.enabled).fixedSize(horizontal:false,vertical:true)}
   ForEach(m.devices){d in deviceChecks(d)}
   Text("Test sounds play through the Output Hub. Microphone mute remains under your control in Global audio.").font(.caption).foregroundColor(.secondary)
   DisclosureGroup("Settings for Discord and other apps"){VStack(alignment:.leading,spacing:12){
    Text("Apps that follow the system default use the routes selected by Autoconfigure. Apps with a fixed device may need their own audio settings updated.").font(.caption).foregroundColor(.secondary)
    ForEach(m.devices){d in VStack(alignment:.leading,spacing:5){Text(m.deviceLabel(d)).font(.subheadline).bold();Text("Output: \(d.audio.first{$0.id==d.appPlaybackOutput}?.name ?? "Run Autoconfigure") · Input: \(d.audio.first{$0.id==d.appMicrophoneInput}?.name ?? "Run Autoconfigure")").font(.caption)}}
   }.padding(.top,8)}
   DisclosureGroup("Advanced details"){VStack(alignment:.leading,spacing:10){
    if !m.diagnosticMessage.isEmpty {Text(m.diagnosticMessage).font(.caption).textSelection(.enabled)}
    ForEach(m.devices){d in VStack(alignment:.leading,spacing:4){Text(m.deviceLabel(d)).font(.subheadline).bold();Text("Input: \(d.audio.first{$0.id==d.microphone}?.name ?? "Not selected")").font(.caption);Text("Output: \(d.audio.first{$0.id==d.headphones}?.name ?? "Not selected")").font(.caption)}}
    Button("Reconnect background service"){m.reconnectAgent()}.disabled(m.busy)
   }.padding(.top,8)}
  }.padding(.trailing,4)}
 }.padding(24).frame(width:620,height:650)}
}

// Scalable suspension bridge; its color follows room connectivity, not audio playback.
struct GoldenGateBridge:Shape {
 func path(in rect:CGRect)->Path {
  var p=Path()
  func point(_ x:CGFloat,_ y:CGFloat)->CGPoint {CGPoint(x:rect.minX+x*rect.width/160,y:rect.minY+y*rect.height/80)}
  func line(_ x:CGFloat,_ y:CGFloat,_ xx:CGFloat,_ yy:CGFloat){p.move(to:point(x,y));p.addLine(to:point(xx,yy))}
  func cable(_ x:CGFloat)->CGFloat {
   if x<45 {let u=(x-2)/43;return 12+42*(1-u*u)}
   if x>115 {let u=(158-x)/43;return 12+42*(1-u*u)}
   let u=(x-80)/35;return 12+32*(1-u*u)
  }
  line(2,58,158,58);line(2,66,158,66)
  p.move(to:point(2,cable(2)))
  for x in stride(from:CGFloat(4),through:158,by:2){p.addLine(to:point(x,cable(x)))}
  for x in stride(from:CGFloat(10),through:150,by:10){line(x,cable(x),x,58)}
  for x:CGFloat in [45,115]{
   line(x-4,10,x-4,73);line(x+4,10,x+4,73)
   for y:CGFloat in [17,29,41,53,68]{line(x-4,y,x+4,y)}
   line(x-7,73,x+7,73)
  }
  return p
 }
}

struct BridgeConnectionView:View {
 private static let trafficColors:[Color]=[
  Color(red:70/255,green:205/255,blue:235/255),Color(red:102/255,green:224/255,blue:169/255),
  Color(red:249/255,green:211/255,blue:88/255),Color(red:185/255,green:154/255,blue:247/255),
  Color(red:108/255,green:163/255,blue:255/255),Color(red:233/255,green:240/255,blue:247/255)
 ]
 let connected:Bool
 @Environment(\.accessibilityReduceMotion) private var reduceMotion
 @Environment(\.scenePhase) private var scenePhase
 private var bridgeColor:Color {connected ? Color(red:0.75,green:0.26,blue:0.17):Color.secondary}
 private func drawTraffic(context:GraphicsContext,size:CGSize,time:TimeInterval) {
  let travel:Double=reduceMotion ? 0:time/6
  let scaleX:CGFloat=size.width/160
  let scaleY:CGFloat=size.height/80
  for lane in 0..<2 {for dot in 0..<3 {
   let journey:Double=travel+Double(dot)/3
   let progress:Double=journey.truncatingRemainder(dividingBy:1)
   let colorIndex:Int=(lane*3+dot+Int(journey))%Self.trafficColors.count
   let x:Double=2+156*(lane==0 ? progress:1-progress)
   let y:CGFloat=58.75+CGFloat(lane)*4
   let rect=CGRect(x:CGFloat(x-3.5)*scaleX,y:y*scaleY,width:7*scaleX,height:2.5*scaleY)
   var stroke=context
   stroke.opacity=min(1,min((x-2)/10,(158-x)/10))
   let dash=Path(roundedRect:rect,cornerRadius:scaleY)
   stroke.fill(dash,with:.color(Self.trafficColors[colorIndex]))
   stroke.stroke(dash,with:.color(Color(red:0.12,green:0.18,blue:0.24)),lineWidth:0.35)
  }}
 }
 private var traffic:some View {
  TimelineView(.animation(minimumInterval:1.0/30,paused:reduceMotion || scenePhase != .active)){timeline in
   Canvas{context,size in
    drawTraffic(context:context,size:size,time:timeline.date.timeIntervalSinceReferenceDate)
   }
  }
 }
 var body:some View {
  ZStack {
   GoldenGateBridge().stroke(bridgeColor,style:StrokeStyle(lineWidth:1.8,lineCap:.round,lineJoin:.round))
   if connected {traffic}
  }.frame(height:80).accessibilityHidden(true)
 }
}

struct ContentView:View {
 @EnvironmentObject var m:BridgeModel
 @State private var showRoom=false
 @State private var showChecks=false
 private func device(_ slot:Int)->RoomDevice? {m.devices.first{$0.slot==slot}}
 private func online(_ d:RoomDevice?)->Bool {guard let d=d else{return false};return m.isOnline(d)}
 private var linkLabel:String {!m.available ? "App unavailable":m.connected ? "Connected":m.paired ? "Reconnecting…":"Not connected"}
 private var micBlocked:Bool {m.number("mute",default:1) != 0 || m.number("deafen") != 0 || (m.diagnostics["muted"] as? Bool == true)}
 @ViewBuilder private func hubChoice(_ role:Int,input:Bool)->some View {
  let title=input ? "Input Hub":"Output Hub"
  let d=device(role)
  let selected=d != nil && (input ? m.inputHubID:m.outputHubID)==d?.id
  let hasEndpoint=d?.audio.contains(where:{a in !a.isVirtual && (input ? (a.id==d?.microphone && a.inputs>0):(a.id==d?.headphones && a.outputs>0))}) ?? false
  let active=selected && m.paired
  let label=active ? "\(title) · \(d?.name ?? "This computer")":"Use \(d?.name ?? "this computer") as \(title)"
  Button{if !selected,let d=d{m.setRole(input:input,deviceID:d.id)}}label:{
   AudioControlIcon(headphones:!input,crowned:true).foregroundColor(active ? .blue:.primary)
  }.buttonStyle(.bordered).tint(active ? .blue:nil)
   .disabled(!online(d) || !m.canStart || m.busy || (!active && !hasEndpoint))
   .help("\(label). \(input ? "Supplies the microphone to both computers.":"Plays the combined audio through its output.")")
   .accessibilityLabel(label).accessibilityValue(active ? "Selected":"Not selected")

 }
 private func globalAudioButton(deafen:Bool)->some View {
  let inherited = !deafen && m.number("deafen") != 0
  let active=inherited || m.number(deafen ? "deafen":"mute",default:deafen ? 0:1) != 0
  let label=inherited ? "Microphone blocked by global deafen. Undeafen both first.":deafen ? (active ? "Undeafen both computers":"Deafen both computers"):(active ? "Unmute microphone everywhere":"Mute microphone everywhere")
  return Button{m.control(deafen ? "deafen":"mute",active ? 0:1)}label:{
   AudioControlIcon(headphones:deafen,blocked:active).foregroundColor(active ? .red:.primary)
  }.buttonStyle(.bordered).help(label).accessibilityLabel(label).accessibilityValue(active ? "On":"Off")
   .disabled(!m.controlsReady || m.busy || inherited)
 }
 @ViewBuilder private func computer(_ role:Int)->some View {
  let d=device(role);let ready=online(d)
  VStack(spacing:12){
   HStack(spacing:8){hubChoice(role,input:true);hubChoice(role,input:false)}.frame(height:44)
   Image(systemName:d?.iconName ?? "desktopcomputer").font(.system(size:68,weight:.light)).foregroundColor(ready ? .blue:.secondary).frame(height:84).accessibilityHidden(true)
   Text(d.map{m.deviceLabel($0)} ?? "Other computer").font(.title3).fontWeight(.semibold).lineLimit(2)
   HStack(spacing:5){Circle().fill(ready ? Color.blue:Color.secondary).frame(width:6,height:6);Text(ready ? ((d?.id==m.localID) ? "This computer · available":"Connected"):(m.paired ? "Offline":"Not connected"))}.font(.caption).foregroundColor(.secondary)
  }.frame(maxWidth:.infinity)
 }
 @ViewBuilder private func routes(_ role:Int)->some View {
  if let d=device(role){DeviceRouteView(model:m,device:d)}else{
   VStack(alignment:.leading,spacing:16){Text("Connect this computer to choose its input and output.").foregroundColor(.secondary);Button("Connect a computer"){showRoom=true}.disabled(!m.available)}.frame(maxWidth:.infinity,alignment:.leading)
  }
 }
 var body:some View {VStack(alignment:.leading,spacing:0){
  HStack{Text("San Audisco").font(.title2).fontWeight(.semibold);Spacer();Button("Autoconfigure"){m.send(["command":"autoconfigure"])}.disabled(!m.available || m.busy).help("Set up audio routes on both connected computers. Keeps your selected physical input and output.");Button(m.paired ? "Room settings":"Connect computers"){showRoom=true};Button{showChecks=true}label:{Label("Audio checks",systemImage:"waveform.path.ecg")}}
  Divider().padding(.vertical,16)
  ScrollView{VStack(spacing:18){
   HStack(alignment:.center,spacing:20){computer(0);VStack(spacing:8){BridgeConnectionView(connected:m.connected && m.available);Text(linkLabel).font(.caption).foregroundColor(.secondary)}.frame(width:160);computer(1)}
   HStack(alignment:.top,spacing:32){routes(0);Divider();routes(1)}
   Text("Connect headphones to the Output Hub. The Input Hub supplies your microphone to both computers.").font(.caption).foregroundColor(.secondary).frame(maxWidth:.infinity,alignment:.leading)
  }.padding(.vertical,6)}
  Divider().padding(.vertical,16)
  VStack(alignment:.leading,spacing:12){
   Text("Global audio").font(.headline)
   if !m.error.isEmpty {HStack(alignment:.top){Image(systemName:"exclamationmark.triangle");Text(m.error).textSelection(.enabled);Spacer();Button("Check setup"){showChecks=true}}.foregroundColor(.red)}
   if m.running && micBlocked {HStack(alignment:.top){Image(systemName:"mic.slash");Text(m.number("deafen") != 0 ? "Both computers are deafened. Listening and microphone delivery are off.":(m.muteReason.isEmpty ? "Microphone is blocked. Check audio readiness before speaking.":m.muteReason)).font(.callout);Spacer()}.foregroundColor(.orange)}
   HStack(spacing:16){
    HStack(spacing:8){globalAudioButton(deafen:false);globalAudioButton(deafen:true)}
    Spacer()
    HStack(spacing:8){Text("Master volume").font(.caption).foregroundColor(.secondary);Slider(value:m.scalar("master",default:1),in:0...1).accessibilityLabel("Master listening volume").help("Volume of both computers combined. Does not change microphone volume.").frame(width:110)}.disabled(!m.controlsReady || m.busy)
    if m.busy{ProgressView().controlSize(.small)}
    Button(m.permissionPending ? "Cancel start":((m.running || m.wantedRunning) ? "Stop audio":"Start audio")){if m.permissionPending{m.cancelPermission()}else if m.running || m.wantedRunning{m.send(["command":"stop"])}else{m.start()}}.disabled(m.busy || (!m.canStart && !m.running && !m.wantedRunning && !m.permissionPending)).keyboardShortcut(.defaultAction).help("Start or stop playback and microphone relay on both connected computers. Your room stays paired.")
   }
   Text(!m.available ? "San Audisco is unavailable. Open Audio checks to reconnect.":(m.permissionPending ? "Waiting for microphone permission…":m.status)).font(.caption).foregroundColor(.secondary).fixedSize(horizontal:false,vertical:true)
  }
 }.padding(28).frame(minWidth:840,minHeight:720).onAppear{m.open()}
 .sheet(isPresented:$showRoom){RoomSettingsView(m:m)}.sheet(isPresented:$showChecks){AudioChecksView(m:m)}
 }
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
 var body:some View {Button("Open San Audisco"){openWindow(id:"main");NSApp.activate(ignoringOtherApps:true);model.poll()};Divider();Button("Quit San Audisco"){model.quit()}}
}
@main struct SanAudiscoApp:App {
 @NSApplicationDelegateAdaptor(AppDelegate.self) private var appDelegate
 @StateObject private var model=BridgeModel()
 var body:some Scene {
  WindowGroup("San Audisco",id:"main"){ContentView().environmentObject(model)}.commands{CommandGroup(replacing:.appTermination){Button("Quit San Audisco"){model.quit()}.keyboardShortcut("q")}}
  MenuBarExtra {MenuControls().environmentObject(model)} label: {
   Label("San Audisco",systemImage:"waveform").help("San Audisco")
  }
 }
}
