#pragma once
#include "core/SaveManager.h"
#include <string>
#include <vector>
#include "raylib.h"

// Modal overlay for saving the game.
// Shows existing saves (click to overwrite) + text input to create a named new save.
class SaveWriter {
public:
    enum class Result { None, Saved, Cancelled };

    void Open();
    void Close() { _open = false; }
    bool IsOpen() const { return _open; }

    const std::string& TargetPath()    const { return _targetPath;    }
    const std::string& DisplayName()   const { return _displayName;   }

    Result Update();
    void   Draw()   const;

private:
    bool _open = false;
    std::vector<SaveManager::SaveMeta> _saves;
    int  _hoveredIdx = -1;
    bool _textActive = false;
    std::string _inputText;
    std::string _targetPath;
    std::string _displayName;

    void CalcRects(int& px, int& py, int& pw, int& ph,
                   int& listX, int& listY, int& listW, int& rowH) const;
};
