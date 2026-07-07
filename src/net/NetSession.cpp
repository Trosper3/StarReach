// ENet's enet.h pulls in <winsock2.h>/<windows.h>, whose USER/GDI sections
// declare CloseWindow, ShowCursor and Rectangle as extern "C" — the same names
// raylib.h declares (reached here via Entity.h). Suppress those Win32 sections
// so the two headers coexist in this translation unit. ENet needs only the
// winsock types, not USER/GDI. These must be defined before any Win32 include.
#if defined(_WIN32)
  #define WIN32_LEAN_AND_MEAN
  #define NOGDI
  #define NOUSER
#endif

#include "NetSession.h"

#include <cstdio>
#include <enet/enet.h>

namespace net {

// ENet requires a single global init/teardown. Reference-counted so multiple
// NetSession objects (or repeated host/join cycles) are safe.
namespace {
int  g_enetRefs = 0;

bool EnetAcquire() {
    if (g_enetRefs == 0) {
        if (enet_initialize() != 0) {
            std::fprintf(stderr, "[net] enet_initialize failed\n");
            return false;
        }
    }
    ++g_enetRefs;
    return true;
}

void EnetRelease() {
    if (g_enetRefs > 0 && --g_enetRefs == 0) {
        enet_deinitialize();
    }
}

// Encode a client's networkId into the ENet peer's user data slot, so inbound
// packets/disconnects can be attributed without a separate map.
void   SetPeerId(ENetPeer* peer, uint32_t id) { peer->data = reinterpret_cast<void*>(static_cast<uintptr_t>(id)); }
uint32_t GetPeerId(ENetPeer* peer)            { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(peer->data)); }
} // namespace

NetSession::~NetSession() {
    Shutdown();
}

bool NetSession::StartHost(uint16_t port) {
    if (_role != NetRole::Offline) Shutdown();
    if (!EnetAcquire()) return false;

    ENetAddress address;
    address.host = ENET_HOST_ANY;
    address.port = port;

    _host = enet_host_create(&address, kMaxPlayers, kChannelCount, 0, 0);
    if (!_host) {
        std::fprintf(stderr, "[net] failed to create host on port %u\n", port);
        EnetRelease();
        return false;
    }

    _role           = NetRole::Host;
    _localNetworkId = 1;        // the host player is always network id 1
    _nextNetworkId  = 2;
    _connected      = true;     // host is trivially "connected" to itself
    std::printf("[net] hosting on port %u (network id 1)\n", port);
    return true;
}

bool NetSession::StartClient(const std::string& hostAddress, uint16_t port, Faction faction) {
    if (_role != NetRole::Offline) Shutdown();
    if (!EnetAcquire()) return false;
    _localFaction = faction;

    _host = enet_host_create(nullptr /*client*/, 1, kChannelCount, 0, 0);
    if (!_host) {
        std::fprintf(stderr, "[net] failed to create client host\n");
        EnetRelease();
        return false;
    }

    ENetAddress address;
    enet_address_set_host(&address, hostAddress.c_str());
    address.port = port;

    _serverPeer = enet_host_connect(_host, &address, kChannelCount, 0);
    if (!_serverPeer) {
        std::fprintf(stderr, "[net] no available peers to connect to %s:%u\n",
                     hostAddress.c_str(), port);
        enet_host_destroy(_host);
        _host = nullptr;
        EnetRelease();
        return false;
    }

    _role           = NetRole::Client;
    _localNetworkId = 0;        // assigned by the host's Welcome
    _connected      = false;    // set true once the handshake completes
    std::printf("[net] connecting to %s:%u ...\n", hostAddress.c_str(), port);
    return true;
}

void NetSession::Shutdown() {
    if (_role == NetRole::Offline) return;

    if (_serverPeer) {
        enet_peer_disconnect_now(_serverPeer, 0);
        _serverPeer = nullptr;
    }
    if (_host) {
        enet_host_destroy(_host);
        _host = nullptr;
    }
    EnetRelease();

    _role           = NetRole::Offline;
    _localNetworkId = 0;
    _connected      = false;
    pendingInputs.clear();
    latestSnapshots.clear();
    latestAsteroidSnapshots.clear();
    latestProjectileSnapshots.clear();
    snapshotDirty          = false;
    pendingPlayerDead      = false;
    pendingServerClosing   = false;
    newPeerIds.clear();
    disconnectedPeerIds.clear();
    pendingWorldSync.reset();
    pendingWarpNotifies.clear();
    pendingStationDeads.clear();
    newPeerFactions.clear();
    pendingDiscoverySync.reset();
    pendingSystemDiscoveries.clear();
}

void NetSession::Poll(float /*dt*/) {
    if (!_host) return;

    ENetEvent event;
    while (enet_host_service(_host, &event, 0) > 0) {
        switch (event.type) {

        case ENET_EVENT_TYPE_CONNECT:
            if (_role == NetRole::Host) {
                if (_nextNetworkId == 0) _nextNetworkId = 2;  // wraparound guard
                uint32_t assigned = _nextNetworkId++;
                SetPeerId(event.peer, assigned);
                auto pkt = EncodeWelcome(assigned);
                ENetPacket* p = enet_packet_create(pkt.data(), pkt.size(),
                                                   ENET_PACKET_FLAG_RELIABLE);
                enet_peer_send(event.peer, kChannelReliable, p);
                newPeerIds.push_back(assigned);  // SpaceFlight drains this to send WorldSync
                std::printf("[net] client joined -> network id %u\n", assigned);
            } else {
                // Client's connection to the host succeeded at the ENet level;
                // send Hello and wait for Welcome before marking connected.
                auto pkt = EncodeHello(_localFaction);
                ENetPacket* p = enet_packet_create(pkt.data(), pkt.size(),
                                                   ENET_PACKET_FLAG_RELIABLE);
                enet_peer_send(event.peer, kChannelReliable, p);
            }
            break;

        case ENET_EVENT_TYPE_RECEIVE:
            handlePacket(event.peer, event.packet->data, event.packet->dataLength);
            enet_packet_destroy(event.packet);
            break;

        case ENET_EVENT_TYPE_DISCONNECT:
            if (_role == NetRole::Host) {
                uint32_t leftId = GetPeerId(event.peer);
                std::printf("[net] client %u disconnected\n", leftId);
                if (leftId != 0) disconnectedPeerIds.push_back(leftId);
                SetPeerId(event.peer, 0);
            } else {
                std::printf("[net] disconnected from host\n");
                _connected          = false;
                _serverPeer         = nullptr;
                pendingServerClosing = true;  // let SpaceFlight return to menu
            }
            break;

        default:
            break;
        }
    }
}

void NetSession::handlePacket(ENetPeer* from, const uint8_t* data, size_t len) {
    MsgType type = PeekType(data, len);
    ByteReader r(data + 1, len - 1);   // skip the leading type byte

    switch (type) {

    case MsgType::Hello: {
        // Host validates the joining client's protocol version and records
        // their faction (for discovery pooling — see SpaceFlight's
        // _peerFaction/_peerFactionDiscovered).
        uint16_t version = 0;
        Faction  faction = Faction::Republic;
        if (!DecodeHello(r, version, faction)) break;
        if (version != kProtocolVersion) {
            auto pkt = EncodeReject(RejectReason::ProtocolMismatch);
            ENetPacket* p = enet_packet_create(pkt.data(), pkt.size(),
                                               ENET_PACKET_FLAG_RELIABLE);
            enet_peer_send(from, kChannelReliable, p);
            std::printf("[net] rejected client (protocol %u != %u)\n",
                        version, kProtocolVersion);
        } else {
            newPeerFactions.push_back({ GetPeerId(from), faction });
        }
        break;
    }

    case MsgType::Welcome: {
        uint32_t id = 0;
        if (!DecodeWelcome(r, id)) break;
        _localNetworkId = id;
        _connected      = true;
        std::printf("[net] joined host; assigned network id %u\n", id);
        break;
    }

    case MsgType::Reject: {
        RejectReason reason = RejectReason::ProtocolMismatch;
        DecodeReject(r, reason);
        std::fprintf(stderr, "[net] connection rejected (reason %u)\n",
                     unsigned(reason));
        break;
    }

    case MsgType::Input: {
        // Host receives a client's input command. Stamp it with the sender's
        // assigned id so a client can't drive another player's avatar.
        InputCommand cmd;
        if (!DecodeInput(r, cmd)) break;
        cmd.networkId = GetPeerId(from);
        pendingInputs.push_back(cmd);
        break;
    }

    case MsgType::Snapshot: {
        // Client receives authoritative state for one system (entities + asteroids + projectiles).
        uint32_t sysId = 0;
        std::vector<ecs::NetworkSnapshot> snaps;
        std::vector<AsteroidSnapshot>     aSnaps;
        std::vector<ProjectileSnapshot>   pSnaps;
        if (DecodeSnapshot(r, sysId, snaps, aSnaps, pSnaps)) {
            latestSnapshotSystemId      = sysId;
            latestSnapshots             = std::move(snaps);
            latestAsteroidSnapshots     = std::move(aSnaps);
            latestProjectileSnapshots   = std::move(pSnaps);
            snapshotDirty = true;
        }
        break;
    }

    case MsgType::PlayerDead: {
        pendingPlayerDead = true;
        break;
    }

    case MsgType::ServerClosing: {
        pendingServerClosing = true;
        break;
    }

    case MsgType::StationDead: {
        uint32_t sysId = 0, id = 0;
        if (DecodeStationDead(r, sysId, id)) pendingStationDeads.push_back({ sysId, id });
        break;
    }

    case MsgType::ClientWarpNotify: {
        // Host: a client warped; remember where it went so SpaceFlight can
        // attach it to (or spin up) that system and reply with a WorldSync.
        uint32_t sysId = 0;
        if (DecodeClientWarpNotify(r, sysId))
            pendingWarpNotifies.push_back({ GetPeerId(from), sysId });
        break;
    }

    case MsgType::WorldSync: {
        WorldSyncData ws;
        if (DecodeWorldSync(r, ws)) pendingWorldSync = ws;
        break;
    }

    case MsgType::DiscoverySync: {
        std::vector<uint32_t> ids;
        if (DecodeDiscoverySync(r, ids)) pendingDiscoverySync = std::move(ids);
        break;
    }

    case MsgType::SystemDiscovered: {
        uint32_t sysId = 0;
        if (DecodeSystemDiscovered(r, sysId)) pendingSystemDiscoveries.push_back(sysId);
        break;
    }

    default:
        break;
    }
}

void NetSession::HostSendSnapshot(const std::vector<uint32_t>&          peerIds,
                                  uint32_t                              systemId,
                                  const std::vector<ecs::Entity>&       entities,
                                  const std::vector<AsteroidSnapshot>&   asteroids,
                                  const std::vector<ProjectileSnapshot>&  projectiles) {
    if (_role != NetRole::Host || !_host || peerIds.empty()) return;

    std::vector<ecs::NetworkSnapshot> snaps;
    snaps.reserve(entities.size());
    for (const auto& e : entities) {
        if (e.id == 0 || e.network.networkId == 0) continue;
        ecs::NetworkSnapshot s;
        s.networkId    = e.network.networkId;
        s.position     = e.transform.position;
        s.velocity     = e.transform.velocity;
        s.rotation     = e.transform.rotation;
        s.shipNameHash = e.network.shipNameHash;
        snaps.push_back(s);
    }

    auto pkt = EncodeSnapshot(systemId, snaps, asteroids, projectiles);
    ENetPacket* p = enet_packet_create(pkt.data(), pkt.size(), 0 /*unreliable*/);
    bool sent = false;
    for (size_t i = 0; i < _host->peerCount; ++i) {
        ENetPeer* peer = &_host->peers[i];
        if (peer->state != ENET_PEER_STATE_CONNECTED) continue;
        uint32_t id = GetPeerId(peer);
        for (uint32_t want : peerIds) {
            if (want == id) {
                enet_peer_send(peer, kChannelUnreliable, p);
                sent = true;
                break;
            }
        }
    }
    if (!sent) enet_packet_destroy(p);  // nobody matched; don't leak the packet
}

void NetSession::HostSendPlayerDead(uint32_t networkId) {
    if (_role != NetRole::Host || !_host) return;
    auto pkt = EncodePlayerDead();
    ENetPacket* p = enet_packet_create(pkt.data(), pkt.size(), ENET_PACKET_FLAG_RELIABLE);
    for (size_t i = 0; i < _host->peerCount; ++i) {
        ENetPeer* peer = &_host->peers[i];
        if (peer->state == ENET_PEER_STATE_CONNECTED && GetPeerId(peer) == networkId) {
            enet_peer_send(peer, kChannelReliable, p);
            std::printf("[net] sent PlayerDead to network id %u\n", networkId);
            return;
        }
    }
    enet_packet_destroy(p);
}

void NetSession::HostBroadcastStationDead(uint32_t systemId, uint32_t stationId) {
    if (_role != NetRole::Host || !_host) return;
    auto pkt = EncodeStationDead(systemId, stationId);
    ENetPacket* p = enet_packet_create(pkt.data(), pkt.size(), ENET_PACKET_FLAG_RELIABLE);
    enet_host_broadcast(_host, kChannelReliable, p);
    std::printf("[net] broadcast StationDead system %u id %u\n", systemId, stationId);
}

void NetSession::BroadcastServerClosing() {
    if (_role != NetRole::Host || !_host) return;
    auto pkt = EncodeServerClosing();
    ENetPacket* p = enet_packet_create(pkt.data(), pkt.size(), ENET_PACKET_FLAG_RELIABLE);
    enet_host_broadcast(_host, kChannelReliable, p);
    enet_host_flush(_host);
    std::printf("[net] broadcast ServerClosing\n");
}

void NetSession::HostSendWorldSync(uint32_t peerId, const WorldSyncData& ws) {
    if (_role != NetRole::Host || !_host) return;

    auto pkt = EncodeWorldSync(ws);
    ENetPacket* p = enet_packet_create(pkt.data(), pkt.size(), ENET_PACKET_FLAG_RELIABLE);

    // Walk the peer list to find the matching peer by stored id.
    for (size_t i = 0; i < _host->peerCount; ++i) {
        ENetPeer* peer = &_host->peers[i];
        if (peer->state == ENET_PEER_STATE_CONNECTED && GetPeerId(peer) == peerId) {
            enet_peer_send(peer, kChannelReliable, p);
            return;
        }
    }
    enet_packet_destroy(p);  // no matching peer; discard
}

void NetSession::HostSendDiscoverySync(uint32_t peerId, const std::vector<uint32_t>& systemIds) {
    if (_role != NetRole::Host || !_host) return;

    auto pkt = EncodeDiscoverySync(systemIds);
    ENetPacket* p = enet_packet_create(pkt.data(), pkt.size(), ENET_PACKET_FLAG_RELIABLE);
    for (size_t i = 0; i < _host->peerCount; ++i) {
        ENetPeer* peer = &_host->peers[i];
        if (peer->state == ENET_PEER_STATE_CONNECTED && GetPeerId(peer) == peerId) {
            enet_peer_send(peer, kChannelReliable, p);
            return;
        }
    }
    enet_packet_destroy(p);  // no matching peer; discard
}

void NetSession::HostBroadcastSystemDiscovered(const std::vector<uint32_t>& peerIds, uint32_t systemId) {
    if (_role != NetRole::Host || !_host || peerIds.empty()) return;

    auto pkt = EncodeSystemDiscovered(systemId);
    ENetPacket* p = enet_packet_create(pkt.data(), pkt.size(), ENET_PACKET_FLAG_RELIABLE);
    bool sent = false;
    for (size_t i = 0; i < _host->peerCount; ++i) {
        ENetPeer* peer = &_host->peers[i];
        if (peer->state != ENET_PEER_STATE_CONNECTED) continue;
        uint32_t id = GetPeerId(peer);
        for (uint32_t want : peerIds) {
            if (want == id) {
                enet_peer_send(peer, kChannelReliable, p);
                sent = true;
                break;
            }
        }
    }
    if (!sent) enet_packet_destroy(p);  // nobody matched; don't leak the packet
}

void NetSession::ClientSendInput(const InputCommand& cmd) {
    if (_role != NetRole::Client || !_serverPeer || !_connected) return;

    auto pkt = EncodeInput(cmd);
    ENetPacket* p = enet_packet_create(pkt.data(), pkt.size(), 0 /*unreliable*/);
    enet_peer_send(_serverPeer, kChannelUnreliable, p);
}

void NetSession::ClientSendWarpNotify(uint32_t systemId) {
    if (_role != NetRole::Client || !_serverPeer || !_connected) return;

    auto pkt = EncodeClientWarpNotify(systemId);
    ENetPacket* p = enet_packet_create(pkt.data(), pkt.size(), ENET_PACKET_FLAG_RELIABLE);
    enet_peer_send(_serverPeer, kChannelReliable, p);
    std::printf("[net] sent ClientWarpNotify for system %u\n", systemId);
}

} // namespace net
