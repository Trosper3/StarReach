#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include "NetCommon.h"
#include "Protocol.h"
#include "../core/FactionEnum.h"
#include "../shared/Entity.h"

// Forward-declare ENet's host/peer so this header pulls no enet include.
// (The .cpp includes <enet/enet.h>.)
struct _ENetHost;
struct _ENetPeer;
typedef struct _ENetHost ENetHost;
typedef struct _ENetPeer ENetPeer;

namespace net {

// One object drives the entire multiplayer connection for either role.
//
// Typical per-frame use (host):
//     session.Poll(dt);                          // pump ENet, fill pendingInputs
//     ApplyInputsToEntities(session.pendingInputs, world);
//     session.pendingInputs.clear();
//     session.HostSendSnapshot(occupants, sysId, world);  // ~20 Hz, per system
//
// Typical per-frame use (client):
//     session.Poll(dt);                          // fills latestSnapshots
//     ecs::NetworkSyncSystem::Update(world, session.latestSnapshots);
//     session.ClientSendInput(localCmd);
//
// All methods are no-ops when role == Offline, so the game loop can call them
// unconditionally in single-player.
class NetSession {
public:
    NetSession() = default;
    ~NetSession();

    NetSession(const NetSession&)            = delete;
    NetSession& operator=(const NetSession&) = delete;

    // ── Lifecycle ───────────────────────────────────────────────────────────
    bool StartHost(uint16_t port = kDefaultPort);
    // faction is this client's own player faction (sent to the host via Hello,
    // once the ENet connection succeeds) — see SpaceFlight's discovery-pooling
    // notes on _peerFaction/_peerFactionDiscovered for why the host needs it.
    bool StartClient(const std::string& hostAddress, uint16_t port = kDefaultPort,
                      Faction faction = Faction::Republic);
    void Shutdown();

    NetRole Role()      const { return _role; }
    bool    IsOnline()  const { return _role != NetRole::Offline; }
    bool    IsHost()    const { return _role == NetRole::Host; }
    bool    IsClient()  const { return _role == NetRole::Client; }

    // Client: 0 until the host's Welcome assigns one. Host: always 1 (the host
    // player owns network id 1).
    uint32_t LocalNetworkId() const { return _localNetworkId; }

    // Client: true once the handshake with the host has completed.
    bool IsConnected() const { return _connected; }

    // ── Per-frame pump ───────────────────────────────────────────────────────
    // Services ENet, processes connects/disconnects and inbound packets.
    // Host  -> appends received InputCommands to pendingInputs.
    // Client-> replaces latestSnapshots with the newest received world state.
    void Poll(float dt);

    // ── Host -> clients ───────────────────────────────────────────────────────
    // Builds one system's snapshot from the given entities (+ asteroids +
    // projectiles) and sends it unreliably to exactly the listed peers — the
    // occupants of that system. Encodes once; the packet is shared across sends.
    void HostSendSnapshot(const std::vector<uint32_t>&          peerIds,
                          uint32_t                              systemId,
                          const std::vector<ecs::Entity>&       entities,
                          const std::vector<AsteroidSnapshot>&  asteroids   = {},
                          const std::vector<ProjectileSnapshot>& projectiles = {},
                          const std::vector<CapitalHardpointSnapshot>& capitals = {},
                          const std::vector<PlayerStationSnapshot>& stations = {},
                          const std::vector<PlayerStationHardpointSnapshot>& stationHardpoints = {});

    // Send a system's seed + live-state diff to a specific peer so they can
    // generate the map and reconcile it (sent at join and on warp arrival).
    void HostSendWorldSync(uint32_t peerId, const WorldSyncData& ws);

    // Tell a specific client their player was killed (reliable, targeted).
    void HostSendPlayerDead(uint32_t networkId);

    // Broadcast to all clients that the server is shutting down, then flush.
    void BroadcastServerClosing();

    // Broadcast that a station in `systemId` was destroyed (reliable). Clients
    // in other systems ignore it; they get station state via WorldSync instead.
    void HostBroadcastStationDead(uint32_t systemId, uint32_t stationId);

    // Send one peer the full discovered-system-id list for their faction —
    // used right after their Hello reveals which faction that is, so a
    // joining/reconnecting party member catches up on everything their
    // faction-mates have already found (see SpaceFlight's discovery pooling).
    void HostSendDiscoverySync(uint32_t peerId, const std::vector<uint32_t>& systemIds);

    // Broadcast a single newly-discovered system id to exactly the listed
    // peers (a party's other same-faction members) — the live counterpart to
    // HostSendDiscoverySync's one-time bulk catch-up.
    void HostBroadcastSystemDiscovered(const std::vector<uint32_t>& peerIds, uint32_t systemId);

    // ── Client -> host ────────────────────────────────────────────────────────
    void ClientSendInput(const InputCommand& cmd);

    // Tell the host this client's warp cinematic reached black and it now
    // occupies `systemId`; the host replies with a WorldSync for that system.
    void ClientSendWarpNotify(uint32_t systemId);

    // Ask the host to build a player station / place a friendly NPC ship at
    // (posX,posY) in the client's current system. The client does NOT create
    // the object locally — it waits for the host's next Snapshot broadcast to
    // show it (see [[tasks-multiplayer]] Epic C). No-op for the host/offline
    // (single-player and the host keep their existing direct-call path).
    void ClientSendBuildStationRequest(const std::string& stationDefId, float posX, float posY);
    void ClientSendPlaceShipRequest(const std::string& shipDefId, float posX, float posY);

    // ── I/O buffers consumed by the game loop ────────────────────────────────
    std::vector<InputCommand>          pendingInputs;              // host side
    std::vector<ecs::NetworkSnapshot>  latestSnapshots;            // client side
    std::vector<AsteroidSnapshot>      latestAsteroidSnapshots;    // client side
    std::vector<ProjectileSnapshot>    latestProjectileSnapshots;  // client side
    std::vector<CapitalHardpointSnapshot> latestCapitalSnapshots;  // client side
    std::vector<PlayerStationSnapshot>    latestPlayerStationSnapshots;          // client side
    std::vector<PlayerStationHardpointSnapshot> latestPlayerStationHardpointSnapshots; // client side
    uint32_t                           latestSnapshotSystemId = 0; // client: system the snapshot describes
    bool                               snapshotDirty    = false;   // client: true when a new snapshot arrived
    bool                               pendingPlayerDead = false;  // client: server killed this player
    bool                               pendingServerClosing = false; // client: host shut down
    std::vector<uint32_t>              newPeerIds;          // host: ids of peers that just joined
    std::vector<uint32_t>              disconnectedPeerIds; // host: ids of peers that just left
    std::optional<WorldSyncData>       pendingWorldSync;    // client: set on WorldSync receipt
    // Host: (peerId, systemId) warp notifications awaiting a WorldSync reply.
    std::vector<std::pair<uint32_t, uint32_t>> pendingWarpNotifies;
    // Client: (systemId, stationId) pairs destroyed on the server.
    std::vector<std::pair<uint32_t, uint32_t>> pendingStationDeads;
    // Host: (peerId, faction) pairs from newly-arrived Hello packets — arrives
    // separately from (and usually a tick or more after) newPeerIds, since
    // Hello is a follow-up packet rather than part of the ENet connect event.
    std::vector<std::pair<uint32_t, Faction>>  newPeerFactions;
    // Client: set once a DiscoverySync (full same-faction discovered-set
    // catch-up) arrives.
    std::optional<std::vector<uint32_t>>       pendingDiscoverySync;
    // Client: system ids from live SystemDiscovered broadcasts, queued for
    // the next drain.
    std::vector<uint32_t>                      pendingSystemDiscoveries;
    // Host: build/placement requests from clients, awaiting drain into the
    // host's own world (see [[tasks-multiplayer]] Epic C).
    std::vector<BuildStationRequest>           pendingBuildStationRequests;
    std::vector<PlaceShipRequest>              pendingPlaceShipRequests;

private:
    void handlePacket(ENetPeer* from, const uint8_t* data, size_t len);

    NetRole   _role           = NetRole::Offline;
    ENetHost* _host           = nullptr;   // ENet host object (both roles)
    ENetPeer* _serverPeer     = nullptr;   // client: the host we connected to
    uint32_t  _localNetworkId = 0;
    uint32_t  _nextNetworkId  = 2;         // host: ids handed to joining clients (1 = host)
    bool      _connected      = false;
    Faction   _localFaction   = Faction::Republic; // client: sent to the host via Hello
};

} // namespace net
