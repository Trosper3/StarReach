#include "raylib.h"
#include "core/GameManager.h"
#include "core/AudioManager.h"
#include "data/DataRegistry.h"
#include "engine/SpriteCache.h"
#include "engine/ResourceManager.h"

int main() {
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT);
    InitWindow(GetMonitorWidth(0), GetMonitorHeight(0), "StarReach");
    MaximizeWindow();
    SetExitKey(KEY_NULL);  // disable ESC closing the window
    SetTargetFPS(60);

    AudioManager::Get().Init();
    DataRegistry::Init();

    GameManager::Get().TransitionTo(GameMode::MainMenu);

    while (!WindowShouldClose() && !GameManager::Get().ShouldQuit()) {
        // Clamp the frame delta: when the window is minimized or loses focus,
        // GetFrameTime() returns the entire elapsed wall-clock time on the
        // catch-up frame. Feeding that spike downstream makes the starfield
        // teleport (all stars wrap at once) and physics tunnel. Cap it so the
        // sim never advances more than one slow frame's worth at a time.
        float dt = GetFrameTime();
        if (dt > 0.1f) dt = 0.1f;

        AudioManager::Get().UpdateMusic();
        GameManager::Get().Update(dt);

        BeginDrawing();
            ClearBackground(BLACK);
            GameManager::Get().Draw();
        EndDrawing();
    }

    AudioManager::Get().Shutdown();
    SpriteCache::Cleanup();
    ResourceManager::Cleanup();
    CloseWindow();
    return 0;
}
