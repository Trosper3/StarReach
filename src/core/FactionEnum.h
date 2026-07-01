#pragma once
#include <cstdint>

// Canonical 9-faction enum — index doubles as matrix row/column
enum class Faction : uint8_t {
    Republic = 0,   // Rep
    Zenith   = 1,   // Zen
    Korrath  = 2,   // Kor
    Merchant = 3,   // Mer
    Eden     = 4,   // Ede
    Reavers  = 5,   // Rea
    Forge    = 6,   // For
    Conclave = 7,   // Con
    Void     = 8,   // Voi
    COUNT    = 9
};
