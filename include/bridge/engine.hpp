#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>

namespace bridge {
enum class Device : unsigned { Windows = 0, Mac = 1 };
constexpr Device other(Device d) noexcept { return d == Device::Windows ? Device::Mac : Device::Windows; }
struct RouteSettings {
    float volume = 1.0f;
    bool micMuted = false;
    bool silenced = false;
    bool deafened = false;
};
struct Settings {
    Device primary = Device::Windows;
    Device microphoneSource = Device::Mac;
    std::array<RouteSettings, 2> routes{};
    bool globalMute = false;
    bool globalDeafen = false;
    float balance = 0.0f; // -1 Windows only; +1 Mac only; center leaves both unchanged.
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

// Single producer / single consumer only. Capacity is in stereo frames.
// Overflow drops newest frames; producer never changes the consumer cursor.
// Consumer discard() is safe while producer runs. It discards only currently
// published samples; during handover first stop the producer, then discard().
template<std::size_t CapacityFrames>
class SpscAudioRing {
    static_assert(CapacityFrames > 0 && (CapacityFrames & (CapacityFrames - 1)) == 0);
public:
    std::size_t push(const float* stereo, std::size_t frames) noexcept {
        const auto w = write_.load(std::memory_order_relaxed);
        const auto r = read_.load(std::memory_order_acquire);
        const auto free = CapacityFrames - (w - r);
        const auto n = frames < free ? frames : free;
        for (std::size_t i = 0; i < n; ++i) {
            const auto p = ((w + i) & (CapacityFrames - 1)) * 2;
            data_[p] = stereo[i * 2]; data_[p + 1] = stereo[i * 2 + 1];
        }
        write_.store(w + n, std::memory_order_release);
        return n;
    }
    // Always initializes the requested output; underrun tail is silence.
    std::size_t pop(float* stereo, std::size_t frames) noexcept {
        const auto r = read_.load(std::memory_order_relaxed);
        const auto w = write_.load(std::memory_order_acquire);
        const auto n = frames < w - r ? frames : w - r;
        for (std::size_t i = 0; i < n; ++i) {
            const auto p = ((r + i) & (CapacityFrames - 1)) * 2;
            stereo[i * 2] = data_[p]; stereo[i * 2 + 1] = data_[p + 1];
        }
        for (std::size_t i = n * 2; i < frames * 2; ++i) stereo[i] = 0.0f;
        read_.store(r + n, std::memory_order_release);
        return n;
    }
    void discard() noexcept { read_.store(write_.load(std::memory_order_acquire), std::memory_order_release); }
private:
    std::array<float, CapacityFrames * 2> data_{};
    alignas(64) std::atomic<std::size_t> write_{0};
    alignas(64) std::atomic<std::size_t> read_{0};
};
} // namespace bridge
