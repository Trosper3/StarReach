#pragma once
#include <cstdint>

struct NetworkComponent {
    uint32_t networkId     = 0;
    bool     isLocalPlayer = false;
    uint32_t shipNameHash  = 0;   // fnv32 of shipTypeId (0 = player entity)
    uint8_t  npcFactionIdx = 0;   // 0=Friendly/player, 1=Neutral, 2=Hostile
};
