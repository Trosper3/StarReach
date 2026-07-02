#include "MainMenu.h"
#include "raylib.h"
#include "net/NetworkManager.h"
#include "net/NetCommon.h"
#include "core/GameManager.h"
#include "core/EntityBlueprints.h"
#include "core/ShipRegistry.h"
#include "core/ShipDef.h"
#include "core/FleetManager.h"
#include "core/InventoryManager.h"
#include "core/SaveManager.h"
#include "engine/SpriteCache.h"
#include "engine/ResourceManager.h"
#include "data/registry/FactionRegistry.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

// ── Helpers ────────────────────────────────────────────────────────────────────

static std::vector<std::string> SplitLines(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == '\n') { out.push_back(cur); cur.clear(); }
        else            cur += c;
    }
    out.push_back(cur);
    return out;
}

static bool IsHovered(Rectangle r) { return CheckCollisionPointRec(GetMousePosition(), r); }
static bool IsClicked(Rectangle r) { return IsHovered(r) && IsMouseButtonReleased(MOUSE_BUTTON_LEFT); }

static void DrawTechCorners(Rectangle r, float len, float thick, Color col) {
    DrawLineEx({ r.x,                 r.y + len            }, { r.x,              r.y             }, thick, col);
    DrawLineEx({ r.x,                 r.y                  }, { r.x + len,        r.y             }, thick, col);
    DrawLineEx({ r.x + r.width - len, r.y                  }, { r.x + r.width,    r.y             }, thick, col);
    DrawLineEx({ r.x + r.width,       r.y                  }, { r.x + r.width,    r.y + len       }, thick, col);
    DrawLineEx({ r.x,                 r.y + r.height - len }, { r.x,              r.y + r.height  }, thick, col);
    DrawLineEx({ r.x,                 r.y + r.height       }, { r.x + len,        r.y + r.height  }, thick, col);
    DrawLineEx({ r.x + r.width - len, r.y + r.height       }, { r.x + r.width,    r.y + r.height  }, thick, col);
    DrawLineEx({ r.x + r.width,       r.y + r.height - len }, { r.x + r.width,    r.y + r.height  }, thick, col);
}

static constexpr float UISpacing = 1.0f;

static void DrawButton(const Font& fnt, Rectangle r, const char* label, float fs) {
    bool hov = IsHovered(r);
    DrawRectangleRec(r, hov ? Color{ 32, 46, 62, 230 } : Color{ 16, 22, 32, 210 });
    DrawRectangleLinesEx(r, 1.0f, Color{ 52, 68, 88, 150 });
    DrawTechCorners(r, 10.0f, 1.5f, Color{ 120, 150, 180, 255 });
    Color lc = hov ? Color{ 220, 232, 248, 255 } : Color{ 165, 192, 220, 255 };
    Vector2 ts = MeasureTextEx(fnt, label, fs, UISpacing);
    DrawTextEx(fnt, label, { r.x + (r.width - ts.x) * 0.5f, r.y + (r.height - ts.y) * 0.5f },
               fs, UISpacing, lc);
}

static void DrawButtonDisabled(const Font& fnt, Rectangle r, const char* label, float fs) {
    DrawRectangleRec(r, Color{ 12, 15, 20, 155 });
    DrawRectangleLinesEx(r, 1.0f, Color{ 36, 44, 54, 120 });
    DrawTechCorners(r, 10.0f, 1.5f, Color{ 58, 72, 88, 150 });
    Vector2 ts = MeasureTextEx(fnt, label, fs, UISpacing);
    DrawTextEx(fnt, label, { r.x + (r.width - ts.x) * 0.5f, r.y + (r.height - ts.y) * 0.5f },
               fs, UISpacing, Color{ 75, 90, 108, 165 });
}

static void UITextCenter(const Font& f, const char* t, float cx, float y, float fs, Color c) {
    float tw = MeasureTextEx(f, t, fs, UISpacing).x;
    DrawTextEx(f, t, { cx - tw * 0.5f, y }, fs, UISpacing, c);
}

// ── Layout / timing constants ──────────────────────────────────────────────────

static constexpr float TitleFontSize = 64.0f;
static constexpr float TitleY        = 40.0f;
static constexpr float CircleGapTop  = 36.0f;
static constexpr float CircleGapBot  = 36.0f;
static constexpr float BtnW          = 260.0f;
static constexpr float BtnH          = 52.0f;
static constexpr float BtnGap        = 14.0f;
static constexpr float BtnFontSize   = 16.0f;
static constexpr float kRotSpeed     = 360.0f / 11.0f;
static constexpr float kFadeDur      = 0.6f;
static constexpr float kHoldDur      = 3.0f;

// ── Faction color palette ──────────────────────────────────────────────────────

static Color FactionColor(const std::string& paletteId) {
    static const struct { const char* id; Color col; } kColors[] = {
        { "faction_republic",   {  80, 140, 220, 255 } },
        { "faction_zenith",     { 220, 195,  55, 255 } },
        { "faction_korrath",    { 180,  50,  50, 255 } },
        { "faction_merchant",   {  55, 185, 185, 255 } },
        { "faction_eden",       {  75, 200, 100, 255 } },
        { "faction_reaperians", { 160,  55, 200, 255 } },
        { "faction_forge",      { 210, 110,  40, 255 } },
        { "faction_conclave",   { 160, 160, 195, 255 } },
        { "faction_void",       {  75,  35, 130, 255 } },
    };
    for (const auto& k : kColors)
        if (paletteId == k.id) return k.col;
    return { 180, 200, 230, 255 };
}

// ── Star field ─────────────────────────────────────────────────────────────────

void MainMenu::InitStars() {
    int sw = GetScreenWidth(), sh = GetScreenHeight();
    for (int i = 0; i < StarCount; ++i) {
        Star& s = _stars[i];
        s.x           = (float)GetRandomValue(0, sw);
        s.y           = (float)GetRandomValue(0, sh);
        s.twinklePhase = (float)GetRandomValue(0, 628) / 100.0f;
        s.twinkleSpeed = (float)GetRandomValue(50, 200) / 100.0f;
        if (i < 180) {
            s.speed     = (float)GetRandomValue(8, 18);
            s.radius    = (float)GetRandomValue(60, 110) / 100.0f;
            s.baseAlpha = (unsigned char)GetRandomValue(60, 120);
        } else if (i < 260) {
            s.speed     = (float)GetRandomValue(20, 38);
            s.radius    = (float)GetRandomValue(120, 180) / 100.0f;
            s.baseAlpha = (unsigned char)GetRandomValue(130, 190);
        } else {
            s.speed     = (float)GetRandomValue(42, 70);
            s.radius    = (float)GetRandomValue(180, 260) / 100.0f;
            s.baseAlpha = (unsigned char)GetRandomValue(190, 245);
        }
    }
}

void MainMenu::UpdateStars(float dt) {
    int sw = GetScreenWidth(), sh = GetScreenHeight();
    for (Star& s : _stars) {
        s.x -= s.speed * dt;
        s.twinklePhase += s.twinkleSpeed * dt;
        if (s.x < -2.0f) {
            s.x = (float)sw + 1.0f;
            s.y = (float)GetRandomValue(0, sh);
        }
    }
}

void MainMenu::DrawStars() const {
    for (const Star& s : _stars) {
        float flicker = sinf(s.twinklePhase) * 28.0f;
        int   alpha   = std::clamp((int)s.baseAlpha + (int)flicker, 0, 255);
        DrawCircleV({ s.x, s.y }, s.radius, { 200, 210, 255, (unsigned char)alpha });
        if (s.radius > 1.8f && alpha > 180)
            DrawCircleV({ s.x, s.y }, s.radius * 3.0f,
                Color{ 180, 190, 255, (unsigned char)(alpha / 5) });
    }
}

// ── Main screen: ship showcase ─────────────────────────────────────────────────

void MainMenu::BuildShowcaseList() {
    _showcaseShips.clear();
    for (const auto& ship : ecs::ShipRegistry::AllShips())
        if (!ship.designArray.empty() || !ship.assetPath.empty())
            _showcaseShips.push_back(&ship);
}

void MainMenu::EmitThrusterParticles(float cx, float cy, float engineDist) {
    float rotRad = _rotAngle * DEG2RAD;
    float edx = -sinf(rotRad);
    float edy =  cosf(rotRad);
    float ex = cx + edx * engineDist;
    float ey = cy + edy * engineDist;
    for (int i = 0; i < 3; ++i) {
        float spread = (float)GetRandomValue(-28, 28) * DEG2RAD;
        float c = cosf(spread), s = sinf(spread);
        float speed = (float)GetRandomValue(70, 150);
        Particle p;
        p.pos     = { ex + (float)GetRandomValue(-2, 2), ey + (float)GetRandomValue(-2, 2) };
        p.vel     = { (edx * c - edy * s) * speed, (edx * s + edy * c) * speed };
        p.maxLife = (float)GetRandomValue(20, 65) / 100.0f;
        p.life    = p.maxLife;
        _particles.push_back(p);
    }
}

void MainMenu::UpdateShowcase(float dt, float cx, float cy, float circleR) {
    for (auto& p : _particles) {
        p.life -= dt; p.pos.x += p.vel.x * dt; p.pos.y += p.vel.y * dt;
    }
    _particles.erase(
        std::remove_if(_particles.begin(), _particles.end(),
                       [](const Particle& p) { return p.life <= 0.0f; }),
        _particles.end());

    if (_showcaseShips.empty()) return;
    float engineDist = circleR * 0.32f;

    switch (_showcaseState) {
    case ShowcaseState::FadeIn:
        _fadeTimer += dt;
        _shipAlpha = std::min(_fadeTimer / kFadeDur, 1.0f) * 255.0f;
        if (_fadeTimer >= kFadeDur) {
            _shipAlpha = 255.0f; _showcaseState = ShowcaseState::Still; _fadeTimer = 0.0f;
        }
        break;
    case ShowcaseState::Still:
        _fadeTimer += dt;
        if (_fadeTimer >= kHoldDur) {
            _showcaseState = ShowcaseState::FadeOut; _fadeTimer = 0.0f;
        }
        break;
    case ShowcaseState::FadeOut:
        _fadeTimer += dt;
        _shipAlpha = (1.0f - std::min(_fadeTimer / kFadeDur, 1.0f)) * 255.0f;
        if (_fadeTimer >= kFadeDur) {
            _shipAlpha = 0.0f;
            _showcaseIdx   = (_showcaseIdx + 1) % (int)_showcaseShips.size();
            _showcaseState = ShowcaseState::FadeIn; _fadeTimer = 0.0f;
        }
        break;
    }
}

void MainMenu::DrawShowcase(float cx, float cy, float circleR) const {
    DrawCircleV({ cx, cy }, circleR, Color{ 8, 12, 22, 235 });

    for (const auto& p : _particles) {
        float t = p.life / p.maxLife;
        auto  a = (unsigned char)(t * 210.0f);
        float r = 1.5f + t * 3.5f;
        DrawCircleV(p.pos, r,        { 255, (unsigned char)(80 + (int)(t * 160)), 20, a });
        DrawCircleV(p.pos, r * 2.2f, { 255, 100, 20, (unsigned char)(a / 4) });
    }

    if (_showcaseShips.empty()) return;
    const auto* def = _showcaseShips[_showcaseIdx];

    Texture2D* tex = nullptr;
    if (!def->designArray.empty())
        tex = SpriteCache::BakeForId(def->id, FactionColor(def->paletteId), WHITE, def->designArray);
    else if (!def->assetPath.empty())
        tex = ResourceManager::Load(def->assetPath);

    auto  alpha   = (unsigned char)std::clamp(_shipAlpha, 0.0f, 255.0f);
    Color ringCol = FactionColor(def->paletteId);

    float glow = 0.5f + 0.5f * sinf((float)GetTime() * 1.4f);
    DrawCircleV({ cx, cy }, circleR * 0.65f,
        { ringCol.r, ringCol.g, ringCol.b, (unsigned char)(glow * 12.0f * (float)alpha / 255.0f) });

    if (tex && tex->id != 0) {
        float maxDim = (float)std::max(tex->width, tex->height);
        float scale  = (circleR * 1.5f) / maxDim;
        float tw = tex->width * scale, th = tex->height * scale;
        DrawTexturePro(*tex,
            { 0.0f, 0.0f, (float)tex->width, (float)tex->height },
            { cx, cy, tw, th }, { tw * 0.5f, th * 0.5f },
            0.0f, Color{ 255, 255, 255, alpha });
    }

    // ── Border ring ───────────────────────────────────────────────────────────
    float bInner = circleR + 3.0f;
    float bOuter = circleR + 13.0f;
    unsigned char bA = (unsigned char)((int)alpha * 210 / 255);

    // Thick grey base ring
    DrawRing({ cx, cy }, bInner, bOuter, 0.0f, 360.0f, 72,
        Color{ 48, 52, 60, bA });

    // Faction-colored arc accents at diagonal positions
    unsigned char fA = (unsigned char)((int)alpha * 235 / 255);
    Color accent = { ringCol.r, ringCol.g, ringCol.b, fA };
    for (float ctr : { -135.0f, -45.0f, 45.0f, 135.0f })
        DrawRing({ cx, cy }, bInner, bOuter, ctr - 22.0f, ctr + 22.0f, 10, accent);

    // Subtle inner-edge faction line
    DrawCircleLines((int)cx, (int)cy, bInner - 1.0f,
        { ringCol.r, ringCol.g, ringCol.b, (unsigned char)((int)alpha * 70 / 255) });
}

// ── Singleplayer screen ────────────────────────────────────────────────────────

void MainMenu::BuildFactionList() {
    static const struct {
        const char* label; const char* paletteId; Faction faction; Color color; const char* iconFile;
    } kDefs[] = {
        { "Aetherian Republic",  "faction_republic",   Faction::Republic, {  80, 140, 220, 255 }, "assets/icons/faction_republic.png"   },
        { "Zenith Technocracy",  "faction_zenith",     Faction::Zenith,   { 220, 195,  55, 255 }, "assets/icons/faction_zenith.png"     },
        { "Kore Industries",     "faction_korrath",    Faction::Korrath,  { 180,  50,  50, 255 }, "assets/icons/faction_korrath.png"    },
        { "Meridian Star Corps", "faction_merchant",   Faction::Merchant, {  55, 185, 185, 255 }, "assets/icons/faction_merchant.png"   },
        { "Edenian Pact",        "faction_eden",       Faction::Eden,     {  75, 200, 100, 255 }, "assets/icons/faction_eden.png"       },
        { "The Reapers",         "faction_reaperians", Faction::Reavers,  { 160,  55, 200, 255 }, "assets/icons/faction_reaperians.png" },
        { "The Forgotten",       "faction_forge",      Faction::Forge,    { 210, 110,  40, 255 }, "assets/icons/faction_forge.png"      },
        { "Automa Concord",      "faction_conclave",   Faction::Conclave, { 160, 160, 195, 255 }, "assets/icons/faction_conclave.png"   },
        { "The Voidwalkers",     "faction_void",       Faction::Void,     {  75,  35, 130, 255 }, "assets/icons/faction_void.png"       },
    };

    const auto& ships = ecs::ShipRegistry::AllShips();
    _factions.clear();

    for (const auto& d : kDefs) {
        FactionInfo fi;
        fi.id          = d.paletteId;
        fi.displayName = d.label;
        fi.faction     = d.faction;
        fi.color       = d.color;
        fi.iconTex     = ResourceManager::Load(d.iconFile);
        if (const auto* fd = FactionRegistry::ById(d.paletteId))
            fi.loreText = fd->loreText;
        for (const auto& ship : ships) {
            if (ship.shipType == ShipType::Fighter && ship.paletteId == d.paletteId) {
                fi.fighterId = ship.id;
                break;
            }
        }
        _factions.push_back(std::move(fi));
    }
}

// Wraps text so no line exceeds maxWidth pixels, preserving explicit \n and leading whitespace.
static std::string WordWrap(const std::string& text, const Font& font, float fontSize, float maxWidth) {
    auto rawLines = SplitLines(text);
    std::string result;
    for (size_t li = 0; li < rawLines.size(); ++li) {
        if (li > 0) result += '\n';
        const std::string& raw = rawLines[li];
        if (raw.empty() ||
            MeasureTextEx(font, raw.c_str(), fontSize, UISpacing).x <= maxWidth) {
            result += raw;
            continue;
        }
        // Preserve leading whitespace (e.g. "  > " bullet lines)
        size_t wsEnd  = raw.find_first_not_of(' ');
        std::string indent = (wsEnd != std::string::npos) ? raw.substr(0, wsEnd) : "";
        std::string rest   = (wsEnd != std::string::npos) ? raw.substr(wsEnd)    : raw;

        std::string curLine = indent;
        bool first = true;
        size_t pos = 0;
        while (pos <= rest.size()) {
            size_t wordEnd = rest.find(' ', pos);
            if (wordEnd == std::string::npos) wordEnd = rest.size();
            std::string word = rest.substr(pos, wordEnd - pos);
            std::string test = first ? (indent + word) : (curLine + ' ' + word);
            if (!first && MeasureTextEx(font, test.c_str(), fontSize, UISpacing).x > maxWidth) {
                result += curLine + '\n';
                curLine = indent + word;
            } else {
                curLine = test;
                first   = false;
            }
            pos = wordEnd + 1;
        }
        result += curLine;
    }
    return result;
}

void MainMenu::OnFactionChanged() {
    _loreElapsed = 0.0f;
    _loreVisible = 0;
    _loreDone    = false;
    _loreBlink   = 0.0f;
    if (!_factions.empty())
        _wrappedLore = WordWrap(_factions[_factionIdx].loreText, _bodyFont, 17.0f, 680.0f);
}

void MainMenu::UpdateSingleplayer(float dt) {
    if (_savePicker.IsOpen()) {
        auto result = _savePicker.Update();
        if (result == SavePicker::Result::Selected) {
            SaveManager::Get().SetPendingLoad(_savePicker.SelectedFile());
            GameManager::Get().TransitionTo(GameMode::SpaceFlight);
        }
        return;
    }

    // Typewriter
    _loreBlink += dt;
    if (!_loreDone && !_wrappedLore.empty()) {
        _loreElapsed += dt;
        _loreVisible = std::min((int)(_loreElapsed * CharsPerSec), (int)_wrappedLore.size());
        if (_loreVisible >= (int)_wrappedLore.size()) _loreDone = true;
    }
    if (!_loreDone && IsKeyPressed(KEY_SPACE)) {
        _loreVisible = (int)_wrappedLore.size();
        _loreDone    = true;
    }

    // Back
    if (IsKeyPressed(KEY_ESCAPE)) { _screen = MenuScreen::Main; return; }
    Rectangle backR = { 16.0f, 16.0f, 110.0f, 40.0f };
    if (IsClicked(backR))         { _screen = MenuScreen::Main; return; }

    int   sw = GetScreenWidth(), sh = GetScreenHeight();
    float cx = sw * 0.5f;

    // Arrow buttons
    static constexpr float circleR  = 130.0f;
    float circleCY = (float)sh - 90.0f - 66.0f - circleR;
    float arrowW   = 44.0f, arrowH = 44.0f;
    float arrowGap = circleR + 55.0f;
    Rectangle leftR  = { cx - arrowGap - arrowW, circleCY - arrowH * 0.5f, arrowW, arrowH };
    Rectangle rightR = { cx + arrowGap,           circleCY - arrowH * 0.5f, arrowW, arrowH };

    if (IsClicked(leftR) && !_factions.empty()) {
        _factionIdx = (_factionIdx - 1 + (int)_factions.size()) % (int)_factions.size();
        OnFactionChanged();
    }
    if (IsClicked(rightR) && !_factions.empty()) {
        _factionIdx = (_factionIdx + 1) % (int)_factions.size();
        OnFactionChanged();
    }

    // Galaxy seed field (optional — blank means a random galaxy each New Game)
    Rectangle seedBoxR = { cx - 156.0f, circleCY + circleR + 14.0f, 240.0f, 32.0f };
    Rectangle seedBtnR = { seedBoxR.x + seedBoxR.width + 8.0f, seedBoxR.y, 64.0f, 32.0f };

    if (_editingSeed) {
        if (IsKeyPressed(KEY_ESCAPE)) {
            _editingSeed = false;
        } else {
            int ch;
            while ((ch = GetCharPressed()) != 0)
                if (ch >= 32 && ch < 127 && (int)_seedEditBuffer.size() < 24)
                    _seedEditBuffer += (char)ch;
            if (IsKeyPressed(KEY_BACKSPACE) && !_seedEditBuffer.empty())
                _seedEditBuffer.pop_back();
            if (IsClicked(seedBtnR) || IsKeyPressed(KEY_ENTER)) {
                _galaxySeedText = _seedEditBuffer;
                _editingSeed    = false;
            } else if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT) &&
                       !IsHovered(seedBoxR) && !IsHovered(seedBtnR)) {
                _editingSeed = false;
            }
        }
    } else if (IsClicked(seedBoxR)) {
        _seedEditBuffer = _galaxySeedText;
        _editingSeed    = true;
    }

    // Bottom buttons
    float btnW = 210.0f, btnH = 50.0f, btnGap = 20.0f;
    float btnY = (float)sh - 90.0f;
    Rectangle newGameR  = { cx - btnW - btnGap * 0.5f, btnY, btnW, btnH };
    Rectangle continueR = { cx + btnGap * 0.5f,        btnY, btnW, btnH };

    if (IsClicked(newGameR) && !_factions.empty()) {
        const auto& fi  = _factions[_factionIdx];
        auto& fleetShip = FleetManager::Get().PlayerShip;
        fleetShip.ShipTypeId    = fi.fighterId.empty() ? "ar3_saber" : fi.fighterId;
        fleetShip.PlayerFaction = fi.faction;
        fleetShip.MaxHull       = 100.0f;
        fleetShip.HullIntegrity = 100.0f;
        fleetShip.ComponentSlots = { "", "", "", "" };
        fleetShip.GalaxySeedInput = _galaxySeedText;
        InventoryManager::Get().Credits = 0;
        InventoryManager::Get().Reset();
        GameManager::Get().TransitionTo(GameMode::SpaceFlight);
        return;
    }

    if (IsClicked(continueR) && _hasSaves)
        _savePicker.Open();
}

void MainMenu::DrawSingleplayer() const {
    int   sw = GetScreenWidth(), sh = GetScreenHeight();
    float cx = sw * 0.5f;

    // Back button
    {
        Rectangle backR = { 16.0f, 16.0f, 110.0f, 40.0f };
        DrawButton(_uiFont, backR, "< BACK", 13.0f);
    }

    if (_factions.empty()) return;
    const FactionInfo& fi = _factions[_factionIdx];

    // Faction name
    static constexpr float nameFontSize = 42.0f;
    static constexpr float nameY        = 44.0f;
    UITextCenter(_uiFont, fi.displayName.c_str(), cx, nameY, nameFontSize,
                 Color{ fi.color.r, fi.color.g, fi.color.b, 255 });
    DrawRectangle((int)(cx - 340.0f), (int)(nameY + nameFontSize + 12.0f), 680, 1,
                  Color{ 55, 72, 95, 140 });

    static constexpr float loreFontSize = 17.0f;
    static constexpr float loreLineH    = 24.0f;
    static constexpr float loreY        = nameY + nameFontSize + 22.0f;
    static constexpr float circleR      = 130.0f;

    float circleCY = (float)sh - 90.0f - 66.0f - circleR;
    float loreMaxY = circleCY - circleR - 60.0f;

    // ── Left column: lore text ─────────────────────────────────────────────────
    {
        static constexpr float loreTextW = 380.0f;
        float loreX = cx - 340.0f;

        std::string vis = _wrappedLore.substr(0, _loreVisible);
        if (!_loreDone && (int)(_loreBlink * 2) % 2 == 0) vis += '_';
        float ty = loreY;
        for (const auto& line : SplitLines(vis)) {
            if (ty + loreLineH > loreMaxY) break;
            DrawTextEx(_bodyFont, line.c_str(), { loreX, ty }, loreFontSize, UISpacing,
                       Color{ 155, 182, 210, 255 });
            ty += loreLineH;
        }
        if (!_loreDone) {
            DrawTextEx(_bodyFont, "[ SPACE to skip ]",
                       { loreX, loreMaxY - 18.0f }, 13.0f, UISpacing,
                       Color{ 78, 100, 124, 255 });
        }
    }

    // ── Center column: faction icon circle, arrows, player info, buttons ──────
    {
        // Player name and rank above the icon circle
        float infoY = circleCY - circleR - 52.0f;
        UITextCenter(_bodyFont, "PLAYER",  cx - 90.0f, infoY,         11.0f, Color{ 100, 130, 165, 255 });
        UITextCenter(_uiFont,   _playerName.c_str(), cx - 90.0f, infoY + 15.0f, 17.0f, Color{ 200, 220, 248, 255 });
        const FactionDef* fd = FactionRegistry::ById(fi.id);
        const char* startRank = (fd && !fd->ranks.empty()) ? fd->ranks[0].c_str() : "—";
        UITextCenter(_bodyFont, "RANK",     cx + 90.0f, infoY,         11.0f, Color{ 100, 130, 165, 255 });
        UITextCenter(_uiFont,   startRank,  cx + 90.0f, infoY + 15.0f, 17.0f, Color{ 200, 220, 248, 255 });

        // Faction icon circle
        Color rc = fi.color;
        DrawCircleV({ cx, circleCY }, circleR, Color{ 8, 12, 22, 235 });
        DrawCircleLines((int)cx, (int)circleCY, circleR + 1.5f, { rc.r, rc.g, rc.b,  38 });
        DrawCircleLines((int)cx, (int)circleCY, circleR,         { rc.r, rc.g, rc.b, 178 });
        DrawCircleLines((int)cx, (int)circleCY, circleR - 2.0f,  { rc.r, rc.g, rc.b,  68 });

        if (fi.iconTex && fi.iconTex->id != 0) {
            float pad = circleR * 0.10f;
            float avD = (circleR - pad) * 2.0f;
            float sc  = std::min(avD / fi.iconTex->width, avD / fi.iconTex->height);
            float tw  = fi.iconTex->width  * sc;
            float th  = fi.iconTex->height * sc;
            DrawTexturePro(*fi.iconTex,
                { 0.0f, 0.0f, (float)fi.iconTex->width, (float)fi.iconTex->height },
                { cx - tw * 0.5f, circleCY - th * 0.5f, tw, th },
                { 0.0f, 0.0f }, 0.0f, WHITE);
        }

        // Arrow buttons
        float arrowW   = 44.0f, arrowH = 44.0f;
        float arrowGap = circleR + 55.0f;
        Rectangle leftR  = { cx - arrowGap - arrowW, circleCY - arrowH * 0.5f, arrowW, arrowH };
        Rectangle rightR = { cx + arrowGap,           circleCY - arrowH * 0.5f, arrowW, arrowH };

        auto DrawArrow = [&](Rectangle r, const char* sym) {
            bool hov = IsHovered(r);
            DrawRectangleRec(r, hov ? Color{ 32, 46, 62, 230 } : Color{ 16, 22, 32, 210 });
            DrawRectangleLinesEx(r, 1.0f, Color{ 52, 68, 88, 150 });
            DrawTechCorners(r, 6.0f, 1.2f, Color{ 120, 150, 180, 255 });
            Vector2 ts = MeasureTextEx(_uiFont, sym, 22.0f, UISpacing);
            DrawTextEx(_uiFont, sym,
                { r.x + (r.width - ts.x) * 0.5f, r.y + (r.height - ts.y) * 0.5f },
                22.0f, UISpacing, hov ? Color{ 220, 232, 248, 255 } : Color{ 165, 192, 220, 255 });
        };
        DrawArrow(leftR, "<");
        DrawArrow(rightR, ">");

        // Galaxy seed field (optional)
        {
            Rectangle seedBoxR = { cx - 156.0f, circleCY + circleR + 14.0f, 240.0f, 32.0f };
            Rectangle seedBtnR = { seedBoxR.x + seedBoxR.width + 8.0f, seedBoxR.y, 64.0f, 32.0f };

            bool editing = _editingSeed;
            DrawRectangleRec(seedBoxR, editing ? Color{ 24, 36, 52, 230 } : Color{ 16, 22, 32, 210 });
            DrawRectangleLinesEx(seedBoxR, 1.0f,
                editing ? Color{ 80, 120, 180, 255 } : Color{ 52, 68, 88, 150 });
            if (editing) DrawTechCorners(seedBoxR, 6.0f, 1.2f, Color{ 80, 140, 220, 255 });

            std::string shown = editing ? _seedEditBuffer : _galaxySeedText;
            bool isPlaceholder = shown.empty() && !editing;
            if (isPlaceholder) shown = "GALAXY SEED (optional)";
            if (editing && (int)(GetTime() * 2) % 2 == 0) shown += '_';
            DrawTextEx(_bodyFont, shown.c_str(),
                { seedBoxR.x + 8.0f, seedBoxR.y + 8.0f }, 13.0f, UISpacing,
                isPlaceholder ? Color{ 90, 110, 135, 200 } : Color{ 200, 220, 248, 255 });

            DrawButton(_uiFont, seedBtnR, "SET", 11.0f);
        }

        // Start New / Continue buttons
        float btnW = 210.0f, btnH = 50.0f, btnGap = 20.0f;
        float btnY = (float)sh - 90.0f;
        Rectangle newGameR  = { cx - btnW - btnGap * 0.5f, btnY, btnW, btnH };
        Rectangle continueR = { cx + btnGap * 0.5f,        btnY, btnW, btnH };
        DrawButton(_uiFont, newGameR, "START NEW", 18.0f);
        if (_hasSaves)
            DrawButton(_uiFont, continueR, "CONTINUE", 18.0f);
        else
            DrawButtonDisabled(_uiFont, continueR, "CONTINUE", 18.0f);
    }

    // ── Right column: ship preview + stats ─────────────────────────────────────
    {
        const ecs::ShipDef* shipDef = ecs::ShipRegistry::ShipById(
            fi.fighterId.empty() ? "ar3_saber" : fi.fighterId);

        static constexpr float rpCircleR = 90.0f;
        float rpx  = cx + 370.0f;
        // Shift up so the ship name label aligns with the PLAYER/RANK row
        float rpCY = circleCY - (circleR + 52.0f) + (rpCircleR + 38.0f);

        // Circle frame
        Color rc = fi.color;
        DrawCircleV({ rpx, rpCY }, rpCircleR, Color{ 8, 12, 22, 235 });
        DrawCircleLines((int)rpx, (int)rpCY, rpCircleR + 1.5f, { rc.r, rc.g, rc.b,  38 });
        DrawCircleLines((int)rpx, (int)rpCY, rpCircleR,         { rc.r, rc.g, rc.b, 178 });
        DrawCircleLines((int)rpx, (int)rpCY, rpCircleR - 2.0f,  { rc.r, rc.g, rc.b,  68 });

        // Rotating ship sprite
        if (shipDef) {
            Texture2D* tex = nullptr;
            if (!shipDef->designArray.empty())
                tex = SpriteCache::BakeForId(shipDef->id, fi.color, WHITE, shipDef->designArray);
            else if (!shipDef->assetPath.empty())
                tex = ResourceManager::Load(shipDef->assetPath);

            if (tex && tex->id != 0) {
                float angle  = fmodf((float)GetTime() * kRotSpeed, 360.0f);
                float maxDim = (float)std::max(tex->width, tex->height);
                float scale  = (rpCircleR * 1.5f) / maxDim;
                float tw = tex->width * scale, th = tex->height * scale;
                DrawTexturePro(*tex,
                    { 0.0f, 0.0f, (float)tex->width, (float)tex->height },
                    { rpx, rpCY, tw, th }, { tw * 0.5f, th * 0.5f },
                    angle, WHITE);
            }
        }

        // Ship name label — inline with the center column's PLAYER/RANK row
        {
            float labelY = rpCY - rpCircleR - 38.0f;
            UITextCenter(_bodyFont, "STARTING SHIP", rpx, labelY,        11.0f, Color{ 100, 130, 165, 255 });
            UITextCenter(_uiFont,   shipDef ? shipDef->displayName.c_str() : "—",
                         rpx, labelY + 15.0f, 17.0f, Color{ 200, 220, 248, 255 });
        }

        // Stats below the circle
        if (shipDef) {
            float sy   = rpCY + rpCircleR + 18.0f;
            float col1 = rpx - 90.0f;

            // Slots Title
            char hullBuf[16];
            snprintf(hullBuf, sizeof(hullBuf), "%.0f", shipDef->baseStats.hull);
            DrawTextEx(_uiFont,   "Module Slots", {col1, sy + 13.0f}, 15.0f, UISpacing, Color{200, 220, 248, 255});
            sy += 36.0f;

            // One row per slot type
            Color slotCol = { fi.color.r, fi.color.g, fi.color.b, 255 };
            struct { const char* label; int count; } slots[] = {
                { "WEAPON",    shipDef->weaponSlots },
                { "ARMOR",     shipDef->armorSlots  },
                { "SHIELD",    shipDef->shieldSlots },
                { "ENGINE",    shipDef->engineSlots },
                { "AUXILIARY", shipDef->auxSlots    },
            };
            for (const auto& s : slots) {
                char buf[8];
                snprintf(buf, sizeof(buf), "%d", s.count);
                DrawTextEx(_bodyFont, s.label, { col1, sy },          16.0f, UISpacing, Color{ 100, 130, 165, 255 });
                DrawTextEx(_uiFont,   buf,     { col1 + 80.0f, sy },  18.0f, UISpacing, Color{ 100, 130, 165, 255 });
                sy += 18.0f;
            }
        }
    }

    _savePicker.Draw();
}

// ── Multiplayer screen ─────────────────────────────────────────────────────────

static constexpr float kConnectTimeout = 8.0f;

void MainMenu::UpdateMultiplayer(float dt) {
    // While waiting for a connection: poll ENet and watch for success or timeout.
    if (_mpState == MpState::Connecting) {
        net::Game().Poll(dt);
        _connectTimer += dt;

        if (net::Game().IsConnected()) {
            GameManager::Get().TransitionTo(GameMode::SpaceFlight);
            return;
        }
        if (_connectTimer >= kConnectTimeout) {
            net::Game().Shutdown();
            _mpState  = MpState::Choose;
            _mpStatus = "Connection failed.";
        }

        // Allow cancelling while connecting.
        if (IsKeyPressed(KEY_ESCAPE)) {
            net::Game().Shutdown();
            _mpState  = MpState::Choose;
            _mpStatus = "";
        }
        return;
    }

    // ── Choose state ───────────────────────────────────────────────────────────
    if (IsKeyPressed(KEY_ESCAPE)) { _screen = MenuScreen::Main; return; }

    Rectangle backR = { 16.0f, 16.0f, 110.0f, 40.0f };
    if (IsClicked(backR)) { _screen = MenuScreen::Main; return; }

    int   sw = GetScreenWidth(), sh = GetScreenHeight();
    float cx = sw * 0.5f;

    // ── HOST button ───────────────────────────────────────────────────────────
    Rectangle hostR = { cx - 130.0f, (float)sh * 0.35f, 260.0f, 56.0f };
    if (IsClicked(hostR)) {
        if (net::Game().StartHost(net::kDefaultPort)) {
            // Initialise player state the same way singleplayer "START NEW" does,
            // so SpaceFlight doesn't read stale hull/faction from a previous run.
            auto& fleetShip = FleetManager::Get().PlayerShip;
            fleetShip.ShipTypeId     = _factions.empty() ? "ar3_saber"
                                       : (_factions[0].fighterId.empty() ? "ar3_saber"
                                                                          : _factions[0].fighterId);
            fleetShip.PlayerFaction  = _factions.empty() ? Faction::Republic
                                                          : _factions[0].faction;
            fleetShip.MaxHull        = 100.0f;
            fleetShip.HullIntegrity  = 100.0f;
            fleetShip.ComponentSlots = { "", "", "", "" };
            InventoryManager::Get().Credits = 0;
            InventoryManager::Get().Reset();
            GameManager::Get().TransitionTo(GameMode::SpaceFlight);
        } else {
            _mpStatus = "Failed to start host.";
        }
        return;
    }

    // ── IP text box ───────────────────────────────────────────────────────────
    Rectangle ipBoxR  = { cx - 130.0f, (float)sh * 0.52f, 196.0f, 38.0f };
    Rectangle joinBR  = { cx +  72.0f, (float)sh * 0.52f,  58.0f, 38.0f };

    if (_editingIp) {
        int ch;
        while ((ch = GetCharPressed()) != 0)
            if ((ch >= '0' && ch <= '9') || ch == '.')
                if ((int)_ipBuffer.size() < 15) _ipBuffer += (char)ch;
        if (IsKeyPressed(KEY_BACKSPACE) && !_ipBuffer.empty())
            _ipBuffer.pop_back();
        if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_ENTER))
            _editingIp = false;
        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT) &&
            !IsHovered(ipBoxR) && !IsHovered(joinBR))
            _editingIp = false;
    } else {
        if (IsClicked(ipBoxR)) {
            _editingIp = true;
        }
    }

    // ── JOIN button ───────────────────────────────────────────────────────────
    if (IsClicked(joinBR)) {
        _editingIp = false;
        if (net::Game().StartClient(_ipBuffer, net::kDefaultPort)) {
            _mpState      = MpState::Connecting;
            _connectTimer = 0.0f;
            _mpStatus     = "";
        } else {
            _mpStatus = "Could not open socket.";
        }
    }
}

void MainMenu::DrawMultiplayer() const {
    int   sw = GetScreenWidth(), sh = GetScreenHeight();
    float cx = sw * 0.5f;

    // Back button
    DrawButton(_uiFont, { 16.0f, 16.0f, 110.0f, 40.0f }, "< BACK", 13.0f);

    // Title
    UITextCenter(_uiFont, "MULTIPLAYER", cx, 44.0f, 42.0f, Color{ 165, 192, 220, 255 });
    DrawRectangle((int)(cx - 200.0f), (int)(44.0f + 48.0f), 400, 1, Color{ 55, 72, 95, 140 });

    if (_mpState == MpState::Connecting) {
        UITextCenter(_uiFont, "Connecting...", cx, (float)sh * 0.45f, 24.0f, Color{ 165, 192, 220, 255 });
        UITextCenter(_bodyFont, "Press ESC to cancel", cx, (float)sh * 0.52f, 15.0f, Color{ 100, 130, 165, 255 });
        return;
    }

    // ── HOST section ──────────────────────────────────────────────────────────
    UITextCenter(_bodyFont, "Start a game — others can join via your IP address.",
                 cx, (float)sh * 0.28f, 15.0f, Color{ 130, 160, 195, 255 });
    DrawButton(_uiFont, { cx - 130.0f, (float)sh * 0.35f, 260.0f, 56.0f }, "HOST GAME", 18.0f);

    // ── Divider ────────────────────────────────────────────────────────────────
    DrawRectangle((int)(cx - 200.0f), (int)((float)sh * 0.46f), 400, 1, Color{ 55, 72, 95, 140 });

    // ── JOIN section ──────────────────────────────────────────────────────────
    UITextCenter(_bodyFont, "JOIN GAME", cx, (float)sh * 0.48f, 13.0f, Color{ 100, 130, 165, 255 });

    // IP box
    Rectangle ipBoxR = { cx - 130.0f, (float)sh * 0.52f, 196.0f, 38.0f };
    bool editing = _editingIp;
    DrawRectangleRec(ipBoxR, editing ? Color{ 24, 36, 52, 230 } : Color{ 16, 22, 32, 210 });
    DrawRectangleLinesEx(ipBoxR, 1.0f,
        editing ? Color{ 80, 120, 180, 255 } : Color{ 52, 68, 88, 150 });
    if (editing) DrawTechCorners(ipBoxR, 6.0f, 1.2f, Color{ 80, 140, 220, 255 });
    std::string displayIp = _ipBuffer;
    if (editing && (int)(GetTime() * 2) % 2 == 0) displayIp += '_';
    DrawTextEx(_bodyFont, displayIp.c_str(),
               { ipBoxR.x + 8.0f, ipBoxR.y + 10.0f }, 16.0f, UISpacing,
               Color{ 200, 220, 248, 255 });

    // JOIN button
    DrawButton(_uiFont, { cx + 72.0f, (float)sh * 0.52f, 58.0f, 38.0f }, "JOIN", 13.0f);

    // Status line
    if (!_mpStatus.empty()) {
        UITextCenter(_bodyFont, _mpStatus.c_str(), cx, (float)sh * 0.62f, 15.0f,
                     Color{ 220, 100, 80, 255 });
    }
}

// ── Lifecycle ──────────────────────────────────────────────────────────────────

void MainMenu::OnEnter() {
    InitStars();
    BuildShowcaseList();
    BuildFactionList();

    _screen            = MenuScreen::Main;
    _showcaseIdx       = 0;
    _rotAngle          = 0.0f;
    _shipAlpha         = 0.0f;
    _showcaseState     = ShowcaseState::FadeIn;
    _fadeTimer         = 0.0f;
    _thrustBurstTimer  = (float)GetRandomValue(60, 200) / 100.0f;
    _thrustActiveTimer = 0.0f;
    _particles.clear();

    for (const auto* def : _showcaseShips) {
        if (!def->assetPath.empty())
            ResourceManager::Load(def->assetPath);
        else if (!def->designArray.empty())
            SpriteCache::BakeForId(def->id, FactionColor(def->paletteId), WHITE, def->designArray);
    }

    _uiFont = LoadFontEx("assets/fonts/Orbitron/Orbitron-VariableFont_wght.ttf", 96, nullptr, 0);
    if (_uiFont.texture.id == 0) _uiFont = GetFontDefault();
    else { GenTextureMipmaps(&_uiFont.texture); SetTextureFilter(_uiFont.texture, TEXTURE_FILTER_TRILINEAR); }

    _bodyFont = LoadFontEx("assets/fonts/Exo_2/Exo2-VariableFont_wght.ttf", 72, nullptr, 0);
    if (_bodyFont.texture.id == 0) _bodyFont = GetFontDefault();
    else { GenTextureMipmaps(&_bodyFont.texture); SetTextureFilter(_bodyFont.texture, TEXTURE_FILTER_TRILINEAR); }

    std::ifstream pf("saves/player_name.txt");
    if (pf.good()) {
        std::string line;
        if (std::getline(pf, line) && !line.empty())
            _playerName = line;
    }
}

void MainMenu::OnExit() {
    UnloadFont(_uiFont);   _uiFont   = {};
    UnloadFont(_bodyFont); _bodyFont = {};
    _particles.clear();
    _showcaseShips.clear();
    _factions.clear();
}

// ── Update ─────────────────────────────────────────────────────────────────────

void MainMenu::Update(float dt) {
    UpdateStars(dt);

    if (_screen == MenuScreen::Singleplayer) {
        UpdateSingleplayer(dt);
        return;
    }

    if (_screen == MenuScreen::Multiplayer) {
        UpdateMultiplayer(dt);
        return;
    }

    // Main screen
    int   sw = GetScreenWidth(), sh = GetScreenHeight();
    float cx      = sw * 0.5f;
    float circleR = std::min((float)sh * 0.20f, 185.0f);
    float circleCY = TitleY + TitleFontSize + CircleGapTop + circleR;
    UpdateShowcase(dt, cx, circleCY, circleR);
    if (_nameSavedTimer > 0.0f) _nameSavedTimer -= dt;

    float btnTop = circleCY + circleR + CircleGapBot;

    // Player name editor
    {
        float nameBoxX = cx - BtnW * 0.5f;
        float nameBoxY = btnTop + 20.0f;
        Rectangle nameBoxR = { nameBoxX, nameBoxY, BtnW - 70.0f, 32.0f };
        Rectangle saveBtnR = { nameBoxX + BtnW - 64.0f, nameBoxY, 64.0f, 32.0f };

        if (_editingName) {
            if (IsKeyPressed(KEY_ESCAPE)) {
                _editingName = false;
            } else {
                int ch;
                while ((ch = GetCharPressed()) != 0)
                    if (ch >= 32 && ch < 127 && (int)_editBuffer.size() < 20)
                        _editBuffer += (char)ch;
                if (IsKeyPressed(KEY_BACKSPACE) && !_editBuffer.empty())
                    _editBuffer.pop_back();
                if (IsClicked(saveBtnR) || IsKeyPressed(KEY_ENTER)) {
                    if (!_editBuffer.empty()) {
                        if (_editBuffer != _playerName) {
                            _playerName = _editBuffer;
                            std::ofstream pf("saves/player_name.txt");
                            if (pf.good()) pf << _playerName;
                            _nameSavedMessage = "Name saved!";
                        } else {
                            _nameSavedMessage = "Name unchanged.";
                        }
                        _nameSavedTimer = 2.0f;
                    }
                    _editingName = false;
                } else if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT) &&
                           !IsHovered(nameBoxR) && !IsHovered(saveBtnR)) {
                    _editingName = false;
                }
            }
        } else {
            if (IsClicked(nameBoxR)) {
                _editBuffer  = _playerName;
                _editingName = true;
            } else if (IsClicked(saveBtnR)) {
                _nameSavedMessage = "Name unchanged.";
                _nameSavedTimer   = 2.0f;
            }
        }
    }

    static constexpr float kNameWidgetOffset = 66.0f;
    auto BtnRect = [&](int i) -> Rectangle {
        return { cx - BtnW * 0.5f, btnTop + kNameWidgetOffset + (float)i * (BtnH + BtnGap), BtnW, BtnH };
    };

    if (IsClicked(BtnRect(0))) {  // Singleplayer
        _screen     = MenuScreen::Singleplayer;
        _factionIdx = 0;
        _hasSaves   = !SaveManager::Get().ListSaves().empty();
        OnFactionChanged();
    }
    if (IsClicked(BtnRect(1))) {  // Multiplayer
        _screen       = MenuScreen::Multiplayer;
        _mpState      = MpState::Choose;
        _mpStatus     = "";
        _connectTimer = 0.0f;
        _editingIp    = false;
    }
    if (IsClicked(BtnRect(4)))  // Quit
        GameManager::Get().RequestQuit();
}

// ── Draw ───────────────────────────────────────────────────────────────────────

void MainMenu::Draw() {
    int   sw = GetScreenWidth(), sh = GetScreenHeight();
    float cx = sw * 0.5f;

    DrawStars();

    if (_screen == MenuScreen::Singleplayer) {
        DrawSingleplayer();
        return;
    }

    if (_screen == MenuScreen::Multiplayer) {
        DrawMultiplayer();
        return;
    }

    // Main screen
    UITextCenter(_uiFont, "STAR REACH", cx, TitleY, TitleFontSize, RAYWHITE);

    float circleR  = std::min((float)sh * 0.20f, 185.0f);
    float circleCY = TitleY + TitleFontSize + CircleGapTop + circleR;
    DrawShowcase(cx, circleCY, circleR);

    float btnTop = circleCY + circleR + CircleGapBot;

    // Player name editor
    {
        float nameBoxX = cx - BtnW * 0.5f;
        float nameBoxY = btnTop + 20.0f;
        DrawTextEx(_bodyFont, "PLAYER NAME", { nameBoxX, btnTop + 4.0f }, 12.0f, UISpacing,
                   Color{ 100, 130, 165, 200 });

        Rectangle nameBoxR = { nameBoxX, nameBoxY, BtnW - 70.0f, 32.0f };
        bool editing = _editingName;
        DrawRectangleRec(nameBoxR, editing ? Color{ 24, 36, 52, 230 } : Color{ 16, 22, 32, 210 });
        DrawRectangleLinesEx(nameBoxR, 1.0f,
            editing ? Color{ 80, 120, 180, 255 } : Color{ 52, 68, 88, 150 });
        if (editing) DrawTechCorners(nameBoxR, 6.0f, 1.2f, Color{ 80, 140, 220, 255 });

        std::string displayName = editing ? _editBuffer : _playerName;
        if (editing && (int)(GetTime() * 2) % 2 == 0) displayName += '_';
        DrawTextEx(_bodyFont, displayName.c_str(),
                   { nameBoxR.x + 6.0f, nameBoxR.y + 8.0f }, 15.0f, UISpacing,
                   Color{ 200, 220, 248, 255 });

        Rectangle saveBtnR = { nameBoxX + BtnW - 64.0f, nameBoxY, 64.0f, 32.0f };
        DrawButton(_uiFont, saveBtnR, "SAVE", 11.0f);

        if (_nameSavedTimer > 0.0f) {
            float t = std::min(_nameSavedTimer / 0.5f, 1.0f);
            unsigned char a = (unsigned char)(t * 230.0f);
            const char* msg = _nameSavedMessage.c_str();
            float PW = MeasureTextEx(_bodyFont, msg, 13.0f, UISpacing).x + 24.0f;
            static constexpr float PH = 34.0f;
            float px = saveBtnR.x + saveBtnR.width + 10.0f;
            float py = saveBtnR.y + (saveBtnR.height - PH) * 0.5f;
            DrawRectangle((int)px, (int)py, (int)PW, (int)PH,
                Color{ 14, 28, 44, (unsigned char)(a * 240 / 230) });
            DrawRectangleLinesEx({ px, py, PW, PH }, 1.0f,
                Color{ 60, 130, 200, a });
            DrawTechCorners({ px, py, PW, PH }, 6.0f, 1.2f,
                Color{ 100, 170, 240, a });
            Vector2 ts = MeasureTextEx(_bodyFont, msg, 13.0f, UISpacing);
            DrawTextEx(_bodyFont, msg,
                { px + (PW - ts.x) * 0.5f, py + (PH - ts.y) * 0.5f },
                13.0f, UISpacing, Color{ 160, 210, 255, a });
        }
    }

    static const char* kLabels[]  = { "SINGLEPLAYER", "MULTIPLAYER", "ACHIEVEMENTS", "OPTIONS", "QUIT" };
    static const bool  kEnabled[] = { true, true, false, false, true };
    static constexpr float kNameWidgetOffset = 66.0f;
    for (int i = 0; i < 5; ++i) {
        Rectangle r = { cx - BtnW * 0.5f, btnTop + kNameWidgetOffset + (float)i * (BtnH + BtnGap), BtnW, BtnH };
        if (kEnabled[i]) DrawButton(_uiFont, r, kLabels[i], BtnFontSize);
        else             DrawButtonDisabled(_uiFont, r, kLabels[i], BtnFontSize);
    }
}
