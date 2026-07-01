#pragma once
#include <cstdint>
#include <cstring>
#include <vector>
#include "NetCommon.h"
#include "../engine/NetworkSyncSystem.h"   // ecs::NetworkSnapshot (reused on the wire)

// Binary wire protocol for StarReach multiplayer.
//
// Every packet begins with a single MsgType byte, followed by a type-specific
// payload. Serialization is little-endian raw-POD copy via ByteWriter/ByteReader
// — fine for the x86/x64 Windows targets this game ships on. If a big-endian or
// mixed-arch target is ever added, swap the put/get helpers for explicit
// byte-order encoding; nothing else in the protocol needs to change.
namespace net {

enum class MsgType : uint8_t {
    Hello         = 1,   // C->S: request to join (carries protocol version)
    Welcome       = 2,   // S->C: accepted; assigns the client its networkId
    Reject        = 3,   // S->C: refused (e.g. protocol mismatch / server full)
    Input         = 4,   // C->S: per-tick player input command
    Snapshot      = 5,   // S->C: authoritative world state (entities + asteroids + projectiles)
    WorldSync     = 6,   // S->C: system id + seed so client generates the same map
    PlayerDead    = 7,   // S->C: targeted client's player was killed; show death screen
    ServerClosing = 8,   // S->C: broadcast before host shuts down; clients return to menu
    StationDead   = 9,   // S->C: a world station was destroyed; client marks it dead
};

// Payload for WorldSync — the client calls SpawnPlanetsAndStations(worldSeed)
// and switches to system systemId on receipt.
struct WorldSyncData {
    uint32_t systemId  = 1;
    uint32_t worldSeed = 0;
};

// Reason codes for a Reject message.
enum class RejectReason : uint8_t {
    ProtocolMismatch = 1,
    ServerFull       = 2,
};

// What a client sends each tick to drive its avatar on the host. Mirrors the
// inputs PlayerInputSystem reads locally (movement axis, aim, fire).
struct InputCommand {
    uint32_t networkId   = 0;     // whose avatar this drives (host-assigned)
    float    moveX       = 0.0f; // -1..1 strafe/thrust axis
    float    moveY       = 0.0f; // -1..1
    float    aimRotation = 0.0f; // degrees
    uint8_t  firing      = 0;    // 1 = trigger held
    uint32_t sequence    = 0;    // client-incrementing; for ordering/prediction
    float    posX        = 0.0f; // client-authoritative position (host uses this to display ship)
    float    posY        = 0.0f;
};

// ── Low-level byte (de)serialization ────────────────────────────────────────

struct ByteWriter {
    std::vector<uint8_t> data;

    template <class T>
    void put(const T& v) {
        const uint8_t* p = reinterpret_cast<const uint8_t*>(&v);
        data.insert(data.end(), p, p + sizeof(T));
    }
};

struct ByteReader {
    const uint8_t* p        = nullptr;
    size_t         remaining = 0;
    bool           ok        = true;   // false once a read ran past the buffer

    ByteReader(const void* d, size_t n)
        : p(static_cast<const uint8_t*>(d)), remaining(n) {}

    template <class T>
    T get() {
        T v{};
        if (remaining < sizeof(T)) { ok = false; return v; }
        std::memcpy(&v, p, sizeof(T));
        p += sizeof(T);
        remaining -= sizeof(T);
        return v;
    }
};

// ── Message encoders ────────────────────────────────────────────────────────

inline std::vector<uint8_t> EncodeHello() {
    ByteWriter w;
    w.put(uint8_t(MsgType::Hello));
    w.put(kProtocolVersion);
    return std::move(w.data);
}

inline std::vector<uint8_t> EncodeWelcome(uint32_t assignedId) {
    ByteWriter w;
    w.put(uint8_t(MsgType::Welcome));
    w.put(assignedId);
    return std::move(w.data);
}

inline std::vector<uint8_t> EncodeReject(RejectReason reason) {
    ByteWriter w;
    w.put(uint8_t(MsgType::Reject));
    w.put(uint8_t(reason));
    return std::move(w.data);
}

inline std::vector<uint8_t> EncodeInput(const InputCommand& c) {
    ByteWriter w;
    w.put(uint8_t(MsgType::Input));
    w.put(c.networkId);
    w.put(c.moveX);
    w.put(c.moveY);
    w.put(c.aimRotation);
    w.put(c.firing);
    w.put(c.sequence);
    w.put(c.posX);
    w.put(c.posY);
    return std::move(w.data);
}

// Asteroid state carried inside a snapshot packet (appended after entity section).
struct AsteroidSnapshot {
    uint32_t id       = 0;
    float    posX     = 0.0f;
    float    posY     = 0.0f;
    float    velX     = 0.0f;
    float    velY     = 0.0f;
    float    rotation = 0.0f;
    int8_t   health   = 1;
    int8_t   tier     = 2;
};

// Projectile state in a snapshot packet (appended after asteroid section).
// Position + velocity only; client dead-reckons between ticks, server is authoritative for hits.
struct ProjectileSnapshot {
    float posX = 0.0f, posY = 0.0f;
    float velX = 0.0f, velY = 0.0f;
};

inline std::vector<uint8_t> EncodeSnapshot(
    const std::vector<ecs::NetworkSnapshot>& snaps,
    const std::vector<AsteroidSnapshot>&     asteroids   = {},
    const std::vector<ProjectileSnapshot>&   projectiles = {})
{
    ByteWriter w;
    w.put(uint8_t(MsgType::Snapshot));
    w.put(uint16_t(snaps.size()));
    for (const auto& s : snaps) {
        w.put(s.networkId);
        w.put(s.position.x);
        w.put(s.position.y);
        w.put(s.velocity.x);
        w.put(s.velocity.y);
        w.put(s.rotation);
    }
    // Asteroid section.
    uint8_t aCount = static_cast<uint8_t>(std::min(asteroids.size(), size_t(255)));
    w.put(aCount);
    for (uint8_t i = 0; i < aCount; ++i) {
        const AsteroidSnapshot& a = asteroids[i];
        w.put(a.id);
        w.put(a.posX);
        w.put(a.posY);
        w.put(a.velX);
        w.put(a.velY);
        w.put(a.rotation);
        w.put(a.health);
        w.put(a.tier);
    }
    // Projectile section.
    uint16_t pCount = static_cast<uint16_t>(std::min(projectiles.size(), size_t(65535)));
    w.put(pCount);
    for (uint16_t i = 0; i < pCount; ++i) {
        w.put(projectiles[i].posX);
        w.put(projectiles[i].posY);
        w.put(projectiles[i].velX);
        w.put(projectiles[i].velY);
    }
    return std::move(w.data);
}

inline std::vector<uint8_t> EncodePlayerDead() {
    ByteWriter w;
    w.put(uint8_t(MsgType::PlayerDead));
    return std::move(w.data);
}

inline std::vector<uint8_t> EncodeServerClosing() {
    ByteWriter w;
    w.put(uint8_t(MsgType::ServerClosing));
    return std::move(w.data);
}

inline std::vector<uint8_t> EncodeStationDead(uint32_t stationId) {
    ByteWriter w;
    w.put(uint8_t(MsgType::StationDead));
    w.put(stationId);
    return std::move(w.data);
}

inline bool DecodeStationDead(ByteReader& r, uint32_t& outId) {
    outId = r.get<uint32_t>();
    return r.ok;
}

inline std::vector<uint8_t> EncodeWorldSync(const WorldSyncData& ws) {
    ByteWriter w;
    w.put(uint8_t(MsgType::WorldSync));
    w.put(ws.systemId);
    w.put(ws.worldSeed);
    return std::move(w.data);
}

// ── Message decoders (each returns false on a malformed/short packet) ────────

inline MsgType PeekType(const void* data, size_t len) {
    if (len < 1) return MsgType(0);
    return MsgType(*static_cast<const uint8_t*>(data));
}

inline bool DecodeHello(ByteReader& r, uint16_t& outVersion) {
    outVersion = r.get<uint16_t>();
    return r.ok;
}

inline bool DecodeWelcome(ByteReader& r, uint32_t& outId) {
    outId = r.get<uint32_t>();
    return r.ok;
}

inline bool DecodeReject(ByteReader& r, RejectReason& outReason) {
    outReason = RejectReason(r.get<uint8_t>());
    return r.ok;
}

inline bool DecodeWorldSync(ByteReader& r, WorldSyncData& out) {
    out.systemId  = r.get<uint32_t>();
    out.worldSeed = r.get<uint32_t>();
    return r.ok;
}

inline bool DecodeInput(ByteReader& r, InputCommand& out) {
    out.networkId   = r.get<uint32_t>();
    out.moveX       = r.get<float>();
    out.moveY       = r.get<float>();
    out.aimRotation = r.get<float>();
    out.firing      = r.get<uint8_t>();
    out.sequence    = r.get<uint32_t>();
    out.posX        = r.get<float>();
    out.posY        = r.get<float>();
    return r.ok;
}

inline bool DecodeSnapshot(ByteReader& r,
                           std::vector<ecs::NetworkSnapshot>& out,
                           std::vector<AsteroidSnapshot>&     asteroidOut,
                           std::vector<ProjectileSnapshot>&   projOut)
{
    out.clear();
    asteroidOut.clear();
    projOut.clear();
    uint16_t count = r.get<uint16_t>();
    if (!r.ok) return false;
    out.reserve(count);
    for (uint16_t i = 0; i < count; ++i) {
        ecs::NetworkSnapshot s;
        s.networkId  = r.get<uint32_t>();
        s.position.x = r.get<float>();
        s.position.y = r.get<float>();
        s.velocity.x = r.get<float>();
        s.velocity.y = r.get<float>();
        s.rotation   = r.get<float>();
        if (!r.ok) return false;
        out.push_back(s);
    }
    // Asteroid section.
    if (r.remaining == 0) return true;
    uint8_t aCount = r.get<uint8_t>();
    if (!r.ok) return true;
    asteroidOut.reserve(aCount);
    for (uint8_t i = 0; i < aCount; ++i) {
        AsteroidSnapshot a;
        a.id       = r.get<uint32_t>();
        a.posX     = r.get<float>();
        a.posY     = r.get<float>();
        a.velX     = r.get<float>();
        a.velY     = r.get<float>();
        a.rotation = r.get<float>();
        a.health   = r.get<int8_t>();
        a.tier     = r.get<int8_t>();
        if (!r.ok) break;
        asteroidOut.push_back(a);
    }
    // Projectile section.
    if (r.remaining == 0) return true;
    uint16_t pCount = r.get<uint16_t>();
    if (!r.ok) return true;
    projOut.reserve(pCount);
    for (uint16_t i = 0; i < pCount; ++i) {
        ProjectileSnapshot ps;
        ps.posX = r.get<float>();
        ps.posY = r.get<float>();
        ps.velX = r.get<float>();
        ps.velY = r.get<float>();
        if (!r.ok) break;
        projOut.push_back(ps);
    }
    return true;
}

} // namespace net
