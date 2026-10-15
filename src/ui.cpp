// ui.cpp - shared GUI helpers, widgets and the header bar
#include "ui.h"

AppState g_app;

// ----------------------------------------------------------------------------
//  Formatting helpers
// ----------------------------------------------------------------------------
Color BatteryColor(float pct) {
    if (pct > 50.0f) return COL_GOOD;
    if (pct > 20.0f) return COL_WARN;
    return COL_BAD;
}

const char* DamageText(Damage d) {
    return d == DMG_NONE ? "OK" : (d == DMG_MINOR ? "Minor damage" : "Major damage");
}

Color DamageColor(Damage d) {
    return d == DMG_NONE ? COL_GOOD : (d == DMG_MINOR ? COL_WARN : COL_BAD);
}

const char* PriorityText(int p) { return p == 1 ? "High" : (p == 2 ? "Normal" : "Low"); }

// ----------------------------------------------------------------------------
//  Widgets
// ----------------------------------------------------------------------------
bool Button(Rectangle r, const char* text, bool enabled) {
    bool hover = enabled && CheckCollisionPointRec(GetMousePosition(), r);
    Color bg = !enabled ? COL_BTN_OFF : (hover ? COL_BTN_HOVER : COL_BTN);
    DrawRectangleRounded(r, 0.25f, 8, bg);
    int tw = MeasureText(text, 16);
    DrawText(text, (int)(r.x + (r.width - tw) / 2), (int)(r.y + (r.height - 16) / 2), 16,
             enabled ? COL_TEXT : COL_GREY);
    return hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

bool TabButton(Rectangle r, const char* text, bool selected) {
    bool hover = CheckCollisionPointRec(GetMousePosition(), r);
    Color bg = selected ? COL_TAB_ON : (hover ? COL_TAB_HOVER : COL_TAB_OFF);
    DrawRectangleRounded(r, 0.3f, 8, bg);
    if (selected) DrawRectangle((int)r.x + 10, (int)(r.y + r.height - 3), (int)r.width - 20, 3, COL_ACCENT);
    int tw = MeasureText(text, 18);
    DrawText(text, (int)(r.x + (r.width - tw) / 2), (int)(r.y + (r.height - 18) / 2), 18,
             selected ? COL_TEXT : COL_DIM);
    return hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

void DrawBatteryBar(Rectangle r, float pct, bool dim) {
    DrawRectangleRec(r, COL_BATT_BG);
    Color c = BatteryColor(pct);
    if (dim) c = Fade(c, 0.4f);
    DrawRectangle((int)r.x, (int)r.y, (int)(r.width * pct / 100.0f), (int)r.height, c);
    DrawRectangleLinesEx(r, 1, COL_BORDER);
}

void Field(const char* label, const char* value, float x, float y, Color vc) {
    DrawText(label, (int)x, (int)y, 16, COL_DIM);
    DrawText(value, (int)x + 126, (int)y, 16, vc);
}

// ----------------------------------------------------------------------------
//  Header
// ----------------------------------------------------------------------------
void DrawHeader() {
    DrawRectangle(0, 0, SCREEN_W, 50, COL_PANEL);
    DrawRectangle(0, 49, SCREEN_W, 1, COL_BORDER);
    DrawText("DRONE FLEET MANAGER", 16, 14, 22, COL_TEXT);

    if (TabButton({ 330, 8, 140, 34 }, "Dashboard", g_app.page == PAGE_DASHBOARD)) g_app.page = PAGE_DASHBOARD;
    if (TabButton({ 480, 8, 140, 34 }, "Map", g_app.page == PAGE_MAP))             g_app.page = PAGE_MAP;

    if (Button({ (float)SCREEN_W - 330, 8, 100, 34 }, g_app.paused ? "Resume" : "Pause")) g_app.paused = !g_app.paused;
    if (Button({ (float)SCREEN_W - 220, 8, 100, 34 }, TextFormat("Sim x%d", g_app.simMult))) {
        g_app.simMult = (g_app.simMult == 1) ? 2 : (g_app.simMult == 2 ? 4 : 1);
    }
    DrawText(TextFormat("%d FPS", GetFPS()), SCREEN_W - 100, 18, 16, COL_DIM);
}
