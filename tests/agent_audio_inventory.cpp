// Test inventory only. Never linked to the installed agent.
#include "bridge/audio.hpp"
namespace bridge {
std::vector<AudioDevice> enumerate_audio_devices(){return {
 {"win-playback","CABLE Output (VB-Audio Virtual Cable)",2,0,48000},
 {"win-mic-sink","Hi-Fi Cable Input (VB-Audio Hi-Fi Cable)",0,2,48000},
 {"win-mic","Test Windows Microphone",2,0,48000},
 {"win-headphones","Test Windows Headphones",0,2,48000},
 {"mac-playback","BlackHole 2ch",2,0,48000},
 {"mac-mic-sink","BlackHole 16ch",0,2,48000},
 {"mac-mic","Test Mac Microphone",2,0,48000},
 {"mac-headphones","Test Mac Headphones",0,2,48000}
 };}
}
