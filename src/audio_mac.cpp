#include "bridge/audio.hpp"
#include <AudioUnit/AudioUnit.h>
#include <CoreAudio/CoreAudio.h>
#include <CoreFoundation/CoreFoundation.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <vector>
#include <map>
#include <mutex>
#include <cstdint>
#include <array>
namespace bridge {
namespace {
void check(OSStatus s, const char* context) { if(s) throw std::runtime_error(std::string(context)+" (OSStatus "+std::to_string(s)+")"); }
AudioObjectPropertyAddress prop(AudioObjectPropertySelector s,AudioObjectPropertyScope scope=kAudioObjectPropertyScopeGlobal) { return {s,scope,kAudioObjectPropertyElementMain}; }
std::string string_prop(AudioObjectID d, AudioObjectPropertySelector s) {
    auto p=prop(s); CFStringRef v=nullptr; UInt32 size=sizeof(v);
    if(AudioObjectGetPropertyData(d,&p,0,nullptr,&size,&v)||!v) return {};
    char buffer[4096]{}; CFStringGetCString(v,buffer,sizeof(buffer),kCFStringEncodingUTF8); CFRelease(v); return buffer;
}
unsigned channels(AudioObjectID d, bool input) {
    auto p=prop(kAudioDevicePropertyStreamConfiguration,input?kAudioDevicePropertyScopeInput:kAudioDevicePropertyScopeOutput);
    UInt32 size=0; if(AudioObjectGetPropertyDataSize(d,&p,0,nullptr,&size)||!size) return 0;
    std::vector<unsigned char> data(size); auto* list=reinterpret_cast<AudioBufferList*>(data.data());
    if(AudioObjectGetPropertyData(d,&p,0,nullptr,&size,list)) return 0;
    unsigned n=0; for(UInt32 i=0;i<list->mNumberBuffers;++i)n+=list->mBuffers[i].mNumberChannels; return n;
}
std::vector<AudioDeviceID> ids() {
    auto p=prop(kAudioHardwarePropertyDevices); UInt32 size=0;
    check(AudioObjectGetPropertyDataSize(kAudioObjectSystemObject,&p,0,nullptr,&size),"Enumerating devices");
    std::vector<AudioDeviceID> ds(size/sizeof(AudioDeviceID));
    check(AudioObjectGetPropertyData(kAudioObjectSystemObject,&p,0,nullptr,&size,ds.data()),"Reading devices"); return ds;
}
double rate(AudioObjectID d) { auto p=prop(kAudioDevicePropertyNominalSampleRate); Float64 r=0; UInt32 size=sizeof(r); check(AudioObjectGetPropertyData(d,&p,0,nullptr,&size,&r),"Reading sample rate"); return r; }
}
std::vector<AudioDevice> enumerate_audio_devices() {
    std::vector<AudioDevice> out; for(auto d:ids())out.push_back({string_prop(d,kAudioDevicePropertyDeviceUID),string_prop(d,kAudioObjectPropertyName),channels(d,true),channels(d,false),rate(d)}); return out;
}
struct AudioStream::Impl {
    struct Status {std::atomic<bool> active{false},invalidated{false};};
    // A notification may already be in flight when its listener is removed.
    // Its opaque token never points into Impl. The registry hands it shared
    // ownership of Status, and a late callback simply finds no registered token.
    struct Registry {
        std::mutex mutex;std::map<std::uintptr_t,std::shared_ptr<Status>> entries;
        std::uintptr_t next=1;
    };
    static Registry& registry(){static auto* value=new Registry;return *value;}
    std::shared_ptr<Status> status=std::make_shared<Status>();
    AudioUnit unit{}; bool input{}; AudioCallback callback;
    AudioDeviceID device=kAudioObjectUnknown;std::uintptr_t listener_token=0;
    std::array<AudioObjectPropertyAddress,3> watched{};std::size_t listener_count=0;
    static OSStatus changed(AudioObjectID,UInt32,const AudioObjectPropertyAddress*,void* context) noexcept {
        std::shared_ptr<Status> state;
        {auto& r=registry();std::lock_guard lock(r.mutex);auto i=r.entries.find(reinterpret_cast<std::uintptr_t>(context));if(i!=r.entries.end())state=i->second;}
        if(state){state->invalidated.store(true,std::memory_order_release);state->active.store(false,std::memory_order_release);}return noErr;
    }
    void watch(AudioDeviceID selected){
        device=selected;status=std::make_shared<Status>();
        {auto& r=registry();std::lock_guard lock(r.mutex);listener_token=r.next++;r.entries.emplace(listener_token,status);}
        watched={prop(kAudioDevicePropertyDeviceIsAlive),prop(kAudioDevicePropertyNominalSampleRate),prop(kAudioDevicePropertyStreamConfiguration,input?kAudioDevicePropertyScopeInput:kAudioDevicePropertyScopeOutput)};
        for(const auto& address:watched){check(AudioObjectAddPropertyListener(device,&address,changed,reinterpret_cast<void*>(listener_token)),"Watch audio device changes");++listener_count;}
    }
    void unwatch()noexcept{
        if(listener_token){
            for(std::size_t i=0;i<listener_count;++i)AudioObjectRemovePropertyListener(device,&watched[i],changed,reinterpret_cast<void*>(listener_token));
            auto& r=registry();std::lock_guard lock(r.mutex);r.entries.erase(listener_token);
        }
        listener_token=0;listener_count=0;device=kAudioObjectUnknown;
    }
    void invalidate()noexcept{status->invalidated.store(true,std::memory_order_release);status->active.store(false,std::memory_order_release);}
    static void silence(AudioBufferList* output)noexcept{if(output)for(UInt32 i=0;i<output->mNumberBuffers;++i)if(output->mBuffers[i].mData)std::memset(output->mBuffers[i].mData,0,output->mBuffers[i].mDataByteSize);}

    unsigned channel_count{}; double native_rate{}, phase{}; std::size_t pending{}; bool primed{};
    static constexpr std::size_t capacity=131072;
    std::vector<float> native, stereo, converted; float previous[2]{};
    static OSStatus process(void* ref, AudioUnitRenderActionFlags* flags,const AudioTimeStamp* time,UInt32,UInt32 frames,AudioBufferList* output) noexcept {
        auto& s=*static_cast<Impl*>(ref);
        if(s.status->invalidated.load(std::memory_order_acquire)){silence(output);return kAudioUnitErr_Uninitialized;}
        if(frames>16384){s.invalidate();silence(output);return kAudioUnitErr_TooManyFramesToProcess;}
        if(s.input) {
            AudioBufferList list{1,{{s.channel_count,frames*s.channel_count*static_cast<UInt32>(sizeof(float)),s.native.data()}}};
            OSStatus status=AudioUnitRender(s.unit,flags,time,1,frames,&list); if(status){s.invalidate();return status;}
            std::size_t n=0; const double step=s.native_rate/48000.0;
            for(UInt32 i=0;i<frames;++i) {
                float l=s.native[i*s.channel_count],r=s.native[i*s.channel_count+(s.channel_count>1?1:0)];
                if(!s.primed){s.previous[0]=l;s.previous[1]=r;s.primed=true;s.phase=1;}
                while(s.phase<=1.0 && n<capacity) {
                    s.converted[2*n]=s.previous[0]+static_cast<float>(s.phase)*(l-s.previous[0]);
                    s.converted[2*n+1]=s.previous[1]+static_cast<float>(s.phase)*(r-s.previous[1]); ++n; s.phase+=step;
                }
                s.phase-=1; s.previous[0]=l;s.previous[1]=r;
            }
            if(n)s.callback(s.converted.data(),n);
        } else {
            const double step=48000.0/s.native_rate;
            const std::size_t needed=static_cast<std::size_t>(std::floor(s.phase+frames*step))+2;
            if(needed>=capacity){s.invalidate();silence(output);return kAudioUnitErr_TooManyFramesToProcess;}
            if(needed>s.pending) { s.callback(s.stereo.data()+2*s.pending,needed-s.pending); s.pending=needed; }
            for(UInt32 i=0;i<frames;++i) {
                auto ix=static_cast<std::size_t>(s.phase); const float frac=static_cast<float>(s.phase-ix);
                const float l=s.stereo[2*ix]+frac*(s.stereo[2*ix+2]-s.stereo[2*ix]);
                const float r=s.stereo[2*ix+1]+frac*(s.stereo[2*ix+3]-s.stereo[2*ix+1]);
                if(s.channel_count==1)s.native[i]=(l+r)*0.5f;
                else {s.native[2*i]=l;s.native[2*i+1]=r;}
                s.phase+=step;
            }
            auto consumed=static_cast<std::size_t>(s.phase); s.phase-=consumed;s.pending-=consumed;
            std::memmove(s.stereo.data(),s.stereo.data()+consumed*2,s.pending*2*sizeof(float));
            if(!output||output->mNumberBuffers!=1||!output->mBuffers[0].mData||output->mBuffers[0].mDataByteSize<frames*s.channel_count*sizeof(float)){s.invalidate();silence(output);return kAudioUnitErr_FormatNotSupported;}
            std::memcpy(output->mBuffers[0].mData,s.native.data(),frames*s.channel_count*sizeof(float));
        } return noErr;
    }
};
AudioStream::AudioStream():impl_(std::make_unique<Impl>()){}
AudioStream::~AudioStream(){close();}
void AudioStream::open(const std::string& uid,bool input,AudioCallback callback) {
    close(); auto& s=*impl_; if(!callback)throw std::runtime_error("Missing audio callback");
    AudioDeviceID chosen=kAudioObjectUnknown; for(auto d:ids())if(string_prop(d,kAudioDevicePropertyDeviceUID)==uid){chosen=d;break;}
    if(chosen==kAudioObjectUnknown)throw std::runtime_error("Audio device unavailable: "+uid);
    const unsigned count=channels(chosen,input);if(!count)throw std::runtime_error("Device has no channels in requested direction");
    s.input=input;s.callback=std::move(callback);s.channel_count=std::min(count,2u);s.native_rate=rate(chosen);
    if(s.native_rate<8000||s.native_rate>192000)throw std::runtime_error("Unsupported device sample rate");
    s.native.resize(Impl::capacity*2);s.stereo.resize(Impl::capacity*2);s.converted.resize(Impl::capacity*2);
    AudioComponentDescription desc{kAudioUnitType_Output,kAudioUnitSubType_HALOutput,kAudioUnitManufacturer_Apple,0,0};
    auto component=AudioComponentFindNext(nullptr,&desc);if(!component)throw std::runtime_error("Core Audio HAL component unavailable");
    check(AudioComponentInstanceNew(component,&s.unit),"Creating HAL unit");
    try {
        s.watch(chosen);
        UInt32 on=1,off=0;
        if(input){check(AudioUnitSetProperty(s.unit,kAudioOutputUnitProperty_EnableIO,kAudioUnitScope_Input,1,&on,sizeof(on)),"Enable capture");check(AudioUnitSetProperty(s.unit,kAudioOutputUnitProperty_EnableIO,kAudioUnitScope_Output,0,&off,sizeof(off)),"Disable playback");}
        check(AudioUnitSetProperty(s.unit,kAudioOutputUnitProperty_CurrentDevice,kAudioUnitScope_Global,0,&chosen,sizeof(chosen)),"Select device");
        AudioStreamBasicDescription fmt{};fmt.mSampleRate=s.native_rate;fmt.mFormatID=kAudioFormatLinearPCM;fmt.mFormatFlags=kAudioFormatFlagIsFloat|kAudioFormatFlagIsPacked;fmt.mBytesPerPacket=fmt.mBytesPerFrame=sizeof(float)*s.channel_count;fmt.mFramesPerPacket=1;fmt.mChannelsPerFrame=s.channel_count;fmt.mBitsPerChannel=32;
        check(AudioUnitSetProperty(s.unit,kAudioUnitProperty_StreamFormat,input?kAudioUnitScope_Output:kAudioUnitScope_Input,input?1:0,&fmt,sizeof(fmt)),"Set stream format");
        UInt32 max_frames=16384;check(AudioUnitSetProperty(s.unit,kAudioUnitProperty_MaximumFramesPerSlice,kAudioUnitScope_Global,0,&max_frames,sizeof(max_frames)),"Set maximum slice");
        AURenderCallbackStruct cb{Impl::process,&s};
        check(AudioUnitSetProperty(s.unit,input?static_cast<AudioUnitPropertyID>(kAudioOutputUnitProperty_SetInputCallback):static_cast<AudioUnitPropertyID>(kAudioUnitProperty_SetRenderCallback),input?kAudioUnitScope_Global:kAudioUnitScope_Input,0,&cb,sizeof(cb)),"Set callback");
        check(AudioUnitInitialize(s.unit),"Initialize audio unit");s.phase=0;s.pending=0;s.primed=false;
    }catch(...){close();throw;}
}
void AudioStream::start(){
    auto& s=*impl_;if(!s.unit)throw std::runtime_error("Audio stream not open");
    if(s.status->invalidated.load())throw std::runtime_error("Audio device changed; reopen the stream");
    s.status->active=true;
    OSStatus result=AudioOutputUnitStart(s.unit);
    if(result){s.status->active=false;check(result,"Start audio stream (check microphone permission)");}
    if(s.status->invalidated.load()){AudioOutputUnitStop(s.unit);s.status->active=false;throw std::runtime_error("Audio device changed while starting");}
}
void AudioStream::stop()noexcept{if(impl_->unit)AudioOutputUnitStop(impl_->unit);impl_->status->active=false;}
void AudioStream::close()noexcept{impl_->unwatch();stop();if(impl_->unit){AudioUnitUninitialize(impl_->unit);AudioComponentInstanceDispose(impl_->unit);impl_->unit=nullptr;}impl_->callback={};}
bool AudioStream::running()const noexcept{return impl_->status->active.load()&&!impl_->status->invalidated.load();}
}
