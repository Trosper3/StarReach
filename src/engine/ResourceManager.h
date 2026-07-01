#pragma once
#include "raylib.h"
#include <string>
#include <unordered_map>

// Loads and caches PNG/image assets from disk.
// Returns stable pointers — pointers are invalidated only by Cleanup().
class ResourceManager {
public:
    // Returns nullptr if the file cannot be loaded.
    static Texture2D* Load(const std::string& path);

    static void Cleanup();

private:
    static std::unordered_map<std::string, Texture2D> s_cache;
};
