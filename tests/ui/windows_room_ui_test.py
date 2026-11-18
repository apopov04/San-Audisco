"""Exercise the real native GUI against a TEST-ONLY agent fixture; never captures audio.
Run on Windows or Wine with a dedicated directory containing AudioBridgeApp.exe and
agent_fixture.cpp compiled as bridge-agent.exe. The directory must be disposable.
"""
import ctypes, ctypes.wintypes as w, pathlib, subprocess, time, json, sys
u=ctypes.WinDLL('user32',use_last_error=True)
u.FindWindowW.argtypes=[w.LPCWSTR,w.LPCWSTR];u.FindWindowW.restype=w.HWND
u.GetDlgItem.argtypes=[w.HWND,ctypes.c_int];u.GetDlgItem.restype=w.HWND
u.SendMessageW.argtypes=[w.HWND,w.UINT,w.WPARAM,w.LPARAM];u.SendMessageW.restype=w.LPARAM
u.SetWindowTextW.argtypes=[w.HWND,w.LPCWSTR]
u.IsWindowVisible.argtypes=[w.HWND];u.IsWindowVisible.restype=w.BOOL
u.IsWindowEnabled.argtypes=[w.HWND];u.IsWindowEnabled.restype=w.BOOL
u.ShowWindow.argtypes=[w.HWND,ctypes.c_int]
def wait(fn,message):
 until=time.monotonic()+12
 while time.monotonic()<until:
  if fn():return
  time.sleep(.05)
 raise AssertionError(message)
def value(h):
 b=ctypes.create_unicode_buffer(8192);u.SendMessageW(h,0xD,len(b),ctypes.cast(b,ctypes.c_void_p).value);return b.value
exe=pathlib.Path(sys.argv[1]).resolve()
assert (exe.parent/'bridge-agent.exe').exists()
assert not u.FindWindowW('AudioBridgeRoomWindow',None),'An existing AudioBridge room UI is running; test refuses to interfere'
state=exe.parent/'ui-fixture-state.json';events=exe.parent/'ui-fixture-requests.jsonl'
state.unlink(missing_ok=True);events.unlink(missing_ok=True)
p=subprocess.Popen([str(exe)])
try:
 wait(lambda:u.FindWindowW('AudioBridgeRoomWindow',None),'GUI did not open')
 hwnd=u.FindWindowW('AudioBridgeRoomWindow',None);get=lambda i:u.GetDlgItem(hwnd,i)
 click=lambda i:u.SendMessageW(get(i),0xF5,0,0)
 def seen(command):
  if not events.exists():return []
  return [r for line in events.read_text().splitlines() if (r:=json.loads(line)).get('command')==command]
 wait(lambda:value(get(201))=='Test Windows','Initial state did not populate')
 assert not u.IsWindowEnabled(get(401)),'Unpaired mixer should be disabled'
 click(203);wait(lambda:seen('create'),'Create request missing')
 click(207);wait(lambda:u.SendMessageW(get(206),0x146,0,0)==1,'Discovery results missing')
 u.SendMessageW(get(206),0x14E,0,0);u.SendMessageW(hwnd,0x111,206|(1<<16),get(206))
 assert value(get(208))=='127.0.0.1:45671','Selected discovery address missing'
 u.SetWindowTextW(get(204),'BAD');click(205);time.sleep(.1);assert not seen('join'),'Malformed short code sent to agent'
 u.SetWindowTextW(get(204),'k7mp-4x9r');click(205)
 wait(lambda:seen('join'),'Join request missing')
 assert seen('join')[-1]['code']=='K7MP4X9R','Code normalization failed'
 assert seen('join')[-1]['room_id']=='fixture-room','Selected discovery room missing'
 wait(lambda:u.IsWindowEnabled(get(401)),'Acknowledged paired mixer disabled')
 click(601);wait(lambda:value(get(601))=='Stop audio','Start not reflected')
 # Select the remote computer and apply a different physical microphone.
 u.SendMessageW(get(301),0x14E,1,0);u.SendMessageW(hwnd,0x111,301|(1<<16),get(301))
 u.SendMessageW(get(303),0x14E,0,0);u.SendMessageW(hwnd,0x111,303|(1<<16),get(303));click(307)
 wait(lambda:seen('configure'),'Remote configure request missing')
 assert seen('configure')[-1]['device_id']=='mac-fixture'
 assert seen('configure')[-1]['physical_microphone']==''
 u.SendMessageW(get(401),0x14E,1,0);u.SendMessageW(hwnd,0x111,401|(1<<16),get(401))
 wait(lambda:seen('control'),'Hub request missing')
 assert seen('control')[-1]['key']=='primary' and seen('control')[-1]['value']==1
 click(501);wait(lambda:'Fixture check complete' in value(get(502)),'Readiness checks not rendered')
 assert u.SendMessageW(get(510),0x408,0,0)>0,'Local signal meter missing'
 assert u.SendMessageW(get(513),0x408,0,0)>0,'Remote signal meter missing'
 assert not seen('test_tone'),'Test sound must never run automatically'
 click(521);wait(lambda:seen('test_tone'),'Explicit remote test sound missing')
 assert seen('test_tone')[-1]['device_id']=='mac-fixture','Test sound used wrong source'
 subprocess.run([str(exe.parent/'bridge-agent.exe'),'request'],input=json.dumps({'command':'simulate_engine_failure'})+'\n',text=True,stdout=subprocess.DEVNULL,check=True)
 wait(lambda:json.loads(state.read_text()).get('wanted_running'),'Fixture retry state missing')
 time.sleep(1.3)
 assert value(get(601))=='Stop audio','Pending restart intent must remain cancellable'
 click(601);wait(lambda:seen('stop'),'Stop while waiting to retry was not sent')
 click(503);time.sleep(.3);assert value(get(601))=='Start audio','Explicit reconnect did not refresh stopped state'
 u.SendMessageW(hwnd,0x10,0,0);wait(lambda:not u.IsWindowVisible(hwnd),'Close did not hide to tray')
 assert p.poll() is None and not seen('quit'),'Close stopped background app'
 u.ShowWindow(hwnd,9);click(602);p.wait(timeout=12)
 assert p.returncode==0 and seen('quit'),'Explicit quit did not stop agent and exit'
 print(json.dumps({'passed':True,'scope':'real Win32 GUI + fixture IPC; no audio or real credentials','checks':['startup state','disabled unpaired mixer','create invite','discovery selection','code validation and normalization','join acknowledgement','start acknowledgement','remote device configuration','Hub selection','readiness diagnostics','local and remote signal meters','explicit remote test sound','cancel pending restart','explicit agent reconnect','close keeps running','explicit quit']}))
finally:
 if p.poll() is None:p.kill();p.wait()
