#include "bridge/engine.hpp"
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <thread>

using namespace bridge;
namespace {
void check(bool ok, const char* msg) { if (!ok) { std::fprintf(stderr, "FAIL: %s\n", msg); std::exit(1); } }
bool near(float a, float b) { return std::abs(a-b) < 1e-6f; }
struct Harness {
    Engine engine;
    Settings settings;
    std::array<float, 4> lp{.1f,.2f,.1f,.2f}, rp{.3f,.4f,.3f,.4f}, lm{.6f,.8f,.6f,.8f}, rm{.2f,.4f,.2f,.4f};
    std::array<float, 4> listen{}, localMic{}, remoteMic{}, sendPlayback{}, sendMic{};
    explicit Harness(Device local) : engine(local, 0) {}
    void run() {
        engine.setSettings(settings);
        engine.process({lp.data(),rp.data(),lm.data(),rm.data()},
                       {listen.data(),localMic.data(),remoteMic.data(),sendPlayback.data(),sendMic.data()},2);
    }
};
}
int main() {
    for (auto local : {Device::A,Device::B}) {
        for (auto primary : {Device::A,Device::B}) {
            for (auto source : {Device::A,Device::B}) {
                Harness h(local); h.settings.primary=primary; h.settings.microphoneSource=source; h.run();
                check(near(h.listen[0],local==primary?.4f:0),"primary selection and sum");
                check(near(h.listen[1],local==primary?.6f:0),"stereo preserved");
                check(near(h.localMic[0],local==source?.7f:.3f),"microphone source isolation");
                check(near(h.sendMic[0],local==source?.7f:0),"do not relay received microphone back");
                check(near(h.sendPlayback[0],local!=primary?.1f:0),"only secondary sends raw playback");
                check(h.sendMic==h.remoteMic,"inspection output equals network microphone");
                for (unsigned mask=0;mask<256;++mask) {
                    h.settings.routes[0].micMuted=mask&1; h.settings.routes[1].micMuted=mask&2;
                    h.settings.routes[0].deafened=mask&4; h.settings.routes[1].deafened=mask&8;
                    h.settings.routes[0].silenced=mask&16; h.settings.routes[1].silenced=mask&32;
                    h.settings.globalMute=mask&64; h.settings.globalDeafen=mask&128; h.run();
                    unsigned li=unsigned(local),ri=1-li;
                    auto enabled=[&](unsigned i){ return !h.settings.globalMute&&!h.settings.globalDeafen&&!h.settings.routes[i].micMuted&&!h.settings.routes[i].deafened; };
                    check(near(h.localMic[0],enabled(li)?(local==source?.7f:.3f):0),"local destination mute matrix");
                    check(near(h.sendMic[0],enabled(ri)&&local==source?.7f:0),"remote destination independent mute matrix");
                    auto gain=[&](unsigned i){return !h.settings.globalDeafen&&!h.settings.routes[i].silenced&&!h.settings.routes[i].deafened?1.f:0.f;};
                    check(near(h.listen[0],local==primary?.1f*gain(li)+.3f*gain(ri):0),"deafen listening matrix");
                }
            }
        }
    }
    Harness h(Device::A); h.settings.microphoneSource=Device::A;
    h.settings.routes[0].micMuted=true; h.settings.routes[0].deafened=true; h.run();
    h.settings.routes[0].deafened=false; h.run();
    check(h.localMic[0]==0 && near(h.sendMic[0],.7f),"undeafen preserves explicit mute and remote continues");
    h.settings.routes[0].micMuted=false; h.settings.balance=-1; h.run();
    check(near(h.listen[0],.1f),"Windows balance endpoint");
    h.settings.balance=1; h.run(); check(near(h.listen[0],.3f),"Mac balance endpoint");
    h.settings.balance=0; h.settings.routes[0].volume=.5f; h.settings.routes[1].volume=.25f; h.settings.masterVolume=.5f; h.run();
    check(near(h.listen[0],.0625f),"independent gains and master");
    h.settings=Settings{}; h.lp.fill(1); h.rp.fill(1); h.run(); check(h.listen[0]==1,"positive clipping limited");
    h.lp.fill(-1); h.rp.fill(-1); h.run(); check(h.listen[0]==-1,"negative clipping limited");
    h.lp.fill(std::numeric_limits<float>::quiet_NaN()); h.rp.fill(std::numeric_limits<float>::infinity()); h.run();
    for(float x:h.listen) check(x==0,"non-finite input produces finite silence");
    h.settings.masterVolume=std::numeric_limits<float>::quiet_NaN(); h.lp.fill(.5f); h.rp.fill(.2f); h.run();
    check(h.listen[0]==0,"non-finite gain sanitized");
    h.engine.process({}, {h.listen.data(),h.localMic.data(),nullptr,nullptr,nullptr},2);
    check(h.listen[0]==0&&h.localMic[0]==0,"missing input produces silence");

    SpscAudioRing<4> ring;
    std::array<float,12> input{1,2,3,4,5,6,7,8,9,10,11,12}, output{};
    check(ring.push(input.data(),6)==4,"ring drops newest overflow");
    check(ring.pop(output.data(),2)==2&&output[0]==1&&output[3]==4,"ring keeps oldest frames");
    check(ring.push(input.data()+8,2)==2,"ring wrap push");
    check(ring.pop(output.data(),6)==4&&output[0]==5&&output[6]==11&&output[8]==0,"ring wrap and underrun zero fill");
    ring.push(input.data(),4); ring.discard();
    check(ring.pop(output.data(),1)==0&&output[0]==0,"discard stale audio");

    // Control updates must not race the RT callback or create mixed settings.
    Engine concurrent(Device::A, 0); std::atomic<bool> done=false;
    std::thread writer([&]{for(int i=0;i<100000;++i){Settings s; s.routes[0].volume=(i&1)?0:1; s.routes[1].volume=(i&1)?1:0; concurrent.setSettings(s);}done=true;});
    std::array<float,2> ones{.25f,.25f}, mixed{};
    while(!done.load()) { concurrent.process({ones.data(),ones.data(),nullptr,nullptr},{mixed.data()},1);check(near(mixed[0],.25f)||near(mixed[0],.5f),"settings snapshot coherent"); }
    writer.join();
    SpscAudioRing<256> concurrentRing; constexpr int total=100000;
    std::thread producer([&]{for(int i=0;i<total;){float frame[2]{float(i),float(-i)};if(concurrentRing.push(frame,1))++i;}});
    for(int i=0;i<total;){float frame[2];if(concurrentRing.pop(frame,1)){check(frame[0]==float(i)&&frame[1]==float(-i),"SPSC threaded order");++i;}}
    producer.join();
    Engine ramped(Device::A, 4);
    Settings rampSettings; ramped.setSettings(rampSettings);
    std::array<float,8> signal{.5,.5,.5,.5,.5,.5,.5,.5}, fade{};
    ramped.process({signal.data()}, {fade.data()}, 4);
    check(near(fade[0],.125f)&&near(fade[2],.25f)&&near(fade[6],.5f),"startup gain ramp");
    rampSettings.routes[0].volume=0; ramped.setSettings(rampSettings);
    ramped.process({signal.data()}, {fade.data()}, 4);
    check(near(fade[0],.375f)&&near(fade[6],0),"volume change ramp");
    rampSettings.routes[0].volume=1; ramped.setSettings(rampSettings);
    ramped.process({signal.data()}, {fade.data()}, 4);
    rampSettings.routes[0].deafened=true; ramped.setSettings(rampSettings);
    ramped.process({signal.data()}, {fade.data()}, 4);
    check(fade[0]==0&&fade[6]==0,"deafen immediate without fade leakage");
    std::puts("engine tests passed (2048 route combinations, isolation, gains, nonfinite, ring, concurrency)");
}
