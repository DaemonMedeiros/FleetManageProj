// main.cpp - window setup, main loop, page switching
#include "raylib.h"

#include "automation.h"
#include "dashboard.h"
#include "fleet.h"
#include "map.h"
#include "osm_map.h"
#include "settings.h"
#include "ui.h"

#include <ctime>

int main() {
    SetConfigFlags(FLAG_MSAA_4X_HINT);
    InitWindow(SCREEN_W, SCREEN_H, "Drone Fleet Manager");
    SetTargetFPS(60);
    SetRandomSeed((unsigned int)time(NULL));

    InitFleet();
    InitMapCamera();

    while (!WindowShouldClose()) {
        // ---- input ----
        if (IsKeyPressed(KEY_TAB)) {
            // Cycle through the three pages.
            g_app.page = (Page)((int)(g_app.page + 1) % 3);
        }
        if (IsKeyPressed(KEY_ONE))   g_app.page = PAGE_DASHBOARD;
        if (IsKeyPressed(KEY_TWO))   g_app.page = PAGE_MAP;
        if (IsKeyPressed(KEY_THREE)) g_app.page = PAGE_SETTINGS;
        if (IsKeyPressed(KEY_SPACE)) g_app.paused = !g_app.paused;
        if (IsKeyPressed(KEY_A)) {
            g_app.automation = !g_app.automation;
            if (!g_app.automation) ReleaseAllFromAutomation();
        }

        // ---- simulation ----
        if (!g_app.paused) {
            float dt = GetFrameTime() * BASE_TIME_SCALE * (float)g_app.simMult;
            UpdateFleet(dt);
            if (g_app.automation) UpdateAutomation(dt, g_app.selected);
        }

        // ---- draw ----
        BeginDrawing();
        ClearBackground(COL_BG);
        DrawHeader();
        switch (g_app.page) {
            case PAGE_DASHBOARD: DrawDashboard(); break;
            case PAGE_MAP:       DrawMap();       break;
            case PAGE_SETTINGS:  DrawSettings();  break;
        }
        EndDrawing();
    }

    UnloadOsmMap();
    CloseWindow();
    return 0;
}