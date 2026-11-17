#include "bridge/adaptive_ring.hpp"
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <thread>
using bridge::AdaptiveAudioRing;
void check(bool x,const char* msg){if(!x){std::fprintf(stderr,"FAIL: %s\n",msg);std::exit(1);}}
int main(){
    std::array<float,1024> input{}; input.fill(.5f);std::array<float,480> output{};
    AdaptiveAudioRing<2048> ring(480);
    ring.push(input.data(),240);check(ring.pop(output.data(),240)==0,"prefill waits");
    ring.push(input.data(),240);check(ring.pop(output.data(),240)==240,"prefill starts");
    for(auto x:output)check(x==.5f,"constant amplitude survives resampling");
    check(ring.pop(output.data(),240)==0,"underrun silences entire block");
    check(ring.underrun==240,"underrun observable");
    check(ring.available()==0,"underrun discards partial stale data");
    for(int i=0;i<5;++i)ring.push(input.data(),512);
    check(ring.dropped>0,"overflow drops newest");
    check(ring.pop(output.data(),240)==0&&ring.available()==0,"overflow discontinuity flushes and reprimes");
    input.fill(std::numeric_limits<float>::quiet_NaN());ring.push(input.data(),512);ring.pop(output.data(),240);
    for(auto x:output)check(x==0,"nonfinite sanitized");
    ring.discard();check(ring.ratio()==1&&ring.available()==0,"explicit reset restores clock");
    AdaptiveAudioRing<2048> interpolation(480);
    for(std::size_t i=0;i<512;++i){input[2*i]=float(i)/512;input[2*i+1]=-float(i)/512;}
    interpolation.push(input.data(),512);interpolation.pop(output.data(),240);
    check(output[0]==0&&std::abs(output[200]-float(100*interpolation.ratio()/512))<1e-5,"linear interpolation follows fractional sample position");
    for(std::size_t i=0;i<240;++i)check(output[2*i]==-output[2*i+1],"resampling preserves stereo separation");
    // Simulate 90 seconds for each direction, with a fractional source clock.
    // Hardware generates 240 +/- fractional drift frames per 5ms consumer block.
    for(double ppm:{-900.,-500.,0.,500.,900.}){
        AdaptiveAudioRing<2048> drift(720);input.fill(.4f);drift.push(input.data(),512);drift.push(input.data(),208);
        double produced=0;std::uint64_t total=0;int priming=0;
        for(int tick=0;tick<18000;++tick){
            if(tick){produced+=240*(1+ppm/1000000.);const auto count=std::uint64_t(produced)-total;total+=count;check(drift.push(input.data(),count)==count,"drift no overflow");}
            if(!drift.pop(output.data(),240))++priming;
            check(std::abs(drift.ratio()-1)<=.00100001,"ratio stays bounded");
            for(auto x:output)check(std::isfinite(x)&&std::abs(x)<=.40001f,"finite bounded samples");
        }
        check(priming==0&&drift.underrun==0&&drift.dropped==0,"clock drift no discontinuities");
        check(std::abs((drift.ratio()-1)*1000000-ppm)<100,"drift ratio converges");
        std::printf("clock %+.0f ppm -> ratio %+.2f ppm, fill %zu\n",ppm,(drift.ratio()-1)*1000000,drift.available());
    }
    // Batch-sized hardware callbacks must coexist with the worker's 240-frame
    // blocks. Prefill accommodates the worst relative phase of these clocks.
    for(std::size_t callback:{std::size_t(128),std::size_t(256),std::size_t(512)}){
        AdaptiveAudioRing<4096> batched(callback+480);input.fill(.2f);
        batched.push(input.data(),512);batched.push(input.data(),512);
        std::size_t pending=0;
        for(int tick=0;tick<6000;++tick){
            if(tick){pending+=240;while(pending>=callback){batched.push(input.data(),callback);pending-=callback;}}
            check(batched.pop(output.data(),240)==240,"batched callbacks never underrun");
        }
        check(batched.dropped==0&&batched.underrun==0,"batched callbacks maintain bounded fill");
    }
    // True concurrent callback producer and consumer stress; discontinuities
    // are expected under unconstrained scheduling, but memory accesses are safe.
    AdaptiveAudioRing<1024> concurrent(480);input.fill(.25f);std::atomic<bool> done=false;
    std::thread producer([&]{for(int i=0;i<10000;++i)concurrent.push(input.data(),240);done=true;});
    while(!done.load()){concurrent.pop(output.data(),240);for(float x:output)check(x==0||std::abs(x-.25f)<1e-6,"concurrent finite signal");}
    producer.join();std::puts("adaptive ring tests passed");
}
