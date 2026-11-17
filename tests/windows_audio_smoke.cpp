#include "bridge/audio.hpp"
#include <atomic>
#include <chrono>
#include <thread>
#include <iostream>
#include <algorithm>
int main(){try{auto devices=bridge::enumerate_audio_devices();auto d=std::find_if(devices.begin(),devices.end(),[](auto& x){return x.output_channels>0;});if(d==devices.end()){std::cerr<<"No output device\n";return 77;}std::atomic<size_t> frames{0};bridge::AudioStream stream;stream.open(d->id,false,[&](float*p,size_t n){std::fill_n(p,n*2,0.f);frames+=n;});stream.start();std::this_thread::sleep_for(std::chrono::milliseconds(500));if(!stream.running())throw std::runtime_error("Stream stopped unexpectedly");stream.stop();if(!frames||stream.running())throw std::runtime_error("No frames or stop failed");std::cout<<"WASAPI silent render passed: "<<frames<<" frames at 48k stereo; device="<<d->name<<"\n";return 0;}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
