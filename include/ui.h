// ui.h - shared GUI state, colours, helpers and widgets
#pragma once

#include "raylib.h"
#include "fleet.h"

// ----------------------------------------------------------------------------
//  Window / app state
// ----------------------------------------------------------------------------
constexpr int SCREEN_W = 1280;
constexpr int SCREEN_H = 720;

enum Page { PAGE_DASHBOARD = 0, PAGE_MAP };

struct AppState {
    Page page     = PAGE_DASHBOARD;
    int  selected = 0;          // index into g_fleet
    bool paused   = false;
    int  simMult  = 1;          // simulation speed multiplier (1, 2, 4)
    bool automation = true;     // automation on/off toggle
};
extern AppState g_app;

// ----------------------------------------------------------------------------
//  Colours
// ----------------------------------------------------------------------------
static const Color COL_BG         = { 14,  18,  26, 255 };
static const Color COL_PANEL      = { 26,  32,  44, 255 };
static const Color COL_PANEL_ALT  = { 31,  38,  52, 255 };
static const Color COL_SEL        = { 36,  70, 120, 255 };
static const Color COL_HOVER      = { 40,  48,  66, 255 };
static const Color COL_TEXT       = { 230, 235, 245, 255 };
static const Color COL_DIM        = { 140, 150, 168, 255 };
static const Color COL_GREY       = { 95,  102, 115, 255 };
static const Color COL_ACCENT     = { 64,  180, 255, 255 };
static const Color COL_GOOD       = { 72,  200, 120, 255 };
static const Color COL_WARN       = { 245, 180, 50,  255 };
static const Color COL_BAD        = { 235, 85,  85,  255 };
static const Color COL_BTN        = { 55,  100, 190, 255 };
static const Color COL_BTN_HOVER  = { 80,  130, 220, 255 };
static const Color COL_BTN_OFF    = { 50,  55,  66,  255 };
static const Color COL_TAB_ON     = { 36,  70, 120, 255 };
static const Color COL_TAB_OFF    = { 34,  40,  52, 255 };
static const Color COL_TAB_HOVER  = { 50,  58,  74, 255 };
static const Color COL_MAP_BG     = { 10,  14,  20, 255 };
static const Color COL_WORLD      = { 20,  31,  44, 255 };
static const Color COL_GRID       = { 32,  46,  64, 255 };
static const Color COL_GRID_MAJOR = { 46,  64,  88, 255 };
static const Color COL_LOCATION   = { 110, 130, 165, 255 };
static const Color COL_BATT_BG    = { 40,  46,  58, 255 };
static const Color COL_BORDER     = { 90,  98,  112, 255 };

// ----------------------------------------------------------------------------
//  Formatting helpers
// ----------------------------------------------------------------------------
Color       BatteryColor(float pct);
Color       DamageColor(Damage d);
const char* DamageText(Damage d);
const char* PriorityText(int p);

// ----------------------------------------------------------------------------
//  Widgets
// ----------------------------------------------------------------------------
bool Button(Rectangle r, const char* text, bool enabled = true);
bool TabButton(Rectangle r, const char* text, bool selected);
void DrawBatteryBar(Rectangle r, float pct, bool dim);
void Field(const char* label, const char* value, float x, float y, Color vc = COL_TEXT);

void DrawHeader();          // title bar: page tabs, pause, sim speed, FPS