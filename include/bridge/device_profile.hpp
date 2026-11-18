#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace bridge::device {
struct Profile {
    std::string kind = "unknown"; // laptop | desktop | server | unknown
    std::string model;
    std::string default_name;
};
// Pure detection rules; no identity, serial number or network address is collected.
Profile mac_profile(std::string_view model);
// RawSMBIOSData returned by GetSystemFirmwareTable('RSMB'), including its 8-byte header.
Profile windows_profile(std::span<const std::uint8_t> raw_smbios, bool server_os = false);
// Call once on a background/control thread. Never used in the audio callback.
Profile detect_device_profile();
}
