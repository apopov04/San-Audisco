#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace bridge {
// Stereo SPSC clock-domain bridge. Exactly one producer calls push(); exactly
// one consumer calls pop()/discard(). Sizes and return values are FRAMES, not
// samples. Consumer uses linear interpolation and a bounded PI controller to
// track clock drift, slewing by <=10ppm per block and bounded to +/-1000ppm.
// This is a lightweight prototype resampler, not a mastering-quality filter.
// Set target >= largest consumer block + 2, and capacity >= target + largest
// producer block + 2. The default target is 10ms at 48kHz for 5ms blocks.
// Overflow drops newest and causes consumer to clear/reprime. Underrun clears
// partial stale audio and reprimes. Neither path accumulates increasing delay.
// For role/device changes stop producer, then call discard() on consumer thread.
template<std::size_t CapacityFrames = 4096>
class AdaptiveAudioRing {
    static_assert(CapacityFrames >= 8 && (CapacityFrames & (CapacityFrames - 1)) == 0);
public:
    explicit AdaptiveAudioRing(std::size_t targetFrames = 480) noexcept
        : target_(std::clamp(targetFrames, std::size_t(2), CapacityFrames-2)) {}
    std::atomic<std::uint64_t> dropped{0}, underrun{0}, discontinuities{0};

    std::size_t push(const float* stereo, std::size_t frames) noexcept {
        const auto w=write_.load(std::memory_order_relaxed), r=read_.load(std::memory_order_acquire);
        const auto n=std::min(frames, CapacityFrames-(w-r));
        for(std::size_t i=0;i<n;++i) {
            const auto p=((w+i)&(CapacityFrames-1))*2;
            data_[p]=sanitize(stereo[2*i]); data_[p+1]=sanitize(stereo[2*i+1]);
        }
        write_.store(w+n,std::memory_order_release);
        if(n<frames) { dropped.fetch_add(frames-n,std::memory_order_relaxed); generation_.fetch_add(1,std::memory_order_release); }
        return n;
    }
    // Always fills all requested output frames. Returns frames of real audio;
    // zero means a priming/discontinuity/underrun block filled entirely with silence.
    std::size_t pop(float* stereo, std::size_t frames) noexcept {
        if(!frames)return 0;
        const auto generation=generation_.load(std::memory_order_acquire);
        if(generation!=seenGeneration_) { discard(); zero(stereo,frames); return 0; }
        const auto r=read_.load(std::memory_order_relaxed),w=write_.load(std::memory_order_acquire);
        const auto available=w-r;
        if(!primed_) {
            if(available<std::max(target_,frames+2)) {zero(stereo,frames);return 0;}
            primed_=true;
        }
        // Filter callback batching jitter before adjusting the clock ratio.
        const double error=(double(available)-double(target_))/double(target_);
        filteredError_ += .02*(error-filteredError_);
        integral_=std::clamp(integral_+filteredError_*.000002,-.001,.001);
        const double desired=1+std::clamp(.003*filteredError_+integral_,-.001,.001);
        ratio_ += std::clamp(desired-ratio_,-.000010,.000010);
        const double end=phase_+double(frames)*ratio_;
        // Need the next sample for interpolation AND retain the sample at the
        // next block's phase. Check before writing so no partially stale blocks.
        if(std::floor(end)+1>=double(available)) {
            underrun.fetch_add(frames,std::memory_order_relaxed);
            discard();zero(stereo,frames);return 0;
        }
        double position=phase_;
        for(std::size_t i=0;i<frames;++i) {
            const auto offset=std::size_t(position);
            const auto a=((r+offset)&(CapacityFrames-1))*2;
            const auto b=((r+offset+1)&(CapacityFrames-1))*2;
            const float t=float(position-double(offset));
            for(unsigned ch=0;ch<2;++ch) stereo[2*i+ch]=data_[a+ch]*(1-t)+data_[b+ch]*t;
            position+=ratio_;
        }
        const auto consumed=std::size_t(position);
        phase_=position-double(consumed);
        read_.store(r+consumed,std::memory_order_release);
        reportedRatio_.store(ratio_,std::memory_order_relaxed);
        return frames;
    }
    void discard() noexcept {
        // Only the consumer writes read_. Samples published after this snapshot
        // are retained as fresh data, never overwritten by either side.
        read_.store(write_.load(std::memory_order_acquire),std::memory_order_release);
        seenGeneration_=generation_.load(std::memory_order_acquire);
        phase_=0;ratio_=1;filteredError_=0;integral_=0;primed_=false;
        reportedRatio_.store(1,std::memory_order_relaxed);
        discontinuities.fetch_add(1,std::memory_order_relaxed);
    }
    // Consumer only; changing latency is an explicit discontinuity.
    void set_target_frames(std::size_t frames) noexcept {
        target_=std::clamp(frames,std::size_t(2),CapacityFrames-2);discard();
    }
    // Observational only. Concurrent producer/consumer sampling can be skewed;
    // clamp instead of leaking an unsigned subtraction wrap to diagnostics.
    std::size_t available() const noexcept {
        const auto r=read_.load(std::memory_order_acquire),w=write_.load(std::memory_order_acquire);
        return std::min(w-r,CapacityFrames);
    }
    double ratio() const noexcept { return reportedRatio_.load(std::memory_order_relaxed); }
private:
    static float sanitize(float f) noexcept {return std::isfinite(f)?f:0.f;}
    static void zero(float* p,std::size_t frames) noexcept {std::fill_n(p,frames*2,0.f);}
    std::size_t target_;
    std::array<float,CapacityFrames*2> data_{};
    alignas(64) std::atomic<std::size_t> write_{0};
    alignas(64) std::atomic<std::size_t> read_{0};
    std::atomic<std::uint64_t> generation_{0};
    std::atomic<double> reportedRatio_{1};
    std::uint64_t seenGeneration_=0;
    double phase_=0,ratio_=1,filteredError_=0,integral_=0;
    bool primed_=false;
};
} // namespace bridge
