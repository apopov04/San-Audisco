#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#include <oleacc.h>
#include <algorithm>
#include <string>
#include <iostream>
#include <stdexcept>
#include "../../apps/windows/appearance.hpp"
int wmain(){try{
 auto require=[](bool ok,const char* why){if(!ok)throw std::runtime_error(why);};
 CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);INITCOMMONCONTROLSEX cc{sizeof(cc),ICC_BAR_CLASSES};InitCommonControlsEx(&cc);appearance::testTheme=1;appearance::refresh();
 auto parent=CreateWindowW(L"STATIC",L"Appearance test",WS_OVERLAPPEDWINDOW,0,0,500,300,nullptr,nullptr,nullptr,nullptr);
 auto button=CreateWindowW(L"BUTTON",L"Mute microphone everywhere",WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX|BS_PUSHLIKE|BS_ICON|WS_TABSTOP,20,20,44,44,parent,reinterpret_cast<HMENU>(440),nullptr,nullptr);appearance::attach(button,96);
 auto slider=CreateWindowW(TRACKBAR_CLASSW,L"Master volume",WS_CHILD|WS_VISIBLE|WS_TABSTOP,90,20,180,28,parent,reinterpret_cast<HMENU>(431),nullptr,nullptr);appearance::attach(slider,96);
 auto font=CreateFontW(-14,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,0,0,L"Segoe UI");SendMessageW(button,WM_SETFONT,reinterpret_cast<WPARAM>(font),0);SendMessageW(slider,WM_SETFONT,reinterpret_cast<WPARAM>(font),0);
 IAccessible* accessible=nullptr;require(SUCCEEDED(AccessibleObjectFromWindow(button,OBJID_CLIENT,IID_IAccessible,reinterpret_cast<void**>(&accessible))),"Native accessibility object unavailable");
 VARIANT self{};self.vt=VT_I4;self.lVal=CHILDID_SELF;BSTR name{};require(SUCCEEDED(accessible->get_accName(self,&name))&&name&&std::wstring(name)==L"Mute microphone everywhere","Accessible name lost under custom paint");SysFreeString(name);
 SendMessageW(button,BM_SETCHECK,BST_CHECKED,0);VARIANT state{};auto stateResult=accessible->get_accState(self,&state);bool wine=GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"wine_get_version")!=nullptr;if(wine)std::cout<<"Wine MSAA reports incomplete native states; native Windows state assertions required separately.\n";require(SUCCEEDED(stateResult)&&(wine||(state.lVal&(STATE_SYSTEM_CHECKED|STATE_SYSTEM_PRESSED))),"Checked state unavailable to assistive technology");VariantClear(&state);
 EnableWindow(button,FALSE);require(SUCCEEDED(accessible->get_accState(self,&state))&&(wine||(state.lVal&STATE_SYSTEM_UNAVAILABLE)),"Disabled state unavailable to assistive technology");VariantClear(&state);EnableWindow(button,TRUE);accessible->Release();
 auto dc=CreateCompatibleDC(nullptr);auto screen=GetDC(nullptr);auto bitmap=CreateCompatibleBitmap(screen,500,300);ReleaseDC(nullptr,screen);auto old=SelectObject(dc,bitmap);
 // GDI+ and native controls initialize resources lazily on the first draw.
 // Log that cost separately, then check for growth across repeated paint batches.
 auto cold=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
 appearance::paint(button,dc);appearance::paint(slider,dc);GdiFlush();
 DWORD handles=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
 std::cout<<"GDI handles cold="<<cold<<" warm="<<handles<<std::endl;
 for(int theme:{0,1,0,1}){appearance::testTheme=theme;appearance::refresh();for(int batch=0;batch<4;++batch){for(int i=0;i<500;++i){SendMessageW(slider,TBM_SETPOS,0,i%101);appearance::paint(button,dc);appearance::paint(slider,dc);}GdiFlush();auto current=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);std::cout<<"theme="<<theme<<" batch="<<batch<<" handles="<<current<<std::endl;require(current<=handles+4,"GDI resources grow on repeated paints");}}
 appearance::palette.highContrast=true;require(!appearance::paint(button,dc),"High contrast must use the native control renderer");
 SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);DestroyWindow(parent);DeleteObject(font);CoUninitialize();std::cout<<(wine?"PASS accessible name, light/dark paint stability, bounded GDI resources and high-contrast fallback; native state checks pending Windows\n":"PASS native accessible names/checked/disabled states, light/dark paint stability, bounded GDI resources and high-contrast fallback\n");return 0;
 }catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
