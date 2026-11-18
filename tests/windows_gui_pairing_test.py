"""Optional Windows onboarding test in an isolated profile.
Pass a GUI beside the TEST-ONLY fake-audio runtime named san-audisco.exe.
This test refuses to overwrite existing San Audisco settings or paired-peer keys.
"""
import ctypes,ctypes.wintypes as w,subprocess,time,sys,pathlib,tempfile,os,secrets,json
u=ctypes.WinDLL('user32',use_last_error=True)
u.FindWindowW.argtypes=[w.LPCWSTR,w.LPCWSTR];u.FindWindowW.restype=w.HWND
u.GetDlgItem.argtypes=[w.HWND,ctypes.c_int];u.GetDlgItem.restype=w.HWND
u.SendMessageW.argtypes=[w.HWND,w.UINT,w.WPARAM,w.LPARAM];u.SendMessageW.restype=w.LPARAM
u.IsWindowEnabled.argtypes=[w.HWND];u.IsWindowEnabled.restype=w.BOOL
u.SetWindowTextW.argtypes=[w.HWND,w.LPCWSTR]
u.GetWindowTextW.argtypes=[w.HWND,w.LPWSTR,ctypes.c_int]
def value(h):
 b=ctypes.create_unicode_buffer(8000);u.SendMessageW(h,0xD,len(b),ctypes.cast(b,ctypes.c_void_p).value);return b.value
def wait(f,message):
 until=time.monotonic()+10
 while time.monotonic()<until:
  if f():return
  time.sleep(.1)
 raise AssertionError(message)
omit_microphone='--omit-microphone' in sys.argv[2:]
exe=pathlib.Path(sys.argv[1]).resolve();engine=exe.parent/'san-audisco.exe'
config=pathlib.Path(os.environ['LOCALAPPDATA'])/'AudioBridge/windows.conf'
credential=config.parent/'keys/paired-peer.dpapi'
if config.exists() or credential.exists():
 print('SKIP: GUI pairing test requires an isolated Windows user profile without existing San Audisco pairing')
 sys.exit(77)
prior=None
with tempfile.TemporaryDirectory(prefix='audio-pairing-') as tmp:
 key=pathlib.Path(tmp)/'pair-ünicode.key';key.write_text(secrets.token_hex(32)+'\n')
 p=subprocess.Popen([str(exe)]);hwnd=None
 try:
  wait(lambda:u.FindWindowW('SanAudiscoWindow',None),'GUI window missing');hwnd=u.FindWindowW('SanAudiscoWindow',None)
  get=lambda i:u.GetDlgItem(hwnd,i)
  wait(lambda:u.SendMessageW(get(210),0x146,0,0)>0,'Device list empty')
  assert not u.IsWindowEnabled(get(110)),'Mixer should be disabled before starting'
  u.SetWindowTextW(get(201),'127.0.0.1');u.SetWindowTextW(get(203),str(key));u.SetWindowTextW(get(100),'')
  for i,name in enumerate(['win-playback','win-mic-sink','win-headphones','win-mic']):
   if omit_microphone and i==3:name='Not used on this computer'
   label=ctypes.create_unicode_buffer(name);pos=u.SendMessageW(get(210+i),0x158,-1,ctypes.cast(label,ctypes.c_void_p).value)
   if pos<0:raise AssertionError('Fake audio device missing: '+name)
   u.SendMessageW(get(210+i),0x14E,pos,0)
  u.SendMessageW(get(102),0xF5,0,0)
  wait(lambda:value(get(102))=='Stop','First start failed: '+value(get(160)))
  time.sleep(1)
  assert value(get(102))=='Stop',value(get(160))
  assert u.IsWindowEnabled(get(110)),'Mixer should be enabled while running'
  saved=config.read_text()
  if omit_microphone:assert 'physical_microphone=\n' in saved,saved
  assert 'port=45670\n' in saved and 'peer_port=45670\n' in saved,saved
  assert 'key_id=paired-peer\n' in saved and 'key_file=' not in saved
  assert value(get(203))=='','Pairing key path not cleared'
  u.SendMessageW(get(102),0xF5,0,0);wait(lambda:value(get(102))=='Run','Stop failed')
  assert not u.IsWindowEnabled(get(110)),'Mixer should be disabled after stop'
  key.unlink()
  u.SendMessageW(get(102),0xF5,0,0);wait(lambda:value(get(102))=='Stop','Stored-key restart failed')
  time.sleep(1);assert value(get(102))=='Stop',value(get(160))
  print(json.dumps({'passed':True,'checks':['device dropdown routing','Unicode key import','DPAPI key ID config','port45670 compatibility','file field cleared','restart after original key file removed','mixer enable lifecycle', 'missing local microphone accepted' if omit_microphone else 'local microphone selected']}))
 finally:
  if hwnd:u.SendMessageW(hwnd,0x10,0,0)
  try:p.wait(timeout=5)
  except subprocess.TimeoutExpired:p.kill();p.wait()
  subprocess.run([str(engine),'key-delete','paired-peer'],capture_output=True)
  if prior is None:config.unlink(missing_ok=True)
  else:config.write_bytes(prior)
