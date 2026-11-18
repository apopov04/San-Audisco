#include "bridge/room.hpp"
#include <chrono>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <memory>
using namespace bridge::room;
using Clock=std::chrono::steady_clock;
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
Config config(const std::string& platform,unsigned lifetime=300){Config c;c.local={new_id(),platform=="mac"?"MacBook Air":"Gaming PC",platform};c.bind_address="127.0.0.1";c.port=0;c.discovery_address="127.0.0.1";c.discovery_port=1;c.invitation_lifetime_seconds=lifetime;return c;}
void pump(Service& a,Service& b,const std::function<bool()>& condition,int ms=2500){auto end=Clock::now()+std::chrono::milliseconds(ms);do{a.poll();b.poll();if(condition())return;std::this_thread::sleep_for(std::chrono::milliseconds(2));}while(Clock::now()<end);throw std::runtime_error("Room test timed out: "+a.status().phase+"/"+a.status().error+" | "+b.status().phase+"/"+b.status().error);}
void settle(Service& a,Service& b,int ms=100){auto end=Clock::now()+std::chrono::milliseconds(ms);while(Clock::now()<end){a.poll();b.poll();std::this_thread::sleep_for(std::chrono::milliseconds(2));}}
Nearby target(Service& s,const Config& c){return Nearby{s.status().room_id,c.local,"127.0.0.1",s.port(),true};}
int main(){try{
 require(normalize_code("k7mp-4x9r")=="K7MP4X9R","Case-insensitive code formatting");
 for(auto code:{"12345678","ABCDEFGI","ABCD","ABCDEFGHO","ABCD\nEFGH"}){bool failed=false;try{normalize_code(code);}catch(...){failed=true;}require(failed,"Invalid code accepted");}
 std::cout<<"PASS code validation\n";
 auto ac=config("windows"),bc=config("mac");Service a(ac);bc.discovery_port=a.port();Service b(bc);
 auto code=a.create_invitation();require(code.size()==9&&normalize_code(code).size()==8,"Generated code width");
 b.discover();pump(a,b,[&]{return !b.nearby().empty();});auto offers=b.nearby();require(offers.size()==1&&offers[0].accepting&&offers[0].device.id==ac.local.id,"LAN room discovery");
 b.join(offers[0],code);pump(a,b,[&]{return a.status().connected&&b.status().connected;});
 auto ak=a.take_credential(),bk=b.take_credential();require(ak&&bk&&ak->secret==bk->secret,"PAKE shared credentials differ");require(audio_key(*ak)==audio_key(*bk)&&audio_key(*ak)!=ak->secret,"Audio domain separation");require(a.status().invitation_code.empty()&&a.status().invitation_seconds==0,"Successful invitation not consumed");require(a.status().peer.id==bc.local.id&&b.status().peer.id==ac.local.id,"Pairing identity binding");
 std::cout<<"PASS discovery and confirmed SPAKE2 pairing\n";
 require(a.send("{\"op\":\"volume\",\"value\":0.5}"),"Encrypted send failed");std::optional<std::string> got;pump(a,b,[&]{got=b.receive();return got.has_value();});require(*got=="{\"op\":\"volume\",\"value\":0.5}","Encrypted control payload mismatch");
 require(b.send(std::string(65536,'x')),"Maximum control payload rejected");pump(a,b,[&]{got=a.receive();return got.has_value();});require(got->size()==65536,"Maximum payload truncated");require(!a.send(std::string(65537,'x')),"Oversized payload accepted");
 for(int i=0;i<100;++i)require(a.send("burst-"+std::to_string(i)),"Burst send failed");int burst=0;pump(a,b,[&]{while(auto item=b.receive()){require(*item=="burst-"+std::to_string(burst++),"Control burst ordering");}return burst==100;},700);
 std::cout<<"PASS bounded bidirectional encrypted controls and buffered burst drain\n";
 bool refused=false;try{a.create_invitation();}catch(...){refused=true;}require(refused,"Paired room accepted another invitation");
 auto cc=config("mac");Service intruder(cc);intruder.join(target(a,ac),code);settle(a,intruder,150);require(!intruder.status().connected&&!intruder.take_credential(),"Consumed invitation reused");
 std::cout<<"PASS paired room refuses third member\n";
 a.forget();b.forget();ak->peer_port=b.port();bk->peer_port=a.port();a.restore(*ak);b.restore(*bk);pump(a,b,[&]{return a.status().connected&&b.status().connected;});require(a.send("after-reconnect"),"Reconnect send failed");pump(a,b,[&]{got=b.receive();return got.has_value();});require(*got=="after-reconnect","Reconnect payload mismatch");
 std::cout<<"PASS saved credentials reconnect without PIN\n";
 a.forget();b.forget();auto bad=*bk;bad.secret[0]^=1;a.restore(*ak);b.restore(bad);settle(a,b,350);require(!a.status().connected&&!b.status().connected,"Wrong saved key authenticated");a.forget();b.forget();
 std::cout<<"PASS wrong saved credential rejected\n";
 code=a.create_invitation();auto wrong=normalize_code(code);wrong[0]=wrong[0]=='A'?'B':'A';b.join(target(a,ac),wrong);pump(a,b,[&]{return b.status().phase=="idle";});require(!a.status().connected&&!a.take_credential()&&!b.take_credential(),"Wrong room code paired");require(!a.status().invitation_code.empty(),"Wrong code consumed invitation");
 std::cout<<"PASS wrong code rejected without consuming invite\n";
 b.join(target(a,ac),code);pump(a,b,[&]{return a.status().connected&&b.status().connected;});a.forget();b.forget();
 std::cout<<"PASS correct retry after wrong code\n";
 auto dc=config("windows",1),ec=config("mac");Service d(dc),e(ec);auto expired=d.create_invitation();auto expiredTarget=target(d,dc);e.discover_at("127.0.0.1",d.port());pump(d,e,[&]{return !e.nearby().empty();});require(e.nearby()[0].room_id==expiredTarget.room_id,"Manual unicast discovery failed");std::cout<<"PASS manual address discovery without broadcast\n";settle(d,e,1100);require(d.status().invitation_code.empty()&&d.status().phase=="idle","Expired invitation remained active");e.join(expiredTarget,expired);settle(d,e,100);require(!e.status().connected&&!d.take_credential(),"Expired code accepted");
 std::cout<<"PASS five-minute policy with shortened expiry test\n";
 auto fc=config("windows"),gc=config("mac");Service f(fc),g(gc);code=f.create_invitation();f.cancel_invitation();require(f.status().phase=="idle"&&f.status().room_id.empty(),"Cancel left discoverable room");
 code=f.create_invitation();auto wrong2=normalize_code(code);wrong2[0]=wrong2[0]=='A'?'B':'A';for(int i=0;i<10;++i){g.join(target(f,fc),wrong2);pump(f,g,[&]{return g.status().phase=="idle";});}
 g.join(target(f,fc),code);settle(f,g,200);require(!f.status().connected&&!g.status().connected,"Rate limit accepted eleventh attempt");
 std::cout<<"PASS cancel and global invitation attempt limit\n";
 std::cout<<"PASS room protocol integration\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
