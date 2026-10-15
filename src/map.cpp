// map.cpp - map page: dynamic OSM tiles, drone markers, paths and history
#include "map.h"

#include "fleet.h"
#include "osm_map.h"
#include "routing.h"
#include "ui.h"

#include "raymath.h"

static const Rectangle MAP_VIEW    = { 270, 60, 700, 650 };
static const Rectangle MAP_OVERLAY = { 980, 60, 290, 650 };

static Vector2 ToScreen(Vector2 km) {
    return LegacyWorldToScreen(km, WORLD_KM, MAP_VIEW);
}

void InitMapCamera() {
    // Kept so the rest of the project does not need a wider interface change.
    InitOsmMap();
}

// ----------------------------------------------------------------------------
//  Sidebar: fleet list
// ----------------------------------------------------------------------------
static void DrawMapSidebar() {
    Rectangle panel = { 10, 60, 250, 650 };
    DrawRectangleRounded(panel, 0.02f, 8, COL_PANEL);
    DrawRectangleLinesEx(panel, 1, COL_BORDER);
    DrawText("FLEET", (int)panel.x + 14, (int)panel.y + 10, 16, COL_DIM);

    Vector2 m = GetMousePosition();
    for (int i = 0; i < (int)g_fleet.size(); i++) {
        const Drone& d = g_fleet[i];
        Rectangle row = { panel.x + 6, panel.y + 36 + i * 44.0f, panel.width - 12, 42 };
        bool hover = CheckCollisionPointRec(m, row);
        Color bg = (i == g_app.selected) ? COL_SEL : (hover ? COL_HOVER : COL_PANEL_ALT);
        DrawRectangleRec(row, bg);
        if (i == g_app.selected) DrawRectangleLinesEx(row, 1, COL_ACCENT);

        Color tc = d.active ? COL_TEXT : COL_GREY;
        DrawCircle((int)row.x + 14, (int)row.y + 21, 6, d.active ? COL_GOOD : COL_GREY);
        DrawText(TextFormat("#%02d %s %s", d.id, d.make.c_str(), d.model.c_str()),
                 (int)row.x + 28, (int)row.y + 6, 14, tc);
        if (d.active)
            DrawText(TextFormat("-> %s", g_locations[d.endLoc].name.c_str()),
                     (int)row.x + 28, (int)row.y + 24, 14, COL_DIM);
        else
            DrawText("Inactive", (int)row.x + 28, (int)row.y + 24, 14, COL_GREY);

        DrawText(TextFormat("%.0f%%", d.battery),
                 (int)(row.x + row.width - 44), (int)row.y + 6, 14,
                 d.active ? BatteryColor(d.battery) : COL_GREY);

        if (hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) g_app.selected = i;
    }
}

// ----------------------------------------------------------------------------
//  Map picking
// ----------------------------------------------------------------------------
static void HandleDronePick() {
    Vector2 m = GetMousePosition();
    if (!CheckCollisionPointRec(m, MAP_VIEW)) return;
    if (!IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) return;

    // If the release follows an actual map drag, UpdateOsmMap consumed the
    // navigation. A simple click can still select the nearest drone.
    float best = 18.0f;
    for (int i = 0; i < (int)g_fleet.size(); i++) {
        Vector2 s = ToScreen(g_fleet[i].pos);
        float dist = Vector2Distance(s, m);
        if (dist < best) {
            best = dist;
            g_app.selected = i;
        }
    }
}

// ----------------------------------------------------------------------------
//  Right-hand status panel
// ----------------------------------------------------------------------------
static void DrawMapOverlay() {
    DrawRectangleRounded(MAP_OVERLAY, 0.02f, 8, COL_PANEL);
    DrawRectangleLinesEx(MAP_OVERLAY, 1, COL_BORDER);

    if (g_fleet.empty()) return;
    Drone& d = g_fleet[g_app.selected];

    const float x = MAP_OVERLAY.x + 14;
    const float w = MAP_OVERLAY.width - 28;
    float y = MAP_OVERLAY.y + 14;

    DrawText("SELECTED DRONE", (int)x, (int)y, 12, COL_DIM); y += 18;
    DrawText(TextFormat("DRONE #%02d", d.id), (int)x, (int)y, 22, COL_TEXT); y += 28;
    DrawText(d.active ? "ACTIVE" : "INACTIVE", (int)x, (int)y, 16,
             d.active ? COL_GOOD : COL_GREY); y += 22;

    DrawRectangle((int)x, (int)y, (int)w, 1, COL_BORDER); y += 12;

    DrawText("MODEL", (int)x, (int)y, 12, COL_DIM); y += 16;
    DrawText(TextFormat("%s %s", d.make.c_str(), d.model.c_str()), (int)x, (int)y, 14, COL_TEXT);
    y += 24;

    DrawText("BATTERY", (int)x, (int)y, 12, COL_DIM); y += 16;
    DrawBatteryBar({ x, y, w, 14 }, d.battery, false); y += 20;
    DrawText(TextFormat("%.0f%%", d.battery), (int)x, (int)y, 14, BatteryColor(d.battery));
    y += 24;

    DrawRectangle((int)x, (int)y, (int)w, 1, COL_BORDER); y += 12;

    if (d.active) {
        DrawText("FROM", (int)x, (int)y, 12, COL_DIM); y += 16;
        DrawText(g_locations[d.startLoc].name.c_str(), (int)x, (int)y, 14, COL_TEXT); y += 20;
        DrawText("TO", (int)x, (int)y, 12, COL_DIM); y += 16;
        DrawText(g_locations[d.endLoc].name.c_str(), (int)x, (int)y, 14, COL_TEXT); y += 26;

        float rem = RemainingKm(d);
        DrawText(TextFormat("Speed       %4.0f km/h", d.speed), (int)x, (int)y, 14, COL_TEXT); y += 18;
        DrawText(TextFormat("Remaining   %4.1f km", rem), (int)x, (int)y, 14, COL_TEXT); y += 18;
        DrawText(TextFormat("ETA         %4.0f min", rem / d.cruise * 60.0f),
                 (int)x, (int)y, 14, COL_TEXT); y += 18;
        DrawText(TextFormat("Waypoints   %4d", (int)d.path.size() - 1),
                 (int)x, (int)y, 14, COL_TEXT); y += 26;

        float direct = Vector2Distance(d.pos, g_locations[d.endLoc].pos);
        float saving = rem - direct;
        if (saving > 0.1f) {
            DrawText(TextFormat("Faster route: -%.1f km", saving), (int)x, (int)y, 14, COL_WARN); y += 18;
            DrawText(TextFormat("              (%.0f%%)", saving / rem * 100.0f),
                     (int)x, (int)y, 14, COL_WARN); y += 22;
            if (Button({ x, y, w, 30 }, "Apply faster route")) ApplyFasterRoute(d);
        } else {
            DrawText("Current route is optimal", (int)x, (int)y, 14, COL_GOOD);
        }
    } else {
        DrawText("PARKED AT", (int)x, (int)y, 12, COL_DIM); y += 16;
        DrawText(d.locIndex >= 0 ? g_locations[d.locIndex].name.c_str() : "-",
                 (int)x, (int)y, 14, COL_TEXT); y += 26;

        DrawText(TextFormat("Condition   %s", DamageText(d.damage)),
                 (int)x, (int)y, 14, DamageColor(d.damage)); y += 18;
        DrawText(TextFormat("Software    v%s", d.sw.c_str()), (int)x, (int)y, 14,
                 SwOutdated(d) ? COL_WARN : COL_TEXT); y += 26;

        if (Button({ x, y, w, 30 }, "Dispatch", CanDispatch(d))) Dispatch(d);
    }
}

// ----------------------------------------------------------------------------
//  Simulation overlays
// ----------------------------------------------------------------------------
static void DrawSimulationOverlay() {
    BeginScissorMode((int)MAP_VIEW.x, (int)MAP_VIEW.y,
                     (int)MAP_VIEW.width, (int)MAP_VIEW.height);

    // Locations
    for (const Location& l : g_locations) {
        Vector2 p = ToScreen(l.pos);
        DrawCircleV(p, 6.0f, COL_LOCATION);
    }

    // Paths of other active drones
    for (int i = 0; i < (int)g_fleet.size(); i++) {
        const Drone& d = g_fleet[i];
        if (!d.active || i == g_app.selected) continue;

        Vector2 prev = ToScreen(d.pos);
        for (int w = d.nextWp; w < (int)d.path.size(); w++) {
            Vector2 p = ToScreen(d.path[w]);
            DrawLineEx(prev, p, 1.5f, Fade(COL_ACCENT, 0.3f));
            prev = p;
        }
    }

    // Selected drone history/path
    if (!g_fleet.empty()) {
        const Drone& d = g_fleet[g_app.selected];

        for (size_t i = 1; i < d.history.size(); i++)
            DrawLineEx(ToScreen(d.history[i - 1]), ToScreen(d.history[i]),
                       2.0f, Fade(RAYWHITE, 0.45f));

        if (d.active && !d.history.empty())
            DrawLineEx(ToScreen(d.history.back()), ToScreen(d.pos),
                       2.0f, Fade(RAYWHITE, 0.45f));

        if (d.active) {
            Vector2 prev = ToScreen(d.pos);
            for (int w = d.nextWp; w < (int)d.path.size(); w++) {
                Vector2 p = ToScreen(d.path[w]);
                DrawLineEx(prev, p, 3.0f, COL_ACCENT);
                if (w < (int)d.path.size() - 1) DrawCircleV(p, 4.5f, COL_ACCENT);
                prev = p;
            }

            if (d.startLoc >= 0)
                DrawRing(ToScreen(g_locations[d.startLoc].pos), 10.0f, 13.0f,
                         0, 360, 32, COL_GOOD);
            if (d.endLoc >= 0)
                DrawRing(ToScreen(g_locations[d.endLoc].pos), 10.0f, 13.0f,
                         0, 360, 32, COL_BAD);
        }
    }

    // Drones
    for (int i = 0; i < (int)g_fleet.size(); i++) {
        const Drone& d = g_fleet[i];
        Vector2 p = ToScreen(d.pos);
        Color c = d.active ? BatteryColor(d.battery) : COL_GREY;

        if (d.active && d.nextWp < (int)d.path.size()) {
            Vector2 next = ToScreen(d.path[d.nextWp]);
            Vector2 dir = Vector2Normalize(Vector2Subtract(next, p));
            DrawLineEx(p, Vector2Add(p, Vector2Scale(dir, 16.0f)), 3.0f, c);
        }

        DrawCircleV(p, 7.0f, c);
        DrawCircleLines((int)p.x, (int)p.y, 7.0f, BLACK);
        if (i == g_app.selected)
            DrawRing(p, 10.0f, 12.5f, 0, 360, 32, WHITE);
    }

    // Labels
    for (const Location& l : g_locations) {
        Vector2 s = ToScreen(l.pos);
        DrawText(l.name.c_str(), (int)s.x + 10, (int)s.y - 7, 14, COL_DIM);
    }

    for (int i = 0; i < (int)g_fleet.size(); i++) {
        const Drone& d = g_fleet[i];
        Vector2 s = ToScreen(d.pos);
        DrawText(TextFormat("D%02d", d.id), (int)s.x + 10, (int)s.y - 24, 14,
                 d.active ? COL_TEXT : COL_GREY);
    }

    // Help / current zoom
    const char* help = TextFormat("Drag: pan  |  Wheel: OSM zoom  |  Zoom: %d", GetOsmZoom());
    DrawRectangle((int)MAP_VIEW.x + 8, (int)(MAP_VIEW.y + MAP_VIEW.height - 29),
                  MeasureText(help, 14) + 12, 21, Fade(BLACK, 0.65f));
    DrawText(help, (int)MAP_VIEW.x + 14,
             (int)(MAP_VIEW.y + MAP_VIEW.height - 25), 14, RAYWHITE);

    EndScissorMode();
}

// ----------------------------------------------------------------------------
//  Map page
// ----------------------------------------------------------------------------
void DrawMap() {
    DrawMapSidebar();

    UpdateOsmMap(MAP_VIEW);
    HandleDronePick();

    DrawOsmMap(MAP_VIEW);
    DrawSimulationOverlay();
    DrawRectangleLinesEx(MAP_VIEW, 1, COL_BORDER);

    DrawMapOverlay();
}
