#pragma once
#include <string>
#include <vector>

// Simplified top-level ship classification: Fighter-scale vs Capital-scale
enum class ShipType  { Fighter, Capital };
enum class ShipGrade { Common, Uncommon, Remarkable, Rare, Epic, Legendary, Mythical };

struct ShipSlots {
    int weapon     = 2;
    int armor      = 1;
    int shield     = 1;
    int engine     = 1;
    int hyperdrive = 1;
    int auxiliary  = 0;
};

// Capital ships group their modules into named hardpoints (weapon batteries,
// engine banks, etc.). Each hardpoint has its own limited slot counts.
// Mechanics for managing hardpoints are deferred until capital ship piloting
// is designed; this struct is structural/data only for now.
enum class HardpointType {
    WeaponBattery,
    EngineBank,
    ArmorPlating,
    ShieldArray,
    CommandBridge
};

struct HardpointDef {
    HardpointType type  = HardpointType::WeaponBattery;
    std::string   name;
    ShipSlots     slots;
};

struct ShipDef {
    std::string id;
    std::string displayName;
    std::string spritePath;
    std::string paletteId;
    float     maxHull   = 100.0f;
    float     thrust    = 0.0f;    // base 0 — engine module provides all movement
    float     turnSpeed = 0.0f;
    float     projSpeed = 0.0f;    // base 0 — weapon module provides all firing
    float     projRange = 0.0f;
    float     fireRate  = 0.0f;
    float     radius    = 18.0f;
    ShipSlots slots;
    ShipType  shipType   = ShipType::Fighter;
    ShipGrade grade      = ShipGrade::Common;
    bool      flipSprite  = false;
    float     pixelScale  = 1.0f;
    std::vector<std::vector<std::string>> designArray;
    std::vector<HardpointDef> hardpoints;  // non-empty for Capital ships
};
