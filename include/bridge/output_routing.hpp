#pragma once
#include <nlohmann/json.hpp>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>

namespace bridge::routing {
// One direction per backend. Explicit per-app selections always stay intact.
struct Backend {
 virtual ~Backend()=default;
 virtual std::map<int,std::string> defaults()=0;
 virtual void set(int role,const std::string& endpoint)=0;
};
std::unique_ptr<Backend> system_backend(bool input=false);

class Lease {
 struct Entry {std::string before,target;};
 Backend& backend_;
 std::function<void(const nlohmann::json&)> save_;
 std::map<int,Entry> entries_;
 std::set<int> overridden_;
 std::string target_,warning_;
 void persist();
public:
 Lease(Backend& backend,std::function<void(const nlohmann::json&)> save,const nlohmann::json& journal);
 void begin();
 void acquire(const std::string& target);
 // Recover a known app cable selected before takeover, using an explicitly
 // configured physical endpoint. Never replaces an existing recovery record.
 void adopt_existing(const std::string& target,const std::string& physical);
 void restore() noexcept;
 const std::string& warning()const{return warning_;}
 bool active()const{return !entries_.empty();}
};
}
