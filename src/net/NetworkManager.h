#pragma once
#include "NetSession.h"

// Global NetSession accessor — survives mode transitions because GameManager
// destroys and recreates each IGameMode on transition; the session must live
// outside that lifecycle. Meyers-singleton; destroyed at program exit after
// NetSession::~NetSession() calls Shutdown() and releases ENet.
namespace net {

inline NetSession& Game() {
    static NetSession s;
    return s;
}

} // namespace net
