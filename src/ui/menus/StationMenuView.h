#pragma once
#include "shared/entities/LoadoutComponent.h"
#include "shared/entities/InventoryComponent.h"
#include "modes/space_flight/StorageMenu.h"
#include "raylib.h"
#include <cstdio>

// View for the ECS station module menu.
// Reads LoadoutComponent and InventoryComponent; renders only — no math, no mutation.
struct StationMenuView {
    static void Draw(const LoadoutComponent& loadout, const InventoryComponent& inventory) {
        const int sw = GetScreenWidth();
        const int sh = GetScreenHeight();

        DrawRectangle(0, 0, sw, sh, {1, 3, 1, 255});

        // Title
        const char* title = "STATION MODULES";
        DrawText(title, (sw - MeasureText(title, 26)) / 2, 18, 26, {68, 162, 68, 255});
        DrawRectangle((sw - 400) / 2, 54, 400, 1, {34, 98, 34, 170});

        // Panel split — loadout on left, resources on right
        const int splitX  = sw * 56 / 100;
        const int padX    = 50;
        const int startY  = 75;
        const int slotSz  = 64;
        const int rowH    = 84;
        const int labelW  = 140;
        const int slotsX  = padX + labelW;

        // Left panel header + vertical divider
        DrawText("LOADOUT", padX, startY - 8, 13, {68, 162, 68, 195});
        DrawRectangle(splitX, 60, 1, sh - 80, {34, 98, 34, 120});

        // Module slots
        for (int i = 0; i < static_cast<int>(loadout.slots.size()); ++i) {
            const ModuleSlot& ms = loadout.slots[i];
            const int slotY = startY + 15 + i * rowH;

            // Row type label
            Color lc = StorageMenu::TypeColor(ms.type);
            DrawText(StorageMenu::TypeName(ms.type),
                     padX, slotY + (slotSz - 14) / 2, 14, {lc.r, lc.g, lc.b, 200});

            // Slot box
            Rectangle r = {
                static_cast<float>(slotsX),
                static_cast<float>(slotY),
                static_cast<float>(slotSz),
                static_cast<float>(slotSz)
            };
            DrawRectangleRec(r, {10, 18, 10, 220});
            DrawRectangleLinesEx(r, 1.0f, {34, 98, 34, 155});

            if (ms.equipped) {
                const ModuleDef& mod = *ms.equipped;
                Color gc = StorageMenu::GradeColor(mod.grade);
                DrawRectangleLinesEx({r.x + 1, r.y + 1, r.width - 2, r.height - 2},
                                     2.0f, {gc.r, gc.g, gc.b, 170});

                // Module name (top line) and grade (bottom line)
                int nameW = MeasureText(mod.displayName.c_str(), 10);
                DrawText(mod.displayName.c_str(),
                         static_cast<int>(r.x + (r.width - nameW) * 0.5f),
                         static_cast<int>(r.y + r.height * 0.5f - 12),
                         10, {210, 235, 210, 230});

                const char* gn = StorageMenu::GradeName(mod.grade);
                int gnW = MeasureText(gn, 8);
                DrawText(gn,
                         static_cast<int>(r.x + (r.width - gnW) * 0.5f),
                         static_cast<int>(r.y + r.height - 13),
                         8, {gc.r, gc.g, gc.b, 160});
            } else {
                const char* em = "EMPTY";
                DrawText(em,
                         static_cast<int>(r.x + (r.width - MeasureText(em, 9)) * 0.5f),
                         static_cast<int>(r.y + r.height * 0.5f - 5),
                         9, {50, 70, 50, 110});
            }
        }

        // Right panel — resource balance
        const int rPadX = splitX + 30;
        DrawText("RESOURCES", rPadX, startY - 8, 13, {68, 162, 68, 195});

        if (inventory.items.empty()) {
            DrawText("(empty)", rPadX, startY + 18, 12, {50, 80, 50, 160});
        } else {
            const int itemRowH = 22;
            int row = 0;
            for (const auto& [id, qty] : inventory.items) {
                const int iy = startY + 18 + row * itemRowH;
                DrawText(id.c_str(), rPadX, iy, 13, {160, 210, 160, 240});
                char qbuf[16];
                std::snprintf(qbuf, sizeof(qbuf), "x%d", qty);
                DrawText(qbuf, rPadX + 170, iy, 13, {100, 180, 100, 220});
                ++row;
            }
        }
    }
};
