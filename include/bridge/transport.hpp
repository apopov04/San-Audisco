#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>
#include <map>

namespace bridge::net {
using Key = std::array<uint8_t,32>;
using Epoch = std::array<uint8_t,16>;
Key random_key();
std::string key_to_hex(const Key&);
Key key_from_hex(const std::string&);
enum class Kind : uint8_t { Hello=1, Ack=2, Audio=3, Control=4, Heartbeat=5 };
enum class Stream : uint8_t { Playback=1, Microphone=2 };
struct Packet { Kind kind{}; Stream stream{}; uint64_t sequence{}, sample_time{}; std::vector<uint8_t> payload; };
struct Config {
    std::string bind_address="0.0.0.0";
    uint16_t bind_port=45670;
    std::string peer_address="127.0.0.1";
    uint16_t peer_port=45670;
    Key key{};
};
// Single-thread ownership. poll() drives authenticated startup handshake.
// Call poll repeatedly (even with no audio), send methods return false until ready.
class Transport {
public:
    explicit Transport(const Config&);
    ~Transport();
    Transport(const Transport&)=delete;
    Transport& operator=(const Transport&)=delete;
    bool ready() const;
    uint16_t local_port() const;
    bool send_audio(Stream, std::span<const int16_t> samples, uint64_t sample_time);
    bool send_control(std::span<const uint8_t> payload);
    std::optional<Packet> poll();
    uint64_t rejected_packets() const;
    uint64_t session_generation() const;
private:
    struct Impl; std::unique_ptr<Impl> impl_;
};
// Authentication/anti-replay core is public for deterministic fault-injection tests.
class PacketCodec {
public:
    PacketCodec(const Key&, const Epoch& local);
    ~PacketCodec();
    void set_peer_epoch(const Epoch&);
    std::vector<uint8_t> encode(Kind, Stream, uint64_t sample_time, std::span<const uint8_t>);
    std::optional<Packet> decode(std::span<const uint8_t>);
    Epoch decoded_sender() const { return decoded_sender_; }
private:
    Key key_; Epoch local_, peer_{}, decoded_sender_{};
    uint64_t tx_=0, highest_=0, replay_bits_=0;
    bool peer_set_=false, received_=false;
};
// Bounded packet reordering, advancing on explicit playout ticks; absent packet=silence.
class JitterBuffer {
public:
    explicit JitterBuffer(size_t capacity=12, size_t prefill=4);
    bool push(Packet);
    std::optional<Packet> pop();
    void reset();
    size_t size() const { return packets_.size(); }
    bool primed() const { return started_ || packets_.size() >= prefill_; }
    bool has_next() const { return started_ ? packets_.contains(next_time_) : packets_.size() >= prefill_; }
    uint64_t discontinuity_count() const { return discontinuities_; }
private:
    size_t capacity_, prefill_; bool started_=false; uint64_t next_time_=0;
    uint64_t last_late_time_=0,discontinuities_=0;
    unsigned late_run_=0; bool recovering_=false;
    std::map<uint64_t,Packet> packets_;
};
// Decode after reordering. Use conceal() for each lost 240-frame playout tick.
class AudioDecoder {
public:
    AudioDecoder();
    ~AudioDecoder();
    AudioDecoder(const AudioDecoder&)=delete;
    AudioDecoder& operator=(const AudioDecoder&)=delete;
    std::array<int16_t,480> decode(const Packet&);
    std::array<int16_t,480> conceal(Stream);
    void reset();
    void reset(Stream);
private:
    struct Impl; std::unique_ptr<Impl> impl_;
};
}
