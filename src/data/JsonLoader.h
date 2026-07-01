#pragma once
#include <algorithm>
#include <fstream>
#include <string>
#include <nlohmann/json.hpp>
#include "raylib.h"

// Minimal safe JSON accessor utilities.
// All getters return a default and never throw.
// Float/Int getters clamp to [lo, hi] so malicious or corrupt files
// cannot inject out-of-range stats into the game.
namespace JL {

inline nlohmann::json LoadFile(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return {};
    try {
        return nlohmann::json::parse(f, nullptr, /*exceptions=*/true, /*ignore_comments=*/true);
    } catch (...) {
        TraceLog(LOG_WARNING, "JL: failed to parse '%s' — using hardcoded defaults", path.c_str());
        return {};
    }
}

inline std::string Str(const nlohmann::json& j, const char* key, const char* def = "") {
    auto it = j.find(key);
    if (it == j.end() || !it->is_string()) return def;
    return it->get<std::string>();
}

inline bool Bool(const nlohmann::json& j, const char* key, bool def = false) {
    auto it = j.find(key);
    if (it == j.end() || !it->is_boolean()) return def;
    return it->get<bool>();
}

inline float Float(const nlohmann::json& j, const char* key, float def,
                   float lo = -1e30f, float hi = 1e30f) {
    auto it = j.find(key);
    float v = (it != j.end() && it->is_number()) ? it->get<float>() : def;
    return std::clamp(v, lo, hi);
}

inline int Int(const nlohmann::json& j, const char* key, int def,
               int lo = -1000000, int hi = 1000000) {
    auto it = j.find(key);
    int v = (it != j.end() && it->is_number_integer()) ? it->get<int>() : def;
    return std::clamp(v, lo, hi);
}

// Reads {"r":R,"g":G,"b":B,"a":A} — any missing channel falls back to def.
inline Color Clr(const nlohmann::json& j, const char* key, Color def) {
    auto it = j.find(key);
    if (it == j.end() || !it->is_object()) return def;
    const auto& c = *it;
    return {
        (unsigned char)Int(c, "r", def.r, 0, 255),
        (unsigned char)Int(c, "g", def.g, 0, 255),
        (unsigned char)Int(c, "b", def.b, 0, 255),
        (unsigned char)Int(c, "a", def.a, 0, 255)
    };
}

} // namespace JL
