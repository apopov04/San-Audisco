#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>

namespace bridge {
enum class Device : unsigned { A = 0, B = 1 };
constexpr Device other(Device d) noexcept { return d == Device::A ? Device::B : Device::A; }
struct RouteSettings {
    float volume = 1.0f;
    bool micMuted = false;
    bool silenced = false;
    bool deafened = false;
};
struct Settings {
    Device primary = Device::A;
    Device microphoneSource = Device::B;
    std::array<RouteSettings, 2> routes{};
    bool globalMute = false;
    bool globalDeafen = false;
    float balance = 0.0f; // -1 participant A only; +1 participant B only; center leaves both unchanged.
    float masterVolume = 1.0f;
};
// All buffers are interleaved stereo; microphone channels are averaged to mono.
// Null input means silence. Inputs and outputs must not overlap. No allocation in process.
struct InputBlock {
    const float* localPlayback = nullptr;
    const float* remotePlayback = nullptr;
    const float* localMicrophone = nullptr;
    const float* remoteMicrophone = nullptr;
};
struct OutputBlock {
    float* listen = nullptr; // Final listening mix on primary; silence on secondary.
    float* localMicrophone = nullptr; // Feed local virtual microphone; destination gated.
    float* remoteMicrophone = nullptr; // Same as sendMicrophone, optional inspection output.
    float* sendPlayback = nullptr; // Unmixed local playback on secondary only.
    float* sendMicrophone = nullptr; // Physical local mic when this device is source,
                                     // gated by REMOTE destination, never local mute.
};
class Engine {
public:
    explicit Engine(Device local, std::size_t gainRampFrames = 240) noexcept;
    // Control threads only. Serializes concurrent writers, never locks audio thread.
    void setSettings(const Settings& settings);
    // Exactly one processing thread per instance. Bounded settings snapshot attempts.
    void process(const InputBlock&, const OutputBlock&, std::size_t frames) noexcept;
private:
    Device local_;
    Settings snapshot_{};
    std::mutex writerMutex_;
    std::atomic<std::uint64_t> revision_{0};
    std::array<std::atomic<float>, 4> gains_{};
    std::atomic<unsigned> flags_{0};
    std::size_t gainRampFrames_;
    std::array<float, 2> currentGain_{};
    std::array<float, 2> targetGain_{};
    std::array<std::size_t, 2> rampRemaining_{};
    bool wasPrimary_ = false;
    void loadSettings() noexcept;
};

} // namespace bridge
