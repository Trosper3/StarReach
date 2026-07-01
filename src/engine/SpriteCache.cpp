#include "engine/SpriteCache.h"
#include <algorithm>
#include <cstdio>
#include <sstream>

std::unordered_map<std::string, Texture2D>  SpriteCache::s_cache;
std::unordered_map<std::string, SpriteMask> SpriteCache::s_masks;

static Color ResolvePixel(char c, Color primary, Color accent) {
    switch (c) {
        case '#': return primary;
        case '+': return accent;
        case 'h': return { 210, 220, 235, 255 };
        case 'd': return {  35,  40,  50, 255 };
        case 'm': return {  95, 105, 120, 255 };
        case 'l': return {  18,  20,  25, 255 };
        case 's': return SKYBLUE;
        case 'n': return {  12,  20,  48, 255 };
        case 'e': return ORANGE;
        case 'b': return { 255, 245, 155, 255 };
        case 'x': return {  50,  28,   8, 255 };
        case 'w': return RED;
        case 'r': return { 255, 100,  70, 255 };
        case '0': return BLACK;
        case '*': return WHITE;
        case 'c': return YELLOW;
        case 'g': return {  60, 200, 100, 255 };
        case '.': return BLANK;
        case ' ': return BLANK;
        default:  return GRAY;
    }
}

// Resolves a per-pixel element string to a Color.
// Length 1 → char palette lookup.
// Length 8 → RRGGBBAA hex (e.g. "CAC3B7FF" or "00000000" for transparent).
static Color ResolveElement(const std::string& elem, Color primary, Color accent) {
    if (elem.size() == 1)
        return ResolvePixel(elem[0], primary, accent);
    if (elem.size() == 8) {
        unsigned int r = 0, g = 0, b = 0, a = 0;
        std::sscanf(elem.c_str(), "%02X%02X%02X%02X", &r, &g, &b, &a);
        return Color{ (uint8_t)r, (uint8_t)g, (uint8_t)b, (uint8_t)a };
    }
    return GRAY;
}

std::string SpriteCache::MakeKey(const std::vector<std::vector<std::string>>& arr,
                                  Color p, Color a) {
    std::ostringstream oss;
    for (const auto& row : arr) {
        for (const auto& elem : row) oss << elem << ',';
        oss << '|';
    }
    oss << std::hex
        << (int)p.r << (int)p.g << (int)p.b << (int)p.a << '_'
        << (int)a.r << (int)a.g << (int)a.b << (int)a.a;
    return oss.str();
}

static Texture2D DoBake(const std::vector<std::vector<std::string>>& designArray,
                         Color primary, Color accent) {
    int height = (int)designArray.size();
    int width  = 0;
    for (const auto& row : designArray)
        width = std::max(width, (int)row.size());

    Image img = GenImageColor(width, height, BLANK);
    ImageFormat(&img, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);

    static const std::string DOT = ".";
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            const std::string& elem = (x < (int)designArray[y].size())
                                      ? designArray[y][x] : DOT;
            ImageDrawPixel(&img, x, y, ResolveElement(elem, primary, accent));
        }

    Texture2D tex = LoadTextureFromImage(img);
    UnloadImage(img);
    SetTextureFilter(tex, TEXTURE_FILTER_POINT);
    return tex;
}

Texture2D* SpriteCache::Bake(const std::vector<std::vector<std::string>>& designArray,
                              Color factionPrimary, Color factionAccent) {
    if (designArray.empty()) return nullptr;
    std::string key = MakeKey(designArray, factionPrimary, factionAccent);
    auto it = s_cache.find(key);
    if (it != s_cache.end()) return &it->second;
    auto [ins, ok] = s_cache.emplace(key, DoBake(designArray, factionPrimary, factionAccent));
    return &ins->second;
}

static SpriteMask BuildMask(const std::vector<std::vector<std::string>>& designArray) {
    SpriteMask mask;
    mask.height = (int)designArray.size();
    mask.width  = 0;
    for (const auto& row : designArray)
        mask.width = std::max(mask.width, (int)row.size());
    mask.bits.assign((size_t)(mask.width * mask.height), false);

    static const std::string DOT = ".";
    for (int y = 0; y < mask.height; ++y)
        for (int x = 0; x < mask.width; ++x) {
            const std::string& elem = (x < (int)designArray[y].size())
                                      ? designArray[y][x] : DOT;
            // Transparent if '.' / ' ' / "00000000" (alpha byte == 0)
            bool opaque = true;
            if (elem.size() == 1)
                opaque = (elem[0] != '.' && elem[0] != ' ');
            else if (elem.size() == 8)
                opaque = (elem[6] != '0' || elem[7] != '0');  // last two hex digits = alpha
            mask.bits[(size_t)(y * mask.width + x)] = opaque;
        }
    return mask;
}

Texture2D* SpriteCache::BakeForId(const std::string& id, Color factionPrimary, Color factionAccent,
                                   const std::vector<std::vector<std::string>>& designArray) {
    if (designArray.empty()) return nullptr;
    char buf[256];
    std::snprintf(buf, sizeof(buf), "id:%s_%02X%02X%02X_%02X%02X%02X",
                  id.c_str(),
                  factionPrimary.r, factionPrimary.g, factionPrimary.b,
                  factionAccent.r,  factionAccent.g,  factionAccent.b);
    std::string key(buf);
    auto it = s_cache.find(key);
    if (it != s_cache.end()) return &it->second;

    // New entry: bake texture AND build collision mask in one pass
    s_masks.emplace(key, BuildMask(designArray));
    auto [ins, ok] = s_cache.emplace(key, DoBake(designArray, factionPrimary, factionAccent));
    return &ins->second;
}

const SpriteMask* SpriteCache::GetMask(const std::string& id, Color primary, Color accent) {
    char buf[256];
    std::snprintf(buf, sizeof(buf), "id:%s_%02X%02X%02X_%02X%02X%02X",
                  id.c_str(), primary.r, primary.g, primary.b,
                  accent.r,   accent.g,  accent.b);
    auto it = s_masks.find(std::string(buf));
    return (it == s_masks.end()) ? nullptr : &it->second;
}

void SpriteCache::Cleanup() {
    for (auto& [key, tex] : s_cache)
        UnloadTexture(tex);
    s_cache.clear();
}
