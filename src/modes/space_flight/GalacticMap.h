#pragma once
#include "raylib.h"
#include <string>
#include <vector>

enum class GalacticMapAction { None, Close, WarpToSystem, OpenSystemMap, GoMainMenu };

// A system actually eligible for interaction this frame: the current
// system, a discovered system in view, or an undiscovered system inside
// hyperdrive range. The (much larger) decorative background field is never
// materialized as blips — see GalacticMap::BuildBackgroundDots.
struct GalacticBlip {
    unsigned int id;
    std::string  name;        // empty for undiscovered blips (never displayed)
    Vector2      galacticPos;
    bool         discovered;
    bool         isCurrent;
};

struct GalacticMapData {
    Vector2                   currentSystemPos;
    float                     hyperdriveRange   = 0.0f;
    unsigned int              currentSystemId   = 0;
    std::vector<unsigned int> discoveredSystemIds;
};

class GalacticMap {
public:
    bool isOpen = false;

    void Open();
    void Close()  { isOpen = false; _selectedId = 0; _dragging = false; }
    void Toggle() { isOpen ? Close() : Open(); }

    void         SetMapData(const GalacticMapData& data) { _mapData = data; }
    unsigned int WarpTargetId() const { return _warpTargetId; }

    GalacticMapAction Update(float dt);
    void              Draw()   const;

private:
    struct MapProjection {
        float cx, cy;       // world-space camera center
        float mapCX, mapCY; // screen-space center of the map panel
        float scale;        // screen px per world unit

        Vector2 Project(Vector2 wp) const {
            return { mapCX + (wp.x - cx) * scale,
                     mapCY + (wp.y - cy) * scale };
        }
        Vector2 Unproject(Vector2 sp) const {
            return { cx + (sp.x - mapCX) / scale,
                     cy + (sp.y - mapCY) / scale };
        }
    };

    // ── Camera (pan/zoom) ────────────────────────────────────────────────────
    Vector2 _camCenter    = {};
    float   _camScale     = 1.0f;
    bool    _dragging     = false;
    Vector2 _dragStartMouse = {};
    Vector2 _dragStartCam   = {};

    float           _time         = 0.0f;
    unsigned int    _selectedId   = 0;
    unsigned int    _warpTargetId = 0;
    GalacticMapData _mapData;

    MapProjection ComputeProjection(const Rectangle& mapRect) const;
    Rectangle     VisibleWorldRect(const Rectangle& mapRect) const;
    void          HandleCameraInput(float dt, const Rectangle& mapRect);

    std::vector<GalacticBlip> BuildInteractiveBlips(const Rectangle& mapRect) const;
    std::vector<Vector2>      BuildBackgroundDots(const Rectangle& mapRect,
                                                   const std::vector<GalacticBlip>& interactive) const;
};
