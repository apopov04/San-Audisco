#include "bridge/engine.hpp"
#include <algorithm>
#include <cmath>

namespace bridge {
namespace {
float finite(float x) noexcept { return std::isfinite(x) ? x : 0.0f; }
float sample(const float* p, std::size_t i) noexcept { return p ? finite(p[i]) : 0.0f; }
float limit(float x) noexcept { return std::clamp(finite(x), -1.0f, 1.0f); }
unsigned index(Device d) noexcept { return d == Device::B ? 1u : 0u; }
unsigned pack(const Settings& s) noexcept {
    unsigned f = index(s.primary) | (index(s.microphoneSource) << 1);
    f |= unsigned(s.globalMute) << 2; f |= unsigned(s.globalDeafen) << 3;
    for (unsigned i = 0; i < 2; ++i) {
        f |= unsigned(s.routes[i].micMuted) << (4 + 3 * i);
        f |= unsigned(s.routes[i].silenced) << (5 + 3 * i);
        f |= unsigned(s.routes[i].deafened) << (6 + 3 * i);
    }
    return f;
}
}
static_assert(std::atomic<float>::is_always_lock_free);
static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
Engine::Engine(Device local, std::size_t gainRampFrames) noexcept : local_(local), gainRampFrames_(gainRampFrames) {
    gains_[0].store(1); gains_[1].store(1); gains_[2].store(0); gains_[3].store(1);
    flags_.store(pack(snapshot_));
}
void Engine::setSettings(const Settings& s) {
    std::lock_guard lock(writerMutex_);
    // Sequential consistency keeps the seqlock ordering valid across all atomic fields.
    revision_.fetch_add(1);
    gains_[0].store(std::clamp(finite(s.routes[0].volume), 0.0f, 1.0f));
    gains_[1].store(std::clamp(finite(s.routes[1].volume), 0.0f, 1.0f));
    gains_[2].store(std::clamp(finite(s.balance), -1.0f, 1.0f));
    gains_[3].store(std::clamp(finite(s.masterVolume), 0.0f, 1.0f));
    flags_.store(pack(s));
    revision_.fetch_add(1);
}
void Engine::loadSettings() noexcept {
    for (int attempt = 0; attempt < 2; ++attempt) {
        const auto begin = revision_.load();
        if (begin & 1) continue;
        Settings s;
        s.routes[0].volume = gains_[0].load(); s.routes[1].volume = gains_[1].load();
        s.balance = gains_[2].load(); s.masterVolume = gains_[3].load();
        const auto f = flags_.load();
        if (revision_.load() != begin) continue;
        s.primary = (f & 1) ? Device::B : Device::A;
        s.microphoneSource = (f & 2) ? Device::B : Device::A;
        s.globalMute = f & 4; s.globalDeafen = f & 8;
        for (unsigned i = 0; i < 2; ++i) {
            s.routes[i].micMuted = f & (1u << (4 + 3 * i));
            s.routes[i].silenced = f & (1u << (5 + 3 * i));
            s.routes[i].deafened = f & (1u << (6 + 3 * i));
        }
        snapshot_ = s; return;
    }
}
void Engine::process(const InputBlock& in, const OutputBlock& out, std::size_t frames) noexcept {
    loadSettings();
    const auto& s = snapshot_;
    const auto li = index(local_), ri = 1 - li;
    float gain[2]; bool micEnabled[2];
    for (unsigned d = 0; d < 2; ++d) {
        const auto& route = s.routes[d];
        const float balance = d == 0 ? 1.0f - std::max(0.0f, s.balance) : 1.0f + std::min(0.0f, s.balance);
        gain[d] = (s.globalDeafen || route.deafened || route.silenced) ? 0 : route.volume * balance * s.masterVolume;
        micEnabled[d] = !(s.globalMute || s.globalDeafen || route.micMuted || route.deafened);
    }
    const bool primary = local_ == s.primary;
    const bool source = local_ == s.microphoneSource;
    if (primary && !wasPrimary_) { currentGain_.fill(0); targetGain_.fill(-1); }
    wasPrimary_ = primary;
    for (unsigned d = 0; d < 2; ++d) {
        // Deafen/silence must take effect at this block boundary, without a fade tail.
        if (s.globalDeafen || s.routes[d].deafened || s.routes[d].silenced) {
            currentGain_[d] = targetGain_[d] = 0; rampRemaining_[d] = 0;
        } else if (gain[d] != targetGain_[d]) {
            targetGain_[d] = gain[d]; rampRemaining_[d] = gainRampFrames_;
            if (!gainRampFrames_) currentGain_[d] = gain[d];
        }
    }
    for (std::size_t f = 0; f < frames; ++f) {
        for (unsigned d = 0; d < 2; ++d) {
            if (rampRemaining_[d]) {
                currentGain_[d] += (targetGain_[d] - currentGain_[d]) / float(rampRemaining_[d]);
                if (!--rampRemaining_[d]) currentGain_[d] = targetGain_[d];
            }
        }
        const auto* mic = source ? in.localMicrophone : in.remoteMicrophone;
        const float voice = limit(sample(mic, 2*f) * .5f + sample(mic, 2*f+1) * .5f);
        for (unsigned ch = 0; ch < 2; ++ch) {
            const auto i = 2*f + ch;
            if (out.listen) out.listen[i] = primary ? limit(sample(in.localPlayback, i) * currentGain_[li] + sample(in.remotePlayback, i) * currentGain_[ri]) : 0;
            if (out.localMicrophone) out.localMicrophone[i] = micEnabled[li] ? voice : 0;
            const float remoteVoice = source && micEnabled[ri] ? voice : 0;
            if (out.sendMicrophone) out.sendMicrophone[i] = remoteVoice;
            if (out.remoteMicrophone) out.remoteMicrophone[i] = remoteVoice;
            if (out.sendPlayback) out.sendPlayback[i] = primary ? 0 : limit(sample(in.localPlayback, i));
        }
    }
}
} // namespace bridge
