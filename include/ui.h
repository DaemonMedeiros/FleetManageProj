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
//  Colours - "Matrix terminal" palette (phosphor green on near-black)
// ----------------------------------------------------------------------------
// Backgrounds
static const Color COL_BG         = {   2,   6,   4, 255 };
static const Color COL_PANEL      = {   5,  16,   9, 255 };
static const Color COL_PANEL_ALT  = {   8,  22,  12, 255 };
static const Color COL_SEL        = {   0,  55,  22, 255 };
static const Color COL_HOVER      = {  12,  38,  20, 255 };

// Text
static const Color COL_TEXT       = { 100, 255, 140, 255 };
static const Color COL_DIM        = {  60, 160,  85, 255 };
static const Color COL_GREY       = {  35,  85,  50, 255 };

// Accents and status
static const Color COL_ACCENT     = {   0, 255,  65, 255 };
static const Color COL_GOOD       = {   0, 230,  80, 255 };
static const Color COL_WARN       = { 235, 180,  40, 255 };
static const Color COL_BAD        = { 235,  70,  70, 255 };

// Buttons
static const Color COL_BTN        = {   0,  50,  22, 255 };
static const Color COL_BTN_HOVER  = {   0,  95,  42, 255 };
static const Color COL_BTN_OFF    = {  14,  24,  17, 255 };

// Tabs
static const Color COL_TAB_ON     = {   0,  60,  28, 255 };
static const Color COL_TAB_OFF    = {   5,  14,   8, 255 };
static const Color COL_TAB_HOVER  = {  10,  30,  16, 255 };

// Map
static const Color COL_MAP_BG     = {   0,   3,   1, 255 };
static const Color COL_WORLD      = {   3,  14,   7, 255 };
static const Color COL_GRID       = {   0,  35,  15, 255 };
static const Color COL_GRID_MAJOR = {   0,  65,  25, 255 };
static const Color COL_LOCATION   = {   0, 150,  55, 255 };

// Misc
static const Color COL_BATT_BG    = {   8,  22,  14, 255 };
static const Color COL_BORDER     = {   0, 100,  40, 255 };

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
void DrawConditionBar(Rectangle r, const Drone& d, bool dim);
void Field(const char* label, const char* value, float x, float y, Color vc = COL_TEXT);

void DrawHeader();          // title bar: page tabs, pause, sim speed, FPS