#include "engine/ResourceManager.h"

std::unordered_map<std::string, Texture2D> ResourceManager::s_cache;

Texture2D* ResourceManager::Load(const std::string& path) {
    auto it = s_cache.find(path);
    if (it != s_cache.end()) return &it->second;

    Texture2D tex = LoadTexture(path.c_str());
    if (tex.id == 0) return nullptr;

    auto [ins, ok] = s_cache.emplace(path, tex);
    return &ins->second;
}

void ResourceManager::Cleanup() {
    for (auto& [path, tex] : s_cache)
        UnloadTexture(tex);
    s_cache.clear();
}
