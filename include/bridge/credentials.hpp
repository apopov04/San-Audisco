#pragma once
#include "bridge/transport.hpp"
#include <array>
#include <filesystem>
#include <span>
#include <string_view>

namespace bridge::credentials {
// Keychain account / DPAPI filename. ASCII letters, digits, '-' and '_', 1..64 chars.
// Names are identifiers only; they must not contain a pairing key.
void validate_id(std::string_view id);
using Record = std::array<uint8_t,36>;
Record encode_record(const net::Key& key);
net::Key decode_record(std::span<const uint8_t> record);
struct StoreOptions {
    // Windows only: override the encrypted-file directory for isolated tests.
    // DPAPI still uses the current Windows user. Ignored on macOS, which uses Keychain.
    std::filesystem::path directory;
};
// Keys are never logged. store_key replaces only the exact named credential.
// macOS: generic-password item service org.audiobridge.pairing, account=id.
// Windows: DPAPI current-user encrypted record in LocalAppData/AudioBridge/keys.
void store_key(std::string_view id, const net::Key& key, const StoreOptions& = {});
net::Key load_key(std::string_view id, const StoreOptions& = {});
// Returns false when the named credential does not exist.
bool delete_key(std::string_view id, const StoreOptions& = {});
}
