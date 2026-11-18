"""Exercise the real native GUI against a TEST-ONLY agent fixture; never captures audio.
Run on Windows or Wine with a dedicated directory containing SanAudisco.exe and
agent_fixture.cpp compiled as bridge-agent.exe. The directory must be disposable.
"""
import ctypes, ctypes.wintypes as w, pathlib, subprocess, time, json, sys, os
u=ctypes.WinDLL('user32',use_last_error=True)
u.FindWindowW.argtypes=[w.LPCWSTR,w.LPCWSTR];u.FindWindowW.restype=w.HWND
u.GetDlgItem.argtypes=[w.HWND,ctypes.c_int];u.GetDlgItem.restype=w.HWND
u.SendMessageW.argtypes=[w.HWND,w.UINT,w.WPARAM,w.LPARAM];u.SendMessageW.restype=w.LPARAM
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
def set_text(h,text):
 # SetWindowTextW cannot set another process's edit control on native Windows.
 # WM_SETTEXT is system-marshalled and works on both Windows and Wine.
 b=ctypes.create_unicode_buffer(text)
 assert u.SendMessageW(h,0xC,0,ctypes.cast(b,ctypes.c_void_p).value),'Text entry failed'
 assert value(h).casefold()==text.casefold(),'Cross-process text entry did not reach the control'
exe=pathlib.Path(sys.argv[1]).resolve()
assert (exe.parent/'bridge-agent.exe').exists()
test_mode=os.environ.get('SAN_AUDISCO_UI_TEST')=='1'
window_class='SanAudiscoTestRoomWindow' if test_mode else 'SanAudiscoRoomWindow'
panel_class='SanAudiscoTestPanel' if test_mode else 'SanAudiscoPanel'
assert not u.FindWindowW(window_class,None),'An existing target GUI is running; test refuses to interfere'
if not test_mode:assert not u.FindWindowW('AudioBridgeRoomWindow',None),'An existing legacy room UI is running; test refuses to interfere'
state=exe.parent/'ui-fixture-state.json';events=exe.parent/'ui-fixture-requests.jsonl'
state.unlink(missing_ok=True);events.unlink(missing_ok=True)
dpi=int(sys.argv[2]) if len(sys.argv)>2 else 0
p=subprocess.Popen([str(exe)]+([f'--ui-test-dpi={dpi}'] if dpi else []))
try:
 wait(lambda:u.FindWindowW(window_class,None),'GUI did not open')
 hwnd=u.FindWindowW(window_class,None)
 wait(lambda:all(u.FindWindowW(panel_class,name) for name in ('Room settings','Advanced audio routes','Audio checks')),'Panels did not open')
 panels=[u.FindWindowW(panel_class,name) for name in ('Room settings','Advanced audio routes','Audio checks')]
 assert all(panels),'Native settings/checks panels missing'
 def get(i):
  for parent in [hwnd]+panels:
   found=u.GetDlgItem(parent,i)
   if found:return found
  return None
 click=lambda i:u.SendMessageW(get(i),0xF5,0,0)
 def page(index):
  for panel in panels:u.ShowWindow(panel,0)
  if index:click({1:604,2:606,3:605}[index])
 def seen(command):
  if not events.exists():return []
  return [r for line in events.read_text().splitlines() if (r:=json.loads(line)).get('command')==command]
 wait(lambda:value(get(201))=='Test Windows','Initial state did not populate')
 # Load the packaged resource locally: Wine cannot marshal a foreign class HICON.
 k=ctypes.WinDLL('kernel32',use_last_error=True)
 k.LoadLibraryExW.argtypes=[w.LPCWSTR,w.HANDLE,w.DWORD];k.LoadLibraryExW.restype=w.HMODULE
 k.FreeLibrary.argtypes=[w.HMODULE]
 u.LoadImageW.argtypes=[w.HINSTANCE,ctypes.c_void_p,w.UINT,ctypes.c_int,ctypes.c_int,w.UINT];u.LoadImageW.restype=w.HANDLE
 u.DestroyIcon.argtypes=[w.HANDLE]
 module=k.LoadLibraryExW(str(exe),None,2)
 assert module,'Cannot inspect application icon resources'
 try:
  for size in (16,32,48,256):
   icon=u.LoadImageW(module,101,1,size,size,0)
   assert icon,f'Packaged icon cannot load at {size}px'
   u.DestroyIcon(icon)
 finally:k.FreeLibrary(module)
 if dpi:
  u.SetWindowPos.argtypes=[w.HWND,w.HWND,ctypes.c_int,ctypes.c_int,ctypes.c_int,ctypes.c_int,w.UINT]
  u.SetWindowPos(hwnd,None,0,0,round(990*dpi/96),round(810*dpi/96),0x6)
 assert value(hwnd)=='San Audisco','Application window name differs from product name'
 assert not u.IsWindowEnabled(get(702)),'Unpaired Hub control should be disabled'
 client=w.RECT();assert u.GetClientRect(hwnd,ctypes.byref(client))
 for control_id in [700,703,705,720,723,725,410,420,440,441,431,601,603,604,605,740,760]:
  rect=w.RECT();assert u.GetWindowRect(get(control_id),ctypes.byref(rect));points=(w.POINT*2)(w.POINT(rect.left,rect.top),w.POINT(rect.right,rect.bottom));u.MapWindowPoints(None,hwnd,points,2)
  assert points[0].x>=0 and points[0].y>=0 and points[1].x<=max(client.right,round(900*(dpi or 96)/96)) and (points[0].y+24 if control_id in [703,705,723,725] else points[1].y)<=max(client.bottom,round(748*(dpi or 96)/96)),f'Control {control_id} outside scrollable content area'
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

 # Every footer control remains reachable even when the OS limits window size.
 u.SendMessageW(hwnd,0x115,7,0);u.SendMessageW(hwnd,0x114,7,0)
 r=bounds(601);pt=(w.POINT*2)(w.POINT(r.left,r.top),w.POINT(r.right,r.bottom));u.MapWindowPoints(None,hwnd,pt,2)
 assert 0<=pt[0].x and 0<=pt[0].y and pt[1].x<=client.right and pt[1].y<=client.bottom,'Scrolling cannot reveal Start/Stop'
 u.SendMessageW(hwnd,0x115,6,0);u.SendMessageW(hwnd,0x114,6,0)
 assert not get(180),'Main screen must not expose the old tabs'
 assert not get(602),'Quit must not overlap the header actions'
 assert bounds(760).bottom<=bounds(740).top,'Bridge paint region overlaps Connected text'
 assert bounds(700).right<=bounds(740).left and bounds(740).right<=bounds(720).left,'Connection label overlaps device names'
 for left,right in [(603,604),(604,605)]:assert bounds(left).right<=bounds(right).left,'Header buttons overlap'
 # Small windows retain readable controls and expose scrollbars instead of scaling text down.
 u.SetWindowPos.argtypes=[w.HWND,w.HWND,ctypes.c_int,ctypes.c_int,ctypes.c_int,ctypes.c_int,w.UINT]
 u.SetWindowPos(hwnd,None,0,0,760,520,0x6)
 time.sleep(.1)
 assert bounds(760).bottom<=bounds(740).top,'Small-window paint overlaps label'
 u.SetWindowPos(hwnd,None,0,0,round(990*(dpi or 96)/96),round(810*(dpi or 96)/96),0x6)
 page(1)
 u.SendMessageW(get(211),0x14E,3,0);u.SendMessageW(hwnd,0x111,211|(1<<16),get(211))
 wait(lambda:seen('device_profile'),'Device icon override request missing')
 assert seen('device_profile')[-1]['kind']=='server'
 assert u.IsWindowVisible(panels[0]) and u.IsWindowVisible(get(703)),'Settings must open independently of main device view'
 click(203);wait(lambda:seen('create'),'Create request missing')
 assert 'name' not in seen('create')[-1],'Room creation freezes an unchanged automatic name'
 click(207);wait(lambda:u.SendMessageW(get(206),0x146,0,0)==1,'Discovery results missing')
 u.SendMessageW(get(206),0x14E,0,0);u.SendMessageW(hwnd,0x111,206|(1<<16),get(206))
 assert value(get(208))=='127.0.0.1:45671','Selected discovery address missing'
 set_text(get(204),'BAD');click(205);time.sleep(.1);assert not seen('join'),'Malformed short code sent to agent'
 set_text(get(204),'k7mp-4x9r');click(205)
 wait(lambda:seen('join'),'Join request missing')
 assert seen('join')[-1]['code']=='K7MP4X9R','Code normalization failed'
 assert seen('join')[-1]['room_id']=='fixture-room','Selected discovery room missing'
 wait(lambda:u.IsWindowEnabled(get(722)),'Acknowledged peer Hub control disabled')
 page(0)
 assert 'Connected' in value(get(721)),'Connected peer must be shown online'
 assert value(get(740))=='Connected','Bridge connection label differs from Mac'
 click(603);wait(lambda:seen('autoconfigure'),'Autoconfigure request missing')
 before=sum(r.get('command')=='status' for r in seen('status'))
 for _ in range(50):u.SendMessageW(hwnd,0x113,2,0)
 time.sleep(.25)
 assert len(seen('status'))-before<=2,'Animation timer must not send IPC status polls'

 saved=json.loads(state.read_text());saved['compatible']=False;saved['shared_ready']=False;state.write_text(json.dumps(saved))
 wait(lambda:not u.IsWindowEnabled(get(601)) and 'Update' in value(get(752)),'Incompatible connected peer can start audio')
 saved['compatible']=True;saved['shared_ready']=True;state.write_text(json.dumps(saved))
 wait(lambda:u.IsWindowEnabled(get(601)),'Compatible room did not enable Start')
 click(601);wait(lambda:value(get(601))=='Stop audio','Start not reflected')
 # Configure the remote computer directly below its icon.
 u.SendMessageW(get(725),0x14E,0,0);u.SendMessageW(hwnd,0x111,725|(1<<16),get(725))
 wait(lambda:seen('configure'),'Remote configure request missing')
 assert seen('configure')[-1]['device_id']=='mac-fixture'
 assert seen('configure')[-1]['physical_microphone']==''
 click(722);wait(lambda:seen('set_role'),'Hub request missing')
 assert seen('set_role')[-1]['role']=='output' and seen('set_role')[-1]['device_id']=='mac-fixture'
 click(704);wait(lambda:seen('set_role')[-1]['role']=='input','Per-device mic source request missing')
 assert seen('set_role')[-1]['device_id']=='win-fixture'
 wait(lambda:value(get(722)).startswith('Output Hub · ') and value(get(704)).startswith('Input Hub · '),'Independent role labels not updated')
 assert value(get(702)).endswith(' as Output Hub') and value(get(724)).endswith(' as Input Hub')
 before=len(seen('set_role'));click(722);time.sleep(.15)
 assert len(seen('set_role'))==before,'Clicking the current Output Hub must not send a role change'

 assert value(get(753))=='Global audio'
 for i in [702,704,722,724]:
  assert u.SendMessageW(get(i),0xF6,1,0),'Hub selector has no crowned icon'
  r=bounds(i);assert r.right-r.left==r.bottom-r.top,'Hub selector must be square'
 # Remove the protective global mute before exercising individual route controls.
 if u.SendMessageW(get(440),0xF0,0,0):click(440)
 wait(lambda:u.IsWindowEnabled(get(411)) and u.SendMessageW(get(411),0xF0,0,0)==0,'Global unmute not reflected')
 # Native icon buttons keep check state, accessible action names and real icons.
 for button_id,key,device_id in [(411,'mute','win-fixture'),(412,'deafen','win-fixture'),(421,'mute','mac-fixture'),(422,'deafen','mac-fixture')]:
  button=get(button_id);assert u.SendMessageW(button,0xF6,1,0),'Audio button has no icon'
  first_icon=u.SendMessageW(button,0xF6,1,0)
  click(button_id)
  wait(lambda:seen('control')[-1].get('key')==key and seen('control')[-1].get('device_id')==device_id and seen('control')[-1].get('value')==1,'Icon did not enable route gate')
  wait(lambda:u.SendMessageW(button,0xF0,0,0)==1 and value(button).startswith('Un'),'Active icon state/name missing')
  assert u.SendMessageW(button,0xF6,1,0)!=first_icon,'Active icon must show the slashed variant'
  click(button_id)
  wait(lambda:seen('control')[-1].get('key')==key and seen('control')[-1].get('device_id')==device_id and seen('control')[-1].get('value')==0,'Icon did not restore route')
  wait(lambda:u.SendMessageW(button,0xF0,0,0)==0,'Restored icon not reflected')
 # Global gates display effective states without overwriting individual preferences.
 click(411);wait(lambda:u.SendMessageW(get(411),0xF0,0,0)==1,'Local mute missing')
 click(441)
 wait(lambda:all(u.SendMessageW(get(i),0xF0,0,0)==1 and not u.IsWindowEnabled(get(i)) for i in [411,412,421,422,440]),'Global deafen must block and mark every route')
 saved={d['id']:d['mix'] for d in json.loads(state.read_text())['devices']}
 assert saved['win-fixture']['mic_muted']==1 and not saved['mac-fixture'].get('mic_muted',0) and not saved['win-fixture'].get('deafened',0) and not saved['mac-fixture'].get('deafened',0),'Global gate overwrote local settings'
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
 for platform,kinds in [('windows',('laptop','server')),('mac',('server','desktop'))]:
  saved=json.loads(state.read_text())
  for device in saved['devices']:
   slot=device['slot'];device['platform']=platform;device['kind']=kinds[slot];device['name']=f'{platform} participant {slot}'
  state.write_text(json.dumps(saved))
  wait(lambda:value(get(700))==f'{platform} participant 0' and value(get(720))==f'{platform} participant 1','Same-platform participants collapsed into one card')
  assert u.IsWindowEnabled(get(703)) and u.IsWindowEnabled(get(723)),'Same-platform routes unavailable'
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
 if dpi:
  page(0)
  for panel_index in (0,1,3):
   if panel_index:
    page(panel_index);target=panels[panel_index-1]
    u.SetWindowPos(target,None,0,0,round(860*dpi/96),round(600*dpi/96),0x6)
   u.SendMessageW(hwnd,0x8000+40,panel_index,0)
   artifact=exe.parent/f'dpi-{dpi}-panel-{panel_index}.bmp'
   wait(lambda:artifact.exists() and artifact.stat().st_size>1000,'DPI screenshot missing')
 u.SendMessageW(hwnd,0x10,0,0);wait(lambda:not u.IsWindowVisible(hwnd),'Close did not hide to tray')
 assert p.poll() is None and not seen('quit'),'Close stopped background app'
 u.ShowWindow(hwnd,9);u.SendMessageW(hwnd,0x112,0xE100,0);p.wait(timeout=12)
 assert p.returncode==0 and seen('quit'),'Explicit quit did not stop agent and exit'
 print(json.dumps({'passed':True,'dpi':dpi or 'native','scope':'real Win32 GUI + fixture IPC; no audio or real credentials','checks':['startup state','native settings/checks panels','device profile override','same-platform participants','non-overlapping header and bridge label','small-window scrolling','two-device main screen','animation timer does not poll agent','main control bounds','physical selector filtering','peer connection state','disabled unpaired roles','create invite','discovery selection','code validation and normalization','join acknowledgement','start acknowledgement','remote device configuration','crowned square hub selectors','individual icon buttons','effective global mute/deafen and preference restoration','long-name alignment','readiness diagnostics','local and remote signal meters','explicit remote test sound','cancel pending restart','explicit agent reconnect','failed status and transport recovery','close keeps running','explicit quit']}))
finally:
 if p.poll() is None:p.kill();p.wait()
