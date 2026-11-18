// Test-only agent protocol fixture. It never starts audio or touches real credentials.
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
using nlohmann::json;
int main(int argc,char**argv){try{
 auto directory=std::filesystem::absolute(argv[0]).parent_path();auto path=directory/"ui-fixture-state.json";
 json state;
 if(std::filesystem::exists(path)){std::ifstream in(path);in>>state;}else {
  auto endpoint=[](const char* id,const char* name,int inputs,int outputs){return json{{"id",id},{"name",name},{"inputs",inputs},{"outputs",outputs},{"rate",48000}};};
  auto device=[&](const char* id,const char* name,const char* platform){return json{{"id",id},{"name",name},{"platform",platform},{"online",true},{"audio_devices",json::array({endpoint("test-headphones","Test headphones",0,2),endpoint("test-mic","Test microphone",2,0),endpoint("test-playback","Test playback cable",2,0),endpoint("test-mic-output","Test microphone cable",0,2)})},{"headphones_output","test-headphones"},{"physical_microphone","test-mic"},{"playback_input","test-playback"},{"microphone_output","test-mic-output"},{"setup_status","Test endpoints ready; no real audio"},{"diagnostics",{{"playback_level",0.1},{"microphone_level",0.05},{"microphone_delivery_level",0.08}}}};};
  state={{"device_id","win-fixture"},{"name","Test Windows"},{"room_id",""},{"paired",false},{"connected",false},{"running",false},{"status","Fixture ready"},{"invite_code",""},{"invite_expires_seconds",300},{"devices",json::array({device("win-fixture","Test Windows","windows"),device("mac-fixture","Test Mac","mac")})},{"settings",{{"primary",0},{"mic_source",1},{"mute",true},{"deafen",false},{"master",1.0},{"balance",0.0},{"win_volume",1.0},{"mac_volume",1.0}}},{"discovered",json::array()}};
 }
 if(argc>1&&std::string(argv[1])=="ensure"){std::ofstream(path)<<state.dump();return 0;}
 std::string line;std::getline(std::cin,line);auto request=json::parse(line);auto command=request.at("command").get<std::string>();
 std::ofstream(directory/"ui-fixture-requests.jsonl",std::ios::app)<<request.dump()<<'\n';
 if(command=="create"){state["invite_code"]="K7MP4X9R";state["status"]="Invitation created";}
 else if(command=="join"){if(request.value("code","")!="K7MP4X9R"){std::cout<<json{{"ok",false},{"error","Wrong invitation code"},{"state",state}}.dump()<<'\n';return 0;}state["paired"]=true;state["connected"]=true;state["invite_code"]="";state["room_id"]="fixture-room";state["status"]="Room connected";}
 else if(command=="discover")state["discovered"]=json::array({{{"id","fixture-room"},{"name","Test room"},{"address","127.0.0.1:45671"}}});
 else if(command=="configure"){for(auto& d:state["devices"])if(d["id"]==request["device_id"])for(auto& [key,value]:request.items())if(key!="command"&&key!="device_id")d[key]=value;}
 else if(command=="control"){state["settings"][request.at("key").get<std::string>()]=request.at("value");state["status"]="Control acknowledged";}
 else if(command=="start"){state["running"]=true;state["status"]="Fixture audio active";state["mute_reason"]="Microphone starts muted. Unmute when ready.";}
 else if(command=="simulate_engine_failure"){state["running"]=false;state["wanted_running"]=true;state["status"]="Waiting to retry audio";}
 else if(command=="stop"){state["wanted_running"]=false;state["running"]=false;state["status"]="Audio stopped";}
 else if(command=="diagnose")state["diagnostics"]={{"message","Fixture check complete; no audio captured"},{"playback_ready",true},{"mic_sink_ready",true},{"headphones_ready",true},{"microphone_ready",true},{"playback_level",0.2},{"microphone_level",0.15},{"microphone_delivery_level",0.1}};
 else if(command=="test_tone")state["status"]="Fixture test sound requested; no real audio";
 else if(command=="quit"){state["running"]=false;state["status"]="Agent exited";}
 else if(command=="forget"){state["paired"]=false;state["connected"]=false;state["running"]=false;state["room_id"]="";}
 std::ofstream(path)<<state.dump();std::cout<<json{{"ok",true},{"state",state}}.dump()<<'\n';return 0;
}catch(const std::exception&e){std::cout<<json{{"ok",false},{"error",e.what()}}.dump()<<'\n';return 1;}}
