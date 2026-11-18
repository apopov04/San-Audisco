#include "bridge/device_profile.hpp"
#include <iostream>
#include <stdexcept>
#include <vector>
#include <random>
using bridge::device::mac_profile;
using bridge::device::windows_profile;
void require(bool v,const char* reason){if(!v)throw std::runtime_error(reason);}
std::vector<std::uint8_t> chassis(unsigned type) {
    std::vector<std::uint8_t> bytes{0,3,5,0,8,0,0,0,3,6,0,0,0,static_cast<std::uint8_t>(type),0,0};
    return bytes;
}
int main(){try{
    require(mac_profile("MacBookAir10,1").default_name=="MacBook Air","legacy MacBook Air");
    require(mac_profile("Mac17,4").kind=="laptop","verified current MacBook Air model");
    require(mac_profile("Mac14,7").default_name=="MacBook Pro","generic modern Mac ID maps to family");
    require(mac_profile("Mac16,9").kind=="server","Mac Studio uses requested server graphic");
    require(mac_profile("Mac14,8").kind=="desktop","Mac Pro is not guessed as rack chassis");
    require(mac_profile("Xserve3,1").kind=="server","Xserve");
    require(mac_profile("iMac20,1").kind=="desktop","iMac");
    require(mac_profile("Macmini9,1").default_name=="Mac mini","Mac mini");
    require(mac_profile("Mac99,1").kind=="unknown","future models use honest fallback");
    require(windows_profile(chassis(10)).kind=="laptop","notebook chassis");
    require(windows_profile(chassis(0x80|31)).kind=="laptop","convertible lock bit");
    require(windows_profile(chassis(3)).kind=="desktop","desktop chassis");
    require(windows_profile(chassis(23)).kind=="server","rack mount chassis");
    require(windows_profile(chassis(1)).kind=="unknown","unspecified chassis");
    require(windows_profile({},true).default_name=="Windows Server","server fallback");
    require(windows_profile(chassis(10),true).kind=="server","server OS remains explicit");
    auto bad=chassis(10);bad[4]=255;require(windows_profile(bad).kind=="unknown","declared size beyond buffer");
    bad=chassis(10);bad.back()=1;require(windows_profile(bad).kind=="unknown","unterminated record");
    bad=chassis(10);bad[9]=3;require(windows_profile(bad).kind=="unknown","short record header");
    auto complete=chassis(10);
    for(std::size_t n=0;n<complete.size();++n)require(windows_profile(std::span(complete).first(n)).kind=="unknown","truncated table");
    std::mt19937 rng(2026);
    for(int i=0;i<10000;++i){std::vector<std::uint8_t> bytes(rng()%256);for(auto& v:bytes)v=std::uint8_t(rng());(void)windows_profile(bytes);}
    std::cout<<"PASS Mac families and unknown fallback; Windows chassis/server classification; malformed and truncated firmware tables\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
