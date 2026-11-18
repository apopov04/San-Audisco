#pragma once
#include "bridge/transport.hpp"
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace bridge::room {
constexpr std::uint16_t default_port=45671;
struct Device { std::string id, name, platform; }; // id: 32 lowercase hex; platform: windows|mac
struct Config {
    Device local;
    std::string bind_address="0.0.0.0";
    std::uint16_t port=default_port; // TCP control/pairing and UDP discovery
    std::string discovery_address="255.255.255.255";
    std::uint16_t discovery_port=default_port;
    std::uint32_t invitation_lifetime_seconds=300; // 1..300; shorter values permit expiry tests
};
struct Credential {
    std::string room_id;
    Device peer;
    std::string last_address;
    std::uint16_t peer_port=default_port;
    net::Key secret{}; // long-lived PAKE-derived pairing key; store only in protected storage
};
struct Nearby {
    std::string room_id;
    Device device;
    std::string address;
    std::uint16_t port=default_port;
    bool accepting=false;
};
struct Status {
    std::string phase="idle"; // idle, inviting, joining, reconnecting, connected
    std::string room_id, invitation_code, error;
    std::uint32_t invitation_seconds=0;
    Device peer;
    std::string peer_address;
    std::uint16_t peer_port=0;
    bool connected=false;
    std::uint64_t generation=0;
};
std::string new_id();
std::string normalize_code(const std::string&); // accepts XXXX-XXXX, rejects ambiguous/nonalphabet characters
net::Key audio_key(const Credential&); // domain-separated from control/session keys
// Single-thread owned. poll() never blocks; call at least every 50 ms.
// Parent persists take_credential() before starting media. No key material in status/discovery.
class Service {
public:
    explicit Service(Config);
    ~Service();
    Service(const Service&)=delete;
    Service& operator=(const Service&)=delete;
    std::string create_invitation(); // five minutes, replaces unused invitation; refuses paired room
    void cancel_invitation();
    void set_local_name(const std::string& name);
    void discover();
    void discover_at(const std::string& address,std::uint16_t port=default_port); // unicast fallback
    std::vector<Nearby> nearby() const;
    void join(const Nearby&,const std::string& code); // async; throws for invalid local input
    void restore(const Credential&); // remembers paired peer and reconnects automatically
    void forget(); // closes peer, wipes remembered secret and invitation
    void poll();
    Status status() const;
    std::optional<Credential> take_credential(); // newly paired or authenticated address update
    bool send(const std::string& message); // max 64 KiB, bounded queued ciphertext; false if offline/full
    std::optional<std::string> receive();
    std::uint16_t port() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
