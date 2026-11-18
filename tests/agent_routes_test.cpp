#include "bridge/agent_routes.hpp"
#include <iostream>
using namespace bridge;using namespace bridge::agent;
void require(bool b){if(!b)throw std::runtime_error("Route assertion failed");}
int main(){
 std::vector<AudioDevice> d{{"capture","CABLE Output (VB-Audio Virtual Cable)",2,0,48000},{"same","CABLE Input (VB-Audio Virtual Cable)",0,2,48000},{"mic","Hi-Fi Cable Input (VB-Audio Hi-Fi Cable)",0,2,48000},{"hifiCapture","Hi-Fi Cable Output (VB-Audio Hi-Fi Cable)",2,0,48000},{"physical","Real microphone",2,0,48000}};
 std::map<std::string,std::string> r;auto_routes(r,d,"windows");require(r["playback_input"]=="capture"&&r["microphone_output"]=="mic");require(setup_status(r,d,"windows")=="Ready");validate_independent(d,r);
 r["microphone_output"]="same";bool refused=false;try{validate_independent(d,r);}catch(...){refused=true;}require(refused);
 refused=false;try{validate_route(d,"physical_microphone","capture");}catch(...){refused=true;}require(refused);
 refused=false;try{validate_route(d,"headphones_output","physical");}catch(...){refused=true;}require(refused);
 d.push_back({"duplicate","CABLE Output (VB-Audio Virtual Cable)",2,0,48000});r.clear();auto_routes(r,d,"windows");require(r["playback_input"].empty());r["playback_input"]="capture";auto_routes(r,d,"windows");require(r["playback_input"]=="capture");
 d[3].sample_rate=44100;require(setup_status(r,d,"windows").find("48000")!=std::string::npos);
 d[3].sample_rate=48000;
 d.push_back({"airpods","Headphones (AirPods Stereo)",0,2,48000});
 d.push_back({"handsfree","Headset (AirPods Hands-Free AG Audio)",1,1,8000});
 d.push_back({"monitor","Monitor speakers",0,2,48000});
 r={{"playback_input","capture"},{"microphone_output","mic"}};
 auto picked=recommended_routes(r,d,"windows",{{0,"same"}});
 require(picked["headphones_output"]=="airpods"&&picked["physical_microphone"]=="physical");
 require(microphone_input(d,"mic","windows")=="hifiCapture");
 picked["headphones_output"]="monitor";require(recommended_routes(picked,d,"windows",{{0,"airpods"}})["headphones_output"]=="monitor");
 d.push_back({"mic2","USB microphone",1,0,48000});picked["physical_microphone"]="";
 require(recommended_routes(picked,d,"windows",{})["physical_microphone"].empty());
 std::cout<<"PASS automatic routes, ambiguity, saved selection, direction, feedback rejection and Hi-Fi sample-rate readiness\n";
}
