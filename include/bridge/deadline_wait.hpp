#pragma once
#include <chrono>
#include <stdexcept>
#include <string>
#include <thread>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace bridge {
// Per-thread deadline wait. No process/global timer-resolution changes.
// Each owner thread has one auto-reset timer, armed against its steady deadline.
class DeadlineWait {
#ifdef _WIN32
    HANDLE timer_{};
#endif
    unsigned long error_{};
public:
    DeadlineWait() {
#ifdef _WIN32
        // CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, supported since Windows 10
        // version 1803. Name the SDK flag locally for older SDK headers.
        constexpr DWORD highResolutionFlag=0x00000002;
        timer_=CreateWaitableTimerExW(nullptr,nullptr,highResolutionFlag,TIMER_MODIFY_STATE|SYNCHRONIZE);
        if(!timer_)throw std::runtime_error("High-resolution timer unavailable: "+std::to_string(GetLastError()));
#endif
    }
    ~DeadlineWait(){
#ifdef _WIN32
        if(timer_)CloseHandle(timer_);
#endif
    }
    DeadlineWait(const DeadlineWait&)=delete;
    DeadlineWait& operator=(const DeadlineWait&)=delete;
    unsigned long last_error() const noexcept{return error_;}
    bool until(std::chrono::steady_clock::time_point deadline) noexcept {
#ifdef _WIN32
        const auto ns=std::chrono::duration_cast<std::chrono::nanoseconds>(deadline-std::chrono::steady_clock::now()).count();
        if(ns<=0)return true;
        LARGE_INTEGER due{};due.QuadPart=-((ns+99)/100);
        if(!SetWaitableTimerEx(timer_,&due,0,nullptr,nullptr,nullptr,0)){error_=GetLastError();return false;}
        const auto status=WaitForSingleObject(timer_,INFINITE);
        if(status!=WAIT_OBJECT_0){error_=status==WAIT_FAILED?GetLastError():ERROR_GEN_FAILURE;return false;}
#else
        std::this_thread::sleep_until(deadline);
#endif
        return true;
    }
};
}
