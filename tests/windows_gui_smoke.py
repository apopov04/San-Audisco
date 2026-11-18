"""Optional Windows GUI smoke. Pass SanAudisco.exe; requires an audio endpoint.
May run under Wine but that is compatibility testing, not native Windows validation.
"""
import ctypes, ctypes.wintypes as w, subprocess,time,json,sys
u=ctypes.WinDLL('user32',use_last_error=True)
u.FindWindowW.argtypes=[w.LPCWSTR,w.LPCWSTR];u.FindWindowW.restype=w.HWND
u.SendMessageW.argtypes=[w.HWND,w.UINT,w.WPARAM,w.LPARAM];u.SendMessageW.restype=w.LPARAM
u.GetWindowTextW.argtypes=[w.HWND,w.LPWSTR,ctypes.c_int]
u.GetClassNameW.argtypes=[w.HWND,w.LPWSTR,ctypes.c_int]
u.GetDlgCtrlID.argtypes=[w.HWND];u.GetDlgCtrlID.restype=ctypes.c_int
callback_type=ctypes.WINFUNCTYPE(w.BOOL,w.HWND,w.LPARAM)
u.EnumChildWindows.argtypes=[w.HWND,callback_type,w.LPARAM]
p=subprocess.Popen([sys.argv[1]])
try:
 until=time.monotonic()+10;hwnd=None
 while time.monotonic()<until:
  hwnd=u.FindWindowW('SanAudiscoWindow',None)
  if hwnd:break
  time.sleep(.1)
 if not hwnd:raise AssertionError('Windows GUI did not create its native window')
 time.sleep(5)
 controls=[]
 @callback_type
 def inspect(h,arg):
  label=ctypes.create_unicode_buffer(1024);cls=ctypes.create_unicode_buffer(256)
  u.SendMessageW(h,0xD,len(label),ctypes.cast(label,ctypes.c_void_p).value);u.GetClassNameW(h,cls,len(cls))
  item={'id':u.GetDlgCtrlID(h),'class':cls.value,'text':label.value}
  if cls.value=='ComboBox':item['items']=int(u.SendMessageW(h,0x146,0,0))
  controls.append(item);return True
 u.EnumChildWindows(hwnd,inspect,0)
 for control_id in (210,211,212,213):
  c=next(x for x in controls if x['id']==control_id)
  if c.get('items',0)<1:raise AssertionError(f'Device list not populated: {c}; status: {[x for x in controls if x["id"]==160]}')
 log=next(x['text'] for x in controls if x['id']==160)
 if 'refreshed' not in log:raise AssertionError(log)
 print(json.dumps({'passed':True,'controls':len(controls),'devices':[x for x in controls if x['id'] in (210,211,212,213)],'status':log}))
finally:
 if hwnd:u.SendMessageW(hwnd,0x10,0,0)
 try:p.wait(timeout=5)
 except subprocess.TimeoutExpired:p.kill();p.wait();raise AssertionError('Windows GUI failed clean shutdown')
