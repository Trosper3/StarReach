#pragma once
#include <cstdint>

// Shared networking constants and role definitions for StarReach multiplayer.
// Topology: listen-server (host). One player's process is the authority; other
// players connect to it as clients. The same NetSession class plays either role.
namespace net {

// Bump whenever the wire format in Protocol.h changes. Host rejects clients
// whose protocol version does not match.
inline constexpr uint16_t kProtocolVersion = 4; // v4: FighterLoadoutReport + FighterHardpointSnapshot (P8-T1)

inline constexpr uint16_t kDefaultPort  = 7777;
inline constexpr int      kMaxPlayers   = 8;   // peers a host will accept

// ENet channel assignment.
//   Reliable  — connection handshake / control messages that must arrive.
//   Unreliable — high-frequency state snapshots; drop-tolerant, latest wins.
inline constexpr uint8_t kChannelReliable   = 0;
inline constexpr uint8_t kChannelUnreliable = 1;
inline constexpr int     kChannelCount      = 2;

enum class NetRole : uint8_t {
    Offline = 0,   // single-player; no ENet host created
    Host,          // listen-server: authoritative sim + accepts peers
    Client         // connected to a remote host
};

} // namespace net
