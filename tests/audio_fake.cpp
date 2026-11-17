// Test-only deterministic AudioStream implementation. Never link into production.
#include "bridge/deadline_wait.hpp"
#include "bridge/audio.hpp"
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace bridge {
namespace {
constexpr double pi=3.14159265358979323846;
// Calibrated against Opus/clock-drift sidebands; indices remain win/mac playback, win/mac mic.
constexpr std::array<double,4> frequencies{410,730,1090,2390};
std::mutex log_mutex;
void log_metrics(const std::string& id,double squares,const std::array<double,4>& sine,const std::array<double,4>& cosine,size_t frames){
 const char* path=std::getenv("BRIDGE_FAKE_LOG");if(!path)return;
 std::lock_guard lock(log_mutex);std::ofstream f(path,std::ios::app);
 auto ms=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
 f<<std::setprecision(17)<<"{\"time\":"<<double(ms)/1000.0<<",\"sink\":\""<<id<<"\",\"rms\":"<<std::sqrt(squares/frames)<<",\"tones\":[";
 for(size_t i=0;i<4;++i){if(i)f<<',';f<<2*std::hypot(sine[i],cosine[i])/frames;}f<<"]}\n";
}
bool valid(const std::string&id){for(auto prefix:{"win-","mac-"})for(auto suffix:{"playback","mic","headphones","mic-sink"})if(id==std::string(prefix)+suffix)return true;return false;}
}
std::vector<AudioDevice> enumerate_audio_devices(){std::vector<AudioDevice> result;for(auto prefix:{"win-","mac-"})for(auto suffix:{"playback","mic","headphones","mic-sink"}){std::string id=std::string(prefix)+suffix;bool input=std::string(suffix)=="playback"||std::string(suffix)=="mic";result.push_back({id,id,input?2u:0u,input?0u:2u,48000});}return result;}
struct AudioStream::Impl {std::string id;bool input=false;AudioCallback callback;std::atomic<bool> active{false};std::thread worker;};
AudioStream::AudioStream():impl_(std::make_unique<Impl>()){}
AudioStream::~AudioStream(){close();}
void AudioStream::open(const std::string&id,bool input,AudioCallback callback){close();if(!valid(id))throw std::runtime_error("Unknown fake audio device: "+id);bool is_input=id.ends_with("playback")||id.ends_with("-mic");if(input!=is_input)throw std::runtime_error("Wrong fake audio direction");impl_->id=id;impl_->input=input;impl_->callback=std::move(callback);}
void AudioStream::start(){auto&v=*impl_;if(v.active)return;if(!v.callback)throw std::runtime_error("Fake stream not open");auto pacing=std::make_shared<DeadlineWait>();v.active=true;v.worker=std::thread([&v,pacing]{std::array<float,480> buffer{};uint64_t sample=0;size_t measured=0;double squares=0;std::array<double,4>sine{},cosine{};bool mac=v.id.starts_with("mac-");double hz=v.id.ends_with("playback")?(mac?730:410):(mac?2390:1090);const char* metricsMode=std::getenv("BRIDGE_FAKE_METRICS");const bool metrics=!metricsMode||std::string(metricsMode)!="0";auto next=std::chrono::steady_clock::now();while(v.active){
 if(v.input){for(size_t n=0;n<240;++n){float x=float(.15*std::sin(2*pi*hz*double(sample+n)/48000));buffer[2*n]=buffer[2*n+1]=x;}}
 else buffer.fill(0);
 v.callback(buffer.data(),240);
 if(!v.input&&metrics){for(size_t n=0;n<240;++n){double x=buffer[2*n];if(!std::isfinite(x))throw std::runtime_error("Non-finite audio output");squares+=x*x;for(size_t i=0;i<4;++i){double a=2*pi*frequencies[i]*double(sample+n)/48000;sine[i]+=x*std::sin(a);cosine[i]+=x*std::cos(a);}}measured+=240;if(measured>=4800){log_metrics(v.id,squares,sine,cosine,measured);measured=0;squares=0;sine.fill(0);cosine.fill(0);}}
 sample+=240;next+=std::chrono::milliseconds(5);if(!pacing->until(next)){std::fprintf(stderr,"PACER_ERROR owner=%s error=%lu\n",v.id.c_str(),static_cast<unsigned long>(pacing->last_error()));v.active=false;break;}
 }});}
void AudioStream::stop()noexcept{impl_->active=false;if(impl_->worker.joinable())impl_->worker.join();}
void AudioStream::close()noexcept{stop();impl_->callback={};impl_->id.clear();}
std::size_t AudioStream::callback_frames_hint()const noexcept{return 240;}
bool AudioStream::running()const noexcept{return impl_->active;}
}
