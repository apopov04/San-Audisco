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
u.GetWindowRect.argtypes=[w.HWND,ctypes.POINTER(w.RECT)]
u.GetClientRect.argtypes=[w.HWND,ctypes.POINTER(w.RECT)]
u.MapWindowPoints.argtypes=[w.HWND,w.HWND,ctypes.POINTER(w.POINT),w.UINT]
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
 class Header(ctypes.Structure):_fields_=[('hwndFrom',w.HWND),('idFrom',ctypes.c_size_t),('code',w.UINT)]
 def page(index):
  tab=get(180);u.SendMessageW(tab,0x130C,index,0);header=Header(tab,180,(-551)&0xffffffff);u.SendMessageW(hwnd,0x4E,180,ctypes.addressof(header))
 def seen(command):
  if not events.exists():return []
  return [r for line in events.read_text().splitlines() if (r:=json.loads(line)).get('command')==command]
 wait(lambda:value(get(201))=='Test Windows','Initial state did not populate')
 assert not u.IsWindowEnabled(get(702)),'Unpaired Hub control should be disabled'
 client=w.RECT();assert u.GetClientRect(hwnd,ctypes.byref(client))
 for control_id in [700,703,705,720,723,725,410,420,440,441,431,601,602,760]:
  rect=w.RECT();assert u.GetWindowRect(get(control_id),ctypes.byref(rect));points=(w.POINT*2)(w.POINT(rect.left,rect.top),w.POINT(rect.right,rect.bottom));u.MapWindowPoints(None,hwnd,points,2)
  assert points[0].x>=0 and points[0].y>=0 and points[1].x<=client.right and (points[0].y+24 if control_id in [703,705,723,725] else points[1].y)<=client.bottom,f'Control {control_id} outside client area'
 assert u.IsWindowVisible(get(703)) and u.IsWindowVisible(get(723)),'Both computer controls must be on the main screen'
 assert u.SendMessageW(get(703),0x146,0,0)==2,'Physical headphones list must exclude virtual cable'
 assert u.SendMessageW(get(705),0x146,0,0)==2,'Physical microphone list must exclude virtual cable'
 assert 'Offline' in value(get(721)),'Peer should be offline before room connection'
 # Independent hub selectors sit above the icons; Input/Output are adjacent.
 def bounds(i):
  r=w.RECT();assert u.GetWindowRect(get(i),ctypes.byref(r));return r
 for base in (700,720):
  assert bounds(base+2).bottom<=bounds(760).top and bounds(base+4).bottom<=bounds(760).top
  assert bounds(base+5).top==bounds(base+3).top and bounds(base+5).right<=bounds(base+3).left
  assert value(get(base+6))=='Input' and value(get(base+7))=='Output'

 page(1)
 click(203);wait(lambda:seen('create'),'Create request missing')
 click(207);wait(lambda:u.SendMessageW(get(206),0x146,0,0)==1,'Discovery results missing')
 u.SendMessageW(get(206),0x14E,0,0);u.SendMessageW(hwnd,0x111,206|(1<<16),get(206))
 assert value(get(208))=='127.0.0.1:45671','Selected discovery address missing'
 u.SetWindowTextW(get(204),'BAD');click(205);time.sleep(.1);assert not seen('join'),'Malformed short code sent to agent'
 u.SetWindowTextW(get(204),'k7mp-4x9r');click(205)
 wait(lambda:seen('join'),'Join request missing')
 assert seen('join')[-1]['code']=='K7MP4X9R','Code normalization failed'
 assert seen('join')[-1]['room_id']=='fixture-room','Selected discovery room missing'
 wait(lambda:u.IsWindowEnabled(get(722)),'Acknowledged peer Hub control disabled')
 page(0)
 assert 'Online' in value(get(721)),'Connected peer must be shown online'
 before=sum(r.get('command')=='status' for r in seen('status'))
 for _ in range(50):u.SendMessageW(hwnd,0x113,2,0)
 time.sleep(.25)
 assert len(seen('status'))-before<=2,'Animation timer must not send IPC status polls'

 click(601);wait(lambda:value(get(601))=='Stop audio','Start not reflected')
 # Configure the remote computer directly below its icon.
 u.SendMessageW(get(725),0x14E,0,0);u.SendMessageW(hwnd,0x111,725|(1<<16),get(725))
 wait(lambda:seen('configure'),'Remote configure request missing')
 assert seen('configure')[-1]['device_id']=='mac-fixture'
 assert seen('configure')[-1]['physical_microphone']==''
 click(722);wait(lambda:seen('control'),'Hub request missing')
 assert seen('control')[-1]['key']=='primary' and seen('control')[-1]['value']==1
 click(704);wait(lambda:seen('control')[-1]['key']=='mic_source','Per-device mic source request missing')
 assert seen('control')[-1]['value']==0
 wait(lambda:value(get(722))=='Output Hub' and value(get(704))=='Input Hub','Independent role labels not updated')
 assert value(get(702))=='Use as Output Hub' and value(get(724))=='Use as Input Hub'
 before=len(seen('control'));click(722);time.sleep(.15)
 assert len(seen('control'))==before,'Clicking the current Output Hub must not send a role change'

 assert value(get(753))=='Global audio'
 for i in [702,704,722,724]:
  assert u.SendMessageW(get(i),0xF6,1,0),'Hub selector has no crowned icon'
  r=bounds(i);assert r.right-r.left==r.bottom-r.top,'Hub selector must be square'
 # Remove the protective global mute before exercising individual route controls.
 if u.SendMessageW(get(440),0xF0,0,0):click(440)
 wait(lambda:u.IsWindowEnabled(get(411)) and u.SendMessageW(get(411),0xF0,0,0)==0,'Global unmute not reflected')
 # Native icon buttons keep check state, accessible action names and real icons.
 for button_id,key in [(411,'win_mute'),(412,'win_deafen'),(421,'mac_mute'),(422,'mac_deafen')]:
  button=get(button_id);assert u.SendMessageW(button,0xF6,1,0),'Audio button has no icon'
  first_icon=u.SendMessageW(button,0xF6,1,0)
  click(button_id)
  wait(lambda:seen('control')[-1].get('key')==key and seen('control')[-1].get('value')==1,'Icon did not enable route gate')
  wait(lambda:u.SendMessageW(button,0xF0,0,0)==1 and value(button).startswith('Un'),'Active icon state/name missing')
  assert u.SendMessageW(button,0xF6,1,0)!=first_icon,'Active icon must show the slashed variant'
  click(button_id)
  wait(lambda:seen('control')[-1].get('key')==key and seen('control')[-1].get('value')==0,'Icon did not restore route')
  wait(lambda:u.SendMessageW(button,0xF0,0,0)==0,'Restored icon not reflected')
 # Global gates display effective states without overwriting individual preferences.
 click(411);wait(lambda:u.SendMessageW(get(411),0xF0,0,0)==1,'Local mute missing')
 click(441)
 wait(lambda:all(u.SendMessageW(get(i),0xF0,0,0)==1 and not u.IsWindowEnabled(get(i)) for i in [411,412,421,422,440]),'Global deafen must block and mark every route')
 saved=json.loads(state.read_text())['settings']
 assert saved['win_mute']==1 and not saved.get('mac_mute',0) and not saved.get('win_deafen',0) and not saved.get('mac_deafen',0),'Global gate overwrote local settings'
 assert 'Global audio' in value(get(412)),'Inherited state explanation missing'
 click(441)
 wait(lambda:all(u.IsWindowEnabled(get(i)) for i in [411,412,421,422,440]),'Undeafen did not restore controls')
 assert u.SendMessageW(get(411),0xF0,0,0)==1 and all(u.SendMessageW(get(i),0xF0,0,0)==0 for i in [412,421,422]),'Local preferences not restored'
 click(411);wait(lambda:u.SendMessageW(get(411),0xF0,0,0)==0,'Local mute did not clear')
 click(440)
 wait(lambda:all(u.SendMessageW(get(i),0xF0,0,0)==1 and not u.IsWindowEnabled(get(i)) for i in [411,421]),'Global mute must mark both microphones')
 assert all(u.SendMessageW(get(i),0xF0,0,0)==0 and u.IsWindowEnabled(get(i)) for i in [412,422]),'Global mute incorrectly deafened output'
 click(440);wait(lambda:u.IsWindowEnabled(get(411)) and u.IsWindowEnabled(get(421)),'Global unmute did not restore controls')
 for base in (700,720):
  before=[bounds(base+i) for i in (3,5)]
  saved=json.loads(state.read_text())
  for device in saved['devices']:
   for audio in device['audio_devices']:
    if audio['id']=='test-headphones':audio['name']='Very long physical output device name '+('extension '*12)
  state.write_text(json.dumps(saved))
  wait(lambda:value(get(base+3)).startswith('Very long'),'Long device name not displayed')
  after=[bounds(base+i) for i in (3,5)]
  assert all((a.left,a.right,a.top)==(b.left,b.right,b.top) for a,b in zip(before,after)),'Long names moved selector bounds'
 page(3)
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
 healthy_state=state.read_bytes();state.write_text('{')
 try:wait(lambda:not u.IsWindowEnabled(get(703)) and not u.IsWindowEnabled(get(440)),'Failed status envelope kept cached controls enabled')
 finally:state.write_bytes(healthy_state)
 wait(lambda:u.IsWindowEnabled(get(703)) and u.IsWindowEnabled(get(440)),'Status recovery did not restore controls')
 fixture=exe.parent/'bridge-agent.exe';unavailable=exe.parent/'bridge-agent-unavailable.exe'
 wait(lambda:not unavailable.exists(),'Unexpected unavailable fixture file')
 fixture.rename(unavailable)
 try:
  wait(lambda:not u.IsWindowEnabled(get(703)) and not u.IsWindowEnabled(get(440)),'Transport failure kept cached controls enabled')
  assert 'unavailable' in value(get(701)) and 'unavailable' in value(get(721)),'Transport failure kept online device state'
 finally:unavailable.rename(fixture)
 wait(lambda:u.IsWindowEnabled(get(703)) and u.IsWindowEnabled(get(440)),'Successful status did not restore controls')
 u.SendMessageW(hwnd,0x10,0,0);wait(lambda:not u.IsWindowVisible(hwnd),'Close did not hide to tray')
 assert p.poll() is None and not seen('quit'),'Close stopped background app'
 u.ShowWindow(hwnd,9);click(602);p.wait(timeout=12)
 assert p.returncode==0 and seen('quit'),'Explicit quit did not stop agent and exit'
 print(json.dumps({'passed':True,'scope':'real Win32 GUI + fixture IPC; no audio or real credentials','checks':['startup state','two-device main screen','animation timer does not poll agent','main control bounds','physical selector filtering','peer connection state','disabled unpaired roles','create invite','discovery selection','code validation and normalization','join acknowledgement','start acknowledgement','remote device configuration','crowned square hub selectors','individual icon buttons','effective global mute/deafen and preference restoration','long-name alignment','readiness diagnostics','local and remote signal meters','explicit remote test sound','cancel pending restart','explicit agent reconnect','failed status and transport recovery','close keeps running','explicit quit']}))
finally:
 if p.poll() is None:p.kill();p.wait()
