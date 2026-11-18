#include "bridge/output_routing.hpp"
#include <stdexcept>
namespace bridge::routing {
Lease::Lease(Backend& backend,std::function<void(const nlohmann::json&)> save,const nlohmann::json& journal):backend_(backend),save_(std::move(save)) {
 if(journal.empty())return;
 if(journal.value("version",0)!=1)throw std::runtime_error("Unknown saved output routing version");
 for(const auto& row:journal.at("entries")) {
  int role=row.at("role");std::string before=row.at("before"),target=row.at("target");
  if(role<0||role>2||before.empty()||target.empty()||before.size()>2048||target.size()>2048)throw std::runtime_error("Invalid saved output route");
  entries_[role]={before,target};
 }
}
void Lease::persist(){
 nlohmann::json rows=nlohmann::json::array();
 for(const auto&[role,e]:entries_)rows.push_back({{"role",role},{"before",e.before},{"target",e.target}});
 save_({{"version",1},{"entries",rows}});
}
void Lease::begin(){restore();overridden_.clear();target_.clear();}
void Lease::acquire(const std::string& target){
 if(target.empty())throw std::runtime_error("No matching playback cable output. Select the app output in Sound settings.");
 if(target!=target_){restore();if(active())throw std::runtime_error(warning_);target_=target;}
 warning_.clear();auto current=backend_.defaults();
 for(const auto&[role,id]:current){
  auto old=entries_.find(role);
  if(old!=entries_.end()){
   if(id==old->second.target)continue;
   // A user/OS selection wins for the rest of this audio session.
   overridden_.insert(role);entries_.erase(old);persist();continue;
  }
  if(id.empty()||id==target||overridden_.contains(role))continue;
  entries_[role]={id,target};
  // Write ahead: a crash after the OS change can be repaired at next launch.
  try{persist();backend_.set(role,target);}
  catch(...){restore();throw;}
 }
}
void Lease::adopt_existing(const std::string& target,const std::string& physical){
 if(target.empty()||physical.empty()||physical==target)return;
 for(const auto&[role,id]:backend_.defaults())if(id==target&&!entries_.contains(role)&&!overridden_.contains(role)){
  entries_[role]={physical,target};
  try{persist();}catch(...){entries_.erase(role);throw;}
 }
}
void Lease::restore() noexcept {
 try {
  if(entries_.empty())return;
  auto current=backend_.defaults();warning_.clear();
  for(auto it=entries_.begin();it!=entries_.end();){
   try {
    auto d=current.find(it->first);
    // Do not overwrite a manual change; retain recovery info if no default exists.
    if(d==current.end())throw std::runtime_error("Output device unavailable; restoration will retry");
    if(d->second==it->second.target)backend_.set(it->first,it->second.before);
    else overridden_.insert(it->first);
    it=entries_.erase(it);
   }catch(const std::exception&e){warning_=e.what();++it;}
  }
  persist();
 }catch(const std::exception&e){warning_=e.what();}
}
}
