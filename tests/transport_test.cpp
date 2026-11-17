#include "bridge/transport.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <stdexcept>
#include <cmath>
using namespace bridge::net;
void require(bool b,const char*m){if(!b)throw std::runtime_error(m);}
int main(){try{
 auto key=random_key();require(key_from_hex(key_to_hex(key))==key,"key roundtrip");
 bool invalid=false;try{key_from_hex("bad");}catch(...){invalid=true;}require(invalid,"invalid key");
 Epoch ea{},eb{},ec{};ea[0]=1;eb[0]=2;ec[0]=3;
 PacketCodec a(key,ea),b(key,eb);a.set_peer_epoch(eb);b.set_peer_epoch(ea);
 std::array<uint8_t,960>pcm{};pcm[0]=42;
 auto first=a.encode(Kind::Audio,Stream::Playback,0,pcm);
 require(first.size()<1200,"MTU");auto decoded=b.decode(first);require(decoded&&decoded->payload[0]==42,"audio roundtrip");require(!b.decode(first),"replay accepted");
 auto damaged=a.encode(Kind::Audio,Stream::Playback,240,pcm);damaged.back()^=1;require(!b.decode(damaged),"tamper accepted");
 PacketCodec wrong(random_key(),eb);wrong.set_peer_epoch(ea);require(!wrong.decode(first),"wrong key accepted");
 PacketCodec new_session(key,ec);new_session.set_peer_epoch(ea);require(!new_session.decode(first),"cross-session replay");
 auto second=a.encode(Kind::Audio,Stream::Microphone,480,pcm);auto third=a.encode(Kind::Audio,Stream::Playback,720,pcm);require(bool(b.decode(third)),"new packet");require(bool(b.decode(second)),"reorder rejected");require(!b.decode(second),"reordered replay");
 auto old=a.encode(Kind::Control,Stream::Playback,0,{});for(int i=0;i<70;++i){auto p=a.encode(Kind::Control,Stream::Playback,0,{});require(bool(b.decode(p)),"control");}require(!b.decode(old),"old replay window");
 JitterBuffer jitter(4,2);auto p=[](uint64_t t){Packet q;q.kind=Kind::Audio;q.sample_time=t;q.payload.resize(960);return q;};require(jitter.push(p(240)),"push");require(jitter.push(p(0)),"reorder push");require(jitter.pop()->sample_time==0,"ordering");require(jitter.pop()->sample_time==240,"ordering 2");require(!jitter.pop(),"missing produces silence");require(!jitter.push(p(240)),"late accepted");require(jitter.push(p(720)),"recovery push");require(jitter.pop()->sample_time==720,"loss recovery");jitter.reset();for(int i=0;i<4;++i)require(jitter.push(p(i*240)),"capacity fill");require(jitter.push(p(960)),"overflow recovery accepts fresh packet");require(jitter.size()==2,"overflow retains only prefill window");require(jitter.pop()->sample_time==720,"overflow drops stale backlog");require(jitter.pop()->sample_time==960,"overflow latest packet retained");
 JitterBuffer late(12,4);for(int i=0;i<4;++i)late.push(p(i*240));for(int i=0;i<44;++i)late.pop();auto before=late.discontinuity_count();for(int i=5;i<12;++i)require(!late.push(p(i*240)),"short late run discarded");require(late.push(p(12*240)),"persistent late stream reanchors");require(late.discontinuity_count()==before+1,"late rebase observable");for(int i=13;i<16;++i)late.push(p(i*240));require(late.pop()->sample_time==12*240,"rebased stream reprimes from fresh timeline");
 JitterBuffer burst(12,4);for(int i=0;i<60;++i)burst.push(p(i*240));require(burst.size()==4&&burst.discontinuity_count()>0,"receiver stall bounds backlog to prefill");require(burst.pop()->sample_time==56*240,"receiver stall keeps newest window");
 Config ca;ca.bind_address="127.0.0.1";ca.bind_port=47891;ca.peer_port=47892;ca.key=key;Config cb=ca;cb.bind_port=47892;cb.peer_port=47891;Transport ta(ca),tb(cb);
 for(int i=0;i<400&&(!ta.ready()||!tb.ready());++i){ta.poll();tb.poll();std::this_thread::sleep_for(std::chrono::milliseconds(2));}require(ta.ready()&&tb.ready(),"loopback handshake");
 std::array<int16_t,480>samples{};samples[0]=-2345;samples[1]=1234;require(ta.send_audio(Stream::Playback,samples,0),"send A");require(tb.send_audio(Stream::Microphone,samples,240),"send B");std::optional<Packet>ra,rb;
 for(int i=0;i<100&&(!ra||!rb);++i){if(!ra)ra=ta.poll();if(!rb)rb=tb.poll();std::this_thread::sleep_for(std::chrono::milliseconds(1));}require(ra&&rb,"duplex receive");AudioDecoder da,db;require(da.decode(*ra).size()==480&&db.decode(*rb).size()==480,"Opus frame decode");require(ra->payload.size()<100&&rb->payload.size()<150,"Opus bitrate bound");require(ra->stream==Stream::Microphone&&rb->stream==Stream::Playback,"stream isolation");
 // Sustained tone survives codec; loss concealment and decoder reset are defined.
 double energy=0;for(int frame=0;frame<20;++frame){for(int j=0;j<240;++j){samples[2*j]=int16_t(10000*std::sin((frame*240+j)*0.06));samples[2*j+1]=samples[2*j];}require(ta.send_audio(Stream::Playback,samples,uint64_t(frame+1)*240),"tone send");std::optional<Packet>pk;for(int i=0;i<100&&!pk;++i){pk=tb.poll();std::this_thread::sleep_for(std::chrono::milliseconds(1));}require(bool(pk),"tone receive");auto result=db.decode(*pk);for(auto v:result)energy+=double(v)*v;}require(energy>1e9,"Opus tone energy");db.conceal(Stream::Playback);db.reset();
 // Both ends idle but polling remain alive through heartbeats.
 auto until=std::chrono::steady_clock::now()+std::chrono::milliseconds(2200);while(std::chrono::steady_clock::now()<until){ta.poll();tb.poll();std::this_thread::sleep_for(std::chrono::milliseconds(5));}require(ta.ready()&&tb.ready(),"heartbeat liveness");
 // One side stops polling; the other expires then both re-establish fresh epochs.
 until=std::chrono::steady_clock::now()+std::chrono::milliseconds(2300);while(std::chrono::steady_clock::now()<until){ta.poll();std::this_thread::sleep_for(std::chrono::milliseconds(5));}require(!ta.ready(),"timeout did not close session");require(ta.session_generation()==1,"session generation");
 for(int i=0;i<1000&&(!ta.ready()||!tb.ready());++i){ta.poll();tb.poll();std::this_thread::sleep_for(std::chrono::milliseconds(5));}require(ta.ready()&&tb.ready(),"fresh reconnect");
 std::cout<<"PASS transport: encryption, tamper, wrong key, replay, session binding, reordering, bounded loss recovery, Opus audio, UDP duplex, heartbeat, timeout and reconnect\n";return 0;
 }catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
