#include "bridge/control.hpp"
#include <cstdio>
#include <cstdlib>
using namespace bridge;
void check(bool x,const char* msg){if(!x){std::fprintf(stderr,"FAIL %s\n",msg);std::exit(1);}}
int main(){
 Settings s;s.globalMute=true;
 check(control::parse(s,"slot0_volume 0.25\nslot1_mute 1\nbalance -0.5\n"),"valid control");
 check(s.routes[0].volume==.25f&&s.routes[1].micMuted&&s.balance==-.5f,"settings applied");
 auto original=control::snapshot(s);
 for(auto bad:{"mute 0\nunknown 1\n","mute 0 garbage\n","primary 4\n","mic_source -1\n","mute nan\n","mute inf\n","slot0_mute .5\n","mute\n","\n"}){
  check(!control::parse(s,bad),"malformed rejected");check(control::snapshot(s)==original,"rejection is atomic");
 }
 check(!control::parse(s,std::string(5000,'x')),"oversized rejected");
 Settings copy;check(control::parse(copy,original)&&control::snapshot(copy)==original,"snapshot roundtrip");
 check(control::parse(s,"master 5\nslot1_volume -2\n")&&s.masterVolume==1&&s.routes[1].volume==0,"gains safely clamped");
 std::puts("control tests passed");
}
