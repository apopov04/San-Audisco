#include "bridge/output_routing.hpp"
#include "bridge/agent_routes.hpp"
#include <iostream>
#include <stdexcept>
using namespace bridge::routing;
using Json=nlohmann::json;
void require(bool b,const char*why){if(!b)throw std::runtime_error(why);}
struct Fake:Backend {
 std::map<int,std::string> values{{0,"speakers"},{1,"headphones"},{2,"calls"}};
 int fail=-1,writes=0;bool unplugged=false;Json* journal=nullptr;
 std::map<int,std::string> defaults()override{return values;}
 void set(int role,const std::string&v)override{
  if(role==fail||unplugged)throw std::runtime_error("device unavailable");
  require(journal&&!journal->at("entries").empty(),"OS write preceded durable recovery record");
  values[role]=v;++writes;
 }
};
int main(){try{
 Json journal=Json::object();Fake backend;backend.journal=&journal;
 auto save=[&](const Json&j){journal=j;};
 Lease lease(backend,save,journal);auto original=backend.values;
 lease.begin();lease.acquire("cable");require(backend.writes==3,"all render roles should switch");
 lease.acquire("cable");require(backend.writes==3,"unchanged polling rewrote OS defaults");
 lease.restore();require(backend.values==original&&!lease.active(),"Stop failed to restore distinct defaults");
 lease.begin();lease.acquire("cable");backend.values[1]="manual";lease.acquire("cable");lease.restore();
 require(backend.values[1]=="manual","manual choice clobbered");lease.acquire("cable");require(backend.values[1]=="manual","manual choice reacquired within session");lease.restore();
 lease.begin();backend.fail=1;bool failed=false;try{lease.acquire("cable");}catch(...){failed=true;}
 require(failed&&backend.values[0]=="speakers"&&backend.values[1]=="manual","partial switch rollback failed");backend.fail=-1;
 lease.begin();lease.acquire("cable");Lease afterCrash(backend,save,journal);afterCrash.restore();require(backend.values[0]=="speakers","crash recovery failed");
 // A device unplugged during restoration keeps the journal for a later retry.
 Lease disconnected(backend,save,journal);disconnected.begin();disconnected.acquire("cable");backend.unplugged=true;disconnected.restore();require(disconnected.active()&&!disconnected.warning().empty(),"unplugged original lost recovery state");backend.unplugged=false;disconnected.restore();require(!disconnected.active(),"restore retry failed");
 // Legacy/manual defaults already set to our cable still need a physical exit.
 backend.values={{0,"cable"},{1,"manual"},{2,"cable"}};journal=Json::object();Lease legacy(backend,save,journal);
 legacy.adopt_existing("cable","selected-headphones");require(journal["entries"].size()==2,"Existing cable defaults were not journaled");
 legacy.adopt_existing("cable","other-device");legacy.restore();require(backend.values[0]=="selected-headphones"&&backend.values[2]=="selected-headphones"&&backend.values[1]=="manual","Legacy recovery changed a manual output or replaced original recovery target");
 backend.values[0]="cable";Lease noFallback(backend,save,Json::object());noFallback.adopt_existing("cable","");noFallback.restore();require(backend.values[0]=="cable","Recovery guessed an output without an explicit physical selection");
 // Never modify OS output when write-ahead storage fails.
 int writes=backend.writes;Lease noDisk(backend,[](const Json&){throw std::runtime_error("disk full");},Json::object());
 try{noDisk.acquire("cable");}catch(...){}require(backend.writes==writes,"switched without recovery journal");
 std::vector<bridge::AudioDevice> devices{{"capture","CABLE Output (VB-Audio Virtual Cable)",2,0,48000},{"render","CABLE Input (VB-Audio Virtual Cable)",0,2,48000},{"other","CABLE In 16ch (VB-Audio Virtual Cable)",0,2,48000},{"mic","Hi-Fi Cable Input (VB-Audio Hi-Fi Cable)",0,2,48000},{"bh","BlackHole 2ch",2,2,48000}};
 require(bridge::agent::playback_output(devices,"capture","windows")=="render","wrong Windows render endpoint");
 require(bridge::agent::playback_output(devices,"bh","mac")=="bh","wrong Mac render endpoint");
 devices.push_back({"duplicate","CABLE Input (VB-Audio Virtual Cable)",0,2,48000});require(bridge::agent::playback_output(devices,"capture","windows").empty(),"ambiguous output was guessed");
 std::cout<<"PASS routing takeover, independent defaults, idempotence, rollback, manual override, crash recovery, unplug/retry, journal failure, cable mapping\n";
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
