"""Compile the production Mac room model and exercise real JSON/IPC with a fixture.
No UI is launched, no audio is captured, and no real agent or credentials are used.
Requires macOS Swift compiler and clang++; uses a disposable output directory.
"""
from pathlib import Path
import subprocess, tempfile, sys
root=Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix='audiobridge-ui-') as tmp:
 out=Path(tmp)
 subprocess.run(['clang++','-std=c++20','-O2','-I'+str(root/'third_party/json/include'),str(root/'tests/ui/agent_fixture.cpp'),'-o',str(out/'bridge-agent')],check=True)
 source=(root/'apps/macos/AudioBridge.swift').read_text().split('struct DeviceRouteView:View')[0]
 # Only expose the actual transport method for the test harness; its body is unchanged.
 source=source.replace('private static func invoke','static func invoke')
 source+='''
@main struct ModelTests {
 static func main() throws {
  func check(_ value:Bool,_ message:String){if !value{fatalError(message)}}
  let agent=URL(fileURLWithPath:CommandLine.arguments[1])
  _=try BridgeModel.invoke(agent,arguments:["ensure"],request:nil)
  var response=try BridgeModel.invoke(agent,arguments:["request"],request:["command":"status"])
  let model=BridgeModel();model.apply(response["state"] as! [String:Any])
  check(!model.paired && model.devices.count==2,"initial inventory")
  check(model.devices[0].audio.count==4 && model.devices[1].roleValue==1,"endpoint and role decoding")
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
  let partial=RoomDevice(["id":"x"]);check(partial.audio.isEmpty && !partial.online,"partial state is safe")
  _=try BridgeModel.invoke(agent,arguments:["request"],request:["command":"quit"])
  print("PASS Mac production model: inventory, role mapping, pending name edit, code validation, invitation formatting, IPC error envelope, paired state, numeric controls, running/mute cause, diagnostics, signal meters, pending reconnect cancellation, partial state")
 }
}
'''
 (out/'model_test.swift').write_text(source)
 subprocess.run(['swiftc','-target','arm64-apple-macos13.0','-parse-as-library',str(out/'model_test.swift'),'-o',str(out/'model_test'),'-framework','SwiftUI','-framework','AppKit','-framework','AVFoundation','-framework','ServiceManagement'],check=True)
 subprocess.run([str(out/'model_test'),str(out/'bridge-agent')],check=True)
