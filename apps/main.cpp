#include "bridge/audio.hpp"
#include "bridge/engine.hpp"
#include "bridge/transport.hpp"
#include "bridge/credentials.hpp"
#include "bridge/adaptive_ring.hpp"
#include "bridge/control.hpp"
#include "bridge/listening_mixer.hpp"
#include <fstream>
#include <iostream>
#include <map>
#include <thread>
#include <sstream>
#include <atomic>
#include <filesystem>
#include <chrono>
#include <cerrno>
#include <vector>
#include <string_view>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <conio.h>
#include <shellapi.h>
#else
#include <poll.h>
#include <unistd.h>
#include <fcntl.h>
#endif
using namespace bridge;
namespace {
std::filesystem::path utf8_path(std::string_view value) { return std::filesystem::path(std::u8string(value.begin(),value.end())); }
void wipe(void* memory,std::size_t size) noexcept {
    auto* p=static_cast<volatile unsigned char*>(memory);while(size--)*p++=0;
}
struct SecretKey {net::Key value{};~SecretKey(){wipe(value.data(),value.size());}};
net::Key read_key_file(const std::filesystem::path& path) {
    std::ifstream file(path,std::ios::binary);if(!file)throw std::runtime_error("Cannot open pairing key file");
    struct SecretText {std::array<char,129> bytes{};~SecretText(){wipe(bytes.data(),bytes.size());}} text;
    file.read(text.bytes.data(),text.bytes.size());const auto count=static_cast<std::size_t>(file.gcount());
    if(file.bad()||count>128)throw std::runtime_error("Pairing key file exceeds 128 bytes or cannot be read");
    auto whitespace=[](char c){return c==' '||c=='\t'||c=='\r'||c=='\n';};
    std::size_t begin=0,end=count;while(begin<end&&whitespace(text.bytes[begin]))++begin;while(end>begin&&whitespace(text.bytes[end-1]))--end;
    if(end-begin!=64)throw std::runtime_error("Pairing key file must contain exactly 64 hexadecimal characters");
    net::Key key{};
    auto nibble=[](char c)->int {if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;if(c>='A'&&c<='F')return c-'A'+10;return -1;};
    for(std::size_t i=0;i<32;++i){int hi=nibble(text.bytes[begin+2*i]),lo=nibble(text.bytes[begin+2*i+1]);if(hi<0||lo<0){wipe(key.data(),key.size());throw std::runtime_error("Pairing key file contains non-hexadecimal characters");}key[i]=static_cast<uint8_t>((hi<<4)|lo);}return key;
}
void generate_key_file(const std::filesystem::path& path) {
    SecretKey key{net::random_key()};auto hex=net::key_to_hex(key.value);hex+='\n';
    struct WipeText {std::string& value;~WipeText(){wipe(value.data(),value.size());}} cleanup{hex};
#ifdef _WIN32
    HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot create key file; existing files are never overwritten");
    DWORD written=0;const bool ok=WriteFile(file,hex.data(),static_cast<DWORD>(hex.size()),&written,nullptr)&&written==hex.size()&&FlushFileBuffers(file);CloseHandle(file);
    if(!ok){DeleteFileW(path.c_str());throw std::runtime_error("Cannot write pairing key file");}
#else
    const int fd=::open(path.c_str(),O_WRONLY|O_CREAT|O_EXCL,0600);
    if(fd<0)throw std::runtime_error("Cannot create key file; existing files are never overwritten");
    std::size_t offset=0;bool ok=true;while(offset<hex.size()){const auto n=::write(fd,hex.data()+offset,hex.size()-offset);if(n<0&&errno==EINTR)continue;if(n<=0){ok=false;break;}offset+=static_cast<std::size_t>(n);}if(ok&&::fsync(fd)!=0)ok=false;::close(fd);
    if(!ok){::unlink(path.c_str());throw std::runtime_error("Cannot write pairing key file");}
#endif
}
std::string quote(const std::string& s) {
    std::string o="\"";const char* hex="0123456789abcdef";
    for(unsigned char c:s){if(c=='"'||c=='\\'){o+='\\';o+=char(c);}else if(c<32){o+="\\u00";o+=hex[c>>4];o+=hex[c&15];}else o+=char(c);}return o+'"';
}
std::map<std::string,std::string> read_config(const char* path) {
    std::ifstream f(utf8_path(path));if(!f)throw std::runtime_error("Cannot open configuration");
    std::map<std::string,std::string> c;std::string line;
    while(std::getline(f,line)){if(!line.empty()&&line.back()=='\r')line.pop_back();auto x=line.find('=');if(x!=std::string::npos)c[line.substr(0,x)]=line.substr(x+1);}return c;
}
uint16_t port(const std::string& s){std::size_t used=0;auto p=std::stoul(s,&used);if(used!=s.size()||p==0||p>65535)throw std::runtime_error("Invalid network port");return static_cast<uint16_t>(p);}
std::size_t buffer_frames(const std::string& s){
    if(s.empty()||!std::all_of(s.begin(),s.end(),[](char c){return c>='0'&&c<='9';}))throw std::runtime_error("buffer_frames must be an integer from 256 to 2048");
    auto n=std::stoul(s);if(n<256||n>2048)throw std::runtime_error("buffer_frames must be from 256 to 2048");return n;
}
// Poll stdin without blocking the audio worker on getline after an audio error.
// GUI supplies a pipe; native terminals and redirected files are also supported.
class Commands {
    std::string pending_;bool dropping_=false;
public:
    bool ended=false;
    template<class Callback> void poll(Callback callback) {
        char bytes[512];std::size_t count=0;
#ifdef _WIN32
        HANDLE handle=GetStdHandle(STD_INPUT_HANDLE);DWORD mode=0;
        if(GetConsoleMode(handle,&mode)) {
            while(_kbhit()&&count<sizeof(bytes)){int c=_getch();if(c==0||c==224){if(_kbhit())_getch();continue;}if(c==3){ended=true;return;}if(c=='\r')c='\n';bytes[count++]=char(c);std::cout<<char(c)<<std::flush;}
        } else {
            DWORD available=0,got=0;
            if(GetFileType(handle)==FILE_TYPE_PIPE){if(!PeekNamedPipe(handle,nullptr,0,nullptr,&available,nullptr)){ended=true;return;}if(!available)return;}
            else available=sizeof(bytes);
            if(!ReadFile(handle,bytes,std::min<DWORD>(sizeof(bytes),available),&got,nullptr)){ended=true;return;}
            count=got;if(!count)ended=true;
        }
#else
        pollfd fd{STDIN_FILENO,POLLIN|POLLHUP,0};int ready=::poll(&fd,1,0);
        if(ready<0){if(errno!=EINTR)ended=true;return;}if(!ready)return;
        auto got=::read(STDIN_FILENO,bytes,sizeof(bytes));if(got<0){if(errno!=EINTR&&errno!=EAGAIN)ended=true;return;}count=std::size_t(got);if(!got)ended=true;
#endif
        for(std::size_t i=0;i<count;++i){char c=bytes[i];if(c=='\r')continue;
            if(c=='\n'){if(!dropping_)callback(pending_);pending_.clear();dropping_=false;}
            else if(c=='\b'||c==127){if(!pending_.empty())pending_.pop_back();}
            else if(pending_.size()<100&&!dropping_)pending_+=c;
            else {pending_.clear();dropping_=true;}
        }
        if(ended&&!pending_.empty()&&!dropping_){callback(pending_);pending_.clear();}
    }
};
}
int main(int argc,char** argv) {try {
#ifdef _WIN32
    // The CRT narrow argv uses the active Windows code page. Decode the
    // original UTF-16 command line so config and pairing-key paths remain UTF-8.
    int wideCount=0;
    struct WideArguments { LPWSTR* value; ~WideArguments(){if(value)LocalFree(value);} };
    WideArguments wideArgs{CommandLineToArgvW(GetCommandLineW(),&wideCount)};
    if(!wideArgs.value)throw std::runtime_error("Cannot read Unicode command line");
    std::vector<std::string> utf8Arguments;utf8Arguments.reserve(wideCount);
    for(int i=0;i<wideCount;++i){
        int size=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,wideArgs.value[i],-1,nullptr,0,nullptr,nullptr);
        if(size<=0)throw std::runtime_error("Invalid Unicode command-line argument");
        std::string value(static_cast<std::size_t>(size),'\0');
        if(!WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,wideArgs.value[i],-1,value.data(),size,nullptr,nullptr))throw std::runtime_error("Cannot convert command-line argument");
        value.pop_back();utf8Arguments.push_back(std::move(value));
    }
    std::vector<char*> pointers;pointers.reserve(utf8Arguments.size()+1);
    for(auto& value:utf8Arguments)pointers.push_back(value.data());pointers.push_back(nullptr);
    argc=wideCount;argv=pointers.data();
#endif
    if(argc==2&&std::string(argv[1])=="devices") {
        for(auto& d:enumerate_audio_devices())std::cout<<"{\"id\":"<<quote(d.id)<<",\"name\":"<<quote(d.name)<<",\"inputs\":"<<d.input_channels<<",\"outputs\":"<<d.output_channels<<",\"rate\":"<<d.sample_rate<<"}\n";return 0;
    }
    if(argc==3&&std::string(argv[1])=="keygen") {
        generate_key_file(utf8_path(argv[2]));
        std::cout<<"Pairing key saved. Transfer privately to other computer.\n";return 0;
    }
    if(argc==4&&std::string(argv[1])=="key-import") {
        credentials::validate_id(argv[2]);SecretKey key{read_key_file(utf8_path(argv[3]))};
        credentials::store_key(argv[2],key.value);
        std::cout<<"Pairing key imported into protected storage. The transfer file is unchanged.\n";return 0;
    }
    if(argc==3&&std::string(argv[1])=="key-delete") {
        bool removed=credentials::delete_key(argv[2]);
        std::cout<<(removed?"Pairing credential deleted.\n":"Pairing credential was not present.\n");return 0;
    }
    if(argc!=3||std::string(argv[1])!="run") {
        std::cout<<"AudioBridge: devices | keygen FILE | key-import ID FILE | key-delete ID | run CONFIG\nCommands on stdin: primary 0|1, mic_source 0|1, win_volume 0..1, mac_volume 0..1, win_mute 0|1, mac_mute 0|1, win_deafen 0|1, mac_deafen 0|1, mute 0|1, deafen 0|1, balance -1..1, quit\n";return 0;
    }
    auto config=read_config(argv[2]);auto required=[&](const char* k){if(config[k].empty())throw std::runtime_error(std::string("Missing config: ")+k);return config[k];};
    if(required("device")!="windows"&&required("device")!="mac")throw std::runtime_error("device must be windows or mac");
    Device local=required("device")=="windows"?Device::Windows:Device::Mac;
    const auto playbackId=required("playback_input"),micOutputId=required("microphone_output");
    // A secondary computer need not have headphones or a physical microphone.
    // Missing routes remain explicit: selecting that role cannot use a default device.
    const auto headphoneId=config["headphones_output"],physicalMicId=config["physical_microphone"];
    if(playbackId==micOutputId)throw std::runtime_error("Playback and microphone paths must use separate virtual cables");
    SecretKey key;
    if(config.contains("key_id")&&!config["key_id"].empty())key.value=credentials::load_key(config["key_id"]);
    else key.value=read_key_file(utf8_path(required("key_file")));
    net::Config nc;nc.key=key.value;nc.peer_address=required("peer");
    if(config.contains("port"))nc.bind_port=port(config["port"]);if(config.contains("peer_port"))nc.peer_port=port(config["peer_port"]);
    net::Transport network(nc);wipe(nc.key.data(),nc.key.size());wipe(key.value.data(),key.value.size());
    Settings settings;settings.globalMute=true;
    // 32ms target covers typical 1024-frame device callbacks plus worker phase.
    // This is deliberately conservative until each real device is benchmarked.
    const auto hardwareBufferFrames=config.contains("buffer_frames")?buffer_frames(config["buffer_frames"]):1536;
    AdaptiveAudioRing<8192> playbackIn{hardwareBufferFrames},micIn{hardwareBufferFrames},micOut{hardwareBufferFrames};
    ListeningMixer listeningMixer(local,hardwareBufferFrames);
    AdaptiveAudioRing<8192> networkMicrophone{720};
    std::atomic<bool> microphoneBlocked{true},listeningBlocked{true};
    AudioStream playback,microphone,listen,micSink;
    playback.open(playbackId,true,[&](float* p,size_t n){playbackIn.push(p,n);listeningMixer.pushLocal(p,n);});
    micSink.open(micOutputId,false,[&](float* p,size_t n){micOut.pop(p,n);if(microphoneBlocked.load())std::fill(p,p+n*2,0.f);});
    playback.start();micSink.start();
    Engine engine(local);net::JitterBuffer jp,jm;net::AudioDecoder decoder;
    std::array<float,480> lp{},lm{},rm{},localMic{},remoteMic{},txp{},txm{};
    std::array<int16_t,480> wire{};
    uint64_t tick=0,revision=0,remoteRevision=0,request=0,appliedRequest=0,generation=0,nextSampleTime=0,playbackDiscontinuities=0,microphoneDiscontinuities=0;
    std::map<std::string,float> pending;
    bool wasReady=false,synchronized=false,run=true,previousMicBlocked=true,previousRemoteMicEnabled=false;
    Settings previous=settings;Commands commands;
    auto next=std::chrono::steady_clock::now(),retryHeadphones=next,retryMicrophone=next,retryPlayback=next,retryMicSink=next;
    const auto mediaEpoch=next;
    auto resumeMicSink=[&]{
        if(micSink.running()||std::chrono::steady_clock::now()<retryMicSink)return;
        microphoneBlocked=true;
        try {
            micSink.close();micOut.discard();
            micSink.open(micOutputId,false,[&](float* p,size_t n){micOut.pop(p,n);if(microphoneBlocked.load())std::fill(p,p+n*2,0.f);});
            micSink.start();
        } catch(const std::exception& e){micSink.close();std::cerr<<"Microphone destination unavailable: "<<e.what()<<'\n';retryMicSink=std::chrono::steady_clock::now()+std::chrono::seconds(1);}
    };
    auto resetRemote=[&]{
        microphoneBlocked=true;settings.globalMute=true;synchronized=false;
        revision=remoteRevision=request=appliedRequest=0;pending.clear();
        jp.reset();jm.reset();decoder.reset();
        listeningMixer.requestReset(ListeningMixer::Remote);networkMicrophone.discard();
        // Producer is this worker, so only stop the output consumer before flush.
        micSink.stop();micOut.discard();resumeMicSink();previousMicBlocked=true;previousRemoteMicEnabled=false;
    };
    while(run) {
        auto began=std::chrono::steady_clock::now();
        // Sender stalls must leave a hole in media time. Incrementing only a
        // worker tick would make resumed packets permanently older than the
        // receiver's PLC clock. Keep timestamps aligned to 240-frame packets
        // and strictly increasing even during brief scheduler catch-up bursts.
        const auto elapsedMs=std::chrono::duration_cast<std::chrono::milliseconds>(began-mediaEpoch).count();
        const auto sampleTime=std::max(nextSampleTime,uint64_t(elapsedMs/5)*240);
        nextSampleTime=sampleTime+240;
        commands.poll([&](const std::string& line){
            if(line=="quit"){run=false;return;}
            Settings candidate=settings;std::map<std::string,float> changes;
            if(!control::parse(candidate,line,&changes)){std::cerr<<"Ignored invalid command\n";return;}
            if(local==Device::Windows){settings=candidate;++revision;}
            else {for(auto&[k,v]:changes)pending[k]=v;++request;}
        });
        if(!run||commands.ended)break;
        for(int count=0;count<128;++count) {
            auto packet=network.poll();
            if(network.session_generation()!=generation){generation=network.session_generation();resetRemote();}
            if(!packet)break;
            if(packet->kind==net::Kind::Audio) {
                if(packet->stream==net::Stream::Playback)jp.push(std::move(*packet));
                else if(packet->stream==net::Stream::Microphone)jm.push(std::move(*packet));
            } else if(packet->kind==net::Kind::Control) {
                std::string message(packet->payload.begin(),packet->payload.end());std::istringstream in(message);
                char type=0;uint64_t rev=0,ack=0;if(!(in>>type>>rev>>ack))continue;
                std::string body((std::istreambuf_iterator<char>(in)),{});
                if(local==Device::Windows&&type=='C'&&rev>appliedRequest) {
                    Settings candidate=settings;if(!control::parse(candidate,body))continue;
                    settings=candidate;appliedRequest=rev;++revision;
                } else if(local==Device::Mac&&type=='S'&&rev>=remoteRevision) {
                    Settings candidate=settings;if(!control::parse(candidate,body))continue;
                    settings=candidate;remoteRevision=rev;synchronized=true;
                    if(ack==request)pending.clear();
                } else if(local==Device::Windows&&type=='A'&&rev==revision)synchronized=true;
            }
        }
        const bool ready=network.ready();
        if(!ready&&wasReady)resetRemote();wasReady=ready;
        if(!playback.running()&&began>=retryPlayback){
            try {playback.close();playbackIn.discard();listeningMixer.requestReset(ListeningMixer::Local);playback.open(playbackId,true,[&](float* p,size_t n){playbackIn.push(p,n);listeningMixer.pushLocal(p,n);});playback.start();}
            catch(const std::exception& e){playback.close();std::cerr<<"Playback source unavailable: "<<e.what()<<'\n';retryPlayback=began+std::chrono::seconds(1);}
        }
        resumeMicSink();
        const bool roleChanged=previous.primary!=settings.primary||previous.microphoneSource!=settings.microphoneSource;
        if(roleChanged) {
            microphoneBlocked=true;listeningBlocked=true;microphone.close();listen.close();micSink.stop();
            micIn.discard();listeningMixer.requestReset();micOut.discard();jp.reset();jm.reset();decoder.reset();
            listeningMixer.requestReset(ListeningMixer::Remote);networkMicrophone.discard();
            // Both peers require explicit unmute after a route handover.
            settings.globalMute=true;if(local==Device::Windows)++revision;
            previousMicBlocked=true;previousRemoteMicEnabled=false;resumeMicSink();retryHeadphones=retryMicrophone=began;
        }
        previous=settings;
        if(settings.primary==local&&!listen.running()&&began>=retryHeadphones) {
            try {if(headphoneId.empty())throw std::runtime_error("No headphones_output configured on the selected primary device");listen.open(headphoneId,false,[&](float* p,size_t n){listeningMixer.render(p,n);if(listeningBlocked.load())std::fill(p,p+n*2,0.f);});listen.start();}
            catch(const std::exception& e){listen.close();std::cerr<<"Headphones unavailable: "<<e.what()<<'\n';retryHeadphones=began+std::chrono::seconds(1);}
        } else if(settings.primary!=local&&listen.running())listen.close();
        if(!config.contains("buffer_frames")&&listen.running()){
            const auto captureFrames=playback.callback_frames_hint(),renderFrames=listen.callback_frames_hint();
            const auto target=captureFrames&&renderFrames?std::clamp(std::max(captureFrames,renderFrames)+240,std::size_t(256),std::size_t(4096)):std::size_t(1536);
            listeningMixer.setTargetFrames(target);
        }
        if(settings.microphoneSource==local&&!microphone.running()&&began>=retryMicrophone) {
            try {if(physicalMicId.empty())throw std::runtime_error("No physical_microphone configured on the selected microphone source");microphone.open(physicalMicId,true,[&](float* p,size_t n){micIn.push(p,n);});microphone.start();}
            catch(const std::exception& e){microphone.close();std::cerr<<"Microphone unavailable: "<<e.what()<<'\n';retryMicrophone=began+std::chrono::seconds(1);}
        } else if(settings.microphoneSource!=local&&microphone.running())microphone.close();
        if(tick%20==0&&ready) {
            std::string message;
            if(local==Device::Windows)message="S "+std::to_string(revision)+" "+std::to_string(appliedRequest)+"\n"+control::snapshot(settings);
            else {
                if(synchronized){std::string ack="A "+std::to_string(remoteRevision)+" 0\n";network.send_control(std::span(reinterpret_cast<const uint8_t*>(ack.data()),ack.size()));}
                if(!pending.empty())message="C "+std::to_string(request)+" 0\n"+control::serialize(pending);
            }
            if(!message.empty())network.send_control(std::span(reinterpret_cast<const uint8_t*>(message.data()),message.size()));
        }
        playbackIn.pop(lp.data(),240);micIn.pop(lm.data(),240);
        auto receive=[&](net::JitterBuffer& jitter,net::Stream stream,uint64_t& seenDiscontinuities){
            const bool isPlayback=stream==net::Stream::Playback;
            if(jitter.discontinuity_count()!=seenDiscontinuities){
                seenDiscontinuities=jitter.discontinuity_count();
                if(isPlayback)listeningMixer.requestReset(ListeningMixer::Remote);else networkMicrophone.discard();
                decoder.reset(stream);
            }
            // Do not advance the media timeline into a queue the audio callback
            // has not cleared yet; otherwise its next reset destroys fresh data.
            if(isPlayback&&listeningMixer.remoteResetPending())return;
            auto append=[&](std::optional<net::Packet> packet){
                std::array<int16_t,480> samples{};
                try {samples=packet?decoder.decode(*packet):decoder.conceal(stream);}
                catch(const std::exception&){samples=decoder.conceal(stream);}
                std::array<float,480> decoded{};
                for(size_t i=0;i<decoded.size();++i)decoded[i]=samples[i]/32768.f;
                if(isPlayback)listeningMixer.pushRemote(decoded.data(),240);else networkMicrophone.push(decoded.data(),240);
            };
            for(unsigned i=0;i<12&&jitter.has_next();++i)append(jitter.pop());
            // The headphone callback consumes decoded playback on its own
            // clock; retain its remote reserve instead of chasing local prefill.
            const auto available=isPlayback?listeningMixer.remoteAvailable():networkMicrophone.available();
            const auto deadlineFrames=isPlayback?listeningMixer.targetFrames():480;
            // Only conceal a known gap (a later packet has arrived), near its
            // playout deadline. Filling prebuffer with speculative PLC advances
            // the expected timestamp ahead of live packets and rejects them.
            if(jitter.primed()&&jitter.size()>0&&available<deadlineFrames)append(jitter.pop());
        };
        receive(jp,net::Stream::Playback,playbackDiscontinuities);receive(jm,net::Stream::Microphone,microphoneDiscontinuities);
        networkMicrophone.pop(rm.data(),240);
        Settings effective=settings;
        if(!ready||!synchronized||!micSink.running()||(settings.primary==local&&!listen.running())||(settings.microphoneSource==local&&!microphone.running()))effective.globalMute=true;
        const bool micBlocked=effective.globalMute||effective.globalDeafen||effective.routes[int(local)].micMuted||effective.routes[int(local)].deafened;
        const auto& remoteRoute=effective.routes[int(other(local))];
        const bool remoteMicEnabled=settings.microphoneSource==local&&!effective.globalMute&&!effective.globalDeafen&&!remoteRoute.micMuted&&!remoteRoute.deafened;
        if(micBlocked)microphoneBlocked=true;
        // Drop captured/received speech as well as rendered speech before either
        // destination reopens. One skipped block is preferable to old speech.
        if((!micBlocked&&previousMicBlocked)||(remoteMicEnabled&&!previousRemoteMicEnabled)) {
            microphoneBlocked=true;const bool restart= microphone.running();microphone.stop();micSink.stop();
            micIn.discard();micOut.discard();networkMicrophone.discard();jm.reset();lm.fill(0);rm.fill(0);
            if(restart){
                try {microphone.start();}
                catch(const std::exception& e){microphone.close();std::cerr<<"Microphone restart unavailable: "<<e.what()<<'\n';retryMicrophone=began+std::chrono::seconds(1);}
            }
            resumeMicSink();
        }
        previousMicBlocked=micBlocked;previousRemoteMicEnabled=remoteMicEnabled;
        engine.setSettings(effective);listeningMixer.setSettings(effective);
        engine.process({lp.data(),nullptr,lm.data(),rm.data()},{nullptr,localMic.data(),remoteMic.data(),txp.data(),txm.data()},240);
        micOut.push(localMic.data(),240);
        listeningBlocked=effective.globalDeafen;microphoneBlocked=micBlocked;
        auto send=[&](net::Stream stream,const std::array<float,480>& audio){
            for(size_t i=0;i<480;++i)wire[i]=static_cast<int16_t>(std::clamp(audio[i],-1.f,0.999969f)*32768.f);
            network.send_audio(stream,wire,sampleTime);
        };
        if(settings.primary!=local)send(net::Stream::Playback,txp);
        if(settings.microphoneSource==local)send(net::Stream::Microphone,txm);
        if(tick%200==0) {
            std::cout<<"{\"connected\":"<<(ready?"true":"false")<<",\"synchronized\":"<<(synchronized?"true":"false")<<",\"primary\":"<<int(settings.primary)
                <<",\"mic_source\":"<<int(settings.microphoneSource)<<",\"muted\":"<<(effective.globalMute?"true":"false")<<",\"deafen\":"<<(settings.globalDeafen?"true":"false")
                <<",\"balance\":"<<settings.balance<<",\"master\":"<<settings.masterVolume;
            for(int i=0;i<2;++i){const auto& r=settings.routes[i];const char* prefix=i==0?"win":"mac";
                std::cout<<",\""<<prefix<<"_mute\":"<<(r.micMuted?"true":"false")<<",\""<<prefix<<"_deafen\":"<<(r.deafened?"true":"false")
                    <<",\""<<prefix<<"_volume\":"<<r.volume<<",\""<<prefix<<"_silence\":"<<(r.silenced?"true":"false");}
            std::cout<<",\"buffer_frames\":"<<hardwareBufferFrames<<",\"listening_buffer_frames\":"<<listeningMixer.targetFrames()<<",\"playback_ready\":"<<(playback.running()?"true":"false")<<",\"mic_sink_ready\":"<<(micSink.running()?"true":"false")
                <<",\"headphones_ready\":"<<(listen.running()?"true":"false")<<",\"microphone_ready\":"<<(microphone.running()?"true":"false")
                <<",\"rejected\":"<<network.rejected_packets()<<",\"drops\":"<<playbackIn.dropped.load()<<",\"settings\":"<<quote(control::snapshot(settings))<<"}"<<std::endl;
        }
        ++tick;next+=std::chrono::milliseconds(5);
        if(next<began-std::chrono::milliseconds(20))next=began+std::chrono::milliseconds(5);
        std::this_thread::sleep_until(next);
    }
    microphoneBlocked=true;listeningBlocked=true;
    microphone.close();listen.close();playback.close();micSink.close();return 0;
} catch(const std::exception& e){std::cerr<<"Audio stopped: "<<e.what()<<std::endl;return 1;}}
