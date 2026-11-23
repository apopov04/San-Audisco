#include "bridge/transport.hpp"
#include <sodium.h>
#include <opus/opus.h>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <stdexcept>
#include <limits>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
using Socket=SOCKET;
static constexpr Socket bad_socket=INVALID_SOCKET;
#else
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
using Socket=int;
static constexpr Socket bad_socket=-1;
#endif
namespace bridge::net {
namespace {
constexpr size_t header=56, tag=16, max_payload=1000;
void init() { static const int status=sodium_init(); if(status<0) throw std::runtime_error("libsodium initialization failed"); }
void put64(uint8_t* p,uint64_t x) { for(int i=0;i<8;++i)p[i]=uint8_t(x>>(8*i)); }
uint64_t get64(const uint8_t* p) { uint64_t x=0;for(int i=0;i<8;++i)x|=uint64_t(p[i])<<(8*i);return x; }
Epoch random_epoch(){Epoch e;randombytes_buf(e.data(),e.size());return e;}
void close_socket(Socket s){if(s==bad_socket)return;
#ifdef _WIN32
closesocket(s);
#else
close(s);
#endif
}
sockaddr_in address(const std::string& ip,uint16_t port){sockaddr_in a{};a.sin_family=AF_INET;a.sin_port=htons(port);if(inet_pton(AF_INET,ip.c_str(),&a.sin_addr)!=1)throw std::runtime_error("Expected numeric IPv4 address: "+ip);return a;}
}
Key random_key(){init();Key k;randombytes_buf(k.data(),k.size());return k;}
std::string key_to_hex(const Key& k){init();std::array<char,65>s{};sodium_bin2hex(s.data(),s.size(),k.data(),k.size());return s.data();}
Key key_from_hex(const std::string&s){init();Key k{};size_t n=0;if(s.size()!=64||sodium_hex2bin(k.data(),k.size(),s.data(),s.size(),nullptr,&n,nullptr)!=0||n!=32)throw std::runtime_error("Pairing key must be 64 hexadecimal characters");return k;}
PacketCodec::PacketCodec(const Key& k,const Epoch& local):key_(k),local_(local){init();}
PacketCodec::~PacketCodec(){sodium_memzero(key_.data(),key_.size());}
void PacketCodec::set_peer_epoch(const Epoch&e){if(peer_set_&&peer_!=e)throw std::runtime_error("Peer epoch changed; explicitly restart session");peer_=e;peer_set_=true;}
std::vector<uint8_t> PacketCodec::encode(Kind kind,Stream stream,uint64_t time,std::span<const uint8_t> payload){
 if(payload.size()>max_payload)throw std::runtime_error("Packet payload exceeds limit");
 if(tx_==std::numeric_limits<uint64_t>::max())throw std::runtime_error("Session sequence exhausted");
 if(kind!=Kind::Hello&&!peer_set_)throw std::runtime_error("No peer epoch");
 std::vector<uint8_t> out(header+payload.size()+tag);auto*p=out.data();p[0]='A';p[1]='B';p[2]=2;p[3]=uint8_t(kind);p[4]=uint8_t(stream);p[5]=0;p[6]=uint8_t(payload.size());p[7]=uint8_t(payload.size()>>8);
 put64(p+8,tx_++);put64(p+16,time);std::copy(local_.begin(),local_.end(),p+24);
 if(kind!=Kind::Hello)std::copy(peer_.begin(),peer_.end(),p+40);
 std::array<uint8_t,24>nonce{};std::copy(local_.begin(),local_.end(),nonce.begin());std::copy(p+8,p+16,nonce.begin()+16);
 unsigned long long count=0;
 crypto_aead_xchacha20poly1305_ietf_encrypt(p+header,&count,payload.data(),payload.size(),p,header,nullptr,nonce.data(),key_.data());return out;
}
std::optional<Packet> PacketCodec::decode(std::span<const uint8_t> wire){
 if(wire.size()<header+tag||wire.size()>header+tag+max_payload)return{};const auto*p=wire.data();
 if(p[0]!='A'||p[1]!='B'||p[2]!=2||p[5]!=0||p[3]<1||p[3]>5)return{};
 size_t n=p[6]|(size_t(p[7])<<8);if(wire.size()!=header+tag+n)return{};
 Kind kind=Kind(p[3]);Epoch sender;std::copy(p+24,p+40,sender.begin());
 if(sender==local_)return{};
 if(kind!=Kind::Hello&&!std::equal(local_.begin(),local_.end(),p+40))return{};
 if(peer_set_&&sender!=peer_)return{};
 if((kind==Kind::Audio||kind==Kind::Control||kind==Kind::Heartbeat)&&!peer_set_)return{};
 std::array<uint8_t,24>nonce{};std::copy(sender.begin(),sender.end(),nonce.begin());std::copy(p+8,p+16,nonce.begin()+16);
 Packet result;result.kind=kind;result.stream=Stream(p[4]);result.sequence=get64(p+8);result.sample_time=get64(p+16);result.payload.resize(n);unsigned long long got=0;
 if(crypto_aead_xchacha20poly1305_ietf_decrypt(result.payload.data(),&got,nullptr,p+header,n+tag,p,header,nonce.data(),key_.data())!=0)return{};
 // Only post-handshake messages enter replay window; Hello can be repeated to recover lost ACK.
 if(kind!=Kind::Hello){auto seq=result.sequence;if(received_&&seq<=highest_){auto delta=highest_-seq;if(delta>=64||(replay_bits_&(uint64_t(1)<<delta)))return{};replay_bits_|=uint64_t(1)<<delta;}else{auto delta=seq-highest_;replay_bits_=received_&&delta<64?(replay_bits_<<delta)|1:1;highest_=seq;received_=true;}}
 if(kind==Kind::Audio&&(p[4]<1||p[4]>2||n==0))return{};
 decoded_sender_=sender;return result;
}
struct Transport::Impl {
 Socket fd=bad_socket;sockaddr_in peer{};PacketCodec codec;bool connected=false;uint16_t port=0; Key key; OpusEncoder* encoders[2]{};uint64_t rejected=0, generation=0;
 std::chrono::steady_clock::time_point last_hello{}, last_received{}, last_heartbeat{};
 Impl(const Config& c):codec(c.key,(init(),random_epoch())),key(c.key){
#ifdef _WIN32
 WSADATA data;if(WSAStartup(MAKEWORD(2,2),&data))throw std::runtime_error("WSAStartup failed");
#endif
 fd=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);if(fd==bad_socket)throw std::runtime_error("UDP socket failed");
 try{auto local=address(c.bind_address,c.bind_port);peer=address(c.peer_address,c.peer_port);if(bind(fd,reinterpret_cast<sockaddr*>(&local),sizeof local)!=0)throw std::runtime_error("UDP bind failed");
#ifdef _WIN32
 u_long nonblock=1;if(ioctlsocket(fd,FIONBIO,&nonblock))throw std::runtime_error("Nonblocking socket failed");int len=sizeof local;
#else
 if(fcntl(fd,F_SETFL,O_NONBLOCK)<0)throw std::runtime_error("Nonblocking socket failed");socklen_t len=sizeof local;
#endif
 if(getsockname(fd,reinterpret_cast<sockaddr*>(&local),&len)!=0)throw std::runtime_error("getsockname failed");port=ntohs(local.sin_port);
 for(int i=0;i<2;++i){int err;encoders[i]=opus_encoder_create(48000,2,OPUS_APPLICATION_RESTRICTED_LOWDELAY,&err);if(err!=OPUS_OK)throw std::runtime_error("Opus encoder initialization failed");opus_encoder_ctl(encoders[i],OPUS_SET_BITRATE(i==0?128000:48000));opus_encoder_ctl(encoders[i],OPUS_SET_COMPLEXITY(3));opus_encoder_ctl(encoders[i],OPUS_SET_VBR(0));}
 }catch(...){for(auto*e:encoders)if(e)opus_encoder_destroy(e);close_socket(fd);fd=bad_socket;throw;}}
 ~Impl(){for(auto*e:encoders)if(e)opus_encoder_destroy(e);sodium_memzero(key.data(),key.size());close_socket(fd);
#ifdef _WIN32
 WSACleanup();
#endif
 }
 bool send(Kind k,Stream s,uint64_t t,std::span<const uint8_t> payload){auto bytes=codec.encode(k,s,t,payload);auto n=sendto(fd,reinterpret_cast<const char*>(bytes.data()),int(bytes.size()),0,reinterpret_cast<sockaddr*>(&peer),sizeof peer);return n==int(bytes.size());}
};
Transport::Transport(const Config&c):impl_(std::make_unique<Impl>(c)){}
Transport::~Transport()=default;
bool Transport::ready()const{return impl_->connected;}
uint16_t Transport::local_port()const{return impl_->port;}
uint64_t Transport::rejected_packets()const{return impl_->rejected;}
uint64_t Transport::session_generation()const{return impl_->generation;}
bool Transport::send_audio(Stream stream,std::span<const int16_t> samples,uint64_t time){if(samples.size()!=480)throw std::runtime_error("Audio packet requires 240 stereo frames");if(stream!=Stream::Playback&&stream!=Stream::Microphone)throw std::runtime_error("Invalid audio stream");if(!ready())return false;std::array<uint8_t,1000> bytes;auto n=opus_encode(impl_->encoders[int(stream)-1],samples.data(),240,bytes.data(),int(bytes.size()));if(n<0)throw std::runtime_error("Opus encode failed");return impl_->send(Kind::Audio,stream,time,std::span(bytes.data(),size_t(n)));}
bool Transport::send_control(std::span<const uint8_t> bytes){return ready()&&impl_->send(Kind::Control,Stream::Playback,0,bytes);}
std::optional<Packet> Transport::poll(){auto&v=*impl_;auto now=std::chrono::steady_clock::now();
 if(v.connected&&now-v.last_received>std::chrono::seconds(2)){v.connected=false;++v.generation;v.codec=PacketCodec(v.key,random_epoch());v.last_hello={};for(auto*e:v.encoders)opus_encoder_ctl(e,OPUS_RESET_STATE);}
 if(v.connected&&now-v.last_heartbeat>std::chrono::milliseconds(250)){v.send(Kind::Heartbeat,Stream::Playback,0,{});v.last_heartbeat=now;}
 if(!v.connected&&now-v.last_hello>std::chrono::milliseconds(200)){v.send(Kind::Hello,Stream::Playback,0,{});v.last_hello=now;}
 for(int tries=0;tries<32;++tries){std::array<uint8_t,1201>bytes;sockaddr_in sender{};
#ifdef _WIN32
 int len=sizeof sender;
#else
 socklen_t len=sizeof sender;
#endif
 auto n=recvfrom(v.fd,reinterpret_cast<char*>(bytes.data()),int(bytes.size()),0,reinterpret_cast<sockaddr*>(&sender),&len);if(n<0)return{};
 if(sender.sin_addr.s_addr!=v.peer.sin_addr.s_addr||sender.sin_port!=v.peer.sin_port){++v.rejected;continue;}
 auto packet=v.codec.decode(std::span(bytes.data(),size_t(n)));if(!packet){++v.rejected;continue;}
 if(packet->kind==Kind::Hello){v.codec.set_peer_epoch(v.codec.decoded_sender());v.send(Kind::Ack,Stream::Playback,0,{});continue;}
 if(packet->kind==Kind::Ack){v.codec.set_peer_epoch(v.codec.decoded_sender());v.connected=true;v.last_received=now;continue;}
 if(!v.connected){++v.rejected;continue;}v.last_received=now;if(packet->kind==Kind::Heartbeat)continue;return packet;
 }return{};}
JitterBuffer::JitterBuffer(size_t capacity,size_t prefill):capacity_(capacity),prefill_(prefill){if(!prefill||prefill>capacity)throw std::runtime_error("Invalid jitter buffer dimensions");}
bool JitterBuffer::push(Packet p){
 if(p.kind!=Kind::Audio||p.sample_time%240||packets_.count(p.sample_time))return false;
 if(started_&&p.sample_time<next_time_){
  // One reordered late packet is normal. A run of advancing authenticated
  // packets behind the PLC timeline means clocks/timelines discontinuously
  // diverged; reanchor instead of rejecting every packet forever.
  late_run_=(late_run_&&p.sample_time>last_late_time_)?late_run_+1:1;
  last_late_time_=p.sample_time;
  if(late_run_<8)return false;
  reset();recovering_=true;
 }else late_run_=0;
 if(packets_.size()>=capacity_){
  started_=false;recovering_=true;late_run_=0;++discontinuities_;
 }
 // During a receive burst after a stall keep the latest prefill window; old
 // audio must not add seconds of drift-controller drain time to latency.
 if(recovering_){
  packets_.emplace(p.sample_time,std::move(p));
  while(packets_.size()>prefill_)packets_.erase(packets_.begin());
 }else packets_.emplace(p.sample_time,std::move(p));
 return true;
}
std::optional<Packet> JitterBuffer::pop(){if(!started_){if(packets_.size()<prefill_)return{};started_=true;recovering_=false;next_time_=packets_.begin()->first;}auto it=packets_.find(next_time_);next_time_+=240;if(it==packets_.end())return{};auto p=std::move(it->second);packets_.erase(it);return p;}
void JitterBuffer::reset(){packets_.clear();started_=false;next_time_=0;late_run_=0;recovering_=false;++discontinuities_;}
struct AudioDecoder::Impl {
 OpusDecoder* decoders[2]{};
 Impl(){for(int i=0;i<2;++i){int err;decoders[i]=opus_decoder_create(48000,2,&err);if(err!=OPUS_OK){for(auto*d:decoders)if(d)opus_decoder_destroy(d);throw std::runtime_error("Opus decoder initialization failed");}}}
 ~Impl(){for(auto*d:decoders)opus_decoder_destroy(d);}
};
AudioDecoder::AudioDecoder():impl_(std::make_unique<Impl>()){}
AudioDecoder::~AudioDecoder()=default;
std::array<int16_t,480> AudioDecoder::decode(const Packet&p){if(p.kind!=Kind::Audio||(p.stream!=Stream::Playback&&p.stream!=Stream::Microphone)||p.payload.empty())throw std::runtime_error("Invalid Opus packet");std::array<int16_t,480> out{};int n=opus_decode(impl_->decoders[int(p.stream)-1],p.payload.data(),int(p.payload.size()),out.data(),240,0);if(n!=240)throw std::runtime_error("Invalid Opus frame length");return out;}
std::array<int16_t,480> AudioDecoder::conceal(Stream s){if(s!=Stream::Playback&&s!=Stream::Microphone)throw std::runtime_error("Invalid stream");std::array<int16_t,480>out{};if(opus_decode(impl_->decoders[int(s)-1],nullptr,0,out.data(),240,0)!=240)out.fill(0);return out;}
void AudioDecoder::reset(){for(auto*d:impl_->decoders)opus_decoder_ctl(d,OPUS_RESET_STATE);}
void AudioDecoder::reset(Stream s){if(s!=Stream::Playback&&s!=Stream::Microphone)throw std::runtime_error("Invalid stream");opus_decoder_ctl(impl_->decoders[int(s)-1],OPUS_RESET_STATE);}
}
