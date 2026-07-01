#pragma once
#include "core/SaveManager.h"
#include <string>
#include <vector>
#include "raylib.h"

// Modal overlay for selecting a save slot to load.
// Embed as a member in MainMenu or GalaxyMap.
class SavePicker {
public:
    enum class Result { None, Selected, Cancelled };

    void Open();
    void Close()  { _open = false; }
    bool IsOpen() const { return _open; }

    const std::string& SelectedFile() const { return _selectedFile; }

    Result Update();
    void   Draw() const;

private:
    Rectangle _delBtnRect;
    bool                             _open        = false;
    std::vector<SaveManager::SaveMeta> _saves;
    int                              _hoveredIdx  = -1;
    int                              _selectedIdx = -1;
    std::string                      _selectedFile;

    void CalcRects(int& px, int& py, int& pw, int& ph,
                   int& listX, int& listY, int& listW, int& rowH) const;
};
