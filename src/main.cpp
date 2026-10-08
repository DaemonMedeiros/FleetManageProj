// ============================================================================
//  Drone Fleet Manager  -  single file C++ / Raylib application
//
//  Page 1 (Dashboard): active / inactive drone list + detail panel
//  Page 2 (Map)      : 2D map showing locations, drone positions and the
//                      current path (plus travelled history) of the selected drone
//
//  Build examples:
//    Linux  : g++ main.cpp -o fleet -std=c++17 -lraylib -lm -ldl -lpthread -lGL -lrt -lX11
//    macOS  : g++ main.cpp -o fleet -std=c++17 -lraylib -framework IOKit -framework Cocoa -framework OpenGL
//    Windows: g++ main.cpp -o fleet.exe -std=c++17 -lraylib -lopengl32 -lgdi32 -lwinmm
//
//  Controls:
//    TAB / 1 / 2      switch page          SPACE  pause / resume simulation
//    Click a row      select drone         Map: drag = pan, wheel = zoom, click drone = select
// ============================================================================

#include "raylib.h"
#include "raymath.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <string>
#include <vector>

// ----------------------------------------------------------------------------
//  Constants
// ----------------------------------------------------------------------------
static const int   SCREEN_W   = 1280;
static const int   SCREEN_H   = 720;

static const float WORLD_KM   = 50.0f;               // map is 50 km x 50 km
static const float PX_PER_KM  = 20.0f;               // world units (pixels) per km
static const float WORLD_PX   = WORLD_KM * PX_PER_KM;

static const float BASE_TIME_SCALE      = 120.0f;    // simulated seconds per real second
static const float DRAIN_PER_HOUR       = 20.0f;     // battery % lost per flight hour
static const float CHARGE_PER_HOUR      = 60.0f;     // battery % gained per hour parked
static const float MIN_DISPATCH_BATTERY = 50.0f;     // minimum battery to start a mission
static const char* LATEST_SW            = "2.4.0";   // newest software version

// Colours
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
static const Color COL_TAB_ON     = { 36,  70,  120, 255 };
static const Color COL_TAB_OFF    = { 34,  40,  52,  255 };
static const Color COL_TAB_HOVER  = { 50,  58,  74,  255 };
static const Color COL_MAP_BG     = { 10,  14,  20,  255 };
static const Color COL_WORLD      = { 20,  31,  44,  255 };
static const Color COL_GRID       = { 32,  46,  64,  255 };
static const Color COL_GRID_MAJOR = { 46,  64,  88,  255 };
static const Color COL_LOCATION   = { 110, 130, 165, 255 };
static const Color COL_BATT_BG    = { 40,  46,  58,  255 };
static const Color COL_BORDER     = { 90,  98,  112, 255 };

// ----------------------------------------------------------------------------
//  Data model
// ----------------------------------------------------------------------------
enum Damage { DMG_NONE = 0, DMG_MINOR, DMG_MAJOR };
enum Page   { PAGE_DASHBOARD = 0, PAGE_MAP };

struct Location {            // named addresses drones fly between
    std::string name;
    Vector2     pos;         // km
};

struct Drone {
    // --- identity / type ---
    int         id = 0;                  // numeric ID
    std::string make;                    // Make
    std::string model;                   // Model
    std::string manufacturer;            // Manufacturer
    float       loadKg = 0.0f;           // load carry size

    // --- status ---
    bool        active = false;          // active / inactive
    float       ageMonths = 0.0f;        // age
    float       lifetimeMonths = 60.0f;  // expected lifetime
    float       battery = 100.0f;        // %
    std::string sw = "2.4.0";            // software version
    Damage      damage = DMG_NONE;
    int         priority = 2;            // 1 = high, 2 = normal, 3 = low

    // --- position / motion ---
    Vector2     pos = { 0, 0 };          // current position vector (km)
    float       cruise = 40.0f;          // cruise speed km/h
    float       speed = 0.0f;            // current speed km/h
    int         locIndex = -1;           // where it is parked (-1 = in flight)

    // --- path ---
    int                  startLoc = -1;  // start address
    int                  endLoc = -1;    // end address
    bool                 returning = false;
    std::vector<Vector2> path;           // list of position vectors (route data)
    int                  nextWp = 0;     // index of next waypoint
    std::vector<Vector2> history;        // travelled trail
    float                histTimer = 0.0f;
};

static std::vector<Location> g_locations;
static std::vector<Drone>    g_fleet;

// UI / app state
static Page   g_page      = PAGE_DASHBOARD;
static int    g_selected  = 0;
static bool   g_paused    = false;
static int    g_simMult   = 1;
static float  g_scroll    = 0.0f;
static Camera2D g_cam     = { 0 };

static const Rectangle MAP_VIEW = { 270, 60, 1000, 650 };
static const Rectangle MAP_OVERLAY = { 280, 70, 350, 160 };

// ----------------------------------------------------------------------------
//  Helpers
// ----------------------------------------------------------------------------
static Vector2 ToPx(Vector2 km) { return Vector2Scale(km, PX_PER_KM); }

static bool SwOutdated(const Drone& d) { return d.sw != LATEST_SW; }

static const char* PriorityText(int p) { return p == 1 ? "High" : (p == 2 ? "Normal" : "Low"); }

static Color BatteryColor(float pct) {
    if (pct > 50.0f) return COL_GOOD;
    if (pct > 20.0f) return COL_WARN;
    return COL_BAD;
}

static const char* DamageText(Damage d) {
    return d == DMG_NONE ? "OK" : (d == DMG_MINOR ? "Minor damage" : "Major damage");
}

static Color DamageColor(Damage d) {
    return d == DMG_NONE ? COL_GOOD : (d == DMG_MINOR ? COL_WARN : COL_BAD);
}

static Vector2 ParkPos(const Drone& d, int loc) {
    // Parked drones are spread slightly around their location so they don't overlap
    float a = d.id * 0.9f;
    return Vector2Add(g_locations[loc].pos, Vector2{ cosf(a) * 1.1f, sinf(a) * 1.1f });
}

static float RemainingKm(const Drone& d) {
    if (!d.active || d.nextWp >= (int)d.path.size()) return 0.0f;
    float r = Vector2Distance(d.pos, d.path[d.nextWp]);
    for (size_t i = d.nextWp + 1; i < d.path.size(); i++)
        r += Vector2Distance(d.path[i - 1], d.path[i]);
    return r;
}

// Advance a drone along its path by 'km' kilometres.
static void MoveAlong(Drone& d, float km) {
    while (km > 0.0f && d.nextWp < (int)d.path.size()) {
        Vector2 t = d.path[d.nextWp];
        float dist = Vector2Distance(d.pos, t);
        if (dist <= km) {
            d.pos = t;
            km -= dist;
            d.nextWp++;
            d.history.push_back(t);
        } else {
            Vector2 dir = Vector2Normalize(Vector2Subtract(t, d.pos));
            d.pos = Vector2Add(d.pos, Vector2Scale(dir, km));
            km = 0.0f;
        }
    }
}

// ----------------------------------------------------------------------------
//  Fleet actions
// ----------------------------------------------------------------------------
static bool CanDispatch(const Drone& d) {
    return !d.active && d.locIndex >= 0 && d.damage != DMG_MAJOR && d.battery >= MIN_DISPATCH_BATTERY;
}

static const char* DispatchBlockReason(const Drone& d) {
    if (d.active)                          return "Drone is already in flight";
    if (d.damage == DMG_MAJOR)             return "Major damage - service required";
    if (d.battery < MIN_DISPATCH_BATTERY)  return "Battery below 50% - charging";
    return "";
}

// Create a new random mission from the drone's current location.
static void Dispatch(Drone& d) {
    int n = (int)g_locations.size();
    int start = d.locIndex;
    int end;
    do { end = GetRandomValue(0, n - 1); } while (end == start);

    d.startLoc = start;
    d.endLoc = end;
    d.returning = false;

    Vector2 a = d.pos;
    Vector2 b = g_locations[end].pos;
    Vector2 dir = Vector2Subtract(b, a);
    float len = Vector2Length(dir);
    Vector2 perp = { -dir.y / len, dir.x / len };

    d.path.clear();
    d.path.push_back(a);
    int wps = GetRandomValue(1, 3);
    for (int i = 1; i <= wps; i++) {
        float t = (float)i / (float)(wps + 1);
        float jitter = GetRandomValue(-60, 60) / 10.0f;               // +-6 km sideways
        Vector2 p = Vector2Add(Vector2Add(a, Vector2Scale(dir, t)), Vector2Scale(perp, jitter));
        p.x = Clamp(p.x, 1.0f, WORLD_KM - 1.0f);
        p.y = Clamp(p.y, 1.0f, WORLD_KM - 1.0f);
        d.path.push_back(p);
    }
    d.path.push_back(b);

    d.nextWp = 1;
    d.active = true;
    d.speed = d.cruise;
    d.locIndex = -1;
    d.history.clear();
    d.history.push_back(a);
    d.histTimer = 0.0f;
}

// Abort mission and fly back to the start address.
static void Recall(Drone& d) {
    if (!d.active || d.startLoc < 0) return;
    d.endLoc = d.startLoc;
    d.returning = true;
    d.path.clear();
    d.path.push_back(d.pos);
    d.path.push_back(g_locations[d.endLoc].pos);
    d.nextWp = 1;
}

// Replace the remaining route with a direct line to the destination.
static void ApplyFasterRoute(Drone& d) {
    if (!d.active || d.endLoc < 0) return;
    d.path.clear();
    d.path.push_back(d.pos);
    d.path.push_back(g_locations[d.endLoc].pos);
    d.nextWp = 1;
}

static void Service(Drone& d) {
    if (d.active) return;
    d.damage = DMG_NONE;
    d.sw = LATEST_SW;
}

static void Arrive(Drone& d) {
    d.active = false;
    d.speed = 0.0f;
    d.locIndex = d.endLoc;
    d.pos = ParkPos(d, d.locIndex);
    d.path.clear();
    d.nextWp = 0;
    d.returning = false;
}

static void UpdateFleet(float simDt) {
    for (Drone& d : g_fleet) {
        d.ageMonths += simDt / (3600.0f * 24.0f * 30.0f);

        if (d.active) {
            d.speed = d.cruise;
            MoveAlong(d, d.cruise / 3600.0f * simDt);

            d.battery -= DRAIN_PER_HOUR * simDt / 3600.0f;
            if (d.battery < 0.0f) d.battery = 0.0f;

            d.histTimer += simDt;
            if (d.histTimer >= 60.0f) {                       // sample once per simulated minute
                d.histTimer = 0.0f;
                d.history.push_back(d.pos);
                if (d.history.size() > 600) d.history.erase(d.history.begin());
            }

            if (d.nextWp >= (int)d.path.size()) Arrive(d);
        } else {
            d.speed = 0.0f;
            d.battery += CHARGE_PER_HOUR * simDt / 3600.0f;
            if (d.battery > 100.0f) d.battery = 100.0f;
        }
    }
}

static void InitFleet() {
    g_locations = {
        { "Central Depot",     { 25.0f, 25.0f } },
        { "Harbour Warehouse", {  7.0f, 41.0f } },
        { "North Hospital",    { 21.0f,  6.0f } },
        { "East Retail Park",  { 44.0f, 14.0f } },
        { "Airport Cargo",     { 42.0f, 40.0f } },
        { "Riverside Hub",     { 29.0f, 45.0f } },
        { "University Campus", {  9.0f, 15.0f } },
        { "Tech Park",         { 36.0f, 29.0f } },
    };

    struct Tpl { const char* make; const char* model; const char* mfr; float load; };
    const Tpl tpls[6] = {
        { "Skyline", "X4",       "AeroDyne",        2.0f  },
        { "Skyline", "X6",       "AeroDyne",        5.0f  },
        { "Falcon",  "F200",     "Vertex Robotics", 3.5f  },
        { "Falcon",  "F500 HD",  "Vertex Robotics", 12.0f },
        { "Orbit",   "O1 Mini",  "Nimbus Labs",     1.0f  },
        { "Orbit",   "O3",       "Nimbus Labs",     4.0f  },
    };
    const char* versions[4] = { "2.4.0", "2.4.0", "2.3.1", "2.2.0" };

    const int COUNT = 12;
    for (int i = 0; i < COUNT; i++) {
        Drone d;
        const Tpl& t = tpls[i % 6];
        d.id = i + 1;
        d.make = t.make;
        d.model = t.model;
        d.manufacturer = t.mfr;
        d.loadKg = t.load;
        d.cruise = 36.0f + (float)((i % 4) * 8);
        d.ageMonths = GetRandomValue(20, 420) / 10.0f;
        d.sw = versions[GetRandomValue(0, 3)];
        d.priority = GetRandomValue(1, 3);
        d.battery = (float)GetRandomValue(60, 100);
        d.locIndex = i % (int)g_locations.size();
        d.pos = ParkPos(d, d.locIndex);
        g_fleet.push_back(d);
    }

    // A few inactive drones with problems, to make the dashboard interesting
    g_fleet[7].damage  = DMG_MINOR;
    g_fleet[10].damage = DMG_MAJOR;
    g_fleet[9].battery = 14.0f;
    g_fleet[8].sw      = "2.2.0";
    g_fleet[11].battery = 38.0f;

    // First six drones start out mid-mission
    for (int i = 0; i < 6; i++) {
        Drone& d = g_fleet[i];
        d.damage = DMG_NONE;
        Dispatch(d);
        d.battery = (float)GetRandomValue(45, 95);
        float total = RemainingKm(d);
        MoveAlong(d, total * GetRandomValue(10, 80) / 100.0f);
    }
}

// ----------------------------------------------------------------------------
//  UI widgets
// ----------------------------------------------------------------------------
static bool Button(Rectangle r, const char* text, bool enabled = true) {
    bool hover = enabled && CheckCollisionPointRec(GetMousePosition(), r);
    Color bg = !enabled ? COL_BTN_OFF : (hover ? COL_BTN_HOVER : COL_BTN);
    DrawRectangleRounded(r, 0.25f, 8, bg);
    int tw = MeasureText(text, 16);
    DrawText(text, (int)(r.x + (r.width - tw) / 2), (int)(r.y + (r.height - 16) / 2), 16,
             enabled ? COL_TEXT : COL_GREY);
    return hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

static bool TabButton(Rectangle r, const char* text, bool selected) {
    bool hover = CheckCollisionPointRec(GetMousePosition(), r);
    Color bg = selected ? COL_TAB_ON : (hover ? COL_TAB_HOVER : COL_TAB_OFF);
    DrawRectangleRounded(r, 0.3f, 8, bg);
    if (selected) DrawRectangle((int)r.x + 10, (int)(r.y + r.height - 3), (int)r.width - 20, 3, COL_ACCENT);
    int tw = MeasureText(text, 18);
    DrawText(text, (int)(r.x + (r.width - tw) / 2), (int)(r.y + (r.height - 18) / 2), 18,
             selected ? COL_TEXT : COL_DIM);
    return hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

static void DrawBatteryBar(Rectangle r, float pct, bool dim) {
    DrawRectangleRec(r, COL_BATT_BG);
    Color c = BatteryColor(pct);
    if (dim) c = Fade(c, 0.4f);
    DrawRectangle((int)r.x, (int)r.y, (int)(r.width * pct / 100.0f), (int)r.height, c);
    DrawRectangleLinesEx(r, 1, COL_BORDER);
}

static void Field(const char* label, const char* value, float x, float y, Color vc = COL_TEXT) {
    DrawText(label, (int)x, (int)y, 16, COL_DIM);
    DrawText(value, (int)x + 126, (int)y, 16, vc);
}

// ----------------------------------------------------------------------------
//  Header
// ----------------------------------------------------------------------------
static void DrawHeader() {
    DrawRectangle(0, 0, SCREEN_W, 50, COL_PANEL);
    DrawRectangle(0, 49, SCREEN_W, 1, COL_BORDER);
    DrawText("DRONE FLEET MANAGER", 16, 14, 22, COL_TEXT);

    if (TabButton({ 330, 8, 140, 34 }, "Dashboard", g_page == PAGE_DASHBOARD)) g_page = PAGE_DASHBOARD;
    if (TabButton({ 480, 8, 140, 34 }, "Map", g_page == PAGE_MAP))             g_page = PAGE_MAP;

    if (Button({ (float)SCREEN_W - 330, 8, 100, 34 }, g_paused ? "Resume" : "Pause")) g_paused = !g_paused;
    if (Button({ (float)SCREEN_W - 220, 8, 100, 34 }, TextFormat("Sim x%d", g_simMult))) {
        g_simMult = (g_simMult == 1) ? 2 : (g_simMult == 2 ? 4 : 1);
    }
    DrawText(TextFormat("%d FPS", GetFPS()), SCREEN_W - 100, 18, 16, COL_DIM);
}

// ----------------------------------------------------------------------------
//  Dashboard page
// ----------------------------------------------------------------------------
static void DrawDetailPanel() {
    const Rectangle panel = { 860, 130, 410, 580 };
    DrawRectangleRounded(panel, 0.02f, 8, COL_PANEL);
    if (g_fleet.empty()) return;

    Drone& d = g_fleet[g_selected];
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

    Field("Software", TextFormat("v%s%s", d.sw.c_str(), SwOutdated(d) ? "  (update available)" : "  (latest)"),
          x, y, SwOutdated(d) ? COL_WARN : COL_TEXT);                                 y += STEP;
    Field("Condition", DamageText(d.damage), x, y, DamageColor(d.damage));            y += STEP;
    Field("Priority", PriorityText(d.priority), x, y);                                y += STEP;
    Field("Position", TextFormat("(%.1f, %.1f) km", d.pos.x, d.pos.y), x, y);         y += STEP;

    if (d.active) {
        Field("Start address", g_locations[d.startLoc].name.c_str(), x, y);           y += STEP;
        Field(d.returning ? "Returning to" : "Destination", g_locations[d.endLoc].name.c_str(), x, y); y += STEP;
        Field("Speed", TextFormat("%.0f km/h", d.speed), x, y);                       y += STEP;
        float rem = RemainingKm(d);
        Field("Remaining", TextFormat("%.1f km  (ETA %.0f min)", rem, rem / d.cruise * 60.0f), x, y); y += STEP;
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
    if (Button({ x + 197, by + 46, 185, 36 }, "View on Map"))                  g_page = PAGE_MAP;
}

static void DrawDashboard() {
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
        DrawText(labels[i], (int)r.x + 12, (int)r.y + 8, 14, COL_DIM);
        DrawText(TextFormat("%d", vals[i]), (int)r.x + 12, (int)r.y + 26, 26, cols[i]);
    }
    {
        Rectangle r = { 860, 62, 410, 56 };
        DrawRectangleRounded(r, 0.2f, 8, COL_PANEL);
        DrawText("SIMULATION", (int)r.x + 12, (int)r.y + 8, 14, COL_DIM);
        DrawText(g_paused ? "PAUSED" : TextFormat("Running at x%d  (1 s = %d min)", g_simMult,
                 (int)(BASE_TIME_SCALE * g_simMult / 60.0f)),
                 (int)r.x + 12, (int)r.y + 28, 18, g_paused ? COL_WARN : COL_TEXT);
    }

    // ---- drone list ----
    const Rectangle listR = { 10, 130, 840, 580 };
    const Rectangle rowsR = { 10, 160, 840, 550 };
    const float ROW_H = 34.0f;
    DrawRectangleRounded(listR, 0.02f, 8, COL_PANEL);

    const float cx = listR.x;
    DrawText("ID",        (int)cx + 14,  138, 14, COL_DIM);
    DrawText("DRONE",     (int)cx + 60,  138, 14, COL_DIM);
    DrawText("STATUS",    (int)cx + 252, 138, 14, COL_DIM);
    DrawText("BATTERY",   (int)cx + 340, 138, 14, COL_DIM);
    DrawText("DEST DIST", (int)cx + 480, 138, 14, COL_DIM);
    DrawText("SPEED",     (int)cx + 565, 138, 14, COL_DIM);
    DrawText("SOFTWARE",  (int)cx + 650, 138, 14, COL_DIM);
    DrawText("HEALTH",    (int)cx + 740, 138, 14, COL_DIM);

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
        Color bg = (idx == g_selected) ? COL_SEL : (hover ? COL_HOVER : ((k % 2) ? COL_PANEL_ALT : COL_PANEL));
        DrawRectangleRec(row, bg);

        Color tc = d.active ? COL_TEXT : COL_GREY;
        int ty = (int)(y + 9);

        DrawText(TextFormat("#%02d", d.id), (int)cx + 14, ty, 16, tc);
        DrawText(TextFormat("%s %s", d.make.c_str(), d.model.c_str()), (int)cx + 60, ty, 16, tc);

        DrawCircle((int)cx + 258, ty + 8, 5, d.active ? COL_GOOD : COL_GREY);
        DrawText(d.active ? "ACTIVE" : "INACTIVE", (int)cx + 270, ty, 16, d.active ? COL_GOOD : COL_GREY);

        DrawBatteryBar({ cx + 340, (float)ty + 2, 70, 12 }, d.battery, !d.active);
        DrawText(TextFormat("%.0f%%", d.battery), (int)cx + 418, ty, 16,
                 d.active ? BatteryColor(d.battery) : COL_GREY);

        DrawText(d.active ? TextFormat("%.1f km", RemainingKm(d)) : "--", (int)cx + 480, ty, 16, tc);
        DrawText(TextFormat("%.0f km/h", d.speed), (int)cx + 565, ty, 16, tc);

        Color swc = SwOutdated(d) ? (d.active ? COL_WARN : Fade(COL_WARN, 0.5f)) : tc;
        DrawText(TextFormat("v%s%s", d.sw.c_str(), SwOutdated(d) ? " !" : ""), (int)cx + 650, ty, 16, swc);

        Color hc = d.active ? DamageColor(d.damage) : Fade(DamageColor(d.damage), 0.5f);
        DrawText(DamageText(d.damage), (int)cx + 740, ty, 16, hc);

        if (hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) g_selected = idx;
    }
    EndScissorMode();

    DrawDetailPanel();
}

// ----------------------------------------------------------------------------
//  Map page
// ----------------------------------------------------------------------------
static void InitCamera() {
    g_cam.offset   = { MAP_VIEW.x + MAP_VIEW.width / 2, MAP_VIEW.y + MAP_VIEW.height / 2 };
    g_cam.target   = { WORLD_PX / 2, WORLD_PX / 2 };
    g_cam.rotation = 0.0f;
    g_cam.zoom     = 0.62f;
}

static void DrawMapSidebar() {
    Rectangle panel = { 10, 60, 250, 650 };
    DrawRectangleRounded(panel, 0.02f, 8, COL_PANEL);
    DrawText("FLEET", (int)panel.x + 14, (int)panel.y + 10, 16, COL_DIM);

    Vector2 m = GetMousePosition();
    for (int i = 0; i < (int)g_fleet.size(); i++) {
        const Drone& d = g_fleet[i];
        Rectangle row = { panel.x + 6, panel.y + 36 + i * 44.0f, panel.width - 12, 42 };
        bool hover = CheckCollisionPointRec(m, row);
        Color bg = (i == g_selected) ? COL_SEL : (hover ? COL_HOVER : COL_PANEL_ALT);
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

        if (hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) g_selected = i;
    }
}

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
                if (dist < best) { best = dist; g_selected = i; }
            }
        }
    }
}

static void DrawMapOverlay() {
    if (g_fleet.empty()) return;
    Drone& d = g_fleet[g_selected];

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

static void DrawMap() {
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
        if (!d.active || i == g_selected) continue;
        Vector2 prev = ToPx(d.pos);
        for (int w = d.nextWp; w < (int)d.path.size(); w++) {
            Vector2 p = ToPx(d.path[w]);
            DrawLineEx(prev, p, 1.5f / z, Fade(COL_ACCENT, 0.3f));
            prev = p;
        }
    }

    // ---- selected drone: history, current path, waypoints, start/end ----
    if (!g_fleet.empty()) {
        const Drone& d = g_fleet[g_selected];

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
        if (i == g_selected) DrawRing(p, 10.0f / z, 12.5f / z, 0, 360, 32, WHITE);
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

// ----------------------------------------------------------------------------
//  Main
// ----------------------------------------------------------------------------
int main() {
    SetConfigFlags(FLAG_MSAA_4X_HINT);
    InitWindow(SCREEN_W, SCREEN_H, "Drone Fleet Manager");
    SetTargetFPS(60);
    SetRandomSeed((unsigned int)time(NULL));

    InitFleet();
    InitCamera();

    while (!WindowShouldClose()) {
        // ---- input ----
        if (IsKeyPressed(KEY_TAB)) g_page = (g_page == PAGE_DASHBOARD) ? PAGE_MAP : PAGE_DASHBOARD;
        if (IsKeyPressed(KEY_ONE)) g_page = PAGE_DASHBOARD;
        if (IsKeyPressed(KEY_TWO)) g_page = PAGE_MAP;
        if (IsKeyPressed(KEY_SPACE)) g_paused = !g_paused;

        // ---- simulation ----
        if (!g_paused) UpdateFleet(GetFrameTime() * BASE_TIME_SCALE * (float)g_simMult);

        // ---- draw ----
        BeginDrawing();
        ClearBackground(COL_BG);
        DrawHeader();
        if (g_page == PAGE_DASHBOARD) DrawDashboard();
        else                          DrawMap();
        EndDrawing();
    }

    CloseWindow();
    return 0;
}
