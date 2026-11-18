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
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <condition_variable>
#include <cmath>
#include <cctype>
#include <deque>
#include <filesystem>
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
constexpr UINT_PTR refresh_timer=1;
int ui_dpi=96;
int px(int value){return MulDiv(value,ui_dpi,96);}
void position(HWND h,int x,int y,int width,int height){SetWindowPos(h,nullptr,px(x),px(y),px(width),px(height),SWP_NOZORDER);}
HFONT ui_font{},title_font{},heading_font{};
HWND card_name[2]{},card_status[2]{},card_headphones[2]{},card_microphone[2]{},card_hub[2]{},card_source[2]{},link_status{};
std::string card_id[2],card_signature[2];std::vector<std::string> card_routes[2][2];
HWND window{},tabs{},status_label{},error_label{},mute_reason{},run_button{};
HWND name_edit{},invite_label{},code_edit{},address_edit{},nearby_box{},login_check{},create_button{},join_button{},forget_button{};
HWND computer_box{},headphones_box{},microphone_box{},playback_box{},micoutput_box{},apply_button{},setup_label{};
HWND playback_label{},micoutput_label{},advanced_button{};
HWND hub_box{},mic_source{},win_volume{},mac_volume{},balance{},master{},mute{},deafen{},win_mute{},mac_mute{},win_deafen{},mac_deafen{},win_label{},mac_label{},diagnostics_box{};
std::vector<HWND> pages[4],mix_controls;
HWND signal_bars[2][3]{},signal_labels[2]{},tone_buttons[2]{};std::string tone_ids[2];
int building_page=-1;
Json state=Json::object();std::vector<Json> devices,nearby;
std::vector<std::string> audio_ids[4];
std::string configured_id,applied_device_signature,selected_room_id,selected_room_address;
bool device_dirty=false,advanced=false,updating=false,quitting=false,agent_available=false,connected=false,running=false,wanted_running=false,paired=false;
NOTIFYICONDATAW tray{};
std::wstring executable_path(){std::vector<wchar_t> p(32768);auto n=GetModuleFileNameW(nullptr,p.data(),static_cast<DWORD>(p.size()));if(!n||n>=p.size())throw std::runtime_error("Cannot locate AudioBridge");return {p.data(),n};}
std::wstring agent_path(){return (std::filesystem::path(executable_path()).parent_path()/L"bridge-agent.exe").wstring();}
std::wstring utf16(const std::string& s){if(s.empty())return {};int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),nullptr,0);if(!n)return L"Invalid text";std::wstring out(n,L'\0');MultiByteToWideChar(CP_UTF8,0,s.data(),static_cast<int>(s.size()),out.data(),n);return out;}
std::string utf8(const std::wstring& s){if(s.empty())return {};int n=WideCharToMultiByte(CP_UTF8,0,s.data(),static_cast<int>(s.size()),nullptr,0,nullptr,nullptr);std::string out(n,'\0');WideCharToMultiByte(CP_UTF8,0,s.data(),static_cast<int>(s.size()),out.data(),n,nullptr,nullptr);return out;}
std::wstring text(HWND w){int n=GetWindowTextLengthW(w);std::wstring s(n+1,L'\0');GetWindowTextW(w,s.data(),n+1);s.resize(n);return s;}
std::string string_value(const Json& v,const char* key,const std::string& fallback={}){auto i=v.find(key);return i!=v.end()&&i->is_string()?i->get<std::string>():fallback;}
bool boolean(const Json& v,const char* key,bool fallback=false){auto i=v.find(key);if(i==v.end())return fallback;if(i->is_boolean())return i->get<bool>();if(i->is_number())return i->get<double>()!=0;return fallback;}
double number(const Json& v,const char* key,double fallback=0){auto i=v.find(key);if(i==v.end())return fallback;if(i->is_boolean())return i->get<bool>()?1:0;if(i->is_number())return i->get<double>();return fallback;}
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
 if(!CreateProcessW(exe.c_str(),cmd.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW|EXTENDED_STARTUPINFO_PRESENT,nullptr,nullptr,&si.StartupInfo,&pi))throw std::runtime_error("Cannot launch bridge-agent.exe beside AudioBridgeApp.exe");
 Handle process(pi.hProcess),thread(pi.hThread);in_read.close();out_write.close();
 if(request){auto data=request->dump()+"\n";DWORD n=0;if(data.size()>65536||!WriteFile(in_write.value,data.data(),static_cast<DWORD>(data.size()),&n,nullptr)||n!=data.size()){TerminateProcess(process.value,1);throw std::runtime_error("Cannot send the agent request");}}
 in_write.close();std::string output;bool finished=false;const auto deadline=GetTickCount64()+15000;
 for(;;){DWORD count=0;if(PeekNamedPipe(out_read.value,nullptr,0,nullptr,&count,nullptr)&&count){char chunk[8192];DWORD n=0;if(ReadFile(out_read.value,chunk,std::min<DWORD>(count,sizeof(chunk)),&n,nullptr))output.append(chunk,n);if(output.size()>1024*1024){TerminateProcess(process.value,1);throw std::runtime_error("Agent response exceeded its size limit");}continue;}
  if(WaitForSingleObject(process.value,20)==WAIT_OBJECT_0){finished=true;DWORD remaining=0;if(PeekNamedPipe(out_read.value,nullptr,0,nullptr,&remaining,nullptr)&&remaining)continue;break;}
  if(GetTickCount64()>=deadline)break;
 }
 if(!finished){TerminateProcess(process.value,1);WaitForSingleObject(process.value,1000);throw std::runtime_error("AudioBridge did not respond; the background audio may still be running");}
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
  if(quiet)status_queued=true;else{++outstanding;SetWindowTextW(error_label,L"");}
  // Coalesce slider edits that haven't been sent yet, keeping the newest intent.
  if(string_value(value,"command")=="control")for(auto it=queue.begin();it!=queue.end();++it)if(string_value(it->value,"command")=="control"&&string_value(it->value,"key")==string_value(value,"key")){it->value=std::move(value);--outstanding;return;}
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
HWND element(const wchar_t* cls,const wchar_t* caption,DWORD style,int x,int y,int width,int height,int id){auto h=CreateWindowExW(0,cls,caption,WS_CHILD|WS_VISIBLE|style,px(x),px(y),px(width),px(height),window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),nullptr,nullptr);SendMessageW(h,WM_SETFONT,reinterpret_cast<WPARAM>(ui_font),TRUE);if(building_page>=0)pages[building_page].push_back(h);return h;}
HWND combo(int x,int y,int width,int id){return element(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_TABSTOP|WS_VSCROLL,x,y,width,220,id);}
HWND slider(int y,int id,int position){auto h=element(TRACKBAR_CLASSW,L"",TBS_NOTICKS|WS_TABSTOP,170,y,390,28,id);SendMessageW(h,TBM_SETRANGE,TRUE,MAKELONG(0,100));SendMessageW(h,TBM_SETPOS,TRUE,position);mix_controls.push_back(h);return h;}
void show_page(){const int page=TabCtrl_GetCurSel(tabs);for(int i=0;i<4;++i)for(auto h:pages[i])ShowWindow(h,i==page?SW_SHOW:SW_HIDE);if(page==2&&!advanced)for(auto h:{playback_box,micoutput_box,playback_label,micoutput_label})ShowWindow(h,SW_HIDE);InvalidateRect(window,nullptr,FALSE);}
std::string selected_id(HWND box,const std::vector<std::string>& ids){auto i=SendMessageW(box,CB_GETCURSEL,0,0);return i>=0&&static_cast<size_t>(i)<ids.size()?ids[static_cast<size_t>(i)]:"";}
Json selected_device(){for(const auto& d:devices)if(string_value(d,"id")==configured_id)return d;return Json::object();}
bool is_virtual(const Json& d);
void populate_audio(){auto d=selected_device();if(d.empty())return;auto stable=d;stable.erase("diagnostics");auto signature=stable.dump()+std::to_string(number(state.value("settings",Json::object()),"primary"));if(device_dirty||signature==applied_device_signature)return;applied_device_signature=signature;
 HWND boxes[]{headphones_box,microphone_box,playback_box,micoutput_box};const char* keys[]{"headphones_output","physical_microphone","playback_input","microphone_output"};
 for(int i=0;i<4;++i){SendMessageW(boxes[i],CB_RESETCONTENT,0,0);audio_ids[i]={""};SendMessageW(boxes[i],CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"Not selected"));SendMessageW(boxes[i],CB_SETCURSEL,0,0);}
 if(d.contains("audio_devices")&&d["audio_devices"].is_array())for(const auto& audio:d["audio_devices"]){if(!audio.is_object())continue;for(int i=0;i<4;++i){const bool input=i==1||i==2;if(number(audio,input?"inputs":"outputs")<=0)continue;if(i<2&&is_virtual(audio))continue;auto id=string_value(audio,"id");auto name=utf16(string_value(audio,"name",id));audio_ids[i].push_back(id);auto pos=SendMessageW(boxes[i],CB_ADDSTRING,0,reinterpret_cast<LPARAM>(name.c_str()));if(id==string_value(d,keys[i]))SendMessageW(boxes[i],CB_SETCURSEL,pos,0);}}
 auto settings=state.value("settings",Json::object());const bool hub=(string_value(d,"platform")=="windows"?0:1)==static_cast<int>(number(settings,"primary"));
 SetWindowTextW(setup_label,utf16(std::string(hub?"Hub · headphones connected here\n":"Satellite · sends audio to the Hub\n")+string_value(d,"setup_status","Checking components…")).c_str());
 EnableWindow(apply_button,FALSE);for(auto h:boxes)EnableWindow(h,boolean(d,"online"));
}
bool is_virtual(const Json& d){auto name=string_value(d,"name");std::transform(name.begin(),name.end(),name.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});return name.find("blackhole")!=name.npos||name.find("cable")!=name.npos||name.find("streaming")!=name.npos;}
Json platform_device(int slot){for(auto& d:devices)if((string_value(d,"platform")=="windows"?0:1)==slot)return d;return Json::object();}
void update_cards(){auto settings=state.value("settings",Json::object());
 for(int slot=0;slot<2;++slot){auto d=platform_device(slot);card_id[slot]=string_value(d,"id");bool online=agent_available&&!d.empty()&&boolean(d,"online")&&(card_id[slot]==string_value(state,"device_id")||connected);
  auto name=string_value(d,"name",slot==0?"Windows PC":"Mac");SetWindowTextW(card_name[slot],utf16(name).c_str());
  SetWindowTextW(card_status[slot],online?(card_id[slot]==string_value(state,"device_id")?L"This computer · Online":L"Online · Room connected"):agent_available?L"Offline · Waiting for connection":L"App connection unavailable");
  SendMessageW(card_hub[slot],BM_SETCHECK,number(settings,"primary")==slot?BST_CHECKED:BST_UNCHECKED,0);SendMessageW(card_source[slot],BM_SETCHECK,number(settings,"mic_source",1)==slot?BST_CHECKED:BST_UNCHECKED,0);
  EnableWindow(card_hub[slot],connected&&online);EnableWindow(card_source[slot],connected&&online);
  auto stable=d;stable.erase("diagnostics");stable.erase("inventory_revision");auto signature=stable.dump();
  if(signature!=card_signature[slot]){card_signature[slot]=signature;HWND boxes[]{card_headphones[slot],card_microphone[slot]};const char* keys[]{"headphones_output","physical_microphone"};
   for(int route=0;route<2;++route){auto box=boxes[route];SendMessageW(box,CB_RESETCONTENT,0,0);card_routes[slot][route]={""};SendMessageW(box,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"Not selected"));SendMessageW(box,CB_SETCURSEL,0,0);
    if(d.contains("audio_devices"))for(auto& audio:d["audio_devices"]){if(is_virtual(audio)||number(audio,route?"inputs":"outputs")<=0)continue;auto id=string_value(audio,"id");auto label=utf16(string_value(audio,"name",id));card_routes[slot][route].push_back(id);auto pos=SendMessageW(box,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));if(id==string_value(d,keys[route]))SendMessageW(box,CB_SETCURSEL,pos,0);}
   }
  }
  EnableWindow(card_headphones[slot],online);EnableWindow(card_microphone[slot],online);
  EnableWindow(slot?mac_volume:win_volume,connected&&online);EnableWindow(slot?mac_mute:win_mute,connected&&online);EnableWindow(slot?mac_deafen:win_deafen,connected&&online);
 }
 InvalidateRect(GetDlgItem(window,760),nullptr,FALSE);SetWindowTextW(link_status,!agent_available?L"App connection unavailable":connected?L"Room connected":paired?L"Reconnecting…":L"Create or join a room");InvalidateRect(window,nullptr,FALSE);
}
void draw_computers(HDC dc){if(TabCtrl_GetCurSel(tabs)!=0)return;
 const COLORREF blue=RGB(30,110,220),gray=RGB(155,163,173);auto pen=CreatePen(connected?PS_SOLID:PS_DASH,connected?2:1,connected?blue:gray);auto old=SelectObject(dc,pen);MoveToEx(dc,335,154,nullptr);LineTo(dc,605,154);SelectObject(dc,old);DeleteObject(pen);
 for(int slot=0;slot<2;++slot){auto d=platform_device(slot);bool online=agent_available&&!d.empty()&&boolean(d,"online")&&(string_value(d,"id")==string_value(state,"device_id")||connected);int x=slot?655:185;auto outline=CreatePen(PS_SOLID,3,online?blue:gray);auto fill=CreateSolidBrush(online?RGB(232,242,255):RGB(240,242,245));auto previousPen=SelectObject(dc,outline),previousBrush=SelectObject(dc,fill);RoundRect(dc,x,108,x+110,174,9,9);if(slot==0){MoveToEx(dc,x+55,174,nullptr);LineTo(dc,x+55,187);MoveToEx(dc,x+30,187,nullptr);LineTo(dc,x+80,187);}else{POINT base[]{{x,174},{x+110,174},{x+124,184},{x+116,188},{x-6,188},{x-14,184}};Polygon(dc,base,6);MoveToEx(dc,x+43,179,nullptr);LineTo(dc,x+67,179);}SelectObject(dc,previousPen);SelectObject(dc,previousBrush);DeleteObject(outline);DeleteObject(fill);}
}
std::wstring platform_name(bool windows){for(const auto& d:devices){auto p=string_value(d,"platform");if((p=="windows"||p=="Windows")==windows)return utf16(string_value(d,"name",windows?"Windows":"Mac"));}return windows?L"Windows":L"Mac";}
void fill_roles(){for(auto box:{hub_box,mic_source}){SendMessageW(box,CB_RESETCONTENT,0,0);for(bool win:{true,false}){auto name=platform_name(win);SendMessageW(box,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(name.c_str()));}}}
void apply_state(const Json& s){updating=true;if(!agent_available){SetWindowTextW(error_label,L"");applied_device_signature.clear();}agent_available=true;state=s;paired=boolean(s,"paired");connected=boolean(s,"connected");running=boolean(s,"running");wanted_running=boolean(s,"wanted_running",running);
 if(text(name_edit).empty())SetWindowTextW(name_edit,utf16(string_value(s,"name","Windows PC")).c_str());
 auto invite=string_value(s,"invite_code");if(invite.size()==8)invite.insert(4,"-");if(!invite.empty())invite+="   ("+std::to_string(static_cast<int>(number(s,"invite_expires_seconds")))+" seconds remaining)";
 else invite=paired?"Pairing remembered. Both apps control this room.":"Create a room here or enter the other computer's code.";
 SetWindowTextW(invite_label,utf16(invite).c_str());SetWindowTextW(status_label,utf16(string_value(s,"status",connected?"Room connected":"Waiting for the other computer")).c_str());
 SetWindowTextW(mute_reason,utf16(string_value(s,"mute_reason")).c_str());SetWindowTextW(run_button,(running||wanted_running)?L"Stop audio":L"Start audio");EnableWindow(run_button,paired||running);
 EnableWindow(create_button,!paired);EnableWindow(join_button,!paired);EnableWindow(code_edit,!paired);EnableWindow(forget_button,paired||!string_value(s,"invite_code").empty());
 auto old=devices;devices.clear();if(s.contains("devices")&&s["devices"].is_array())for(const auto& d:s["devices"])if(d.is_object())devices.push_back(d);
 std::vector<std::string> old_ids,new_ids;for(const auto& d:old)old_ids.push_back(string_value(d,"id")+string_value(d,"name"));for(const auto& d:devices)new_ids.push_back(string_value(d,"id")+string_value(d,"name"));
 if(old_ids!=new_ids){SendMessageW(computer_box,CB_RESETCONTENT,0,0);int selection=0;for(size_t i=0;i<devices.size();++i){auto name=utf16(string_value(devices[i],"name"));if(string_value(devices[i],"id")==string_value(s,"device_id"))name+=L" (this PC)";SendMessageW(computer_box,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(name.c_str()));if(string_value(devices[i],"id")==configured_id)selection=static_cast<int>(i);}SendMessageW(computer_box,CB_SETCURSEL,selection,0);if(!devices.empty())configured_id=string_value(devices[selection],"id");applied_device_signature.clear();fill_roles();}
 populate_audio();
 auto new_nearby=s.value("discovered",Json::array());Json old_nearby=nearby;if(new_nearby.is_array()&&new_nearby!=old_nearby){nearby.clear();SendMessageW(nearby_box,CB_RESETCONTENT,0,0);for(const auto& d:new_nearby)if(d.is_object()){nearby.push_back(d);auto name=utf16(string_value(d,"name","Nearby room")+" · "+string_value(d,"address"));SendMessageW(nearby_box,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(name.c_str()));}}
 const auto settings=s.value("settings",Json::object());
 if(settings.is_object()){
  SendMessageW(hub_box,CB_SETCURSEL,number(settings,"primary"),0);SendMessageW(mic_source,CB_SETCURSEL,number(settings,"mic_source",1),0);
  for(auto item:{std::pair{mute,"mute"},std::pair{deafen,"deafen"},std::pair{win_mute,"win_mute"},std::pair{mac_mute,"mac_mute"},std::pair{win_deafen,"win_deafen"},std::pair{mac_deafen,"mac_deafen"}})SendMessageW(item.first,BM_SETCHECK,boolean(settings,item.second)?BST_CHECKED:BST_UNCHECKED,0);
  for(auto item:{std::pair{win_volume,"win_volume"},std::pair{mac_volume,"mac_volume"},std::pair{master,"master"}})if(GetCapture()!=item.first)SendMessageW(item.first,TBM_SETPOS,TRUE,static_cast<LPARAM>(number(settings,item.second,1)*100));
  if(GetCapture()!=balance)SendMessageW(balance,TBM_SETPOS,TRUE,static_cast<LPARAM>((number(settings,"balance")+1)*50));
 }
 SetWindowTextW(win_label,platform_name(true).c_str());SetWindowTextW(mac_label,platform_name(false).c_str());for(auto h:mix_controls)EnableWindow(h,connected);
 std::wstring checks;
 for(const auto& d:devices){checks+=utf16(string_value(d,"name"))+L" — "+(boolean(d,"online")?L"Online":L"Offline")+L"\r\n";checks+=utf16(string_value(d,"setup_status"))+L"\r\n\r\n";}
 auto diagnostic=s.value("diagnostics",Json::object());if(diagnostic.is_object()){
  checks+=utf16(string_value(diagnostic,"message"))+L"\r\n";
  if(boolean(diagnostic,"muted")&&!boolean(settings,"mute"))checks+=L"Microphone blocked by audio readiness: resolve the inactive route.\r\n";
  for(auto item:{std::pair{"playback_ready",L"Playback capture"},std::pair{"mic_sink_ready",L"Shared microphone output"},std::pair{"headphones_ready",L"Headphone output"},std::pair{"microphone_ready",L"Physical microphone"}})if(diagnostic.contains(item.first))checks+=std::wstring(item.second)+L": "+(boolean(diagnostic,item.first)?L"Ready":L"Not active")+L"\r\n";
 }
 checks+=L"\r\nA device can be inactive when the other computer owns that role.\r\nNo microphone recordings are saved by these checks.";
 for(int slot=0;slot<2;++slot){
  if(slot>=static_cast<int>(devices.size())){tone_ids[slot].clear();SetWindowTextW(signal_labels[slot],L"Other computer not connected");EnableWindow(tone_buttons[slot],FALSE);for(auto h:signal_bars[slot])SendMessageW(h,PBM_SETPOS,0,0);continue;}
  const auto& d=devices[slot];tone_ids[slot]=string_value(d,"id");SetWindowTextW(signal_labels[slot],utf16(string_value(d,"name")).c_str());
  auto levels=tone_ids[slot]==string_value(s,"device_id")?diagnostic:d.value("diagnostics",Json::object());
  const char* keys[]{"playback_level","microphone_level","microphone_delivery_level"};
  for(int column=0;column<3;++column){double level=number(levels,keys[column]);if(!std::isfinite(level))level=0;double meter=level>0?std::clamp((20*std::log10(level)+60)/60,0.0,1.0):0;SendMessageW(signal_bars[slot][column],PBM_SETPOS,static_cast<WPARAM>(meter*100),0);}
  EnableWindow(tone_buttons[slot],running&&boolean(d,"online"));
 }
 SetWindowTextW(diagnostics_box,checks.c_str());update_cards();updating=false;
}
void startup(bool enabled){HKEY raw{};if(RegCreateKeyExW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",0,nullptr,0,KEY_SET_VALUE,nullptr,&raw,nullptr)!=ERROR_SUCCESS)throw std::runtime_error("Cannot update your login setting");struct Key{HKEY k;~Key(){RegCloseKey(k);}}key{raw};LONG result;
 if(enabled){auto value=L"\""+executable_path()+L"\" --background";result=RegSetValueExW(raw,L"AudioBridge",0,REG_SZ,reinterpret_cast<const BYTE*>(value.c_str()),static_cast<DWORD>((value.size()+1)*sizeof(wchar_t)));}else{result=RegDeleteValueW(raw,L"AudioBridge");if(result==ERROR_FILE_NOT_FOUND)result=ERROR_SUCCESS;}
 if(result!=ERROR_SUCCESS)throw std::runtime_error("Could not save your login setting");
}
bool startup_enabled(){wchar_t value[32768];DWORD size=sizeof(value),type=0;if(RegGetValueW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",L"AudioBridge",RRF_RT_REG_SZ,&type,value,&size)!=ERROR_SUCCESS)return false;return std::wstring(value)==L"\""+executable_path()+L"\" --background";}
void open_window(){ShowWindow(window,SW_RESTORE);SetForegroundWindow(window);request({{"command","status"}},true);}
void tray_menu(){HMENU menu=CreatePopupMenu();AppendMenuW(menu,MF_STRING,tray_open,L"Open AudioBridge");AppendMenuW(menu,MF_STRING,tray_quit,L"Quit AudioBridge");POINT point;GetCursorPos(&point);SetForegroundWindow(window);auto command=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,point.x,point.y,0,window,nullptr);DestroyMenu(menu);if(command==tray_open)open_window();else if(command==tray_quit){quitting=true;request({{"command","quit"}},false,true);}}
void choose_configured_device(){auto selected=SendMessageW(computer_box,CB_GETCURSEL,0,0);if(selected>=0&&static_cast<size_t>(selected)<devices.size()){configured_id=string_value(devices[selected],"id");device_dirty=false;applied_device_signature.clear();populate_audio();}}
void apply_devices(){if(configured_id.empty())return;request({{"command","configure"},{"device_id",configured_id},{"headphones_output",selected_id(headphones_box,audio_ids[0])},{"physical_microphone",selected_id(microphone_box,audio_ids[1])},{"playback_input",selected_id(playback_box,audio_ids[2])},{"microphone_output",selected_id(micoutput_box,audio_ids[3])}});device_dirty=false;applied_device_signature.clear();}
LRESULT CALLBACK proc(HWND w,UINT msg,WPARAM wp,LPARAM lp){switch(msg){
 case WM_CREATE:{window=w;ui_font=CreateFontW(-px(16),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");title_font=CreateFontW(-px(27),0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");heading_font=CreateFontW(-px(20),0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
  auto title=element(L"STATIC",L"AudioBridge",0,32,18,700,35,0);SendMessageW(title,WM_SETFONT,reinterpret_cast<WPARAM>(title_font),TRUE);
  tabs=element(WC_TABCONTROLW,L"",WS_TABSTOP,32,62,890,510,180);const wchar_t* labels[]{L"Audio",L"Room",L"Advanced",L"Checks"};for(int i=0;i<4;i++){TCITEMW tab{};tab.mask=TCIF_TEXT;tab.pszText=const_cast<wchar_t*>(labels[i]);TabCtrl_InsertItem(tabs,i,&tab);}
  building_page=0;
  element(L"STATIC",L"Two computers linked by this room",SS_OWNERDRAW,60,100,830,95,760);
  link_status=element(L"STATIC",L"Connecting…",SS_CENTER,340,119,270,26,740);
  for(int slot=0;slot<2;++slot){int x=slot?530:60;int base=700+slot*20;
   card_name[slot]=element(L"STATIC",slot?L"Mac":L"Windows PC",SS_CENTER|SS_ENDELLIPSIS,x,199,360,28,base);SendMessageW(card_name[slot],WM_SETFONT,reinterpret_cast<WPARAM>(heading_font),TRUE);
   card_status[slot]=element(L"STATIC",L"Waiting for connection",SS_CENTER,x,231,360,22,base+1);
   card_hub[slot]=element(L"BUTTON",L"Hub · listen on this computer",BS_RADIOBUTTON|WS_TABSTOP,x,265,360,25,base+2);
   element(L"STATIC",L"Headphones",0,x,298,120,22,0);card_headphones[slot]=combo(x+125,295,235,base+3);
   card_source[slot]=element(L"BUTTON",L"Use this computer's microphone",BS_RADIOBUTTON|WS_TABSTOP,x,341,360,25,base+4);
   element(L"STATIC",L"Microphone",0,x,374,120,22,0);card_microphone[slot]=combo(x+125,371,235,base+5);
   SendMessageW(card_headphones[slot],CB_SETDROPPEDWIDTH,px(380),0);SendMessageW(card_microphone[slot],CB_SETDROPPEDWIDTH,px(380),0);
   element(L"STATIC",L"Audio from this computer",0,x,413,360,22,0);
   auto vol=slider(438,slot?420:410,100);position(vol,x-5,436,370,26);if(slot)mac_volume=vol;else win_volume=vol;
   auto m=element(L"BUTTON",L"Mute voice to this computer",BS_AUTOCHECKBOX|WS_TABSTOP,x,474,360,25,slot?421:411);
   auto d=element(L"BUTTON",L"Deafen this computer",BS_AUTOCHECKBOX|WS_TABSTOP,x,506,360,25,slot?422:412);
   mix_controls.push_back(m);mix_controls.push_back(d);if(slot){mac_mute=m;mac_deafen=d;mac_label=card_name[slot];}else{win_mute=m;win_deafen=d;win_label=card_name[slot];}
  }
  element(L"STATIC",L"Connect your AirPods to the Hub in Bluetooth settings. Room connection does not confirm audio readiness.",0,60,542,830,24,0);
  building_page=1;element(L"STATIC",L"This computer",0,38,95,145,22,0);name_edit=element(L"EDIT",L"",WS_BORDER|ES_AUTOHSCROLL|WS_TABSTOP,190,91,425,27,201);element(L"BUTTON",L"Save name",WS_TABSTOP,630,91,160,28,202);
  element(L"STATIC",L"Use the same local network. Create a room here, or join from your other computer.",0,38,138,750,25,0);create_button=element(L"BUTTON",L"Create room",WS_TABSTOP,38,174,155,32,203);invite_label=element(L"STATIC",L"",SS_LEFT,210,180,580,40,0);
  element(L"STATIC",L"Room code",0,38,238,145,22,0);code_edit=element(L"EDIT",L"",WS_BORDER|ES_AUTOHSCROLL|WS_TABSTOP|ES_UPPERCASE,190,233,425,29,204);SendMessageW(code_edit,EM_SETLIMITTEXT,16,0);join_button=element(L"BUTTON",L"Join room",WS_TABSTOP,630,233,160,30,205);
  element(L"STATIC",L"Nearby rooms",0,38,286,145,22,0);nearby_box=combo(190,281,425,206);element(L"BUTTON",L"Find rooms",WS_TABSTOP,630,281,160,30,207);
  element(L"STATIC",L"Manual address",0,38,334,145,22,0);address_edit=element(L"EDIT",L"",WS_BORDER|ES_AUTOHSCROLL|WS_TABSTOP,190,329,600,29,208);SendMessageW(address_edit,EM_SETCUEBANNER,TRUE,reinterpret_cast<LPARAM>(L"Optional: use if discovery is blocked"));
  login_check=element(L"BUTTON",L"Start AudioBridge at login",BS_AUTOCHECKBOX|WS_TABSTOP,38,382,420,28,209);SendMessageW(login_check,BM_SETCHECK,startup_enabled()?BST_CHECKED:BST_UNCHECKED,0);
  forget_button=element(L"BUTTON",L"Forget this pairing…",WS_TABSTOP,38,430,185,30,210);element(L"STATIC",L"Closing the window keeps audio running.\nOpen or quit AudioBridge from its notification-area icon.",0,250,428,540,46,0);
  building_page=2;element(L"STATIC",L"Configure computer",0,38,96,160,22,0);computer_box=combo(215,91,575,301);element(L"STATIC",L"Headphones",0,38,146,165,22,0);headphones_box=combo(215,141,575,302);element(L"STATIC",L"Physical microphone",0,38,196,165,22,0);microphone_box=combo(215,191,575,303);
  setup_label=element(L"STATIC",L"Checking audio components…",0,38,240,750,52,0);advanced_button=element(L"BUTTON",L"Show advanced routes",WS_TABSTOP,38,301,200,30,304);
  playback_label=element(L"STATIC",L"Playback cable capture",0,38,354,175,22,0);playback_box=combo(215,349,575,305);micoutput_label=element(L"STATIC",L"Shared mic output",0,38,400,175,22,0);micoutput_box=combo(215,395,575,306);
  element(L"STATIC",L"Connect AirPods to the Hub in Bluetooth settings.\nAudioBridge does not move their Bluetooth connection.",0,38,460,560,44,0);apply_button=element(L"BUTTON",L"Apply devices",WS_TABSTOP,630,461,160,32,307);
  element(L"STATIC",L"Balance: Windows / Mac",0,38,530,190,22,0);balance=slider(525,430,50);position(balance,230,525,560,28);
  // Compatibility role controls are hidden: the main screen associates roles with each device.
  building_page=-1;hub_box=combo(0,0,1,401);mic_source=combo(0,0,1,402);ShowWindow(hub_box,SW_HIDE);ShowWindow(mic_source,SW_HIDE);fill_roles();
  building_page=3;element(L"BUTTON",L"Check setup",WS_TABSTOP,38,92,160,30,501);element(L"BUTTON",L"Reconnect background agent",WS_TABSTOP,215,92,245,30,503);diagnostics_box=element(L"EDIT",L"",WS_BORDER|ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL|WS_VSCROLL,38,132,750,110,502);
  element(L"STATIC",L"Application audio",0,210,256,160,20,0);element(L"STATIC",L"Physical microphone",0,385,256,165,20,0);element(L"STATIC",L"Shared microphone",0,560,256,160,20,0);
  for(int slot=0;slot<2;++slot){int y=285+slot*72;signal_labels[slot]=element(L"STATIC",L"Waiting for device",0,38,y,165,24,0);for(int c=0;c<3;++c){signal_bars[slot][c]=element(PROGRESS_CLASSW,L"",0,210+c*175,y,155,18,510+slot*3+c);SendMessageW(signal_bars[slot][c],PBM_SETRANGE,0,MAKELPARAM(0,100));}tone_buttons[slot]=element(L"BUTTON",L"Play 2-second test sound",WS_TABSTOP,210,y+27,245,27,520+slot);EnableWindow(tone_buttons[slot],FALSE);}
  element(L"STATIC",L"Test sounds play through the Hub headphones. Meters show signal activity; listen to confirm.",0,38,430,750,25,0);
  element(L"STATIC",L"Deadlock: output CABLE Input; mic Hi-Fi Cable Output.\nDiscord on Mac: speaker BlackHole 2ch; mic BlackHole 16ch.",0,38,470,750,46,0);
  building_page=-1;
  mute=element(L"BUTTON",L"Mute microphone everywhere",BS_AUTOCHECKBOX|WS_TABSTOP,40,581,330,26,440);deafen=element(L"BUTTON",L"Deafen both",BS_AUTOCHECKBOX|WS_TABSTOP,385,581,170,26,441);
  element(L"STATIC",L"Overall volume",0,580,584,140,22,0);master=slider(578,431,100);position(master,720,579,200,28);mix_controls.push_back(mute);mix_controls.push_back(deafen);
  status_label=element(L"STATIC",L"Connecting to background agent…",SS_ENDELLIPSIS,40,616,880,23,750);mute_reason=element(L"STATIC",L"",SS_ENDELLIPSIS,40,642,880,23,751);error_label=element(L"STATIC",L"",SS_LEFT,40,668,650,40,752);
  run_button=element(L"BUTTON",L"Start audio",WS_TABSTOP,745,698,175,34,601);element(L"BUTTON",L"Quit AudioBridge",WS_TABSTOP,750,20,170,30,602);
  for(auto h:mix_controls)EnableWindow(h,FALSE);for(auto h:card_hub)EnableWindow(h,FALSE);for(auto h:card_source)EnableWindow(h,FALSE);EnableWindow(run_button,FALSE);EnableWindow(apply_button,FALSE);show_page();
  tray.cbSize=sizeof(tray);tray.hWnd=w;tray.uID=1;tray.uFlags=NIF_MESSAGE|NIF_ICON|NIF_TIP;tray.uCallbackMessage=tray_message;tray.hIcon=LoadIconW(nullptr,IDI_APPLICATION);wcscpy_s(tray.szTip,L"AudioBridge — audio continues when the window closes");Shell_NotifyIconW(NIM_ADD,&tray);
  start_worker();request({{"command","status"}},true);SetTimer(w,refresh_timer,1000,nullptr);return 0;}
 case WM_DRAWITEM:{auto item=reinterpret_cast<DRAWITEMSTRUCT*>(lp);if(item->CtlID==760){FillRect(item->hDC,&item->rcItem,GetSysColorBrush(COLOR_WINDOW));auto saved=SaveDC(item->hDC);SetMapMode(item->hDC,MM_ANISOTROPIC);SetWindowExtEx(item->hDC,96,96,nullptr);SetViewportExtEx(item->hDC,ui_dpi,ui_dpi,nullptr);SetViewportOrgEx(item->hDC,-px(60),-px(100),nullptr);draw_computers(item->hDC);RestoreDC(item->hDC,saved);return TRUE;}break;}

 case WM_CTLCOLORSTATIC:{auto dc=reinterpret_cast<HDC>(wp);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,reinterpret_cast<HWND>(lp)==error_label?RGB(185,40,40):reinterpret_cast<HWND>(lp)==mute_reason?RGB(150,92,20):RGB(30,36,45));return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));}
 case WM_NOTIFY:if(reinterpret_cast<NMHDR*>(lp)->hwndFrom==tabs&&reinterpret_cast<NMHDR*>(lp)->code==TCN_SELCHANGE)show_page();return 0;
 case WM_COMMAND:{if(updating)return 0;const int id=LOWORD(wp),event=HIWORD(wp);
  if(id>=700&&id<=725){int slot=id>=720?1:0;int action=id-(700+slot*20);
   if(action==2)control("primary",slot);else if(action==4)control("mic_source",slot);
   else if((action==3||action==5)&&event==CBN_SELCHANGE&&!card_id[slot].empty()){int route=action==5?1:0;request({{"command","configure"},{"device_id",card_id[slot]},{route?"physical_microphone":"headphones_output",selected_id(route?card_microphone[slot]:card_headphones[slot],card_routes[slot][route])}});card_signature[slot].clear();}
  }
  else if(id==202)request({{"command","configure"},{"device_id",string_value(state,"device_id")},{"name",utf8(text(name_edit))}});
  else if(id==203)request({{"command","create"},{"name",utf8(text(name_edit))}});
  else if(id==205){auto code=utf8(text(code_edit));code.erase(std::remove_if(code.begin(),code.end(),[](unsigned char c){return c=='-'||c==' '||c=='\t'||c=='\r'||c=='\n';}),code.end());for(auto& c:code)if(c>='a'&&c<='z')c=static_cast<char>(c-'a'+'A');if(code.size()!=8||!std::all_of(code.begin(),code.end(),[](unsigned char c){return (c>='A'&&c<='Z')||(c>='0'&&c<='9');}))SetWindowTextW(error_label,L"Enter the eight letters and numbers shown on the other computer.");else {auto address=utf8(text(address_edit));Json join={{"command","join"},{"code",code},{"address",address}};if(!selected_room_id.empty()&&address==selected_room_address)join["room_id"]=selected_room_id;request(std::move(join));}}
  else if(id==207)request({{"command","discover"}});
  else if(id==206&&event==CBN_SELCHANGE){auto index=SendMessageW(nearby_box,CB_GETCURSEL,0,0);if(index>=0&&static_cast<size_t>(index)<nearby.size()){selected_room_address=string_value(nearby[index],"address");selected_room_id=string_value(nearby[index],"id");SetWindowTextW(address_edit,utf16(selected_room_address).c_str());}}
  else if(id==209){try{startup(SendMessageW(login_check,BM_GETCHECK,0,0)==BST_CHECKED);}catch(const std::exception&e){SetWindowTextW(error_label,utf16(e.what()).c_str());SendMessageW(login_check,BM_SETCHECK,startup_enabled()?BST_CHECKED:BST_UNCHECKED,0);}}
  else if(id==210){if(MessageBoxW(w,L"Audio will stop. You will need a new room code to connect these computers again.",L"Forget this pairing?",MB_OKCANCEL|MB_ICONQUESTION)==IDOK)request({{"command","forget"}});}
  else if(id==301&&event==CBN_SELCHANGE)choose_configured_device();
  else if((id==302||id==303||id==305||id==306)&&event==CBN_SELCHANGE){device_dirty=true;EnableWindow(apply_button,TRUE);}
  else if(id==304){advanced=!advanced;SetWindowTextW(advanced_button,advanced?L"Hide advanced routes":L"Show advanced routes");show_page();}
  else if(id==307)apply_devices();
  else if((id==401||id==402)&&event==CBN_SELCHANGE)control(id==401?"primary":"mic_source",static_cast<double>(SendMessageW(id==401?hub_box:mic_source,CB_GETCURSEL,0,0)));
  else if(id==411)control("win_mute",SendMessageW(win_mute,BM_GETCHECK,0,0)==BST_CHECKED);
  else if(id==412)control("win_deafen",SendMessageW(win_deafen,BM_GETCHECK,0,0)==BST_CHECKED);
  else if(id==421)control("mac_mute",SendMessageW(mac_mute,BM_GETCHECK,0,0)==BST_CHECKED);
  else if(id==422)control("mac_deafen",SendMessageW(mac_deafen,BM_GETCHECK,0,0)==BST_CHECKED);
  else if(id==440)control("mute",SendMessageW(mute,BM_GETCHECK,0,0)==BST_CHECKED);
  else if(id==441)control("deafen",SendMessageW(deafen,BM_GETCHECK,0,0)==BST_CHECKED);
  else if(id==503)request({{"command","_ensure"}});
  else if(id==501)request({{"command","diagnose"}});
  else if((id==520||id==521)&&!tone_ids[id-520].empty())request({{"command","test_tone"},{"device_id",tone_ids[id-520]}});
  else if(id==601)request({{"command",(running||wanted_running)?"stop":"start"}});
  else if(id==602){quitting=true;request({{"command","quit"}},false,true);}
  return 0;}
 case WM_HSCROLL:{if(updating)return 0;auto h=reinterpret_cast<HWND>(lp);const char* key=h==win_volume?"win_volume":h==mac_volume?"mac_volume":h==master?"master":"balance";double value=static_cast<double>(SendMessageW(h,TBM_GETPOS,0,0))/100;if(h==balance)value=value*2-1;control(key,value);return 0;}
 case response_message:{std::unique_ptr<Response> result(reinterpret_cast<Response*>(lp));{std::lock_guard lock(queue_mutex);if(result->quiet)status_queued=false;else if(outstanding)--outstanding;}
  if(result->transport_failure){agent_available=false;connected=false;SetWindowTextW(status_label,L"Background app unavailable. Audio may still be running; reconnect from Checks.");for(auto h:mix_controls)EnableWindow(h,FALSE);for(auto h:{run_button,apply_button,headphones_box,microphone_box,playback_box,micoutput_box,create_button,join_button,forget_button})EnableWindow(h,FALSE);for(auto h:tone_buttons)EnableWindow(h,FALSE);update_cards();}
  if(result->value.contains("state")&&result->value["state"].is_object())apply_state(result->value["state"]);
  if(!boolean(result->value,"ok")){SetWindowTextW(error_label,utf16(string_value(result->value,"error","AudioBridge could not apply the request")).c_str());if(result->exit_after)quitting=false;}
  else if(result->exit_after){DestroyWindow(w);}
  return 0;}
 case WM_TIMER:if(IsWindowVisible(w)&&!quitting){std::lock_guard lock(queue_mutex);if(!outstanding&&!status_queued&&!stopping){status_queued=true;queue.push_back({Json{{"command","status"}},true,false});queue_cv.notify_one();}}return 0;
 case tray_message:if(lp==WM_LBUTTONDBLCLK)open_window();else if(lp==WM_RBUTTONUP||lp==WM_CONTEXTMENU)tray_menu();return 0;
 case WM_CLOSE:ShowWindow(w,SW_HIDE);return 0;
 case WM_ENDSESSION:if(wp)DestroyWindow(w);return 0;
 case WM_DESTROY:DeleteObject(ui_font);DeleteObject(title_font);DeleteObject(heading_font);KillTimer(w,refresh_timer);Shell_NotifyIconW(NIM_DELETE,&tray);{std::lock_guard lock(queue_mutex);stopping=true;queue.clear();}queue_cv.notify_one();PostQuitMessage(0);return 0;
 }return DefWindowProcW(w,msg,wp,lp);}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR command_line,int show){
 SetProcessDPIAware();auto screen=GetDC(nullptr);if(screen){ui_dpi=GetDeviceCaps(screen,LOGPIXELSX);ReleaseDC(nullptr,screen);}RECT work{};if(SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0)){ui_dpi=std::min({ui_dpi,int((work.right-work.left)*96/990),int((work.bottom-work.top)*96/810)});}ui_dpi=std::max(ui_dpi,72);INITCOMMONCONTROLSEX cc{sizeof(cc),ICC_BAR_CLASSES|ICC_TAB_CLASSES|ICC_PROGRESS_CLASS};InitCommonControlsEx(&cc);
 WNDCLASSW cls{};cls.lpfnWndProc=proc;cls.hInstance=instance;cls.lpszClassName=L"AudioBridgeRoomWindow";cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);cls.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);RegisterClassW(&cls);
 // One visible control window per session; the agent independently enforces one engine owner.
 auto existing=FindWindowW(cls.lpszClassName,nullptr);if(existing){if(!command_line||!wcsstr(command_line,L"--background")){ShowWindow(existing,SW_RESTORE);SetForegroundWindow(existing);}return 0;}
 HWND w=CreateWindowExW(0,cls.lpszClassName,L"AudioBridge",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,CW_USEDEFAULT,CW_USEDEFAULT,px(970),px(780),nullptr,nullptr,instance,nullptr);if(!w)return 1;
 if(!command_line||!wcsstr(command_line,L"--background"))ShowWindow(w,show);
 MSG m;while(GetMessageW(&m,nullptr,0,0)>0){if(!IsDialogMessageW(w,&m)){TranslateMessage(&m);DispatchMessageW(&m);}}
 if(worker.joinable())worker.join();while(PeekMessageW(&m,nullptr,response_message,response_message,PM_REMOVE))delete reinterpret_cast<Response*>(m.lParam);return 0;
}
