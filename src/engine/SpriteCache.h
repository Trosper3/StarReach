#pragma once
#include "raylib.h"
#include <string>
#include <unordered_map>
#include <vector>

// Converts design arrays into cached Texture2D objects.
//
// Each element of the inner vector is one pixel, interpreted by length:
//   1-char  → palette code: '#' primary  '+' accent  '.' transparent
//             'h' highlight  'm' mid-panel  'd' shadow  'e' engine  etc.
//   8-chars → RRGGBBAA hex  (raw color — used by img2pixel imported sprites)
//             "00000000" = fully transparent
struct Palette {
    std::unordered_map<char, Color> colors;
};

// Flat bitmask of non-transparent pixels, stored in row-major order.
// Used for pixel-accurate collision narrow-phase after circle broad-phase passes.
struct SpriteMask {
    int width  = 0;
    int height = 0;
    std::vector<bool> bits;   // bits[y * width + x] = true means opaque

    bool Test(int x, int y) const {
        if (x < 0 || y < 0 || x >= width || y >= height) return false;
        return bits[(size_t)(y * width + x)];
    }
};

class SpriteCache {
public:
    // Returns a stable pointer into the internal cache.
    // factionPrimary colors '#' pixels; factionAccent colors '+' pixels.
    // Accepts both old char-row format (expanded at load time) and new per-pixel hex format.
    static Texture2D* Bake(const std::vector<std::vector<std::string>>& designArray,
                           Color factionPrimary,
                           Color factionAccent = WHITE);

    // Fast path for named assets: uses "id:name_RRGGBB_RRGGBB" as key instead of
    // hashing all pixel data. Cache hit is O(id.length) instead of O(pixels).
    static Texture2D* BakeForId(const std::string& id,
                                Color factionPrimary,
                                Color factionAccent,
                                const std::vector<std::vector<std::string>>& designArray);

    // Returns the collision mask for a sprite baked with BakeForId.
    // Returns nullptr if not found (call BakeForId first).
    static const SpriteMask* GetMask(const std::string& id, Color primary, Color accent);

    static void Cleanup();

    static std::unordered_map<std::string, Palette> _factionPalettes;

private:
    static std::string MakeKey(const std::vector<std::vector<std::string>>& designArray,
                               Color primary, Color accent);

    static std::unordered_map<std::string, Texture2D>  s_cache;
    static std::unordered_map<std::string, SpriteMask> s_masks;
};
