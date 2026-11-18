#pragma once
#include "bridge/audio.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <map>
#include <stdexcept>
namespace bridge::agent {
using Json=nlohmann::json;
inline bool virtual_device(const AudioDevice&d){auto s=d.name;std::transform(s.begin(),s.end(),s.begin(),[](unsigned char c){return char(std::tolower(c));});return s.find("blackhole")!=s.npos||s.find("cable")!=s.npos||s.find("streaming")!=s.npos;}
inline std::string cable_family(const AudioDevice&d){
 if(d.name.find("Hi-Fi Cable")!=d.name.npos)return "hifi";
 if(d.name.find("VB-Audio Virtual Cable")!=d.name.npos)return "vbcable";
 if(d.name.find("BlackHole")!=d.name.npos)return d.name;
 return d.id;
}
inline void validate_independent(const std::vector<AudioDevice>&ds,const std::map<std::string,std::string>&routes){
 auto p=routes.find("playback_input"),m=routes.find("microphone_output");if(p==routes.end()||m==routes.end()||p->second.empty()||m->second.empty())return;
 auto a=std::find_if(ds.begin(),ds.end(),[&](auto&d){return d.id==p->second;});auto b=std::find_if(ds.begin(),ds.end(),[&](auto&d){return d.id==m->second;});
 if(p->second==m->second||(a!=ds.end()&&b!=ds.end()&&cable_family(*a)==cable_family(*b)))throw std::runtime_error("Playback and microphone must use independent virtual cables to prevent audio feedback");
}
inline Json audio_json(const std::vector<AudioDevice>&ds){Json a=Json::array();for(auto&d:ds)a.push_back({{"id",d.id},{"name",d.name},{"inputs",d.input_channels},{"outputs",d.output_channels},{"rate",d.sample_rate}});return a;}
inline bool available(const std::vector<AudioDevice>&ds,const std::string&id,bool input){return !id.empty()&&std::any_of(ds.begin(),ds.end(),[&](auto&d){return d.id==id&&(input?d.input_channels:d.output_channels)>0;});}
inline void auto_routes(std::map<std::string,std::string>&r,const std::vector<AudioDevice>&ds,const std::string&platform){
 auto fill=[&](const char*key,bool input,auto match){if(available(ds,r[key],input))return;std::vector<std::string> found;for(auto&d:ds)if((input?d.input_channels:d.output_channels)>0&&match(d))found.push_back(d.id);if(found.size()==1)r[key]=found.front();};
 if(platform=="mac"){
  fill("playback_input",true,[](auto&d){return d.id=="BlackHole2ch_UID";});
  fill("microphone_output",false,[](auto&d){return d.id=="BlackHole16ch_UID";});
  fill("physical_microphone",true,[](auto&d){return d.id=="BuiltInMicrophoneDevice";});
 }else{
  fill("playback_input",true,[](auto&d){return d.name.starts_with("CABLE Output (")&&d.name.find("VB-Audio")!=std::string::npos;});
  fill("microphone_output",false,[](auto&d){return d.name.starts_with("Hi-Fi Cable Input (")&&d.name.find("VB-Audio")!=std::string::npos;});
 }
}
inline std::string setup_status(const std::map<std::string,std::string>&r,const std::vector<AudioDevice>&ds,const std::string&platform){
 auto get=[&](const char*k){auto it=r.find(k);return it==r.end()?std::string():it->second;};
 if(!available(ds,get("playback_input"),true)||!available(ds,get("microphone_output"),false))return "Required audio components missing or unavailable. Run AudioBridge Setup, then Refresh devices.";
 if(platform=="windows")for(auto&d:ds)if(d.name.find("Hi-Fi Cable")!=d.name.npos&&d.sample_rate!=48000)return "Hi-Fi Cable must use 48000 Hz on both endpoints. Run AudioBridge Setup to finish configuration.";
 return "Ready";
}
inline void validate_route(const std::vector<AudioDevice>&ds,const std::string&key,const std::string&id){
 bool physical=key=="physical_microphone"||key=="headphones_output";bool input=key=="physical_microphone"||key=="playback_input";
 if(key!="playback_input"&&key!="microphone_output"&&!physical)throw std::runtime_error("Unknown audio route");
 if(id.empty()&&physical)return;
 auto it=std::find_if(ds.begin(),ds.end(),[&](auto&d){return d.id==id&&(input?d.input_channels:d.output_channels)>0;});
 if(it==ds.end())throw std::runtime_error("Selected audio device is unavailable or has the wrong direction");
 if(physical&&virtual_device(*it))throw std::runtime_error("Choose physical headphones or a physical microphone, not a virtual cable");
 if(!physical&&!virtual_device(*it))throw std::runtime_error("Playback and microphone delivery require virtual audio cables");
 if(id.find_first_of("\r\n")!=std::string::npos)throw std::runtime_error("Invalid device identifier");
}
}
