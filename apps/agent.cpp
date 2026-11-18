#include "bridge/agent_platform.hpp"
#include "bridge/agent_routes.hpp"
#include "bridge/output_routing.hpp"
#include "bridge/credentials.hpp"
#include "bridge/control.hpp"
#include "bridge/room.hpp"
#include "bridge/device_profile.hpp"
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
#include <limits>
#include <cmath>
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
uint64_t counter(const Json&j,const char*key,bool allow_zero=false){
 auto&v=j.at(key);if(!v.is_number_integer()||(v.is_number_integer()&&!v.is_number_unsigned()&&v.get<int64_t>()<0))throw std::runtime_error("Invalid room counter");
 auto n=v.get<uint64_t>();if((!allow_zero&&!n)||n>uint64_t(9007199254740991ULL))throw std::runtime_error("Room counter outside supported range");return n;
}
uint64_t next_counter(uint64_t value){if(value>=9007199254740991ULL)throw std::runtime_error("Room revision exhausted; recreate the room before further changes");return value+1;}
void validate_diagnostics(const Json&j){
 if(!j.is_object()||j.dump().size()>8192)throw std::runtime_error("Invalid peer diagnostics");
 for(auto key:{"connected","synchronized","playback_ready","mic_sink_ready","headphones_ready","microphone_ready","muted","deafen"})if(j.contains(key)&&!j.at(key).is_boolean())throw std::runtime_error("Invalid diagnostic flag");
 for(auto key:{"microphone_level","playback_level","microphone_delivery_level","received_microphone_level"})if(j.contains(key)&&(!j.at(key).is_number()||!std::isfinite(j.at(key).get<double>())))throw std::runtime_error("Invalid signal level");
 if(j.contains("settings")&&(!j.at("settings").is_string()||j.at("settings").get_ref<const std::string&>().size()>4096))throw std::runtime_error("Invalid diagnostic settings");
}
void validate_inventory(const Json&d){
 if(!d.is_object()||d.dump().size()>45000)throw std::runtime_error("Invalid peer inventory");
 if(bounded(d,"name",64).empty())throw std::runtime_error("Missing peer name");
 for(auto key:{"headphones_output","physical_microphone","playback_input","microphone_output","output_routing_warning","app_playback_output","app_microphone_input","setup_status","kind","model","kind_override"})if(d.contains(key))bounded(d,key,1200);
 for(auto key:{"online","capture_authorized"})if(d.contains(key)&&!d.at(key).is_boolean())throw std::runtime_error("Invalid inventory flag");
 auto port=counter(d,"media_port");if(port>65535)throw std::runtime_error("Invalid peer media port");
 auto&ds=d.at("audio_devices");if(!ds.is_array()||ds.size()>256)throw std::runtime_error("Invalid endpoint list");
 for(auto&a:ds){if(bounded(a,"id",1024).empty()||bounded(a,"name",1024).empty())throw std::runtime_error("Invalid endpoint identity");for(auto key:{"inputs","outputs"})if(counter(a,key,true)>256)throw std::runtime_error("Invalid channel count");if(!a.at("rate").is_number()||!std::isfinite(a.at("rate").get<double>()))throw std::runtime_error("Invalid endpoint rate");}
 if(d.contains("diagnostics"))validate_diagnostics(d.at("diagnostics"));
}
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
 o.engine=bridge::agent::executable_path().parent_path()/"san-audisco.exe";
#else
 o.engine=bridge::agent::executable_path().parent_path()/"bridge-engine";if(!fs::exists(o.engine))o.engine=bridge::agent::executable_path().parent_path()/"san-audisco";
#endif
 }return o;}
bool wait_socket(Socket s,bool writing,int milliseconds){fd_set set;FD_ZERO(&set);FD_SET(s,&set);timeval t{milliseconds/1000,(milliseconds%1000)*1000};return select(int(s)+1,writing?nullptr:&set,writing?&set:nullptr,nullptr,&t)>0;}
Json request(const Options&o,const Json&q){auto e=read_json(o.directory/"endpoint.json");auto token=e.value("token",std::string());auto port=e.value("port",0);if(token.size()!=64||port<1||port>65535)throw std::runtime_error("San Audisco background agent is not running");OwnedSocket socket(::socket(AF_INET,SOCK_STREAM,IPPROTO_TCP));if(socket.s==invalid)throw std::runtime_error("Cannot create local connection");nonblock(socket.s);sockaddr_in a{};a.sin_family=AF_INET;a.sin_port=htons(uint16_t(port));inet_pton(AF_INET,"127.0.0.1",&a.sin_addr);connect(socket.s,reinterpret_cast<sockaddr*>(&a),sizeof a);if(!wait_socket(socket.s,true,1500))throw std::runtime_error("Agent connection timed out");
 auto wire=Json{{"token",token},{"request",q}}.dump()+"\n";size_t at=0;auto end=Clock::now()+std::chrono::seconds(4);while(at<wire.size()&&Clock::now()<end){if(!wait_socket(socket.s,true,100))continue;auto n=send(socket.s,wire.data()+at,int(wire.size()-at),0);if(n<=0)throw std::runtime_error("Agent is unavailable");at+=size_t(n);}std::string result;while(Clock::now()<end){if(!wait_socket(socket.s,false,100))continue;char b[8192];auto n=recv(socket.s,b,sizeof b,0);if(n<=0)break;result.append(b,size_t(n));if(result.size()>131072)throw std::runtime_error("Agent response too large");if(result.find('\n')!=result.npos)return Json::parse(result.substr(0,result.find('\n')));}throw std::runtime_error("Agent response timed out");}
Json settings_json(const bridge::Settings&s){Json j;std::istringstream in(bridge::control::snapshot(s));std::string k;double v;while(in>>k>>v)j[k]=v;return j;}
bridge::Settings parse_settings(const Json&j){bridge::Settings s;if(!j.is_object())throw std::runtime_error("Invalid saved settings");for(auto it=j.begin();it!=j.end();++it)if(!it.value().is_number()||!bridge::control::update(s,it.key(),it.value().get<float>()))throw std::runtime_error("Invalid saved control value");return s;}
class Agent {
 bridge::device::Profile profile_;std::string kind_override_="auto";bool name_automatic_=true;
 Options o_;Json saved_,peer_=Json::object(),telemetry_=Json::object();bridge::room::Device identity_;std::unique_ptr<bridge::room::Service> room_;std::optional<bridge::room::Credential> credential_;
 std::vector<bridge::AudioDevice> audio_;std::map<std::string,std::string> routes_;bridge::agent::Child engine_;bridge::Settings settings_;std::string stdout_,message_="Create or join a room.",mute_reason_="Microphone starts muted. Unmute when ready.";
 uint64_t authority_epoch_=1,settings_revision_=1,recovery_epoch_=0,recovery_revision_=0;bool migration_pending_=false;
 bool peer_compatible_=false,shared_ready_=false,authority_ready_=false,membership_ready_=false,legacy_settings_=false,recovery_required_=false;
 Json transfer_=Json::object();Clock::time_point transfer_deadline_{};
 bool wanted_=false,was_connected_=false,engine_synced_=false;uint64_t sequence_=0,peer_sequence_=0,inventory_revision_=0;Clock::time_point refresh_{},advertise_{},retry_{},last_telemetry_{};
 std::map<std::string,Clock::time_point> pending_config_;uint64_t room_generation_=0;
 std::optional<Json> pending_join_;Clock::time_point join_deadline_{};
 std::unique_ptr<bridge::routing::Backend> output_backend_;
 std::unique_ptr<bridge::routing::Lease> output_lease_;
 std::unique_ptr<bridge::routing::Backend> input_backend_;
 std::unique_ptr<bridge::routing::Lease> input_lease_;
 bool auto_microphone_=false;
 Clock::time_point output_check_{},output_unhealthy_{};
 std::string output_warning_;
 bridge::Device local_slot()const{return credential_&&identity_.id>credential_->peer.id?bridge::Device::B:bridge::Device::A;}
 std::string id_for(bridge::Device slot)const{if(slot==local_slot())return identity_.id;return credential_?credential_->peer.id:std::string();}
 bridge::Device slot_for(const std::string&id)const{if(id==identity_.id)return local_slot();if(credential_&&id==credential_->peer.id)return bridge::other(local_slot());throw std::runtime_error("Unknown room device");}
 bool is_coordinator()const{return settings_.primary==local_slot();}
 void establish_membership(){
  if(membership_ready_)return;
  // The two stable IDs determine slots identically, independent of OS, roles,
  // reconnect order and which side created the invitation.
  if(legacy_settings_&&saved_.contains("room")&&saved_.contains("settings")){
   bool windowsIsB=(o_.platform=="windows")==(local_slot()==bridge::Device::B);
   if(windowsIsB){std::swap(settings_.routes[0],settings_.routes[1]);settings_.primary=bridge::other(settings_.primary);settings_.microphoneSource=bridge::other(settings_.microphoneSource);settings_.balance=-settings_.balance;}
  }
  legacy_settings_=false;membership_ready_=true;
 }
 Json shared_state()const{return {{"schema",3},{"epoch",authority_epoch_},{"revision",settings_revision_},{"slots",Json::array({id_for(bridge::Device::A),id_for(bridge::Device::B)})},{"settings",settings_json(settings_)}};}
 bridge::Settings validate_shared(const Json&j)const{
  if(j.at("schema")!=3||j.at("slots")!=Json::array({id_for(bridge::Device::A),id_for(bridge::Device::B)})||!counter(j,"epoch")||!counter(j,"revision"))throw std::runtime_error("Invalid room state identity or revision");
  auto values=j.at("settings");bridge::Settings result;auto expected=settings_json(result);
  if(!values.is_object()||values.size()!=expected.size())throw std::runtime_error("Incomplete room settings");
  for(auto it=values.begin();it!=values.end();++it)if(!expected.contains(it.key())||!it.value().is_number()||!bridge::control::update(result,it.key(),it.value().get<float>()))throw std::runtime_error("Invalid room settings");
  return result;
 }
 void send_engine_state(){if(!engine_.running())return;auto body=bridge::control::snapshot(settings_);std::replace(body.begin(),body.end(),'\n','|');engine_.send("state "+std::to_string(authority_epoch_)+" "+std::to_string(settings_revision_)+" "+body+"\n");}
 void adopt_shared(const Json&j){if(j==shared_state())return;settings_=validate_shared(j);authority_epoch_=j.at("epoch");settings_revision_=j.at("revision");persist();send_engine_state();}
 void receive_shared(const Json&j){
  auto candidate=validate_shared(j);auto epoch=counter(j,"epoch"),revision=counter(j,"revision");
  if(epoch<authority_epoch_)return;
  if(epoch>authority_epoch_){
   bool prepared=!transfer_.empty()&&transfer_.value("next",Json::object())==j;
   if(id_for(candidate.primary)!=credential_->peer.id&&!prepared)throw std::runtime_error("Unexpected authority transfer");
   stop_local();wanted_=false;adopt_shared(j);enter_recovery();
  }else if(is_coordinator()&&revision>settings_revision_){
   if(candidate.primary!=settings_.primary||candidate.microphoneSource!=settings_.microphoneSource)throw std::runtime_error("Conflicting room roles");
   // A restored coordinator cache can lag a follower that durably observed its
   // last commit. Recover the newer state, muted, without silently resuming.
   auto recoveredRevision=next_counter(revision);stop_local();settings_=candidate;settings_.globalMute=true;settings_revision_=recoveredRevision;enter_recovery();snapshot();
  }else if(!is_coordinator()&&id_for(candidate.primary)==credential_->peer.id&&revision>=settings_revision_){
   // Ordinary snapshots cannot change either role; those require the handover.
   if(candidate.primary!=settings_.primary||candidate.microphoneSource!=settings_.microphoneSource)throw std::runtime_error("Role changed without handover");
   adopt_shared(j);
  }
 }
 void enter_recovery(){stop_local(true);authority_ready_=false;if(!recovery_required_){recovery_epoch_=authority_epoch_;recovery_revision_=settings_revision_;}recovery_required_=true;persist();}
 void require_epoch(const Json&q)const{if(!peer_compatible_||q.value("authority_epoch",uint64_t(0))!=authority_epoch_)throw std::runtime_error("Room authority changed; retry the action");}
 void validate_role_target(const Json&q)const{
  auto id=bounded(q,"device_id",64),role=bounded(q,"role",16);slot_for(id);
  if(role!="output"&&role!="input")throw std::runtime_error("Invalid audio role");
  const bool own=id==identity_.id;auto key=role=="output"?"headphones_output":"physical_microphone";
  if(own){if(!bridge::agent::available(audio_,routes_.at(key),role=="input"))throw std::runtime_error("Choose an available input/output on the selected computer first");}
  else{if(!room_->status().connected||peer_.value(key,std::string()).empty())throw std::runtime_error("Choose an available input/output on the selected computer first");}
 }
 void emit_handover(const Json&j){
#ifdef BRIDGE_AGENT_FAKE
  // Fault injection is compiled only into the isolated synthetic test agent.
  if(auto drop=std::getenv("BRIDGE_TEST_HANDOVER_DROP");drop&&j.value("step",std::string())==drop)return;
#endif
  send(j);
 }
 void begin_handover(const Json&q){
  if(!is_coordinator()||!peer_compatible_||!shared_ready_)throw std::runtime_error("Wait for the Output Hub to synchronize");
  if(!transfer_.empty()||recovery_required_)throw std::runtime_error("Another hub switch is pending");validate_role_target(q);
  auto next=settings_;auto target=slot_for(bounded(q,"device_id",64));auto role=bounded(q,"role",16);
  if((role=="output"?next.primary:next.microphoneSource)==target)return;
  if(role=="output")next.primary=target;else next.microphoneSource=target;next.globalMute=true;
  auto doc=shared_state();doc["epoch"]=next_counter(authority_epoch_);doc["revision"]=1;doc["settings"]=settings_json(next);
  transfer_={{"id",bridge::room::new_id()},{"phase","prepare"},{"next",doc},{"role",role},{"device_id",bounded(q,"device_id",64)},{"was_running",wanted_}};
  stop_local();persist();transfer_deadline_=Clock::now()+std::chrono::seconds(8);message_="Switching "+std::string(role=="output"?"Output":"Input")+" Hub…";
  emit_handover({{"type","handover"},{"step","prepare"},{"epoch",authority_epoch_},{"transaction",transfer_}});
 }
 void receive_handover(const Json&j){
  if(!peer_compatible_)throw std::runtime_error("Incompatible room control version");auto step=bounded(j,"step",16);
  auto transaction=j.at("transaction");auto id=bounded(transaction,"id",64);if(id.empty())throw std::runtime_error("Missing transfer identity");
  if(step=="prepare"){
   if(is_coordinator()||j.at("epoch")!=authority_epoch_||!transfer_.empty())throw std::runtime_error("Invalid handover authority");
   auto candidate=validate_shared(transaction.at("next"));if(transaction.at("next").at("epoch")!=next_counter(authority_epoch_)||transaction.at("next").at("revision")!=1||!candidate.globalMute)throw std::runtime_error("Invalid handover generation");
   validate_role_target(transaction);auto expected=settings_;auto role=bounded(transaction,"role",16);auto target=slot_for(bounded(transaction,"device_id",64));if(role=="output")expected.primary=target;else expected.microphoneSource=target;expected.globalMute=true;
   if(settings_json(expected)!=settings_json(candidate))throw std::runtime_error("Handover changed unrelated settings");
   transfer_=transaction;transfer_["phase"]="prepared";transfer_["was_running"]=wanted_;stop_local();persist();transfer_deadline_=Clock::now()+std::chrono::seconds(8);
   message_="Switching audio hub…";emit_handover({{"type","handover"},{"step","prepared"},{"transaction",transfer_}});return;
  }
  if(transfer_.empty()||transfer_.at("id")!=id||transfer_.at("next")!=transaction.at("next"))throw std::runtime_error("Stale handover message");
  auto phase=transfer_.at("phase").get<std::string>();
  if(step=="prepared"&&phase=="prepare"){
   transfer_["phase"]="commit";adopt_shared(transfer_.at("next"));persist();emit_handover({{"type","handover"},{"step","commit"},{"transaction",transfer_}});
  }else if(step=="commit"&&phase=="prepared"){
   transfer_["phase"]="committed";adopt_shared(transfer_.at("next"));persist();emit_handover({{"type","handover"},{"step","committed"},{"transaction",transfer_}});
  }else if(step=="committed"&&phase=="commit"){
   auto final=transfer_;transfer_=Json::object();persist();emit_handover({{"type","handover"},{"step","finalize"},{"transaction",final}});shared_ready_=true;mute_reason_="Audio role changed. Unmute when ready.";snapshot();
  }else if(step=="finalize"&&phase=="committed"){
   transfer_=Json::object();persist();shared_ready_=true;mute_reason_="Audio role changed. Unmute when ready.";snapshot();
  }else throw std::runtime_error("Unexpected handover step");
 }
 void resume_room(bool notify){
  if(!peer_compatible_||!shared_ready_||!room_->status().connected)throw std::runtime_error("Wait for both compatible computers to synchronize");
  if(!transfer_.empty())throw std::runtime_error("Wait for hub switching to finish");
  if(recovery_required_){recovery_required_=false;persist();}
  // Both sides independently require a muted startup, and the coordinator
  // publishes that state before either engine can deliver microphone audio.
  if(is_coordinator()&&notify){settings_.globalMute=true;settings_revision_=next_counter(settings_revision_);persist();snapshot();}
  wanted_=true;try{start_local();if(notify)remote_request({{"command","start"}});persist();}catch(...){wanted_=false;stop_local();persist();throw;}
 }
 void update_output(){
  if(!output_lease_||Clock::now()<output_check_)return;
  auto now=Clock::now();output_check_=now+std::chrono::seconds(1);
  auto remote=peer_.value("diagnostics",Json::object());
  auto local=local_slot();
  auto hub=settings_.primary==local?telemetry_:remote;
  bool ready=engine_.running()&&room_->status().connected&&now-last_telemetry_<std::chrono::seconds(3)
   &&telemetry_.value("connected",false)&&telemetry_.value("synchronized",false)
   &&telemetry_.value("playback_ready",false)&&remote.value("connected",false)&&remote.value("synchronized",false)
   &&remote.value("playback_ready",false)&&hub.value("headphones_ready",false);
  try {
   if(ready){output_unhealthy_={};output_lease_->acquire(bridge::agent::playback_output(audio_,routes_["playback_input"],o_.platform));output_warning_=output_lease_->warning();
    auto source=settings_.microphoneSource==local?telemetry_:remote;
    if(auto_microphone_&&source.value("microphone_ready",false)&&telemetry_.value("mic_sink_ready",false)&&remote.value("mic_sink_ready",false))input_lease_->acquire(bridge::agent::microphone_input(audio_,routes_["microphone_output"],o_.platform));
    else input_lease_->restore();
    if(!input_lease_->warning().empty())output_warning_=input_lease_->warning();}
   else {if(output_unhealthy_==Clock::time_point{})output_unhealthy_=now;if(now-output_unhealthy_>=std::chrono::seconds(3)){output_lease_->restore();input_lease_->restore();output_warning_=output_lease_->warning();if(!input_lease_->warning().empty())output_warning_=input_lease_->warning();}}
  }catch(const std::exception&e){output_warning_=e.what();}
 }
 std::string secret_id()const{return "room-"+credential_->room_id;}
 std::string audio_id()const{return "audio-"+credential_->room_id;}
 void persist(){saved_["version"]=3;saved_["authority_epoch"]=authority_epoch_;saved_["settings_revision"]=settings_revision_;saved_["transfer"]=transfer_;saved_["recovery_required"]=recovery_required_;saved_["recovery_epoch"]=recovery_epoch_;saved_["recovery_revision"]=recovery_revision_;saved_["migration_pending"]=migration_pending_;saved_["name_automatic"]=name_automatic_;saved_["kind_override"]=kind_override_;saved_["device_id"]=identity_.id;saved_["name"]=identity_.name;saved_["platform"]=identity_.platform;saved_["routes"]=routes_;saved_["wanted_running"]=wanted_;saved_["auto_microphone"]=auto_microphone_;saved_["settings"]=settings_json(settings_);if(credential_){auto&c=*credential_;saved_["room"]={{"id",c.room_id},{"peer_id",c.peer.id},{"peer_name",c.peer.name},{"peer_platform",c.peer.platform},{"address",c.last_address},{"port",c.peer_port}};}else saved_.erase("room");bridge::agent::atomic_write(o_.directory/"state.json",saved_.dump(2)+"\n");}
 void refresh(){audio_=bridge::enumerate_audio_devices();bridge::agent::auto_routes(routes_,audio_,o_.platform);refresh_=Clock::now()+std::chrono::seconds(5);++inventory_revision_;}
 Json local_info(){Json j={{"id",identity_.id},{"name",identity_.name},{"platform",o_.platform},{"kind",kind_override_=="auto"?profile_.kind:kind_override_},{"detected_kind",profile_.kind},{"kind_override",kind_override_},{"model",profile_.model},{"app_playback_output",bridge::agent::playback_output(audio_,routes_["playback_input"],o_.platform)},{"app_microphone_input",bridge::agent::microphone_input(audio_,routes_["microphone_output"],o_.platform)},{"slot",unsigned(local_slot())},{"online",true},{"audio_devices",bridge::agent::audio_json(audio_)},{"setup_status",bridge::agent::setup_status(routes_,audio_,o_.platform)},{"capture_authorized",bridge::agent::capture_authorized()},{"output_routing_warning",output_warning_},{"media_port",o_.media_port},{"inventory_revision",inventory_revision_},{"diagnostics",telemetry_}};for(auto&[k,v]:routes_)j[k]=v;return j;}
 void send(const Json&j){if(!room_->send(j.dump()))throw std::runtime_error("Other device is unavailable; change was not sent");}
 void snapshot(){int protocol=3;
#ifdef BRIDGE_AGENT_FAKE
  if(auto version=std::getenv("BRIDGE_TEST_PROTOCOL_VERSION"))protocol=std::stoi(version);
#endif
  if(room_->status().connected){Json j={{"type","snapshot"},{"protocol_version",protocol},{"shared",shared_state()},{"transfer_pending",!transfer_.empty()||recovery_required_},{"recovery_required",recovery_required_},{"recovery_epoch",recovery_epoch_},{"recovery_revision",recovery_revision_},{"migration_pending",migration_pending_},{"device",local_info()},{"settings",settings_json(settings_)},{"running",engine_.running()},{"diagnostics",telemetry_},{"mute_reason",mute_reason_}};room_->send(j.dump());}advertise_=Clock::now()+std::chrono::seconds(1);}
 void stop_local(bool forget_intent=false){if(input_lease_)input_lease_->restore();if(output_lease_){output_lease_->restore();output_warning_=output_lease_->warning();}engine_.stop();telemetry_=Json::object();engine_synced_=false;stdout_.clear();if(forget_intent)wanted_=false;}
 void start_local(){
  if(engine_.running())return;if(!credential_||!room_->status().connected||peer_.empty())throw std::runtime_error("Wait for both room devices to connect");
  if(!peer_compatible_||!shared_ready_)throw std::runtime_error("Update San Audisco on both computers and wait for room settings to synchronize");
  if(!transfer_.empty()||recovery_required_)throw std::runtime_error("Hub switch interrupted. Wait for both devices to reconcile, then press Start.");
  refresh();bridge::agent::validate_independent(audio_,routes_);auto setup=bridge::agent::setup_status(routes_,audio_,o_.platform);if(setup!="Ready")throw std::runtime_error(setup);
  if(!bridge::agent::capture_authorized())throw std::runtime_error("Grant San Audisco microphone access on this Mac, then press Start there once");
  auto local=local_slot();
  if(settings_.primary==local&&!bridge::agent::available(audio_,routes_["headphones_output"],false))throw std::runtime_error("Select available headphones on the Hub");
  if(settings_.microphoneSource==local&&!bridge::agent::available(audio_,routes_["physical_microphone"],true))throw std::runtime_error("Select an available physical microphone on the microphone source");
  if(output_lease_)output_lease_->begin();if(input_lease_)input_lease_->begin();output_check_={};output_unhealthy_={};
  auto media=peer_.value("media_port",45670);if(media<1||media>65535)throw std::runtime_error("Invalid peer media port");auto address=room_->status().peer_address;in_addr ip{};if(inet_pton(AF_INET,address.c_str(),&ip)!=1)throw std::runtime_error("Authenticated peer address unavailable");
  std::ostringstream config;config<<"slot="<<unsigned(local_slot())<<"\npeer="<<address<<"\nport="<<o_.media_port<<"\npeer_port="<<media<<"\nkey_stdin=1\n";for(auto key:{"playback_input","microphone_output","headphones_output","physical_microphone"})config<<key<<'='<<routes_[key]<<'\n';
  bridge::agent::atomic_write(o_.directory/"engine.conf",config.str());engine_.start(o_.engine,{"run",utf8(o_.directory/"engine.conf")});auto media_key=bridge::room::audio_key(*credential_);auto key_frame=bridge::net::key_to_hex(media_key)+"\n";sodium_memzero(media_key.data(),media_key.size());try{engine_.send(key_frame);}catch(...){sodium_memzero(key_frame.data(),key_frame.size());stop_local();throw;}sodium_memzero(key_frame.data(),key_frame.size());engine_synced_=false;stdout_.clear();telemetry_=Json::object();send_engine_state();mute_reason_="Audio started or reconnected. Unmute when ready.";message_="Starting audio on both devices…";retry_=Clock::now()+std::chrono::seconds(3);
 }
 void apply_control(const Json&q){
  if(!is_coordinator()||!authority_ready_)throw std::runtime_error("Wait for the Output Hub to synchronize");
  if(!transfer_.empty()||recovery_required_)throw std::runtime_error("Wait for the hub switch to finish");
  std::string key=bounded(q,"key",40);auto id=bounded(q,"device_id",64);
  if(!id.empty()){auto slot=slot_for(id);if(key!="volume"&&key!="mute"&&key!="deafen"&&key!="silence")throw std::runtime_error("Invalid device control");key="slot"+std::to_string(unsigned(slot))+"_"+key;}
  if(key=="primary"||key=="mic_source")throw std::runtime_error("Use the device-ID role selector");
  float value=q.at("value").get<float>();auto next=settings_;if(!bridge::control::update(next,key,value))throw std::runtime_error("Invalid mixer setting");
  auto nextRevision=next_counter(settings_revision_);settings_=next;settings_revision_=nextRevision;persist();send_engine_state();snapshot();
  if(key=="mute")mute_reason_=value?"Microphone muted from the room controls.":"";
  message_="Room settings applied by the Output Hub.";
 }
 void configure_local(const Json&q){auto next=routes_;std::string name=q.contains("name")?bounded(q,"name",64):identity_.name;if(name.empty())throw std::runtime_error("Device name cannot be empty");refresh();for(auto key:{"playback_input","microphone_output","headphones_output","physical_microphone"})if(q.contains(key)){auto value=bounded(q,key);bridge::agent::validate_route(audio_,key,value);next[key]=value;}
  bridge::agent::validate_independent(audio_,next);bool changed=next!=routes_;if(changed)stop_local();routes_=std::move(next);identity_.name=name;if(q.contains("name"))name_automatic_=false;room_->set_local_name(name);++inventory_revision_;persist();if(changed&&wanted_){try{start_local();}catch(const std::exception&e){message_=e.what();}}snapshot();
 }
 void autoconfigure_local(){
  refresh();std::map<int,std::string> defaults;try{defaults=output_backend_->defaults();}catch(const std::exception&){}
  auto next=bridge::agent::recommended_routes(routes_,audio_,o_.platform,defaults);
  auto local=local_slot();
  if(settings_.primary==local&&next["headphones_output"].empty())throw std::runtime_error("Select your headphones, then press Autoconfigure again.");
  if(settings_.microphoneSource==local&&next["physical_microphone"].empty())throw std::runtime_error("Select your microphone, then press Autoconfigure again.");
  auto_microphone_=true;Json q=next;configure_local(q);if(output_lease_)output_lease_->begin();if(input_lease_)input_lease_->begin();output_check_={};
  message_="Audio routes configured. Start audio to send system playback to the Output Hub.";
 }
 void remote(const Json&j){auto type=bounded(j,"type",32);
  if(type=="snapshot"){
   auto d=j.at("device");if(d.at("id")!=credential_->peer.id||d.at("platform")!=credential_->peer.platform)throw std::runtime_error("Peer identity changed");
   validate_inventory(d);auto diagnostics=j.value("diagnostics",Json::object());validate_diagnostics(diagnostics);
   if(j.contains("running")&&!j.at("running").is_boolean())throw std::runtime_error("Invalid running state");
   const bool compatible=j.contains("protocol_version")&&j.at("protocol_version")==3;
   if(compatible){validate_shared(j.at("shared"));for(auto key:{"recovery_required","migration_pending","transfer_pending"})if(j.contains(key)&&!j.at(key).is_boolean())throw std::runtime_error("Invalid room status flag");if(j.contains("recovery_epoch"))counter(j,"recovery_epoch",true);if(j.contains("recovery_revision"))counter(j,"recovery_revision",true);}
   peer_=d;peer_["online"]=true;peer_["running"]=j.value("running",false);peer_["diagnostics"]=diagnostics;peer_compatible_=compatible;
   if(!peer_compatible_){shared_ready_=false;stop_local();message_="Update San Audisco on the other computer to use this room.";return;}
   if(migration_pending_){
    if(o_.platform=="mac"&&credential_->peer.platform=="windows"){adopt_shared(j.at("shared"));migration_pending_=false;persist();snapshot();}
    else if(o_.platform=="windows"&&shared_state()==j.at("shared")){migration_pending_=false;persist();snapshot();}
   }else receive_shared(j.at("shared"));
   auto re=j.value("recovery_epoch",uint64_t(0)),rr=j.value("recovery_revision",uint64_t(0));
   if(j.value("recovery_required",false)&&!recovery_required_&&(re>authority_epoch_||(re==authority_epoch_&&rr>=settings_revision_)))enter_recovery();
   if(recovery_required_&&shared_state()==j.at("shared")){transfer_=Json::object();persist();message_="Hub switch interrupted. Settings reconciled; press Start to resume.";}
   shared_ready_=!migration_pending_&&!j.value("migration_pending",false)&&shared_state()==j.at("shared");if(shared_ready_)authority_ready_=true;
  }else if(type=="handover"){receive_handover(j);
  }else if(type=="request"){
   auto seq=counter(j,"sequence");if(seq<=peer_sequence_)return;peer_sequence_=seq;auto q=j.at("body");auto command=bounded(q,"command",32);Json result={{"type","result"},{"sequence",seq},{"ok",true}};
   try{if(command=="configure")configure_local(q);else if(command=="autoconfigure")autoconfigure_local();else if(command=="start"){require_epoch(q);resume_room(is_coordinator());}else if(command=="stop"){stop_local(true);message_="Audio stopped on this computer.";persist();}else if(command=="test_tone"){engine_.send("test_tone\n");}else if(command=="control"){require_epoch(q);apply_control(q);}else if(command=="set_role"){require_epoch(q);begin_handover(q);}else if(command=="protect_mic"){if(is_coordinator()){settings_.globalMute=true;settings_revision_=next_counter(settings_revision_);persist();send_engine_state();snapshot();}}else throw std::runtime_error("Unsupported remote command");}
   catch(const std::exception&e){result["ok"]=false;result["error"]=e.what();}room_->send(result.dump());snapshot();
  }else if(type=="result"){
   auto seq=std::to_string(j.at("sequence").get<uint64_t>());pending_config_.erase(seq);if(!j.value("ok",false))message_="Other device: "+j.value("error",std::string("request failed"));else message_="Room change acknowledged.";
  }
 }
 void remote_request(Json q){q["authority_epoch"]=authority_epoch_;auto seq=++sequence_;send({{"type","request"},{"sequence",seq},{"body",q}});pending_config_[std::to_string(seq)]=Clock::now()+std::chrono::seconds(8);}
 bool try_join(const Json&q){auto address=bounded(q,"address",128),id=bounded(q,"room_id",64);std::vector<bridge::room::Nearby> candidates;for(auto&n:room_->nearby())if(n.accepting&&(id.empty()||id==n.room_id)&&(address.empty()||address==n.address||address==n.address+":"+std::to_string(n.port)))candidates.push_back(n);
  if(candidates.size()>1)throw std::runtime_error("Several rooms are nearby. Select the room to join.");if(candidates.empty())return false;
  room_->join(candidates.front(),bounded(q,"code",16));message_="Pairing securely…";return true;
 }
 void handle_telemetry(){stdout_+=engine_.read_output();if(stdout_.size()>131072){stdout_.clear();throw std::runtime_error("Audio engine output exceeded limit");}for(size_t end;(end=stdout_.find('\n'))!=stdout_.npos;){auto line=stdout_.substr(0,end);stdout_.erase(0,end+1);try{auto j=Json::parse(line);telemetry_=j;last_telemetry_=Clock::now();bool ready=j.value("connected",false)&&j.value("synchronized",false);
    if(ready&&!engine_synced_){engine_synced_=true;message_="Audio bridge active. Unmute the microphone when ready.";}
    if(j.contains("settings")&&!settings_.globalMute){auto actual=settings_;bridge::control::parse(actual,j.at("settings").get<std::string>());if(actual.globalMute&&j.value("settings_revision",uint64_t(0))==settings_revision_){if(is_coordinator()){settings_.globalMute=true;settings_revision_=next_counter(settings_revision_);persist();send_engine_state();snapshot();}else if(room_->status().connected)remote_request({{"command","protect_mic"}});}}
    if(j.value("muted",true)&&mute_reason_.empty())mute_reason_="Microphone is muted after reconnect, route change, or unavailable audio device. Check readiness, then unmute.";
    if(!j.value("muted",true))mute_reason_.clear();
   }catch(const Json::exception&){} }
   auto errors=engine_.read_errors();if(!errors.empty())message_=errors.substr(0,1200);
 }
public:
 bool quitting=false;
 explicit Agent(Options o):o_(std::move(o)){saved_=read_json(o_.directory/"state.json");legacy_settings_=saved_.value("version",2)<3;migration_pending_=saved_.value("migration_pending",false)||(legacy_settings_&&saved_.contains("room"));
  if(!saved_.empty()&&legacy_settings_&&!fs::exists(o_.directory/"state-v2.backup.json"))bridge::agent::atomic_write(o_.directory/"state-v2.backup.json",saved_.dump(2)+"\n");
  profile_=bridge::device::detect_device_profile();name_automatic_=saved_.value("name_automatic",!saved_.contains("name"));kind_override_=saved_.value("kind_override",std::string("auto"));
  identity_={saved_.value("device_id",bridge::room::new_id()),name_automatic_?profile_.default_name:saved_.value("name",profile_.default_name),o_.platform};
  authority_epoch_=saved_.contains("authority_epoch")?counter(saved_,"authority_epoch"):1;settings_revision_=saved_.contains("settings_revision")?counter(saved_,"settings_revision"):1;transfer_=saved_.value("transfer",Json::object());recovery_required_=saved_.value("recovery_required",false)||!transfer_.empty();recovery_epoch_=saved_.contains("recovery_epoch")?counter(saved_,"recovery_epoch",true):authority_epoch_;recovery_revision_=saved_.contains("recovery_revision")?counter(saved_,"recovery_revision",true):settings_revision_;
if(saved_.contains("routes"))routes_=saved_["routes"].get<std::map<std::string,std::string>>();else{
    auto legacy=o_.directory.parent_path()/(o_.platform=="mac"?"mac.conf":"windows.conf");if(fs::exists(legacy)){std::ifstream in(legacy);std::string line;while(std::getline(in,line)){auto p=line.find('=');if(p==line.npos)continue;auto k=line.substr(0,p),v=line.substr(p+1);if(!v.empty()&&v.back()=='\r')v.pop_back();if(k=="playback_input"||k=="microphone_output"||k=="headphones_output"||k=="physical_microphone")routes_[k]=v;}if(!fs::exists(o_.directory/"legacy-config.backup"))fs::copy_file(legacy,o_.directory/"legacy-config.backup");}}
#ifdef BRIDGE_AGENT_FAKE
  if(routes_.empty()){std::string p=o_.platform=="mac"?"mac-":"win-";routes_={{"playback_input",p+"playback"},{"microphone_output",p+"mic-sink"},{"headphones_output",p+"headphones"},{"physical_microphone",p+"mic"}};}
#endif
  if(saved_.contains("settings")){auto values=saved_["settings"];if(legacy_settings_){Json converted=Json::object();for(auto it=values.begin();it!=values.end();++it){auto k=it.key();if(k.starts_with("win_"))k="slot0_"+k.substr(4);if(k.starts_with("mac_"))k="slot1_"+k.substr(4);converted[k]=it.value();}values=converted;}settings_=parse_settings(values);}settings_.globalMute=true;wanted_=saved_.value("wanted_running",false);refresh();bridge::room::Config c;c.local=identity_;c.bind_address=o_.bind;c.port=o_.room_port;c.discovery_port=o_.discovery_port;if(o_.bind=="127.0.0.1")c.discovery_address="127.0.0.1";room_=std::make_unique<bridge::room::Service>(c);
  output_backend_=bridge::routing::system_backend();
  output_lease_=std::make_unique<bridge::routing::Lease>(*output_backend_,[this](const Json&j){bridge::agent::atomic_write(o_.directory/"output-restore.json",j.dump(2)+"\n");},read_json(o_.directory/"output-restore.json"));
  output_lease_->restore();output_warning_=output_lease_->warning();
  input_backend_=bridge::routing::system_backend(true);
  input_lease_=std::make_unique<bridge::routing::Lease>(*input_backend_,[this](const Json&j){bridge::agent::atomic_write(o_.directory/"input-restore.json",j.dump(2)+"\n");},read_json(o_.directory/"input-restore.json"));
  input_lease_->restore();auto_microphone_=saved_.value("auto_microphone",false);
  if(saved_.contains("room")){try{auto r=saved_["room"];bridge::room::Credential key;key.room_id=r.at("id");key.peer={r.at("peer_id"),r.at("peer_name"),r.at("peer_platform")};key.last_address=r.at("address");key.peer_port=r.at("port");
#ifdef BRIDGE_AGENT_FAKE
   if(auto fail=std::getenv("BRIDGE_TEST_CREDENTIAL_FAILURE");fail&&std::string(fail)=="1")throw std::runtime_error("Synthetic credential access failure");
#endif
   key.secret=bridge::credentials::load_key("room-"+key.room_id);credential_=key;room_->restore(key);sodium_memzero(key.secret.data(),key.secret.size());message_="Reconnecting to remembered room…";}catch(const std::exception&e){message_=std::string("Saved room could not be restored: ")+e.what();wanted_=false;if(credential_)sodium_memzero(credential_->secret.data(),credential_->secret.size());credential_.reset();throw std::runtime_error(message_+". Saved room retained; grant credential access and reconnect the agent.");}}
  if(credential_){establish_membership();if(is_coordinator()){settings_revision_=next_counter(settings_revision_);if(recovery_required_){recovery_epoch_=authority_epoch_;recovery_revision_=settings_revision_;}}}if(recovery_required_)wanted_=false;

  persist();
 }
 ~Agent(){stop_local();if(credential_)sodium_memzero(credential_->secret.data(),credential_->secret.size());}
 Json status(){auto rs=room_->status();Json ds=Json::array();ds.push_back(local_info());if(!peer_.empty()){auto p=peer_;p["online"]=rs.connected;ds.push_back(p);}else if(credential_)ds.push_back({{"id",credential_->peer.id},{"name",credential_->peer.name},{"platform",credential_->peer.platform},{"online",false},{"audio_devices",Json::array()},{"setup_status","Offline"}});
  for(auto& d:ds){auto slot=slot_for(d.at("id").get<std::string>());d["slot"]=unsigned(slot);auto&r=settings_.routes[unsigned(slot)];d["mix"]={{"volume",r.volume},{"mic_muted",r.micMuted},{"deafened",r.deafened},{"silenced",r.silenced}};}
  auto role=[&](bridge::Device d){return id_for(d);};Json nearby=Json::array();for(auto&n:room_->nearby())if(n.accepting)nearby.push_back({{"id",n.room_id},{"name",n.device.name},{"address",n.address+":"+std::to_string(n.port)}});auto diagnostics=telemetry_;diagnostics["message"]=bridge::agent::setup_status(routes_,audio_,o_.platform);diagnostics["permission_granted"]=bridge::agent::capture_authorized();diagnostics["output_routing_active"]=output_lease_&&output_lease_->active();diagnostics["output_routing_warning"]=output_warning_;diagnostics["input_routing_active"]=input_lease_&&input_lease_->active();if(!output_warning_.empty())diagnostics["message"]=output_warning_;
  auto displayed=rs.error.empty()?message_:rs.error;if(rs.error.empty()){if(!output_warning_.empty())displayed=output_warning_;else if(!peer_.value("output_routing_warning",std::string()).empty())displayed=peer_.value("name",std::string("Other device"))+": "+peer_.value("output_routing_warning",std::string());}
  return {{"device_id",identity_.id},{"name",identity_.name},{"room_id",rs.room_id},{"room_port",room_->port()},{"invite_code",rs.invitation_code},{"invite_expires_seconds",rs.invitation_seconds},{"paired",credential_.has_value()},{"connected",rs.connected},{"running",engine_.running()},{"wanted_running",wanted_},{"status",displayed},{"phase",rs.phase},{"mute_reason",mute_reason_},{"hub_device_id",role(settings_.primary)},{"mic_device_id",role(settings_.microphoneSource)},{"devices",ds},{"settings",settings_json(settings_)},{"discovered",nearby},{"diagnostics",diagnostics},{"pending_changes",pending_config_.size()+(transfer_.empty()?0:1)},{"protocol_version",3},{"controls_ready",authority_ready_&&peer_compatible_&&rs.connected&&transfer_.empty()&&!recovery_required_},{"compatible",peer_compatible_},{"shared_ready",shared_ready_},{"authority_epoch",authority_epoch_},{"settings_revision",settings_revision_},{"handover_active",!transfer_.empty()},{"recovery_required",recovery_required_}};
 }
 Json command(const Json&q){auto cmd=bounded(q,"command",32);
  if(cmd=="status"){}
#ifdef BRIDGE_AGENT_FAKE
  else if(cmd=="test_peer_message"){send(q.at("message"));}
#endif
  else if(cmd=="discover"){room_->discover();message_="Looking for nearby rooms…";}
  else if(cmd=="refresh"||cmd=="diagnose"){refresh();persist();snapshot();message_=bridge::agent::setup_status(routes_,audio_,o_.platform);}
  else if(cmd=="create"){if(q.contains("name"))configure_local(q);room_->create_invitation();message_="Enter this code on your other computer. It expires in five minutes.";}
  else if(cmd=="cancel_invite"){room_->cancel_invitation();message_="Invitation cancelled.";}
  else if(cmd=="join"){
    bridge::room::normalize_code(bounded(q,"code",16));pending_join_.reset();if(!try_join(q)){auto address=bounded(q,"address",128);if(address.empty())room_->discover();else{auto sep=address.find(':');uint16_t port=45671;if(sep!=address.npos){auto text=address.substr(sep+1);size_t n=0;auto p=std::stoul(text,&n);if(n!=text.size()||!p||p>65535)throw std::runtime_error("Invalid room address port");port=uint16_t(p);address.resize(sep);}room_->discover_at(address,port);}pending_join_=q;join_deadline_=Clock::now()+std::chrono::seconds(6);message_="Finding the room before pairing…";}
  }
  else if(cmd=="configure"){auto id=bounded(q,"device_id",64);if(id.empty()||id==identity_.id)configure_local(q);else if(credential_&&id==credential_->peer.id){remote_request(q);message_="Waiting for the other device to apply setup…";}else throw std::runtime_error("Unknown room device");}
  else if(cmd=="autoconfigure"){autoconfigure_local();if(room_->status().connected){remote_request({{"command","autoconfigure"}});message_="Configuring audio routes on both computers…";}else message_="This computer is configured. Connect the other computer and run Autoconfigure again.";}
  else if(cmd=="control"||cmd=="set_role"){if(!room_->status().connected||!authority_ready_||!peer_compatible_)throw std::runtime_error("Wait for both compatible devices to synchronize");if(is_coordinator()){if(cmd=="control")apply_control(q);else begin_handover(q);}else{remote_request(q);message_="Waiting for Output Hub acknowledgement…";}}
  else if(cmd=="device_profile"){auto kind=bounded(q,"kind",16);if(kind!="auto"&&kind!="laptop"&&kind!="desktop"&&kind!="server")throw std::runtime_error("Invalid device type");kind_override_=kind;++inventory_revision_;persist();snapshot();}
  else if(cmd=="test_tone"){auto id=bounded(q,"device_id",64);if(id.empty()||id==identity_.id)engine_.send("test_tone\n");else if(credential_&&id==credential_->peer.id)remote_request(q);else throw std::runtime_error("Unknown room device");message_="Playing a quiet two-second test tone through the Hub headphones.";}
  else if(cmd=="start"){if(!peer_compatible_||!shared_ready_||!room_->status().connected)throw std::runtime_error("Update San Audisco on both computers and wait for synchronized room settings");if(is_coordinator())resume_room(true);else{remote_request({{"command","start"}});message_="Waiting for the Output Hub to start audio…";}}
  else if(cmd=="stop"){if(room_->status().connected)remote_request({{"command","stop"}});stop_local(true);message_="Audio stopped on this device; stop sent to the room.";persist();}
  else if(cmd=="forget"){stop_local(true);pending_join_.reset();if(credential_){bridge::credentials::delete_key(secret_id());bridge::credentials::delete_key(audio_id());sodium_memzero(credential_->secret.data(),credential_->secret.size());credential_.reset();}room_->forget();peer_=Json::object();pending_config_.clear();transfer_=Json::object();recovery_required_=false;membership_ready_=false;peer_compatible_=shared_ready_=authority_ready_=false;authority_epoch_=settings_revision_=1;settings_=bridge::Settings{};settings_.globalMute=true;message_="Room forgotten on this device.";persist();}
  else if(cmd=="quit"){stop_local();quitting=true;message_="San Audisco stopped on this computer.";}
  else throw std::runtime_error("Unknown agent command");return status();
 }
 void poll(){room_->poll();if(pending_join_){try{if(try_join(*pending_join_))pending_join_.reset();else if(Clock::now()>join_deadline_){pending_join_.reset();message_="Room not found. Check both devices are on the same network and allow San Audisco through the private-network firewall.";}}catch(const std::exception&e){pending_join_.reset();message_=e.what();}}while(auto key=room_->take_credential()){
   try{bridge::credentials::store_key("room-"+key->room_id,key->secret);credential_=*key;establish_membership();persist();message_="Room paired. Choose the Hub, headphones, and microphone.";}catch(const std::exception&e){stop_local(true);room_->forget();if(credential_)sodium_memzero(credential_->secret.data(),credential_->secret.size());credential_.reset();message_=std::string("Could not save pairing: ")+e.what();persist();}sodium_memzero(key->secret.data(),key->secret.size());
  }
  auto rs=room_->status();if(rs.generation!=room_generation_){room_generation_=rs.generation;peer_sequence_=0;pending_config_.clear();peer_compatible_=shared_ready_=authority_ready_=false;}
  if(rs.connected&&!was_connected_){snapshot();refresh_=Clock::now();}if(!rs.connected&&was_connected_){if(!transfer_.empty())enter_recovery();stop_local();if(is_coordinator()){settings_.globalMute=true;settings_revision_=next_counter(settings_revision_);if(recovery_required_){recovery_epoch_=authority_epoch_;recovery_revision_=settings_revision_;}}persist();mute_reason_="Connection interrupted. Microphone will remain muted after reconnection.";message_="Reconnecting…";}was_connected_=rs.connected;
  for(int i=0;i<32;++i){auto s=room_->receive();if(!s)break;try{remote(Json::parse(*s));}catch(const std::exception&e){message_=std::string("Rejected room update: ")+e.what();}}
  if(Clock::now()>=refresh_){refresh();}if(Clock::now()>=advertise_)snapshot();
  if(wanted_&&rs.connected&&peer_compatible_&&shared_ready_&&transfer_.empty()&&!recovery_required_&&!peer_.empty()&&!engine_.running()&&Clock::now()>=retry_){retry_=Clock::now()+std::chrono::seconds(5);try{start_local();}catch(const std::exception&e){message_=e.what();}}
  if(!transfer_.empty()&&!recovery_required_&&Clock::now()>transfer_deadline_){enter_recovery();message_="Hub switch interrupted. Wait for both devices, then press Start.";snapshot();}
  handle_telemetry();update_output();for(auto it=pending_config_.begin();it!=pending_config_.end();){if(Clock::now()>it->second){message_="Other device did not acknowledge the change. Check its connection and retry.";it=pending_config_.erase(it);}else ++it;}
 }
};
struct Client {Socket socket;std::string input,output;size_t sent=0;Clock::time_point deadline=Clock::now()+std::chrono::seconds(2);};
void serve(const Options&o){bridge::agent::private_directory(o.directory);bridge::agent::InstanceLock lock(o.directory);Agent agent(o);OwnedSocket server(::socket(AF_INET,SOCK_STREAM,IPPROTO_TCP));if(server.s==invalid)throw std::runtime_error("Cannot listen for local controls");nonblock(server.s);sockaddr_in addr{};addr.sin_family=AF_INET;addr.sin_addr.s_addr=htonl(INADDR_LOOPBACK);addr.sin_port=0;if(bind(server.s,reinterpret_cast<sockaddr*>(&addr),sizeof addr)||listen(server.s,8))throw std::runtime_error("Cannot listen for local controls");
#ifdef _WIN32
 int len=sizeof addr;
#else
 socklen_t len=sizeof addr;
#endif
 getsockname(server.s,reinterpret_cast<sockaddr*>(&addr),&len);auto key=bridge::net::random_key();auto token=bridge::net::key_to_hex(key);sodium_memzero(key.data(),key.size());bridge::agent::atomic_write(o.directory/"endpoint.json",Json{{"port",ntohs(addr.sin_port)},{"token",token},{"version",3}}.dump());std::vector<Client> clients;
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
 if(cmd=="ensure"){std::optional<Json> existing;try{existing=request(o,{{"command","status"}});}catch(...){}
  if(existing&&existing->value("ok",false)){if(existing->at("state").value("protocol_version",0)!=3)throw std::runtime_error("An older local San Audisco agent is still running. Quit San Audisco completely, then reopen it to finish the update.");std::cout<<"{\"ok\":true}\n";return 0;}
  bridge::agent::private_directory(o.directory);std::vector<std::string> args{"serve"};args.insert(args.end(),o.forwarded.begin(),o.forwarded.end());bridge::agent::detach(bridge::agent::executable_path(),args);auto end=Clock::now()+std::chrono::seconds(6);while(Clock::now()<end){std::this_thread::sleep_for(std::chrono::milliseconds(100));try{auto r=request(o,{{"command","status"}});if(r.value("ok",false)){std::cout<<"{\"ok\":true}\n";return 0;}}catch(...){}}throw std::runtime_error("Background agent could not start. Check whether the room port is already in use.");}
 if(cmd=="request"){std::string input;char c;while(std::cin.get(c)&&c!='\n'){input+=c;if(input.size()>60000)throw std::runtime_error("Request too large");}auto q=Json::parse(input);Json response;try{response=request(o,q);}catch(...){if(q.value("command",std::string())!="quit")throw;bridge::agent::private_directory(o.directory);bridge::agent::InstanceLock absent(o.directory);response={{"ok",true},{"state",{{"running",false},{"status","San Audisco is already stopped on this computer."}}}};}std::cout<<response.dump()<<'\n';return response.value("ok",false)?0:1;}
 throw std::runtime_error("Unknown command");
 }catch(const std::exception&e){std::cout<<Json{{"ok",false},{"error",e.what()}}.dump()<<'\n';return 1;}}
