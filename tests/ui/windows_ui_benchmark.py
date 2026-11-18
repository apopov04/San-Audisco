"""Measure an isolated GUI+fixture only; never launch against production audio."""
import ctypes,ctypes.wintypes as w,json,pathlib,subprocess,sys,time,os
exe=pathlib.Path(sys.argv[1]).resolve();seconds=float(sys.argv[2]) if len(sys.argv)>2 else 10
assert (exe.parent/'bridge-agent.exe').is_file()
u=ctypes.WinDLL('user32');k=ctypes.WinDLL('kernel32');ps=ctypes.WinDLL('psapi')
u.FindWindowW.argtypes=[w.LPCWSTR,w.LPCWSTR];u.FindWindowW.restype=w.HWND
u.ShowWindow.argtypes=[w.HWND,ctypes.c_int];u.SendMessageW.argtypes=[w.HWND,w.UINT,w.WPARAM,w.LPARAM]
k.OpenProcess.argtypes=[w.DWORD,w.BOOL,w.DWORD];k.OpenProcess.restype=w.HANDLE;k.CloseHandle.argtypes=[w.HANDLE]
k.GetProcessTimes.argtypes=[w.HANDLE]+[ctypes.POINTER(w.FILETIME)]*4
u.GetGuiResources.argtypes=[w.HANDLE,w.DWORD];u.GetGuiResources.restype=w.DWORD
class Memory(ctypes.Structure):
 _fields_=[('cb',w.DWORD),('faults',w.DWORD)]+[(key,ctypes.c_size_t) for key in ['peak_rss','rss','paged_peak','paged','nonpaged_peak','nonpaged','pagefile','peak_pagefile']]
ps.GetProcessMemoryInfo.argtypes=[w.HANDLE,ctypes.POINTER(Memory),w.DWORD]
assert not u.FindWindowW('SanAudiscoTestRoomWindow',None),'An isolated GUI is already running'
p=subprocess.Popen([str(exe),'--ui-test-dpi=96','--ui-test-theme=dark']);handle=None
try:
 until=time.monotonic()+12
 while not (hwnd:=u.FindWindowW('SanAudiscoTestRoomWindow',None)):
  assert time.monotonic()<until and p.poll() is None,'Test GUI did not open';time.sleep(.1)
 handle=k.OpenProcess(0x410,False,p.pid);assert handle
 def cpu():
  create,exit,kernel,user=[w.FILETIME() for _ in range(4)]
  assert k.GetProcessTimes(handle,ctypes.byref(create),ctypes.byref(exit),ctypes.byref(kernel),ctypes.byref(user))
  return sum((v.dwHighDateTime<<32)|v.dwLowDateTime for v in [kernel,user])/1e7
 def measure(label):
  time.sleep(1);start=time.monotonic();before=cpu();time.sleep(seconds);elapsed=time.monotonic()-start;used=cpu()-before
  mem=Memory();mem.cb=ctypes.sizeof(mem);assert ps.GetProcessMemoryInfo(handle,ctypes.byref(mem),mem.cb)
  return {'state':label,'seconds':elapsed,'cpu_percent_one_core':used/elapsed*100,'rss_mib':mem.rss/1048576,'gdi_handles':u.GetGuiResources(handle,0),'user_handles':u.GetGuiResources(handle,1)}
 results=[measure('visible_connected_animation')];u.ShowWindow(hwnd,6);results.append(measure('minimized'))
 print(json.dumps({'scope':'GUI process only with isolated fixture; excludes engine, helper subprocess CPU and native audio','results':results},indent=2))
 u.SendMessageW(hwnd,0x112,0xE100,0);p.wait(timeout=12)
finally:
 if handle:k.CloseHandle(handle)
 if p.poll() is None:p.kill();p.wait()
