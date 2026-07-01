#pragma once
#include <string>
#include <unordered_map>
#include "raylib.h"

/// Centralized audio control.
/// Music streams on a single persistent player; SFX are cached Sounds.
/// Call UpdateMusic() once per frame to keep the stream alive.
class AudioManager {
public:
    static AudioManager& Get() {
        static AudioManager instance;
        return instance;
    }

    void Init();
    void Shutdown();
    void UpdateMusic();

    void PlayMusic(const std::string& resPath, float volume = 1.f);
    void StopMusic();
    void SetMusicVolume(float volume);

    void PlaySfx(const std::string& resPath, float volume = 1.f);

private:
    AudioManager() = default;
    Music _currentMusic  = {};
    bool  _musicLoaded   = false;
    std::unordered_map<std::string, Sound> _soundCache;
};
