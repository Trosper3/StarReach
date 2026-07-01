#pragma once
#include "ui/SavePicker.h"
#include "ui/SaveWriter.h"
#include "raylib.h"
#include <string>
#include <vector>

enum class MapAction { None, Close, GoMainMenu, SaveToFile, LoadGame, WarpTo, OpenGalacticMap };

struct MapBlip {
    unsigned int id;
    Vector2      worldPos;
    float        radius;
    bool         isPlanet;
    bool         discovered;
};

struct SystemMapData {
    Vector2              playerPos;
    float                hyperdriveRange = 0.0f;
    std::vector<MapBlip> blips;
};

class SystemMap {
public:
    bool isOpen = false;

    void Open()  { isOpen = true; _time = 0.0f; _selectedBlip = -1; }
    void Close() { isOpen = false; _savePicker.Close(); _saveWriter.Close(); _selectedBlip = -1; }
    void Toggle() { isOpen ? Close() : Open(); }

    void AckSave() { _saveFeedbackTimer = 2.8f; }
    const std::string& LoadFilename()    const { return _savePicker.SelectedFile(); }
    const std::string& SavePath()        const { return _saveWriter.TargetPath();   }
    const std::string& SaveDisplayName() const { return _saveWriter.DisplayName();  }
    bool IsPickerOpen() const { return _savePicker.IsOpen() || _saveWriter.IsOpen(); }

    void    SetMapData(const SystemMapData& data) { _mapData = data; }
    Vector2 WarpTarget() const { return _warpTarget; }

    MapAction Update(float dt);
    void Draw() const;

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

    float         _time              = 0.0f;
    float         _saveFeedbackTimer = 0.0f;
    int           _selectedBlip      = -1;
    Vector2       _warpTarget        = {};
    SystemMapData _mapData;
    SavePicker    _savePicker;
    SaveWriter    _saveWriter;

    MapProjection ComputeProjection(const Rectangle& mapRect) const;
};
