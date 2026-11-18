#include "bridge/agent_platform.hpp"
#include <fstream>
#include <stdexcept>
#include <chrono>
#include <thread>
#include <array>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <sodium.h>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <shlobj.h>
#include <sddl.h>
#else
#include <unistd.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <mach-o/dyld.h>
extern char **environ;
#endif
namespace bridge::agent {
namespace {
std::string suffix(){std::array<unsigned char,8>b{};randombytes_buf(b.data(),b.size());char s[17];sodium_bin2hex(s,sizeof s,b.data(),b.size());return s;}
#ifdef _WIN32
std::wstring wide(const std::string&s){if(s.empty())return {};int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),int(s.size()),nullptr,0);if(!n)throw std::runtime_error("Invalid UTF-8");std::wstring r(n,L' ');MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),int(s.size()),r.data(),n);return r;}
std::wstring quote(const std::wstring&s){std::wstring r=L"\"";size_t slash=0;for(auto c:s){if(c==L'\\'){++slash;continue;}if(c==L'\"')r.append(slash*2+1,L'\\');else r.append(slash,L'\\');slash=0;r+=c;}r.append(slash*2,L'\\');return r+L"\"";}
std::wstring command(const std::filesystem::path&e,const std::vector<std::string>&a){auto r=quote(e.wstring());for(auto&s:a)r+=L" "+quote(wide(s));return r;}
void close(HANDLE&h){if(h&&h!=INVALID_HANDLE_VALUE)CloseHandle(h);h=nullptr;}
#else
void close(int&fd){if(fd>=0)::close(fd);fd=-1;}
void nonblock(int fd){fcntl(fd,F_SETFL,fcntl(fd,F_GETFL)|O_NONBLOCK);fcntl(fd,F_SETFD,FD_CLOEXEC);}
#endif
}
std::filesystem::path executable_path(){
#ifdef _WIN32
 std::wstring p(32768,L'\0');auto n=GetModuleFileNameW(nullptr,p.data(),DWORD(p.size()));if(!n||n==p.size())throw std::runtime_error("Executable path unavailable");p.resize(n);return p;
#else
 uint32_t n=0;_NSGetExecutablePath(nullptr,&n);std::string p(n,'\0');if(_NSGetExecutablePath(p.data(),&n))throw std::runtime_error("Executable path unavailable");return std::filesystem::weakly_canonical(p.c_str());
#endif
}
std::filesystem::path default_directory(){
#ifdef _WIN32
 PWSTR p=nullptr;if(FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&p)))throw std::runtime_error("LocalAppData unavailable");std::filesystem::path r(p);CoTaskMemFree(p);return r/L"AudioBridge"/L"room-v2";
#else
 auto h=getenv("HOME");if(!h)throw std::runtime_error("User home unavailable");return std::filesystem::path(h)/"Library/Application Support/AudioBridge/room-v2";
#endif
}
void private_directory(const std::filesystem::path&p){
 if(std::filesystem::is_symlink(p))throw std::runtime_error("Agent directory cannot be a symbolic link");std::filesystem::create_directories(p);
#ifdef _WIN32
 HANDLE token=nullptr;if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token))throw std::runtime_error("Cannot read user identity");DWORD n=0;GetTokenInformation(token,TokenUser,nullptr,0,&n);std::vector<char> data(n);if(!GetTokenInformation(token,TokenUser,data.data(),n,&n)){CloseHandle(token);throw std::runtime_error("Cannot read user identity");}CloseHandle(token);LPWSTR sid=nullptr;if(!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(data.data())->User.Sid,&sid))throw std::runtime_error("Cannot format user identity");std::wstring s=L"D:P(A;OICI;FA;;;"+std::wstring(sid)+L")";LocalFree(sid);PSECURITY_DESCRIPTOR sd=nullptr;if(!ConvertStringSecurityDescriptorToSecurityDescriptorW(s.c_str(),SDDL_REVISION_1,&sd,nullptr))throw std::runtime_error("Cannot protect agent directory");BOOL ok=SetFileSecurityW(p.c_str(),DACL_SECURITY_INFORMATION|PROTECTED_DACL_SECURITY_INFORMATION,sd);LocalFree(sd);if(!ok)throw std::runtime_error("Cannot protect agent directory");
#else
 struct stat st{};if(lstat(p.c_str(),&st)||st.st_uid!=getuid()||!S_ISDIR(st.st_mode)||chmod(p.c_str(),0700))throw std::runtime_error("Agent directory is not privately owned");
#endif
}
void atomic_write(const std::filesystem::path&p,const std::string&s){auto tmp=p;tmp+="."+suffix()+".tmp";
#ifdef _WIN32
 HANDLE f=CreateFileW(tmp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);if(f==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot create state file");DWORD n=0;bool ok=WriteFile(f,s.data(),DWORD(s.size()),&n,nullptr)&&n==s.size()&&FlushFileBuffers(f);CloseHandle(f);if(!ok||!MoveFileExW(tmp.c_str(),p.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){DeleteFileW(tmp.c_str());throw std::runtime_error("Cannot save state file");}
#else
 int f=::open(tmp.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC,0600);if(f<0)throw std::runtime_error("Cannot create state file");size_t at=0;bool ok=true;while(at<s.size()){auto n=write(f,s.data()+at,s.size()-at);if(n<0&&errno==EINTR)continue;if(n<=0){ok=false;break;}at+=size_t(n);}if(fsync(f))ok=false;::close(f);if(!ok||rename(tmp.c_str(),p.c_str())){unlink(tmp.c_str());throw std::runtime_error("Cannot save state file");}
#endif
}
InstanceLock::InstanceLock(const std::filesystem::path&p){
#ifdef _WIN32
 auto file=p/L"agent.lock";HANDLE h=CreateFileW(file.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);if(h==INVALID_HANDLE_VALUE)throw std::runtime_error("AudioBridge agent is already running");handle_=reinterpret_cast<std::intptr_t>(h);
#else
 auto file=p/"agent.lock";int f=::open(file.c_str(),O_RDWR|O_CREAT|O_CLOEXEC|O_NOFOLLOW,0600);if(f<0)throw std::runtime_error("Cannot open agent lock");if(flock(f,LOCK_EX|LOCK_NB)){::close(f);throw std::runtime_error("AudioBridge agent is already running");}handle_=f;
#endif
}
InstanceLock::~InstanceLock(){if(handle_!=-1){
#ifdef _WIN32
 CloseHandle(reinterpret_cast<HANDLE>(handle_));
#else
 ::close(int(handle_));
#endif
}}
void detach(const std::filesystem::path&e,const std::vector<std::string>&args){
#ifdef _WIN32
 auto cmd=command(e,args);STARTUPINFOW si{};si.cb=sizeof si;PROCESS_INFORMATION pi{};if(!CreateProcessW(e.c_str(),cmd.data(),nullptr,nullptr,FALSE,DETACHED_PROCESS|CREATE_NEW_PROCESS_GROUP,nullptr,e.parent_path().c_str(),&si,&pi))throw std::runtime_error("Cannot start background agent");CloseHandle(pi.hThread);CloseHandle(pi.hProcess);
#else
 auto pid=fork();if(pid<0)throw std::runtime_error("Cannot start background agent");if(pid==0){setsid();int fd=::open("/dev/null",O_RDWR);if(fd>=0){dup2(fd,0);dup2(fd,1);dup2(fd,2);if(fd>2)::close(fd);}std::vector<std::string> a{e.string()};a.insert(a.end(),args.begin(),args.end());std::vector<char*> av;for(auto&s:a)av.push_back(s.data());av.push_back(nullptr);execv(e.c_str(),av.data());_exit(127);}
#endif
}
#if defined(_WIN32) || defined(BRIDGE_AGENT_FAKE)
bool capture_authorized(){return true;}
#endif
struct Child::Impl {
#ifdef _WIN32
 HANDLE process=nullptr,input=nullptr,output=nullptr,errors=nullptr;
 std::thread writer;std::mutex mutex;std::condition_variable condition;std::deque<std::string> commands;bool stopping=false,write_failed=false;
#else
 pid_t pid=-1;int input=-1,output=-1,errors=-1;
#endif
};
Child::Child():p_(std::make_unique<Impl>()){}
Child::~Child(){stop();}
void Child::start(const std::filesystem::path&e,const std::vector<std::string>&args){stop();auto&p=*p_;
#ifdef _WIN32
 SECURITY_ATTRIBUTES sa{sizeof sa,nullptr,TRUE};HANDLE inR=nullptr,outW=nullptr,errW=nullptr;
 if(!CreatePipe(&inR,&p.input,&sa,0)||!CreatePipe(&p.output,&outW,&sa,0)||!CreatePipe(&p.errors,&errW,&sa,0)){close(inR);close(outW);close(errW);stop();throw std::runtime_error("Cannot create engine pipes");}
 SetHandleInformation(p.input,HANDLE_FLAG_INHERIT,0);SetHandleInformation(p.output,HANDLE_FLAG_INHERIT,0);SetHandleInformation(p.errors,HANDLE_FLAG_INHERIT,0);
 STARTUPINFOEXW si{};si.StartupInfo.cb=sizeof si;si.StartupInfo.dwFlags=STARTF_USESTDHANDLES;si.StartupInfo.hStdInput=inR;si.StartupInfo.hStdOutput=outW;si.StartupInfo.hStdError=errW;SIZE_T size=0;InitializeProcThreadAttributeList(nullptr,1,0,&size);std::vector<char> buf(size);si.lpAttributeList=reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(buf.data());bool init=InitializeProcThreadAttributeList(si.lpAttributeList,1,0,&size);HANDLE inherit[]{inR,outW,errW};bool attrs=init&&UpdateProcThreadAttribute(si.lpAttributeList,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,inherit,sizeof inherit,nullptr,nullptr);auto cmd=command(e,args);PROCESS_INFORMATION pi{};bool ok=attrs&&CreateProcessW(e.c_str(),cmd.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW|EXTENDED_STARTUPINFO_PRESENT,nullptr,e.parent_path().c_str(),&si.StartupInfo,&pi);if(init)DeleteProcThreadAttributeList(si.lpAttributeList);close(inR);close(outW);close(errW);if(!ok){stop();throw std::runtime_error("Cannot launch audio engine");}p.process=pi.hProcess;CloseHandle(pi.hThread);
 {std::lock_guard guard(p.mutex);p.stopping=false;p.write_failed=false;p.commands.clear();}
 p.writer=std::thread([&p]{for(;;){std::string command;{std::unique_lock lock(p.mutex);p.condition.wait(lock,[&]{return p.stopping||!p.commands.empty();});if(p.stopping)return;command=std::move(p.commands.front());p.commands.pop_front();}DWORD n=0;bool ok=WriteFile(p.input,command.data(),DWORD(command.size()),&n,nullptr)&&n==command.size();sodium_memzero(command.data(),command.size());if(!ok){std::lock_guard guard(p.mutex);p.write_failed=true;return;}}});
#else
 int a[2],b[2],c[2];if(pipe(a))throw std::runtime_error("Cannot create engine pipe");if(pipe(b)){::close(a[0]);::close(a[1]);throw std::runtime_error("Cannot create engine pipe");}if(pipe(c)){for(auto f:{a[0],a[1],b[0],b[1]})::close(f);throw std::runtime_error("Cannot create engine pipe");}
 posix_spawn_file_actions_t actions;posix_spawn_file_actions_init(&actions);posix_spawn_file_actions_adddup2(&actions,a[0],0);posix_spawn_file_actions_adddup2(&actions,b[1],1);posix_spawn_file_actions_adddup2(&actions,c[1],2);for(auto f:{a[0],a[1],b[0],b[1],c[0],c[1]})posix_spawn_file_actions_addclose(&actions,f);
 std::vector<std::string> values{e.string()};values.insert(values.end(),args.begin(),args.end());std::vector<char*> av;for(auto&s:values)av.push_back(s.data());av.push_back(nullptr);int rc=posix_spawn(&p.pid,e.c_str(),&actions,nullptr,av.data(),environ);posix_spawn_file_actions_destroy(&actions);::close(a[0]);::close(b[1]);::close(c[1]);if(rc){::close(a[1]);::close(b[0]);::close(c[0]);p.pid=-1;throw std::runtime_error("Cannot launch audio engine");}p.input=a[1];p.output=b[0];p.errors=c[0];nonblock(p.input);nonblock(p.output);nonblock(p.errors);
#endif
}
bool Child::running(){auto&p=*p_;
#ifdef _WIN32
 return p.process&&WaitForSingleObject(p.process,0)==WAIT_TIMEOUT;
#else
 if(p.pid<=0)return false;int status=0;auto r=waitpid(p.pid,&status,WNOHANG);if(r==0)return true;if(r==p.pid||errno==ECHILD)p.pid=-1;return false;
#endif
}
void Child::send(const std::string&s){if(s.size()>4096||!running())throw std::runtime_error("Audio engine is not ready");auto&p=*p_;
#ifdef _WIN32
 {std::lock_guard guard(p.mutex);if(p.stopping||p.write_failed||p.commands.size()>=16)throw std::runtime_error("Audio engine command queue unavailable");p.commands.push_back(s);}p.condition.notify_one();
#else
 auto n=write(p.input,s.data(),s.size());if(n<0||size_t(n)!=s.size())throw std::runtime_error("Audio engine command failed");
#endif
}
static std::string read_pipe(
#ifdef _WIN32
 HANDLE f
#else
 int f
#endif
){std::string result;char b[4096];for(int i=0;i<16;++i){
#ifdef _WIN32
 DWORD avail=0,n=0;if(!f||!PeekNamedPipe(f,nullptr,0,nullptr,&avail,nullptr)||!avail)break;if(!ReadFile(f,b,std::min<DWORD>(sizeof b,avail),&n,nullptr)||!n)break;
#else
 if(f<0)break;auto n=read(f,b,sizeof b);if(n<=0)break;
#endif
 result.append(b,size_t(n));}return result;}
std::string Child::read_output(){return read_pipe(p_->output);}
std::string Child::read_errors(){return read_pipe(p_->errors);}
void Child::stop(){auto&p=*p_;if(running()){try{send("quit\n");}catch(...){}auto until=std::chrono::steady_clock::now()+std::chrono::milliseconds(750);while(running()&&std::chrono::steady_clock::now()<until){read_output();read_errors();std::this_thread::sleep_for(std::chrono::milliseconds(10));}if(running()){
#ifdef _WIN32
 TerminateProcess(p.process,1);WaitForSingleObject(p.process,1000);
#else
 kill(p.pid,SIGTERM);std::this_thread::sleep_for(std::chrono::milliseconds(50));if(running()){kill(p.pid,SIGKILL);waitpid(p.pid,nullptr,0);}p.pid=-1;
#endif
 }}
#ifdef _WIN32
 {std::lock_guard guard(p.mutex);p.stopping=true;for(auto&command:p.commands)sodium_memzero(command.data(),command.size());p.commands.clear();}p.condition.notify_all();if(p.writer.joinable()){CancelSynchronousIo(p.writer.native_handle());p.writer.join();}
 close(p.process);
#endif
 close(p.input);close(p.output);close(p.errors);
}
}
