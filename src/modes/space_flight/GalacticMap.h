#pragma once
#include "raylib.h"
#include <string>
#include <vector>

enum class GalacticMapAction { None, Close, WarpToSystem, OpenSystemMap, GoMainMenu };

struct GalacticBlip {
    unsigned int id;
    std::string  name;
    Vector2      galacticPos;
    bool         discovered;
    bool         isCurrent;
};

struct GalacticMapData {
    Vector2                   currentSystemPos;
    float                     hyperdriveRange = 0.0f;
    unsigned int              currentSystemId = 0;
    std::vector<GalacticBlip> systems;
};

class GalacticMap {
public:
    bool isOpen = false;

    void Open()   { isOpen = true; _time = 0.0f; _selectedBlip = -1; }
    void Close()  { isOpen = false; _selectedBlip = -1; }
    void Toggle() { isOpen ? Close() : Open(); }

    void         SetMapData(const GalacticMapData& data) { _mapData = data; }
    unsigned int WarpTargetId() const { return _warpTargetId; }

    GalacticMapAction Update(float dt);
    void              Draw()   const;

private:
    struct MapProjection {
        float cx, cy;
        float mapCX, mapCY;
        float scale;

        Vector2 Project(Vector2 wp) const {
            return { mapCX + (wp.x - cx) * scale,
                     mapCY + (wp.y - cy) * scale };
        }
    };

    float           _time         = 0.0f;
    int             _selectedBlip = -1;
    unsigned int    _warpTargetId = 0;
    GalacticMapData _mapData;

    MapProjection ComputeProjection(const Rectangle& mapRect) const;
};
