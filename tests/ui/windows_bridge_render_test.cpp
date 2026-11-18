// Exercise the actual GDI renderer and timer lifecycle in-process. No agent/audio.
#define wWinMain audiobridge_app_entry
#include "../../apps/windows/main.cpp"
#undef wWinMain
#include <iostream>

int wmain(){
 try {
  auto require=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
  INITCOMMONCONTROLSEX common{sizeof(common),ICC_TAB_CLASSES};InitCommonControlsEx(&common);
  window=CreateWindowW(L"STATIC",L"Bridge render test",WS_OVERLAPPEDWINDOW,0,0,970,780,nullptr,nullptr,nullptr,nullptr);
  tabs=CreateWindowW(WC_TABCONTROLW,L"",WS_CHILD,0,0,200,100,window,nullptr,nullptr,nullptr);
  TCITEMW tab{};tab.mask=TCIF_TEXT;tab.pszText=const_cast<wchar_t*>(L"Audio");TabCtrl_InsertItem(tabs,0,&tab);TabCtrl_InsertItem(tabs,1,&tab);TabCtrl_SetCurSel(tabs,0);
  auto frame=[&]{
   BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=830;info.bmiHeader.biHeight=-95;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
   void* raw=nullptr;auto dc=CreateCompatibleDC(nullptr);auto bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&raw,nullptr,0);require(dc&&bitmap&&raw,"DIB allocation failed");auto previous=SelectObject(dc,bitmap);
   RECT area{0,0,830,95};FillRect(dc,&area,GetSysColorBrush(COLOR_WINDOW));SetViewportOrgEx(dc,-60,-140,nullptr);draw_computers(dc);GdiFlush();
   std::vector<DWORD> pixels(static_cast<DWORD*>(raw),static_cast<DWORD*>(raw)+830*95);SelectObject(dc,previous);DeleteObject(bitmap);DeleteDC(dc);return pixels;
  };
  auto dashes=[](const auto& pixels){return std::count_if(pixels.begin(),pixels.end(),[](DWORD v){switch(v&0xffffff){case 0x46cdeb:case 0x66e0a9:case 0xf9d358:case 0xb99af7:case 0x6ca3ff:case 0xe9f0f7:return true;default:return false;}});};
  agent_available=true;connected=false;auto a=frame();Sleep(180);require(a==frame(),"Disconnected bridge changed");require(dashes(a)==0,"Disconnected bridge has traffic");
  connected=true;BOOL effects=TRUE;SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION,0,&effects,0);a=frame();Sleep(180);auto b=frame();require(dashes(a)>0&&dashes(b)>0,"Connected dashes missing");require((a!=b)==bool(effects),"Motion does not follow OS animation setting");
  ShowWindow(window,SW_SHOW);sync_bridge_animation();require(bridge_timer_running==bool(effects),"Visible connected timer state incorrect");
  ShowWindow(window,SW_MINIMIZE);sync_bridge_animation();require(!bridge_timer_running,"Minimized window kept animation timer");
  ShowWindow(window,SW_RESTORE);TabCtrl_SetCurSel(tabs,1);sync_bridge_animation();require(!bridge_timer_running,"Other tab kept animation timer");
  TabCtrl_SetCurSel(tabs,0);sync_bridge_animation();ShowWindow(window,SW_HIDE);sync_bridge_animation();require(!bridge_timer_running,"Hidden window kept animation timer");
  connected=false;ShowWindow(window,SW_SHOW);sync_bridge_animation();require(!bridge_timer_running,"Disconnected window kept animation timer");DestroyWindow(window);
  std::cout<<"PASS actual Windows bridge renderer: connected dashes, motion preference, static disconnected frame; timer stops when hidden, minimized, off-tab or disconnected. Animation enabled="<<effects<<"\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
}
