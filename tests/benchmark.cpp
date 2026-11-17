#include "bridge/engine.hpp"
#include <array>
#include <chrono>
#include <iostream>
int main(){bridge::Engine e(bridge::Device::Windows);bridge::Settings s;e.setSettings(s);std::array<float,480>a{},b{},m{},o{},v{};a.fill(.1f);b.fill(.2f);m.fill(.3f);constexpr int n=200000;auto start=std::chrono::steady_clock::now();for(int i=0;i<n;i++)e.process({a.data(),b.data(),m.data(),m.data()},{o.data(),v.data(),nullptr,nullptr,nullptr},240);double seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();std::cout<<"Routing engine only: "<<n<<" blocks, "<<seconds<<" seconds, "<<seconds/n*1e6<<" us per 5ms block, "<<seconds/(n*.005)*100<<"% of one core equivalent. Excludes I/O, codec, network and GUI.\n";}
