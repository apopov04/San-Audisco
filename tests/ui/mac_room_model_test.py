"""Compile the production Mac room model and exercise real JSON/IPC with a fixture.
No UI is launched, no audio is captured, and no real agent or credentials are used.
Requires macOS Swift compiler and clang++; uses a disposable output directory.
"""
from pathlib import Path
import platform, subprocess, tempfile, sys
root=Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix='san-audisco-ui-') as tmp:
 out=Path(tmp)
 subprocess.run(['clang++','-std=c++20','-O2','-I'+str(root/'third_party/json/include'),str(root/'tests/ui/agent_fixture.cpp'),'-o',str(out/'bridge-agent')],check=True)
 source=(root/'apps/macos/SanAudisco.swift').read_text().split('struct DeviceRouteView:View')[0]
 # Only expose the actual transport method for the test harness; its body is unchanged.
 source=source.replace('private static func invoke','static func invoke')
 source=source.replace('Bundle.main.bundleURL.appendingPathComponent("Contents/MacOS/bridge-agent")','URL(fileURLWithPath:CommandLine.arguments[1])')
 source+='''
@main struct ModelTests {
 static func main() throws {
  func check(_ value:Bool,_ message:String){if !value{fatalError(message)}}
  let agent=URL(fileURLWithPath:CommandLine.arguments[1])
  _=try BridgeModel.invoke(agent,arguments:["ensure"],request:nil)
  var response=try BridgeModel.invoke(agent,arguments:["request"],request:["command":"status"])
  let model=BridgeModel();model.apply(response["state"] as! [String:Any])
  check(!model.paired && model.devices.count==2,"initial inventory")
  check(model.devices[0].audio.count==4 && model.devices[1].slot==1,"endpoint and role decoding")
  check(model.devices[0].audio.filter{!$0.isVirtual}.count==2,"physical selections must exclude virtual cables")
  model.available=true
  check(model.isOnline(model.devices[0]) && !model.isOnline(model.devices[1]),"cached peer inventory does not imply connection")
  model.available=false
  check(!model.isOnline(model.devices[0]),"missing local agent must not show connected")
  model.available=true
  model.deviceName="Unsaved name";model.apply(response["state"] as! [String:Any]);check(model.deviceName=="Unsaved name","poll must preserve name edit")
  model.joinCode="BAD";model.join();check(model.error.contains("eight"),"invalid code must be rejected before IPC")
  response=try BridgeModel.invoke(agent,arguments:["request"],request:["command":"create"])
  model.apply(response["state"] as! [String:Any]);check(model.formattedInvite=="K7MP-4X9R" && model.expires==300,"invite formatting and lifetime")
  response=try BridgeModel.invoke(agent,arguments:["request"],request:["command":"cancel_invite"])
  model.apply(response["state"] as! [String:Any]);check(model.invite.isEmpty,"cancel invitation keeps setup and clears code")
  response=try BridgeModel.invoke(agent,arguments:["request"],request:["command":"create"])
  response=try BridgeModel.invoke(agent,arguments:["request"],request:["command":"join","code":"WRONG123"])
  check(response["ok"] as? Bool == false && response["error"] as? String == "Wrong invitation code","negative acknowledgement")
  response=try BridgeModel.invoke(agent,arguments:["request"],request:["command":"join","code":"K7MP4X9R"])
  model.apply(response["state"] as! [String:Any]);check(model.paired && model.connected && model.invite.isEmpty,"join acknowledgement")
  check(model.isOnline(model.devices[1]),"authenticated peer shows connected")
  response=try BridgeModel.invoke(agent,arguments:["request"],request:["command":"control","key":"mute","value":0])
  model.apply(response["state"] as! [String:Any]);check(model.number("mute")==0,"numeric control acknowledgement")
  response=try BridgeModel.invoke(agent,arguments:["request"],request:["command":"start"])
  model.apply(response["state"] as! [String:Any]);check(model.running && !model.muteReason.isEmpty,"running and mute cause")
  response=try BridgeModel.invoke(agent,arguments:["request"],request:["command":"diagnose"])
  model.apply(response["state"] as! [String:Any]);check(model.diagnosticMessage.contains("Fixture check complete"),"diagnostics")
  check(model.level(model.devices[0],"playback_level")==0.2 && model.level(model.devices[1],"playback_level")==0.1,"local and remote signal meters")
  model.apply(["running":false,"wanted_running":true]);check(!model.running && model.wantedRunning,"pending reconnect intent remains stoppable")
  let sameOS=BridgeModel()
  sameOS.apply(["device_id":"mac-b","hub_device_id":"mac-a","mic_device_id":"mac-b","devices":[
   ["id":"mac-b","name":"MacBook Air","platform":"mac","slot":1,"kind":"laptop","online":true,"mix":["volume":0.7,"mic_muted":true]],
   ["id":"mac-a","name":"Mac Studio","platform":"mac","slot":0,"kind":"server","online":true,"mix":["volume":0.3,"deafened":true]]]])
  check(sameOS.devices.map{$0.id} == ["mac-a","mac-b"],"stable participant ordering independent of platform or local device")
  check(sameOS.devices.map{$0.iconName} == ["server.rack","laptopcomputer"],"hardware kind selects icons")
  check(sameOS.deviceNumber("mac-a","volume")==0.3 && sameOS.deviceNumber("mac-b","volume")==0.7,"same-platform volume ownership")
  check(sameOS.deviceNumber("mac-a","deafen")==1 && sameOS.deviceNumber("mac-b","mute")==1,"per-device mute decoding")
  check(sameOS.outputHubID=="mac-a" && sameOS.inputHubID=="mac-b","roles identify actual computers")
  sameOS.available=true;sameOS.connected=true;sameOS.compatible=true;sameOS.sharedReady=true
  check(sameOS.canStart,"matching room can start")
  sameOS.compatible=false;check(!sameOS.canStart,"older peer cannot start audio")
  sameOS.compatible=true;sameOS.handoverActive=true;check(!sameOS.canStart,"handover cannot start a second audio session")
  sameOS.handoverActive=false;sameOS.sharedReady=false;check(!sameOS.canStart,"unsynchronized room cannot start audio")
  sameOS.apply(["connected":true,"compatible":true,"shared_ready":false,"controls_ready":true]);check(sameOS.controlsReady && !sameOS.canStart,"ordinary pending revision keeps mixer controls available but gates Start")
  let duplicate=BridgeModel();duplicate.apply(["devices":[["id":"same-1","slot":0,"name":"MacBook Air"],["id":"same-2","slot":1,"name":"MacBook Air"]]])
  check(duplicate.deviceLabel(duplicate.devices[0]) != duplicate.deviceLabel(duplicate.devices[1]),"duplicate names remain distinguishable even when short ID prefixes collide")
  let windowsLaptop=RoomDevice(["id":"pc-b","platform":"windows","slot":1,"kind":"laptop"])
  check(windowsLaptop.iconName=="laptopcomputer","Windows laptops are not rendered as desktops")
  func settle(){let deadline=Date().addingTimeInterval(5);while model.busy && Date()<deadline{RunLoop.current.run(until:Date().addingTimeInterval(0.01))};check(!model.busy,"model command timed out")}
  model.setRole(input:false,deviceID:"mac-fixture");settle()
  model.deviceControl("mac-fixture","volume",0.6);settle()
  let events=try String(contentsOf:agent.deletingLastPathComponent().appendingPathComponent("ui-fixture-requests.jsonl"))
  let requests=try events.split(separator:"\\n").map{try JSONSerialization.jsonObject(with:Data($0.utf8)) as! [String:Any]}
  check(requests.contains{($0["command"] as? String)=="set_role" && ($0["role"] as? String)=="output" && ($0["device_id"] as? String)=="mac-fixture"},"role changes target device ID")
  check(requests.contains{($0["command"] as? String)=="control" && ($0["key"] as? String)=="volume" && ($0["device_id"] as? String)=="mac-fixture"},"volume changes target device ID")
  let partial=RoomDevice(["id":"x"]);check(partial.audio.isEmpty && !partial.online,"partial state is safe")
  _=try BridgeModel.invoke(agent,arguments:["request"],request:["command":"quit"])
  print("PASS Mac production model: inventory, same-platform IDs/roles/icons/controls, pending name edit, code validation, invitation formatting, IPC error envelope, paired state, numeric controls, running/mute cause, diagnostics, signal meters, pending reconnect cancellation, partial state")
 }
}
'''
 (out/'model_test.swift').write_text(source)
 subprocess.run(['swiftc','-target',platform.machine()+'-apple-macos13.0','-parse-as-library',str(out/'model_test.swift'),'-o',str(out/'model_test'),'-framework','SwiftUI','-framework','AppKit','-framework','AVFoundation','-framework','ServiceManagement'],check=True)
 subprocess.run([str(out/'model_test'),str(out/'bridge-agent')],check=True)
