#include "bridge/audio.hpp"
#define NOMINMAX
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>
#include <avrt.h>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <future>
#include <stdexcept>
#include <thread>
#include <vector>
namespace bridge {
namespace {
void check(HRESULT hr,const char* context){if(FAILED(hr))throw std::runtime_error(std::string(context)+" (HRESULT "+std::to_string(static_cast<unsigned long>(hr))+")");}
template<class T>struct Com {T* p{};~Com(){if(p)p->Release();}T** out(){return &p;}T* operator->(){return p;}};
struct Apartment {HRESULT hr=CoInitializeEx(nullptr,COINIT_MULTITHREADED);Apartment(){if(FAILED(hr)&&hr!=RPC_E_CHANGED_MODE)check(hr,"COM initialization");}~Apartment(){if(SUCCEEDED(hr))CoUninitialize();}};
std::string narrow(const wchar_t* s){if(!s)return {};int n=WideCharToMultiByte(CP_UTF8,0,s,-1,nullptr,0,nullptr,nullptr);std::string v(n,'\0');WideCharToMultiByte(CP_UTF8,0,s,-1,v.data(),n,nullptr,nullptr);if(n)v.pop_back();return v;}
std::wstring wide(const std::string& s){int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.c_str(),-1,nullptr,0);if(!n)throw std::runtime_error("Invalid device identifier");std::wstring v(n,L'\0');MultiByteToWideChar(CP_UTF8,0,s.c_str(),-1,v.data(),n);v.pop_back();return v;}
void enumerator(Com<IMMDeviceEnumerator>& e){check(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,__uuidof(IMMDeviceEnumerator),reinterpret_cast<void**>(e.out())),"Create audio enumerator");}
}
std::vector<AudioDevice> enumerate_audio_devices(){
    Apartment apartment;Com<IMMDeviceEnumerator> e;enumerator(e);std::vector<AudioDevice> result;
    for(auto flow:{eRender,eCapture}){
        Com<IMMDeviceCollection> list;check(e->EnumAudioEndpoints(flow,DEVICE_STATE_ACTIVE,list.out()),"Enumerate audio endpoints");UINT count=0;check(list->GetCount(&count),"Count audio endpoints");
        for(UINT i=0;i<count;++i){
            Com<IMMDevice> d;check(list->Item(i,d.out()),"Read endpoint");LPWSTR id=nullptr;check(d->GetId(&id),"Get endpoint ID");AudioDevice info;info.id=narrow(id);CoTaskMemFree(id);
            Com<IPropertyStore> store;check(d->OpenPropertyStore(STGM_READ,store.out()),"Read device properties");PROPVARIANT v;PropVariantInit(&v);
            if(SUCCEEDED(store->GetValue(PKEY_Device_FriendlyName,&v))&&v.vt==VT_LPWSTR)info.name=narrow(v.pwszVal);PropVariantClear(&v);
            Com<IAudioClient> client;if(SUCCEEDED(d->Activate(__uuidof(IAudioClient),CLSCTX_ALL,nullptr,reinterpret_cast<void**>(client.out())))){
                WAVEFORMATEX* fmt=nullptr;if(SUCCEEDED(client->GetMixFormat(&fmt))){info.sample_rate=fmt->nSamplesPerSec;if(flow==eCapture)info.input_channels=fmt->nChannels;else info.output_channels=fmt->nChannels;CoTaskMemFree(fmt);}
            }
            result.push_back(std::move(info));
        }
    }return result;
}
struct AudioStream::Impl {
    std::atomic<std::size_t> callback_hint{0};std::string device;bool input{};AudioCallback callback;std::atomic<bool> active{false};HANDLE stop_event{};std::thread worker;
    void run(std::promise<void> ready)noexcept{
        bool signaled=false;
        try{
            Apartment apartment;Com<IMMDeviceEnumerator> e;enumerator(e);Com<IMMDevice> d;check(e->GetDevice(wide(device).c_str(),d.out()),"Open selected audio endpoint");
            Com<IAudioClient> client;check(d->Activate(__uuidof(IAudioClient),CLSCTX_ALL,nullptr,reinterpret_cast<void**>(client.out())),"Activate audio endpoint");
            WAVEFORMATEX format{};format.wFormatTag=WAVE_FORMAT_IEEE_FLOAT;format.nChannels=2;format.nSamplesPerSec=48000;format.wBitsPerSample=32;format.nBlockAlign=8;format.nAvgBytesPerSec=384000;
            const DWORD flags=AUDCLNT_STREAMFLAGS_EVENTCALLBACK|AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM|AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
            check(client->Initialize(AUDCLNT_SHAREMODE_SHARED,flags,100000,0,&format,nullptr),"Initialize 48k stereo shared audio stream");
            struct Event{HANDLE value=CreateEventW(nullptr,FALSE,FALSE,nullptr);~Event(){if(value)CloseHandle(value);}} audio_event;
            if(!audio_event.value)throw std::runtime_error("Create audio event failed");check(client->SetEventHandle(audio_event.value),"Set audio event");
            UINT32 buffer_frames=0;check(client->GetBufferSize(&buffer_frames),"Get audio buffer size");
            callback_hint=buffer_frames;
            std::vector<float> samples(static_cast<std::size_t>(buffer_frames)*2);
            Com<IAudioCaptureClient> capture;Com<IAudioRenderClient> render;
            if(input)check(client->GetService(__uuidof(IAudioCaptureClient),reinterpret_cast<void**>(capture.out())),"Get capture service");
            else{
                check(client->GetService(__uuidof(IAudioRenderClient),reinterpret_cast<void**>(render.out())),"Get render service");BYTE* data=nullptr;check(render->GetBuffer(buffer_frames,&data),"Prime playback");check(render->ReleaseBuffer(buffer_frames,AUDCLNT_BUFFERFLAGS_SILENT),"Prime playback silence");
            }
            DWORD task_index=0;struct Priority{HANDLE value{};~Priority(){if(value)AvRevertMmThreadCharacteristics(value);}} priority{AvSetMmThreadCharacteristicsW(L"Audio",&task_index)};
            check(client->Start(),"Start audio stream");active=true;ready.set_value();signaled=true;
            HANDLE events[]{stop_event,audio_event.value};
            for(;;){
                DWORD wake=WaitForMultipleObjects(2,events,FALSE,2000);if(wake==WAIT_OBJECT_0)break;
                if(wake==WAIT_TIMEOUT)throw std::runtime_error("Audio device stopped signaling for two seconds; reopen required");if(wake!=WAIT_OBJECT_0+1)throw std::runtime_error("Audio event wait failed");
                if(input){
                    UINT32 packet=0;check(capture->GetNextPacketSize(&packet),"Query capture packet");
                    while(packet){
                        BYTE* data=nullptr;UINT32 frames=0;DWORD status=0;check(capture->GetBuffer(&data,&frames,&status,nullptr,nullptr),"Capture buffer");
                        if(frames>buffer_frames){capture->ReleaseBuffer(frames);throw std::runtime_error("Unexpected oversized capture buffer");}
                        if(status&AUDCLNT_BUFFERFLAGS_SILENT)std::fill_n(samples.data(),frames*2,0.f);else std::memcpy(samples.data(),data,frames*2*sizeof(float));
                        capture->ReleaseBuffer(frames);callback(samples.data(),frames);check(capture->GetNextPacketSize(&packet),"Query capture packet");
                    }
                }else{
                    UINT32 padding=0;check(client->GetCurrentPadding(&padding),"Query playback padding");if(padding>buffer_frames)throw std::runtime_error("Invalid playback padding");UINT32 frames=buffer_frames-padding;
                    if(frames){BYTE* data=nullptr;check(render->GetBuffer(frames,&data),"Get playback buffer");callback(reinterpret_cast<float*>(data),frames);check(render->ReleaseBuffer(frames,0),"Submit playback buffer");}
                }
            }client->Stop();
        }catch(...){if(!signaled)try{ready.set_exception(std::current_exception());}catch(...){} }
        active=false;
    }
};
AudioStream::AudioStream():impl_(std::make_unique<Impl>()){}
AudioStream::~AudioStream(){close();}
void AudioStream::open(const std::string& id,bool input,AudioCallback cb){close();if(id.empty()||!cb)throw std::runtime_error("Explicit device and callback required");impl_->device=id;impl_->input=input;impl_->callback=std::move(cb);}
void AudioStream::start(){auto& s=*impl_;if(!s.callback)throw std::runtime_error("Audio stream not open");if(s.worker.joinable())throw std::runtime_error("Audio stream already started");s.stop_event=CreateEventW(nullptr,TRUE,FALSE,nullptr);if(!s.stop_event)throw std::runtime_error("Create audio stop event failed");std::promise<void> p;auto f=p.get_future();s.worker=std::thread([&s,p=std::move(p)]()mutable{s.run(std::move(p));});try{f.get();}catch(...){stop();throw;}}
void AudioStream::stop()noexcept{auto& s=*impl_;if(s.stop_event)SetEvent(s.stop_event);if(s.worker.joinable())s.worker.join();if(s.stop_event){CloseHandle(s.stop_event);s.stop_event=nullptr;}s.active=false;}
void AudioStream::close()noexcept{stop();impl_->callback={};impl_->device.clear();}
std::size_t AudioStream::callback_frames_hint()const noexcept{return impl_->callback_hint.load();}
bool AudioStream::running()const noexcept{return impl_->active.load();}
}
