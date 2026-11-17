#pragma once
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <vector>
namespace bridge {
struct AudioDevice {
    std::string id, name;
    unsigned input_channels{}, output_channels{};
    double sample_rate{};
};
std::vector<AudioDevice> enumerate_audio_devices();
// All callbacks use interleaved stereo floats at 48000 Hz. Capture callbacks
// receive samples to read; playback callbacks must fill the supplied buffer.
// Callback must not allocate, block, throw, or call AudioStream methods.
using AudioCallback = std::function<void(float*, std::size_t)>;
class AudioStream {
public:
    AudioStream();
    ~AudioStream();
    AudioStream(const AudioStream&) = delete;
    AudioStream& operator=(const AudioStream&) = delete;
    void open(const std::string& device_id, bool input, AudioCallback callback);
    void start();
    void stop() noexcept;
    void close() noexcept;
    bool running() const noexcept;
    // Estimated maximum callback quantum in 48kHz frames; 0 when unknown.
    std::size_t callback_frames_hint() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
