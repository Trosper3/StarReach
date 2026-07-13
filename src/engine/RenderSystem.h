#pragma once
#include "../shared/Entity.h"
#include "../shared/entities/Hardpoint.h"
#include "SpriteCache.h"
#include "raylib.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace ecs {

    // Must be called inside a BeginMode2D / EndMode2D block managed by the caller.
    class RenderSystem {
    public:
        // Unified composited draw for fighters, capitals, and stations alike
        // (docs/plans/unified_hardpoint_tasks.md P2). Draws each alive
        // hardpoint's equipped module at pos + rotate(hp.localOffset +
        // slot.subOffset, rotation). localOffset/subOffset are always final
        // on-screen pixel units (matching the existing capital/station
        // convention — see GetCapitalHardpointWorldPos in SpaceFlight.cpp),
        // never multiplied by `scale`; `scale` only sizes the drawn icon, so
        // the same call works whether the hull itself is drawn at
        // pixelScale 1.0 (capitals/stations) or ~0.025 (fighters).
        //
        // No ModuleDef currently ships a designArray (P2-T5 finding — art
        // doesn't exist yet for any of the 58 module defs), so every slot
        // falls back to a small square colored by ModuleTypeColor until real
        // module art lands; the texture path is wired and will "just work"
        // once a designArray is added to a ModuleDef.
        static void DrawHardpointRig(Vector2 pos, float rotation, float scale,
                                      const std::vector<Hardpoint>& hardpoints,
                                      Color factionPrimary, Color factionAccent) {
            float cosR = cosf(rotation * DEG2RAD), sinR = sinf(rotation * DEG2RAD);
            auto rotate = [&](Vector2 v) -> Vector2 {
                return { v.x * cosR - v.y * sinR, v.x * sinR + v.y * cosR };
            };
            for (const auto& hp : hardpoints) {
                if (!hp.alive) continue;
                for (const auto& slot : hp.slots) {
                    if (!slot.equipped.has_value()) continue;
                    const ModuleDef& mod = *slot.equipped;
                    Vector2 localOff = { hp.localOffset.x + slot.subOffset.x,
                                          hp.localOffset.y + slot.subOffset.y };
                    Vector2 off      = rotate(localOff);
                    Vector2 drawPos  = { pos.x + off.x, pos.y + off.y };

                    Texture2D* tex = mod.texture;
                    if ((!tex || tex->id == 0) && !mod.designArray.empty())
                        tex = SpriteCache::BakeForId(mod.id, factionPrimary, factionAccent, mod.designArray);

                    if (tex && tex->id != 0) {
                        float w = tex->width * scale, h = tex->height * scale;
                        Rectangle src  = { 0.0f, 0.0f, (float)tex->width, (float)tex->height };
                        Rectangle dst  = { drawPos.x, drawPos.y, w, h };
                        Vector2   orig = { w * 0.5f, h * 0.5f };
                        DrawTexturePro(*tex, src, dst, orig, rotation, WHITE);
                    } else {
                        float sz = std::max(4.0f, 9.0f * scale);
                        Rectangle dst = { drawPos.x, drawPos.y, sz, sz };
                        DrawRectanglePro(dst, { sz * 0.5f, sz * 0.5f }, rotation, ModuleTypeColor(mod.type));
                    }
                }
            }
        }

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
            case ModuleType::Facility:   return Color{ 0, 200, 180, 255 }; // raylib has no built-in teal
            default:                     return DARKGRAY;
            }
        }
    };
} // namespace ecs
