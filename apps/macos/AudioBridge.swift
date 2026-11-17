import SwiftUI
import AppKit
struct DeviceInfo: Decodable, Identifiable { var id: String; var name: String; var inputs: Int; var outputs: Int; var rate: Double }
final class BridgeModel: ObservableObject {
 @Published var devices: [DeviceInfo] = []
 @Published var playback = ""
 @Published var microphoneOutput = ""
 @Published var headphones = ""
 @Published var microphone = ""
 @Published var peer = ""
 @Published var keyFile = ""
 @Published var running = false
 @Published var connected = false
 @Published var status = "Choose audio routes and start."
 @Published var primary = 0
 @Published var micSource = 1
 @Published var winVolume = 1.0
 @Published var macVolume = 1.0
 @Published var balance = 0.0
 @Published var master = 1.0
 @Published var winMute = false
 @Published var macMute = false
 @Published var winDeafen = false
 @Published var macDeafen = false
 @Published var mute = true
 @Published var deafen = false
 var lastValues: [String:Double] = [:]
 var process: Process?; var stdinPipe: Pipe?
 var executable: URL { Bundle.main.bundleURL.appendingPathComponent("Contents/MacOS/bridge-engine") }
 init() {
  let url=FileManager.default.urls(for:.applicationSupportDirectory,in:.userDomainMask)[0].appendingPathComponent("AudioBridge/mac.conf")
  if let text=try? String(contentsOf:url,encoding:.utf8) {for line in text.split(separator:"\n") {let a=line.split(separator:"=",maxSplits:1,omittingEmptySubsequences:false);guard a.count==2 else{continue};let v=String(a[1]);switch a[0] {case "peer":peer=v;case "key_file":keyFile=v;case "playback_input":playback=v;case "microphone_output":microphoneOutput=v;case "headphones_output":headphones=v;case "physical_microphone":microphone=v;default:break}}}
 }
 func refresh() {
  do { let p=Process(); p.executableURL=executable;p.arguments=["devices"];let out=Pipe();p.standardOutput=out;try p.run();let data=out.fileHandleForReading.readDataToEndOfFile();p.waitUntilExit();devices=String(decoding:data,as:UTF8.self).split(separator:"\n").compactMap {try? JSONDecoder().decode(DeviceInfo.self,from:Data($0.utf8))} } catch {status=error.localizedDescription}
 }
 func chooseKey() { let panel=NSOpenPanel(); panel.canChooseDirectories=false;if panel.runModal() == .OK {keyFile=panel.url!.path} }
 func makeKey() {let panel=NSSavePanel();panel.nameFieldStringValue="audiobridge.key";if panel.runModal() == .OK,let url=panel.url {do{let p=Process();p.executableURL=executable;p.arguments=["keygen",url.path];try p.run();p.waitUntilExit();if p.terminationStatus==0 {keyFile=url.path;status="Transfer the key file privately to your PC."}}catch{status=error.localizedDescription}}}
 func command(_ name:String,_ value:Double) {guard running else{return};if lastValues[name] == value{return};lastValues[name]=value;try? stdinPipe?.fileHandleForWriting.write(contentsOf:Data("\(name) \(value)\n".utf8))}
 func sync(_ text: String) {
  for line in text.split(separator:"\n") {let parts=line.split(separator:" ");guard parts.count==2,let v=Double(parts[1]) else{continue};let k=String(parts[0]);lastValues[k]=v
   switch k {case "primary":primary=Int(v);case "mic_source":micSource=Int(v);case "mute":mute=v != 0;case "deafen":deafen=v != 0;case "win_volume":winVolume=v;case "mac_volume":macVolume=v;case "balance":balance=v;case "master":master=v;case "win_mute":winMute=v != 0;case "mac_mute":macMute=v != 0;case "win_deafen":winDeafen=v != 0;case "mac_deafen":macDeafen=v != 0;default:break}
  }
 }
 func start() {do {
  guard !peer.isEmpty && !keyFile.isEmpty && !playback.isEmpty && !microphoneOutput.isEmpty && !headphones.isEmpty && !microphone.isEmpty else {status="Complete all connection and audio fields.";return}
  guard playback != microphoneOutput else {status="Playback and microphone must use separate virtual cables.";return}
  let dir=FileManager.default.urls(for:.applicationSupportDirectory,in:.userDomainMask)[0].appendingPathComponent("AudioBridge");try FileManager.default.createDirectory(at:dir,withIntermediateDirectories:true)
  let conf=dir.appendingPathComponent("mac.conf")
  let text="device=mac\npeer=\(peer)\nkey_file=\(keyFile)\nplayback_input=\(playback)\nmicrophone_output=\(microphoneOutput)\nheadphones_output=\(headphones)\nphysical_microphone=\(microphone)\n"
  try text.write(to:conf,atomically:true,encoding:.utf8)
  let p=Process();p.executableURL=executable;p.arguments=["run",conf.path];let input=Pipe(),out=Pipe(),err=Pipe();p.standardInput=input;p.standardOutput=out;p.standardError=err;stdinPipe=input;process=p
  var pendingOutput=""
  out.fileHandleForReading.readabilityHandler={handle in let data=handle.availableData;if data.isEmpty{return};pendingOutput += String(decoding:data,as:UTF8.self);guard let end=pendingOutput.lastIndex(of:"\n") else{return};let lines=String(pendingOutput[...end]);pendingOutput.removeSubrange(...end);DispatchQueue.main.async {for line in lines.split(separator:"\n"){if let object=try? JSONSerialization.jsonObject(with:Data(line.utf8)) as? [String:Any] {if let state=object["settings"] as? String {self.sync(state)};self.connected=object["connected"] as? Bool ?? false;self.status=self.connected ? "Connected · audio bridge active" : "Waiting for the Windows app";if let muted=object["muted"] as? Bool, muted {self.status += " · microphone muted"}}}}}
  err.fileHandleForReading.readabilityHandler={handle in let text=String(decoding:handle.availableData,as:UTF8.self);if !text.isEmpty {DispatchQueue.main.async{self.status=text}}}
  p.terminationHandler={_ in DispatchQueue.main.async{self.running=false;self.connected=false;self.process=nil;self.stdinPipe=nil}}
  try p.run();running=true;status="Starting audio…"
 }catch{status=error.localizedDescription}}
 func stop(){try? stdinPipe?.fileHandleForWriting.write(contentsOf:Data("quit\n".utf8));try? stdinPipe?.fileHandleForWriting.close();running=false;connected=false;status="Stopped"}
}
struct ContentView: View {
 @StateObject var m=BridgeModel()
 func devicePicker(_ title:String,_ selection:Binding<String>,input:Bool)->some View {Picker(title,selection:selection){Text("Select a device").tag("");ForEach(m.devices.filter{input ? $0.inputs>0 : $0.outputs>0}){d in Text(d.name).tag(d.id)}}}
 var body: some View {ScrollView {VStack(alignment:.leading,spacing:16){
  HStack{Text("AudioBridge").font(.largeTitle).bold();Spacer();Circle().fill(m.connected ? Color.green : Color.gray).frame(width:10,height:10);Text(m.connected ? "Paired" : "Offline")}
  Text("Windows game audio + Mac Discord, in one pair of headphones.").foregroundColor(.secondary)
  GroupBox("Connection"){VStack{TextField("Windows IP address",text:$m.peer);HStack{TextField("Pairing key file",text:$m.keyFile);Button("Choose…"){m.chooseKey()};Button("Create key…"){m.makeKey()}}}.padding(8)}.disabled(m.running)
  GroupBox("Mac audio routes"){VStack{devicePicker("Discord playback cable input",$m.playback,input:true);devicePicker("Microphone cable output",$m.microphoneOutput,input:false);devicePicker("Headphones",$m.headphones,input:false);devicePicker("Physical microphone",$m.microphone,input:true);Button("Refresh devices"){m.refresh()}}.padding(8)}.disabled(m.running)
  HStack{Picker("Primary",selection:$m.primary){Text("Windows").tag(0);Text("Mac").tag(1)}.onChange(of:m.primary){m.command("primary",Double($0))};Picker("Mic source",selection:$m.micSource){Text("Windows").tag(0);Text("Mac").tag(1)}.onChange(of:m.micSource){m.command("mic_source",Double($0))}}
  Text("Connect AirPods to the selected primary in Bluetooth settings. Switching pauses audio.").font(.caption).foregroundColor(.secondary)
  GroupBox("Mix"){VStack{HStack{Text("Windows").frame(width:75);Slider(value:$m.winVolume).onChange(of:m.winVolume){m.command("win_volume",$0)};Toggle("Mute mic",isOn:$m.winMute).onChange(of:m.winMute){m.command("win_mute",$0 ? 1:0)};Toggle("Deafen",isOn:$m.winDeafen).onChange(of:m.winDeafen){m.command("win_deafen",$0 ? 1:0)}};HStack{Text("Mac").frame(width:75);Slider(value:$m.macVolume).onChange(of:m.macVolume){m.command("mac_volume",$0)};Toggle("Mute mic",isOn:$m.macMute).onChange(of:m.macMute){m.command("mac_mute",$0 ? 1:0)};Toggle("Deafen",isOn:$m.macDeafen).onChange(of:m.macDeafen){m.command("mac_deafen",$0 ? 1:0)}};HStack{Text("Windows");Slider(value:$m.balance,in:-1...1).onChange(of:m.balance){m.command("balance",$0)};Text("Mac")}}.padding(8)}
  HStack{Text("Master volume");Slider(value:$m.master).onChange(of:m.master){m.command("master",$0)}}
  HStack{Toggle("Mute microphone everywhere",isOn:$m.mute).onChange(of:m.mute){m.command("mute",$0 ? 1:0)};Toggle("Deafen all",isOn:$m.deafen).onChange(of:m.deafen){m.command("deafen",$0 ? 1:0)};Spacer();Button(m.running ? "Stop" : "Start"){if m.running{m.stop()}else{m.start()}}.keyboardShortcut(.defaultAction)}
  Text(m.status).font(.caption).textSelection(.enabled)
 }.padding(24)}.frame(minWidth:720,minHeight:560).onAppear{m.refresh()}.onDisappear{m.stop()}}
}
@main struct AudioBridgeApp: App {var body: some Scene {WindowGroup{ContentView()}}}
