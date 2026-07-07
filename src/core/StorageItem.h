#pragma once
#include "core/Module.h"
#include "data/PlayerStationDef.h"
#include <string>

enum class StorageItemType { Empty, Material, Module, Hardpoint };

struct StorageItem {
    StorageItemType type = StorageItemType::Empty;
    std::string     displayName;
    std::string     materialId;   // populated when type == Material
    int             count  = 0;   // materials only, max StorageMenu::MaxStack
    ModuleDef       module;       // populated when type == Module
    StationHardpointDef hardpoint; // populated when type == Hardpoint
};
