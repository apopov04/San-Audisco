#pragma once
#include "bridge/engine.hpp"
#include <algorithm>
#include <cmath>
#include <map>
#include <sstream>
#include <string>
namespace bridge::control {
inline bool update(Settings& s,const std::string& key,float v) {
    if(!std::isfinite(v))return false;
    const bool bit=v==0 || v==1;
    if(key=="primary") {if(!bit)return false;s.primary=v==0?Device::A:Device::B;}
    else if(key=="mic_source") {if(!bit)return false;s.microphoneSource=v==0?Device::A:Device::B;}
    else if(key=="mute") {if(!bit)return false;s.globalMute=v!=0;}
    else if(key=="deafen") {if(!bit)return false;s.globalDeafen=v!=0;}
    else if(key=="balance")s.balance=std::clamp(v,-1.f,1.f);
    else if(key=="master")s.masterVolume=std::clamp(v,0.f,1.f);
    else {
        auto p=key.find('_');if(p==std::string::npos)return false;
        int i=key.substr(0,p)=="slot0"?0:key.substr(0,p)=="slot1"?1:-1;
        if(i<0)return false;auto k=key.substr(p+1);auto& r=s.routes[i];
        if(k=="volume")r.volume=std::clamp(v,0.f,1.f);
        else if(!bit)return false;
        else if(k=="mute")r.micMuted=v!=0;
        else if(k=="deafen")r.deafened=v!=0;
        else if(k=="silence")r.silenced=v!=0;
        else return false;
    }
    return true;
}
// Parse whole lines and commit atomically. Unknown/malformed commands never
// partially apply a preceding unmute. Bound input before allocating the stream.
inline bool parse(Settings& s,const std::string& text,std::map<std::string,float>* changes=nullptr) {
    if(text.size()>4096)return false;
    Settings candidate=s;std::map<std::string,float> values;
    std::istringstream input(text);std::string line;bool any=false;
    while(std::getline(input,line)) {
        if(line.find_first_not_of(" \t\r")==std::string::npos)continue;
        std::istringstream row(line);std::string key,extra;float value;
        if(!(row>>key>>value)||(row>>extra)||!update(candidate,key,value))return false;
        values[key]=value;any=true;
    }
    if(!any)return false;s=candidate;if(changes)*changes=std::move(values);return true;
}
inline std::string snapshot(const Settings& s) {
    std::ostringstream o;o<<"primary "<<int(s.primary)<<"\nmic_source "<<int(s.microphoneSource)<<"\nmute "<<s.globalMute<<"\ndeafen "<<s.globalDeafen<<"\nbalance "<<s.balance<<"\nmaster "<<s.masterVolume<<'\n';
    for(int i=0;i<2;i++){auto&r=s.routes[i];auto p=i==0?"slot0_":"slot1_";o<<p<<"volume "<<r.volume<<'\n'<<p<<"mute "<<r.micMuted<<'\n'<<p<<"deafen "<<r.deafened<<'\n'<<p<<"silence "<<r.silenced<<'\n';}return o.str();
}
inline std::string serialize(const std::map<std::string,float>& values) {std::ostringstream o;for(auto&[key,value]:values)o<<key<<' '<<value<<'\n';return o.str();}
}
