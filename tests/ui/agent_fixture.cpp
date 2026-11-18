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
  auto device=[&](const char* id,const char* name,const char* platform){return json{{"id",id},{"name",name},{"platform",platform},{"slot",std::string(id)=="win-fixture"?0:1},{"kind",std::string(id)=="win-fixture"?"desktop":"laptop"},{"mix",{{"volume",1.0},{"mic_muted",false},{"deafened",false},{"silenced",false}}},{"online",true},{"audio_devices",json::array({endpoint("test-headphones","Test headphones",0,2),endpoint("test-mic","Test microphone",2,0),endpoint("test-playback","Test playback cable",2,0),endpoint("test-mic-output","Test microphone cable",0,2)})},{"headphones_output","test-headphones"},{"physical_microphone","test-mic"},{"playback_input","test-playback"},{"microphone_output","test-mic-output"},{"setup_status","Test endpoints ready; no real audio"},{"diagnostics",{{"playback_level",0.1},{"microphone_level",0.05},{"microphone_delivery_level",0.08}}}};};
  state={{"device_id","win-fixture"},{"hub_device_id","win-fixture"},{"mic_device_id","mac-fixture"},{"name","Test Windows"},{"room_id",""},{"paired",false},{"connected",false},{"compatible",true},{"controls_ready",false},{"shared_ready",false},{"handover_active",false},{"running",false},{"status","Fixture ready"},{"invite_code",""},{"invite_expires_seconds",300},{"devices",json::array({device("win-fixture","Test Windows","windows"),device("mac-fixture","Test Mac","mac")})},{"settings",{{"primary",0},{"mic_source",1},{"mute",true},{"deafen",false},{"master",1.0},{"balance",0.0},{"slot0_volume",1.0},{"slot1_volume",1.0}}},{"discovered",json::array()}};
 }
 if(argc>1&&std::string(argv[1])=="ensure"){std::ofstream(path)<<state.dump();return 0;}
 std::string line;std::getline(std::cin,line);auto request=json::parse(line);auto command=request.at("command").get<std::string>();
 std::ofstream(directory/"ui-fixture-requests.jsonl",std::ios::app)<<request.dump()<<'\n';
 if(command=="create"){state["invite_code"]="K7MP4X9R";state["status"]="Invitation created";}
 else if(command=="cancel_invite"){state["invite_code"]="";state["status"]="Invitation cancelled";}
 else if(command=="join"){if(request.value("code","")!="K7MP4X9R"){std::cout<<json{{"ok",false},{"error","Wrong invitation code"},{"state",state}}.dump()<<'\n';return 0;}state["paired"]=true;state["connected"]=true;state["shared_ready"]=true;state["controls_ready"]=true;state["invite_code"]="";state["room_id"]="fixture-room";state["status"]="Room connected";}
 else if(command=="discover")state["discovered"]=json::array({{{"id","fixture-room"},{"name","Test room"},{"address","127.0.0.1:45671"}}});
 else if(command=="configure"){for(auto& d:state["devices"])if(d["id"]==request["device_id"])for(auto& [key,value]:request.items())if(key!="command"&&key!="device_id")d[key]=value;}
 else if(command=="control"){auto key=request.at("key").get<std::string>();if(request.contains("device_id")){for(auto&d:state["devices"])if(d["id"]==request["device_id"]){auto field=key=="mute"?"mic_muted":key=="deafen"?"deafened":key=="silence"?"silenced":"volume";d["mix"][field]=request.at("value");state["settings"]["slot"+std::to_string(d["slot"].get<int>())+"_"+key]=request.at("value");}}else state["settings"][key]=request.at("value");state["status"]="Control acknowledged";}
 else if(command=="set_role"){auto input=request.at("role")=="input";state[input?"mic_device_id":"hub_device_id"]=request.at("device_id");for(auto&d:state["devices"])if(d["id"]==request.at("device_id"))state["settings"][input?"mic_source":"primary"]=d["slot"];state["settings"]["mute"]=true;}
 else if(command=="device_profile"){for(auto&d:state["devices"])if(d["id"]==state["device_id"]){d["kind_override"]=request.at("kind");if(request.at("kind")!="auto")d["kind"]=request.at("kind");}}
 else if(command=="start"){state["running"]=true;state["status"]="Fixture audio active";state["mute_reason"]="Microphone starts muted. Unmute when ready.";}
 else if(command=="simulate_engine_failure"){state["running"]=false;state["wanted_running"]=true;state["status"]="Waiting to retry audio";}
 else if(command=="stop"){state["wanted_running"]=false;state["running"]=false;state["status"]="Audio stopped";}
 else if(command=="diagnose")state["diagnostics"]={{"message","Fixture check complete; no audio captured"},{"playback_ready",true},{"mic_sink_ready",true},{"headphones_ready",true},{"microphone_ready",true},{"playback_level",0.2},{"microphone_level",0.15},{"microphone_delivery_level",0.1}};
 else if(command=="test_tone")state["status"]="Fixture test sound requested; no real audio";
 else if(command=="quit"){state["running"]=false;state["status"]="Agent exited";}
 else if(command=="forget"){state["paired"]=false;state["connected"]=false;state["shared_ready"]=false;state["controls_ready"]=false;state["running"]=false;state["room_id"]="";}
 std::ofstream(path)<<state.dump();std::cout<<json{{"ok",true},{"state",state}}.dump()<<'\n';return 0;
}catch(const std::exception&e){std::cout<<json{{"ok",false},{"error",e.what()}}.dump()<<'\n';return 1;}}
