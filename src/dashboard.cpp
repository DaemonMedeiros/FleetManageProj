// dashboard.cpp - dashboard page: summary cards, drone list, detail panel
#include "dashboard.h"

#include "automation.h"
#include "fleet.h"
#include "routing.h"
#include "ui.h"

#include "raymath.h"

#include <algorithm>
#include <vector>

static float g_scroll = 0.0f;       // list scroll offset in pixels

// ----------------------------------------------------------------------------
//  Detail panel (selected drone)
// ----------------------------------------------------------------------------
static void DrawDetailPanel() {
    const Rectangle panel = { 860, 130, 410, 580 };
    DrawRectangleRounded(panel, 0.02f, 8, COL_PANEL);
    DrawRectangleLinesEx(panel, 1, COL_BORDER);
    if (g_fleet.empty()) return;

    Drone& d = g_fleet[g_app.selected];
    float x = panel.x + 14;
    float y = panel.y + 12;

    DrawText(TextFormat("DRONE #%02d", d.id), (int)x, (int)y, 24, COL_TEXT);
    const char* st = d.active ? "ACTIVE" : "INACTIVE";
    int sw = MeasureText(st, 16);
    DrawRectangleRounded({ panel.x + panel.width - sw - 34, y + 2, (float)sw + 20, 24 }, 0.4f, 6,
                         d.active ? Fade(COL_GOOD, 0.25f) : Fade(COL_GREY, 0.35f));
    DrawText(st, (int)(panel.x + panel.width - sw - 24), (int)y + 6, 16, d.active ? COL_GOOD : COL_DIM);

    y = panel.y + 52;
    const float STEP = 25.0f;

    Field("Make / Model", TextFormat("%s %s", d.make.c_str(), d.model.c_str()), x, y); y += STEP;
    Field("Manufacturer", d.manufacturer.c_str(), x, y);                              y += STEP;
    Field("Load capacity", TextFormat("%.1f kg", d.loadKg), x, y);                    y += STEP;
    Field("Age / lifetime", TextFormat("%.1f / %.0f months", d.ageMonths, d.lifetimeMonths), x, y); y += STEP;

    DrawText("Battery", (int)x, (int)y, 16, COL_DIM);
    DrawBatteryBar({ x + 126, y + 2, 130, 14 }, d.battery, false);
    DrawText(TextFormat("%.0f%%", d.battery), (int)x + 266, (int)y, 16, BatteryColor(d.battery));
    y += STEP;

    // Condition shown as a bar + percentage, matching the battery row layout
    DrawText("Condition", (int)x, (int)y, 16, COL_DIM);
    DrawConditionBar({ x + 126, y + 2, 130, 14 }, d, false);
    DrawText(TextFormat("%.0f%%", d.condition), (int)x + 266, (int)y, 16, DamageColor(d.damage));
    y += STEP;

    Field("Software", TextFormat("v%s%s", d.sw.c_str(), SwOutdated(d) ? "  (update available)" : "  (latest)"),
          x, y, SwOutdated(d) ? COL_WARN : COL_TEXT);                                 y += STEP;
    Field("Automation", AutoStateText(d), x, y);                                      y += STEP;
    Field("Priority", PriorityText(d.priority), x, y);                                y += STEP;
    Field("Position", TextFormat("(%.1f, %.1f) km", d.pos.x, d.pos.y), x, y);         y += STEP;

    if (d.active) {
        Field("Start address", g_locations[d.startLoc].name.c_str(), x, y);           y += STEP;
        Field(d.returning ? "Returning to" : "Destination", g_locations[d.endLoc].name.c_str(), x, y); y += STEP;
        Field("Speed", TextFormat("%.0f km/h", d.speed), x, y);                       y += STEP;
        float rem = RemainingKm(d);
        Field("DTD", TextFormat("%.1f km  (ETA %.0f min)", rem, rem / d.cruise * 60.0f), x, y); y += STEP;
        Field("Path", TextFormat("%d / %d waypoints", std::max(0, d.nextWp - 1), (int)d.path.size() - 1), x, y);
    } else {
        Field("Parked at", d.locIndex >= 0 ? g_locations[d.locIndex].name.c_str() : "-", x, y); y += STEP;
        Field("Speed", "0 km/h", x, y);
    }

    // Action buttons
    float by = panel.y + panel.height - 118;
    if (!d.active) {
        const char* why = DispatchBlockReason(d);
        if (why[0]) DrawText(why, (int)x, (int)by - 22, 14, COL_WARN);
    }
    bool needsService = !d.active && (d.damage != DMG_NONE || SwOutdated(d));

    if (Button({ x, by, 185, 36 }, "Dispatch", CanDispatch(d)))       Dispatch(d);
    if (Button({ x + 197, by, 185, 36 }, "Recall", d.active && !d.returning)) Recall(d);
    if (Button({ x, by + 46, 185, 36 }, "Repair / Update", needsService))     Service(d);
    if (Button({ x + 197, by + 46, 185, 36 }, "View on Map"))                  g_app.page = PAGE_MAP;
}

// ----------------------------------------------------------------------------
//  Dashboard page
// ----------------------------------------------------------------------------
void DrawDashboard() {
    Vector2 m = GetMousePosition();

    // ---- summary cards ----
    int total = (int)g_fleet.size(), active = 0, attention = 0;
    for (const Drone& d : g_fleet) {
        if (d.active) active++;
        if (d.battery < 20.0f || d.damage != DMG_NONE || SwOutdated(d)) attention++;
    }
    const char* labels[4] = { "TOTAL DRONES", "ACTIVE", "INACTIVE", "NEED ATTENTION" };
    int vals[4] = { total, active, total - active, attention };
    Color cols[4] = { COL_TEXT, COL_GOOD, COL_DIM, attention ? COL_WARN : COL_GOOD };
    for (int i = 0; i < 4; i++) {
        Rectangle r = { 10.0f + i * 210.0f, 62, 200, 56 };
        DrawRectangleRounded(r, 0.2f, 8, COL_PANEL);
        DrawRectangleLinesEx(r, 1, COL_BORDER);
        DrawText(labels[i], (int)r.x + 12, (int)r.y + 8, 14, COL_DIM);
        DrawText(TextFormat("%d", vals[i]), (int)r.x + 12, (int)r.y + 26, 26, cols[i]);
    }
    {
        Rectangle r = { 860, 62, 410, 56 };
        DrawRectangleRounded(r, 0.2f, 8, COL_PANEL);
        DrawRectangleLinesEx(r, 1, COL_BORDER);
        DrawText("SIMULATION", (int)r.x + 12, (int)r.y + 8, 14, COL_DIM);
        DrawText(g_app.paused ? "PAUSED" : TextFormat("Running at x%d  (1 s = %d min)", g_app.simMult,
                 (int)(BASE_TIME_SCALE * g_app.simMult / 60.0f)),
                 (int)r.x + 12, (int)r.y + 28, 18, g_app.paused ? COL_WARN : COL_TEXT);
    }

    // ---- drone list ----
    const Rectangle listR = { 10, 130, 840, 580 };
    const Rectangle rowsR = { 10, 160, 840, 550 };
    const float ROW_H = 34.0f;
    DrawRectangleRounded(listR, 0.02f, 8, COL_PANEL);
    DrawRectangleLinesEx(listR, 1, COL_BORDER);

    const float cx = listR.x;
    DrawText("ID",         (int)cx + 14,  138, 14, COL_DIM);
    DrawText("DRONE",      (int)cx + 60,  138, 14, COL_DIM);
    DrawText("STATUS",     (int)cx + 240, 138, 14, COL_DIM);
    DrawText("BATTERY",    (int)cx + 330, 138, 14, COL_DIM);
    DrawText("DTD",        (int)cx + 470, 138, 14, COL_DIM);
    DrawText("SPEED",      (int)cx + 555, 138, 14, COL_DIM);
    DrawText("SOFTWARE",   (int)cx + 640, 138, 14, COL_DIM);
    DrawText("CONDITION",  (int)cx + 730, 138, 14, COL_DIM);

    // Active drones first, then inactive (greyed out)
    std::vector<int> order;
    for (int i = 0; i < (int)g_fleet.size(); i++) order.push_back(i);
    std::stable_sort(order.begin(), order.end(), [](int a, int b) {
        if (g_fleet[a].active != g_fleet[b].active) return g_fleet[a].active;
        return g_fleet[a].id < g_fleet[b].id;
    });

    float maxScroll = std::max(0.0f, order.size() * ROW_H - rowsR.height);
    if (CheckCollisionPointRec(m, rowsR)) g_scroll -= GetMouseWheelMove() * 40.0f;
    g_scroll = Clamp(g_scroll, 0.0f, maxScroll);

    BeginScissorMode((int)rowsR.x, (int)rowsR.y, (int)rowsR.width, (int)rowsR.height);
    for (int k = 0; k < (int)order.size(); k++) {
        int idx = order[k];
        const Drone& d = g_fleet[idx];
        float y = rowsR.y + k * ROW_H - g_scroll;
        if (y + ROW_H < rowsR.y || y > rowsR.y + rowsR.height) continue;

        Rectangle row = { listR.x + 4, y, listR.width - 8, ROW_H - 2 };
        bool hover = CheckCollisionPointRec(m, row) && CheckCollisionPointRec(m, rowsR);
        Color bg = (idx == g_app.selected) ? COL_SEL : (hover ? COL_HOVER : ((k % 2) ? COL_PANEL_ALT : COL_PANEL));
        DrawRectangleRec(row, bg);

        Color tc = d.active ? COL_TEXT : COL_GREY;
        int ty = (int)(y + 9);

        DrawText(TextFormat("#%02d", d.id), (int)cx + 14, ty, 16, tc);
        DrawText(TextFormat("%s %s", d.make.c_str(), d.model.c_str()), (int)cx + 60, ty, 16, tc);

        DrawCircle((int)cx + 246, ty + 8, 5, d.active ? COL_GOOD : COL_GREY);
        DrawText(d.active ? "ACTIVE" : "INACTIVE", (int)cx + 258, ty, 16,
                 d.active ? COL_GOOD : COL_GREY);

        // Battery bar + %
        DrawBatteryBar({ cx + 330, (float)ty + 2, 60, 12 }, d.battery, !d.active);
        DrawText(TextFormat("%.0f%%", d.battery), (int)cx + 396, ty, 16,
                 d.active ? BatteryColor(d.battery) : COL_GREY);

        // DTD + speed
        DrawText(d.active ? TextFormat("%.1f km", RemainingKm(d)) : "--",
                 (int)cx + 470, ty, 16, tc);
        DrawText(TextFormat("%.0f km/h", d.speed), (int)cx + 555, ty, 16, tc);

        // Software
        Color swc = SwOutdated(d) ? (d.active ? COL_WARN : Fade(COL_WARN, 0.5f)) : tc;
        DrawText(TextFormat("v%s%s", d.sw.c_str(), SwOutdated(d) ? " !" : ""),
                 (int)cx + 640, ty, 16, swc);

        // Condition bar + % (mirrors the battery column).  Fill uses the
        // continuous condition so it visibly ticks down during flight.
        DrawConditionBar({ cx + 730, (float)ty + 2, 50, 12 }, d, !d.active);
        DrawText(TextFormat("%.0f%%", d.condition), (int)cx + 786, ty, 16,
                 d.active ? DamageColor(d.damage) : Fade(DamageColor(d.damage), 0.5f));

        if (hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) g_app.selected = idx;
    }
    EndScissorMode();

    DrawDetailPanel();
}