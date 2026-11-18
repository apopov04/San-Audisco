#include "bridge/engine.hpp"
#include "bridge/transport.hpp"
#include <array>
#include <cmath>
#include <iostream>
#include <thread>
#include <stdexcept>
using namespace bridge;
void check(bool b,const char* msg){if(!b)throw std::runtime_error(msg);}
int main(){try{auto key=net::random_key();net::Config a,b;a.bind_address=b.bind_address="127.0.0.1";a.bind_port=46981;a.peer_port=46982;b.bind_port=46982;b.peer_port=46981;a.key=b.key=key;net::Transport win(a),mac(b);for(int i=0;i<400&&(!win.ready()||!mac.ready());i++){win.poll();mac.poll();std::this_thread::sleep_for(std::chrono::milliseconds(5));}check(win.ready()&&mac.ready(),"Handshake failed");
 Engine ew(Device::A),em(Device::B);Settings s;s.primary=Device::A;s.microphoneSource=Device::B;ew.setSettings(s);em.setSettings(s);
 std::array<float,480> game{},discord{},voice{},txplay{},txmic{},receivedPlay{},receivedMic{},listen{},gameMic{},discordMic{};std::array<int16_t,480> pcm{};game.fill(.15f);discord.fill(.25f);voice.fill(.35f);
 em.process({discord.data(),nullptr,voice.data(),nullptr},{nullptr,discordMic.data(),nullptr,txplay.data(),txmic.data()},240);
 net::AudioDecoder decoder;
 for(int frame=0;frame<80;frame++){
  for(size_t i=0;i<240;i++){double t=(frame*240+i)/48000.;for(int c=0;c<2;c++){game[i*2+c]=.15f*std::sin(2*3.141592653589793*500*t);discord[i*2+c]=.25f*std::sin(2*3.141592653589793*1000*t);voice[i*2+c]=.35f*std::sin(2*3.141592653589793*3000*t);}}
  em.process({discord.data(),nullptr,voice.data(),nullptr},{nullptr,discordMic.data(),nullptr,txplay.data(),txmic.data()},240);
  for(size_t i=0;i<480;i++)pcm[i]=int16_t(txplay[i]*32767);check(mac.send_audio(net::Stream::Playback,pcm,frame*240),"Send playback failed");for(size_t i=0;i<480;i++)pcm[i]=int16_t(txmic[i]*32767);check(mac.send_audio(net::Stream::Microphone,pcm,frame*240),"Send mic failed");
  int got=0;for(int i=0;i<100&&got<2;i++){auto p=win.poll();if(p&&p->kind==net::Kind::Audio){auto samples=decoder.decode(*p);auto& out=p->stream==net::Stream::Playback?receivedPlay:receivedMic;for(size_t j=0;j<480;j++)out[j]=samples[j]/32768.f;got++;}std::this_thread::sleep_for(std::chrono::milliseconds(1));}check(got==2,"Receive missing stream");
  ew.process({game.data(),receivedPlay.data(),nullptr,receivedMic.data()},{listen.data(),gameMic.data(),nullptr,nullptr,nullptr},240);
 }
 auto energy=[](const auto& x){double a=0;for(float v:x)a+=v*v;return a/x.size();};
 check(energy(listen)>.02,"Playback mix missing");check(energy(gameMic)>.03,"Remote microphone missing");check(energy(discordMic)>.03,"Local microphone missing");
 // The relayed microphone has 3kHz energy, not the 1kHz Discord playback.
 auto magnitude=[](const auto& x,int hz){double re=0,im=0;for(size_t i=0;i<240;i++){double t=2*3.141592653589793*hz*i/48000.;re+=x[i*2]*std::cos(t);im+=x[i*2]*std::sin(t);}return std::hypot(re,im);};
 check(magnitude(gameMic,3000)>10*magnitude(gameMic,1000),"Playback leaked to microphone");
 s.routes[0].micMuted=true;ew.setSettings(s);ew.process({game.data(),receivedPlay.data(),nullptr,receivedMic.data()},{listen.data(),gameMic.data(),nullptr,nullptr,nullptr},240);for(float v:gameMic)check(v==0,"Muted microphone leaked");
 std::cout<<"PASS actual encrypted UDP -> two routing engines -> isolated playback and microphone\n";return 0;
 }catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
