#pragma once
#include "raylib.h"

namespace ecs {

class LightingSystem {
public:
    // Attempts to compile the lighting shader. Falls back to tint mode on failure.
    void Init();
    void Shutdown();

    // Call before DrawTexturePro for a lit sprite. Returns the tint to pass to the draw call.
    // Shader mode: enters BeginShaderMode, sets uniforms, returns WHITE.
    // Tint fallback: returns a computed gray tint, no shader state change.
    // Marked const so it can be called from const draw helpers.
    Color BeginLit(Vector2 objectPos, Vector2 sunPos, float maxLightRange) const;
    void  EndLit() const;

    bool IsShaderActive() const { return _useShader; }

private:
    Shader _shader      = {};
    bool   _useShader   = false;
    int    _lightIntLoc = -1;
};

} // namespace ecs
