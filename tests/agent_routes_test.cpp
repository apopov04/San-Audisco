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
 std::cout<<"PASS automatic routes, ambiguity, saved selection, direction, feedback rejection and Hi-Fi sample-rate readiness\n";
}
