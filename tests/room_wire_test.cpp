// End-to-end adversarial wire tests. A local TCP relay mutates/replays frames;
// all keys and devices are disposable synthetic test data.
#include "bridge/room.hpp"
#include <nlohmann/json.hpp>
#include <chrono>
#include <deque>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>
#include <array>
#ifdef _WIN32
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
using Sock=SOCKET;using Len=int;constexpr Sock bad=INVALID_SOCKET;
void closefd(Sock s){if(s!=bad)closesocket(s);}void nonblock(Sock s){u_long yes=1;ioctlsocket(s,FIONBIO,&yes);}
#else
#include <sys/socket.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
using Sock=int;using Len=socklen_t;constexpr Sock bad=-1;
void closefd(Sock s){if(s!=bad)close(s);}void nonblock(Sock s){fcntl(s,F_SETFL,O_NONBLOCK);int yes=1;setsockopt(s,SOL_SOCKET,SO_NOSIGPIPE,&yes,sizeof(yes));}
#endif
using namespace bridge::room;
using Bytes=std::vector<unsigned char>;
using Json=nlohmann::json;
void need(bool value,const char* why){if(!value)throw std::runtime_error(why);}
sockaddr_in addr(std::uint16_t port){sockaddr_in a{};a.sin_family=AF_INET;a.sin_port=htons(port);inet_pton(AF_INET,"127.0.0.1",&a.sin_addr);return a;}
Config cfg(const std::string& p){Config c;c.local={new_id(),p,p};c.bind_address="127.0.0.1";c.port=0;c.discovery_address="127.0.0.1";c.discovery_port=1;return c;}
struct Relay {
 Sock listener=bad,client=bad,server=bad;std::uint16_t port=0;Bytes fromClient,fromServer,toClient,toServer,recordedClient;std::string attack;bool attacked=false;
 explicit Relay(std::uint16_t target){listener=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);auto a=addr(0);need(bind(listener,reinterpret_cast<sockaddr*>(&a),sizeof(a))==0,"relay bind");need(listen(listener,1)==0,"relay listen");Len n=sizeof(a);getsockname(listener,reinterpret_cast<sockaddr*>(&a),&n);port=ntohs(a.sin_port);nonblock(listener);server=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);a=addr(target);need(connect(server,reinterpret_cast<sockaddr*>(&a),sizeof(a))==0,"relay server connect");nonblock(server);}
 ~Relay(){closefd(client);closefd(server);closefd(listener);}
 void frames(Bytes& in,Bytes& out,bool clientDirection){while(in.size()>=4){auto*p=in.data();std::size_t n=(std::uint32_t(p[0])<<24)|(std::uint32_t(p[1])<<16)|(std::uint32_t(p[2])<<8)|p[3];need(n<100000,"relay oversized frame");if(in.size()<n+4)break;Bytes frame(in.begin(),in.begin()+4+n);in.erase(in.begin(),in.begin()+4+n);if(clientDirection)recordedClient.insert(recordedClient.end(),frame.begin(),frame.end());
   if(!attacked&&clientDirection&&frame[4]==0x80&&(attack=="tamper"||attack=="replay")){attacked=true;if(attack=="tamper")frame.back()^=1;else out.insert(out.end(),frame.begin(),frame.end());}
   if(!attacked&&!clientDirection&&frame[4]=='{'&&(attack=="name"||attack=="port")){auto j=Json::parse(frame.begin()+4,frame.end());if(j.value("type","")=="server"){attacked=true;if(attack=="name")j["device"]["name"]="Impersonated machine";else j["port"]=59999;auto s=j.dump();frame.resize(4+s.size());frame[0]=s.size()>>24;frame[1]=s.size()>>16;frame[2]=s.size()>>8;frame[3]=s.size();std::copy(s.begin(),s.end(),frame.begin()+4);}}
   out.insert(out.end(),frame.begin(),frame.end());}}
 void read(Sock fd,Bytes& in){std::array<unsigned char,8192>b{};for(int i=0;i<16;++i){int n=recv(fd,reinterpret_cast<char*>(b.data()),b.size(),0);if(n<=0)break;in.insert(in.end(),b.begin(),b.begin()+n);}}
 void write(Sock fd,Bytes& out){if(out.empty())return;int n=send(fd,reinterpret_cast<const char*>(out.data()),out.size(),0);if(n>0)out.erase(out.begin(),out.begin()+n);}
 void poll(){if(client==bad){client=accept(listener,nullptr,nullptr);if(client!=bad)nonblock(client);}if(client==bad)return;read(client,fromClient);read(server,fromServer);frames(fromClient,toServer,true);frames(fromServer,toClient,false);write(client,toClient);write(server,toServer);}
};
void pump(Service& server,Service& client,Relay& relay,std::function<bool()> done,int ms=2500){auto end=std::chrono::steady_clock::now()+std::chrono::milliseconds(ms);while(std::chrono::steady_clock::now()<end){server.poll();client.poll();relay.poll();if(done())return;std::this_thread::sleep_for(std::chrono::milliseconds(2));}throw std::runtime_error("wire test timeout: "+server.status().error+" / "+client.status().error);}
int main(){try{
 for(auto attack:{"tamper","replay"}){auto sc=cfg("windows"),cc=cfg("mac");Service server(sc),client(cc);auto code=server.create_invitation();Relay relay(server.port());Nearby target{server.status().room_id,sc.local,"127.0.0.1",relay.port,true};client.join(target,code);pump(server,client,relay,[&]{return server.status().connected&&client.status().connected;});relay.attack=attack;need(client.send("set-microphone"),"test send");pump(server,client,relay,[&]{return relay.attacked&&!server.status().connected;});need(server.status().error.find(attack==std::string("tamper")?"authentication":"replay")!=std::string::npos,"attack did not cause expected protocol error");need(!server.receive(),"unauthenticated/replayed message survived disconnect");std::cout<<"PASS ciphertext "<<attack<<" rejected\n";}
 for(auto attack:{"name","port"}){auto sc=cfg("windows"),cc=cfg("mac");Service server(sc),client(cc);auto code=server.create_invitation();Relay relay(server.port());relay.attack=attack;Nearby target{server.status().room_id,sc.local,"127.0.0.1",relay.port,true};client.join(target,code);pump(server,client,relay,[&]{return relay.attacked&&client.status().phase=="idle";});need(!client.status().connected&&!server.status().connected&&!client.take_credential()&&!server.take_credential(),"modified identity metadata authenticated");std::cout<<"PASS handshake "<<attack<<" binding\n";}
 std::cout<<"PASS adversarial room wire tests\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
