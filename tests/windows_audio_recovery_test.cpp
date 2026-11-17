// Standalone test: includes the backend to inject a stalled WASAPI event wait.
// Do not also link bridge_audio into this executable.
#define NOMINMAX
#include <windows.h>
#include <atomic>
std::atomic<bool> inject_timeout{false};
DWORD recovery_test_wait(DWORD count,const HANDLE* handles,BOOL all,DWORD milliseconds){
    if(inject_timeout.exchange(false))return WAIT_TIMEOUT;
    return ::WaitForMultipleObjects(count,handles,all,milliseconds);
}
#define WaitForMultipleObjects recovery_test_wait
#include "../src/audio_windows.cpp"
#undef WaitForMultipleObjects
#include <iostream>
#include <chrono>

void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
int main(){try{
    auto devices=bridge::enumerate_audio_devices();
    auto output=std::find_if(devices.begin(),devices.end(),[](const auto& d){return d.output_channels>0;});
    if(output==devices.end()){std::cout<<"SKIP: no output device\n";return 77;}
    std::atomic<size_t> frames{0};
    auto silence=[&](float* samples,size_t n){std::fill_n(samples,n*2,0.f);frames+=n;};
    bridge::AudioStream stream;
    stream.open(output->id,false,silence);stream.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    require(stream.running()&&frames>0,"Initial WASAPI render failed");stream.close();
    stream.open(output->id,false,silence);inject_timeout=true;stream.start();
    auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
    while(stream.running()&&std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(10));
    require(!stream.running(),"WASAPI timeout did not stop stream");stream.close();
    frames=0;stream.open(output->id,false,silence);stream.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    require(stream.running()&&frames>0,"WASAPI did not recover after reopen");stream.stop();
    require(!stream.running(),"Stop event did not stop stream");
    std::cout<<"PASS WASAPI silent rendering, injected event timeout, reopen recovery and stop event\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
