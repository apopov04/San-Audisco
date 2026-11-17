#include "bridge/listening_mixer.hpp"
#include "bridge/transport.hpp"
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>
using namespace bridge;
void check(bool yes,const char* message){if(!yes){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}}
int main(){
    constexpr std::size_t target=1536,block=240,impulseTick=100;
    ListeningMixer direct(Device::Windows,target);Settings settings;direct.setSettings(settings);
    AdaptiveAudioRing<8192> oldCapture(target),oldRender(target);Engine oldEngine(Device::Windows);
    oldEngine.setSettings(settings);
    std::array<float,block*2> input{},capture{},mixed{},oldOutput{},newOutput{};
    std::size_t directImpulse=0,oldImpulse=0;
    for(std::size_t tick=0;tick<180;++tick){
        input.fill(0);if(tick==impulseTick)input[0]=input[1]=.8f;
        direct.pushLocal(input.data(),block);direct.render(newOutput.data(),block);
        oldCapture.push(input.data(),block);oldCapture.pop(capture.data(),block);
        oldEngine.process({capture.data()},{mixed.data()},block);
        oldRender.push(mixed.data(),block);oldRender.pop(oldOutput.data(),block);
        for(std::size_t frame=0;frame<block;++frame){
            if(!directImpulse&&newOutput[frame*2]>.1f)directImpulse=tick*block+frame;
            if(!oldImpulse&&oldOutput[frame*2]>.1f)oldImpulse=tick*block+frame;
        }
    }
    check(directImpulse&&oldImpulse,"impulse reaches both paths");
    check(oldImpulse>directImpulse+1000,"direct mixer removes one full buffering stage");
    const auto emitted=impulseTick*block;
    std::printf("Same1536-frame target: direct impulse delay %.3fms; previous two-queue path %.3fms; saved %.3fms\n",
        double(directImpulse-emitted)/48,double(oldImpulse-emitted)/48,double(oldImpulse-directImpulse)/48);

    // Arbitrary callback sizes remain safe and no worker tick is needed to hear
    // captured local audio. Settings are published from a concurrent controller.
    ListeningMixer callbackMixer(Device::Windows,768);callbackMixer.setSettings(settings);
    std::array<float,2048> constant{},rendered{};constant.fill(.2f);
    callbackMixer.pushLocal(constant.data(),1024);
    for(int i=0;i<5;++i)callbackMixer.pushRemote(constant.data(),1024);
    callbackMixer.render(rendered.data(),513);
    check(rendered[1024]>.39f&&rendered[1025]>.39f,"callback chunks preserve stereo mix");
    settings.globalDeafen=true;callbackMixer.setSettings(settings);callbackMixer.render(rendered.data(),513);
    for(std::size_t i=0;i<1026;++i)check(rendered[i]==0,"deafen immediate in render callback");
    callbackMixer.setTargetFrames(512);callbackMixer.requestReset();callbackMixer.render(rendered.data(),240);
    check(callbackMixer.targetFrames()==512,"target changed without producer touching consumer cursors");
    for(std::size_t i=0;i<480;++i)check(rendered[i]==0,"reset discards stale source queues");

    // Network decoding must wait for the actual consumer flush. Otherwise a
    // worker can advance its packet timeline, enqueue fresh audio, and have the
    // next headphone callback throw that fresh audio away with the old queue.
    ListeningMixer resetMixer(Device::Windows,480);Settings resetSettings;
    resetMixer.setSettings(resetSettings);resetMixer.pushRemote(constant.data(),1024);
    resetMixer.requestReset(ListeningMixer::Remote);
    check(resetMixer.remoteResetPending(),"remote reset requires consumer acknowledgement");
    check(resetMixer.pushRemote(constant.data(),240)==0,"fresh packets cannot enter pending flush");
    resetMixer.render(rendered.data(),240);
    check(!resetMixer.remoteResetPending(),"render acknowledges actual flush");
    for(int i=0;i<5;++i)check(resetMixer.pushRemote(constant.data(),1024)==1024,"fresh packet accepted after acknowledgement");
    resetMixer.render(rendered.data(),240);
    check(rendered[478]>.19f,"acknowledged reset retains fresh network audio");
    check(resetMixer.targetFrames()==480&&resetMixer.remoteTargetFrames()==4800,"network reserve does not add local latency");

    // A 5 ms sender continues while Wi-Fi delivers sixteen packets together
    // every 80 ms. Reverse each burst to exercise reordering, and decode at most
    // twelve packets per worker tick as the runtime does. Test actual continuous
    // output on both remote paths and a local impulse during the receive gap.
    ListeningMixer wifiMixer(Device::Windows,480);wifiMixer.setSettings(Settings{});
    AdaptiveAudioRing<8192> wifiMicrophone(4800);
    net::JitterBuffer wifiPackets(32,4);
    std::array<float,480> localSignal{},remoteSignal{},wifiOutput{},micOutput{};
    for(std::size_t n=0;n<240;++n)remoteSignal[n*2+1]=.2f;
    std::vector<net::Packet> pendingPackets;
    uint64_t expectedTime=0;std::size_t localImpulseFrame=0;
    constexpr std::size_t localImpulseTick=105;
    for(std::size_t tick=0;tick<300;++tick){
        net::Packet packet;packet.kind=net::Kind::Audio;packet.sample_time=tick*240;
        pendingPackets.push_back(std::move(packet));
        const bool burstPeriod=tick>=100&&tick<228;
        if(!burstPeriod||(tick-100)%16==15){
            for(auto it=pendingPackets.rbegin();it!=pendingPackets.rend();++it)
                check(wifiPackets.push(std::move(*it)),"Wi-Fi burst packet accepted without reanchor");
            pendingPackets.clear();
        }
        for(unsigned n=0;n<12&&wifiPackets.has_next();++n){
            auto received=wifiPackets.pop();
            check(received&&received->sample_time==expectedTime,"burst remains in exact media order");
            expectedTime+=240;
            check(wifiMixer.pushRemote(remoteSignal.data(),240)==240,"remote playback burst fits reservoir");
            check(wifiMicrophone.push(remoteSignal.data(),240)==240,"remote microphone burst fits reservoir");
        }
        localSignal.fill(0);
        if(tick==localImpulseTick)localSignal[0]=.8f;
        wifiMixer.pushLocal(localSignal.data(),240);
        wifiMixer.render(wifiOutput.data(),240);
        const auto micFrames=wifiMicrophone.pop(micOutput.data(),240);
        for(std::size_t n=0;n<240;++n){
            if(!localImpulseFrame&&wifiOutput[n*2]>.1f)localImpulseFrame=tick*240+n;
            if(tick>=30){
                check(std::abs(wifiOutput[n*2+1]-.2f)<1e-5,"80 ms receive bursts keep remote playback continuous");
                check(micFrames==240&&std::abs(micOutput[n*2+1]-.2f)<1e-5,"80 ms receive bursts keep remote microphone continuous");
            }
        }
    }
    check(localImpulseFrame>=localImpulseTick*240&&localImpulseFrame<=localImpulseTick*240+720,
        "local impulse stays within 15 ms while network audio has a 100 ms reserve");
    check(!wifiPackets.discontinuity_count(),"sixteen-packet receive bursts never reset media timeline");
    check(!wifiMicrophone.underrun.load()&&!wifiMicrophone.dropped.load(),"remote microphone has no underrun or overflow");
    std::printf("80 ms receive bursts: continuous remote playback/mic; local impulse delay %.3fms\n",
        double(localImpulseFrame-localImpulseTick*240)/48);

    // Exercise the auto sizing rule across108 callback/clock combinations.
    // Time is simulated in source frames; capture and render callbacks run on
    // separate clocks with a deliberately nonaligned17-frame initial phase.
    std::array<float,4096> batchedInput{},batchedOutput{};batchedInput.fill(.2f);
    for(std::size_t captureFrames:{128,240,256,512,1024,1536})
    for(std::size_t renderFrames:{128,240,256,512,1024,1536})
    for(double ppm:{-500.,0.,500.}) {
        ListeningMixer batched(Device::Windows,std::max(captureFrames,renderFrames)+240);
        Settings clean;batched.setSettings(clean);double captureTime=0,renderTime=17;
        while(std::min(captureTime,renderTime)<48000*5.) {
            if(captureTime<=renderTime){
                batched.pushLocal(batchedInput.data(),captureFrames);
                captureTime+=double(captureFrames)/(1+ppm*1e-6);
            } else {
                batched.render(batchedOutput.data(),renderFrames);
                if(renderTime>48000)for(std::size_t n=0;n<renderFrames*2;++n)
                    check(std::abs(batchedOutput[n]-.2f)<1e-5,"callback batching preserves continuous constant audio");
                renderTime+=renderFrames;
            }
        }
        check(!batched.localDrops(),"callback sizing avoids overflow");
    }
    std::puts("108 callback/clock combinations passed without steady-state missing samples or overflow");

    ListeningMixer concurrent(Device::Windows,480);std::atomic<bool> stop=false;
    std::thread producer([&]{std::array<float,480> samples{};samples.fill(.1f);while(!stop){concurrent.pushLocal(samples.data(),240);concurrent.pushRemote(samples.data(),240);}});
    std::thread controller([&]{for(int i=0;i<10000;++i){Settings s;s.routes[0].volume=(i&1)?.25f:1;concurrent.setSettings(s);if(i%20==0)concurrent.requestReset();if(i%100==0)concurrent.setTargetFrames((i&100)?480:768);}});
    for(int i=0;i<10000;++i){concurrent.render(rendered.data(),240);for(std::size_t n=0;n<480;++n)check(std::isfinite(rendered[n])&&std::abs(rendered[n])<=.201f,"bounded concurrent output");}
    stop=true;producer.join();controller.join();
    std::puts("listening mixer tests passed");
}
