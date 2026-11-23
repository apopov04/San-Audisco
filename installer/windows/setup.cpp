// Private bootstrap. Vendor programs are unchanged and retain their normal UI.
// No driver removal, certificate-store writes, automatic restart or security overrides.
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <propkey.h>
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
#include <algorithm>
#include <memory>
#include "payload.hpp"
#include "setup_policy.h"
namespace fs=std::filesystem;
HWND window{},statusLabel{},detailLabel{},primaryButton{},stepLabel{},logo{};
HWND cardTitle[3]{},cardDescription[3]{},cardStatus[3]{},cardFrame[3]{};
std::vector<HWND> buttons,detailControls;
std::atomic<bool> busy{false};
std::atomic<int> finishedVendor{0};
bool restartPending=false,checkOnly=false,verified=false,started=false,details=false,appCurrent=false;
unsigned long long installationBoot=0;
std::mutex messageMutex;
std::wstring actionError;
std::wstring message=L"Connect two computers and listen through one output. Setup checks this computer and guides you through anything it needs.";
fs::path base,appDir,stage;
bool cableReady=false,hifiReady=false,cablePresent=false,hifiPresent=false,hifiWrongRate=false;
int dpi=96,scrollOffset=0;
HFONT bodyFont{},titleFont{},headingFont{};
HICON brandIcon{};
HBRUSH cardBrush{};
bool highContrast=false;
struct Inventory {bool cableReady=false,hifiReady=false,cablePresent=false,hifiPresent=false,hifiWrongRate=false,appCurrent=false,verified=false,restart=false;unsigned long long boot=0;std::wstring error;};
int px(int n){return MulDiv(n,dpi,96);}
void render();void layout();void beginCheck();

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
Inventory detect(){
 Inventory result;bool &cableReady=result.cableReady,&hifiReady=result.hifiReady,&cablePresent=result.cablePresent,&hifiPresent=result.hifiPresent,&hifiWrongRate=result.hifiWrongRate;IMMDeviceEnumerator* e=nullptr;
 if(FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,__uuidof(IMMDeviceEnumerator),reinterpret_cast<void**>(&e))))return result;
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
 }e->Release();cableReady=cableIn&&cableOut;hifiReady=hifiIn&&hifiOut;return result;
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
void rememberBoot(unsigned long long boot){
 fs::create_directories(base);auto temporary=base/L"setup-v2-restart.txt.tmp";std::ofstream out(temporary,std::ios::trunc);out<<boot<<"\n";out.close();if(!out||!MoveFileExW(temporary.c_str(),(base/L"setup-v2-restart.txt").c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))fail("Cannot save restart progress. Keep this setup open until the vendor installation finishes.");
}

bool appAvailable(){return fs::is_regular_file(appDir/L"SanAudisco.exe")&&fs::is_regular_file(appDir/L"san-audisco.exe")&&fs::is_regular_file(appDir/L"bridge-agent.exe");}

void saveState(){
 fs::create_directories(base);auto temp=base/L"setup-v2.json.tmp";std::ofstream out(temp);std::wstring msg;{std::lock_guard<std::mutex> lock(messageMutex);msg=message;}
 out<<"{\"schema\":1,\"vbcable_ready\":"<<(cableReady?"true":"false")<<",\"hifi_ready\":"<<(hifiReady?"true":"false")<<",\"app_installed\":"<<(appAvailable()?"true":"false")<<",\"restart_pending\":"<<(restartPending?"true":"false")<<",\"automatic_restart\":false,\"status\":"<<json(msg)<<"}\n";out.close();if(!out)fail("Cannot save setup progress");
 if(!MoveFileExW(temp.c_str(),(base/L"setup-v2.json").c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))fail("Cannot commit setup progress");
}
bool currentApp(){
 for(const auto& file:payload){if(std::wstring(file.group)!=L"app")continue;std::wstring name=file.path;
  if(name!=L"SanAudisco.exe"&&name!=L"san-audisco.exe"&&name!=L"bridge-agent.exe")continue;
  std::ifstream in(appDir/name,std::ios::binary);if(!in)return false;std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(in)),{});
  if(bytes.size()>MAXDWORD||digest(bytes.data(),DWORD(bytes.size()))!=file.sha256)return false;
 }return appAvailable();
}
int nextStep(){return ab_setup_next(verified,appCurrent,cableReady,cablePresent,hifiReady,hifiPresent,restartPending);}
void beginCheck(){
 if(busy.exchange(true))return;started=true;{std::lock_guard lock(messageMutex);message=L"Checking the package and audio components…";}render();const bool alreadyVerified=verified;
 std::thread([alreadyVerified]{CoInitializeEx(nullptr,COINIT_MULTITHREADED);auto result=std::make_unique<Inventory>();try{
  if(!alreadyVerified)for(const auto&file:payload)resource(file);
  *result=detect();result->verified=true;result->appCurrent=currentApp();
  result->cablePresent=result->cablePresent||driverFilesPresent(false);result->hifiPresent=result->hifiPresent||driverFilesPresent(true);
  std::ifstream previous(base/L"setup-v2-restart.txt");previous>>result->boot;
  auto now=bootSeconds();const auto completed=finishedVendor.exchange(0);
  if((completed==1&&result->cablePresent)||(completed==2&&result->hifiPresent)){result->boot=now;rememberBoot(now);}
  result->restart=result->boot&&((now>result->boot?now-result->boot:result->boot-now)<5);
  if(result->boot&&!result->restart){result->boot=0;rememberBoot(0);}
 }catch(const std::exception&e){result->error=widen(e.what());}
 CoUninitialize();PostMessageW(window,WM_APP+2,0,reinterpret_cast<LPARAM>(result.release()));}).detach();
}
void describeNext(){
 std::wstring value;
 switch(nextStep()){
 case AB_APP:value=L"Install the app to continue. Your rooms, settings, and existing audio components will be kept.";break;
 case AB_FIRST_COMPONENT:case AB_SECOND_COMPONENT:value=L"One audio component is missing. Its original installer will open and may ask for administrator access.";break;
 case AB_NEEDS_REVIEW:value=hifiWrongRate?L"The shared microphone needs 48 kHz on both Hi-Fi Cable endpoints. Open Details → Sound settings, then check again.":L"An existing audio component needs attention. Open Details to review it. Setup will preserve existing files.";break;
 case AB_NEEDS_RESTART:value=L"Restart Windows when convenient, then reopen this setup. Your progress is saved.";break;
 case AB_OPEN:value=L"This computer is ready. Open San Audisco to create or join a room, choose your hubs, and run Autoconfigure.";break;
 default:value=L"The package must pass its checks before anything is installed.";
 }{std::lock_guard lock(messageMutex);message=value;}
}
void render(){
 const auto next=nextStep();int phase=!started?0:!verified?1:next==AB_OPEN?3:2;
 const wchar_t* stages[]{L"1  Welcome     ›     2  Check     ›     3  Set up     ›     4  Ready",L"Welcome     ›     2  Checking this computer     ›     Set up     ›     Ready",L"Welcome     ›     Check     ›     3  Set up     ›     Ready",L"Welcome     ›     Check     ›     Set up     ›     4  Ready"};
 SetWindowTextW(stepLabel,stages[phase]);
 auto status=[&](bool ready,bool present){return !started?L"Not checked":busy?L"Checking…":restartPending&&present?L"Restart required":ready?L"Ready":present?L"Needs attention":L"Missing";};
 SetWindowTextW(cardStatus[0],!started?L"Not checked":busy?L"Checking…":appCurrent?L"Ready":appAvailable()?L"Update available":L"Missing");
 SetWindowTextW(cardStatus[1],status(cableReady,cablePresent));SetWindowTextW(cardStatus[2],status(hifiReady,hifiPresent));
 std::wstring technical=L"VB-CABLE: "+std::wstring(cableReady?L"both audio endpoints available.":cablePresent?L"existing component found; endpoints unavailable.":L"not installed.")+L"\r\nHi-Fi Cable: "+std::wstring(hifiReady?L"both endpoints available at 48 kHz.":hifiWrongRate?L"set playback AND recording endpoints to 48 kHz.":hifiPresent?L"existing component found; endpoints unavailable.":L"not installed.")+L"\r\nApp: "+appDir.wstring()+L"\r\nVB-CABLE and Hi-Fi Cable by VB-Audio. Original packages and notices are included unchanged.";
 SetWindowTextW(detailLabel,technical.c_str());std::wstring msg;{std::lock_guard lock(messageMutex);msg=message;}SetWindowTextW(statusLabel,msg.c_str());
 const wchar_t* primary=!started?L"Check this computer":busy?L"Working…":next==AB_APP?L"Install / update San Audisco":next==AB_FIRST_COMPONENT?L"Install playback component":next==AB_SECOND_COMPONENT?L"Install microphone component":next==AB_OPEN?L"Open San Audisco":L"Check again";
 SetWindowTextW(primaryButton,primary);for(auto button:buttons)EnableWindow(button,!busy);EnableWindow(primaryButton,!busy);EnableWindow(GetDlgItem(window,108),!busy&&verified&&appAvailable());
 SetWindowTextW(GetDlgItem(window,110),details?L"Hide details":L"Details");
 for(auto h:detailControls)ShowWindow(h,details?SW_SHOW:SW_HIDE);
 try{if(started&&!busy)saveState();}catch(const std::exception&e){SetWindowTextW(statusLabel,widen(e.what()).c_str());}
}

void installApp(){
 if(processRunning(L"SanAudisco.exe")||processRunning(L"san-audisco.exe")||processRunning(L"AudioBridgeApp.exe")||processRunning(L"audiobridge.exe")||processRunning(L"bridge-agent.exe"))fail("Quit San Audisco and its background agent before updating. Setup will not interrupt live audio.");
 auto candidate=base/L"app-install-new";std::error_code ec;fs::remove_all(candidate,ec);extract(L"app",candidate);
 auto backup=base/(L"app-backup-"+std::to_wstring(GetTickCount64()));
 if(fs::exists(appDir))fs::rename(appDir,backup);
 fs::create_directories(appDir.parent_path());
 try{fs::rename(candidate,appDir);}catch(...){if(fs::exists(backup)&&!fs::exists(appDir))fs::rename(backup,appDir);throw;}
 wchar_t programs[MAX_PATH]{};
 if(SUCCEEDED(SHGetFolderPathW(nullptr,CSIDL_PROGRAMS,nullptr,SHGFP_TYPE_CURRENT,programs))){IShellLinkW* shortcut=nullptr;if(SUCCEEDED(CoCreateInstance(CLSID_ShellLink,nullptr,CLSCTX_INPROC_SERVER,IID_IShellLinkW,reinterpret_cast<void**>(&shortcut)))){
  shortcut->SetPath((appDir/L"SanAudisco.exe").c_str());shortcut->SetWorkingDirectory(appDir.c_str());shortcut->SetDescription(L"San Audisco");shortcut->SetIconLocation((appDir/L"SanAudisco.exe").c_str(),0);IPropertyStore* properties=nullptr;if(SUCCEEDED(shortcut->QueryInterface(IID_IPropertyStore,reinterpret_cast<void**>(&properties)))){PROPVARIANT identity{};identity.vt=VT_LPWSTR;identity.pwszVal=const_cast<LPWSTR>(L"SanAudisco.Desktop");properties->SetValue(PKEY_AppUserModel_ID,identity);properties->Commit();properties->Release();}IPersistFile* persist=nullptr;if(SUCCEEDED(shortcut->QueryInterface(IID_IPersistFile,reinterpret_cast<void**>(&persist)))){persist->Save((fs::path(programs)/L"San Audisco.lnk").c_str(),TRUE);persist->Release();}shortcut->Release();}}
 // Retire only this app's known old shortcut and migrate its login command.
 auto legacy=base/L"app";auto legacyExe=legacy/L"AudioBridgeApp.exe";
 if(SUCCEEDED(SHGetFolderPathW(nullptr,CSIDL_PROGRAMS,nullptr,SHGFP_TYPE_CURRENT,programs))){
  auto oldLink=fs::path(programs)/L"AudioBridge.lnk";IShellLinkW* link=nullptr;
  if(SUCCEEDED(CoCreateInstance(CLSID_ShellLink,nullptr,CLSCTX_INPROC_SERVER,IID_IShellLinkW,reinterpret_cast<void**>(&link)))){
   IPersistFile* file=nullptr;if(SUCCEEDED(link->QueryInterface(IID_IPersistFile,reinterpret_cast<void**>(&file)))){
    wchar_t target[32768]{};if(SUCCEEDED(file->Load(oldLink.c_str(),STGM_READ))&&SUCCEEDED(link->GetPath(target,32768,nullptr,SLGP_RAWPATH))&&fs::path(target)==legacyExe)fs::remove(oldLink,ec);
    file->Release();
   }link->Release();
  }
 }
 HKEY run=nullptr;
 if(RegOpenKeyExW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",0,KEY_QUERY_VALUE|KEY_SET_VALUE,&run)==ERROR_SUCCESS){
  wchar_t value[32768]{};DWORD size=sizeof value,type=0;
  if(RegQueryValueExW(run,L"AudioBridge",nullptr,&type,reinterpret_cast<BYTE*>(value),&size)==ERROR_SUCCESS&&type==REG_SZ&&std::wstring(value)==L"\""+legacyExe.wstring()+L"\" --background"){
   auto replacement=L"\""+(appDir/L"SanAudisco.exe").wstring()+L"\" --background";
   if(RegSetValueExW(run,L"San Audisco",0,REG_SZ,reinterpret_cast<const BYTE*>(replacement.c_str()),DWORD((replacement.size()+1)*sizeof(wchar_t)))==ERROR_SUCCESS)RegDeleteValueW(run,L"AudioBridge");
  }RegCloseKey(run);
 }
 if(fs::exists(legacyExe))fs::rename(legacy,base/(L"legacy-app-backup-"+std::to_wstring(GetTickCount64())));
 report(L"San Audisco installed for this user. Settings and existing audio drivers were retained. Setup never starts audio automatically.");
}
void removeApp(){
 if(ab_setup_remove_shared_driver()!=0)fail("Unexpected uninstall policy; no changes made.");
 if(processRunning(L"SanAudisco.exe")||processRunning(L"san-audisco.exe")||processRunning(L"AudioBridgeApp.exe")||processRunning(L"audiobridge.exe")||processRunning(L"bridge-agent.exe"))fail("Quit San Audisco and its background agent before removing the app.");
 // Only files owned by this bootstrap's app payload are removed. Drivers, user
 // configuration, pairing records, setup state and previous-version backups stay.
 for(const auto& file:payload){if(std::wstring(file.group)!=L"app")continue;auto target=appDir/file.path;if(fs::exists(target)&&fs::is_regular_file(target))fs::remove(target);}
 std::error_code error;fs::remove(appDir/L"licenses",error);fs::remove(appDir,error);
 wchar_t programs[MAX_PATH]{};if(SUCCEEDED(SHGetFolderPathW(nullptr,CSIDL_PROGRAMS,nullptr,SHGFP_TYPE_CURRENT,programs)))fs::remove(fs::path(programs)/L"San Audisco.lnk",error);
 report(L"San Audisco app removed. Shared audio drivers, pairing/settings, setup files and previous-version backups were retained.");
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
 if(!verified||busy.exchange(true))return;render();const bool ready=id==103?hifiReady:cableReady,present=id==103?hifiPresent:cablePresent;
 std::thread([id,ready,present]{CoInitializeEx(nullptr,COINIT_MULTITHREADED);try{if(id==101)installApp();else if(id==108)removeApp();else installVendor(id==103,ready,present);}catch(const std::exception&e){report(widen(e.what()));CoUninitialize();PostMessageW(window,WM_APP+3,1,0);return;}CoUninitialize();PostMessageW(window,WM_APP+3,0,0);}).detach();
}
void primaryAction(){
 if(busy)return;if(!started){beginCheck();return;}
 switch(nextStep()){case AB_APP:asyncAction(101);break;case AB_FIRST_COMPONENT:asyncAction(102);break;case AB_SECOND_COMPONENT:asyncAction(103);break;case AB_OPEN:ShellExecuteW(window,L"open",(appDir/L"SanAudisco.exe").c_str(),nullptr,appDir.c_str(),SW_SHOWNORMAL);break;default:beginCheck();}
}
void position(HWND h,int x,int y,int w,int height){SetWindowPos(h,nullptr,px(x),px(y)-scrollOffset,px(w),px(height),SWP_NOZORDER|SWP_NOACTIVATE);}
void fonts(){
 if(bodyFont)DeleteObject(bodyFont);if(titleFont)DeleteObject(titleFont);if(headingFont)DeleteObject(headingFont);
 bodyFont=CreateFontW(-px(15),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
 titleFont=CreateFontW(-px(27),0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
 headingFont=CreateFontW(-px(17),0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
 for(auto h=GetWindow(window,GW_CHILD);h;h=GetWindow(h,GW_HWNDNEXT))SendMessageW(h,WM_SETFONT,reinterpret_cast<WPARAM>(bodyFont),TRUE);
 SendMessageW(GetDlgItem(window,201),WM_SETFONT,reinterpret_cast<WPARAM>(titleFont),TRUE);for(auto h:cardTitle)SendMessageW(h,WM_SETFONT,reinterpret_cast<WPARAM>(headingFont),TRUE);
 if(brandIcon)DestroyIcon(brandIcon);brandIcon=static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr),MAKEINTRESOURCEW(101),IMAGE_ICON,px(72),px(72),0));SendMessageW(logo,STM_SETICON,reinterpret_cast<WPARAM>(brandIcon),0);
}
void layout(){
 RECT client{};GetClientRect(window,&client);const int width=std::max(620,MulDiv(client.right,96,dpi));const int content=details?850:620;
 SCROLLINFO si{sizeof(si),SIF_RANGE|SIF_PAGE|SIF_POS};si.nMin=0;si.nMax=px(content)-1;si.nPage=client.bottom;scrollOffset=std::clamp(scrollOffset,0,std::max(0,px(content)-int(client.bottom)));si.nPos=scrollOffset;SetScrollInfo(window,SB_VERT,&si,TRUE);
 position(logo,28,24,72,72);position(GetDlgItem(window,201),118,26,width-150,36);position(GetDlgItem(window,202),118,68,width-150,28);position(stepLabel,28,120,width-56,30);
 for(int i=0;i<3;++i){int y=166+i*80;position(cardFrame[i],28,y,width-56,72);position(cardTitle[i],46,y+13,width-260,26);position(cardDescription[i],46,y+43,width-260,22);position(cardStatus[i],width-210,y+25,160,26);}
 position(statusLabel,30,420,width-60,70);position(GetDlgItem(window,110),28,504,135,30);position(detailLabel,30,548,width-60,118);
 position(GetDlgItem(window,106),28,680,220,30);position(GetDlgItem(window,107),260,680,220,30);position(GetDlgItem(window,109),492,680,128,30);position(GetDlgItem(window,108),28,724,220,30);
 const int footer=details?792:562;position(GetDlgItem(window,203),28,footer+6,width-330,36);position(primaryButton,width-300,footer,272,40);
 InvalidateRect(window,nullptr,TRUE);
}
LRESULT CALLBACK procedure(HWND h,UINT m,WPARAM w,LPARAM l){
 switch(m){
 case WM_CTLCOLORSTATIC:{auto dc=reinterpret_cast<HDC>(w);int id=GetDlgCtrlID(reinterpret_cast<HWND>(l));const bool card=id>=220&&id<=242;SetBkMode(dc,TRANSPARENT);SetTextColor(dc,GetSysColor(COLOR_WINDOWTEXT));return reinterpret_cast<LRESULT>(card&&!highContrast?cardBrush:GetSysColorBrush(COLOR_WINDOW));}
 case WM_DRAWITEM:{auto item=reinterpret_cast<DRAWITEMSTRUCT*>(l);if(item->CtlID>=210&&item->CtlID<=212){FillRect(item->hDC,&item->rcItem,GetSysColorBrush(COLOR_WINDOW));auto pen=CreatePen(PS_SOLID,1,highContrast?GetSysColor(COLOR_WINDOWTEXT):RGB(231,234,239));auto oldPen=SelectObject(item->hDC,pen),oldBrush=SelectObject(item->hDC,highContrast?GetSysColorBrush(COLOR_WINDOW):cardBrush);RoundRect(item->hDC,item->rcItem.left,item->rcItem.top,item->rcItem.right,item->rcItem.bottom,px(12),px(12));SelectObject(item->hDC,oldPen);SelectObject(item->hDC,oldBrush);DeleteObject(pen);return TRUE;}break;}
 case WM_COMMAND:{auto id=LOWORD(w);if(id==100)primaryAction();else if(id==110){details=!details;render();layout();}else if(id==108){if(MessageBoxW(h,L"Remove the San Audisco app for this user? Shared audio drivers and pairing/settings will be kept.",L"Remove app only",MB_YESNO|MB_ICONQUESTION)==IDYES)asyncAction(id);}else if(id==109)MessageBoxW(h,L"On your trusted home network, allow bridge-agent.exe and san-audisco.exe through Windows Defender Firewall for Private networks.\n\nRoom discovery/control uses UDP/TCP 45671; audio uses UDP 45670. Guest Wi-Fi client isolation can prevent pairing.",L"Connect computers on your network",MB_OK|MB_ICONINFORMATION);else if(id==107)ShellExecuteW(h,L"open",L"control.exe",L"mmsys.cpl",nullptr,SW_SHOWNORMAL);else if(id==106)ShellExecuteW(h,L"open",L"https://vb-audio.com/Services/licensing.htm",nullptr,nullptr,SW_SHOWNORMAL);return 0;}
 case WM_APP+1:render();return 0;
 case WM_APP+2:{std::unique_ptr<Inventory> result(reinterpret_cast<Inventory*>(l));cableReady=result->cableReady;hifiReady=result->hifiReady;cablePresent=result->cablePresent;hifiPresent=result->hifiPresent;hifiWrongRate=result->hifiWrongRate;appCurrent=result->appCurrent;verified=result->verified;restartPending=result->restart;installationBoot=result->boot;busy=false;if(result->error.empty())describeNext();else{std::lock_guard lock(messageMutex);message=result->error;}if(!actionError.empty()){std::lock_guard lock(messageMutex);message=actionError+L"\r\n"+message;actionError.clear();}render();return 0;}
 case WM_APP+3:busy=false;if(w){std::lock_guard lock(messageMutex);actionError=message;}beginCheck();return 0;
 case WM_SIZE:if(primaryButton)layout();return 0;
 case WM_DPICHANGED:{dpi=HIWORD(w);auto r=reinterpret_cast<RECT*>(l);SetWindowPos(h,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);fonts();layout();return 0;}
 case WM_GETMINMAXINFO:{auto info=reinterpret_cast<MINMAXINFO*>(l);RECT area{};SystemParametersInfoW(SPI_GETWORKAREA,0,&area,0);info->ptMinTrackSize.x=std::min(px(660),int(area.right-area.left));info->ptMinTrackSize.y=std::min(px(450),int(area.bottom-area.top));return 0;}
 case WM_VSCROLL:{SCROLLINFO si{sizeof(si),SIF_ALL};GetScrollInfo(h,SB_VERT,&si);switch(LOWORD(w)){case SB_LINEUP:scrollOffset-=px(30);break;case SB_LINEDOWN:scrollOffset+=px(30);break;case SB_PAGEUP:scrollOffset-=int(si.nPage);break;case SB_PAGEDOWN:scrollOffset+=int(si.nPage);break;case SB_THUMBTRACK:scrollOffset=si.nTrackPos;break;}layout();return 0;}
 case WM_MOUSEWHEEL:scrollOffset-=GET_WHEEL_DELTA_WPARAM(w)/WHEEL_DELTA*px(60);layout();return 0;
 case WM_CLOSE:if(busy){MessageBoxW(h,L"Finish or cancel the current operation first. Setup will not interrupt an installer.",L"Setup is working",MB_OK);return 0;}DestroyWindow(h);return 0;
 case WM_DESTROY:if(cardBrush)DeleteObject(cardBrush);if(brandIcon)DestroyIcon(brandIcon);DeleteObject(bodyFont);DeleteObject(titleFont);DeleteObject(headingFont);PostQuitMessage(0);return 0;
 }return DefWindowProcW(h,m,w,l);
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int){
 try{
  int count=0;auto argv=CommandLineToArgvW(GetCommandLineW(),&count);checkOnly=count==2&&std::wstring(argv[1])==L"--check-payload";LocalFree(argv);
  if(checkOnly){for(const auto&file:payload)resource(file);return 0;}
  HANDLE singleton=CreateMutexW(nullptr,TRUE,L"Local\\AudioBridgePrivateSetupV2");if(!singleton||GetLastError()==ERROR_ALREADY_EXISTS)fail("San Audisco Setup is already running.");
  CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);PWSTR local=nullptr;if(FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&local)))fail("Cannot locate per-user application data");base=fs::path(local)/L"AudioBridge";CoTaskMemFree(local);appDir=base.parent_path()/L"SanAudisco"/L"app";stage=base/L"setup-v2-payload";
  auto setDpi=reinterpret_cast<BOOL(WINAPI*)(HANDLE)>(GetProcAddress(GetModuleHandleW(L"user32.dll"),"SetProcessDpiAwarenessContext"));if(setDpi)setDpi(reinterpret_cast<HANDLE>(-4));else SetProcessDPIAware();
  WNDCLASSW wc{};wc.lpfnWndProc=procedure;wc.hInstance=instance;wc.lpszClassName=L"SanAudiscoPrivateSetup";wc.hCursor=LoadCursor(nullptr,IDC_ARROW);wc.hIcon=LoadIconW(instance,MAKEINTRESOURCEW(101));wc.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);RegisterClassW(&wc);
  window=CreateWindowW(wc.lpszClassName,L"San Audisco Setup",WS_OVERLAPPEDWINDOW|WS_VSCROLL|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,800,700,nullptr,nullptr,instance,nullptr);auto getDpi=reinterpret_cast<UINT(WINAPI*)(HWND)>(GetProcAddress(GetModuleHandleW(L"user32.dll"),"GetDpiForWindow"));if(getDpi)dpi=int(getDpi(window));else{auto dc=GetDC(window);dpi=GetDeviceCaps(dc,LOGPIXELSX);ReleaseDC(window,dc);}
  auto item=[&](const wchar_t* cls,const wchar_t* text,DWORD style,int id){auto h=CreateWindowW(cls,text,WS_CHILD|WS_VISIBLE|style,0,0,0,0,window,reinterpret_cast<HMENU>(INT_PTR(id)),instance,nullptr);return h;};
  HIGHCONTRASTW contrast{sizeof(contrast)};SystemParametersInfoW(SPI_GETHIGHCONTRAST,sizeof(contrast),&contrast,0);highContrast=contrast.dwFlags&HCF_HIGHCONTRASTON;cardBrush=CreateSolidBrush(RGB(247,248,250));
  logo=item(L"STATIC",L"",SS_ICON,200);item(L"STATIC",L"San Audisco",0,201);item(L"STATIC",L"One place for your computers’ audio.",0,202);stepLabel=item(L"STATIC",L"",0,204);
  const wchar_t* titles[]{L"San Audisco app",L"Computer audio",L"Shared microphone"};const wchar_t* captions[]{L"Your rooms and settings stay with you.",L"Brings this computer’s sound into the room.",L"Makes your microphone available to voice apps."};
  for(int i=0;i<3;++i){cardFrame[i]=item(L"BUTTON",L"",BS_OWNERDRAW,210+i);cardTitle[i]=item(L"STATIC",titles[i],0,220+i);cardDescription[i]=item(L"STATIC",captions[i],0,230+i);cardStatus[i]=item(L"STATIC",L"Not checked",SS_RIGHT,240+i);}
  statusLabel=item(L"STATIC",L"",0,250);detailLabel=item(L"STATIC",L"",0,251);detailControls.push_back(detailLabel);
  buttons.push_back(item(L"BUTTON",L"Details",WS_TABSTOP,110));
  const wchar_t* labels[]{L"Vendor information",L"Sound settings (48 kHz)",L"Network help",L"Remove app only"};const int ids[]{106,107,109,108};for(int i=0;i<4;++i){auto h=item(L"BUTTON",labels[i],WS_TABSTOP,ids[i]);buttons.push_back(h);detailControls.push_back(h);}
  item(L"STATIC",L"Setup never restarts Windows.",0,203);primaryButton=item(L"BUTTON",L"Check this computer",WS_TABSTOP|BS_DEFPUSHBUTTON,100);buttons.push_back(primaryButton);
  fonts();render();RECT desired{0,0,px(760),px(620)};auto adjust=reinterpret_cast<BOOL(WINAPI*)(LPRECT,DWORD,BOOL,DWORD,UINT)>(GetProcAddress(GetModuleHandleW(L"user32.dll"),"AdjustWindowRectExForDpi"));if(adjust)adjust(&desired,WS_OVERLAPPEDWINDOW|WS_VSCROLL,FALSE,0,UINT(dpi));else AdjustWindowRectEx(&desired,WS_OVERLAPPEDWINDOW|WS_VSCROLL,FALSE,0);RECT area{};SystemParametersInfoW(SPI_GETWORKAREA,0,&area,0);SetWindowPos(window,nullptr,0,0,std::min(int(desired.right-desired.left),int(area.right-area.left)),std::min(int(desired.bottom-desired.top),int(area.bottom-area.top)),SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);layout();ShowWindow(window,SW_SHOWNORMAL);
  MSG msg;while(GetMessageW(&msg,nullptr,0,0)>0){if(!IsDialogMessageW(window,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}}CoUninitialize();CloseHandle(singleton);return 0;
 }catch(const std::exception&e){if(checkOnly)return 1;MessageBoxW(nullptr,widen(e.what()).c_str(),L"San Audisco Setup",MB_ICONERROR|MB_OK);return 1;}
}
