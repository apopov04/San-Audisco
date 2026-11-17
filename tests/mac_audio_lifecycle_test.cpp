// Standalone target: deliberately includes the backend to inject notifications.
// Compile with -fno-access-control; do not also link bridge_audio.
#include "../src/audio_mac.cpp"
#include <chrono>
#include <iostream>
#include <thread>

void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
int main(){try{
    auto devices=bridge::enumerate_audio_devices();
    auto selected=std::find_if(devices.begin(),devices.end(),[](const auto& d){return d.output_channels>0;});
    if(selected==devices.end()){std::cout<<"SKIP: no native output device available\n";return 77;}
    std::atomic<std::size_t> rendered{0};
    auto silence=[&](float* samples,std::size_t frames){std::fill_n(samples,frames*2,0.f);rendered+=frames;};
    bridge::AudioStream stream;
    stream.open(selected->id,false,silence);stream.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    require(stream.running(),"native output not running");
    require(rendered>0,"native output did not request samples");
    const auto oldToken=stream.impl_->listener_token;
    // Exercise the exact listener callback without changing real hardware state.
    bridge::AudioStream::Impl::changed(0,0,nullptr,reinterpret_cast<void*>(oldToken));
    require(!stream.running(),"device notification did not invalidate stream");
    stream.close();
    bridge::AudioStream::Impl::changed(0,0,nullptr,reinterpret_cast<void*>(oldToken));
    stream.open(selected->id,false,silence);stream.start();
    bridge::AudioStream::Impl::changed(0,0,nullptr,reinterpret_cast<void*>(oldToken));
    require(stream.running(),"stale callback invalidated reopened stream");
    stream.close();require(!stream.running(),"closed stream reports running");
    auto& registry=bridge::AudioStream::Impl::registry();
    {std::lock_guard lock(registry.mutex);require(registry.entries.empty(),"listener state leaked after close");}
    std::cout<<"Native silent playback and listener invalidation/reopen/cleanup passed\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
