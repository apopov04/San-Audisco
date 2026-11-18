#include "bridge/output_routing.hpp"
namespace bridge::routing {
namespace {
// Isolated test process memory only; this file is never linked into a release.
class Fake:public Backend {
 std::map<int,std::string> values_{{0,"test-speakers"},{1,"test-headphones"},{2,"test-calls"}};
public:
 std::map<int,std::string> defaults()override{return values_;}
 void set(int role,const std::string& endpoint)override{values_[role]=endpoint;}
};
}
std::unique_ptr<Backend> system_backend(bool){return std::make_unique<Fake>();}
}
