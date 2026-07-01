#include "AudioManager.h"

void AudioManager::Init() {
    InitAudioDevice();
}

void AudioManager::Shutdown() {
    if (_musicLoaded) {
        StopMusicStream(_currentMusic);
        UnloadMusicStream(_currentMusic);
    }
    for (auto& [path, sound] : _soundCache)
        UnloadSound(sound);
    CloseAudioDevice();
}

void AudioManager::UpdateMusic() {
    if (_musicLoaded) UpdateMusicStream(_currentMusic);
}

void AudioManager::PlayMusic(const std::string& resPath, float volume) {
    if (_musicLoaded) {
        StopMusicStream(_currentMusic);
        UnloadMusicStream(_currentMusic);
    }
    _currentMusic = LoadMusicStream(resPath.c_str());
    _musicLoaded  = true;
    ::SetMusicVolume(_currentMusic, volume);
    PlayMusicStream(_currentMusic);
}

void AudioManager::StopMusic() {
    if (_musicLoaded) StopMusicStream(_currentMusic);
}

void AudioManager::SetMusicVolume(float volume) {
    if (_musicLoaded) ::SetMusicVolume(_currentMusic, volume);
}

void AudioManager::PlaySfx(const std::string& resPath, float volume) {
    if (_soundCache.find(resPath) == _soundCache.end())
        _soundCache[resPath] = LoadSound(resPath.c_str());
    SetSoundVolume(_soundCache[resPath], volume);
    PlaySound(_soundCache[resPath]);
}
