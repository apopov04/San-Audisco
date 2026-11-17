#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <string>
#include <thread>
#include <array>
#include <sstream>
#include <vector>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <map>
namespace {
constexpr UINT log_message=WM_APP+1;
constexpr UINT refresh_message=WM_APP+2;
HWND tabs{},peer_edit{},key_edit{},device_boxes[4]{},master{};
std::vector<HWND> pages[2];int building_page=-1;
struct DeviceInfo{std::wstring id,name;int inputs{},outputs{};};std::vector<DeviceInfo> devices;
HWND window{},path_edit{},log_edit{},primary{},mic_source{},run_button{};
HANDLE child_process{},child_input{};std::thread reader;
HWND win_volume{},mac_volume{},balance{},mute{},deafen{},win_mute{},mac_mute{},win_deafen{},mac_deafen{};
std::wstring child_exe(){wchar_t p[32768];GetModuleFileNameW(nullptr,p,32768);std::wstring s=p;return s.substr(0,s.find_last_of(L"\\/")+1)+L"audiobridge.exe";}
std::wstring text(HWND w){int n=GetWindowTextLengthW(w);std::wstring s(n+1,L'\0');GetWindowTextW(w,s.data(),n+1);s.resize(n);return s;}
void log(const std::wstring& s){int n=GetWindowTextLengthW(log_edit);if(n>32000){SetWindowTextW(log_edit,L"");n=0;}SendMessageW(log_edit,EM_SETSEL,n,n);SendMessageW(log_edit,EM_REPLACESEL,FALSE,reinterpret_cast<LPARAM>((s+L"\r\n").c_str()));}

void sync_state(const std::wstring& json){
 auto number=[&](const wchar_t* key,double& value){auto token=L"\""+std::wstring(key)+L"\":";auto p=json.find(token);if(p==std::wstring::npos)return false;p+=token.size();while(p<json.size()&&json[p]==L' ')++p;if(json.compare(p,4,L"true")==0){value=1;return true;}if(json.compare(p,5,L"false")==0){value=0;return true;}wchar_t* end=nullptr;value=wcstod(json.c_str()+p,&end);return end!=json.c_str()+p;};
 double v=0;if(number(L"primary",v))SendMessageW(primary,CB_SETCURSEL,v==0?0:1,0);if(number(L"mic_source",v))SendMessageW(mic_source,CB_SETCURSEL,v==0?0:1,0);
 for(auto item:{std::pair{mute,L"muted"},std::pair{deafen,L"deafen"},std::pair{win_mute,L"win_mute"},std::pair{mac_mute,L"mac_mute"},std::pair{win_deafen,L"win_deafen"},std::pair{mac_deafen,L"mac_deafen"}})if(number(item.second,v))SendMessageW(item.first,BM_SETCHECK,v!=0?BST_CHECKED:BST_UNCHECKED,0);
 for(auto item:{std::pair{win_volume,L"win_volume"},std::pair{mac_volume,L"mac_volume"}})if(number(item.second,v)&&GetCapture()!=item.first)SendMessageW(item.first,TBM_SETPOS,TRUE,static_cast<LPARAM>(v*100));
 if(number(L"master",v)&&GetCapture()!=master)SendMessageW(master,TBM_SETPOS,TRUE,static_cast<LPARAM>(v*100));
 if(number(L"balance",v)&&GetCapture()!=balance)SendMessageW(balance,TBM_SETPOS,TRUE,static_cast<LPARAM>((v+1)*50));
}
void command(const std::string& s){if(child_input){DWORD n=0;auto line=s+'\n';WriteFile(child_input,line.data(),static_cast<DWORD>(line.size()),&n,nullptr);}}
void stop(){if(child_process){command("quit");if(child_input){CloseHandle(child_input);child_input=nullptr;}if(WaitForSingleObject(child_process,2500)==WAIT_TIMEOUT){TerminateProcess(child_process,1);WaitForSingleObject(child_process,1000);}if(reader.joinable())reader.join();CloseHandle(child_process);child_process=nullptr;}SetWindowTextW(run_button,L"Run");}
std::wstring utf16(const std::string& s){int n=MultiByteToWideChar(CP_UTF8,0,s.data(),static_cast<int>(s.size()),nullptr,0);std::wstring out(n,L'\0');MultiByteToWideChar(CP_UTF8,0,s.data(),static_cast<int>(s.size()),out.data(),n);return out;}
std::string utf8(const std::wstring& s){int n=WideCharToMultiByte(CP_UTF8,0,s.data(),static_cast<int>(s.size()),nullptr,0,nullptr,nullptr);std::string out(n,'\0');WideCharToMultiByte(CP_UTF8,0,s.data(),static_cast<int>(s.size()),out.data(),n,nullptr,nullptr);return out;}
std::wstring file_dialog(bool save,const wchar_t* title){wchar_t path[32768]{};OPENFILENAMEW o{};o.lStructSize=sizeof(o);o.hwndOwner=window;o.lpstrTitle=title;o.lpstrFile=path;o.nMaxFile=32768;o.lpstrFilter=L"All files\0*.*\0";o.Flags=OFN_PATHMUSTEXIST|(save?OFN_OVERWRITEPROMPT:OFN_FILEMUSTEXIST);return (save?GetSaveFileNameW(&o):GetOpenFileNameW(&o))?path:L"";}
std::string capture_cli(const std::wstring& arguments){
 SECURITY_ATTRIBUTES sa{sizeof(sa),nullptr,TRUE};HANDLE read{},write{};if(!CreatePipe(&read,&write,&sa,0))throw std::runtime_error("Cannot create command pipe");SetHandleInformation(read,HANDLE_FLAG_INHERIT,0);
 STARTUPINFOW si{};si.cb=sizeof(si);si.dwFlags=STARTF_USESTDHANDLES;si.hStdOutput=write;si.hStdError=write;si.hStdInput=GetStdHandle(STD_INPUT_HANDLE);PROCESS_INFORMATION pi{};auto exe=child_exe();auto cmd=L"\""+exe+L"\" "+arguments;
 BOOL ok=CreateProcessW(exe.c_str(),cmd.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW,nullptr,nullptr,&si,&pi);CloseHandle(write);if(!ok){CloseHandle(read);throw std::runtime_error("Could not launch audiobridge.exe");}CloseHandle(pi.hThread);
 std::string output;const ULONGLONG deadline=GetTickCount64()+10000;bool finished=false;for(;;){DWORD available=0;if(PeekNamedPipe(read,nullptr,0,nullptr,&available,nullptr)&&available){char buf[2048];DWORD n=0;if(ReadFile(read,buf,std::min<DWORD>(available,sizeof(buf)),&n,nullptr))output.append(buf,n);if(output.size()>1024*1024)break;continue;}if(WaitForSingleObject(pi.hProcess,20)==WAIT_OBJECT_0){finished=true;break;}if(GetTickCount64()>deadline)break;}
 DWORD code=1;if(!finished)TerminateProcess(pi.hProcess,1);else GetExitCodeProcess(pi.hProcess,&code);CloseHandle(read);CloseHandle(pi.hProcess);if(!finished)throw std::runtime_error("Audio device command timed out");if(code)throw std::runtime_error(output);return output;
}
std::string json_string(const std::string& line,const std::string& key){auto p=line.find("\""+key+"\":");if(p==std::string::npos)return {};p=line.find('"',p+key.size()+3);if(p==std::string::npos)return {};std::string out;for(++p;p<line.size();++p){char c=line[p];if(c=='"')break;if(c=='\\'&&p+1<line.size()){c=line[++p];if(c=='n')c='\n';else if(c=='r')c='\r';else if(c=='t')c='\t';}out+=c;}return out;}
int json_int(const std::string& line,const std::string& key){auto p=line.find("\""+key+"\":");return p==std::string::npos?0:std::atoi(line.c_str()+p+key.size()+3);}
std::filesystem::path config_path();
void refresh_devices(){try{
 std::map<std::string,std::string> saved;try{std::ifstream in(config_path());std::string line;while(std::getline(in,line)){auto p=line.find('=');if(p!=std::string::npos)saved[line.substr(0,p)]=line.substr(p+1);}}catch(...){}
 if(text(peer_edit).empty())SetWindowTextW(peer_edit,utf16(saved["peer"]).c_str());if(text(key_edit).empty())SetWindowTextW(key_edit,utf16(saved["key_file"]).c_str());
 const char* names[]{"playback_input","microphone_output","headphones_output","physical_microphone"};std::array<std::wstring,4> previous;for(int i=0;i<4;i++){auto pos=SendMessageW(device_boxes[i],CB_GETCURSEL,0,0);auto index=pos==CB_ERR?devices.size():static_cast<size_t>(SendMessageW(device_boxes[i],CB_GETITEMDATA,pos,0));previous[i]=index<devices.size()?devices[index].id:utf16(saved[names[i]]);}
 auto output=capture_cli(L"devices");devices.clear();for(auto box:device_boxes)SendMessageW(box,CB_RESETCONTENT,0,0);std::istringstream lines(output);std::string line;while(std::getline(lines,line)){if(line.empty()||line[0]!='{')continue;DeviceInfo d{utf16(json_string(line,"id")),utf16(json_string(line,"name")),json_int(line,"inputs"),json_int(line,"outputs")};if(d.id.empty())continue;devices.push_back(d);auto index=devices.size()-1;for(int i=0;i<4;i++){bool input=i==0||i==3;if((input?d.inputs:d.outputs)<=0)continue;auto pos=SendMessageW(device_boxes[i],CB_ADDSTRING,0,reinterpret_cast<LPARAM>(d.name.c_str()));SendMessageW(device_boxes[i],CB_SETITEMDATA,pos,index);if(d.id==previous[i])SendMessageW(device_boxes[i],CB_SETCURSEL,pos,0);}}
 log(L"Device list refreshed. Select the two virtual cable endpoints and physical devices explicitly.");}catch(const std::exception& e){log(utf16(e.what()));}}
std::filesystem::path config_path(){wchar_t folder[32768];DWORD n=GetEnvironmentVariableW(L"LOCALAPPDATA",folder,32768);if(!n||n>=32768)throw std::runtime_error("LOCALAPPDATA unavailable");auto path=std::filesystem::path(folder)/L"AudioBridge";std::filesystem::create_directories(path);return path/L"windows.conf";}
std::wstring save_setup(){auto peer=text(peer_edit),key=text(key_edit);if(peer.empty()||key.empty())throw std::runtime_error("Enter the Mac IPv4 address and select a shared pairing key file");if(peer.find_first_not_of(L"0123456789.")!=std::wstring::npos)throw std::runtime_error("Peer must be an IPv4 address");std::ifstream keyfile{std::filesystem::path(key)};std::string secret;keyfile>>secret;if(secret.size()!=64||secret.find_first_not_of("0123456789abcdefABCDEF")!=std::string::npos)throw std::runtime_error("Pairing key must contain 64 hexadecimal characters");
 std::array<std::wstring,4> ids;for(int i=0;i<4;i++){auto selected=SendMessageW(device_boxes[i],CB_GETCURSEL,0,0);if(selected==CB_ERR)throw std::runtime_error("Select all four audio endpoints");auto index=static_cast<size_t>(SendMessageW(device_boxes[i],CB_GETITEMDATA,selected,0));if(index>=devices.size())throw std::runtime_error("Refresh and select audio devices again");ids[i]=devices[index].id;}
 auto path=config_path();std::ofstream out(path);out<<"device=windows\npeer="<<utf8(peer)<<"\nkey_file="<<utf8(key)<<"\nport=47190\npeer_port=47190\n";const char* names[]{"playback_input","microphone_output","headphones_output","physical_microphone"};for(int i=0;i<4;i++)out<<names[i]<<'='<<utf8(ids[i])<<'\n';if(!out)throw std::runtime_error("Could not save configuration");return path.wstring();}
void start(){
 if(child_process){stop();return;}auto config=text(path_edit);if(config.empty()){try{config=save_setup();}catch(const std::exception& e){log(utf16(e.what()));return;}}
 SECURITY_ATTRIBUTES sa{sizeof(sa),nullptr,TRUE};HANDLE stdin_read{},stdout_read{},stdout_write{};
 if(!CreatePipe(&stdin_read,&child_input,&sa,0)||!CreatePipe(&stdout_read,&stdout_write,&sa,0)){log(L"Could not create process pipes.");if(stdin_read)CloseHandle(stdin_read);if(child_input){CloseHandle(child_input);child_input=nullptr;}return;}
 SetHandleInformation(child_input,HANDLE_FLAG_INHERIT,0);SetHandleInformation(stdout_read,HANDLE_FLAG_INHERIT,0);
 STARTUPINFOW si{};si.cb=sizeof(si);si.dwFlags=STARTF_USESTDHANDLES;si.hStdInput=stdin_read;si.hStdOutput=stdout_write;si.hStdError=stdout_write;PROCESS_INFORMATION pi{};
 std::wstring executable=child_exe();std::wstring cmd=L"\""+executable+L"\" run \""+config+L"\"";
 BOOL ok=CreateProcessW(executable.c_str(),cmd.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW,nullptr,nullptr,&si,&pi);
 CloseHandle(stdin_read);CloseHandle(stdout_write);
 if(!ok){CloseHandle(stdout_read);CloseHandle(child_input);child_input=nullptr;log(L"Could not launch audiobridge.exe beside this application. Windows error "+std::to_wstring(GetLastError()));return;}
 CloseHandle(pi.hThread);child_process=pi.hProcess;SetWindowTextW(run_button,L"Stop");log(L"Engine started. Microphone starts muted. Bluetooth connection switching remains manual.");
 SendMessageW(mute,BM_SETCHECK,BST_CHECKED,0);
 SendMessageW(primary,CB_SETCURSEL,0,0);SendMessageW(mic_source,CB_SETCURSEL,1,0);SendMessageW(win_volume,TBM_SETPOS,TRUE,100);SendMessageW(mac_volume,TBM_SETPOS,TRUE,100);SendMessageW(balance,TBM_SETPOS,TRUE,50);for(auto h:{deafen,win_mute,mac_mute,win_deafen,mac_deafen})SendMessageW(h,BM_SETCHECK,BST_UNCHECKED,0);
 reader=std::thread([stdout_read]{std::array<char,2048> buffer;DWORD count;std::string line;while(ReadFile(stdout_read,buffer.data(),buffer.size(),&count,nullptr)&&count){line.append(buffer.data(),count);for(size_t p;(p=line.find('\n'))!=std::string::npos;){auto part=line.substr(0,p);line.erase(0,p+1);int n=MultiByteToWideChar(CP_UTF8,0,part.data(),static_cast<int>(part.size()),nullptr,0);auto* msg=new std::wstring(n,L'\0');MultiByteToWideChar(CP_UTF8,0,part.data(),static_cast<int>(part.size()),msg->data(),n);if(!PostMessageW(window,log_message,0,reinterpret_cast<LPARAM>(msg)))delete msg;}}CloseHandle(stdout_read);});
}
HWND control(const wchar_t* cls,const wchar_t* caption,DWORD style,int x,int y,int width,int height,int id){auto h=CreateWindowExW(0,cls,caption,WS_CHILD|WS_VISIBLE|style,x,y,width,height,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),nullptr,nullptr);SendMessageW(h,WM_SETFONT,reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)),TRUE);if(building_page>=0)pages[building_page].push_back(h);return h;}
HWND slider(int y,int id,int position){auto h=control(TRACKBAR_CLASSW,L"",TBS_AUTOTICKS|WS_TABSTOP,160,y,340,34,id);SendMessageW(h,TBM_SETRANGE,TRUE,MAKELONG(0,100));SendMessageW(h,TBM_SETPOS,TRUE,position);return h;}
void toggle(HWND h,const char* cmd){command(std::string(cmd)+" "+(SendMessageW(h,BM_GETCHECK,0,0)==BST_CHECKED?"1":"0"));}
LRESULT CALLBACK proc(HWND w,UINT msg,WPARAM wp,LPARAM lp){switch(msg){
 case WM_CREATE:{window=w;control(L"STATIC",L"AudioBridge — Windows + Mac",0,20,15,610,26,0);
 tabs=control(WC_TABCONTROLW,L"",WS_TABSTOP,20,48,610,380,180);TCITEMW tab{};tab.mask=TCIF_TEXT;tab.pszText=const_cast<wchar_t*>(L"Connection and devices");TabCtrl_InsertItem(tabs,0,&tab);tab.pszText=const_cast<wchar_t*>(L"Mixer");TabCtrl_InsertItem(tabs,1,&tab);
 building_page=0;control(L"STATIC",L"Mac IPv4 address",0,35,90,160,22,0);peer_edit=control(L"EDIT",L"",WS_BORDER|ES_AUTOHSCROLL|WS_TABSTOP,205,87,230,25,201);control(L"BUTTON",L"Refresh devices",WS_TABSTOP,450,86,160,27,202);
 control(L"STATIC",L"Shared pairing key",0,35,127,160,22,0);key_edit=control(L"EDIT",L"",WS_BORDER|ES_AUTOHSCROLL|WS_TABSTOP,205,124,230,25,203);control(L"BUTTON",L"Choose…",WS_TABSTOP,445,123,80,27,204);control(L"BUTTON",L"Create…",WS_TABSTOP,530,123,80,27,205);
 const wchar_t* labels[]{L"Game audio (Cable A recording)",L"Voice to game (Cable B playback)",L"Headphones / AirPods output",L"Physical microphone"};for(int i=0;i<4;i++){control(L"STATIC",labels[i],0,35,172+i*39,260,22,0);device_boxes[i]=control(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_TABSTOP|WS_VSCROLL,300,167+i*39,310,200,210+i);}
 control(L"STATIC",L"Use the same key on both computers. Install two virtual audio cables first.\nSet Deadlock output to Cable A playback and its mic to Cable B recording.",0,35,325,570,43,0);
 control(L"STATIC",L"Advanced config override (optional)",0,35,374,230,22,0);path_edit=control(L"EDIT",L"",WS_BORDER|ES_AUTOHSCROLL|WS_TABSTOP,270,370,250,25,100);control(L"BUTTON",L"Browse…",WS_TABSTOP,530,369,80,27,101);
 building_page=1;control(L"STATIC",L"AirPods output host",0,35,94,140,22,0);primary=control(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_TABSTOP,175,90,120,100,110);SendMessageW(primary,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"Windows"));SendMessageW(primary,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"Mac"));SendMessageW(primary,CB_SETCURSEL,0,0);
 control(L"STATIC",L"Microphone source",0,320,94,135,22,0);mic_source=control(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_TABSTOP,465,90,140,100,111);SendMessageW(mic_source,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"Windows"));SendMessageW(mic_source,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"Mac"));SendMessageW(mic_source,CB_SETCURSEL,1,0);
 control(L"STATIC",L"Windows volume",0,35,152,120,22,0);win_volume=slider(142,120,100);win_mute=control(L"BUTTON",L"Mute mic",BS_AUTOCHECKBOX|WS_TABSTOP,505,140,100,25,121);win_deafen=control(L"BUTTON",L"Deafen",BS_AUTOCHECKBOX|WS_TABSTOP,505,168,100,25,122);
 control(L"STATIC",L"Mac volume",0,35,208,120,22,0);mac_volume=slider(198,130,100);mac_mute=control(L"BUTTON",L"Mute mic",BS_AUTOCHECKBOX|WS_TABSTOP,505,196,100,25,131);mac_deafen=control(L"BUTTON",L"Deafen",BS_AUTOCHECKBOX|WS_TABSTOP,505,224,100,25,132);
 control(L"STATIC",L"Favor Windows / Mac",0,35,262,130,28,0);balance=slider(252,140,50);control(L"STATIC",L"Master volume",0,35,308,130,22,0);master=slider(298,141,100);
 mute=control(L"BUTTON",L"Mute microphone to both",BS_AUTOCHECKBOX|WS_TABSTOP,35,353,260,28,150);SendMessageW(mute,BM_SETCHECK,BST_CHECKED,0);deafen=control(L"BUTTON",L"Deafen both",BS_AUTOCHECKBOX|WS_TABSTOP,320,353,200,28,151);
 building_page=-1;for(auto h:pages[1])ShowWindow(h,SW_HIDE);control(L"STATIC",L"Status and setup messages",0,20,443,480,24,0);log_edit=control(L"EDIT",L"",WS_BORDER|ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL|WS_VSCROLL,20,472,610,130,160);run_button=control(L"BUTTON",L"Run",WS_TABSTOP,510,615,120,32,102);control(L"STATIC",L"Connect AirPods manually to the selected output host.",0,20,621,480,24,0);SetTimer(w,1,500,nullptr);PostMessageW(w,refresh_message,0,0);return 0;}
 case WM_NOTIFY:if(reinterpret_cast<NMHDR*>(lp)->hwndFrom==tabs&&reinterpret_cast<NMHDR*>(lp)->code==TCN_SELCHANGE){int page=TabCtrl_GetCurSel(tabs);for(int i=0;i<2;i++)for(auto h:pages[i])ShowWindow(h,i==page?SW_SHOW:SW_HIDE);}return 0;
 case refresh_message:refresh_devices();return 0;
 case WM_COMMAND:{int id=LOWORD(wp);if(id==202){refresh_devices();}else if(id==204){auto path=file_dialog(false,L"Select shared pairing key");if(!path.empty())SetWindowTextW(key_edit,path.c_str());}else if(id==205){auto path=file_dialog(true,L"Create a new pairing key (copy it privately to your Mac)");if(!path.empty()){try{auto result=capture_cli(L"keygen \""+path+L"\"");SetWindowTextW(key_edit,path.c_str());log(utf16(result));}catch(const std::exception& e){log(utf16(e.what()));}}}else if(id==101){wchar_t p[32768]{};OPENFILENAMEW o{};o.lStructSize=sizeof(o);o.hwndOwner=w;o.lpstrFilter=L"Configuration files\0*.conf;*.txt\0All files\0*.*\0";o.lpstrFile=p;o.nMaxFile=32768;o.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST;if(GetOpenFileNameW(&o))SetWindowTextW(path_edit,p);}else if(id==102)start();else if(HIWORD(wp)==CBN_SELCHANGE&&(id==110||id==111))command(std::string(id==110?"primary ":"mic_source ")+std::to_string(SendMessageW(id==110?primary:mic_source,CB_GETCURSEL,0,0)));else if(id==121)toggle(win_mute,"win_mute");else if(id==122)toggle(win_deafen,"win_deafen");else if(id==131)toggle(mac_mute,"mac_mute");else if(id==132)toggle(mac_deafen,"mac_deafen");else if(id==150)toggle(mute,"mute");else if(id==151)toggle(deafen,"deafen");return 0;}
 case WM_HSCROLL:{auto h=reinterpret_cast<HWND>(lp);float v=static_cast<float>(SendMessageW(h,TBM_GETPOS,0,0))/100.f;std::string key=h==win_volume?"win_volume":h==mac_volume?"mac_volume":h==master?"master":"balance";if(h==balance)v=v*2-1;command(key+" "+std::to_string(v));return 0;}
 case log_message:{auto* s=reinterpret_cast<std::wstring*>(lp);log(*s);sync_state(*s);delete s;return 0;}
 case WM_TIMER:if(child_process&&WaitForSingleObject(child_process,0)==WAIT_OBJECT_0){stop();log(L"Engine exited. Review the messages above.");}return 0;
 case WM_CLOSE:stop();DestroyWindow(w);return 0;
 case WM_DESTROY:PostQuitMessage(0);return 0;
 }return DefWindowProcW(w,msg,wp,lp);}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int show){INITCOMMONCONTROLSEX cc{sizeof(cc),ICC_BAR_CLASSES|ICC_TAB_CLASSES};InitCommonControlsEx(&cc);WNDCLASSW cls{};cls.lpfnWndProc=proc;cls.hInstance=instance;cls.lpszClassName=L"AudioBridgeWindow";cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);cls.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);RegisterClassW(&cls);HWND w=CreateWindowExW(0,cls.lpszClassName,L"AudioBridge",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,CW_USEDEFAULT,CW_USEDEFAULT,670,700,nullptr,nullptr,instance,nullptr);if(!w)return 1;ShowWindow(w,show);MSG m;while(GetMessageW(&m,nullptr,0,0)>0){if(!IsDialogMessageW(w,&m)){TranslateMessage(&m);DispatchMessageW(&m);}}return 0;}
