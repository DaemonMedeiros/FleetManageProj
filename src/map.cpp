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

        if (hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            g_app.selected    = i;
            g_app.selectedLoc = -1;   // picking a drone clears the location panel
        }
    }
}

// ----------------------------------------------------------------------------
//  Map picking (drones first, then location markers)
// ----------------------------------------------------------------------------
static void HandleMapPicking() {
    Vector2 m = GetMousePosition();
    if (!CheckCollisionPointRec(m, MAP_VIEW)) return;
    if (!IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) return;

    // Prefer a nearby drone hit (small radius) over a location hit.
    float best = 18.0f;
    int   bestDrone = -1;
    for (int i = 0; i < (int)g_fleet.size(); i++) {
        const Drone& d = g_fleet[i];
        if (!d.active) continue;                      // inactive drones are not drawn
        Vector2 s = ToScreen(d.pos);
        float dist = Vector2Distance(s, m);
        if (dist < best) { best = dist; bestDrone = i; }
    }
    if (bestDrone >= 0) {
        g_app.selected    = bestDrone;
        g_app.selectedLoc = -1;
        return;
    }

    // Otherwise check for a location marker hit.
    float bestLocDist = 12.0f;
    int   bestLoc = -1;
    for (int i = 0; i < (int)g_locations.size(); i++) {
        Vector2 s = ToScreen(g_locations[i].pos);
        float dist = Vector2Distance(s, m);
        if (dist < bestLocDist) { bestLocDist = dist; bestLoc = i; }
    }
    if (bestLoc >= 0) {
        g_app.selectedLoc = bestLoc;
        // A location panel takes over the overlay, so clear any "drone selected"
        // highlight to avoid a stale highlight ring on the map.
        g_app.selected    = -1;
    }
}

// ----------------------------------------------------------------------------
//  Right-hand status panel: drone detail (default) or location panel
// ----------------------------------------------------------------------------
static void DrawDroneOverlay() {
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

// Location panel: lists every drone parked at the selected location, with a
// per-drone Dispatch button and a "Dispatch all" button at the top.
static void DrawLocationOverlay() {
    const int loc = g_app.selectedLoc;
    if (loc < 0 || loc >= (int)g_locations.size()) return;

    const Location& L = g_locations[loc];
    const float x = MAP_OVERLAY.x + 14;
    const float w = MAP_OVERLAY.width - 28;
    float y = MAP_OVERLAY.y + 14;

    DrawText("SELECTED LOCATION", (int)x, (int)y, 12, COL_DIM); y += 18;
    DrawText(L.name.c_str(), (int)x, (int)y, 22, COL_TEXT); y += 30;

    DrawRectangle((int)x, (int)y, (int)w, 1, COL_BORDER); y += 12;

    // Collect every drone parked here.
    std::vector<int> here;
    int dispatchable = 0;
    for (int i = 0; i < (int)g_fleet.size(); i++) {
        const Drone& d = g_fleet[i];
        if (!d.active && d.locIndex == loc) {
            here.push_back(i);
            if (CanDispatch(d)) dispatchable++;
        }
    }

    DrawText(TextFormat("Docked: %d   Dispatchable: %d", (int)here.size(), dispatchable),
             (int)x, (int)y, 14, COL_DIM); y += 22;

    // "Dispatch all" is only enabled when at least one drone is dispatchable.
    if (Button({ x, y, w, 30 }, "Dispatch all", dispatchable > 0)) {
        for (int idx : here) {
            Drone& d = g_fleet[idx];
            if (CanDispatch(d)) Dispatch(d);
        }
    }
    y += 38;

    DrawRectangle((int)x, (int)y, (int)w, 1, COL_BORDER); y += 12;

    if (here.empty()) {
        DrawText("No drones docked here.", (int)x, (int)y, 14, COL_GREY);
        return;
    }

    // Scrollable per-drone list.
    static float s_locScroll = 0.0f;
    const float rowH = 40.0f;
    const float listTop = y;
    const float listH   = MAP_OVERLAY.y + MAP_OVERLAY.height - listTop - 14;

    Vector2 mouse = GetMousePosition();
    Rectangle listRect = { x, listTop, w, listH };
    if (CheckCollisionPointRec(mouse, listRect))
        s_locScroll -= GetMouseWheelMove() * 30.0f;
    float maxScroll = std::max(0.0f, here.size() * rowH - listH);
    s_locScroll = Clamp(s_locScroll, 0.0f, maxScroll);

    BeginScissorMode((int)listRect.x, (int)listRect.y,
                     (int)listRect.width, (int)listRect.height);

    for (int k = 0; k < (int)here.size(); k++) {
        const int idx = here[k];
        Drone& d = g_fleet[idx];

        float ry = listTop + k * rowH - s_locScroll;
        if (ry + rowH < listTop || ry > listTop + listH) continue;

        Rectangle row = { x, ry, w, rowH - 4 };
        bool hover = CheckCollisionPointRec(mouse, row) &&
                     CheckCollisionPointRec(mouse, listRect);
        if (hover) { g_app.selected = idx; g_app.selectedLoc = -1; /* handled below */ }

        DrawRectangleRec(row, (idx == g_app.selected) ? COL_SEL
                              : (hover ? COL_HOVER : COL_PANEL_ALT));
        DrawRectangleLinesEx(row, 1, COL_BORDER);

        DrawText(TextFormat("#%02d", d.id), (int)row.x + 6, (int)row.y + 4, 14, COL_TEXT);
        DrawText(TextFormat("%s %s", d.make.c_str(), d.model.c_str()),
                 (int)row.x + 40, (int)row.y + 4, 13, COL_TEXT);

        // Right-hand dispatch button.
        Rectangle btn = { row.x + row.width - 86, row.y + 5, 78, 26 };
        bool can = CanDispatch(d);
        if (Button(btn, "Dispatch", can)) Dispatch(d);

        // Small status line under the model.
        Color st = DamageColor(d.damage);
        DrawText(TextFormat("B %.0f%%  C %.0f%%  v%s",
                            d.battery, d.condition, d.sw.c_str()),
                 (int)row.x + 40, (int)row.y + 21, 12, st);
    }

    EndScissorMode();
}

static void DrawMapOverlay() {
    DrawRectangleRounded(MAP_OVERLAY, 0.02f, 8, COL_PANEL);
    DrawRectangleLinesEx(MAP_OVERLAY, 1, COL_BORDER);

    if (g_app.selectedLoc >= 0) {
        DrawLocationOverlay();
        return;
    }
    if (g_app.selected >= 0 && g_app.selected < (int)g_fleet.size()) {
        DrawDroneOverlay();
        return;
    }

    DrawText("Nothing selected.", (int)MAP_OVERLAY.x + 14,
             (int)MAP_OVERLAY.y + 14, 14, COL_DIM);
}

// ----------------------------------------------------------------------------
//  Simulation overlays
// ----------------------------------------------------------------------------
static void DrawSimulationOverlay() {
    BeginScissorMode((int)MAP_VIEW.x, (int)MAP_VIEW.y,
                     (int)MAP_VIEW.width, (int)MAP_VIEW.height);

    // Location markers - clickable.  Draw a subtle highlight on the selected one.
    for (int i = 0; i < (int)g_locations.size(); i++) {
        Vector2 p = ToScreen(g_locations[i].pos);
        DrawCircleV(p, 6.0f, COL_LOCATION);
        if (g_app.selectedLoc == i)
            DrawRing(p, 9.0f, 12.0f, 0, 360, 32, COL_ACCENT);
    }

    // Paths of active drones that are not the currently inspected one.
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

    // Selected drone history / path.  Skipped if no drone is selected.
    if (g_app.selected >= 0 && g_app.selected < (int)g_fleet.size()) {
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

    // Drone markers - ONLY active drones.  Inactive drones are drawn in the
    // sidebar and in the location panel, not as markers on the map.
    for (int i = 0; i < (int)g_fleet.size(); i++) {
        const Drone& d = g_fleet[i];
        if (!d.active) continue;

        Vector2 p = ToScreen(d.pos);
        Color c = BatteryColor(d.battery);

        if (d.nextWp < (int)d.path.size()) {
            Vector2 next = ToScreen(d.path[d.nextWp]);
            Vector2 dir = Vector2Normalize(Vector2Subtract(next, p));
            DrawLineEx(p, Vector2Add(p, Vector2Scale(dir, 16.0f)), 3.0f, c);
        }

        DrawCircleV(p, 7.0f, c);
        DrawCircleLines((int)p.x, (int)p.y, 7.0f, BLACK);
        if (i == g_app.selected)
            DrawRing(p, 10.0f, 12.5f, 0, 360, 32, WHITE);
    }

    // Location labels
    for (const Location& l : g_locations) {
        Vector2 s = ToScreen(l.pos);
        DrawText(l.name.c_str(), (int)s.x + 10, (int)s.y - 7, 14, COL_DIM);
    }

    // Active drone labels only (inactive markers are not drawn)
    for (int i = 0; i < (int)g_fleet.size(); i++) {
        const Drone& d = g_fleet[i];
        if (!d.active) continue;
        Vector2 s = ToScreen(d.pos);
        DrawText(TextFormat("D%02d", d.id), (int)s.x + 10, (int)s.y - 24, 14, COL_TEXT);
    }

    const char* help = TextFormat("Drag: pan  |  Wheel: OSM zoom  |  Click drone: select  |  Click location: dock");
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
    HandleMapPicking();

    DrawOsmMap(MAP_VIEW);
    DrawSimulationOverlay();
    DrawRectangleLinesEx(MAP_VIEW, 1, COL_BORDER);

    DrawMapOverlay();
}