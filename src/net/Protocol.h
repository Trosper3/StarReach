#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include "NetCommon.h"
#include "../core/FactionEnum.h"           // Faction, carried in Hello for discovery pooling
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
    Hello           = 1,   // C->S: request to join (carries protocol version + player faction)
    Welcome         = 2,   // S->C: accepted; assigns the client its networkId
    Reject          = 3,   // S->C: refused (e.g. protocol mismatch / server full)
    Input           = 4,   // C->S: per-tick player input command
    Snapshot        = 5,   // S->C: authoritative state of ONE system (entities + asteroids + projectiles)
    WorldSync       = 6,   // S->C: system id + seed + live-state diff; sent at join and on warp arrival
    PlayerDead      = 7,   // S->C: targeted client's player was killed; show death screen
    ServerClosing   = 8,   // S->C: broadcast before host shuts down; clients return to menu
    StationDead     = 9,   // S->C: a world station was destroyed; client marks it dead
    ClientWarpNotify= 10,  // C->S: client's warp cinematic hit black; wants system `systemId`
    // Discovery pooling (party = same faction, per [[tasks-galaxy-map-features]]
    // item #5): DiscoverySync delivers the recipient's faction's full known-
    // discovered set once, right after Hello reveals their faction (a plain
    // WorldSync resend would work too, but re-triggers a full client-side
    // world regen as a side effect — this is a dedicated, side-effect-free
    // bulk catch-up instead). SystemDiscovered is the live incremental update
    // broadcast to faction-mates whenever anyone in the party warps somewhere.
    DiscoverySync   = 11,  // S->C: full discovered-system-id list for the recipient's faction
    SystemDiscovered= 12,  // S->C: one more system id discovered by a same-faction party member
    // Epic C (player-built structures MP sync): a client wanting to build a
    // station or place a friendly ship sends a request instead of creating
    // the object locally; only the host ever actually creates it (canonical
    // id assignment), then it reaches everyone through the normal Snapshot
    // broadcast. Materials/credits are validated and spent locally by the
    // requesting client before sending — co-op trust model, not anti-cheat.
    BuildStationRequest = 13, // C->S: build a player station at (posX,posY)
    PlaceShipRequest    = 14, // C->S: place a friendly NPC ship at (posX,posY)
};

// Station state that differs from what seed-regeneration produces. Sent inside
// WorldSync so a client arriving at a lived-in system sees damage/deaths that
// happened before it got there. NPCs/asteroids/projectiles need no diff: they
// self-heal from the first Snapshot (clients evict anything absent from it).
struct StationStateSync {
    uint32_t id    = 0;
    float    hull  = 0.0f;
    uint8_t  alive = 1;
};

// Payload for WorldSync — the client calls SpawnPlanetsAndStations(worldSeed),
// fast-forwards planet orbits by worldAge, applies the station diff, and
// switches to system systemId. gameSeed is the universe master seed
// (UniverseRegistry) and galaxyId picks which galaxy within it (defaults to
// 1, the home galaxy, for hosts that never leave it) — together they resolve
// to the galaxy seed passed to StarSystemRegistry::Init, so the client's
// galactic map matches the host's; both are independent of worldSeed, which
// only seeds one system's content. Only the host is ever authoritative for
// which galaxy is "current" — clients follow along on WorldSync, they never
// initiate a cross-galaxy warp themselves (see SpaceFlight::Update's
// MapAction::WarpToGalaxy handling).
struct WorldSyncData {
    uint32_t systemId  = 1;
    uint32_t worldSeed = 0;
    uint32_t gameSeed  = 0;
    uint32_t galaxyId  = 1;
    float    worldAge  = 0.0f;   // seconds the host has simulated this system
    std::vector<StationStateSync> stations;  // only stations that differ from genesis
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
    uint8_t  docked       = 0;   // 1 = client is inside a station menu — host excludes this
                                  // peer from targeting/collision/broadcast until it clears
};

// C->S: client requests the host build a player station at (posX,posY) in
// the client's current system. Materials/credits are validated and spent
// locally by the requesting client BEFORE sending this — the host does not
// re-validate affordability (co-op trust model, not anti-cheat). See
// [[tasks-multiplayer]] Epic C.
struct BuildStationRequest {
    std::string stationDefId;
    float       posX = 0.0f;
    float       posY = 0.0f;
};

// C->S: client requests the host place a friendly NPC ship (incl. a
// player-built capital) at (posX,posY). Same locally-trusted spend model as
// BuildStationRequest. requesterId is NOT sent on the wire (the host already
// knows the sending peer from the ENet connection) — NetSession fills it in
// after decode, mirroring how MsgType::Input stamps cmd.networkId, so the
// host knows which peer's system to place the ship into.
struct PlaceShipRequest {
    std::string shipDefId;
    float       posX = 0.0f;
    float       posY = 0.0f;
    uint32_t    requesterId = 0;
};

// ── Low-level byte (de)serialization ────────────────────────────────────────

struct ByteWriter {
    std::vector<uint8_t> data;

    template <class T>
    void put(const T& v) {
        const uint8_t* p = reinterpret_cast<const uint8_t*>(&v);
        data.insert(data.end(), p, p + sizeof(T));
    }

    // Length-prefixed string (uint8_t length, capped at 255 bytes) — mirrors
    // ByteReader::getStr(). Truncates silently past 255 chars (defIds are
    // short identifiers, never expected to hit this).
    void putStr(const std::string& s) {
        uint8_t len = static_cast<uint8_t>(std::min(s.size(), size_t(255)));
        put(len);
        data.insert(data.end(), s.begin(), s.begin() + len);
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

    // Length-prefixed string (uint8_t length, capped at 255 bytes) — the
    // template get<T>()/put<T>() above assume a fixed-size POD, which a
    // std::string is not.
    std::string getStr() {
        uint8_t len = get<uint8_t>();
        if (!ok || remaining < len) { ok = false; return {}; }
        std::string s(reinterpret_cast<const char*>(p), len);
        p += len;
        remaining -= len;
        return s;
    }
};

// ── Message encoders ────────────────────────────────────────────────────────

inline std::vector<uint8_t> EncodeHello(Faction faction) {
    ByteWriter w;
    w.put(uint8_t(MsgType::Hello));
    w.put(kProtocolVersion);
    w.put(uint8_t(faction));
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
    w.put(c.docked);
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

// Per-hardpoint capital-ship state (appended after the projectile section).
// One flat row per alive-ship hardpoint; the client groups rows by capitalId.
struct CapitalHardpointSnapshot {
    uint32_t capitalId = 0;
    uint8_t  hpIndex   = 0;
    uint16_t hull      = 0;
    uint8_t  alive     = 1;
};

// Player-built station body state (appended after the capital-hardpoint
// section). Resent in full every snapshot tick (position is static once
// placed) — keeps encode/decode symmetric with AsteroidSnapshot rather than
// adding a delta/dirty-tracking scheme for a handful of stations per system.
struct PlayerStationSnapshot {
    uint32_t    id           = 0;
    std::string stationDefId;
    float       posX         = 0.0f;
    float       posY         = 0.0f;
    uint8_t     alive        = 1;
};

// Per-hardpoint player-station state (appended after the PlayerStationSnapshot
// section). One flat row per hardpoint; client groups rows by stationId.
// Kept separate from CapitalHardpointSnapshot because PlayerStation::id and
// NpcMeta::id are independent per-process counters that can collide.
struct PlayerStationHardpointSnapshot {
    uint32_t stationId = 0;
    uint8_t  hpIndex   = 0;
    uint16_t hull      = 0;
    uint8_t  alive     = 1;
};

inline std::vector<uint8_t> EncodeSnapshot(
    uint32_t                                     systemId,
    const std::vector<ecs::NetworkSnapshot>&     snaps,
    const std::vector<AsteroidSnapshot>&         asteroids   = {},
    const std::vector<ProjectileSnapshot>&       projectiles = {},
    const std::vector<CapitalHardpointSnapshot>& capitals    = {},
    const std::vector<PlayerStationSnapshot>&    stations    = {},
    const std::vector<PlayerStationHardpointSnapshot>& stationHardpoints = {})
{
    ByteWriter w;
    w.put(uint8_t(MsgType::Snapshot));
    w.put(systemId);   // which system this snapshot describes; clients drop mismatches
    w.put(uint16_t(snaps.size()));
    for (const auto& s : snaps) {
        w.put(s.networkId);
        w.put(s.position.x);
        w.put(s.position.y);
        w.put(s.velocity.x);
        w.put(s.velocity.y);
        w.put(s.rotation);
        w.put(s.shipNameHash);
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
    // Capital hardpoint section.
    uint16_t cCount = static_cast<uint16_t>(std::min(capitals.size(), size_t(65535)));
    w.put(cCount);
    for (uint16_t i = 0; i < cCount; ++i) {
        w.put(capitals[i].capitalId);
        w.put(capitals[i].hpIndex);
        w.put(capitals[i].hull);
        w.put(capitals[i].alive);
    }
    // Player-station body section.
    uint16_t sCount = static_cast<uint16_t>(std::min(stations.size(), size_t(65535)));
    w.put(sCount);
    for (uint16_t i = 0; i < sCount; ++i) {
        w.put(stations[i].id);
        w.putStr(stations[i].stationDefId);
        w.put(stations[i].posX);
        w.put(stations[i].posY);
        w.put(stations[i].alive);
    }
    // Player-station hardpoint section.
    uint16_t shCount = static_cast<uint16_t>(std::min(stationHardpoints.size(), size_t(65535)));
    w.put(shCount);
    for (uint16_t i = 0; i < shCount; ++i) {
        w.put(stationHardpoints[i].stationId);
        w.put(stationHardpoints[i].hpIndex);
        w.put(stationHardpoints[i].hull);
        w.put(stationHardpoints[i].alive);
    }
    return std::move(w.data);
}

inline std::vector<uint8_t> EncodeBuildStationRequest(const std::string& stationDefId, float posX, float posY) {
    ByteWriter w;
    w.put(uint8_t(MsgType::BuildStationRequest));
    w.putStr(stationDefId);
    w.put(posX);
    w.put(posY);
    return std::move(w.data);
}

inline bool DecodeBuildStationRequest(ByteReader& r, std::string& outStationDefId, float& outX, float& outY) {
    outStationDefId = r.getStr();
    outX = r.get<float>();
    outY = r.get<float>();
    return r.ok;
}

inline std::vector<uint8_t> EncodePlaceShipRequest(const std::string& shipDefId, float posX, float posY) {
    ByteWriter w;
    w.put(uint8_t(MsgType::PlaceShipRequest));
    w.putStr(shipDefId);
    w.put(posX);
    w.put(posY);
    return std::move(w.data);
}

inline bool DecodePlaceShipRequest(ByteReader& r, std::string& outShipDefId, float& outX, float& outY) {
    outShipDefId = r.getStr();
    outX = r.get<float>();
    outY = r.get<float>();
    return r.ok;
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

inline std::vector<uint8_t> EncodeStationDead(uint32_t systemId, uint32_t stationId) {
    ByteWriter w;
    w.put(uint8_t(MsgType::StationDead));
    w.put(systemId);
    w.put(stationId);
    return std::move(w.data);
}

inline bool DecodeStationDead(ByteReader& r, uint32_t& outSystemId, uint32_t& outId) {
    outSystemId = r.get<uint32_t>();
    outId       = r.get<uint32_t>();
    return r.ok;
}

inline std::vector<uint8_t> EncodeClientWarpNotify(uint32_t systemId) {
    ByteWriter w;
    w.put(uint8_t(MsgType::ClientWarpNotify));
    w.put(systemId);
    return std::move(w.data);
}

inline bool DecodeClientWarpNotify(ByteReader& r, uint32_t& outSystemId) {
    outSystemId = r.get<uint32_t>();
    return r.ok;
}

inline std::vector<uint8_t> EncodeDiscoverySync(const std::vector<uint32_t>& systemIds) {
    ByteWriter w;
    w.put(uint8_t(MsgType::DiscoverySync));
    uint16_t count = static_cast<uint16_t>(std::min(systemIds.size(), size_t(65535)));
    w.put(count);
    for (uint16_t i = 0; i < count; ++i) w.put(systemIds[i]);
    return std::move(w.data);
}

inline bool DecodeDiscoverySync(ByteReader& r, std::vector<uint32_t>& outIds) {
    outIds.clear();
    uint16_t count = r.get<uint16_t>();
    if (!r.ok) return false;
    outIds.reserve(count);
    for (uint16_t i = 0; i < count; ++i) {
        outIds.push_back(r.get<uint32_t>());
        if (!r.ok) return false;
    }
    return true;
}

inline std::vector<uint8_t> EncodeSystemDiscovered(uint32_t systemId) {
    ByteWriter w;
    w.put(uint8_t(MsgType::SystemDiscovered));
    w.put(systemId);
    return std::move(w.data);
}

inline bool DecodeSystemDiscovered(ByteReader& r, uint32_t& outSystemId) {
    outSystemId = r.get<uint32_t>();
    return r.ok;
}

inline std::vector<uint8_t> EncodeWorldSync(const WorldSyncData& ws) {
    ByteWriter w;
    w.put(uint8_t(MsgType::WorldSync));
    w.put(ws.systemId);
    w.put(ws.worldSeed);
    w.put(ws.gameSeed);
    w.put(ws.galaxyId);
    w.put(ws.worldAge);
    uint16_t count = static_cast<uint16_t>(std::min(ws.stations.size(), size_t(65535)));
    w.put(count);
    for (uint16_t i = 0; i < count; ++i) {
        w.put(ws.stations[i].id);
        w.put(ws.stations[i].hull);
        w.put(ws.stations[i].alive);
    }
    return std::move(w.data);
}

// ── Message decoders (each returns false on a malformed/short packet) ────────

inline MsgType PeekType(const void* data, size_t len) {
    if (len < 1) return MsgType(0);
    return MsgType(*static_cast<const uint8_t*>(data));
}

inline bool DecodeHello(ByteReader& r, uint16_t& outVersion, Faction& outFaction) {
    outVersion = r.get<uint16_t>();
    outFaction = Faction(r.get<uint8_t>());
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
    out.gameSeed  = r.get<uint32_t>();
    out.galaxyId  = r.get<uint32_t>();
    out.worldAge  = r.get<float>();
    uint16_t count = r.get<uint16_t>();
    if (!r.ok) return false;
    out.stations.clear();
    out.stations.reserve(count);
    for (uint16_t i = 0; i < count; ++i) {
        StationStateSync ss;
        ss.id    = r.get<uint32_t>();
        ss.hull  = r.get<float>();
        ss.alive = r.get<uint8_t>();
        if (!r.ok) return false;
        out.stations.push_back(ss);
    }
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
    out.docked      = r.get<uint8_t>();
    return r.ok;
}

inline bool DecodeSnapshot(ByteReader& r,
                           uint32_t&                          outSystemId,
                           std::vector<ecs::NetworkSnapshot>& out,
                           std::vector<AsteroidSnapshot>&     asteroidOut,
                           std::vector<ProjectileSnapshot>&   projOut,
                           std::vector<CapitalHardpointSnapshot>& capsOut,
                           std::vector<PlayerStationSnapshot>&    stationsOut,
                           std::vector<PlayerStationHardpointSnapshot>& stationHpsOut)
{
    out.clear();
    asteroidOut.clear();
    projOut.clear();
    capsOut.clear();
    stationsOut.clear();
    stationHpsOut.clear();
    outSystemId = r.get<uint32_t>();
    uint16_t count = r.get<uint16_t>();
    if (!r.ok) return false;
    out.reserve(count);
    for (uint16_t i = 0; i < count; ++i) {
        ecs::NetworkSnapshot s;
        s.networkId    = r.get<uint32_t>();
        s.position.x   = r.get<float>();
        s.position.y   = r.get<float>();
        s.velocity.x   = r.get<float>();
        s.velocity.y   = r.get<float>();
        s.rotation     = r.get<float>();
        s.shipNameHash = r.get<uint32_t>();
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
    // Capital hardpoint section.
    if (r.remaining == 0) return true;
    uint16_t cCount = r.get<uint16_t>();
    if (!r.ok) return true;
    capsOut.reserve(cCount);
    for (uint16_t i = 0; i < cCount; ++i) {
        CapitalHardpointSnapshot cs;
        cs.capitalId = r.get<uint32_t>();
        cs.hpIndex   = r.get<uint8_t>();
        cs.hull      = r.get<uint16_t>();
        cs.alive     = r.get<uint8_t>();
        if (!r.ok) break;
        capsOut.push_back(cs);
    }
    // Player-station body section.
    if (r.remaining == 0) return true;
    uint16_t sCount = r.get<uint16_t>();
    if (!r.ok) return true;
    stationsOut.reserve(sCount);
    for (uint16_t i = 0; i < sCount; ++i) {
        PlayerStationSnapshot ss;
        ss.id           = r.get<uint32_t>();
        ss.stationDefId = r.getStr();
        ss.posX         = r.get<float>();
        ss.posY         = r.get<float>();
        ss.alive        = r.get<uint8_t>();
        if (!r.ok) break;
        stationsOut.push_back(ss);
    }
    // Player-station hardpoint section.
    if (r.remaining == 0) return true;
    uint16_t shCount = r.get<uint16_t>();
    if (!r.ok) return true;
    stationHpsOut.reserve(shCount);
    for (uint16_t i = 0; i < shCount; ++i) {
        PlayerStationHardpointSnapshot shs;
        shs.stationId = r.get<uint32_t>();
        shs.hpIndex   = r.get<uint8_t>();
        shs.hull      = r.get<uint16_t>();
        shs.alive     = r.get<uint8_t>();
        if (!r.ok) break;
        stationHpsOut.push_back(shs);
    }
    return true;
}

} // namespace net
