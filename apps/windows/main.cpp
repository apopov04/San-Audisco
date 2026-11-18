#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <condition_variable>
#include <cmath>
#include <cctype>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
using Json=nlohmann::json;
constexpr UINT response_message=WM_APP+1,tray_message=WM_APP+2;
constexpr UINT tray_open=900,tray_quit=901;
constexpr UINT_PTR refresh_timer=1,bridge_timer=2;
bool bridge_timer_running=false;
int ui_dpi=96;
#ifdef BRIDGE_UI_TEST
int test_dpi=0;
#endif
int px(int value){return MulDiv(value,ui_dpi,96);}
void position(HWND h,int x,int y,int width,int height);
HFONT ui_font{},title_font{},heading_font{};
HICON audio_icons[2][2]{},hub_icons[2][2]{};HWND audio_tooltips{};
HWND card_name[2]{},card_status[2]{},card_headphones[2]{},card_microphone[2]{},card_hub[2]{},card_source[2]{},link_status{};
std::string card_id[2],card_signature[2];std::vector<std::string> card_routes[2][2];
HWND autoconfigure_button{},balance_label{},routing_help{};
HWND panels[4]{};
struct LayoutItem {HWND handle; int x,y,width,height,page;};
std::vector<LayoutItem> layout_items;
int scroll_x[4]{},scroll_y[4]{};
bool laying_out=false;
void layout_window(HWND);
void open_panel(int);
LRESULT CALLBACK panel_proc(HWND,UINT,WPARAM,LPARAM);
LRESULT CALLBACK focus_proc(HWND,UINT,WPARAM,LPARAM,UINT_PTR,DWORD_PTR);
HWND window{},status_label{},error_label{},mute_reason{},run_button{};
HWND name_edit{},kind_box{},invite_label{},code_edit{},address_edit{},nearby_box{},login_check{},create_button{},join_button{},forget_button{};
HWND computer_box{},headphones_box{},microphone_box{},playback_box{},micoutput_box{},apply_button{},setup_label{};
HWND playback_label{},micoutput_label{},advanced_button{};
HWND hub_box{},mic_source{},first_volume{},second_volume{},balance{},master{},mute{},deafen{},first_mute{},second_mute{},first_deafen{},second_deafen{},first_label{},second_label{},diagnostics_box{};
std::vector<HWND> pages[4],mix_controls;
HWND signal_bars[2][3]{},signal_labels[2]{},tone_buttons[2]{};std::string tone_ids[2];
int building_page=-1;
Json state=Json::object();std::vector<Json> devices,nearby;
std::vector<std::string> audio_ids[4];
std::string configured_id,applied_device_signature,selected_room_id,selected_room_address,last_local_name;
bool device_dirty=false,advanced=false,updating=false,quitting=false,agent_available=false,connected=false,running=false,wanted_running=false,paired=false;
NOTIFYICONDATAW tray{};
std::wstring executable_path(){std::vector<wchar_t> p(32768);auto n=GetModuleFileNameW(nullptr,p.data(),static_cast<DWORD>(p.size()));if(!n||n>=p.size())throw std::runtime_error("Cannot locate San Audisco");return {p.data(),n};}
std::wstring agent_path(){return (std::filesystem::path(executable_path()).parent_path()/L"bridge-agent.exe").wstring();}
std::wstring utf16(const std::string& s){if(s.empty())return {};int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),nullptr,0);if(!n)return L"Invalid text";std::wstring out(n,L'\0');MultiByteToWideChar(CP_UTF8,0,s.data(),static_cast<int>(s.size()),out.data(),n);return out;}
std::string utf8(const std::wstring& s){if(s.empty())return {};int n=WideCharToMultiByte(CP_UTF8,0,s.data(),static_cast<int>(s.size()),nullptr,0,nullptr,nullptr);std::string out(n,'\0');WideCharToMultiByte(CP_UTF8,0,s.data(),static_cast<int>(s.size()),out.data(),n,nullptr,nullptr);return out;}
std::wstring text(HWND w){int n=GetWindowTextLengthW(w);std::wstring s(n+1,L'\0');GetWindowTextW(w,s.data(),n+1);s.resize(n);return s;}
// Native controls often repaint even when their value did not change.
void set_text(HWND w,const wchar_t* value){if(w&&text(w)!=value)SetWindowTextW(w,value);}
void enable_control(HWND w,BOOL enabled){if(w&&bool(IsWindowEnabled(w))!=bool(enabled))EnableWindow(w,enabled);}
LRESULT control_message(HWND w,UINT message,WPARAM wp,LPARAM lp){
 if(!w)return 0;
 UINT getter=0;WPARAM argument=0;LRESULT desired=0;
 switch(message){
 case BM_SETIMAGE:getter=BM_GETIMAGE;argument=wp;desired=lp;break;
 case BM_SETCHECK:getter=BM_GETCHECK;desired=wp;break;
 case TBM_SETPOS:getter=TBM_GETPOS;desired=lp;break;
 case PBM_SETPOS:getter=PBM_GETPOS;desired=wp;break;
 case CB_SETCURSEL:getter=CB_GETCURSEL;desired=wp;break;
 default:break;
 }
 if(getter){auto current=SendMessageW(w,getter,argument,0);if(current==desired)return current;}
 return SendMessageW(w,message,wp,lp);
}
std::string string_value(const Json& v,const char* key,const std::string& fallback={}){auto i=v.find(key);return i!=v.end()&&i->is_string()?i->get<std::string>():fallback;}
bool boolean(const Json& v,const char* key,bool fallback=false){auto i=v.find(key);if(i==v.end())return fallback;if(i->is_boolean())return i->get<bool>();if(i->is_number())return i->get<double>()!=0;return fallback;}
double number(const Json& v,const char* key,double fallback=0){auto i=v.find(key);if(i==v.end())return fallback;if(i->is_boolean())return i->get<bool>()?1:0;if(i->is_number())return i->get<double>();return fallback;}
bool controls_ready(){return connected&&boolean(state,"compatible")&&boolean(state,"controls_ready")&&!boolean(state,"handover_active");}
bool room_ready(){return connected&&boolean(state,"compatible")&&boolean(state,"shared_ready")&&!boolean(state,"handover_active");}
std::string device_name(const Json& d){
 auto name=string_value(d,"name","Other computer"),id=string_value(d,"id");int duplicates=0;for(const auto& other:devices)if(string_value(other,"name")==name)++duplicates;
 if(duplicates>1&&!id.empty()){size_t count=std::min<size_t>(4,id.size());for(;count<id.size();++count){bool unique=true;for(const auto& other:devices)if(string_value(other,"name")==name&&string_value(other,"id")!=id&&string_value(other,"id").substr(0,count)==id.substr(0,count))unique=false;if(unique)break;}name+=" · "+id.substr(0,count);}return name;
}
struct Handle{HANDLE value{};Handle()=default;explicit Handle(HANDLE h):value(h){};~Handle(){if(value&&value!=INVALID_HANDLE_VALUE)CloseHandle(value);}Handle(const Handle&)=delete;Handle& operator=(const Handle&)=delete;void close(){if(value&&value!=INVALID_HANDLE_VALUE)CloseHandle(value);value=nullptr;}};
Json invoke(const wchar_t* argument,const Json* request=nullptr){
 SECURITY_ATTRIBUTES sa{sizeof(sa),nullptr,TRUE};Handle in_read,in_write,out_read,out_write;
 if(!CreatePipe(&in_read.value,&in_write.value,&sa,0)||!CreatePipe(&out_read.value,&out_write.value,&sa,0))throw std::runtime_error("Cannot create agent pipes");
 SetHandleInformation(in_write.value,HANDLE_FLAG_INHERIT,0);SetHandleInformation(out_read.value,HANDLE_FLAG_INHERIT,0);
 Handle null(CreateFileW(L"NUL",GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,&sa,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr));
 if(null.value==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot open agent error sink");
 SIZE_T attribute_bytes=0;InitializeProcThreadAttributeList(nullptr,1,0,&attribute_bytes);std::vector<unsigned char> attribute_buffer(attribute_bytes);
 auto* attributes=reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attribute_buffer.data());
 if(!InitializeProcThreadAttributeList(attributes,1,0,&attribute_bytes))throw std::runtime_error("Cannot isolate agent handles");
 struct Cleanup{LPPROC_THREAD_ATTRIBUTE_LIST p;~Cleanup(){DeleteProcThreadAttributeList(p);}}cleanup{attributes};
 HANDLE handles[]{in_read.value,out_write.value,null.value};
 if(!UpdateProcThreadAttribute(attributes,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,handles,sizeof(handles),nullptr,nullptr))throw std::runtime_error("Cannot restrict agent handles");
 STARTUPINFOEXW si{};si.StartupInfo.cb=sizeof(si);si.StartupInfo.dwFlags=STARTF_USESTDHANDLES;si.StartupInfo.hStdInput=in_read.value;si.StartupInfo.hStdOutput=out_write.value;si.StartupInfo.hStdError=null.value;si.lpAttributeList=attributes;
 PROCESS_INFORMATION pi{};auto exe=agent_path();auto cmd=L"\""+exe+L"\" "+argument;
 if(!CreateProcessW(exe.c_str(),cmd.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW|EXTENDED_STARTUPINFO_PRESENT,nullptr,nullptr,&si.StartupInfo,&pi))throw std::runtime_error("Cannot launch bridge-agent.exe beside SanAudisco.exe");
 Handle process(pi.hProcess),thread(pi.hThread);in_read.close();out_write.close();
 if(request){auto data=request->dump()+"\n";DWORD n=0;if(data.size()>65536||!WriteFile(in_write.value,data.data(),static_cast<DWORD>(data.size()),&n,nullptr)||n!=data.size()){TerminateProcess(process.value,1);throw std::runtime_error("Cannot send the agent request");}}
 in_write.close();std::string output;bool finished=false;const auto deadline=GetTickCount64()+15000;
 for(;;){DWORD count=0;if(PeekNamedPipe(out_read.value,nullptr,0,nullptr,&count,nullptr)&&count){char chunk[8192];DWORD n=0;if(ReadFile(out_read.value,chunk,std::min<DWORD>(count,sizeof(chunk)),&n,nullptr))output.append(chunk,n);if(output.size()>1024*1024){TerminateProcess(process.value,1);throw std::runtime_error("Agent response exceeded its size limit");}continue;}
  if(WaitForSingleObject(process.value,20)==WAIT_OBJECT_0){finished=true;DWORD remaining=0;if(PeekNamedPipe(out_read.value,nullptr,0,nullptr,&remaining,nullptr)&&remaining)continue;break;}
  if(GetTickCount64()>=deadline)break;
 }
 if(!finished){TerminateProcess(process.value,1);WaitForSingleObject(process.value,1000);throw std::runtime_error("San Audisco did not respond; the background audio may still be running");}
 DWORD code=1;GetExitCodeProcess(process.value,&code);
 if(std::wstring(argument)==L"ensure"&&code==0)return Json::object();
 auto result=Json::parse(output,nullptr,false);if(result.is_discarded()||!result.is_object())throw std::runtime_error("The background agent returned an unreadable response");return result;
}
struct Request{Json value;bool quiet=false;bool exit_after=false;};
struct Response{Json value;bool quiet=false;bool exit_after=false;bool transport_failure=false;};
std::mutex queue_mutex;std::condition_variable queue_cv;std::deque<Request> queue;
std::thread worker;bool stopping=false,status_queued=false;unsigned outstanding=0;
void request(Json value,bool quiet=false,bool exit_after=false){
 {std::lock_guard lock(queue_mutex);if(stopping)return;if(quiet&&status_queued)return;
  if(quiet)status_queued=true;else{++outstanding;set_text(error_label,L"");}
  // Coalesce slider edits that haven't been sent yet, keeping the newest intent.
  if(string_value(value,"command")=="control")for(auto it=queue.begin();it!=queue.end();++it)if(string_value(it->value,"command")=="control"&&string_value(it->value,"key")==string_value(value,"key")&&string_value(it->value,"device_id")==string_value(value,"device_id")){it->value=std::move(value);--outstanding;return;}
  queue.push_back({std::move(value),quiet,exit_after});}
 queue_cv.notify_one();
}
void start_worker(){worker=std::thread([]{
 try{auto result=invoke(L"ensure");if(result.contains("ok")&&!boolean(result,"ok"))throw std::runtime_error(string_value(result,"error","Background agent could not start"));}catch(const std::exception&e){auto* response=new Response{Json{{"ok",false},{"error",e.what()}},true,false,true};if(!PostMessageW(window,response_message,0,reinterpret_cast<LPARAM>(response)))delete response;}
 for(;;){Request next;{std::unique_lock lock(queue_mutex);queue_cv.wait(lock,[]{return stopping||!queue.empty();});if(stopping)break;next=std::move(queue.front());queue.pop_front();}
  Json result;bool transport_failure=false;try{if(string_value(next.value,"command")=="_ensure"){result=invoke(L"ensure");if(result.contains("ok")&&!boolean(result,"ok"))throw std::runtime_error(string_value(result,"error","Background agent could not start"));next.value={{"command","status"}};}result=invoke(L"request",&next.value);}catch(const std::exception&e){transport_failure=true;result={{"ok",false},{"error",e.what()}};}
  transport_failure=transport_failure||(string_value(next.value,"command")=="status"&&!boolean(result,"ok"));
  auto* response=new Response{std::move(result),next.quiet,next.exit_after,transport_failure};if(!PostMessageW(window,response_message,0,reinterpret_cast<LPARAM>(response)))delete response;
 }
});}
void control(const char* key,double value){request({{"command","control"},{"key",key},{"value",value}});}
void device_control(int slot,const char* key,double value){if(slot>=0&&slot<2&&!card_id[slot].empty())request({{"command","control"},{"device_id",card_id[slot]},{"key",key},{"value",value}});}
void set_role(int slot,bool input){if(slot>=0&&slot<2&&!card_id[slot].empty()&&card_id[slot]!=string_value(state,input?"mic_device_id":"hub_device_id"))request({{"command","set_role"},{"role",input?"input":"output"},{"device_id",card_id[slot]}});}
HWND element(const wchar_t* cls,const wchar_t* caption,DWORD style,int x,int y,int width,int height,int id){
 int page=building_page>0?building_page:0;auto parent=page?panels[page]:window;
 if(page)y-=40;
 auto h=CreateWindowExW(0,cls,caption,WS_CHILD|WS_VISIBLE|WS_CLIPSIBLINGS|style,px(x),px(y),px(width),px(height),parent,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),nullptr,nullptr);
 control_message(h,WM_SETFONT,reinterpret_cast<WPARAM>(ui_font),TRUE);if(building_page>=0)pages[building_page].push_back(h);
 layout_items.push_back({h,x,y,width,height,page});if(style&WS_TABSTOP)SetWindowSubclass(h,focus_proc,1,0);return h;
}
HICON audio_icon(bool headphones,bool blocked,bool crowned=false){
 int size=px(crowned?32:28);BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=size;info.bmiHeader.biHeight=-size;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
 void* raw=nullptr;HDC dc=CreateCompatibleDC(nullptr);auto bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&raw,nullptr,0);if(!dc||!bitmap||!raw){if(bitmap)DeleteObject(bitmap);if(dc)DeleteDC(dc);return nullptr;}
 std::fill_n(static_cast<DWORD*>(raw),size*size,0);auto previous=SelectObject(dc,bitmap);SetMapMode(dc,MM_ANISOTROPIC);SetWindowExtEx(dc,28,28,nullptr);SetViewportExtEx(dc,size,size,nullptr);
 auto ink=blocked?(crowned?RGB(30,110,220):RGB(190,45,50)):GetSysColor(COLOR_BTNTEXT);if(!ink)ink=RGB(1,1,1);auto pen=CreatePen(PS_SOLID,2,ink);auto oldPen=SelectObject(dc,pen),oldBrush=SelectObject(dc,GetStockObject(NULL_BRUSH));
 if(crowned){POINT crown[]{{8,7},{6,1},{11,4},{14,1},{17,4},{22,1},{20,7},{8,7}};Polyline(dc,crown,8);SetViewportExtEx(dc,px(24),px(24),nullptr);SetViewportOrgEx(dc,px(4),px(10),nullptr);}
 if(headphones){MoveToEx(dc,5,15,nullptr);POINT arc[]{{5,1},{23,1},{23,15}};PolyBezierTo(dc,arc,3);RoundRect(dc,3,13,9,23,4,4);RoundRect(dc,19,13,25,23,4,4);}
 else{RoundRect(dc,10,3,18,17,7,7);MoveToEx(dc,6,12,nullptr);POINT arc[]{{6,24},{22,24},{22,12}};PolyBezierTo(dc,arc,3);MoveToEx(dc,14,21,nullptr);LineTo(dc,14,25);MoveToEx(dc,9,25,nullptr);LineTo(dc,19,25);}
 if(blocked&&!crowned){MoveToEx(dc,3,25,nullptr);LineTo(dc,25,3);}
 GdiFlush();auto pixels=static_cast<DWORD*>(raw);for(int i=0;i<size*size;++i)if(pixels[i]&0xffffff)pixels[i]|=0xff000000;
 SelectObject(dc,oldBrush);SelectObject(dc,oldPen);SelectObject(dc,previous);DeleteObject(pen);DeleteDC(dc);
 auto mask=CreateBitmap(size,size,1,1,nullptr);HDC maskDC=CreateCompatibleDC(nullptr);auto oldMask=SelectObject(maskDC,mask);RECT area{0,0,size,size};FillRect(maskDC,&area,static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));SelectObject(maskDC,oldMask);DeleteDC(maskDC);
 ICONINFO icon{};icon.fIcon=TRUE;icon.hbmColor=bitmap;icon.hbmMask=mask;auto result=CreateIconIndirect(&icon);DeleteObject(bitmap);DeleteObject(mask);return result;
}
void audio_tooltip(HWND button){TOOLINFOW tip{};tip.cbSize=sizeof tip;tip.uFlags=TTF_IDISHWND|TTF_SUBCLASS;tip.hwnd=window;tip.uId=reinterpret_cast<UINT_PTR>(button);tip.lpszText=LPSTR_TEXTCALLBACKW;control_message(audio_tooltips,TTM_ADDTOOLW,0,reinterpret_cast<LPARAM>(&tip));}
HWND combo(int x,int y,int width,int id){return element(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_TABSTOP|WS_VSCROLL,x,y,width,220,id);}
HWND slider(int y,int id,int position){auto h=element(TRACKBAR_CLASSW,L"",TBS_NOTICKS|WS_TABSTOP,170,y,390,28,id);control_message(h,TBM_SETRANGE,TRUE,MAKELONG(0,100));control_message(h,TBM_SETPOS,TRUE,position);mix_controls.push_back(h);return h;}
void sync_bridge_animation(){
 BOOL effects=TRUE;SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION,0,&effects,0);
 bool animate=window&&agent_available&&connected&&IsWindowVisible(window)&&!IsIconic(window)&&effects;
 if(animate&&!bridge_timer_running)bridge_timer_running=SetTimer(window,bridge_timer,33,nullptr)!=0;
 else if(!animate&&bridge_timer_running){KillTimer(window,bridge_timer);bridge_timer_running=false;}
}
void show_page(){for(auto h:{playback_box,micoutput_box,playback_label,micoutput_label})ShowWindow(h,advanced?SW_SHOW:SW_HIDE);sync_bridge_animation();}
std::string selected_id(HWND box,const std::vector<std::string>& ids){auto i=control_message(box,CB_GETCURSEL,0,0);return i>=0&&static_cast<size_t>(i)<ids.size()?ids[static_cast<size_t>(i)]:"";}
Json selected_device(){for(const auto& d:devices)if(string_value(d,"id")==configured_id)return d;return Json::object();}
bool is_virtual(const Json& d);
void populate_audio(){auto d=selected_device();if(d.empty())return;auto stable=d;stable.erase("diagnostics");stable.erase("inventory_revision");stable.erase("mix");auto signature=stable.dump()+std::to_string(number(state.value("settings",Json::object()),"primary"));if(device_dirty||signature==applied_device_signature)return;applied_device_signature=signature;
 HWND boxes[]{headphones_box,microphone_box,playback_box,micoutput_box};const char* keys[]{"headphones_output","physical_microphone","playback_input","microphone_output"};
 for(int i=0;i<4;++i){control_message(boxes[i],CB_RESETCONTENT,0,0);audio_ids[i]={""};control_message(boxes[i],CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"Not selected"));control_message(boxes[i],CB_SETCURSEL,0,0);}
 if(d.contains("audio_devices")&&d["audio_devices"].is_array())for(const auto& audio:d["audio_devices"]){if(!audio.is_object())continue;for(int i=0;i<4;++i){const bool input=i==1||i==2;if(number(audio,input?"inputs":"outputs")<=0)continue;if(i<2&&is_virtual(audio))continue;auto id=string_value(audio,"id");auto name=utf16(string_value(audio,"name",id));audio_ids[i].push_back(id);auto pos=control_message(boxes[i],CB_ADDSTRING,0,reinterpret_cast<LPARAM>(name.c_str()));if(id==string_value(d,keys[i]))control_message(boxes[i],CB_SETCURSEL,pos,0);}}
 auto settings=state.value("settings",Json::object());const bool hub=string_value(d,"id")==string_value(state,"hub_device_id");
 set_text(setup_label,utf16(std::string(hub?"Output Hub · mixed audio plays here\n":"Audio is sent to the Output Hub\n")+string_value(d,"setup_status","Checking components…")).c_str());
 enable_control(apply_button,FALSE);for(auto h:boxes)enable_control(h,boolean(d,"online"));
}
bool is_virtual(const Json& d){auto name=string_value(d,"name");std::transform(name.begin(),name.end(),name.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});return name.find("blackhole")!=name.npos||name.find("cable")!=name.npos||name.find("streaming")!=name.npos;}
Json slot_device(int slot){for(auto& d:devices)if(number(d,"slot",-1)==slot)return d;return Json::object();}
void update_cards(){auto settings=state.value("settings",Json::object());
 for(int slot=0;slot<2;++slot){auto d=slot_device(slot);card_id[slot]=string_value(d,"id");bool online=agent_available&&!d.empty()&&boolean(d,"online")&&(card_id[slot]==string_value(state,"device_id")||connected);
  auto name=device_name(d);set_text(card_name[slot],utf16(name).c_str());
  set_text(card_status[slot],online?(card_id[slot]==string_value(state,"device_id")?L"This computer · available":L"Connected"):agent_available?L"Offline · Waiting for connection":L"App connection unavailable");
  control_message(card_hub[slot],BM_SETCHECK,paired&&string_value(state,"hub_device_id")==card_id[slot]&&!card_id[slot].empty()?BST_CHECKED:BST_UNCHECKED,0);control_message(card_source[slot],BM_SETCHECK,paired&&string_value(state,"mic_device_id")==card_id[slot]&&!card_id[slot].empty()?BST_CHECKED:BST_UNCHECKED,0);
  set_text(card_hub[slot],utf16(paired&&string_value(state,"hub_device_id")==card_id[slot]&&!card_id[slot].empty()?"Output Hub · "+name:"Use "+name+" as Output Hub").c_str());set_text(card_source[slot],utf16(paired&&string_value(state,"mic_device_id")==card_id[slot]&&!card_id[slot].empty()?"Input Hub · "+name:"Use "+name+" as Input Hub").c_str());
  auto hasEndpoint=[&](bool input){auto id=string_value(d,input?"physical_microphone":"headphones_output");if(d.contains("audio_devices"))for(auto& a:d["audio_devices"])if(string_value(a,"id")==id&&!is_virtual(a)&&number(a,input?"inputs":"outputs")>0)return true;return false;};
  enable_control(card_hub[slot],room_ready()&&online&&(string_value(state,"hub_device_id")==card_id[slot]||hasEndpoint(false)));enable_control(card_source[slot],room_ready()&&online&&(string_value(state,"mic_device_id")==card_id[slot]||hasEndpoint(true)));
  auto stable=d;stable.erase("diagnostics");stable.erase("inventory_revision");stable.erase("mix");auto signature=stable.dump();
  if(signature!=card_signature[slot]){card_signature[slot]=signature;HWND boxes[]{card_headphones[slot],card_microphone[slot]};const char* keys[]{"headphones_output","physical_microphone"};
   for(int route=0;route<2;++route){auto box=boxes[route];control_message(box,CB_RESETCONTENT,0,0);card_routes[slot][route]={""};control_message(box,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"Not selected"));control_message(box,CB_SETCURSEL,0,0);
    if(d.contains("audio_devices"))for(auto& audio:d["audio_devices"]){if(is_virtual(audio)||number(audio,route?"inputs":"outputs")<=0)continue;auto id=string_value(audio,"id");auto label=utf16(string_value(audio,"name",id));card_routes[slot][route].push_back(id);auto pos=control_message(box,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));if(id==string_value(d,keys[route]))control_message(box,CB_SETCURSEL,pos,0);}
   }
  }
  enable_control(card_headphones[slot],online);enable_control(card_microphone[slot],online);
  for(int kind=0;kind<2;++kind){auto button=kind?(slot?second_deafen:first_deafen):(slot?second_mute:first_mute);auto mix=d.value("mix",Json::object());
   bool inherited=boolean(settings,"deafen")||(!kind&&(boolean(settings,"mute")||boolean(mix,"deafened")));
   bool active=inherited||boolean(mix,kind?"deafened":"mic_muted");
   control_message(button,BM_SETCHECK,active?BST_CHECKED:BST_UNCHECKED,0);
   control_message(button,BM_SETIMAGE,IMAGE_ICON,reinterpret_cast<LPARAM>(audio_icons[kind][active?1:0]));
   auto label=(kind?(active?std::string("Undeafen "):std::string("Deafen ")):(active?std::string("Unmute microphone on "):std::string("Mute microphone on ")))+name;
   if(inherited)label=(kind?std::string("Deafened "):std::string("Microphone muted on "))+name+": "+(boolean(settings,"deafen")?"Deafen is on in Global audio. Turn it off there first.":boolean(settings,"mute")?"Microphone is muted in Global audio. Unmute it there first.":"This computer is deafened. Undeafen it first.");
   set_text(button,utf16(label).c_str());enable_control(button,controls_ready()&&online&&!inherited);
  }
  for(int kind=0;kind<2;++kind){bool selected=paired&&!card_id[slot].empty()&&string_value(state,kind?"hub_device_id":"mic_device_id")==card_id[slot];control_message(kind?card_hub[slot]:card_source[slot],BM_SETIMAGE,IMAGE_ICON,reinterpret_cast<LPARAM>(hub_icons[kind][selected?1:0]));}
  auto percent=std::to_string(static_cast<int>(std::lround(number(d.value("mix",Json::object()),"volume",1)*100)));set_text(GetDlgItem(window,780+slot),utf16("Audio from this computer · "+percent+"%").c_str());
  auto volume=slot?second_volume:first_volume;enable_control(volume,controls_ready()&&online);if(GetCapture()!=volume)control_message(volume,TBM_SETPOS,TRUE,static_cast<LPARAM>(number(d.value("mix",Json::object()),"volume",1)*100));
 }
 Json graphic={{"available",agent_available},{"connected",connected}};
 for(int slot=0;slot<2;++slot){auto d=slot_device(slot);graphic[std::to_string(slot)]={{"online",boolean(d,"online")&&(string_value(d,"id")==string_value(state,"device_id")||connected)},{"kind",string_value(d,"kind")}};}
 static Json previous_graphic;
 if(graphic!=previous_graphic){previous_graphic=graphic;InvalidateRect(GetDlgItem(window,760),nullptr,FALSE);}
 set_text(link_status,!agent_available?L"App unavailable":connected?L"Connected":paired?L"Reconnecting…":L"Not connected");sync_bridge_animation();
}
void draw_computers(HDC dc){
 const COLORREF blue=RGB(30,110,220),gray=RGB(155,163,173);
 auto pen=CreatePen(PS_SOLID,2,connected&&agent_available?RGB(191,66,43):gray);auto old=SelectObject(dc,pen);
 auto point=[](int x,int y){return POINT{355+x*3/2,143+y};};
 auto line=[&](int x,int y,int xx,int yy){auto a=point(x,y),b=point(xx,yy);MoveToEx(dc,a.x,a.y,nullptr);LineTo(dc,b.x,b.y);};
 auto cable=[](int x){double u=x<45?(x-2)/43.0:x>115?(158-x)/43.0:(x-80)/35.0;return 12+static_cast<int>((x<45||x>115?42:32)*(1-u*u));};
 line(2,58,158,58);line(2,66,158,66);
 for(int x=4;x<=158;x+=2)line(x-2,cable(x-2),x,cable(x));
 for(int x=10;x<=150;x+=10)line(x,cable(x),x,58);
 for(int x:{45,115}){line(x-4,10,x-4,73);line(x+4,10,x+4,73);for(int y:{17,29,41,53,68})line(x-4,y,x+4,y);line(x-7,73,x+7,73);}
 SelectObject(dc,old);DeleteObject(pen);
 if(connected&&agent_available){
  BOOL effects=TRUE;SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION,0,&effects,0);
  double travel=effects?GetTickCount64()/6000.0:0;
  constexpr COLORREF colors[]{RGB(70,205,235),RGB(102,224,169),RGB(249,211,88),RGB(185,154,247),RGB(108,163,255),RGB(233,240,247)};
  auto previousBrush=SelectObject(dc,GetStockObject(DC_BRUSH)),previousPen=SelectObject(dc,GetStockObject(DC_PEN));
  auto oldBrushColor=SetDCBrushColor(dc,colors[0]),oldPenColor=SetDCPenColor(dc,RGB(31,46,61));
  int saved=SaveDC(dc);IntersectClipRect(dc,358,202,592,209);
  for(int lane=0;lane<2;++lane)for(int dot=0;dot<3;++dot){
   double journey=travel+dot/3.0,progress=std::fmod(journey,1.0);
   SetDCBrushColor(dc,colors[(lane*3+dot+static_cast<unsigned long long>(journey))%6]);
   int x=2+static_cast<int>(156*(lane==0?progress:1-progress));auto a=point(x-3,59+lane*4),b=point(x+4,62+lane*4);RoundRect(dc,a.x,a.y,b.x,b.y,2,2);
  }
  RestoreDC(dc,saved);SetDCBrushColor(dc,oldBrushColor);SetDCPenColor(dc,oldPenColor);SelectObject(dc,previousBrush);SelectObject(dc,previousPen);
 }
 for(int slot=0;slot<2;++slot){auto d=slot_device(slot);bool online=agent_available&&!d.empty()&&boolean(d,"online")&&(string_value(d,"id")==string_value(state,"device_id")||connected);int x=slot?655:185;auto outline=CreatePen(PS_SOLID,3,online?blue:gray);auto fill=CreateSolidBrush(online?RGB(232,242,255):RGB(240,242,245));auto previousPen=SelectObject(dc,outline),previousBrush=SelectObject(dc,fill);
  auto kind=string_value(d,"kind","desktop");
  if(kind=="server"){
   for(int row=0;row<3;++row){int y=150+row*25;RoundRect(dc,x+15,y,x+95,y+21,5,5);Ellipse(dc,x+23,y+7,x+29,y+13);MoveToEx(dc,x+40,y+10,nullptr);LineTo(dc,x+84,y+10);}
  }else{
   RoundRect(dc,x,148,x+110,214,9,9);
   if(kind=="laptop"){POINT base[]{{x,214},{x+110,214},{x+124,224},{x+116,228},{x-6,228},{x-14,224}};Polygon(dc,base,6);MoveToEx(dc,x+43,219,nullptr);LineTo(dc,x+67,219);}
   else{MoveToEx(dc,x+55,214,nullptr);LineTo(dc,x+55,227);MoveToEx(dc,x+30,227,nullptr);LineTo(dc,x+80,227);}
  }
  SelectObject(dc,previousPen);SelectObject(dc,previousBrush);DeleteObject(outline);DeleteObject(fill);
 }
}
std::wstring slot_name(int slot){return utf16(device_name(slot_device(slot)));}
void fill_roles(){for(auto box:{hub_box,mic_source}){control_message(box,CB_RESETCONTENT,0,0);for(int slot=0;slot<2;++slot){auto name=slot_name(slot);control_message(box,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(name.c_str()));}}}
void apply_state(const Json& s){updating=true;if(!agent_available){set_text(error_label,L"");applied_device_signature.clear();}agent_available=true;state=s;paired=boolean(s,"paired");connected=boolean(s,"connected");running=boolean(s,"running");wanted_running=boolean(s,"wanted_running",running);
 enable_control(autoconfigure_button,TRUE);
 auto incoming_name=string_value(s,"name","This computer");if(text(name_edit).empty()||utf8(text(name_edit))==last_local_name)set_text(name_edit,utf16(incoming_name).c_str());last_local_name=incoming_name;
 auto invite=string_value(s,"invite_code");if(invite.size()==8)invite.insert(4,"-");if(!invite.empty())invite+="   ("+std::to_string(static_cast<int>(number(s,"invite_expires_seconds")))+" seconds remaining)";
 else invite=paired?"Pairing remembered. Both apps control this room.":"Create a room here or enter the other computer's code.";
 set_text(invite_label,utf16(invite).c_str());set_text(status_label,utf16(string_value(s,"status",connected?"Connected":"Waiting for the other computer")).c_str());
 set_text(mute_reason,utf16(string_value(s,"mute_reason")).c_str());set_text(run_button,(running||wanted_running)?L"Stop audio":L"Start audio");enable_control(run_button,running||wanted_running||room_ready());
 static bool upgrade_banner=false;bool incompatible=connected&&!boolean(s,"compatible");if(incompatible)set_text(error_label,L"Update San Audisco on the other computer before starting audio.");else if(upgrade_banner)set_text(error_label,L"");upgrade_banner=incompatible;
 enable_control(create_button,!paired);enable_control(join_button,!paired);enable_control(code_edit,!paired);enable_control(forget_button,paired||!string_value(s,"invite_code").empty());
 auto old=devices;devices.clear();if(s.contains("devices")&&s["devices"].is_array())for(const auto& d:s["devices"])if(d.is_object())devices.push_back(d);
 std::vector<std::string> old_ids,new_ids;for(const auto& d:old)old_ids.push_back(string_value(d,"id")+string_value(d,"name"));for(const auto& d:devices)new_ids.push_back(string_value(d,"id")+string_value(d,"name"));
 if(old_ids!=new_ids){control_message(computer_box,CB_RESETCONTENT,0,0);int selection=0;for(size_t i=0;i<devices.size();++i){auto name=utf16(device_name(devices[i]));if(string_value(devices[i],"id")==string_value(s,"device_id"))name+=L" (this computer)";control_message(computer_box,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(name.c_str()));if(string_value(devices[i],"id")==configured_id)selection=static_cast<int>(i);}control_message(computer_box,CB_SETCURSEL,selection,0);if(!devices.empty())configured_id=string_value(devices[selection],"id");applied_device_signature.clear();fill_roles();}
 for(const auto& d:devices)if(string_value(d,"id")==string_value(state,"device_id")){auto kind=string_value(d,"kind_override","auto");int selected=kind=="laptop"?1:kind=="desktop"?2:kind=="server"?3:0;control_message(kind_box,CB_SETCURSEL,selected,0);}
 populate_audio();
 auto new_nearby=s.value("discovered",Json::array());Json old_nearby=nearby;if(new_nearby.is_array()&&new_nearby!=old_nearby){nearby.clear();control_message(nearby_box,CB_RESETCONTENT,0,0);for(const auto& d:new_nearby)if(d.is_object()){nearby.push_back(d);auto name=utf16(string_value(d,"name","Nearby room")+" · "+string_value(d,"address"));control_message(nearby_box,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(name.c_str()));}}
 const auto settings=s.value("settings",Json::object());
 if(settings.is_object()){
  control_message(hub_box,CB_SETCURSEL,number(settings,"primary"),0);control_message(mic_source,CB_SETCURSEL,number(settings,"mic_source",1),0);
  for(auto item:{std::pair{mute,"mute"},std::pair{deafen,"deafen"}})control_message(item.first,BM_SETCHECK,boolean(settings,item.second)?BST_CHECKED:BST_UNCHECKED,0);
  for(auto item:{std::pair{master,"master"}})if(GetCapture()!=item.first)control_message(item.first,TBM_SETPOS,TRUE,static_cast<LPARAM>(number(settings,item.second,1)*100));
  if(GetCapture()!=balance)control_message(balance,TBM_SETPOS,TRUE,static_cast<LPARAM>((number(settings,"balance")+1)*50));
 }
 set_text(first_label,slot_name(0).c_str());set_text(second_label,slot_name(1).c_str());set_text(balance_label,(slot_name(0)+L" / "+slot_name(1)).c_str());for(auto h:mix_controls)enable_control(h,controls_ready());
 for(int kind=0;kind<2;++kind){auto button=kind?deafen:mute;bool inherited=!kind&&boolean(settings,"deafen"),active=inherited||boolean(settings,kind?"deafen":"mute");
  control_message(button,BM_SETCHECK,active?BST_CHECKED:BST_UNCHECKED,0);control_message(button,BM_SETIMAGE,IMAGE_ICON,reinterpret_cast<LPARAM>(audio_icons[kind][active?1:0]));
  set_text(button,inherited?L"Microphone blocked by global deafen. Undeafen both first.":kind?(active?L"Undeafen both computers":L"Deafen both computers"):(active?L"Unmute microphone everywhere":L"Mute microphone everywhere"));enable_control(button,controls_ready()&&!inherited);
 }
 std::wstring checks;
 for(const auto& d:devices){checks+=utf16(device_name(d))+L" — "+(boolean(d,"online")?L"Online":L"Offline")+L"\r\n";checks+=utf16(string_value(d,"setup_status"))+L"\r\n\r\n";}
 auto diagnostic=s.value("diagnostics",Json::object());if(diagnostic.is_object()){
  checks+=utf16(string_value(diagnostic,"message"))+L"\r\n";
  if(boolean(diagnostic,"muted")&&!boolean(settings,"mute"))checks+=L"Microphone blocked by audio readiness: resolve the inactive route.\r\n";
  for(auto item:{std::pair{"playback_ready",L"Playback capture"},std::pair{"mic_sink_ready",L"Shared microphone output"},std::pair{"headphones_ready",L"Headphone output"},std::pair{"microphone_ready",L"Physical microphone"}})if(diagnostic.contains(item.first))checks+=std::wstring(item.second)+L": "+(boolean(diagnostic,item.first)?L"Ready":L"Not active")+L"\r\n";
 }
 checks+=L"\r\nA device can be inactive when the other computer owns that role.\r\nNo microphone recordings are saved by these checks.";
 for(int slot=0;slot<2;++slot){
  auto d=slot_device(slot);if(d.empty()){tone_ids[slot].clear();set_text(signal_labels[slot],L"Other computer not connected");enable_control(tone_buttons[slot],FALSE);for(auto h:signal_bars[slot])control_message(h,PBM_SETPOS,0,0);continue;}
  tone_ids[slot]=string_value(d,"id");set_text(signal_labels[slot],utf16(device_name(d)).c_str());
  auto levels=tone_ids[slot]==string_value(s,"device_id")?diagnostic:d.value("diagnostics",Json::object());
  const char* keys[]{"playback_level","microphone_level","microphone_delivery_level"};
  for(int column=0;column<3;++column){double level=number(levels,keys[column]);if(!std::isfinite(level))level=0;double meter=level>0?std::clamp((20*std::log10(level)+60)/60,0.0,1.0):0;control_message(signal_bars[slot][column],PBM_SETPOS,static_cast<WPARAM>(meter*100),0);}
  enable_control(tone_buttons[slot],running&&boolean(d,"online"));
 }
 set_text(diagnostics_box,checks.c_str());
 std::wstring routeText;
 for(const auto& d:devices){routeText+=utf16(device_name(d))+L": ";
  auto label=[&](const char* key){auto id=string_value(d,key);if(d.contains("audio_devices"))for(auto&a:d["audio_devices"])if(string_value(a,"id")==id)return utf16(string_value(a,"name",id));return std::wstring(L"run Autoconfigure");};
  routeText+=L"app output → "+label("app_playback_output")+L"; microphone → "+label("app_microphone_input")+L"\r\n";
 }
 set_text(routing_help,routeText.c_str());update_cards();updating=false;
}
int window_page(HWND w){if(w==window)return 0;return static_cast<int>(GetWindowLongPtrW(w,GWLP_USERDATA));}
int window_dpi(HWND w){
#ifdef BRIDGE_UI_TEST
 if(test_dpi)return test_dpi;
#endif
 auto fn=reinterpret_cast<UINT(WINAPI*)(HWND)>(GetProcAddress(GetModuleHandleW(L"user32.dll"),"GetDpiForWindow"));return fn?static_cast<int>(fn(w)):ui_dpi;}
std::map<int,std::array<HFONT,3>> dpi_fonts;
const std::array<HFONT,3>& fonts_for(int dpi){auto it=dpi_fonts.find(dpi);if(it!=dpi_fonts.end())return it->second;std::array<HFONT,3> f{};for(int i=0;i<3;++i)f[i]=CreateFontW(-MulDiv(i==0?16:i==1?27:20,dpi,96),0,0,0,i?FW_SEMIBOLD:FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");return dpi_fonts.emplace(dpi,f).first->second;}
void position(HWND h,int x,int y,int width,int height){for(auto&item:layout_items)if(item.handle==h){item.x=x;item.y=y-(item.page?40:0);item.width=width;item.height=height;break;}SetWindowPos(h,nullptr,px(x),px(y),px(width),px(height),SWP_NOZORDER);}
void layout_window(HWND target){
 if(!target||laying_out)return;laying_out=true;
 int page=window_page(target),dpi=window_dpi(target);if(dpi<=0)dpi=ui_dpi;
 RECT client{};GetClientRect(target,&client);int width=std::max(page?840:900,MulDiv(client.right,96,dpi));int contentHeight=page?540:748;
 auto scale=[&](int v){return MulDiv(v,dpi,96);};
 SCROLLINFO vertical{sizeof(vertical),SIF_RANGE|SIF_PAGE|SIF_POS,0,scale(contentHeight)-1,static_cast<UINT>(client.bottom),scroll_y[page],0};SetScrollInfo(target,SB_VERT,&vertical,TRUE);vertical.fMask=SIF_POS;GetScrollInfo(target,SB_VERT,&vertical);scroll_y[page]=vertical.nPos;
 SCROLLINFO horizontal{sizeof(horizontal),SIF_RANGE|SIF_PAGE|SIF_POS,0,scale(width)-1,static_cast<UINT>(client.right),scroll_x[page],0};SetScrollInfo(target,SB_HORZ,&horizontal,TRUE);horizontal.fMask=SIF_POS;GetScrollInfo(target,SB_HORZ,&horizontal);scroll_x[page]=horizontal.nPos;
 const auto& fonts=fonts_for(dpi);
 HDWP batch=BeginDeferWindowPos(static_cast<int>(layout_items.size()));
 for(auto&item:layout_items){if(item.page!=page||!IsWindow(item.handle))continue;
  int x=item.x,y=item.y,w=item.width,h=item.height,id=GetDlgCtrlID(item.handle);
  if(!page){double ratio=width/970.0;bool fixed=(id==702||id==704||id==722||id==724||id==411||id==412||id==421||id==422||id==440||id==441);x=static_cast<int>(std::lround(x*ratio));w=fixed?w:static_cast<int>(std::lround(w*ratio));}
  else if(width>840){w=static_cast<int>(std::lround(w*width/840.0));x=static_cast<int>(std::lround(x*width/840.0));}
  auto font=fonts[(id==100)?1:(id==700||id==720||(id>=661&&id<=663))?2:0];if(reinterpret_cast<HFONT>(SendMessageW(item.handle,WM_GETFONT,0,0))!=font)SendMessageW(item.handle,WM_SETFONT,reinterpret_cast<WPARAM>(font),FALSE);
  batch=DeferWindowPos(batch,item.handle,nullptr,scale(x)-scroll_x[page],scale(y)-scroll_y[page],scale(w),scale(h),SWP_NOZORDER|SWP_NOACTIVATE);
 }
 if(batch)EndDeferWindowPos(batch);laying_out=false;InvalidateRect(target,nullptr,TRUE);
}
LRESULT CALLBACK focus_proc(HWND control,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR){
 if(message==WM_SETFOCUS){auto parent=GetParent(control);if(IsWindowVisible(parent)){RECT r{},client{};GetWindowRect(control,&r);MapWindowPoints(nullptr,parent,reinterpret_cast<POINT*>(&r),2);GetClientRect(parent,&client);int dx=r.left<8?r.left-8:r.right>client.right-8?r.right-client.right+8:0,dy=r.top<8?r.top-8:r.bottom>client.bottom-8?r.bottom-client.bottom+8:0;
  if(dx||dy){int page=window_page(parent);for(int axis=0;axis<2;++axis){SCROLLINFO info{};info.cbSize=sizeof(info);info.fMask=SIF_POS;int bar=axis?SB_VERT:SB_HORZ;GetScrollInfo(parent,bar,&info);info.nPos+=axis?dy:dx;SetScrollInfo(parent,bar,&info,TRUE);GetScrollInfo(parent,bar,&info);(axis?scroll_y[page]:scroll_x[page])=info.nPos;}layout_window(parent);}
 }}else if(message==WM_NCDESTROY)RemoveWindowSubclass(control,focus_proc,1);
 return DefSubclassProc(control,message,wp,lp);
}
bool scroll_window(HWND target,UINT message,WPARAM wp,LPARAM lp){
 bool vertical=message==WM_VSCROLL||message==WM_MOUSEWHEEL;
 if(message!=WM_MOUSEWHEEL&&((message!=WM_VSCROLL&&message!=WM_HSCROLL)||lp))return false;
 int bar=vertical?SB_VERT:SB_HORZ;SCROLLINFO info{};info.cbSize=sizeof(info);info.fMask=SIF_ALL;GetScrollInfo(target,bar,&info);int position=info.nPos;
 if(message==WM_MOUSEWHEEL)position-=GET_WHEEL_DELTA_WPARAM(wp)*MulDiv(48,window_dpi(target),96)/WHEEL_DELTA;
 else switch(LOWORD(wp)){case SB_LINEUP:position-=32;break;case SB_LINEDOWN:position+=32;break;case SB_PAGEUP:position-=info.nPage;break;case SB_PAGEDOWN:position+=info.nPage;break;case SB_THUMBTRACK:position=info.nTrackPos;break;case SB_TOP:position=info.nMin;break;case SB_BOTTOM:position=info.nMax;break;default:return true;}
 info.fMask=SIF_POS;info.nPos=position;SetScrollInfo(target,bar,&info,TRUE);GetScrollInfo(target,bar,&info);int page=window_page(target);(vertical?scroll_y[page]:scroll_x[page])=info.nPos;layout_window(target);return true;
}
void open_panel(int page){if(page<1||page>3)return;auto target=panels[page];RECT owner{},work{};GetWindowRect(window,&owner);SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0);int dpi=window_dpi(window);int width=std::min(MulDiv(860,dpi,96),int(work.right-work.left-40)),height=std::min(MulDiv(600,dpi,96),int(work.bottom-work.top-40));SetWindowPos(target,nullptr,std::max(int(work.left),int(owner.left+(owner.right-owner.left-width)/2)),std::max(int(work.top),int(owner.top+40)),width,height,SWP_NOZORDER|SWP_NOACTIVATE);ShowWindow(target,SW_SHOWNORMAL);layout_window(target);SetForegroundWindow(target);}
#ifdef BRIDGE_UI_TEST
void capture_test_window(HWND target,int page){
 RECT r{};GetWindowRect(target,&r);int width=r.right-r.left,height=r.bottom-r.top;
 BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
 void* raw=nullptr;auto dc=CreateCompatibleDC(nullptr);auto bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&raw,nullptr,0);if(!dc||!bitmap||!raw)return;
 auto old=SelectObject(dc,bitmap);RECT area{0,0,width,height};FillRect(dc,&area,GetSysColorBrush(COLOR_WINDOW));
 // Render native child controls separately: Wine's WM_PRINT/PRF_CHILDREN
 // does not reliably retain the child clip region when composing a parent.
 for(auto child=GetWindow(target,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT)){
  if(!(GetWindowLongPtrW(child,GWL_STYLE)&WS_VISIBLE))continue;RECT rect{};GetWindowRect(child,&rect);int x=rect.left-r.left,y=rect.top-r.top;
  int saved=SaveDC(dc);IntersectClipRect(dc,x,y,x+rect.right-rect.left,y+rect.bottom-rect.top);SetViewportOrgEx(dc,x,y,nullptr);
  wchar_t cls[64]{};GetClassNameW(child,cls,64);int childWidth=rect.right-rect.left,childHeight=rect.bottom-rect.top;
  if(std::wstring(cls)==L"Static"){
   // STATIC does not implement WM_PRINT in this Wine build. Use the real
   // font, text, alignment and rectangle; owner-draw calls its production renderer.
   if((GetWindowLongPtrW(child,GWL_STYLE)&SS_TYPEMASK)==SS_ICON){auto icon=reinterpret_cast<HICON>(SendMessageW(child,STM_GETICON,0,0));if(icon)DrawIconEx(dc,0,0,icon,childWidth,childHeight,0,nullptr,DI_NORMAL);}
   else if((GetWindowLongPtrW(child,GWL_STYLE)&SS_TYPEMASK)==SS_ETCHEDHORZ){auto pen=CreatePen(PS_SOLID,1,GetSysColor(COLOR_3DSHADOW));auto before=SelectObject(dc,pen);MoveToEx(dc,0,0,nullptr);LineTo(dc,childWidth,0);SelectObject(dc,before);DeleteObject(pen);}
   else if(GetDlgCtrlID(child)==760){SetMapMode(dc,MM_ANISOTROPIC);SetWindowExtEx(dc,830,95,nullptr);SetViewportExtEx(dc,childWidth,childHeight,nullptr);SetWindowOrgEx(dc,60,140,nullptr);draw_computers(dc);}
   else{auto font=SendMessageW(child,WM_GETFONT,0,0);if(font)SelectObject(dc,reinterpret_cast<HGDIOBJ>(font));SendMessageW(GetParent(child),WM_CTLCOLORSTATIC,reinterpret_cast<WPARAM>(dc),reinterpret_cast<LPARAM>(child));auto label=text(child);RECT bounds{0,0,childWidth,childHeight};auto style=GetWindowLongPtrW(child,GWL_STYLE);UINT flags=DT_NOPREFIX|DT_WORDBREAK;if((style&SS_TYPEMASK)==SS_CENTER)flags|=DT_CENTER;else if((style&SS_TYPEMASK)==SS_RIGHT)flags|=DT_RIGHT;if(style&SS_ENDELLIPSIS)flags=DT_NOPREFIX|DT_SINGLELINE|DT_END_ELLIPSIS|((style&SS_TYPEMASK)==SS_CENTER?DT_CENTER:0);DrawTextW(dc,label.c_str(),static_cast<int>(label.size()),&bounds,flags);}
  }else SendMessageW(child,WM_PRINT,reinterpret_cast<WPARAM>(dc),PRF_CLIENT|PRF_CHILDREN|PRF_NONCLIENT|PRF_ERASEBKGND);
  RestoreDC(dc,saved);
 }
 GdiFlush();
 BITMAPFILEHEADER header{};header.bfType=0x4d42;header.bfOffBits=sizeof(header)+sizeof(BITMAPINFOHEADER);header.bfSize=header.bfOffBits+width*height*4;
 auto path=std::filesystem::path(executable_path()).parent_path()/("dpi-"+std::to_string(test_dpi)+"-panel-"+std::to_string(page)+".bmp");std::ofstream out(path,std::ios::binary);out.write(reinterpret_cast<const char*>(&header),sizeof(header));out.write(reinterpret_cast<const char*>(&info.bmiHeader),sizeof(BITMAPINFOHEADER));out.write(static_cast<const char*>(raw),width*height*4);
 SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);
}
#endif
LRESULT CALLBACK panel_proc(HWND h,UINT message,WPARAM wp,LPARAM lp){
 if(message==WM_NCCREATE){auto data=reinterpret_cast<CREATESTRUCTW*>(lp);SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(data->lpCreateParams));}
 if(scroll_window(h,message,wp,lp))return 0;
 switch(message){case WM_COMMAND:case WM_NOTIFY:case WM_HSCROLL:case WM_CTLCOLORSTATIC:return SendMessageW(window,message,wp,lp);
 case WM_CLOSE:ShowWindow(h,SW_HIDE);SetForegroundWindow(window);return 0;
 case WM_SIZE:layout_window(h);return 0;
 case WM_DPICHANGED:{auto r=reinterpret_cast<RECT*>(lp);SetWindowPos(h,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);layout_window(h);return 0;}
 }
 return DefWindowProcW(h,message,wp,lp);
}
void startup(bool enabled){HKEY raw{};if(RegCreateKeyExW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",0,nullptr,0,KEY_SET_VALUE,nullptr,&raw,nullptr)!=ERROR_SUCCESS)throw std::runtime_error("Cannot update your login setting");struct Key{HKEY k;~Key(){RegCloseKey(k);}}key{raw};LONG result;
 if(enabled){auto value=L"\""+executable_path()+L"\" --background";result=RegSetValueExW(raw,L"San Audisco",0,REG_SZ,reinterpret_cast<const BYTE*>(value.c_str()),static_cast<DWORD>((value.size()+1)*sizeof(wchar_t)));}else{result=RegDeleteValueW(raw,L"San Audisco");if(result==ERROR_FILE_NOT_FOUND)result=ERROR_SUCCESS;}
 if(result!=ERROR_SUCCESS)throw std::runtime_error("Could not save your login setting");
}
bool startup_enabled(){wchar_t value[32768];DWORD size=sizeof(value),type=0;if(RegGetValueW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",L"San Audisco",RRF_RT_REG_SZ,&type,value,&size)!=ERROR_SUCCESS)return false;return std::wstring(value)==L"\""+executable_path()+L"\" --background";}
void open_window(){ShowWindow(window,SW_RESTORE);SetForegroundWindow(window);request({{"command","status"}},true);}
void tray_menu(){HMENU menu=CreatePopupMenu();AppendMenuW(menu,MF_STRING,tray_open,L"Open San Audisco");AppendMenuW(menu,MF_STRING,tray_quit,L"Quit San Audisco");POINT point;GetCursorPos(&point);SetForegroundWindow(window);auto command=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,point.x,point.y,0,window,nullptr);DestroyMenu(menu);if(command==tray_open)open_window();else if(command==tray_quit){quitting=true;request({{"command","quit"}},false,true);}}
void choose_configured_device(){auto selected=control_message(computer_box,CB_GETCURSEL,0,0);if(selected>=0&&static_cast<size_t>(selected)<devices.size()){configured_id=string_value(devices[selected],"id");device_dirty=false;applied_device_signature.clear();populate_audio();}}
void apply_devices(){if(configured_id.empty())return;request({{"command","configure"},{"device_id",configured_id},{"headphones_output",selected_id(headphones_box,audio_ids[0])},{"physical_microphone",selected_id(microphone_box,audio_ids[1])},{"playback_input",selected_id(playback_box,audio_ids[2])},{"microphone_output",selected_id(micoutput_box,audio_ids[3])}});device_dirty=false;applied_device_signature.clear();}
LRESULT CALLBACK proc(HWND w,UINT msg,WPARAM wp,LPARAM lp){if(scroll_window(w,msg,wp,lp))return 0;switch(msg){
 case WM_CREATE:{window=w;ui_font=CreateFontW(-px(16),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");title_font=CreateFontW(-px(27),0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");heading_font=CreateFontW(-px(20),0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
  WNDCLASSW pane{};pane.lpfnWndProc=panel_proc;pane.hInstance=GetModuleHandleW(nullptr);
#ifdef BRIDGE_UI_TEST
 pane.lpszClassName=L"SanAudiscoTestPanel";
#else
 pane.lpszClassName=L"SanAudiscoPanel";
#endif
 pane.hCursor=LoadCursorW(nullptr,IDC_ARROW);pane.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);pane.hIcon=LoadIconW(pane.hInstance,MAKEINTRESOURCEW(101));RegisterClassW(&pane);
  const wchar_t* panelTitles[]{L"",L"Room settings",L"Advanced audio routes",L"Audio checks"};
  for(int i=1;i<4;++i){panels[i]=CreateWindowExW(WS_EX_CONTROLPARENT,pane.lpszClassName,panelTitles[i],WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN|WS_VSCROLL|WS_HSCROLL,CW_USEDEFAULT,CW_USEDEFAULT,px(860),px(600),w,nullptr,pane.hInstance,reinterpret_cast<void*>(INT_PTR(i)));}
  for(int i=1;i<4;++i){building_page=i;auto brand=element(L"STATIC",L"San Audisco logo",SS_ICON|SS_CENTERIMAGE,38,44,40,40,650+i);control_message(brand,STM_SETICON,reinterpret_cast<WPARAM>(LoadImageW(GetModuleHandleW(nullptr),MAKEINTRESOURCEW(101),IMAGE_ICON,px(40),px(40),LR_SHARED)),0);auto caption=std::wstring(L"San Audisco · ")+panelTitles[i];element(L"STATIC",caption.c_str(),SS_ENDELLIPSIS,92,50,680,30,660+i);}building_page=-1;
  auto menu=GetSystemMenu(w,FALSE);AppendMenuW(menu,MF_SEPARATOR,0,nullptr);AppendMenuW(menu,MF_STRING,0xE100,L"Quit San Audisco");
  auto title=element(L"STATIC",L"San Audisco",0,32,18,300,35,100);
  autoconfigure_button=element(L"BUTTON",L"Autoconfigure",WS_TABSTOP,482,20,150,32,603);control_message(title,WM_SETFONT,reinterpret_cast<WPARAM>(title_font),TRUE);
  element(L"BUTTON",L"Room settings",WS_TABSTOP,644,20,150,32,604);
  element(L"BUTTON",L"Audio checks",WS_TABSTOP,806,20,132,32,605);
  audio_tooltips=CreateWindowExW(WS_EX_TOPMOST,TOOLTIPS_CLASSW,nullptr,WS_POPUP|TTS_ALWAYSTIP,CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,w,nullptr,nullptr,nullptr);
  for(int kind=0;kind<2;++kind)for(int active=0;active<2;++active){audio_icons[kind][active]=audio_icon(kind!=0,active!=0);hub_icons[kind][active]=audio_icon(kind!=0,active!=0,true);}
  building_page=0;
  element(L"STATIC",L"Two computers linked by this room",SS_OWNERDRAW,60,140,830,95,760);
  link_status=element(L"STATIC",L"Connecting…",SS_CENTER,402,238,146,24,740);
  for(int slot=0;slot<2;++slot){int x=slot?530:60;int base=700+slot*20;
   card_name[slot]=element(L"STATIC",L"Other computer",SS_CENTER|SS_ENDELLIPSIS,x+36,244,288,28,base);control_message(card_name[slot],WM_SETFONT,reinterpret_cast<WPARAM>(heading_font),TRUE);
   card_status[slot]=element(L"STATIC",L"Waiting for connection",SS_CENTER,x,276,360,22,base+1);
   card_source[slot]=element(L"BUTTON",L"Use as Input Hub",BS_CHECKBOX|BS_PUSHLIKE|BS_ICON|WS_TABSTOP,x+134,98,40,40,base+4);
   card_hub[slot]=element(L"BUTTON",L"Use as Output Hub",BS_CHECKBOX|BS_PUSHLIKE|BS_ICON|WS_TABSTOP,x+186,98,40,40,base+2);
   audio_tooltip(card_source[slot]);audio_tooltip(card_hub[slot]);
   element(L"STATIC",L"Input",0,x,313,175,22,base+6);card_microphone[slot]=combo(x,337,175,base+5);
   element(L"STATIC",L"Output",0,x+185,313,175,22,base+7);card_headphones[slot]=combo(x+185,337,175,base+3);
   control_message(card_headphones[slot],CB_SETDROPPEDWIDTH,px(380),0);control_message(card_microphone[slot],CB_SETDROPPEDWIDTH,px(380),0);audio_tooltip(card_headphones[slot]);audio_tooltip(card_microphone[slot]);
   element(L"STATIC",L"Audio from this computer",0,x,387,360,22,780+slot);
   auto vol=slider(412,slot?420:410,100);position(vol,x-5,410,370,26);if(slot)second_volume=vol;else first_volume=vol;
   auto m=element(L"BUTTON",L"Mute microphone on this computer",BS_AUTOCHECKBOX|BS_PUSHLIKE|BS_ICON|WS_TABSTOP,x,448,44,38,slot?421:411);
   auto d=element(L"BUTTON",L"Deafen this computer",BS_AUTOCHECKBOX|BS_PUSHLIKE|BS_ICON|WS_TABSTOP,x+52,448,44,38,slot?422:412);
   control_message(m,BM_SETIMAGE,IMAGE_ICON,reinterpret_cast<LPARAM>(audio_icons[0][0]));control_message(d,BM_SETIMAGE,IMAGE_ICON,reinterpret_cast<LPARAM>(audio_icons[1][0]));audio_tooltip(m);audio_tooltip(d);
   mix_controls.push_back(m);mix_controls.push_back(d);if(slot){second_mute=m;second_deafen=d;second_label=card_name[slot];}else{first_mute=m;first_deafen=d;first_label=card_name[slot];}
  }
  element(L"STATIC",L"Connect headphones to the Output Hub. The Input Hub supplies microphone audio to both computers.",0,60,530,830,24,754);
  element(L"BUTTON",L"Advanced audio routes…",WS_TABSTOP,60,494,250,30,606);
  building_page=1;element(L"STATIC",L"This computer",0,38,95,145,22,0);name_edit=element(L"EDIT",L"",WS_BORDER|ES_AUTOHSCROLL|WS_TABSTOP,190,91,425,27,201);element(L"BUTTON",L"Save name",WS_TABSTOP,630,91,160,28,202);
  element(L"STATIC",L"Use the same local network. Create a room here, or join from your other computer.",0,38,138,750,25,0);create_button=element(L"BUTTON",L"Create room",WS_TABSTOP,38,174,155,32,203);invite_label=element(L"STATIC",L"",SS_LEFT,210,180,580,40,0);
  element(L"STATIC",L"Room code",0,38,238,145,22,0);code_edit=element(L"EDIT",L"",WS_BORDER|ES_AUTOHSCROLL|WS_TABSTOP|ES_UPPERCASE,190,233,425,29,204);control_message(code_edit,EM_SETLIMITTEXT,16,0);join_button=element(L"BUTTON",L"Join room",WS_TABSTOP,630,233,160,30,205);
  element(L"STATIC",L"Nearby rooms",0,38,286,145,22,0);nearby_box=combo(190,281,425,206);element(L"BUTTON",L"Find rooms",WS_TABSTOP,630,281,160,30,207);
  element(L"STATIC",L"Manual address",0,38,334,145,22,0);address_edit=element(L"EDIT",L"",WS_BORDER|ES_AUTOHSCROLL|WS_TABSTOP,190,329,600,29,208);control_message(address_edit,EM_SETCUEBANNER,TRUE,reinterpret_cast<LPARAM>(L"Optional: use if discovery is blocked"));
  login_check=element(L"BUTTON",L"Start San Audisco at login",BS_AUTOCHECKBOX|WS_TABSTOP,38,382,380,28,209);control_message(login_check,BM_SETCHECK,startup_enabled()?BST_CHECKED:BST_UNCHECKED,0);
  element(L"STATIC",L"Device icon",0,430,386,120,22,0);kind_box=combo(555,382,235,211);for(auto label:{L"Automatic",L"Laptop",L"Desktop",L"Server"})control_message(kind_box,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label));control_message(kind_box,CB_SETCURSEL,0,0);
  forget_button=element(L"BUTTON",L"Forget this pairing…",WS_TABSTOP,38,430,185,30,210);element(L"STATIC",L"Closing the window keeps audio running.\nOpen or quit San Audisco from its notification-area icon.",0,250,428,540,46,0);
  building_page=2;element(L"STATIC",L"Configure computer",0,38,96,160,22,0);computer_box=combo(215,91,575,301);element(L"STATIC",L"Output",0,38,146,165,22,0);headphones_box=combo(215,141,575,302);element(L"STATIC",L"Input",0,38,196,165,22,0);microphone_box=combo(215,191,575,303);
  setup_label=element(L"STATIC",L"Checking audio components…",0,38,240,750,52,0);advanced_button=element(L"BUTTON",L"Show advanced routes",WS_TABSTOP,38,301,200,30,304);
  playback_label=element(L"STATIC",L"Playback cable capture",0,38,354,175,22,0);playback_box=combo(215,349,575,305);micoutput_label=element(L"STATIC",L"Shared mic output",0,38,400,175,22,0);micoutput_box=combo(215,395,575,306);
  element(L"STATIC",L"Connect AirPods to the Output Hub in Bluetooth settings.\nSan Audisco does not move their Bluetooth connection.",0,38,460,560,44,0);apply_button=element(L"BUTTON",L"Apply devices",WS_TABSTOP,630,461,160,32,307);
  balance_label=element(L"STATIC",L"Mix balance",SS_ENDELLIPSIS,38,530,190,22,308);balance=slider(525,430,50);position(balance,230,525,560,28);
  // Compatibility role controls are hidden: the main screen associates roles with each device.
  building_page=-1;hub_box=combo(0,0,1,401);mic_source=combo(0,0,1,402);ShowWindow(hub_box,SW_HIDE);ShowWindow(mic_source,SW_HIDE);fill_roles();
  building_page=3;element(L"BUTTON",L"Check setup",WS_TABSTOP,38,92,160,30,501);element(L"BUTTON",L"Reconnect background agent",WS_TABSTOP,215,92,245,30,503);diagnostics_box=element(L"EDIT",L"",WS_BORDER|ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL|WS_VSCROLL,38,132,750,110,502);
  element(L"STATIC",L"Application audio",0,210,256,160,20,0);element(L"STATIC",L"Physical microphone",0,385,256,165,20,0);element(L"STATIC",L"Shared microphone",0,560,256,160,20,0);
  for(int slot=0;slot<2;++slot){int y=285+slot*72;signal_labels[slot]=element(L"STATIC",L"Waiting for device",0,38,y,165,24,0);for(int c=0;c<3;++c){signal_bars[slot][c]=element(PROGRESS_CLASSW,L"",0,210+c*175,y,155,18,510+slot*3+c);control_message(signal_bars[slot][c],PBM_SETRANGE,0,MAKELPARAM(0,100));}tone_buttons[slot]=element(L"BUTTON",L"Play 2-second test sound",WS_TABSTOP,210,y+27,245,27,520+slot);enable_control(tone_buttons[slot],FALSE);}
  element(L"STATIC",L"Test sounds play through the Output Hub. Meters show signal activity; listen to confirm.",0,38,430,750,25,0);
  routing_help=element(L"EDIT",L"Selected application routes appear here after setup.",WS_BORDER|ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL|WS_VSCROLL|WS_TABSTOP,38,470,750,80,530);
  building_page=-1;
  element(L"STATIC",L"",SS_ETCHEDHORZ,40,564,880,2,756);
  element(L"STATIC",L"Global audio",0,40,580,200,24,753);
  mute=element(L"BUTTON",L"Mute microphone everywhere",BS_AUTOCHECKBOX|BS_PUSHLIKE|BS_ICON|WS_TABSTOP,40,620,40,40,440);deafen=element(L"BUTTON",L"Deafen both",BS_AUTOCHECKBOX|BS_PUSHLIKE|BS_ICON|WS_TABSTOP,92,620,40,40,441);
  audio_tooltip(mute);audio_tooltip(deafen);
  element(L"STATIC",L"Master volume",0,420,629,130,22,755);master=slider(620,431,100);position(master,555,622,170,28);mix_controls.push_back(mute);mix_controls.push_back(deafen);
  status_label=element(L"STATIC",L"Connecting to background agent…",SS_ENDELLIPSIS,40,674,880,24,750);mute_reason=element(L"STATIC",L"",SS_ENDELLIPSIS,40,704,880,24,751);error_label=element(L"STATIC",L"",SS_LEFT,250,578,670,36,752);
  run_button=element(L"BUTTON",L"Start audio",WS_TABSTOP,745,622,175,34,601);
  audio_tooltip(master);audio_tooltip(run_button);
  for(auto h:mix_controls)enable_control(h,FALSE);for(auto h:card_hub)enable_control(h,FALSE);for(auto h:card_source)enable_control(h,FALSE);enable_control(run_button,FALSE);enable_control(apply_button,FALSE);show_page();layout_window(w);for(int i=1;i<4;++i)layout_window(panels[i]);
  tray.cbSize=sizeof(tray);tray.hWnd=w;tray.uID=1;tray.uFlags=NIF_MESSAGE|NIF_ICON|NIF_TIP;tray.uCallbackMessage=tray_message;tray.hIcon=LoadIconW(GetModuleHandleW(nullptr),MAKEINTRESOURCEW(101));wcscpy_s(tray.szTip,L"San Audisco — audio continues when the window closes");Shell_NotifyIconW(NIM_ADD,&tray);
  enable_control(autoconfigure_button,FALSE);start_worker();request({{"command","status"}},true);SetTimer(w,refresh_timer,1000,nullptr);return 0;}
#ifdef BRIDGE_UI_TEST
 case WM_APP+40:if(wp<4)capture_test_window(wp?panels[wp]:window,static_cast<int>(wp));return 0;
#endif
 case WM_DRAWITEM:{auto item=reinterpret_cast<DRAWITEMSTRUCT*>(lp);if(item->CtlID==760){
  // Buffer this small native graphic so the moving dashes do not flicker.
  int width=item->rcItem.right,height=item->rcItem.bottom;auto buffer=CreateCompatibleDC(item->hDC);auto bitmap=CreateCompatibleBitmap(item->hDC,width,height);
  if(!buffer||!bitmap){if(bitmap)DeleteObject(bitmap);if(buffer)DeleteDC(buffer);return TRUE;}
  auto previous=SelectObject(buffer,bitmap);FillRect(buffer,&item->rcItem,GetSysColorBrush(COLOR_WINDOW));auto saved=SaveDC(buffer);SetMapMode(buffer,MM_ANISOTROPIC);SetWindowExtEx(buffer,96,96,nullptr);SetWindowExtEx(buffer,830,95,nullptr);SetViewportExtEx(buffer,width,height,nullptr);SetWindowOrgEx(buffer,60,140,nullptr);draw_computers(buffer);RestoreDC(buffer,saved);
  BitBlt(item->hDC,0,0,width,height,buffer,0,0,SRCCOPY);SelectObject(buffer,previous);DeleteObject(bitmap);DeleteDC(buffer);return TRUE;}break;}

 case WM_CTLCOLORSTATIC:{auto dc=reinterpret_cast<HDC>(wp);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,reinterpret_cast<HWND>(lp)==error_label?RGB(185,40,40):reinterpret_cast<HWND>(lp)==mute_reason?RGB(150,92,20):RGB(30,36,45));return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));}
 case WM_NOTIFY:if(reinterpret_cast<NMHDR*>(lp)->hwndFrom==audio_tooltips&&reinterpret_cast<NMHDR*>(lp)->code==TTN_GETDISPINFOW){auto tip=reinterpret_cast<NMTTDISPINFOW*>(lp);static wchar_t label[512];GetWindowTextW(reinterpret_cast<HWND>(tip->hdr.idFrom),label,512);auto id=GetDlgCtrlID(reinterpret_cast<HWND>(tip->hdr.idFrom));
 if(id==431)wcscpy_s(label,L"Volume of both computers combined. Does not change microphone volume.");
 if(id==601)wcscpy_s(label,L"Start or stop playback and microphone relay on both connected computers. Your room stays paired.");
 tip->lpszText=label;return 0;}return 0;
 case WM_COMMAND:{if(updating)return 0;const int id=LOWORD(wp),event=HIWORD(wp);
  if(id>=700&&id<=725){int slot=id>=720?1:0;int action=id-(700+slot*20);
   if(action==2)set_role(slot,false);else if(action==4)set_role(slot,true);
   else if((action==3||action==5)&&event==CBN_SELCHANGE&&!card_id[slot].empty()){int route=action==5?1:0;request({{"command","configure"},{"device_id",card_id[slot]},{route?"physical_microphone":"headphones_output",selected_id(route?card_microphone[slot]:card_headphones[slot],card_routes[slot][route])}});card_signature[slot].clear();}
  }
  else if(id==202)request({{"command","configure"},{"device_id",string_value(state,"device_id")},{"name",utf8(text(name_edit))}});
  else if(id==203){Json q={{"command","create"}};auto name=utf8(text(name_edit));if(name!=string_value(state,"name"))q["name"]=name;request(std::move(q));}
  else if(id==205){auto code=utf8(text(code_edit));code.erase(std::remove_if(code.begin(),code.end(),[](unsigned char c){return c=='-'||c==' '||c=='\t'||c=='\r'||c=='\n';}),code.end());for(auto& c:code)if(c>='a'&&c<='z')c=static_cast<char>(c-'a'+'A');if(code.size()!=8||!std::all_of(code.begin(),code.end(),[](unsigned char c){return (c>='A'&&c<='Z')||(c>='0'&&c<='9');}))set_text(error_label,L"Enter the eight letters and numbers shown on the other computer.");else {auto address=utf8(text(address_edit));Json join={{"command","join"},{"code",code},{"address",address}};if(!selected_room_id.empty()&&address==selected_room_address)join["room_id"]=selected_room_id;request(std::move(join));}}
  else if(id==207)request({{"command","discover"}});
  else if(id==206&&event==CBN_SELCHANGE){auto index=control_message(nearby_box,CB_GETCURSEL,0,0);if(index>=0&&static_cast<size_t>(index)<nearby.size()){selected_room_address=string_value(nearby[index],"address");selected_room_id=string_value(nearby[index],"id");set_text(address_edit,utf16(selected_room_address).c_str());}}
  else if(id==209){try{startup(control_message(login_check,BM_GETCHECK,0,0)==BST_CHECKED);}catch(const std::exception&e){set_text(error_label,utf16(e.what()).c_str());control_message(login_check,BM_SETCHECK,startup_enabled()?BST_CHECKED:BST_UNCHECKED,0);}}
  else if(id==211&&event==CBN_SELCHANGE){const char* kinds[]{"auto","laptop","desktop","server"};auto selected=control_message(kind_box,CB_GETCURSEL,0,0);if(selected>=0&&selected<4)request({{"command","device_profile"},{"kind",kinds[selected]}});}
  else if(id==210){if(MessageBoxW(w,L"Audio will stop. You will need a new room code to connect these computers again.",L"Forget this pairing?",MB_OKCANCEL|MB_ICONQUESTION)==IDOK)request({{"command","forget"}});}
  else if(id==301&&event==CBN_SELCHANGE)choose_configured_device();
  else if((id==302||id==303||id==305||id==306)&&event==CBN_SELCHANGE){device_dirty=true;enable_control(apply_button,TRUE);}
  else if(id==304){advanced=!advanced;set_text(advanced_button,advanced?L"Hide advanced routes":L"Show advanced routes");show_page();}
  else if(id==307)apply_devices();
  else if((id==401||id==402)&&event==CBN_SELCHANGE)set_role(static_cast<int>(control_message(id==401?hub_box:mic_source,CB_GETCURSEL,0,0)),id==402);
  else if(id==411)device_control(0,"mute",control_message(first_mute,BM_GETCHECK,0,0)==BST_CHECKED);
  else if(id==412)device_control(0,"deafen",control_message(first_deafen,BM_GETCHECK,0,0)==BST_CHECKED);
  else if(id==421)device_control(1,"mute",control_message(second_mute,BM_GETCHECK,0,0)==BST_CHECKED);
  else if(id==422)device_control(1,"deafen",control_message(second_deafen,BM_GETCHECK,0,0)==BST_CHECKED);
  else if(id==440)control("mute",control_message(mute,BM_GETCHECK,0,0)==BST_CHECKED);
  else if(id==441)control("deafen",control_message(deafen,BM_GETCHECK,0,0)==BST_CHECKED);
  else if(id==503)request({{"command","_ensure"}});
  else if(id==501)request({{"command","diagnose"}});
  else if((id==520||id==521)&&!tone_ids[id-520].empty())request({{"command","test_tone"},{"device_id",tone_ids[id-520]}});
  else if(id==601)request({{"command",(running||wanted_running)?"stop":"start"}});
  else if(id==603)request({{"command","autoconfigure"}});
  else if(id==604)open_panel(1);else if(id==605)open_panel(3);else if(id==606)open_panel(2);
  else if(id==602){quitting=true;request({{"command","quit"}},false,true);}
  return 0;}
 case WM_HSCROLL:{if(!lp)break;if(updating)return 0;auto h=reinterpret_cast<HWND>(lp);double value=static_cast<double>(control_message(h,TBM_GETPOS,0,0))/100;if(h==first_volume||h==second_volume)device_control(h==first_volume?0:1,"volume",value);else control(h==master?"master":"balance",h==balance?value*2-1:value);return 0;}
 case response_message:{std::unique_ptr<Response> result(reinterpret_cast<Response*>(lp));{std::lock_guard lock(queue_mutex);if(result->quiet)status_queued=false;else if(outstanding)--outstanding;}
  if(result->transport_failure){agent_available=false;connected=false;enable_control(autoconfigure_button,FALSE);set_text(status_label,L"Background app unavailable. Audio may still be running; reconnect from Checks.");for(auto h:mix_controls)enable_control(h,FALSE);for(auto h:{run_button,apply_button,headphones_box,microphone_box,playback_box,micoutput_box,create_button,join_button,forget_button})enable_control(h,FALSE);for(auto h:tone_buttons)enable_control(h,FALSE);update_cards();}
  if(result->value.contains("state")&&result->value["state"].is_object())apply_state(result->value["state"]);
  if(!boolean(result->value,"ok")){set_text(error_label,utf16(string_value(result->value,"error","San Audisco could not apply the request")).c_str());if(result->exit_after)quitting=false;}
  else if(result->exit_after){DestroyWindow(w);}
  return 0;}
 case WM_SIZE:layout_window(w);sync_bridge_animation();break;
 case WM_DPICHANGED:{ui_dpi=HIWORD(wp);for(auto& row:audio_icons)for(auto& icon:row){if(icon)DestroyIcon(icon);icon=nullptr;}for(auto& row:hub_icons)for(auto& icon:row){if(icon)DestroyIcon(icon);icon=nullptr;}for(int kind=0;kind<2;++kind)for(int active=0;active<2;++active){audio_icons[kind][active]=audio_icon(kind!=0,active!=0);hub_icons[kind][active]=audio_icon(kind!=0,active!=0,true);}auto r=reinterpret_cast<RECT*>(lp);SetWindowPos(w,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);layout_window(w);apply_state(state);return 0;}
 case WM_SHOWWINDOW:case WM_SETTINGCHANGE:sync_bridge_animation();break;
 case WM_SYSCOMMAND:if((wp&0xFFF0)==0xE100){quitting=true;request({{"command","quit"}},false,true);return 0;}break;
 case WM_TIMER:if(wp==bridge_timer){sync_bridge_animation();if(bridge_timer_running){auto graphic=GetDlgItem(w,760);RECT area{};GetClientRect(graphic,&area);RECT lane{MulDiv(293,area.right,830),MulDiv(59,area.bottom,95),MulDiv(538,area.right,830),MulDiv(71,area.bottom,95)};InvalidateRect(graphic,&lane,FALSE);}return 0;}if(wp!=refresh_timer)return 0;if(IsWindowVisible(w)&&!quitting){std::lock_guard lock(queue_mutex);if(!outstanding&&!status_queued&&!stopping){status_queued=true;queue.push_back({Json{{"command","status"}},true,false});queue_cv.notify_one();}}return 0;
 case tray_message:if(lp==WM_LBUTTONDBLCLK)open_window();else if(lp==WM_RBUTTONUP||lp==WM_CONTEXTMENU)tray_menu();return 0;
 case WM_CLOSE:for(int i=1;i<4;++i)ShowWindow(panels[i],SW_HIDE);ShowWindow(w,SW_HIDE);return 0;
 case WM_ENDSESSION:if(wp)DestroyWindow(w);return 0;
 case WM_DESTROY:for(auto& icons:hub_icons)for(auto icon:icons)if(icon)DestroyIcon(icon);for(auto& icons:audio_icons)for(auto icon:icons)if(icon)DestroyIcon(icon);DeleteObject(ui_font);DeleteObject(title_font);DeleteObject(heading_font);for(auto& entry:dpi_fonts)for(auto font:entry.second)DeleteObject(font);dpi_fonts.clear();KillTimer(w,refresh_timer);KillTimer(w,bridge_timer);Shell_NotifyIconW(NIM_DELETE,&tray);{std::lock_guard lock(queue_mutex);stopping=true;queue.clear();}queue_cv.notify_one();PostQuitMessage(0);return 0;
 }return DefWindowProcW(w,msg,wp,lp);}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR command_line,int show){
#ifdef BRIDGE_UI_TEST
 SetCurrentProcessExplicitAppUserModelID(L"SanAudisco.UITest");
#else
 SetCurrentProcessExplicitAppUserModelID(L"SanAudisco.Desktop");
#endif
 auto user=GetModuleHandleW(L"user32.dll");auto awareness=reinterpret_cast<BOOL(WINAPI*)(HANDLE)>(GetProcAddress(user,"SetProcessDpiAwarenessContext"));if(awareness)awareness(reinterpret_cast<HANDLE>(-4));else SetProcessDPIAware();auto screen=GetDC(nullptr);if(screen){ui_dpi=GetDeviceCaps(screen,LOGPIXELSX);ReleaseDC(nullptr,screen);}
#ifdef BRIDGE_UI_TEST
 if(command_line)if(auto flag=wcsstr(command_line,L"--ui-test-dpi=")){int value=static_cast<int>(wcstol(flag+14,nullptr,10));if(value==96||value==120||value==144||value==192)test_dpi=ui_dpi=value;}
#endif
 INITCOMMONCONTROLSEX cc{sizeof(cc),ICC_BAR_CLASSES|ICC_TAB_CLASSES|ICC_PROGRESS_CLASS};InitCommonControlsEx(&cc);
 WNDCLASSW cls{};cls.lpfnWndProc=proc;cls.hInstance=instance;
#ifdef BRIDGE_UI_TEST
 cls.lpszClassName=L"SanAudiscoTestRoomWindow";
#else
 cls.lpszClassName=L"SanAudiscoRoomWindow";
#endif
 cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);cls.hIcon=LoadIconW(instance,MAKEINTRESOURCEW(101));cls.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);RegisterClassW(&cls);
 // One visible control window per session; the agent independently enforces one engine owner.
 auto existing=FindWindowW(cls.lpszClassName,nullptr);
#ifndef BRIDGE_UI_TEST
 if(!existing)existing=FindWindowW(L"AudioBridgeRoomWindow",nullptr);
#endif
 if(existing){if(!command_line||!wcsstr(command_line,L"--background")){ShowWindow(existing,SW_RESTORE);SetForegroundWindow(existing);}return 0;}
 HWND w=CreateWindowExW(0,cls.lpszClassName,L"San Audisco",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN|WS_VSCROLL|WS_HSCROLL,CW_USEDEFAULT,CW_USEDEFAULT,px(990),px(810),nullptr,nullptr,instance,nullptr);if(!w)return 1;
 RECT work{};if(SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0)){RECT r{};GetWindowRect(w,&r);SetWindowPos(w,nullptr,work.left+24,work.top+24,std::min<int>(r.right-r.left,work.right-work.left-48),std::min<int>(r.bottom-r.top,work.bottom-work.top-48),SWP_NOZORDER);}
 if(!command_line||!wcsstr(command_line,L"--background"))ShowWindow(w,show);
 MSG m;while(GetMessageW(&m,nullptr,0,0)>0){bool handled=false;for(int i=1;i<4;++i)if(IsWindowVisible(panels[i])&&IsDialogMessageW(panels[i],&m)){handled=true;break;}if(!handled&&!IsDialogMessageW(w,&m)){TranslateMessage(&m);DispatchMessageW(&m);}}
 if(worker.joinable())worker.join();while(PeekMessageW(&m,nullptr,response_message,response_message,PM_REMOVE))delete reinterpret_cast<Response*>(m.lParam);return 0;
}
