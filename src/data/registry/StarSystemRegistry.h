#pragma once
#include "core/StarSystem.h"
#include <cctype>
#include <vector>

// Galactic distances from Sol (0, 0) — hyperdrive tier requirements:
//   Short-Jump   3 000 u :  id 2 ~2 777
//   Sector Drive 7 500 u :  id 3 ~6 185
//   Warp Core   15 000 u :  id 4 ~11 403  /  id 5 ~13 416
//   Fold Engine 30 000 u :  id 6 ~23 414  /  id 7 ~28 284
//   Quantum Leap 60 000 u :  id 8 ~54 083
//   Void Piercer100 000 u :  id 9 ~94 340
//   Singularity  1 000 000 :  id10 ~360 555

namespace StarSystemRegistry {

namespace detail {

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

inline std::vector<StarSystem> BuildSystems() {
    struct Raw { unsigned int id, seed; float x, y; };
    static const Raw kRaw[] = {
        {  1, 1001,       0.0f,       0.0f },
        {  2, 2002,    2500.0f,    1200.0f },
        {  3, 3003,    6000.0f,   -1500.0f },
        {  4, 4004,   11000.0f,    3000.0f },
        {  5, 5005,  -12000.0f,    6000.0f },
        {  6, 6006,   22000.0f,   -8000.0f },
        {  7, 7007,  -20000.0f,   20000.0f },
        {  8, 8008,   45000.0f,   30000.0f },
        {  9, 9009,  -80000.0f,   50000.0f },
        { 10, 1010,  300000.0f, -200000.0f },
    };
    std::vector<StarSystem> v;
    v.reserve(sizeof(kRaw) / sizeof(kRaw[0]));
    for (const Raw& r : kRaw)
        v.push_back({ r.id, GenerateName(r.seed), r.seed, { r.x, r.y } });
    return v;
}

} // namespace detail

inline const std::vector<StarSystem>& All() {
    static const std::vector<StarSystem> kSystems = detail::BuildSystems();
    return kSystems;
}

inline const StarSystem* ById(unsigned int id) {
    for (const StarSystem& s : All())
        if (s.id == id) return &s;
    return nullptr;
}

} // namespace StarSystemRegistry
