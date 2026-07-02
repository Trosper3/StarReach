#pragma once
#include "core/StarSystem.h"
#include "raylib.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// Galactic distances from Sol (0, 0) — hyperdrive tier requirements:
//   Short-Jump   3 000 u
//   Sector Drive 7 500 u
//   Warp Core   15 000 u
//   Fold Engine 30 000 u
//   Quantum Leap 60 000 u
//   Void Piercer100 000 u
//   Singularity  1 000 000 u
//
// The galaxy is a procedurally generated grid-hash lattice keyed off a single
// master seed (see Init()) — no per-system data is ever stored in bulk.
// Generate()/ById() compute a system's seed/position on demand in O(1);
// QueryRegion() is the only entry point that touches more than one system at
// a time, and it's bounded to a caller-supplied result budget regardless of
// how large a world-space rect it's asked about (see its comment below).

namespace StarSystemRegistry {

inline constexpr unsigned int kDefaultCount = 1'000'000u;   // adjustable via Init()
inline constexpr float        kGalaxySpan   = 2'000'000.0f; // world spans [-span/2, +span/2]

namespace detail {

// Simple integer hash (Wang/xorshift-style mix) used to derive per-system
// seeds and position jitter from (masterSeed, id, salt) — deterministic,
// no allocation, cheap enough to call thousands of times per frame.
inline uint32_t Hash32(uint32_t a, uint32_t b, uint32_t salt) {
    uint32_t h = a * 0x9E3779B1u ^ b * 0x85EBCA77u ^ salt * 0xC2B2AE3Du;
    h ^= h >> 15; h *= 0x2545F491u;
    h ^= h >> 13; h *= 0x9E3779B1u;
    h ^= h >> 16;
    return h;
}

inline std::string GenerateName(unsigned int seed) {
    auto rng = [s = seed + 1u]() mutable -> unsigned int {
        s ^= s << 13u;
        s ^= s >> 17u;
        s ^= s << 5u;
        return s;
    };

    static const char* kOnsets[] = {
        "b","c","d","f","g","h","k","l","m","n","p","r","s","t","v","z",
        "br","cr","dr","fr","gr","pr","tr","kh","th","sh","zh","vr","xl"
    };
    static const char* kVowels[] = {
        "a","e","i","o","u","ae","ei","io","ou","ai","ia","eo"
    };
    static const char* kCodas[] = {
        "","n","r","s","x","k","l","m","nd","st","rk","rx","th"
    };
    const int nOnsets = (int)(sizeof(kOnsets) / sizeof(kOnsets[0]));
    const int nVowels = (int)(sizeof(kVowels) / sizeof(kVowels[0]));
    const int nCodas  = (int)(sizeof(kCodas)  / sizeof(kCodas[0]));

    int syllables = 2 + (int)(rng() % 2);
    std::string name;

    for (int i = 0; i < syllables; ++i) {
        if (i == 0 && rng() % 4 == 0) {
            // vowel-initial start
        } else {
            name += kOnsets[rng() % nOnsets];
        }
        name += kVowels[rng() % nVowels];
        if (rng() % 2 == 0)
            name += kCodas[1 + rng() % (nCodas - 1)];
    }

    if (!name.empty())
        name[0] = (char)toupper((unsigned char)name[0]);
    return name;
}

inline unsigned int& MasterSeedRef() { static unsigned int s = 1u; return s; }
inline unsigned int& CountRef()      { static unsigned int s = kDefaultCount; return s; }

} // namespace detail

// Turns a player-typed seed string into a deterministic master seed (FNV-1a).
inline uint32_t HashSeedString(const std::string& text) {
    uint32_t h = 2166136261u;
    for (unsigned char c : text) {
        h ^= c;
        h *= 16777619u;
    }
    return h == 0u ? 1u : h; // 0 is reserved as "uninitialized"
}

// Must be called once per game session (New Game / Load / multiplayer
// WorldSync) before any other function in this namespace is used.
inline void Init(unsigned int masterSeed, unsigned int count = kDefaultCount) {
    detail::MasterSeedRef() = masterSeed == 0u ? 1u : masterSeed;
    detail::CountRef()      = count == 0u ? 1u : count;
}

inline unsigned int Count() { return detail::CountRef(); }

// Systems live on a GridDim x GridDim lattice (GridDim = ceil(sqrt(Count()))
// so the default 1,000,000 is an exact 1000x1000 grid). id = cy*dim + cx + 1.
inline unsigned int GridDim() {
    return (unsigned int)std::ceil(std::sqrt((double)detail::CountRef()));
}

inline float CellSize() { return kGalaxySpan / (float)GridDim(); }

// O(1), no storage — computes a system's seed + galactic position purely
// from its id and the current master seed. id == 1 is special-cased to the
// origin (home/Sol). Caller must ensure id is in [1, Count()].
inline StarSystem Generate(unsigned int id) {
    StarSystem s;
    s.id   = id;
    s.seed = detail::Hash32(detail::MasterSeedRef(), id, 0xA53Fu);

    if (id == 1) {
        s.galacticPos = { 0.0f, 0.0f };
        return s;
    }

    unsigned int dim = GridDim();
    unsigned int idx = id - 1;
    unsigned int cx  = idx % dim;
    unsigned int cy  = idx / dim;

    float cellSize     = CellSize();
    float originOffset = kGalaxySpan * 0.5f;
    float cellCX = (cx + 0.5f) * cellSize - originOffset;
    float cellCY = (cy + 0.5f) * cellSize - originOffset;

    // Jitter within the cell (up to 80% of cell size) so systems don't sit
    // on a visibly perfect grid.
    uint32_t jh = detail::Hash32(detail::MasterSeedRef(), id, 0x51ED27u);
    float jx = ((jh & 0xFFFFu) / 65535.0f - 0.5f) * cellSize * 0.8f;
    float jy = (((jh >> 16) & 0xFFFFu) / 65535.0f - 0.5f) * cellSize * 0.8f;

    s.galacticPos = { cellCX + jx, cellCY + jy };
    return s;
}

inline std::optional<StarSystem> ById(unsigned int id) {
    if (id == 0u || id > detail::CountRef()) return std::nullopt;
    return Generate(id);
}

// The only function that touches more than a handful of systems. Bounds the
// number of systems returned to maxResults regardless of how large worldRect
// is — beyond that budget it strides through the grid so the result set
// thins out uniformly rather than growing unbounded. This is what makes it
// safe to call every frame while the galactic map is open, at any zoom
// level, even though the underlying galaxy may hold 1,000,000 systems.
inline std::vector<StarSystem> QueryRegion(Rectangle worldRect, int maxResults) {
    std::vector<StarSystem> out;

    unsigned int dim         = GridDim();
    float        cellSize    = CellSize();
    float        originOffset = kGalaxySpan * 0.5f;

    auto ToCell = [&](float world) -> long long {
        return (long long)std::floor((world + originOffset) / cellSize);
    };

    long long cx0 = std::max(0LL, ToCell(worldRect.x));
    long long cx1 = std::min((long long)dim - 1, ToCell(worldRect.x + worldRect.width));
    long long cy0 = std::max(0LL, ToCell(worldRect.y));
    long long cy1 = std::min((long long)dim - 1, ToCell(worldRect.y + worldRect.height));
    if (cx1 < cx0 || cy1 < cy0) return out;

    long long cellsWide  = cx1 - cx0 + 1;
    long long cellsHigh  = cy1 - cy0 + 1;
    long long totalCells = cellsWide * cellsHigh;

    long long stride = 1;
    if (maxResults > 0 && totalCells > (long long)maxResults) {
        stride = (long long)std::ceil(std::sqrt((double)totalCells / (double)maxResults));
        stride = std::max(1LL, stride);
    }

    out.reserve((size_t)std::min<long long>((cellsWide / stride + 1) * (cellsHigh / stride + 1), 20000));
    for (long long cy = cy0; cy <= cy1; cy += stride) {
        for (long long cx = cx0; cx <= cx1; cx += stride) {
            unsigned int id = (unsigned int)(cy * (long long)dim + cx + 1);
            if (id == 0u || id > detail::CountRef()) continue;
            out.push_back(Generate(id));
        }
    }
    return out;
}

// Lazy name resolution — only call this for systems actually about to be
// labeled on screen (current/selected/discovered), never for the bulk
// decorative background returned by QueryRegion().
inline std::string NameOf(unsigned int seed) { return detail::GenerateName(seed); }

} // namespace StarSystemRegistry
