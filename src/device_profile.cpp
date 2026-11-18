#include "bridge/device_profile.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <vector>
#if defined(__APPLE__) && !defined(BRIDGE_AGENT_FAKE)
#include <sys/sysctl.h>
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#elif defined(_WIN32) && !defined(BRIDGE_AGENT_FAKE)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace bridge::device {
Profile mac_profile(std::string_view model) {
    auto result = [&](const char* kind, const char* name) { return Profile{kind, std::string(model), name}; };
    if (model.starts_with("MacBookAir")) return result("laptop", "MacBook Air");
    if (model.starts_with("MacBookPro")) return result("laptop", "MacBook Pro");
    if (model.starts_with("MacBook")) return result("laptop", "MacBook");
    if (model.starts_with("iMac")) return result("desktop", "iMac");
    if (model.starts_with("Macmini")) return result("desktop", "Mac mini");
    if (model.starts_with("MacPro")) return result("desktop", "Mac Pro");
    if (model.starts_with("Xserve")) return result("server", "Xserve");
    // Apple Silicon model IDs do not encode a product family in their prefix.
    // Keep explicit known entries; unknown future IDs must never be guessed as Studio.
    constexpr std::array air{"Mac14,2", "Mac14,15", "Mac15,12", "Mac15,13", "Mac16,12", "Mac16,13", "Mac17,4"};
    constexpr std::array pro{"Mac14,5", "Mac14,6", "Mac14,7", "Mac14,9", "Mac14,10", "Mac15,3", "Mac15,6", "Mac15,7", "Mac15,8", "Mac15,9", "Mac15,10", "Mac15,11", "Mac16,1", "Mac16,5", "Mac16,6", "Mac16,7", "Mac16,8"};
    constexpr std::array mini{"Mac14,3", "Mac14,12", "Mac16,10", "Mac16,11"};
    constexpr std::array imac{"Mac15,4", "Mac15,5", "Mac16,2", "Mac16,3"};
    constexpr std::array studio{"Mac13,1", "Mac13,2", "Mac14,13", "Mac14,14", "Mac15,14", "Mac16,9"};
    auto contains = [&](const auto& values) { return std::find(values.begin(), values.end(), model) != values.end(); };
    if (contains(air)) return result("laptop", "MacBook Air");
    if (contains(pro)) return result("laptop", "MacBook Pro");
    if (contains(mini)) return result("desktop", "Mac mini");
    if (contains(imac)) return result("desktop", "iMac");
    if (contains(studio)) return result("server", "Mac Studio");
    if (model == "Mac14,8") return result("desktop", "Mac Pro");
    return result("unknown", "Mac");
}

Profile windows_profile(std::span<const std::uint8_t> raw, bool server_os) {
    Profile result{server_os ? "server" : "unknown", "", server_os ? "Windows Server" : "Windows PC"};
    // RawSMBIOSData uses DWORD Length at offset 4, little endian. Cap the total
    // table before interpreting structures; malformed firmware is common in VMs.
    if (raw.size() < 8 || raw.size() > 1024 * 1024) return result;
    std::uint32_t length = std::uint32_t(raw[4]) | std::uint32_t(raw[5]) << 8 | std::uint32_t(raw[6]) << 16 | std::uint32_t(raw[7]) << 24;
    if (length > raw.size() - 8) return result;
    const std::size_t end = 8 + length;
    for (std::size_t at = 8; at + 4 <= end;) {
        const unsigned type = raw[at], formatted = raw[at + 1];
        if (formatted < 4 || formatted > end - at) break;
        std::size_t next = at + formatted;
        while (next + 1 < end && (raw[next] != 0 || raw[next + 1] != 0)) ++next;
        if (next + 1 >= end) break; // Unterminated string area: reject this record.
        if (type == 3 && formatted >= 6) {
            // The high bit indicates a chassis lock, not another chassis type.
            const unsigned chassis = raw[at + 5] & 0x7f;
            switch (chassis) {
                case 8: case 9: case 10: case 11: case 14: case 30: case 31: case 32:
                    if (!server_os) { result.kind = "laptop"; result.default_name = "Windows Laptop"; }
                    break;
                case 17: case 23: case 28: case 29:
                    result.kind = "server"; result.default_name = "Windows Server"; break;
                case 3: case 4: case 5: case 6: case 7: case 13: case 15: case 16: case 24: case 35: case 36:
                    if (!server_os) result.kind = "desktop";
                    break;
                default: break;
            }
            result.model = "SMBIOS chassis " + std::to_string(chassis);
            if (result.kind != "unknown") return result;
        }
        if (type == 127) break;
        at = next + 2;
    }
    return result;
}

Profile detect_device_profile() {
#if defined(BRIDGE_AGENT_FAKE)
    return {"unknown", "", "Computer"};
#elif defined(__APPLE__)
    std::array<char, 256> model{}; std::size_t size = model.size();
    if (sysctlbyname("hw.model", model.data(), &size, nullptr, 0) != 0) model[0] = 0;
    model.back() = 0;
    auto profile = mac_profile(model.data());
    if (profile.kind == "unknown") {
        const auto battery = IOServiceGetMatchingService(kIOMainPortDefault, IOServiceMatching("AppleSmartBattery"));
        if (battery) {
            auto installed = IORegistryEntryCreateCFProperty(battery, CFSTR("BatteryInstalled"), kCFAllocatorDefault, 0);
            if (installed) {
                if (CFGetTypeID(installed) == CFBooleanGetTypeID() && CFBooleanGetValue(static_cast<CFBooleanRef>(installed))) {
                    profile.kind = "laptop"; profile.default_name = "MacBook";
                }
                CFRelease(installed);
            }
            IOObjectRelease(battery);
        }
    }
    return profile;
#elif defined(_WIN32)
    // Product type distinguishes a server OS without depending on the manifest's
    // reported version number. This is a fallback, not a claim about chassis.
    OSVERSIONINFOEXW version{}; version.dwOSVersionInfoSize = sizeof(version);
    bool server = GetVersionExW(reinterpret_cast<OSVERSIONINFOW*>(&version)) && version.wProductType != VER_NT_WORKSTATION;
    constexpr DWORD provider = 0x52534d42; // 'RSMB'
    const UINT count = GetSystemFirmwareTable(provider, 0, nullptr, 0);
    if (count < 8 || count > 1024 * 1024) return windows_profile({}, server);
    std::vector<std::uint8_t> bytes(count);
    if (GetSystemFirmwareTable(provider, 0, bytes.data(), count) != count) return windows_profile({}, server);
    return windows_profile(bytes, server);
#else
    return {"unknown", "", "Computer"};
#endif
}
}
