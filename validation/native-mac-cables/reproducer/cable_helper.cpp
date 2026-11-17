#include "bridge/audio.hpp"
#include <array>
#include <atomic>
#include <cmath>
#include <csignal>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <thread>
#include <stdexcept>
#include <algorithm>
constexpr double pi=3.14159265358979323846;
constexpr std::array<double,4> hz{410,730,1090,2390};
volatile std::sig_atomic_t stopping=0;
void stop_signal(int){stopping=1;}
struct Measurement { double time; std::array<double,2> rms; std::array<std::array<double,4>,2> tones; };
int main(int argc,char**argv){try{
 if(argc!=3)throw std::runtime_error("Usage: cable_helper LOG.jsonl MAX_SECONDS");
 double seconds=std::stod(argv[2]);if(seconds<1||seconds>180)throw std::runtime_error("Duration outside 1..180 seconds");
 std::ofstream log(argv[1]);if(!log)throw std::runtime_error("Cannot create metrics log");log<<std::setprecision(17);
 std::signal(SIGTERM,stop_signal);std::signal(SIGINT,stop_signal);
 const auto devices=bridge::enumerate_audio_devices();
 for(auto uid:{"BlackHole2ch_UID","BlackHole16ch_UID"})if(std::none_of(devices.begin(),devices.end(),[&](const auto&d){return d.id==uid&&d.input_channels>=2&&d.output_channels>=2;}))throw std::runtime_error("Expected virtual cable unavailable");
 std::array<Measurement,64> queue{};std::atomic<size_t> read{0},write{0};std::atomic<bool> overflow{false};
 std::atomic<uint64_t> captured{0},injected{0};
 uint64_t capture_sample=0,inject_sample=0;size_t measured=0;
 std::array<double,2>squares{};std::array<std::array<double,4>,2>sine{},cosine{};
 bridge::AudioStream capture,inject;
 capture.open("BlackHole16ch_UID",true,[&](float* samples,size_t n){
  for(size_t i=0;i<n;++i){
   for(size_t channel=0;channel<2;++channel){double x=samples[2*i+channel];squares[channel]+=x*x;
    for(size_t k=0;k<4;++k){double angle=2*pi*hz[k]*double(capture_sample)/48000;sine[channel][k]+=x*std::sin(angle);cosine[channel][k]+=x*std::cos(angle);}}
   ++capture_sample;++measured;
   if(measured==4800){
    Measurement m{};m.time=std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
    for(size_t c=0;c<2;++c){m.rms[c]=std::sqrt(squares[c]/measured);for(size_t k=0;k<4;++k)m.tones[c][k]=2*std::hypot(sine[c][k],cosine[c][k])/measured;}
    auto w=write.load(std::memory_order_relaxed);auto next=(w+1)%queue.size();if(next==read.load(std::memory_order_acquire))overflow=true;else{queue[w]=m;write.store(next,std::memory_order_release);}
    measured=0;squares={};sine={};cosine={};
   }
  }captured+=n;
 });
 inject.open("BlackHole2ch_UID",false,[&](float* samples,size_t n){for(size_t i=0;i<n;++i){float value=float(.15*std::sin(2*pi*730*double(inject_sample++)/48000));samples[2*i]=samples[2*i+1]=value;}injected+=n;});
 capture.start();inject.start();std::cout<<"READY virtual-only BlackHole2ch injection 730Hz amplitude0.15; BlackHole16ch stereo capture\n"<<std::flush;
 auto started=std::chrono::steady_clock::now();
 while(!stopping&&std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count()<seconds){
  if(!capture.running()||!inject.running())throw std::runtime_error("Native helper audio stream stopped");
  if(overflow)throw std::runtime_error("Metric queue overflow");
  auto r=read.load(std::memory_order_relaxed);while(r!=write.load(std::memory_order_acquire)){
   const auto&m=queue[r];for(size_t c=0;c<2;++c){log<<"{\"time\":"<<m.time<<",\"sink\":\"mac-mic-"<<(c?"right":"left")<<"\",\"rms\":"<<m.rms[c]<<",\"tones\":[";for(size_t k=0;k<4;++k){if(k)log<<',';log<<m.tones[c][k];}log<<"]}\n";}
   r=(r+1)%queue.size();read.store(r,std::memory_order_release);
  }log.flush();std::this_thread::sleep_for(std::chrono::milliseconds(10));
 }
 inject.close();capture.close();if(!captured||!injected)throw std::runtime_error("No native audio frames");
 std::cout<<"STOP captured_stereo_frames="<<captured<<" injected_stereo_frames="<<injected<<"\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
