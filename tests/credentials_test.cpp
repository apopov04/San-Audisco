#include "bridge/credentials.hpp"
#include <iostream>
#include <stdexcept>
#include <string>
using namespace bridge;
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
int main(int argc,char**argv){try{
 for(auto id:{"home-pc","MacBook_1","A","123"})credentials::validate_id(id);
 for(auto id:{"","../escape","a/b","a\\b","a.b","contains space","nonascii-\xc3\xa9"}){bool rejected=false;try{credentials::validate_id(id);}catch(const std::invalid_argument&){rejected=true;}require(rejected,"Invalid identifier accepted");}
 bool rejected=false;try{credentials::validate_id(std::string(65,'a'));}catch(const std::invalid_argument&){rejected=true;}require(rejected,"Long identifier accepted");
 auto key=net::random_key();auto record=credentials::encode_record(key);require(credentials::decode_record(record)==key,"Record roundtrip");record[3]=2;rejected=false;try{credentials::decode_record(record);}catch(const std::runtime_error&){rejected=true;}require(rejected,"Unknown version accepted");rejected=false;try{credentials::decode_record(std::span(record.data(),35));}catch(const std::runtime_error&){rejected=true;}require(rejected,"Short record accepted");
 std::cout<<"PASS credential identifiers and versioned record validation\n";
 if(argc>1&&std::string(argv[1])=="--store-roundtrip"){
  const std::string id="test-"+net::key_to_hex(net::random_key()).substr(0,24);bool created=false;
  try{
   require(!credentials::delete_key(id),"Unexpected random test entry existed");
   credentials::store_key(id,key);created=true;require(credentials::load_key(id)==key,"Protected storage roundtrip");
   auto replacement=net::random_key();credentials::store_key(id,replacement);require(credentials::load_key(id)==replacement,"Protected storage update");
   require(credentials::delete_key(id),"Delete protected entry");created=false;require(!credentials::delete_key(id),"Delete missing must return false");
   bool missing=false;try{credentials::load_key(id);}catch(const std::runtime_error&){missing=true;}require(missing,"Deleted credential remains readable");
   std::cout<<"PASS OS protected storage create/read/update/delete; temporary entry removed\n";
  }catch(...){if(created){try{credentials::delete_key(id);}catch(...){std::cerr<<"Temporary credential cleanup failed for "<<id<<"\n";}}throw;}
 }
 return 0;
 }catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
