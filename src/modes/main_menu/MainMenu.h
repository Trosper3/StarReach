#pragma once
#include "modes/IGameMode.h"
#include "core/FactionEnum.h"
#include "ui/SavePicker.h"
#include "raylib.h"
#include <array>
#include <string>
#include <vector>

namespace ecs { struct ShipDef; }

class MainMenu : public IGameMode {
public:
    void OnEnter() override;
    void Update(float dt) override;
    void Draw()   override;
    void OnExit() override;

private:
    // ── Menu navigation ────────────────────────────────────────────────────────
    enum class MenuScreen { Main, Singleplayer, Multiplayer };
    MenuScreen _screen = MenuScreen::Main;

    // ── Star field ─────────────────────────────────────────────────────────────
    struct Star {
        float x, y, speed, radius;
        unsigned char baseAlpha;
        float twinklePhase, twinkleSpeed;
    };
    static constexpr int StarCount = 300;
    std::array<Star, StarCount> _stars;

    void InitStars();
    void UpdateStars(float dt);
    void DrawStars() const;

    // ── Main screen: ship showcase ─────────────────────────────────────────────
    struct Particle { Vector2 pos, vel; float life, maxLife; };
    enum class ShowcaseState { FadeIn, Still, FadeOut };

    std::vector<const ecs::ShipDef*> _showcaseShips;
    int           _showcaseIdx   = 0;
    float         _rotAngle      = 0.0f;
    float         _shipAlpha     = 0.0f;
    ShowcaseState _showcaseState = ShowcaseState::FadeIn;
    float         _fadeTimer     = 0.0f;
    std::vector<Particle> _particles;
    float _thrustBurstTimer  = 0.0f;
    float _thrustActiveTimer = 0.0f;

    void BuildShowcaseList();
    void UpdateShowcase(float dt, float cx, float cy, float circleR);
    void DrawShowcase(float cx, float cy, float circleR) const;
    void EmitThrusterParticles(float cx, float cy, float engineDist);

    // ── Multiplayer screen ─────────────────────────────────────────────────────
    enum class MpState { Choose, Connecting };
    MpState     _mpState      = MpState::Choose;
    std::string _ipBuffer     = "127.0.0.1";
    bool        _editingIp    = false;
    std::string _mpStatus;
    float       _connectTimer = 0.0f;

    void UpdateMultiplayer(float dt);
    void DrawMultiplayer()  const;

    // ── Singleplayer screen ────────────────────────────────────────────────────
    struct FactionInfo {
        std::string id, displayName, loreText, fighterId;
        Color       color   = {};
        Faction     faction = Faction::Republic;
        Texture2D*  iconTex = nullptr;
    };
    std::vector<FactionInfo> _factions;
    int   _factionIdx  = 0;
    float _loreElapsed = 0.0f;
    int   _loreVisible = 0;
    bool  _loreDone    = false;
    float _loreBlink   = 0.0f;
    bool  _hasSaves    = false;
    std::string _playerName      = "Player";
    std::string _editBuffer;
    bool        _editingName     = false;
    float       _nameSavedTimer   = 0.0f;
    std::string _nameSavedMessage;
    std::string _wrappedLore;

    // New Game galaxy seed (optional) — blank means "random". Mirrors the
    // _editingName/_editBuffer click-to-edit pattern above.
    std::string _galaxySeedText;
    std::string _seedEditBuffer;
    bool        _editingSeed = false;
    static constexpr float CharsPerSec = 38.0f;
    SavePicker  _savePicker;

    void BuildFactionList();
    void OnFactionChanged();
    void UpdateSingleplayer(float dt);
    void DrawSingleplayer() const;

    // ── Fonts ─────────────────────────────────────────────────────────────────
    Font _uiFont   = {};  // Orbitron — title, buttons, faction name
    Font _bodyFont = {};  // Exo 2 — lore text
};
