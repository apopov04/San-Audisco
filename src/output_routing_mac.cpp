#include "bridge/output_routing.hpp"
#include <CoreAudio/CoreAudio.h>
#include <CoreFoundation/CoreFoundation.h>
#include <stdexcept>
#include <vector>
namespace bridge::routing {
namespace {
void check(OSStatus s){if(s!=noErr)throw std::runtime_error("macOS output selection failed ("+std::to_string(s)+")");}
AudioObjectPropertyAddress property(AudioObjectPropertySelector s){return {s,kAudioObjectPropertyScopeGlobal,kAudioObjectPropertyElementMain};}
std::string uid(AudioDeviceID id){
 auto a=property(kAudioDevicePropertyDeviceUID);CFStringRef value=nullptr;UInt32 size=sizeof value;
 check(AudioObjectGetPropertyData(id,&a,0,nullptr,&size,&value));
 char text[4096]{};bool ok=value&&CFStringGetCString(value,text,sizeof text,kCFStringEncodingUTF8);if(value)CFRelease(value);
 if(!ok)throw std::runtime_error("Cannot read output device identity");return text;
}
class MacBackend:public Backend {
 bool input_;
public:
 explicit MacBackend(bool input):input_(input){}
 std::map<int,std::string> defaults()override{
  // Media only: leave macOS alert sounds at the user's existing selection.
  auto a=property(input_?kAudioHardwarePropertyDefaultInputDevice:kAudioHardwarePropertyDefaultOutputDevice);AudioDeviceID id=0;UInt32 size=sizeof id;
  check(AudioObjectGetPropertyData(kAudioObjectSystemObject,&a,0,nullptr,&size,&id));
  if(!id)return {};return {{0,uid(id)}};
 }
 void set(int role,const std::string& endpoint)override{
  if(role!=0)throw std::runtime_error("Invalid macOS output role");
  auto list=property(kAudioHardwarePropertyDevices);UInt32 size=0;check(AudioObjectGetPropertyDataSize(kAudioObjectSystemObject,&list,0,nullptr,&size));
  std::vector<AudioDeviceID> ids(size/sizeof(AudioDeviceID));check(AudioObjectGetPropertyData(kAudioObjectSystemObject,&list,0,nullptr,&size,ids.data()));
  for(auto id:ids)if(uid(id)==endpoint){auto a=property(input_?kAudioHardwarePropertyDefaultInputDevice:kAudioHardwarePropertyDefaultOutputDevice);check(AudioObjectSetPropertyData(kAudioObjectSystemObject,&a,0,nullptr,sizeof id,&id));return;}
  throw std::runtime_error("Previous output is unavailable; reconnect it to restore playback");
 }
};
}
std::unique_ptr<Backend> system_backend(bool input){return std::make_unique<MacBackend>(input);}
}
