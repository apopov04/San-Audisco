// Private bootstrap. Vendor programs are unchanged and retain their normal UI.
// No driver removal, certificate-store writes, automatic restart or security overrides.
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>
#include <wintrust.h>
#include <softpub.h>
#include <wincrypt.h>
#include <bcrypt.h>
#include <tlhelp32.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <mutex>
#include <thread>
#include <atomic>
#include <stdexcept>
#include "payload.hpp"
#include "setup_policy.h"
namespace fs=std::filesystem;
HWND window{},statusLabel{},detailLabel{};
std::vector<HWND> buttons;
std::atomic<bool> busy{false};
std::atomic<int> finishedVendor{0};
bool restartPending=false,checkOnly=false;
unsigned long long installationBoot=0;
std::mutex messageMutex;
std::wstring message=L"Verify the components and install only what is missing.";
fs::path base,appDir,stage;
bool cableReady=false,hifiReady=false,cablePresent=false,hifiPresent=false,hifiWrongRate=false;

void fail(const char* s){throw std::runtime_error(s);}
std::wstring widen(const std::string&s){int n=MultiByteToWideChar(CP_UTF8,0,s.data(),int(s.size()),nullptr,0);std::wstring w(n,0);MultiByteToWideChar(CP_UTF8,0,s.data(),int(s.size()),w.data(),n);return w;}
std::string narrow(const std::wstring&s){int n=WideCharToMultiByte(CP_UTF8,0,s.data(),int(s.size()),nullptr,0,nullptr,nullptr);std::string v(n,0);WideCharToMultiByte(CP_UTF8,0,s.data(),int(s.size()),v.data(),n,nullptr,nullptr);return v;}
std::string json(const std::wstring&s){std::string result="\"";for(unsigned char c:narrow(s)){if(c=='"'||c=='\\'){result+='\\';result+=char(c);}else if(c=='\n')result+="\\n";else if(c=='\r')result+="\\r";else if(c>=32)result+=char(c);}return result+'"';}
void report(const std::wstring& value){{std::lock_guard<std::mutex> lock(messageMutex);message=value;}PostMessageW(window,WM_APP+1,0,0);}
std::string digest(const unsigned char* p,DWORD bytes){
 BCRYPT_ALG_HANDLE algorithm{};BCRYPT_HASH_HANDLE hash{};
 if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)fail("SHA256 provider unavailable");
 DWORD size=0,got=0;BCryptGetProperty(algorithm,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&size),sizeof(size),&got,0);
 std::vector<unsigned char> object(size),out(32);
 auto status=BCryptCreateHash(algorithm,&hash,object.data(),size,nullptr,0,0);
 if(status>=0)status=BCryptHashData(hash,const_cast<PUCHAR>(p),bytes,0);
 if(status>=0)status=BCryptFinishHash(hash,out.data(),DWORD(out.size()),0);
 if(hash)BCryptDestroyHash(hash);BCryptCloseAlgorithmProvider(algorithm,0);
 if(status<0)fail("SHA256 calculation failed");
 const char* hex="0123456789abcdef";std::string text;for(auto x:out){text+=hex[x>>4];text+=hex[x&15];}return text;
}
std::pair<const unsigned char*,DWORD> resource(const Payload& file){
 auto r=FindResourceW(nullptr,MAKEINTRESOURCEW(file.id),RT_RCDATA);
 if(!r)fail("Missing installer payload");auto data=LoadResource(nullptr,r);auto bytes=SizeofResource(nullptr,r);
 auto p=static_cast<const unsigned char*>(LockResource(data));
 if(!p||digest(p,bytes)!=file.sha256)fail("Installer payload integrity check failed");return {p,bytes};
}
void extract(const std::wstring& group,const fs::path& destination){
 for(const auto& file:payload){if(group!=file.group)continue;auto [p,n]=resource(file);
  auto target=destination/file.path;fs::create_directories(target.parent_path());
  // Staging uses this user's application data, never a shared temporary folder.
  std::ofstream out(target,std::ios::binary|std::ios::trunc);out.write(reinterpret_cast<const char*>(p),n);out.close();if(!out)fail("Cannot write installation payload");
  std::ifstream check(target,std::ios::binary);std::vector<unsigned char> written((std::istreambuf_iterator<char>(check)),std::istreambuf_iterator<char>());if(written.size()!=n||digest(written.data(),DWORD(written.size()))!=file.sha256)fail("Extracted file failed integrity verification; installation stopped.");
 }
}
bool processRunning(const wchar_t* name){
 HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);if(snapshot==INVALID_HANDLE_VALUE)return true;
 PROCESSENTRY32W row{};row.dwSize=sizeof(row);bool found=false;
 if(Process32FirstW(snapshot,&row))do{if(_wcsicmp(row.szExeFile,name)==0){found=true;break;}}while(Process32NextW(snapshot,&row));CloseHandle(snapshot);return found;
}
bool trustVendor(const fs::path& file){
 WINTRUST_FILE_INFO fi{};fi.cbStruct=sizeof(fi);fi.pcwszFilePath=file.c_str();
 WINTRUST_DATA wd{};wd.cbStruct=sizeof(wd);wd.dwUIChoice=WTD_UI_NONE;wd.fdwRevocationChecks=WTD_REVOKE_WHOLECHAIN;wd.dwUnionChoice=WTD_CHOICE_FILE;wd.pFile=&fi;wd.dwStateAction=WTD_STATEACTION_VERIFY;
 GUID action=WINTRUST_ACTION_GENERIC_VERIFY_V2;auto result=WinVerifyTrust(nullptr,&action,&wd);
 bool expected=false;
 if(result==ERROR_SUCCESS){auto provider=WTHelperProvDataFromStateData(wd.hWVTStateData);auto signer=provider?WTHelperGetProvSignerFromChain(provider,0,FALSE,0):nullptr;
  if(signer&&signer->csCertChain){wchar_t publisher[512]{};CertGetNameStringW(signer->pasCertChain[0].pCert,CERT_NAME_SIMPLE_DISPLAY_TYPE,0,nullptr,publisher,512);std::wstring name=publisher;
   expected=name==L"BUREL VINCENT Entrepreneur individuel"||name==L"Vincent Burel";}
 }
 wd.dwStateAction=WTD_STATEACTION_CLOSE;WinVerifyTrust(nullptr,&action,&wd);return result==ERROR_SUCCESS&&expected;
}
void detect(){
 cableReady=hifiReady=cablePresent=hifiPresent=hifiWrongRate=false;IMMDeviceEnumerator* e=nullptr;
 if(FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,__uuidof(IMMDeviceEnumerator),reinterpret_cast<void**>(&e))))return;
 bool cableIn=false,cableOut=false,hifiIn=false,hifiOut=false;
 for(EDataFlow flow:{eRender,eCapture}){IMMDeviceCollection* list=nullptr;if(FAILED(e->EnumAudioEndpoints(flow,DEVICE_STATEMASK_ALL,&list)))continue;UINT count=0;list->GetCount(&count);
  for(UINT i=0;i<count;++i){IMMDevice* d=nullptr;if(FAILED(list->Item(i,&d)))continue;IPropertyStore* properties=nullptr;
   if(SUCCEEDED(d->OpenPropertyStore(STGM_READ,&properties))){PROPVARIANT value;PropVariantInit(&value);if(SUCCEEDED(properties->GetValue(PKEY_Device_FriendlyName,&value))&&value.vt==VT_LPWSTR){std::wstring name=value.pwszVal;DWORD state=0;d->GetState(&state);bool active=state==DEVICE_STATE_ACTIVE;
    bool goodFormat=false;IAudioClient* client=nullptr;if(active&&SUCCEEDED(d->Activate(__uuidof(IAudioClient),CLSCTX_ALL,nullptr,reinterpret_cast<void**>(&client)))){WAVEFORMATEX* format=nullptr;if(SUCCEEDED(client->GetMixFormat(&format))){goodFormat=format->nSamplesPerSec==48000&&format->nChannels>=2;CoTaskMemFree(format);}client->Release();}
    if(name==L"CABLE Input (VB-Audio Virtual Cable)"&&flow==eRender){cablePresent=true;cableIn=active;}
    if(name==L"CABLE Output (VB-Audio Virtual Cable)"&&flow==eCapture){cablePresent=true;cableOut=active;}
    if(name==L"Hi-Fi Cable Input (VB-Audio Hi-Fi Cable)"&&flow==eRender){hifiPresent=true;hifiIn=active&&goodFormat;if(active&&!goodFormat)hifiWrongRate=true;}
    if(name==L"Hi-Fi Cable Output (VB-Audio Hi-Fi Cable)"&&flow==eCapture){hifiPresent=true;hifiOut=active&&goodFormat;if(active&&!goodFormat)hifiWrongRate=true;}
   }PropVariantClear(&value);properties->Release();}d->Release();
  }list->Release();
 }e->Release();cableReady=cableIn&&cableOut;hifiReady=hifiIn&&hifiOut;
}
bool driverFilesPresent(bool hifi){
 wchar_t windows[MAX_PATH]{};GetWindowsDirectoryW(windows,MAX_PATH);auto dir=fs::path(windows)/L"System32"/L"drivers";
 std::error_code error;
 for(const auto& entry:fs::directory_iterator(dir,error)){auto name=entry.path().filename().wstring();for(auto&c:name)c=towlower(c);
  if(name.ends_with(L".sys")&&(hifi?name.find(L"vbaudio_hifi")!=std::wstring::npos:name.find(L"vbaudio_cable")!=std::wstring::npos))return true;
 }
 return false;
}
unsigned long long bootSeconds(){FILETIME ft{};GetSystemTimeAsFileTime(&ft);ULARGE_INTEGER time{};time.LowPart=ft.dwLowDateTime;time.HighPart=ft.dwHighDateTime;return time.QuadPart/10000000-GetTickCount64()/1000;}
void rememberBoot(){
 fs::create_directories(base);auto temporary=base/L"setup-v2-restart.txt.tmp";std::ofstream out(temporary,std::ios::trunc);out<<installationBoot<<"\n";out.close();if(!out||!MoveFileExW(temporary.c_str(),(base/L"setup-v2-restart.txt").c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))fail("Cannot save restart progress. Keep this setup open until the vendor installation finishes.");
}
void readBoot(){
 std::ifstream in(base/L"setup-v2-restart.txt");in>>installationBoot;
 auto boot=bootSeconds();restartPending=installationBoot&&((boot>installationBoot?boot-installationBoot:installationBoot-boot)<5);
 if(installationBoot&&!restartPending){installationBoot=0;rememberBoot();}
}

bool appAvailable(){return fs::is_regular_file(appDir/L"AudioBridgeApp.exe")&&fs::is_regular_file(appDir/L"audiobridge.exe")&&fs::is_regular_file(appDir/L"bridge-agent.exe");}

void saveState(){
 fs::create_directories(base);auto temp=base/L"setup-v2.json.tmp";std::ofstream out(temp);std::wstring msg;{std::lock_guard<std::mutex> lock(messageMutex);msg=message;}
 out<<"{\"schema\":1,\"vbcable_ready\":"<<(cableReady?"true":"false")<<",\"hifi_ready\":"<<(hifiReady?"true":"false")<<",\"app_installed\":"<<(appAvailable()?"true":"false")<<",\"restart_pending\":"<<(restartPending?"true":"false")<<",\"automatic_restart\":false,\"status\":"<<json(msg)<<"}\n";out.close();if(!out)fail("Cannot save setup progress");
 if(!MoveFileExW(temp.c_str(),(base/L"setup-v2.json").c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))fail("Cannot commit setup progress");
}
void refresh(){
 detect();try{const int completed=finishedVendor.exchange(0);if((completed==1&&cablePresent)||(completed==2&&hifiPresent)){installationBoot=bootSeconds();restartPending=true;rememberBoot();}readBoot();}catch(const std::exception&e){std::lock_guard<std::mutex>lock(messageMutex);message=widen(e.what());}
 std::wstring detail=L"VB-CABLE: "+std::wstring(cableReady?L"audio endpoints ready — preserve existing installation":L"not ready; install or check after restart")+L"\r\n\r\nHi-Fi Cable: "+std::wstring(hifiReady?L"both endpoints ready at 48 kHz — preserve installation":(hifiWrongRate?L"set BOTH playback and recording endpoints to 48 kHz in Sound settings":L"not ready; install or check after restart"))+L"\r\n\r\nGolden Gate Audio: "+std::wstring(appAvailable()?L"installed for this Windows user":L"not installed");
 SetWindowTextW(detailLabel,detail.c_str());std::wstring msg;{std::lock_guard<std::mutex>lock(messageMutex);msg=message;}if(restartPending)msg+=L"\r\nA driver was installed during this boot. Restart Windows when convenient, then reopen setup.";SetWindowTextW(statusLabel,msg.c_str());
 for(auto button:buttons)EnableWindow(button,!busy.load());
 if(buttons.size()>=5){EnableWindow(buttons[1],!busy&&!cableReady);EnableWindow(buttons[2],!busy&&!hifiReady);EnableWindow(buttons[4],!busy&&ab_setup_action(cableReady&&hifiReady,1,restartPending)==AB_KEEP&&appAvailable());}
 try{saveState();}catch(...){}
}
void installApp(){
 if(processRunning(L"AudioBridgeApp.exe")||processRunning(L"audiobridge.exe")||processRunning(L"bridge-agent.exe"))fail("Quit Golden Gate Audio and its background agent before updating. Setup will not interrupt live audio.");
 auto candidate=base/L"app-v0.2-new";std::error_code ec;fs::remove_all(candidate,ec);extract(L"app",candidate);
 auto backup=base/(L"app-backup-"+std::to_wstring(GetTickCount64()));
 if(fs::exists(appDir))fs::rename(appDir,backup);
 try{fs::rename(candidate,appDir);}catch(...){if(fs::exists(backup)&&!fs::exists(appDir))fs::rename(backup,appDir);throw;}
 wchar_t programs[MAX_PATH]{};
 if(SUCCEEDED(SHGetFolderPathW(nullptr,CSIDL_PROGRAMS,nullptr,SHGFP_TYPE_CURRENT,programs))){IShellLinkW* shortcut=nullptr;if(SUCCEEDED(CoCreateInstance(CLSID_ShellLink,nullptr,CLSCTX_INPROC_SERVER,IID_IShellLinkW,reinterpret_cast<void**>(&shortcut)))){
  shortcut->SetPath((appDir/L"AudioBridgeApp.exe").c_str());shortcut->SetWorkingDirectory(appDir.c_str());IPersistFile* persist=nullptr;if(SUCCEEDED(shortcut->QueryInterface(IID_IPersistFile,reinterpret_cast<void**>(&persist)))){persist->Save((fs::path(programs)/L"AudioBridge.lnk").c_str(),TRUE);persist->Release();}shortcut->Release();}}
 report(L"Golden Gate Audio installed for this user. Settings and existing audio drivers were retained. Setup never starts audio automatically.");
}
void removeApp(){
 if(ab_setup_remove_shared_driver()!=0)fail("Unexpected uninstall policy; no changes made.");
 if(processRunning(L"AudioBridgeApp.exe")||processRunning(L"audiobridge.exe")||processRunning(L"bridge-agent.exe"))fail("Quit Golden Gate Audio and its background agent before removing the app.");
 // Only files owned by this bootstrap's app payload are removed. Drivers, user
 // configuration, pairing records, setup state and previous-version backups stay.
 for(const auto& file:payload){if(std::wstring(file.group)!=L"app")continue;auto target=appDir/file.path;if(fs::exists(target)&&fs::is_regular_file(target))fs::remove(target);}
 std::error_code error;fs::remove(appDir/L"licenses",error);fs::remove(appDir,error);
 wchar_t programs[MAX_PATH]{};if(SUCCEEDED(SHGetFolderPathW(nullptr,CSIDL_PROGRAMS,nullptr,SHGFP_TYPE_CURRENT,programs)))fs::remove(fs::path(programs)/L"AudioBridge.lnk",error);
 report(L"Golden Gate Audio app removed. Shared audio drivers, pairing/settings, setup files and previous-version backups were retained.");
}

void installVendor(bool hifi,bool ready,bool present){
 // Never use the installer as a blind repair/uninstaller when anything exists.
 const auto action=ab_setup_action(ready,present||driverFilesPresent(hifi),0);
 if(action==AB_KEEP){report(L"Component already ready; existing installation preserved.");return;}
 if(action!=AB_INSTALL)fail("Driver files already exist but its audio endpoints are unavailable. Restart or review the existing installation; automatic replacement is disabled.");
 auto dir=stage/(hifi?L"hifi":L"vbcable");extract(hifi?L"hifi":L"vbcable",dir);
 auto file=dir/(hifi?L"HiFiCableAsioBridgeSetup.exe":L"VBCABLE_Setup_x64.exe");
 if(!trustVendor(file))fail("Windows could not validate the expected VB-Audio publisher. Installer was not started. Check network/certificate availability.");
 report(L"Opening the original vendor installer. Follow its prompts. Defer a restart until both audio components are installed, then reopen this setup to check them.");
 SHELLEXECUTEINFOW launch{};launch.cbSize=sizeof(launch);launch.fMask=SEE_MASK_NOCLOSEPROCESS;launch.hwnd=window;launch.lpVerb=L"runas";launch.lpFile=file.c_str();launch.lpDirectory=dir.c_str();launch.nShow=SW_SHOWNORMAL;
 if(!ShellExecuteExW(&launch))fail(GetLastError()==ERROR_CANCELLED?"Installation cancelled at the Windows administrator prompt. You can retry.":"Could not open the vendor installer.");
 if(launch.hProcess){WaitForSingleObject(launch.hProcess,INFINITE);DWORD code=0;GetExitCodeProcess(launch.hProcess,&code);CloseHandle(launch.hProcess);if(ab_setup_installer_result((code==0||code==3010||code==1641)?AB_RETURNED:AB_FAILED)==AB_FAILED_RETRY)fail("Vendor installer returned an error. Check its message and retry setup.");}
 finishedVendor.store(hifi?2:1);
 report(L"Vendor installer closed. Its exit status is not proof of working audio. Check the endpoints below; follow the vendor's restart instruction before using audio. Setup will not restart Windows.");
}
void asyncAction(int id){
 if(busy.exchange(true))return;refresh();const bool ready=id==103?hifiReady:cableReady,present=id==103?hifiPresent:cablePresent;
 std::thread([id,ready,present]{CoInitializeEx(nullptr,COINIT_MULTITHREADED);try{if(id==101)installApp();else if(id==108)removeApp();else installVendor(id==103,ready,present);}catch(const std::exception&e){report(widen(e.what()));}CoUninitialize();busy=false;PostMessageW(window,WM_APP+1,0,0);}).detach();
}
LRESULT CALLBACK procedure(HWND h,UINT m,WPARAM w,LPARAM l){
 switch(m){case WM_COMMAND:{auto id=LOWORD(w);if(id>=101&&id<=103)asyncAction(id);else if(id==108){if(MessageBoxW(h,L"Remove the Golden Gate Audio app for this user? Shared audio drivers and pairing/settings will be kept.",L"Remove app only",MB_YESNO|MB_ICONQUESTION)==IDYES)asyncAction(id);}else if(id==104)refresh();else if(id==105)ShellExecuteW(h,L"open",(appDir/L"AudioBridgeApp.exe").c_str(),nullptr,appDir.c_str(),SW_SHOWNORMAL);else if(id==109)MessageBoxW(h,L"On your trusted home network, allow bridge-agent.exe and audiobridge.exe through Windows Defender Firewall for Private networks. Do not enable Public access or disable the firewall.\n\nIf room discovery fails, use Windows Security > Firewall & network protection > Allow an app through firewall and select the installed binaries in LocalAppData\\AudioBridge\\app.\n\nRoom discovery/control uses UDP/TCP 45671; audio uses UDP 45670. Guest Wi-Fi client isolation can also prevent pairing.",L"Private-network firewall setup",MB_OK|MB_ICONINFORMATION);else if(id==107)ShellExecuteW(h,L"open",L"control.exe",L"mmsys.cpl",nullptr,SW_SHOWNORMAL);else if(id==106)ShellExecuteW(h,L"open",L"https://vb-audio.com/Services/licensing.htm",nullptr,nullptr,SW_SHOWNORMAL);return 0;}
 case WM_APP+1:refresh();return 0;
 case WM_CLOSE:if(busy){MessageBoxW(h,L"Finish or cancel the vendor installer first. Setup will not kill it.",L"Installation in progress",MB_OK);return 0;}DestroyWindow(h);return 0;
 case WM_DESTROY:PostQuitMessage(0);return 0;}
 return DefWindowProcW(h,m,w,l);
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int){
 try{
  int count=0;auto argv=CommandLineToArgvW(GetCommandLineW(),&count);checkOnly=count==2&&std::wstring(argv[1])==L"--check-payload";LocalFree(argv);
  for(const auto&file:payload)resource(file);if(checkOnly)return 0;
  HANDLE singleton=CreateMutexW(nullptr,TRUE,L"Local\\AudioBridgePrivateSetupV2");if(!singleton||GetLastError()==ERROR_ALREADY_EXISTS)fail("Golden Gate Audio Setup is already running.");
  CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);PWSTR local=nullptr;if(FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&local)))fail("Cannot locate per-user application data");base=fs::path(local)/L"AudioBridge";CoTaskMemFree(local);appDir=base/L"app";stage=base/L"setup-v2-payload";
  WNDCLASSW wc{};wc.lpfnWndProc=procedure;wc.hInstance=instance;wc.lpszClassName=L"AudioBridgePrivateSetup";wc.hCursor=LoadCursor(nullptr,IDC_ARROW);wc.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);RegisterClassW(&wc);
  window=CreateWindowW(wc.lpszClassName,L"Golden Gate Audio Setup — private build",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,CW_USEDEFAULT,CW_USEDEFAULT,760,535,nullptr,nullptr,instance,nullptr);
  auto label=[&](const wchar_t* text,int y,int height){return CreateWindowW(L"STATIC",text,WS_CHILD|WS_VISIBLE,24,y,700,height,window,nullptr,instance,nullptr);};
  label(L"Golden Gate Audio — existing audio components",20,30);detailLabel=label(L"Checking installed endpoints...",60,125);
  const wchar_t* titles[]{L"Install / update app",L"Install VB-CABLE",L"Install Hi-Fi Cable",L"Check again",L"Open app"};
  for(int i=0;i<5;++i){int row=i/3,col=i%3;buttons.push_back(CreateWindowW(L"BUTTON",titles[i],WS_CHILD|WS_VISIBLE|WS_TABSTOP,24+col*236,200+row*42,224,32,window,reinterpret_cast<HMENU>(INT_PTR(101+i)),instance,nullptr));}
  statusLabel=label(L"",300,70);label(L"VB-CABLE / Hi-Fi Cable by VB-Audio. Donationware — contributions welcome.\r\nVendor prompts may require a restart; Golden Gate Audio never restarts Windows.",378,40);
  buttons.push_back(CreateWindowW(L"BUTTON",L"Vendor licensing and support",WS_CHILD|WS_VISIBLE|WS_TABSTOP,24,422,240,24,window,reinterpret_cast<HMENU>(106),instance,nullptr));
  buttons.push_back(CreateWindowW(L"BUTTON",L"Sound settings (48 kHz)",WS_CHILD|WS_VISIBLE|WS_TABSTOP,280,422,240,24,window,reinterpret_cast<HMENU>(107),instance,nullptr));
  buttons.push_back(CreateWindowW(L"BUTTON",L"Firewall help",WS_CHILD|WS_VISIBLE|WS_TABSTOP,532,422,180,24,window,reinterpret_cast<HMENU>(109),instance,nullptr));
  buttons.push_back(CreateWindowW(L"BUTTON",L"Remove app only",WS_CHILD|WS_VISIBLE|WS_TABSTOP,24,458,224,28,window,reinterpret_cast<HMENU>(108),instance,nullptr));
  refresh();ShowWindow(window,SW_SHOWNORMAL);MSG msg;while(GetMessageW(&msg,nullptr,0,0)>0){if(!IsDialogMessageW(window,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}}CoUninitialize();CloseHandle(singleton);return 0;
 }catch(const std::exception&e){if(checkOnly)return 1;MessageBoxW(nullptr,widen(e.what()).c_str(),L"Golden Gate Audio Setup",MB_ICONERROR|MB_OK);return 1;}
}
