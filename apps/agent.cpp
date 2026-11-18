#include "bridge/agent_platform.hpp"
#include "bridge/agent_routes.hpp"
#include "bridge/credentials.hpp"
#include "bridge/control.hpp"
#include "bridge/room.hpp"
#include <sodium.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <chrono>
#include <thread>
#include <memory>
#include <optional>
#include <csignal>
#ifdef _WIN32
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#else
#include <arpa/inet.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <unistd.h>
#endif
using bridge::agent::Json;
namespace fs=std::filesystem;
using Clock=std::chrono::steady_clock;
namespace {
#ifdef _WIN32
using Socket=SOCKET;constexpr Socket invalid=INVALID_SOCKET;
void close_socket(Socket s){if(s!=invalid)closesocket(s);}
void nonblock(Socket s){u_long v=1;ioctlsocket(s,FIONBIO,&v);}
bool again(){return WSAGetLastError()==WSAEWOULDBLOCK;}
#else
using Socket=int;constexpr Socket invalid=-1;
void close_socket(Socket s){if(s!=invalid)::close(s);}
void nonblock(Socket s){fcntl(s,F_SETFL,fcntl(s,F_GETFL)|O_NONBLOCK);fcntl(s,F_SETFD,FD_CLOEXEC);}
bool again(){return errno==EAGAIN||errno==EWOULDBLOCK;}
#endif
struct OwnedSocket{Socket s=invalid;~OwnedSocket(){close_socket(s);}OwnedSocket()=default;explicit OwnedSocket(Socket v):s(v){}OwnedSocket(const OwnedSocket&)=delete;OwnedSocket&operator=(const OwnedSocket&)=delete;};
volatile std::sig_atomic_t interrupted=0;
void signal_stop(int){interrupted=1;}
std::string utf8(const fs::path&p){auto s=p.u8string();return {s.begin(),s.end()};}
fs::path path(const std::string&s){return fs::path(std::u8string(s.begin(),s.end()));}
Json read_json(const fs::path&p){std::ifstream in(p,std::ios::binary|std::ios::ate);if(!in)return Json::object();auto size=in.tellg();if(size<0||size>131072)throw std::runtime_error("Invalid state file size");in.seekg(0);std::string s(size_t(size),' ');in.read(s.data(),s.size());return Json::parse(s);}
std::string bounded(const Json&j,const char*key,size_t max=1024){auto v=j.value(key,std::string());if(v.size()>max||v.find('\0')!=v.npos||v.find_first_of("\r\n")!=v.npos)throw std::runtime_error(std::string("Invalid ")+key);return v;}
struct Options{
 fs::path directory=bridge::agent::default_directory(),engine;
#ifdef _WIN32
 std::string platform="windows";
#else
 std::string platform="mac";
#endif
 std::string bind="0.0.0.0";uint16_t room_port=45671,media_port=45670,discovery_port=45671;
 std::vector<std::string> forwarded;
};
Options options(int argc,char**argv){Options o;for(int i=2;i<argc;i+=2){if(i+1==argc)throw std::runtime_error("Option needs a value");std::string k=argv[i],v=argv[i+1];o.forwarded.push_back(k);o.forwarded.push_back(v);if(k=="--data-dir")o.directory=fs::absolute(path(v));else if(k=="--engine")o.engine=fs::absolute(path(v));else if(k=="--platform")o.platform=v;else if(k=="--bind")o.bind=v;else {size_t n=0;auto p=std::stoul(v,&n);if(n!=v.size()||p>65535)throw std::runtime_error("Invalid port");if(k=="--room-port")o.room_port=uint16_t(p);else if(k=="--media-port"&&p)o.media_port=uint16_t(p);else if(k=="--discovery-port"&&p)o.discovery_port=uint16_t(p);else throw std::runtime_error("Unknown option");}}if(o.platform!="windows"&&o.platform!="mac")throw std::runtime_error("Unknown platform");if(o.engine.empty()){
#ifdef _WIN32
 o.engine=bridge::agent::executable_path().parent_path()/"audiobridge.exe";
#else
 o.engine=bridge::agent::executable_path().parent_path()/"bridge-engine";if(!fs::exists(o.engine))o.engine=bridge::agent::executable_path().parent_path()/"audiobridge";
#endif
 }return o;}
bool wait_socket(Socket s,bool writing,int milliseconds){fd_set set;FD_ZERO(&set);FD_SET(s,&set);timeval t{milliseconds/1000,(milliseconds%1000)*1000};return select(int(s)+1,writing?nullptr:&set,writing?&set:nullptr,nullptr,&t)>0;}
Json request(const Options&o,const Json&q){auto e=read_json(o.directory/"endpoint.json");auto token=e.value("token",std::string());auto port=e.value("port",0);if(token.size()!=64||port<1||port>65535)throw std::runtime_error("AudioBridge background agent is not running");OwnedSocket socket(::socket(AF_INET,SOCK_STREAM,IPPROTO_TCP));if(socket.s==invalid)throw std::runtime_error("Cannot create local connection");nonblock(socket.s);sockaddr_in a{};a.sin_family=AF_INET;a.sin_port=htons(uint16_t(port));inet_pton(AF_INET,"127.0.0.1",&a.sin_addr);connect(socket.s,reinterpret_cast<sockaddr*>(&a),sizeof a);if(!wait_socket(socket.s,true,1500))throw std::runtime_error("Agent connection timed out");
 auto wire=Json{{"token",token},{"request",q}}.dump()+"\n";size_t at=0;auto end=Clock::now()+std::chrono::seconds(4);while(at<wire.size()&&Clock::now()<end){if(!wait_socket(socket.s,true,100))continue;auto n=send(socket.s,wire.data()+at,int(wire.size()-at),0);if(n<=0)throw std::runtime_error("Agent is unavailable");at+=size_t(n);}std::string result;while(Clock::now()<end){if(!wait_socket(socket.s,false,100))continue;char b[8192];auto n=recv(socket.s,b,sizeof b,0);if(n<=0)break;result.append(b,size_t(n));if(result.size()>131072)throw std::runtime_error("Agent response too large");if(result.find('\n')!=result.npos)return Json::parse(result.substr(0,result.find('\n')));}throw std::runtime_error("Agent response timed out");}
Json settings_json(const bridge::Settings&s){Json j;std::istringstream in(bridge::control::snapshot(s));std::string k;double v;while(in>>k>>v)j[k]=v;return j;}
bridge::Settings parse_settings(const Json&j){bridge::Settings s;for(auto it=j.begin();it!=j.end();++it)if(it.value().is_number())bridge::control::update(s,it.key(),it.value().get<float>());return s;}
class Agent {
 Options o_;Json saved_,peer_=Json::object(),telemetry_=Json::object();bridge::room::Device identity_;std::unique_ptr<bridge::room::Service> room_;std::optional<bridge::room::Credential> credential_;
 std::vector<bridge::AudioDevice> audio_;std::map<std::string,std::string> routes_;bridge::agent::Child engine_;bridge::Settings settings_;std::string stdout_,message_="Create or join a room.",mute_reason_="Microphone starts muted. Unmute when ready.";
 bool wanted_=false,was_connected_=false,engine_synced_=false;uint64_t sequence_=0,peer_sequence_=0,inventory_revision_=0;Clock::time_point refresh_{},advertise_{},retry_{},last_telemetry_{};
 std::map<std::string,Clock::time_point> pending_config_;uint64_t room_generation_=0;
 std::optional<Json> pending_join_;Clock::time_point join_deadline_{};
 std::string secret_id()const{return "room-"+credential_->room_id;}
 std::string audio_id()const{return "audio-"+credential_->room_id;}
 void persist(){saved_["version"]=2;saved_["device_id"]=identity_.id;saved_["name"]=identity_.name;saved_["platform"]=identity_.platform;saved_["routes"]=routes_;saved_["wanted_running"]=wanted_;saved_["settings"]=settings_json(settings_);if(credential_){auto&c=*credential_;saved_["room"]={{"id",c.room_id},{"peer_id",c.peer.id},{"peer_name",c.peer.name},{"peer_platform",c.peer.platform},{"address",c.last_address},{"port",c.peer_port}};}else saved_.erase("room");bridge::agent::atomic_write(o_.directory/"state.json",saved_.dump(2)+"\n");}
 void refresh(){audio_=bridge::enumerate_audio_devices();bridge::agent::auto_routes(routes_,audio_,o_.platform);refresh_=Clock::now()+std::chrono::seconds(5);++inventory_revision_;}
 Json local_info(){Json j={{"id",identity_.id},{"name",identity_.name},{"platform",o_.platform},{"online",true},{"audio_devices",bridge::agent::audio_json(audio_)},{"setup_status",bridge::agent::setup_status(routes_,audio_,o_.platform)},{"capture_authorized",bridge::agent::capture_authorized()},{"media_port",o_.media_port},{"inventory_revision",inventory_revision_},{"diagnostics",telemetry_}};for(auto&[k,v]:routes_)j[k]=v;return j;}
 void send(const Json&j){if(!room_->send(j.dump()))throw std::runtime_error("Other device is unavailable; change was not sent");}
 void snapshot(){if(room_->status().connected){Json j={{"type","snapshot"},{"device",local_info()},{"settings",settings_json(settings_)},{"running",engine_.running()},{"diagnostics",telemetry_},{"mute_reason",mute_reason_}};room_->send(j.dump());}advertise_=Clock::now()+std::chrono::seconds(1);}
 void stop_local(bool forget_intent=false){engine_.stop();telemetry_=Json::object();engine_synced_=false;stdout_.clear();if(forget_intent)wanted_=false;}
 void start_local(){
  if(engine_.running())return;if(!credential_||!room_->status().connected||peer_.empty())throw std::runtime_error("Wait for both room devices to connect");
  if(credential_->peer.platform==o_.platform)throw std::runtime_error("This release requires one Windows and one Mac device");
  refresh();bridge::agent::validate_independent(audio_,routes_);auto setup=bridge::agent::setup_status(routes_,audio_,o_.platform);if(setup!="Ready")throw std::runtime_error(setup);
  if(!bridge::agent::capture_authorized())throw std::runtime_error("Grant AudioBridge microphone access on this Mac, then press Start there once");
  auto local=o_.platform=="windows"?bridge::Device::Windows:bridge::Device::Mac;
  if(settings_.primary==local&&!bridge::agent::available(audio_,routes_["headphones_output"],false))throw std::runtime_error("Select available headphones on the Hub");
  if(settings_.microphoneSource==local&&!bridge::agent::available(audio_,routes_["physical_microphone"],true))throw std::runtime_error("Select an available physical microphone on the microphone source");
  auto media=peer_.value("media_port",45670);if(media<1||media>65535)throw std::runtime_error("Invalid peer media port");auto address=room_->status().peer_address;in_addr ip{};if(inet_pton(AF_INET,address.c_str(),&ip)!=1)throw std::runtime_error("Authenticated peer address unavailable");
  std::ostringstream config;config<<"device="<<o_.platform<<"\npeer="<<address<<"\nport="<<o_.media_port<<"\npeer_port="<<media<<"\nkey_stdin=1\n";for(auto key:{"playback_input","microphone_output","headphones_output","physical_microphone"})config<<key<<'='<<routes_[key]<<'\n';
  bridge::agent::atomic_write(o_.directory/"engine.conf",config.str());engine_.start(o_.engine,{"run",utf8(o_.directory/"engine.conf")});auto media_key=bridge::room::audio_key(*credential_);auto key_frame=bridge::net::key_to_hex(media_key)+"\n";sodium_memzero(media_key.data(),media_key.size());try{engine_.send(key_frame);}catch(...){sodium_memzero(key_frame.data(),key_frame.size());stop_local();throw;}sodium_memzero(key_frame.data(),key_frame.size());engine_synced_=false;stdout_.clear();telemetry_=Json::object();settings_.globalMute=true;mute_reason_="Audio started or reconnected. Unmute when ready.";message_="Starting audio on both devices…";retry_=Clock::now()+std::chrono::seconds(3);
 }
 void apply_control(const Json&q){std::string key=bounded(q,"key",40);float value=q.at("value").get<float>();auto next=settings_;if(!bridge::control::update(next,key,value))throw std::runtime_error("Invalid mixer setting");if(engine_.running()){engine_.send(key+" "+std::to_string(value)+"\n");message_="Mixer change submitted; waiting for audio-engine acknowledgement…";}else{settings_=next;persist();}
  if(key=="mute")mute_reason_=value?"Microphone muted from the room controls.":"";
  if(key=="primary"||key=="mic_source")mute_reason_="Audio role changed. Unmute when ready.";
 }
 void configure_local(const Json&q){auto next=routes_;std::string name=q.contains("name")?bounded(q,"name",64):identity_.name;if(name.empty())throw std::runtime_error("Device name cannot be empty");refresh();for(auto key:{"playback_input","microphone_output","headphones_output","physical_microphone"})if(q.contains(key)){auto value=bounded(q,key);bridge::agent::validate_route(audio_,key,value);next[key]=value;}
  bridge::agent::validate_independent(audio_,next);bool changed=next!=routes_;if(changed)stop_local();routes_=std::move(next);identity_.name=name;room_->set_local_name(name);++inventory_revision_;persist();if(changed&&wanted_){try{start_local();}catch(const std::exception&e){message_=e.what();}}snapshot();
 }
 void remote(const Json&j){auto type=bounded(j,"type",32);
  if(type=="snapshot"){
   auto d=j.at("device");if(d.at("id")!=credential_->peer.id||d.at("platform")!=credential_->peer.platform)throw std::runtime_error("Peer identity changed");if(d.dump().size()>45000)throw std::runtime_error("Peer inventory too large");peer_=d;peer_["online"]=true;peer_["running"]=j.value("running",false);peer_["diagnostics"]=j.value("diagnostics",Json::object());
   if(o_.platform=="mac"&&!engine_.running()&&j.contains("settings"))settings_=parse_settings(j.at("settings"));
  }else if(type=="request"){
   auto seq=j.at("sequence").get<uint64_t>();if(seq<=peer_sequence_)return;peer_sequence_=seq;auto q=j.at("body");auto command=bounded(q,"command",32);Json result={{"type","result"},{"sequence",seq},{"ok",true}};
   try{if(command=="configure")configure_local(q);else if(command=="start"){wanted_=true;start_local();persist();}else if(command=="stop"){stop_local(true);persist();}else if(command=="test_tone"){engine_.send("test_tone\n");}else if(command=="control"){if(o_.platform!="windows")throw std::runtime_error("Mixer coordinator is Windows");apply_control(q);}else throw std::runtime_error("Unsupported remote command");}
   catch(const std::exception&e){result["ok"]=false;result["error"]=e.what();}room_->send(result.dump());snapshot();
  }else if(type=="result"){
   auto seq=std::to_string(j.at("sequence").get<uint64_t>());pending_config_.erase(seq);if(!j.value("ok",false))message_="Other device: "+j.value("error",std::string("request failed"));else message_="Room change acknowledged.";
  }
 }
 void remote_request(Json q){auto seq=++sequence_;send({{"type","request"},{"sequence",seq},{"body",q}});pending_config_[std::to_string(seq)]=Clock::now()+std::chrono::seconds(8);}
 bool try_join(const Json&q){auto address=bounded(q,"address",128),id=bounded(q,"room_id",64);std::vector<bridge::room::Nearby> candidates;for(auto&n:room_->nearby())if(n.accepting&&(id.empty()||id==n.room_id)&&(address.empty()||address==n.address||address==n.address+":"+std::to_string(n.port)))candidates.push_back(n);
  if(candidates.size()>1)throw std::runtime_error("Several rooms are nearby. Select the room to join.");if(candidates.empty())return false;
  if(candidates.front().device.platform==o_.platform)throw std::runtime_error("This release pairs one Windows computer with one Mac");room_->join(candidates.front(),bounded(q,"code",16));message_="Pairing securely…";return true;
 }
 void handle_telemetry(){stdout_+=engine_.read_output();if(stdout_.size()>131072){stdout_.clear();throw std::runtime_error("Audio engine output exceeded limit");}for(size_t end;(end=stdout_.find('\n'))!=stdout_.npos;){auto line=stdout_.substr(0,end);stdout_.erase(0,end+1);try{auto j=Json::parse(line);telemetry_=j;last_telemetry_=Clock::now();bool ready=j.value("connected",false)&&j.value("synchronized",false);
    if(ready&&!engine_synced_){engine_synced_=true;if(o_.platform=="windows"){settings_.globalMute=true;engine_.send(bridge::control::snapshot(settings_));}message_="Audio bridge active. Unmute the microphone when ready.";}
    if(engine_synced_){auto old=settings_json(settings_);if(j.contains("settings"))bridge::control::parse(settings_,j.at("settings").get<std::string>());if(old!=settings_json(settings_))persist();}
    if(j.value("muted",true)&&mute_reason_.empty())mute_reason_="Microphone is muted after reconnect, route change, or unavailable audio device. Check readiness, then unmute.";
    if(!j.value("muted",true))mute_reason_.clear();
   }catch(const Json::exception&){} }
   auto errors=engine_.read_errors();if(!errors.empty())message_=errors.substr(0,1200);
 }
public:
 bool quitting=false;
 explicit Agent(Options o):o_(std::move(o)){saved_=read_json(o_.directory/"state.json");identity_={saved_.value("device_id",bridge::room::new_id()),saved_.value("name",o_.platform=="mac"?std::string("MacBook"):std::string("Windows PC")),o_.platform};if(saved_.contains("routes"))routes_=saved_["routes"].get<std::map<std::string,std::string>>();else{
    auto legacy=o_.directory.parent_path()/(o_.platform=="mac"?"mac.conf":"windows.conf");if(fs::exists(legacy)){std::ifstream in(legacy);std::string line;while(std::getline(in,line)){auto p=line.find('=');if(p==line.npos)continue;auto k=line.substr(0,p),v=line.substr(p+1);if(!v.empty()&&v.back()=='\r')v.pop_back();if(k=="playback_input"||k=="microphone_output"||k=="headphones_output"||k=="physical_microphone")routes_[k]=v;}if(!fs::exists(o_.directory/"legacy-config.backup"))fs::copy_file(legacy,o_.directory/"legacy-config.backup");}}
#ifdef BRIDGE_AGENT_FAKE
  if(routes_.empty()){std::string p=o_.platform=="mac"?"mac-":"win-";routes_={{"playback_input",p+"playback"},{"microphone_output",p+"mic-sink"},{"headphones_output",p+"headphones"},{"physical_microphone",p+"mic"}};}
#endif
  if(saved_.contains("settings"))settings_=parse_settings(saved_["settings"]);settings_.globalMute=true;wanted_=saved_.value("wanted_running",false);refresh();bridge::room::Config c;c.local=identity_;c.bind_address=o_.bind;c.port=o_.room_port;c.discovery_port=o_.discovery_port;if(o_.bind=="127.0.0.1")c.discovery_address="127.0.0.1";room_=std::make_unique<bridge::room::Service>(c);
  if(saved_.contains("room")){try{auto r=saved_["room"];bridge::room::Credential key;key.room_id=r.at("id");key.peer={r.at("peer_id"),r.at("peer_name"),r.at("peer_platform")};key.last_address=r.at("address");key.peer_port=r.at("port");key.secret=bridge::credentials::load_key("room-"+key.room_id);credential_=key;room_->restore(key);sodium_memzero(key.secret.data(),key.secret.size());message_="Reconnecting to remembered room…";}catch(const std::exception&e){message_=std::string("Saved room could not be restored: ")+e.what();wanted_=false;if(credential_)sodium_memzero(credential_->secret.data(),credential_->secret.size());credential_.reset();}}
  persist();
 }
 ~Agent(){stop_local();if(credential_)sodium_memzero(credential_->secret.data(),credential_->secret.size());}
 Json status(){auto rs=room_->status();Json ds=Json::array();ds.push_back(local_info());if(!peer_.empty()){auto p=peer_;p["online"]=rs.connected;ds.push_back(p);}else if(credential_)ds.push_back({{"id",credential_->peer.id},{"name",credential_->peer.name},{"platform",credential_->peer.platform},{"online",false},{"audio_devices",Json::array()},{"setup_status","Offline"}});
  auto role=[&](bridge::Device d){auto p=d==bridge::Device::Windows?"windows":"mac";if(o_.platform==p)return identity_.id;if(credential_&&credential_->peer.platform==p)return credential_->peer.id;return std::string();};Json nearby=Json::array();for(auto&n:room_->nearby())if(n.accepting)nearby.push_back({{"id",n.room_id},{"name",n.device.name},{"address",n.address+":"+std::to_string(n.port)}});auto diagnostics=telemetry_;diagnostics["message"]=bridge::agent::setup_status(routes_,audio_,o_.platform);diagnostics["permission_granted"]=bridge::agent::capture_authorized();
  return {{"device_id",identity_.id},{"name",identity_.name},{"room_id",rs.room_id},{"room_port",room_->port()},{"invite_code",rs.invitation_code},{"invite_expires_seconds",rs.invitation_seconds},{"paired",credential_.has_value()},{"connected",rs.connected},{"running",engine_.running()},{"wanted_running",wanted_},{"status",rs.error.empty()?message_:rs.error},{"phase",rs.phase},{"mute_reason",mute_reason_},{"hub_device_id",role(settings_.primary)},{"mic_device_id",role(settings_.microphoneSource)},{"devices",ds},{"settings",settings_json(settings_)},{"discovered",nearby},{"diagnostics",diagnostics},{"pending_changes",pending_config_.size()}};
 }
 Json command(const Json&q){auto cmd=bounded(q,"command",32);
  if(cmd=="status"){}
  else if(cmd=="discover"){room_->discover();message_="Looking for nearby rooms…";}
  else if(cmd=="refresh"||cmd=="diagnose"){refresh();persist();snapshot();message_=bridge::agent::setup_status(routes_,audio_,o_.platform);}
  else if(cmd=="create"){if(q.contains("name"))configure_local(q);room_->create_invitation();message_="Enter this code on your other computer. It expires in five minutes.";}
  else if(cmd=="cancel_invite"){room_->cancel_invitation();message_="Invitation cancelled.";}
  else if(cmd=="join"){
    bridge::room::normalize_code(bounded(q,"code",16));pending_join_.reset();if(!try_join(q)){auto address=bounded(q,"address",128);if(address.empty())room_->discover();else{auto sep=address.find(':');uint16_t port=45671;if(sep!=address.npos){auto text=address.substr(sep+1);size_t n=0;auto p=std::stoul(text,&n);if(n!=text.size()||!p||p>65535)throw std::runtime_error("Invalid room address port");port=uint16_t(p);address.resize(sep);}room_->discover_at(address,port);}pending_join_=q;join_deadline_=Clock::now()+std::chrono::seconds(6);message_="Finding the room before pairing…";}
  }
  else if(cmd=="configure"){auto id=bounded(q,"device_id",64);if(id.empty()||id==identity_.id)configure_local(q);else if(credential_&&id==credential_->peer.id){remote_request(q);message_="Waiting for the other device to apply setup…";}else throw std::runtime_error("Unknown room device");}
  else if(cmd=="control"){if(o_.platform=="windows")apply_control(q);else{remote_request(q);message_="Waiting for mixer acknowledgement…";}}
  else if(cmd=="test_tone"){auto id=bounded(q,"device_id",64);if(id.empty()||id==identity_.id)engine_.send("test_tone\n");else if(credential_&&id==credential_->peer.id)remote_request(q);else throw std::runtime_error("Unknown room device");message_="Playing a quiet two-second test tone through the Hub headphones.";}
  else if(cmd=="start"){if(!room_->status().connected||peer_.empty())throw std::runtime_error("Wait for both devices to connect");wanted_=true;try{start_local();remote_request({{"command","start"}});persist();}catch(...){wanted_=false;stop_local();persist();throw;}}
  else if(cmd=="stop"){if(room_->status().connected)remote_request({{"command","stop"}});stop_local(true);message_="Audio stopped on this device; stop sent to the room.";persist();}
  else if(cmd=="forget"){stop_local(true);pending_join_.reset();if(credential_){bridge::credentials::delete_key(secret_id());bridge::credentials::delete_key(audio_id());sodium_memzero(credential_->secret.data(),credential_->secret.size());credential_.reset();}room_->forget();peer_=Json::object();pending_config_.clear();message_="Room forgotten on this device.";persist();}
  else if(cmd=="quit"){stop_local();quitting=true;message_="AudioBridge stopped on this computer.";}
  else throw std::runtime_error("Unknown agent command");return status();
 }
 void poll(){room_->poll();if(pending_join_){try{if(try_join(*pending_join_))pending_join_.reset();else if(Clock::now()>join_deadline_){pending_join_.reset();message_="Room not found. Check both devices are on the same network and allow AudioBridge through the private-network firewall.";}}catch(const std::exception&e){pending_join_.reset();message_=e.what();}}while(auto key=room_->take_credential()){
   try{bridge::credentials::store_key("room-"+key->room_id,key->secret);credential_=*key;persist();message_="Room paired. Choose the Hub, headphones, and microphone.";}catch(const std::exception&e){stop_local(true);room_->forget();if(credential_)sodium_memzero(credential_->secret.data(),credential_->secret.size());credential_.reset();message_=std::string("Could not save pairing: ")+e.what();persist();}sodium_memzero(key->secret.data(),key->secret.size());
  }
  auto rs=room_->status();if(rs.generation!=room_generation_){room_generation_=rs.generation;peer_sequence_=0;pending_config_.clear();}
  if(rs.connected&&!was_connected_){snapshot();refresh_=Clock::now();}if(!rs.connected&&was_connected_){stop_local();mute_reason_="Connection interrupted. Microphone will remain muted after reconnection.";message_="Reconnecting…";}was_connected_=rs.connected;
  for(int i=0;i<32;++i){auto s=room_->receive();if(!s)break;try{remote(Json::parse(*s));}catch(const std::exception&e){message_=std::string("Rejected room update: ")+e.what();}}
  if(Clock::now()>=refresh_){refresh();}if(Clock::now()>=advertise_)snapshot();
  if(wanted_&&rs.connected&&!peer_.empty()&&!engine_.running()&&Clock::now()>=retry_){retry_=Clock::now()+std::chrono::seconds(5);try{start_local();}catch(const std::exception&e){message_=e.what();}}
  handle_telemetry();for(auto it=pending_config_.begin();it!=pending_config_.end();){if(Clock::now()>it->second){message_="Other device did not acknowledge the change. Check its connection and retry.";it=pending_config_.erase(it);}else ++it;}
 }
};
struct Client {Socket socket;std::string input,output;size_t sent=0;Clock::time_point deadline=Clock::now()+std::chrono::seconds(2);};
void serve(const Options&o){bridge::agent::private_directory(o.directory);bridge::agent::InstanceLock lock(o.directory);Agent agent(o);OwnedSocket server(::socket(AF_INET,SOCK_STREAM,IPPROTO_TCP));if(server.s==invalid)throw std::runtime_error("Cannot listen for local controls");nonblock(server.s);sockaddr_in addr{};addr.sin_family=AF_INET;addr.sin_addr.s_addr=htonl(INADDR_LOOPBACK);addr.sin_port=0;if(bind(server.s,reinterpret_cast<sockaddr*>(&addr),sizeof addr)||listen(server.s,8))throw std::runtime_error("Cannot listen for local controls");
#ifdef _WIN32
 int len=sizeof addr;
#else
 socklen_t len=sizeof addr;
#endif
 getsockname(server.s,reinterpret_cast<sockaddr*>(&addr),&len);auto key=bridge::net::random_key();auto token=bridge::net::key_to_hex(key);sodium_memzero(key.data(),key.size());bridge::agent::atomic_write(o.directory/"endpoint.json",Json{{"port",ntohs(addr.sin_port)},{"token",token},{"version",2}}.dump());std::vector<Client> clients;
 while(!interrupted&&(!agent.quitting||!clients.empty())){if(!agent.quitting)agent.poll();Socket incoming=accept(server.s,nullptr,nullptr);if(incoming!=invalid){nonblock(incoming);if(clients.size()<8&&!agent.quitting)clients.push_back({incoming});else close_socket(incoming);}
  for(auto it=clients.begin();it!=clients.end();){bool done=Clock::now()>it->deadline;if(!done&&it->output.empty()){char b[8192];auto n=recv(it->socket,b,sizeof b,0);if(n>0)it->input.append(b,size_t(n));else if(n==0||(n<0&&!again()))done=true;if(it->input.size()>65536)done=true;auto end=it->input.find('\n');if(!done&&end!=std::string::npos){Json reply;try{auto q=Json::parse(it->input.substr(0,end));auto t=q.value("token",std::string());if(t.size()!=token.size()||sodium_memcmp(t.data(),token.data(),token.size()))throw std::runtime_error("Local authentication failed");reply={{"ok",true},{"state",agent.command(q.at("request"))}};}catch(const std::exception&e){reply={{"ok",false},{"error",e.what()}};}it->output=reply.dump()+"\n";}}
   if(!done&&!it->output.empty()){auto n=send(it->socket,it->output.data()+it->sent,int(it->output.size()-it->sent),0);if(n>0)it->sent+=size_t(n);else if(n<0&&!again())done=true;if(it->sent==it->output.size())done=true;}
   if(done){close_socket(it->socket);it=clients.erase(it);}else ++it;
  }std::this_thread::sleep_for(std::chrono::milliseconds(10));
 }for(auto&c:clients)close_socket(c.socket);std::error_code ec;fs::remove(o.directory/"endpoint.json",ec);
}
}
int main(int argc,char**argv){try{
#ifdef _WIN32
 WSADATA w{};if(WSAStartup(MAKEWORD(2,2),&w))throw std::runtime_error("Network initialization failed");
#else
 signal(SIGPIPE,SIG_IGN);
#endif
 signal(SIGTERM,signal_stop);signal(SIGINT,signal_stop);if(sodium_init()<0)throw std::runtime_error("Crypto initialization failed");if(argc<2)throw std::runtime_error("Usage: bridge-agent ensure | request | serve");auto o=options(argc,argv);auto cmd=std::string(argv[1]);
 if(cmd=="serve"){serve(o);return 0;}
 if(cmd=="ensure"){try{auto r=request(o,{{"command","status"}});if(r.value("ok",false)){std::cout<<"{\"ok\":true}\n";return 0;}}catch(...){}bridge::agent::private_directory(o.directory);std::vector<std::string> args{"serve"};args.insert(args.end(),o.forwarded.begin(),o.forwarded.end());bridge::agent::detach(bridge::agent::executable_path(),args);auto end=Clock::now()+std::chrono::seconds(6);while(Clock::now()<end){std::this_thread::sleep_for(std::chrono::milliseconds(100));try{auto r=request(o,{{"command","status"}});if(r.value("ok",false)){std::cout<<"{\"ok\":true}\n";return 0;}}catch(...){}}throw std::runtime_error("Background agent could not start. Check whether the room port is already in use.");}
 if(cmd=="request"){std::string input;char c;while(std::cin.get(c)&&c!='\n'){input+=c;if(input.size()>60000)throw std::runtime_error("Request too large");}auto q=Json::parse(input);Json response;try{response=request(o,q);}catch(...){if(q.value("command",std::string())!="quit")throw;bridge::agent::private_directory(o.directory);bridge::agent::InstanceLock absent(o.directory);response={{"ok",true},{"state",{{"running",false},{"status","AudioBridge is already stopped on this computer."}}}};}std::cout<<response.dump()<<'\n';return response.value("ok",false)?0:1;}
 throw std::runtime_error("Unknown command");
 }catch(const std::exception&e){std::cout<<Json{{"ok",false},{"error",e.what()}}.dump()<<'\n';return 1;}}
