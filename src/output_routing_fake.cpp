#include "bridge/output_routing.hpp"
#include <cstdlib>
#include <fstream>
namespace bridge::routing {
namespace {
// Test process memory, or an explicitly supplied test file; never a release backend.
class Fake:public Backend {
 std::map<int,std::string> values_{{0,"test-speakers"},{1,"test-headphones"},{2,"test-calls"}};
 std::string file_,direction_;
 nlohmann::json read(){std::ifstream in(file_);return nlohmann::json::parse(in);}
public:
 explicit Fake(bool input):direction_(input?"input":"output"){if(auto path=std::getenv("BRIDGE_FAKE_DEFAULTS_FILE"))file_=path;}
 std::map<int,std::string> defaults()override{if(file_.empty())return values_;auto j=read();std::map<int,std::string> result;for(auto it=j.at(direction_).begin();it!=j.at(direction_).end();++it)result[std::stoi(it.key())]=it.value().get<std::string>();return result;}
 void set(int role,const std::string& endpoint)override{if(file_.empty()){values_[role]=endpoint;return;}auto j=read();j[direction_][std::to_string(role)]=endpoint;std::ofstream(file_)<<j.dump(2);}
};
}
std::unique_ptr<Backend> system_backend(bool input){return std::make_unique<Fake>(input);}
}
