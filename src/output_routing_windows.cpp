#include "bridge/output_routing.hpp"
#define NOMINMAX
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <propsys.h>
#include <stdexcept>

namespace bridge::routing {
namespace {
// Windows has no documented public setter for default render endpoints.
// IPolicyConfig is isolated here; failure leaves manual selection available.
const CLSID policy_class={0x870af99c,0x171d,0x4f9e,{0xaf,0x0d,0xe6,0x3d,0xf4,0x0c,0x2b,0xc9}};
const IID policy_iid={0xf8679f50,0x850a,0x41cf,{0x9c,0x72,0x43,0x0f,0x29,0x02,0x90,0xc8}};
struct Policy: IUnknown {
 virtual HRESULT STDMETHODCALLTYPE GetMixFormat(PCWSTR,WAVEFORMATEX**)=0;
 virtual HRESULT STDMETHODCALLTYPE GetDeviceFormat(PCWSTR,INT,WAVEFORMATEX**)=0;
 virtual HRESULT STDMETHODCALLTYPE ResetDeviceFormat(PCWSTR)=0;
 virtual HRESULT STDMETHODCALLTYPE SetDeviceFormat(PCWSTR,WAVEFORMATEX*,WAVEFORMATEX*)=0;
 virtual HRESULT STDMETHODCALLTYPE GetProcessingPeriod(PCWSTR,INT,PINT64,PINT64)=0;
 virtual HRESULT STDMETHODCALLTYPE SetProcessingPeriod(PCWSTR,PINT64)=0;
 virtual HRESULT STDMETHODCALLTYPE GetShareMode(PCWSTR,void*)=0;
 virtual HRESULT STDMETHODCALLTYPE SetShareMode(PCWSTR,void*)=0;
 virtual HRESULT STDMETHODCALLTYPE GetPropertyValue(PCWSTR,const PROPERTYKEY&,PROPVARIANT*)=0;
 virtual HRESULT STDMETHODCALLTYPE SetPropertyValue(PCWSTR,const PROPERTYKEY&,PROPVARIANT*)=0;
 virtual HRESULT STDMETHODCALLTYPE SetDefaultEndpoint(PCWSTR,ERole)=0;
 virtual HRESULT STDMETHODCALLTYPE SetEndpointVisibility(PCWSTR,INT)=0;
};
void check(HRESULT h){if(FAILED(h))throw std::runtime_error("Windows output selection failed ("+std::to_string(static_cast<unsigned long>(h))+"). Select CABLE Input in Sound settings.");}
template<class T>struct Com {T* p=nullptr;~Com(){if(p)p->Release();}T** out(){return &p;}T* operator->(){return p;}};
struct Apartment {HRESULT h=CoInitializeEx(nullptr,COINIT_MULTITHREADED);Apartment(){if(h!=RPC_E_CHANGED_MODE)check(h);}~Apartment(){if(SUCCEEDED(h))CoUninitialize();}};
std::string narrow(PCWSTR s){int n=WideCharToMultiByte(CP_UTF8,0,s,-1,nullptr,0,nullptr,nullptr);if(!n)throw std::runtime_error("Invalid output identity");std::string r(n,'\0');WideCharToMultiByte(CP_UTF8,0,s,-1,r.data(),n,nullptr,nullptr);r.pop_back();return r;}
std::wstring wide(const std::string&s){int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.c_str(),-1,nullptr,0);if(!n)throw std::runtime_error("Invalid output identity");std::wstring r(n,L'\0');MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.c_str(),-1,r.data(),n);r.pop_back();return r;}
class WindowsBackend:public Backend {
 bool input_;
public:
 explicit WindowsBackend(bool input):input_(input){}
 std::map<int,std::string> defaults()override{
  Apartment a;Com<IMMDeviceEnumerator> e;check(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,__uuidof(IMMDeviceEnumerator),reinterpret_cast<void**>(e.out())));
  std::map<int,std::string> result;
  for(int role=0;role<3;++role){Com<IMMDevice> d;auto h=e->GetDefaultAudioEndpoint(input_?eCapture:eRender,static_cast<ERole>(role),d.out());if(h==HRESULT_FROM_WIN32(ERROR_NOT_FOUND))continue;check(h);LPWSTR id=nullptr;check(d->GetId(&id));result[role]=narrow(id);CoTaskMemFree(id);}
  return result;
 }
 void set(int role,const std::string& endpoint)override{
  if(role<0||role>2)throw std::runtime_error("Invalid Windows output role");
  Apartment a;Com<Policy> policy;check(CoCreateInstance(policy_class,nullptr,CLSCTX_ALL,policy_iid,reinterpret_cast<void**>(policy.out())));
  check(policy->SetDefaultEndpoint(wide(endpoint).c_str(),static_cast<ERole>(role)));
 }
};
}
std::unique_ptr<Backend> system_backend(bool input){return std::make_unique<WindowsBackend>(input);}
}
