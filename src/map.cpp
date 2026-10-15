// map.cpp - map page: pan/zoom map, drone markers, path + history of the selected drone
#include "map.h"

#include "fleet.h"
#include "routing.h"
#include "ui.h"

#include "raymath.h"

static Camera2D g_cam = {};

static const Rectangle MAP_VIEW    = { 270, 60, 1000, 650 };
static const Rectangle MAP_OVERLAY = { 280, 70, 350, 160 };

static Vector2 ToPx(Vector2 km) { return Vector2Scale(km, PX_PER_KM); }

void InitMapCamera() {
    g_cam.offset   = { MAP_VIEW.x + MAP_VIEW.width / 2, MAP_VIEW.y + MAP_VIEW.height / 2 };
    g_cam.target   = { WORLD_PX / 2, WORLD_PX / 2 };
    g_cam.rotation = 0.0f;
    g_cam.zoom     = 0.62f;
}

// ----------------------------------------------------------------------------
//  Sidebar: fleet list
// ----------------------------------------------------------------------------
static void DrawMapSidebar() {
    Rectangle panel = { 10, 60, 250, 650 };
    DrawRectangleRounded(panel, 0.02f, 8, COL_PANEL);
    DrawText("FLEET", (int)panel.x + 14, (int)panel.y + 10, 16, COL_DIM);

    Vector2 m = GetMousePosition();
    for (int i = 0; i < (int)g_fleet.size(); i++) {
        const Drone& d = g_fleet[i];
        Rectangle row = { panel.x + 6, panel.y + 36 + i * 44.0f, panel.width - 12, 42 };
        bool hover = CheckCollisionPointRec(m, row);
        Color bg = (i == g_app.selected) ? COL_SEL : (hover ? COL_HOVER : COL_PANEL_ALT);
        DrawRectangleRounded(row, 0.15f, 6, bg);

        Color tc = d.active ? COL_TEXT : COL_GREY;
        DrawCircle((int)row.x + 14, (int)row.y + 21, 6, d.active ? COL_GOOD : COL_GREY);
        DrawText(TextFormat("#%02d %s %s", d.id, d.make.c_str(), d.model.c_str()), (int)row.x + 28, (int)row.y + 6, 14, tc);
        if (d.active)
            DrawText(TextFormat("-> %s", g_locations[d.endLoc].name.c_str()), (int)row.x + 28, (int)row.y + 24, 14, COL_DIM);
        else
            DrawText("Inactive", (int)row.x + 28, (int)row.y + 24, 14, COL_GREY);
        DrawText(TextFormat("%.0f%%", d.battery), (int)(row.x + row.width - 44), (int)row.y + 6, 14,
                 d.active ? BatteryColor(d.battery) : COL_GREY);

        if (hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) g_app.selected = i;
    }
}

// ----------------------------------------------------------------------------
//  Pan / zoom / pick
// ----------------------------------------------------------------------------
static void HandleMapInput() {
    static Vector2 pressPos = { 0, 0 };
    static bool dragging = false;

    Vector2 m = GetMousePosition();
    bool inMap = CheckCollisionPointRec(m, MAP_VIEW);
    bool inOverlay = !g_fleet.empty() && CheckCollisionPointRec(m, MAP_OVERLAY);

    if (inMap && !inOverlay) {
        float wheel = GetMouseWheelMove();
        if (wheel != 0.0f) {
            Vector2 before = GetScreenToWorld2D(m, g_cam);
            g_cam.zoom = Clamp(g_cam.zoom * (1.0f + wheel * 0.12f), 0.3f, 3.0f);
            Vector2 after = GetScreenToWorld2D(m, g_cam);
            g_cam.target = Vector2Add(g_cam.target, Vector2Subtract(before, after));
        }
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) { dragging = true; pressPos = m; }
    }

    if (dragging && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
        g_cam.target = Vector2Subtract(g_cam.target, Vector2Scale(GetMouseDelta(), 1.0f / g_cam.zoom));
    }

    if (dragging && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
        dragging = false;
        if (Vector2Distance(m, pressPos) < 4.0f) {                 // a click, not a drag: pick nearest drone
            float best = 18.0f;
            for (int i = 0; i < (int)g_fleet.size(); i++) {
                Vector2 s = GetWorldToScreen2D(ToPx(g_fleet[i].pos), g_cam);
                float dist = Vector2Distance(s, m);
                if (dist < best) { best = dist; g_app.selected = i; }
            }
        }
    }
}

// ----------------------------------------------------------------------------
//  Overlay: selected drone summary + faster route
// ----------------------------------------------------------------------------
static void DrawMapOverlay() {
    if (g_fleet.empty()) return;
    Drone& d = g_fleet[g_app.selected];

    DrawRectangleRounded(MAP_OVERLAY, 0.06f, 8, Fade(BLACK, 0.78f));
    float x = MAP_OVERLAY.x + 12, y = MAP_OVERLAY.y + 10;

    DrawText(TextFormat("DRONE #%02d  -  %s", d.id, d.active ? "ACTIVE" : "INACTIVE"), (int)x, (int)y, 18,
             d.active ? COL_GOOD : COL_DIM);
    y += 28;

    if (d.active) {
        DrawText(TextFormat("%s -> %s", g_locations[d.startLoc].name.c_str(), g_locations[d.endLoc].name.c_str()),
                 (int)x, (int)y, 14, COL_TEXT);
        y += 20;
        float rem = RemainingKm(d);
        DrawText(TextFormat("Remaining %.1f km | ETA %.0f min | %.0f km/h", rem, rem / d.cruise * 60.0f, d.speed),
                 (int)x, (int)y, 14, COL_TEXT);
        y += 20;
        DrawText(TextFormat("Battery %.0f%% | Waypoints %d", d.battery, (int)d.path.size() - 1),
                 (int)x, (int)y, 14, BatteryColor(d.battery));
        y += 24;

        float direct = Vector2Distance(d.pos, g_locations[d.endLoc].pos);
        float saving = rem - direct;
        if (saving > 0.1f) {
            DrawText(TextFormat("Faster route: -%.1f km (%.0f%%)", saving, saving / rem * 100.0f),
                     (int)x, (int)y, 14, COL_WARN);
            if (Button({ x, y + 20, 200, 28 }, "Apply faster route")) ApplyFasterRoute(d);
        } else {
            DrawText("Current route is optimal", (int)x, (int)y, 14, COL_GOOD);
        }
    } else {
        DrawText(TextFormat("Parked at %s", d.locIndex >= 0 ? g_locations[d.locIndex].name.c_str() : "-"),
                 (int)x, (int)y, 14, COL_TEXT);
        y += 20;
        DrawText(TextFormat("Battery %.0f%%", d.battery), (int)x, (int)y, 14, BatteryColor(d.battery));
        y += 24;
        if (Button({ x, y + 4, 200, 28 }, "Dispatch", CanDispatch(d))) Dispatch(d);
    }
}

// ----------------------------------------------------------------------------
//  Map page
// ----------------------------------------------------------------------------
void DrawMap() {
    DrawMapSidebar();
    HandleMapInput();

    DrawRectangleRec(MAP_VIEW, COL_MAP_BG);
    BeginScissorMode((int)MAP_VIEW.x, (int)MAP_VIEW.y, (int)MAP_VIEW.width, (int)MAP_VIEW.height);

    float z = g_cam.zoom;
    BeginMode2D(g_cam);

    // ---- world + grid ----
    DrawRectangle(0, 0, (int)WORLD_PX, (int)WORLD_PX, COL_WORLD);
    for (int k = 0; k <= (int)WORLD_KM; k += 5) {
        float p = k * PX_PER_KM;
        Color c = (k % 10 == 0) ? COL_GRID_MAJOR : COL_GRID;
        DrawLineEx({ p, 0 }, { p, WORLD_PX }, 1.0f / z, c);
        DrawLineEx({ 0, p }, { WORLD_PX, p }, 1.0f / z, c);
    }
    DrawRectangleLinesEx({ 0, 0, WORLD_PX, WORLD_PX }, 2.0f / z, COL_BORDER);

    // ---- locations ----
    for (const Location& l : g_locations) DrawCircleV(ToPx(l.pos), 6.0f / z, COL_LOCATION);

    // ---- paths of other active drones (faint) ----
    for (int i = 0; i < (int)g_fleet.size(); i++) {
        const Drone& d = g_fleet[i];
        if (!d.active || i == g_app.selected) continue;
        Vector2 prev = ToPx(d.pos);
        for (int w = d.nextWp; w < (int)d.path.size(); w++) {
            Vector2 p = ToPx(d.path[w]);
            DrawLineEx(prev, p, 1.5f / z, Fade(COL_ACCENT, 0.3f));
            prev = p;
        }
    }

    // ---- selected drone: history, current path, waypoints, start/end ----
    if (!g_fleet.empty()) {
        const Drone& d = g_fleet[g_app.selected];

        for (size_t i = 1; i < d.history.size(); i++)
            DrawLineEx(ToPx(d.history[i - 1]), ToPx(d.history[i]), 2.0f / z, Fade(RAYWHITE, 0.45f));
        if (d.active && !d.history.empty())
            DrawLineEx(ToPx(d.history.back()), ToPx(d.pos), 2.0f / z, Fade(RAYWHITE, 0.45f));

        if (d.active) {
            Vector2 prev = ToPx(d.pos);
            for (int w = d.nextWp; w < (int)d.path.size(); w++) {
                Vector2 p = ToPx(d.path[w]);
                DrawLineEx(prev, p, 3.0f / z, COL_ACCENT);
                if (w < (int)d.path.size() - 1) DrawCircleV(p, 4.5f / z, COL_ACCENT);
                prev = p;
            }
            if (d.startLoc >= 0)
                DrawRing(ToPx(g_locations[d.startLoc].pos), 10.0f / z, 13.0f / z, 0, 360, 32, COL_GOOD);
            if (d.endLoc >= 0)
                DrawRing(ToPx(g_locations[d.endLoc].pos), 10.0f / z, 13.0f / z, 0, 360, 32, COL_BAD);
        }
    }

    // ---- drones ----
    for (int i = 0; i < (int)g_fleet.size(); i++) {
        const Drone& d = g_fleet[i];
        Vector2 p = ToPx(d.pos);
        Color c = d.active ? BatteryColor(d.battery) : COL_GREY;

        if (d.active && d.nextWp < (int)d.path.size()) {
            Vector2 dir = Vector2Normalize(Vector2Subtract(d.path[d.nextWp], d.pos));
            DrawLineEx(p, Vector2Add(p, Vector2Scale(dir, 16.0f / z)), 3.0f / z, c);
        }
        DrawCircleV(p, 7.0f / z, c);
        DrawCircleLines((int)p.x, (int)p.y, 7.0f / z, BLACK);
        if (i == g_app.selected) DrawRing(p, 10.0f / z, 12.5f / z, 0, 360, 32, WHITE);
    }

    EndMode2D();

    // ---- labels in screen space (crisp text) ----
    for (const Location& l : g_locations) {
        Vector2 s = GetWorldToScreen2D(ToPx(l.pos), g_cam);
        DrawText(l.name.c_str(), (int)s.x + 10, (int)s.y - 7, 14, COL_DIM);
    }
    for (int i = 0; i < (int)g_fleet.size(); i++) {
        const Drone& d = g_fleet[i];
        Vector2 s = GetWorldToScreen2D(ToPx(d.pos), g_cam);
        DrawText(TextFormat("D%02d", d.id), (int)s.x + 10, (int)s.y - 24, 14, d.active ? COL_TEXT : COL_GREY);
    }

    // scale bar + help
    float barLen = 5.0f * PX_PER_KM * g_cam.zoom;
    float bx = MAP_VIEW.x + 14, by = MAP_VIEW.y + MAP_VIEW.height - 26;
    DrawRectangle((int)bx, (int)by, (int)barLen, 3, RAYWHITE);
    DrawText("5 km", (int)bx, (int)by - 18, 14, RAYWHITE);
    const char* help = "Drag: pan   |   Wheel: zoom   |   Click drone: select";
    DrawText(help, (int)(MAP_VIEW.x + MAP_VIEW.width - MeasureText(help, 14) - 12),
             (int)(MAP_VIEW.y + MAP_VIEW.height - 24), 14, COL_DIM);

    EndScissorMode();

    DrawMapOverlay();
}
