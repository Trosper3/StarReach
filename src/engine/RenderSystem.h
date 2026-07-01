#pragma once
#include "../shared/Entity.h"
#include "raylib.h"
#include <vector>

namespace ecs {

    // Must be called inside a BeginMode2D / EndMode2D block managed by the caller.
    class RenderSystem {
    public:
        static void Draw(const std::vector<Entity>& entities) {
            for (const auto& e : entities) {
                // Skip unspawned entities (0 is null)
                if (e.id == 0) continue;

                // 1. Draw main hull sprite
                if (e.sprite.texture != nullptr && e.sprite.texture->id != 0) {
                    const Texture2D& tex = *e.sprite.texture;
                    float w = tex.width * e.sprite.scale;
                    float h = tex.height * e.sprite.scale;

                    Rectangle src = { 0.0f, 0.0f, (float)tex.width, (float)tex.height };
                    Rectangle dest = { e.transform.position.x, e.transform.position.y, w, h };
                    Vector2 orig = { w * 0.5f, h * 0.5f };

                    DrawTexturePro(tex, src, dest, orig, e.transform.rotation, e.sprite.tint);
                }

                // 2. Draw module textures at their specific offsets
                for (const auto& slot : e.loadout.slots) {
                    // FIX: Use 'slot.equipped' instead of 'slot.installedModule'
                    // FIX: Access the texture inside the ModuleDef object
                    if (slot.equipped.has_value() && slot.equipped->texture != nullptr) {

                        const Texture2D& modTex = *slot.equipped->texture;

                        // FIX: Use 'slot.hardpointOffset' instead of 'slot.offset'
                        Vector2 offset = slot.hardpointOffset;

                        // ... (rest of your rotation math remains the same) ...
                        float cosR = cosf(e.transform.rotation * DEG2RAD);
                        float sinR = sinf(e.transform.rotation * DEG2RAD);
                        Vector2 rotatedOffset = {
                            offset.x * cosR - offset.y * sinR,
                            offset.x * sinR + offset.y * cosR
                        };

                        // ... (rest of your draw logic) ...
                        Rectangle src = { 0.0f, 0.0f, (float)modTex.width, (float)modTex.height };
                        Rectangle dest = {
                            e.transform.position.x + rotatedOffset.x,
                            e.transform.position.y + rotatedOffset.y,
                            (float)modTex.width * e.sprite.scale,
                            (float)modTex.height * e.sprite.scale
                        };
                        Vector2 orig = { (float)modTex.width * 0.5f * e.sprite.scale,
                                         (float)modTex.height * 0.5f * e.sprite.scale };

                        DrawTexturePro(modTex, src, dest, orig, e.transform.rotation, WHITE);
                    }
                }
            }
        }

    private:
        static Color ModuleTypeColor(ModuleType t) {
            switch (t) {
            case ModuleType::Weapon:     return RED;
            case ModuleType::Armor:      return GRAY;
            case ModuleType::Shield:     return SKYBLUE;
            case ModuleType::Engine:     return YELLOW;
            case ModuleType::Hyperdrive: return PURPLE;
            default:                     return DARKGRAY;
            }
        }
    };
} // namespace ecs
