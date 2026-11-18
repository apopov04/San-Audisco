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
inline std::string playback_output(const std::vector<AudioDevice>&ds,const std::string&capture,const std::string&platform){
 auto input=std::find_if(ds.begin(),ds.end(),[&](auto&d){return d.id==capture&&d.input_channels>0;});
 if(input==ds.end())return {};
 if(platform=="mac")return virtual_device(*input)&&input->output_channels>0?input->id:std::string();
 // These driver endpoints have distinct capture/render IDs. Never select the
 // microphone cable, an arbitrary physical endpoint, or the 16ch alternative.
 auto expected=input->name;auto at=expected.find(" Output (");
 if(at==std::string::npos||expected.find("VB-Audio")==std::string::npos)return {};
 expected.replace(at,9," Input (");
 std::string result;
 for(auto&d:ds)if(d.output_channels>0&&d.name==expected){if(!result.empty())return {};result=d.id;}
 return result;
}
inline std::string microphone_input(const std::vector<AudioDevice>&ds,const std::string&render,const std::string&platform){
 auto output=std::find_if(ds.begin(),ds.end(),[&](auto&d){return d.id==render&&d.output_channels>0;});
 if(output==ds.end())return {};
 if(platform=="mac")return virtual_device(*output)&&output->input_channels>0?output->id:std::string();
 auto expected=output->name;auto at=expected.find(" Input (");
 if(at==std::string::npos||expected.find("VB-Audio")==std::string::npos)return {};
 expected.replace(at,8," Output (");std::string result;
 for(auto&d:ds)if(d.input_channels>0&&d.name==expected){if(!result.empty())return {};result=d.id;}
 return result;
}
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
 if(!available(ds,get("playback_input"),true)||!available(ds,get("microphone_output"),false))return "Required audio components missing or unavailable. Run San Audisco Setup, then Refresh devices.";
 if(platform=="windows")for(auto&d:ds)if(d.name.find("Hi-Fi Cable")!=d.name.npos&&d.sample_rate!=48000)return "Hi-Fi Cable must use 48000 Hz on both endpoints. Run San Audisco Setup to finish configuration.";
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
inline std::map<std::string,std::string> recommended_routes(std::map<std::string,std::string> current,const std::vector<AudioDevice>&ds,const std::string&platform,const std::map<int,std::string>&defaults){
 std::map<std::string,std::string> recommended;auto_routes(recommended,ds,platform);
 for(auto key:{"playback_input","microphone_output"})if(!recommended[key].empty())current[key]=recommended[key];
 auto physical=[&](const std::string&id,bool input){auto d=std::find_if(ds.begin(),ds.end(),[&](auto&v){return v.id==id;});return d!=ds.end()&&!virtual_device(*d)&&(input?d->input_channels:d->output_channels)>0;};
 for(auto key:{"physical_microphone","headphones_output"}){
  bool input=std::string(key)=="physical_microphone";if(physical(current[key],input))continue;current[key].clear();
  if(input&&physical(recommended[key],true)){current[key]=recommended[key];continue;}
  std::vector<std::string> candidates,headphones;
  for(auto&d:ds){auto name=d.name;std::transform(name.begin(),name.end(),name.begin(),[](unsigned char c){return char(std::tolower(c));});
   if(!physical(d.id,input)||name.find("hands-free")!=name.npos||(input&&name.find("stereo mix")!=name.npos))continue;
   candidates.push_back(d.id);if(!input&&name.find("headphone")!=name.npos)headphones.push_back(d.id);
  }
  if(!input){for(const auto&[role,id]:defaults){(void)role;if(std::find(candidates.begin(),candidates.end(),id)!=candidates.end()){current[key]=id;break;}}
   if(current[key].empty()&&headphones.size()==1)current[key]=headphones.front();
   if(current[key].empty()&&platform=="mac"&&physical("BuiltInSpeakerDevice",false))current[key]="BuiltInSpeakerDevice";
  }
  if(current[key].empty()&&candidates.size()==1)current[key]=candidates.front();
 }
 for(auto&[key,value]:current)if(key=="playback_input"||key=="microphone_output"||key=="headphones_output"||key=="physical_microphone")validate_route(ds,key,value);
 validate_independent(ds,current);return current;
}
}
