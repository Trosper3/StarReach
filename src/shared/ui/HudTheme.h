#pragma once
#include "raylib.h"

// Shared chrome/glass HUD theme — palette + panel/button drawing helpers used
// by SpaceFlight's cockpit HUD and any other screen that wants to match it
// (e.g. the SystemMap pause menu).
namespace hudtheme {

inline const Color HudBg       = {  9, 14, 20, 218 }; // glass panel fill
inline const Color HudBorder   = { 90,150,190, 210 }; // chrome — structural frame
inline const Color HudLabel    = { 95,125,150, 255 }; // dim chrome — micro labels
inline const Color HudValue    = { 210,230,245, 255 }; // bright chrome — values
inline const Color HudDiv      = { 45, 68, 88, 150 }; // hairline dividers
inline const Color HudGood     = { 60,210,130, 255 }; // healthy / ready / friendly
inline const Color HudCaution  = { 235,175, 60, 255 }; // degraded / neutral
inline const Color HudCritical = { 230, 70, 70, 255 }; // critical / hostile

// Glass panel with corner brackets instead of a full border box — reads as an
// instrument bezel rather than a filled debug rectangle.
inline void DrawHudBracketPanel(Rectangle r, Color glass, Color chrome,
    float corner = 16.0f, float thick = 2.0f) {
    DrawRectangleRec(r, glass);
    auto Bracket = [&](Vector2 o, Vector2 h, Vector2 v) {
        DrawLineEx(o, { o.x + h.x, o.y + h.y }, thick, chrome);
        DrawLineEx(o, { o.x + v.x, o.y + v.y }, thick, chrome);
        };
    Bracket({ r.x,             r.y },              {  corner, 0 }, { 0,  corner });
    Bracket({ r.x + r.width,   r.y },               { -corner, 0 }, { 0,  corner });
    Bracket({ r.x,             r.y + r.height },    {  corner, 0 }, { 0, -corner });
    Bracket({ r.x + r.width,   r.y + r.height },    { -corner, 0 }, { 0, -corner });
}

// Chamfered ("cut corner") rect used for HUD tab buttons — reads as a sci-fi
// panel tab rather than a plain rectangle.
inline void DrawHudChamferRect(Rectangle r, float cut, Color fill, Color border, float thick = 1.0f) {
    Vector2 pts[6] = {
        { r.x + cut,           r.y                 },
        { r.x + r.width,       r.y                 },
        { r.x + r.width,       r.y + r.height - cut },
        { r.x + r.width - cut, r.y + r.height       },
        { r.x,                 r.y + r.height       },
        { r.x,                 r.y + cut            },
    };
    DrawTriangleFan(pts, 6, fill);
    for (int i = 0; i < 6; ++i) DrawLineEx(pts[i], pts[(i + 1) % 6], thick, border);
}

inline void DrawHudButton(Rectangle r, Color fill, Color border, Color fg,
    const char* label, const Font& font, float fontSize) {
    DrawHudChamferRect(r, 6.0f, fill, border, 1.5f);
    Vector2 ts = MeasureTextEx(font, label, fontSize, 1.0f);
    DrawTextEx(font, label, { r.x + (r.width - ts.x) / 2.0f, r.y + (r.height - ts.y) / 2.0f },
        fontSize, 1.0f, fg);
}

} // namespace hudtheme
