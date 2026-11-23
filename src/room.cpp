#include "bridge/room.hpp"
#include <nlohmann/json.hpp>
#include <openssl/curve25519.h>
#include <sodium.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <deque>
#include <limits>
#include <map>
#include <stdexcept>
#include <utility>
#ifdef _WIN32
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
using RoomSocket=SOCKET;
using RoomSocklen=int;
static constexpr RoomSocket invalid_socket=INVALID_SOCKET;
#else
#include <sys/socket.h>
#include <sys/select.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
using RoomSocket=int;
using RoomSocklen=socklen_t;
static constexpr RoomSocket invalid_socket=-1;
#endif

namespace bridge::room {
namespace {
using Json=nlohmann::json;
using Bytes=std::vector<unsigned char>;
using Clock=std::chrono::steady_clock;
constexpr std::size_t max_message=65536, max_frame=max_message+64, max_queued=512*1024;
constexpr const char* alphabet="23456789ABCDEFGHJKLMNPQRSTUVWXYZ";
// Stable wire/KDF identity: changing the former product name here would break
// discovery, saved pairing authentication, and derived media keys.
constexpr const char* protocol="AudioBridge-room-v2-spake2-boringssl";
void init(){static int result=sodium_init();if(result<0)throw std::runtime_error("Cryptographic initialization failed");}
void close_socket(RoomSocket s){if(s==invalid_socket)return;
#ifdef _WIN32
closesocket(s);
#else
close(s);
#endif
}
bool would_block(){
#ifdef _WIN32
return WSAGetLastError()==WSAEWOULDBLOCK||WSAGetLastError()==WSAEINPROGRESS;
#else
return errno==EAGAIN||errno==EWOULDBLOCK||errno==EINPROGRESS;
#endif
}
void nonblocking(RoomSocket s){
#ifdef _WIN32
u_long enabled=1;if(ioctlsocket(s,FIONBIO,&enabled))throw std::runtime_error("Cannot configure room socket");
#else
if(fcntl(s,F_SETFL,O_NONBLOCK)<0||fcntl(s,F_SETFD,FD_CLOEXEC)<0)throw std::runtime_error("Cannot configure room socket");
#ifdef SO_NOSIGPIPE
int enabled=1;setsockopt(s,SOL_SOCKET,SO_NOSIGPIPE,&enabled,sizeof(enabled));
#endif
#endif
}
sockaddr_in endpoint(const std::string& ip,std::uint16_t port){sockaddr_in a{};a.sin_family=AF_INET;a.sin_port=htons(port);if(inet_pton(AF_INET,ip.c_str(),&a.sin_addr)!=1)throw std::runtime_error("Expected numeric IPv4 room address");return a;}
std::string ip_string(const sockaddr_in& a){std::array<char,INET_ADDRSTRLEN> b{};if(!inet_ntop(AF_INET,&a.sin_addr,b.data(),b.size()))throw std::runtime_error("Cannot read peer address");return b.data();}
std::string hex(const unsigned char* p,std::size_t n){std::string out(n*2+1,'\0');sodium_bin2hex(out.data(),out.size(),p,n);out.resize(n*2);return out;}
Bytes unhex(const std::string& s,std::size_t n){Bytes b(n);std::size_t got=0;if(s.size()!=n*2||sodium_hex2bin(b.data(),n,s.data(),s.size(),nullptr,&got,nullptr)||got!=n)throw std::runtime_error("Invalid room protocol encoding");return b;}
bool valid_id(const std::string& s){return s.size()==32&&std::all_of(s.begin(),s.end(),[](unsigned char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');});}
void validate_name(const std::string& name){if(name.empty()||name.size()>80||std::any_of(name.begin(),name.end(),[](unsigned char c){return c<32||c==127;}))throw std::runtime_error("Device name must contain 1–80 printable UTF-8 bytes");Json(name).dump();}
void validate_device(const Device& d){if(!valid_id(d.id)||(d.platform!="windows"&&d.platform!="mac"))throw std::runtime_error("Invalid room device identity");validate_name(d.name);}
Json device_json(const Device& d){return Json{{"id",d.id},{"name",d.name},{"platform",d.platform}};}
Device parse_device(const Json& j){Device d{j.at("id").get<std::string>(),j.at("name").get<std::string>(),j.at("platform").get<std::string>()};validate_device(d);return d;}
std::uint16_t parse_port(const Json& j){auto v=j.get<unsigned>();if(v==0||v>65535)throw std::runtime_error("Invalid room port");return std::uint16_t(v);}
net::Key derive(const net::Key& key,const std::string& label){net::Key out{};crypto_generichash(out.data(),out.size(),reinterpret_cast<const unsigned char*>(label.data()),label.size(),key.data(),key.size());return out;}
void put64(unsigned char* p,std::uint64_t n){for(int i=0;i<8;++i)p[i]=static_cast<unsigned char>(n>>(i*8));}
std::uint64_t get64(const unsigned char* p){std::uint64_t n=0;for(int i=0;i<8;++i)n|=std::uint64_t(p[i])<<(i*8);return n;}
void wipe(std::string& s){if(!s.empty())sodium_memzero(s.data(),s.size());s.clear();}
void wipe(net::Key& k){sodium_memzero(k.data(),k.size());}
}
std::string new_id(){init();std::array<unsigned char,16> b{};randombytes_buf(b.data(),b.size());return hex(b.data(),b.size());}
std::string normalize_code(const std::string& input){if(input.size()>32)throw std::runtime_error("Room code must contain eight characters");std::string out;for(unsigned char c:input){if(c==' '||c=='-')continue;if(c>='a'&&c<='z')c-=32;if(!std::strchr(alphabet,c)||c==0)throw std::runtime_error("Invalid room code character");out+=char(c);}if(out.size()!=8)throw std::runtime_error("Room code must contain eight characters");return out;}
net::Key audio_key(const Credential& c){init();if(!valid_id(c.room_id))throw std::runtime_error("Invalid saved room");return derive(c.secret,std::string(protocol)+"|audio|"+c.room_id);}

struct Service::Impl {
 struct Connection {
  RoomSocket fd=invalid_socket;
  bool outgoing=false,connecting=false,dead=false,hello_received=false,derived=false,confirmed=false,proof_sent=false,pairing=false;
  std::string address,room_id,code,local_nonce,remote_nonce,transcript,expected_peer;
  std::uint16_t peer_port=0;
  Device peer;
  SPAKE2_CTX* spake=nullptr;
  net::Key master{},tx_key{},rx_key{};
  std::uint64_t tx_sequence=0,rx_sequence=0;
  Bytes incoming;
  std::deque<Bytes> outgoing_frames;
  std::size_t write_offset=0,queued=0;
  Clock::time_point created=Clock::now(),received=created,sent=created;
  ~Connection(){close_socket(fd);SPAKE2_CTX_free(spake);wipe(code);wipe(master);wipe(tx_key);wipe(rx_key);}
 };
 Config config;
 RoomSocket listener=invalid_socket,udp=invalid_socket;
 bool winsock=false;
 std::uint16_t bound_port=0;
 Status state;
 std::string invitation;
 Clock::time_point invitation_end{},last_discover{},next_connect{},rate_window{};
 unsigned attempts=0,invite_attempts=0;
 std::optional<Credential> credential,pending_credential;
 std::string join_code;
 std::vector<std::unique_ptr<Connection>> connections;
 Connection* active=nullptr;
 struct Seen{Nearby item;Clock::time_point time;};
 std::map<std::string,Seen> seen;
 std::deque<std::string> messages;
 std::size_t message_bytes=0;
 explicit Impl(Config c):config(std::move(c)){
  init();validate_device(config.local);
  if(config.invitation_lifetime_seconds<1||config.invitation_lifetime_seconds>300)throw std::runtime_error("Invitation lifetime must be between 1 and 300 seconds");
#ifdef _WIN32
  WSADATA data;if(WSAStartup(MAKEWORD(2,2),&data))throw std::runtime_error("Windows socket initialization failed");winsock=true;
#endif
  try{
   listener=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);if(listener==invalid_socket)throw std::runtime_error("Cannot create room listener");nonblocking(listener);
   int one=1;
#ifdef _WIN32
   setsockopt(listener,SOL_SOCKET,SO_EXCLUSIVEADDRUSE,reinterpret_cast<const char*>(&one),sizeof(one));
#else
   setsockopt(listener,SOL_SOCKET,SO_REUSEADDR,&one,sizeof(one));
#endif
   auto address=endpoint(config.bind_address,config.port);
   if(bind(listener,reinterpret_cast<const sockaddr*>(&address),sizeof(address))||listen(listener,8))throw std::runtime_error("Room port is already in use or unavailable");
   RoomSocklen len=sizeof(address);if(getsockname(listener,reinterpret_cast<sockaddr*>(&address),&len))throw std::runtime_error("Cannot read room port");bound_port=ntohs(address.sin_port);
   udp=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);if(udp==invalid_socket)throw std::runtime_error("Cannot create discovery socket");nonblocking(udp);
   setsockopt(udp,SOL_SOCKET,SO_BROADCAST,reinterpret_cast<const char*>(&one),sizeof(one));
   if(bind(udp,reinterpret_cast<const sockaddr*>(&address),sizeof(address)))throw std::runtime_error("Discovery port is already in use or unavailable");
  }catch(...){close_socket(listener);close_socket(udp);
#ifdef _WIN32
   if(winsock)WSACleanup();
#endif
   throw;
  }
 }
 ~Impl(){connections.clear();close_socket(listener);close_socket(udp);wipe(invitation);wipe(join_code);if(credential)wipe(credential->secret);if(pending_credential)wipe(pending_credential->secret);
#ifdef _WIN32
 if(winsock)WSACleanup();
#endif
 }
 bool accepting()const{return !invitation.empty()&&!credential&&Clock::now()<invitation_end;}
 void clear_connections(){active=nullptr;connections.clear();messages.clear();message_bytes=0;state.connected=false;}
 void fail(Connection& c,const std::string& why){c.dead=true;if(&c==active){active=nullptr;state.connected=false;messages.clear();message_bytes=0;state.phase=credential?"reconnecting":"idle";state.error=why;}else if(c.outgoing){state.error=why;state.phase=credential?"reconnecting":(accepting()?"inviting":"idle");}if(c.outgoing&&!credential){wipe(join_code);}}
 bool queue(Connection& c,const Bytes& bytes){if(bytes.empty()||bytes.size()>max_frame||c.queued+bytes.size()+4>max_queued)return false;Bytes frame(bytes.size()+4);const auto n=std::uint32_t(bytes.size());frame[0]=n>>24;frame[1]=n>>16;frame[2]=n>>8;frame[3]=n;std::copy(bytes.begin(),bytes.end(),frame.begin()+4);c.queued+=frame.size();c.outgoing_frames.push_back(std::move(frame));return true;}
 void plain(Connection& c,const Json& j){auto s=j.dump();if(s.size()>4096||!queue(c,Bytes(s.begin(),s.end())))throw std::runtime_error("Room handshake queue exceeded");}
 bool encrypted(Connection& c,unsigned char type,const std::string& message){
  if(!c.derived||message.size()>max_message||c.tx_sequence==std::numeric_limits<std::uint64_t>::max())return false;
  Bytes payload(message.size()+1);payload[0]=type;std::copy(message.begin(),message.end(),payload.begin()+1);
  Bytes wire(9+payload.size()+crypto_aead_xchacha20poly1305_ietf_ABYTES);wire[0]=0x80;put64(wire.data()+1,c.tx_sequence);
  std::array<unsigned char,24> nonce{};put64(nonce.data()+16,c.tx_sequence);
  unsigned long long n=0;
  crypto_aead_xchacha20poly1305_ietf_encrypt(wire.data()+9,&n,payload.data(),payload.size(),wire.data(),9,nullptr,nonce.data(),c.tx_key.data());
  if(!queue(c,wire))return false;++c.tx_sequence;return true;
 }
 Json hello(const Connection& c,bool client){return Json{{"type",client?"client":"server"},{"protocol",protocol},{"room",c.room_id},{"device",device_json(config.local)},{"nonce",c.local_nonce},{"port",bound_port},{"mode",c.pairing?"pair":"resume"}};}
 std::string binding(Connection& c){const auto& a=c.outgoing?config.local:c.peer;const auto& b=c.outgoing?c.peer:config.local;return Json{{"protocol",protocol},{"room",c.room_id},{"client",device_json(a)},{"server",device_json(b)},{"client_port",c.outgoing?bound_port:c.peer_port},{"server_port",c.outgoing?c.peer_port:bound_port},{"client_nonce",c.outgoing?c.local_nonce:c.remote_nonce},{"server_nonce",c.outgoing?c.remote_nonce:c.local_nonce},{"mode",c.pairing?"pair":"resume"}}.dump();}
 void send_proof(Connection& c){c.tx_key=derive(c.master,c.transcript+(c.outgoing?"|client-to-server":"|server-to-client"));c.rx_key=derive(c.master,c.transcript+(c.outgoing?"|server-to-client":"|client-to-server"));c.derived=true;auto proof=derive(c.master,c.transcript+(c.outgoing?"|client-confirm":"|server-confirm"));plain(c,Json{{"type","confirm"},{"proof",hex(proof.data(),proof.size())}});wipe(proof);c.proof_sent=true;}
 void setup_spake(Connection& c){
  const auto mine=c.transcript+(c.outgoing?"|client":"|server");const auto theirs=c.transcript+(c.outgoing?"|server":"|client");
  c.spake=SPAKE2_CTX_new(c.outgoing?spake2_role_alice:spake2_role_bob,reinterpret_cast<const unsigned char*>(mine.data()),mine.size(),reinterpret_cast<const unsigned char*>(theirs.data()),theirs.size());
  if(!c.spake)throw std::runtime_error("Cannot initialize secure pairing");
  std::array<unsigned char,SPAKE2_MAX_MSG_SIZE> msg{};std::size_t n=0;
  if(!SPAKE2_generate_msg(c.spake,msg.data(),&n,msg.size(),reinterpret_cast<const unsigned char*>(c.code.data()),c.code.size()))throw std::runtime_error("Cannot initialize secure pairing");
  wipe(c.code);plain(c,Json{{"type","spake"},{"message",hex(msg.data(),n)}});
 }
 bool rate_allow(){auto now=Clock::now();if(now-rate_window>std::chrono::minutes(1)){rate_window=now;attempts=0;}if(attempts>=10||invite_attempts>=20)return false;++attempts;++invite_attempts;return true;}
 void complete(Connection& c){
  if(c.confirmed)return;if(active&&active!=&c)throw std::runtime_error("Room already connected");
  if(c.pairing&&!c.outgoing&&!accepting())throw std::runtime_error("Room invitation expired or already used");
  if(c.pairing){if(credential)throw std::runtime_error("Room already paired");credential=Credential{c.room_id,c.peer,c.address,c.peer_port,c.master};}
  if(!credential||credential->peer.id!=c.peer.id||credential->room_id!=c.room_id)throw std::runtime_error("Unexpected room identity");
  credential->last_address=c.address;credential->peer_port=c.peer_port;credential->peer=c.peer;pending_credential=*credential;
  c.confirmed=true;active=&c;state.connected=true;state.phase="connected";state.error.clear();state.room_id=c.room_id;state.peer=c.peer;state.peer_address=c.address;state.peer_port=c.peer_port;++state.generation;
  wipe(invitation);state.invitation_code.clear();state.invitation_seconds=0;wipe(join_code);
  for(auto& other:connections)if(other.get()!=&c)other->dead=true;
 }
 void handshake(Connection& c,const Json& j){
  if(!j.is_object())throw std::runtime_error("Invalid room handshake");auto type=j.at("type").get<std::string>();
  if(type=="server"||type=="client"){
   if(c.hello_received||type!=(c.outgoing?"server":"client")||j.at("protocol")!=protocol)throw std::runtime_error("Incompatible room handshake");
   c.peer=parse_device(j.at("device"));if(c.peer.id==config.local.id)throw std::runtime_error("A computer cannot pair with itself");
   if(!c.expected_peer.empty()&&c.expected_peer!=c.peer.id)throw std::runtime_error("Room device identity changed");
   const auto rid=j.at("room").get<std::string>();if(!valid_id(rid)||rid!=c.room_id)throw std::runtime_error("Room invitation changed");
   c.remote_nonce=j.at("nonce").get<std::string>();unhex(c.remote_nonce,32);c.peer_port=parse_port(j.at("port"));
   if(!c.outgoing){const auto mode=j.at("mode").get<std::string>();if(mode=="pair"){if(!accepting()||!rate_allow())throw std::runtime_error("Invitation unavailable or too many attempts");c.pairing=true;c.code=invitation;}else if(mode=="resume"){if(!credential||credential->peer.id!=c.peer.id||credential->room_id!=rid)throw std::runtime_error("Unknown paired device");c.pairing=false;c.master=credential->secret;}else throw std::runtime_error("Invalid handshake mode");}
   else if(!c.pairing){if(!credential||credential->peer.id!=c.peer.id||credential->room_id!=rid)throw std::runtime_error("Unknown paired device");c.master=credential->secret;}
   c.hello_received=true;c.transcript=binding(c);
   if(c.outgoing)plain(c,hello(c,true));
   if(c.pairing)setup_spake(c);else send_proof(c);
  }else if(type=="spake"){
   if(!c.hello_received||!c.pairing||!c.spake||c.derived)throw std::runtime_error("Unexpected secure pairing message");
   auto msg=unhex(j.at("message").get<std::string>(),SPAKE2_MAX_MSG_SIZE);std::array<unsigned char,SPAKE2_MAX_KEY_SIZE> result{};std::size_t n=0;
   const bool ok=SPAKE2_process_msg(c.spake,result.data(),&n,result.size(),msg.data(),msg.size())==1;
   SPAKE2_CTX_free(c.spake);c.spake=nullptr;
   if(!ok){sodium_memzero(result.data(),result.size());throw std::runtime_error("Secure pairing failed");}
   crypto_generichash_state hash;crypto_generichash_init(&hash,nullptr,0,c.master.size());crypto_generichash_update(&hash,reinterpret_cast<const unsigned char*>(c.transcript.data()),c.transcript.size());crypto_generichash_update(&hash,result.data(),n);crypto_generichash_final(&hash,c.master.data(),c.master.size());sodium_memzero(result.data(),result.size());sodium_memzero(&hash,sizeof(hash));send_proof(c);
  }else if(type=="confirm"){
   if(!c.derived||!c.proof_sent||c.confirmed)throw std::runtime_error("Unexpected room confirmation");auto got=unhex(j.at("proof").get<std::string>(),32);auto expected=derive(c.master,c.transcript+(c.outgoing?"|server-confirm":"|client-confirm"));const bool ok=sodium_memcmp(got.data(),expected.data(),32)==0;wipe(expected);if(!ok)throw std::runtime_error("Room code or saved pairing does not match");complete(c);
  }else throw std::runtime_error("Unknown room handshake message");
 }
 void frame(Connection& c,const Bytes& wire){
  if(wire.empty())throw std::runtime_error("Empty room message");
  if(wire[0]!=0x80){if(c.confirmed||wire.size()>4096)throw std::runtime_error("Unexpected plaintext room message");handshake(c,Json::parse(wire));return;}
  if(!c.confirmed||wire.size()<26)throw std::runtime_error("Unauthenticated room message");const auto sequence=get64(wire.data()+1);if(sequence!=c.rx_sequence||sequence==std::numeric_limits<std::uint64_t>::max())throw std::runtime_error("Room message replay or invalid sequence");
  std::array<unsigned char,24> nonce{};put64(nonce.data()+16,sequence);Bytes clear(wire.size()-9-16);unsigned long long n=0;
  if(crypto_aead_xchacha20poly1305_ietf_decrypt(clear.data(),&n,nullptr,wire.data()+9,wire.size()-9,wire.data(),9,nonce.data(),c.rx_key.data()))throw std::runtime_error("Room message authentication failed");
  ++c.rx_sequence;
  if(clear[0]==1){if(clear.size()-1>max_message||message_bytes+clear.size()-1>max_queued||messages.size()>=128)throw std::runtime_error("Room receive queue exceeded");messages.emplace_back(clear.begin()+1,clear.end());message_bytes+=clear.size()-1;}
  else if(clear[0]!=2||clear.size()!=1)throw std::runtime_error("Invalid encrypted room message");
 }
 void connect_to(const Nearby& target,bool pairing){
  if(connections.size()>=8)return;
  auto c=std::make_unique<Connection>();c->outgoing=true;c->connecting=true;c->pairing=pairing;c->address=target.address;c->peer_port=target.port;c->room_id=target.room_id;c->expected_peer=target.device.id;c->local_nonce=hex(net::random_key().data(),32);if(pairing)c->code=join_code;
  auto address=endpoint(target.address,target.port);c->fd=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);if(c->fd==invalid_socket)throw std::runtime_error("Cannot create room connection");nonblocking(c->fd);
  if(::connect(c->fd,reinterpret_cast<const sockaddr*>(&address),sizeof(address))==0)c->connecting=false;else if(!would_block())throw std::runtime_error("Room device is unreachable");
  connections.push_back(std::move(c));
 }
 void accept_connections(){
  for(int tries=0;tries<8;++tries){sockaddr_in addr{};RoomSocklen len=sizeof(addr);RoomSocket fd=::accept(listener,reinterpret_cast<sockaddr*>(&addr),&len);if(fd==invalid_socket)break;
   if(active||connections.size()>=8||(!accepting()&&!credential)){close_socket(fd);continue;}
   auto c=std::make_unique<Connection>();c->fd=fd;c->address=ip_string(addr);c->room_id=credential?credential->room_id:state.room_id;c->local_nonce=hex(net::random_key().data(),32);c->pairing=!credential;
   try{nonblocking(fd);plain(*c,hello(*c,false));connections.push_back(std::move(c));}catch(...){}
  }
 }
 void io(Connection& c){
  if(c.connecting){fd_set writable;FD_ZERO(&writable);FD_SET(c.fd,&writable);timeval zero{};if(select(int(c.fd)+1,nullptr,&writable,nullptr,&zero)>0){int err=0;RoomSocklen n=sizeof(err);if(getsockopt(c.fd,SOL_SOCKET,SO_ERROR,reinterpret_cast<char*>(&err),&n)||err)throw std::runtime_error("Room device is unreachable");c.connecting=false;}else return;}
  for(int count=0;count<16&&!c.outgoing_frames.empty();++count){auto& bytes=c.outgoing_frames.front();
#ifdef MSG_NOSIGNAL
   constexpr int send_flags=MSG_NOSIGNAL;
#else
   constexpr int send_flags=0;
#endif
   int sent=::send(c.fd,reinterpret_cast<const char*>(bytes.data()+c.write_offset),int(bytes.size()-c.write_offset),send_flags);if(sent<0){if(would_block())break;throw std::runtime_error("Room connection lost");}if(sent==0)throw std::runtime_error("Room connection closed");c.sent=Clock::now();c.write_offset+=sent;c.queued-=sent;if(c.write_offset==bytes.size()){c.outgoing_frames.pop_front();c.write_offset=0;}
  }
  unsigned processed=0;
  auto consume=[&]{while(processed<64&&c.incoming.size()>=4){const auto p=c.incoming.data();const std::size_t size=(std::uint32_t(p[0])<<24)|(std::uint32_t(p[1])<<16)|(std::uint32_t(p[2])<<8)|p[3];if(size==0||size>max_frame||(!c.confirmed&&size>4096))throw std::runtime_error("Room frame exceeds limit");if(c.incoming.size()<size+4)break;Bytes wire(c.incoming.begin()+4,c.incoming.begin()+4+size);c.incoming.erase(c.incoming.begin(),c.incoming.begin()+4+size);++processed;frame(c,wire);if(c.dead)break;}};
  // Drain already-buffered frames even when the socket has no new readable bytes.
  // A burst exceeding one poll's budget must not wait for another network packet.
  consume();if(c.dead||processed>=64)return;
  std::array<unsigned char,8192> buffer{};
  for(int count=0;count<16;++count){int n=::recv(c.fd,reinterpret_cast<char*>(buffer.data()),int(buffer.size()),0);if(n<0){if(would_block())break;throw std::runtime_error("Room connection lost");}if(n==0)throw std::runtime_error("Room connection closed");c.received=Clock::now();if(c.incoming.size()+n>max_queued)throw std::runtime_error("Room input buffer exceeded");c.incoming.insert(c.incoming.end(),buffer.begin(),buffer.begin()+n);consume();if(c.dead||processed>=64)return;}
 }
 void advertise(const sockaddr_in& to){if(state.room_id.empty())return;auto j=Json{{"protocol",protocol},{"type","offer"},{"room",state.room_id},{"device",device_json(config.local)},{"port",bound_port},{"accepting",accepting()}};auto s=j.dump();sendto(udp,s.data(),int(s.size()),0,reinterpret_cast<const sockaddr*>(&to),sizeof(to));}
 void discover_at(const std::string& address,std::uint16_t port){auto a=endpoint(address,port);auto s=Json{{"protocol",protocol},{"type","discover"},{"device",config.local.id}}.dump();sendto(udp,s.data(),int(s.size()),0,reinterpret_cast<const sockaddr*>(&a),sizeof(a));last_discover=Clock::now();}
 void discover(){discover_at(config.discovery_address,config.discovery_port);}
 void discovery_io(){for(int i=0;i<32;++i){std::array<char,2048> b{};sockaddr_in from{};RoomSocklen n=sizeof(from);auto count=recvfrom(udp,b.data(),int(b.size()),0,reinterpret_cast<sockaddr*>(&from),&n);if(count<=0)break;try{auto j=Json::parse(b.data(),b.data()+count);if(j.at("protocol")!=protocol)continue;auto type=j.at("type").get<std::string>();if(type=="discover"){if(j.at("device")!=config.local.id)advertise(from);}else if(type=="offer"){auto device=parse_device(j.at("device"));auto room=j.at("room").get<std::string>();if(device.id==config.local.id||!valid_id(room))continue;if(seen.size()>=64&&!seen.contains(device.id))continue;seen[device.id]=Seen{Nearby{room,device,ip_string(from),parse_port(j.at("port")),j.at("accepting").get<bool>()},Clock::now()};}}catch(...) {}}
  auto now=Clock::now();for(auto it=seen.begin();it!=seen.end();)if(now-it->second.time>std::chrono::seconds(10))it=seen.erase(it);else ++it;
 }
 void tick(){
  const auto now=Clock::now();if(!invitation.empty()&&now>=invitation_end){wipe(invitation);state.invitation_code.clear();state.invitation_seconds=0;if(!credential){state.phase="idle";state.error="Room invitation expired. Create a new code.";}for(auto& c:connections)if(c->pairing&&!c->confirmed)c->dead=true;}
  if(now-last_discover>std::chrono::seconds(3))discover();discovery_io();accept_connections();
  if(!active&&credential&&config.local.id<credential->peer.id&&connections.empty()&&now>=next_connect){Nearby target{credential->room_id,credential->peer,credential->last_address,credential->peer_port,false};auto it=seen.find(credential->peer.id);if(it!=seen.end()&&it->second.item.room_id==credential->room_id)target=it->second.item;try{connect_to(target,false);}catch(const std::exception& e){state.error=e.what();}next_connect=now+std::chrono::seconds(2);}
  for(auto& c:connections){if(c->dead)continue;try{if(!c->confirmed&&now-c->created>std::chrono::seconds(10))throw std::runtime_error("Room handshake timed out");if(c->confirmed&&now-c->received>std::chrono::seconds(5))throw std::runtime_error("Room connection interrupted; reconnecting");if(c->confirmed&&now-c->sent>std::chrono::seconds(1)&&c->outgoing_frames.empty())if(!encrypted(*c,2,{}))throw std::runtime_error("Room send queue exceeded");io(*c);}catch(const std::exception& e){fail(*c,e.what());}}
  connections.erase(std::remove_if(connections.begin(),connections.end(),[](auto& c){return c->dead;}),connections.end());
 }
};
Service::Service(Config c):impl_(std::make_unique<Impl>(std::move(c))){}
Service::~Service()=default;
std::string Service::create_invitation(){auto& v=*impl_;if(v.credential)throw std::runtime_error("Forget the current room before creating another");v.clear_connections();wipe(v.join_code);wipe(v.invitation);v.state=Status{};v.state.room_id=new_id();for(int i=0;i<8;++i)v.invitation+=alphabet[randombytes_uniform(std::strlen(alphabet))];v.state.invitation_code=v.invitation.substr(0,4)+"-"+v.invitation.substr(4);v.invitation_end=Clock::now()+std::chrono::seconds(v.config.invitation_lifetime_seconds);v.state.phase="inviting";v.invite_attempts=0;v.discover();return v.state.invitation_code;}
void Service::cancel_invitation(){auto& v=*impl_;wipe(v.invitation);v.state.invitation_code.clear();v.state.invitation_seconds=0;if(!v.credential){v.clear_connections();wipe(v.join_code);v.state=Status{};}}
void Service::set_local_name(const std::string& name){validate_name(name);impl_->config.local.name=name;}
void Service::discover(){impl_->discover();}
void Service::discover_at(const std::string& address,std::uint16_t port){if(!port)throw std::runtime_error("Invalid discovery port");impl_->discover_at(address,port);}
std::vector<Nearby> Service::nearby()const{std::vector<Nearby> result;for(auto& [id,s]:impl_->seen)result.push_back(s.item);return result;}
void Service::join(const Nearby& target,const std::string& code){auto& v=*impl_;if(v.credential)throw std::runtime_error("Forget the current room before joining another");if(!valid_id(target.room_id)||(!target.device.id.empty()&&!valid_id(target.device.id))||!target.port)throw std::runtime_error("Invalid room invitation");auto normalized=normalize_code(code);endpoint(target.address,target.port);v.clear_connections();wipe(v.invitation);v.state=Status{};v.state.room_id=target.room_id;v.state.phase="joining";wipe(v.join_code);v.join_code=std::move(normalized);try{v.connect_to(target,true);}catch(...){wipe(v.join_code);v.state.phase="idle";throw;}}
void Service::restore(const Credential& credential){auto& v=*impl_;if(!valid_id(credential.room_id))throw std::runtime_error("Invalid saved room");validate_device(credential.peer);if(credential.peer.id==v.config.local.id||!credential.peer_port)throw std::runtime_error("Invalid saved room peer");endpoint(credential.last_address,credential.peer_port);v.clear_connections();if(v.pending_credential)wipe(v.pending_credential->secret);v.pending_credential.reset();if(v.credential)wipe(v.credential->secret);v.credential=credential;wipe(v.invitation);wipe(v.join_code);v.state=Status{};v.state.room_id=credential.room_id;v.state.peer=credential.peer;v.state.peer_address=credential.last_address;v.state.peer_port=credential.peer_port;v.state.phase="reconnecting";v.next_connect={};v.discover();}
void Service::forget(){auto& v=*impl_;v.clear_connections();if(v.credential)wipe(v.credential->secret);if(v.pending_credential)wipe(v.pending_credential->secret);v.credential.reset();v.pending_credential.reset();wipe(v.invitation);wipe(v.join_code);v.state=Status{};}
void Service::poll(){impl_->tick();}
Status Service::status()const{auto s=impl_->state;if(impl_->accepting())s.invitation_seconds=static_cast<std::uint32_t>(std::max(std::int64_t(1),std::chrono::duration_cast<std::chrono::seconds>(impl_->invitation_end-Clock::now()).count()));return s;}
std::optional<Credential> Service::take_credential(){auto value=impl_->pending_credential;if(impl_->pending_credential)wipe(impl_->pending_credential->secret);impl_->pending_credential.reset();return value;}
bool Service::send(const std::string& message){return impl_->active&&impl_->active->confirmed&&impl_->encrypted(*impl_->active,1,message);}
std::optional<std::string> Service::receive(){if(impl_->messages.empty())return{};auto s=std::move(impl_->messages.front());impl_->messages.pop_front();impl_->message_bytes-=s.size();return s;}
std::uint16_t Service::port()const{return impl_->bound_port;}
}
