#pragma once
#include "bridge/adaptive_ring.hpp"
#include "bridge/engine.hpp"
#include <array>
#include <atomic>
#include <cstddef>

namespace bridge {
// Headphone-callback-driven mixer. Local capture and network decoding each have
// their own producer queue. The headphone callback is their only consumer.
// No worker-thread rescheduling or second render FIFO on the local audio path.
class ListeningMixer {
public:
    enum Reset : unsigned { Local = 1, Remote = 2, Both = 3 };
    // 100 ms applies only to received audio; local game latency stays callback-derived.
    ListeningMixer(Device local,std::size_t targetFrames)
        : engine_(local),local_(targetFrames),remote_(std::max(targetFrames,std::size_t(4800))),requestedTarget_(targetFrames),activeTarget_(targetFrames) {}
    void setSettings(const Settings& settings){engine_.setSettings(settings);}
    // Remote reset/target requests and pushRemote share the decoding worker.
    // It must wait for remoteResetPending()==false before advancing packets.
    // Actual resets are executed by the sole consumer.
    void requestReset(unsigned which=Both) noexcept {
        if(which&Local)resets_.fetch_or(Local,std::memory_order_release);
        if(which&Remote)remoteResetRequested_.fetch_add(1,std::memory_order_release);
    }
    void setTargetFrames(std::size_t frames) noexcept {
        frames=std::clamp(frames,std::size_t(256),std::size_t(4096));
        if(requestedTarget_.exchange(frames,std::memory_order_acq_rel)!=frames)remoteResetRequested_.fetch_add(1,std::memory_order_release);
    }
    std::size_t targetFrames() const noexcept {return requestedTarget_.load(std::memory_order_acquire);}
    std::size_t remoteTargetFrames() const noexcept {return std::max(targetFrames(),std::size_t(4800));}
    bool remoteResetPending() const noexcept {return remoteResetRequested_.load(std::memory_order_acquire)!=remoteResetAcknowledged_.load(std::memory_order_acquire);}
    std::size_t pushLocal(const float* samples,std::size_t frames) noexcept {return local_.push(samples,frames);}
    std::size_t pushRemote(const float* samples,std::size_t frames) noexcept {return remoteResetPending()?0:remote_.push(samples,frames);}
    std::size_t remoteAvailable() const noexcept {return remote_.available();}
    std::uint64_t localDrops() const noexcept {return local_.dropped.load(std::memory_order_relaxed);}
    // One audio callback thread; preallocated240-frame scratch handles any
    // callback size in bounded chunks. Never allocates, blocks or performs I/O.
    void render(float* destination,std::size_t frames) noexcept {
        // Acquire the epoch BEFORE its associated target. Observing a new epoch
        // synchronizes the preceding target publication; acknowledging that
        // epoch must never leave a later target reset that would erase fresh data.
        const auto remoteReset=remoteResetRequested_.load(std::memory_order_acquire);
        const auto requested=requestedTarget_.load(std::memory_order_acquire);
        if(requested!=activeTarget_){local_.set_target_frames(requested);remote_.set_target_frames(std::max(requested,std::size_t(4800)));activeTarget_=requested;}
        const auto reset=resets_.exchange(0,std::memory_order_acq_rel);
        if(reset&Local)local_.discard();
        if(remoteReset!=remoteResetAcknowledged_.load(std::memory_order_relaxed)){
            remote_.discard();remoteResetAcknowledged_.store(remoteReset,std::memory_order_release);
        }
        for(std::size_t offset=0;offset<frames;) {
            const auto n=std::min(std::size_t(240),frames-offset);
            local_.pop(localScratch_.data(),n);remote_.pop(remoteScratch_.data(),n);
            engine_.process({localScratch_.data(),remoteScratch_.data(),nullptr,nullptr},
                {destination+offset*2,nullptr,nullptr,nullptr,nullptr},n);
            offset+=n;
        }
    }
private:
    Engine engine_;
    AdaptiveAudioRing<8192> local_,remote_;
    std::atomic<unsigned> resets_{0};
    std::atomic<std::uint64_t> remoteResetRequested_{0},remoteResetAcknowledged_{0};
    std::atomic<std::size_t> requestedTarget_;
    std::size_t activeTarget_;
    std::array<float,480> localScratch_{},remoteScratch_{};
};
}
