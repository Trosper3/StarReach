#include "LightingSystem.h"
#include "raymath.h"
#include <cmath>

namespace ecs {

static constexpr float kAmbient = 0.18f;

void LightingSystem::Init() {
    _shader    = LoadShader(nullptr, "assets/shaders/lighting.frag");
    _useShader = (_shader.id != 0);
    if (_useShader)
        _lightIntLoc = GetShaderLocation(_shader, "lightIntensity");
}

void LightingSystem::Shutdown() {
    if (_useShader) UnloadShader(_shader);
    _shader    = {};
    _useShader = false;
}

Color LightingSystem::BeginLit(Vector2 objectPos, Vector2 sunPos, float maxLightRange) const {
    if (maxLightRange <= 0.0f) return WHITE;

    float dist      = Vector2Distance(objectPos, sunPos);
    float intensity = 1.0f - Clamp(dist / maxLightRange, 0.0f, 1.0f);

    if (_useShader) {
        SetShaderValue(_shader, _lightIntLoc, &intensity, SHADER_UNIFORM_FLOAT);
        BeginShaderMode(_shader);
        return WHITE;
    }

    // Tint fallback: flat distance-based brightness
    float b  = kAmbient + (1.0f - kAmbient) * intensity;
    auto  b8 = static_cast<unsigned char>(b * 255.0f);
    return Color{ b8, b8, b8, 255 };
}

void LightingSystem::EndLit() const {
    if (_useShader) EndShaderMode();
}

} // namespace ecs
