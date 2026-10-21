// fleet.cpp - fleet data, initial population, and per-tick status simulation
#include "fleet.h"
#include "osm_map.h"          // LatLonToKm for real-world anchored locations
#include "routing.h"

#include "raymath.h"

#include <cmath>

std::vector<Location> g_locations;
std::vector<Drone>    g_fleet;

// ----------------------------------------------------------------------------
//  Helpers
// ----------------------------------------------------------------------------
Vector2 ParkPos(const Drone& d, int loc) {
    // Parked drones are spread slightly around their location so they don't overlap
    float a = d.id * 0.9f;
    return Vector2Add(g_locations[loc].pos, Vector2{ cosf(a) * 1.1f, sinf(a) * 1.1f });
}

bool SwOutdated(const Drone& d) { return d.sw != LATEST_SW; }

bool CanDispatch(const Drone& d) {
    return d.autoState == AUTO_IDLE && !d.active && d.locIndex >= 0
        && d.damage != DMG_MAJOR && d.battery >= MIN_DISPATCH_BATTERY;
}

const char* DispatchBlockReason(const Drone& d) {
    if (d.autoState != AUTO_IDLE)          return "Automation is servicing this drone";
    if (d.active)                          return "Drone is already in flight";
    if (d.damage == DMG_MAJOR)             return "Major damage - service required";
    if (d.battery < MIN_DISPATCH_BATTERY)  return "Battery below 50% - charging";
    return "";
}

void Service(Drone& d) {
    if (d.active) return;
    d.damage    = DMG_NONE;
    d.condition = 100.0f;
    d.sw        = LATEST_SW;
}

// Map continuous condition to the discrete damage label that UI colours and
// gating logic (CanDispatch) use.
Damage DamageFromCondition(float c) {
    if (c >= 80.0f) return DMG_NONE;
    if (c >= 40.0f) return DMG_MINOR;
    return DMG_MAJOR;
}

// ----------------------------------------------------------------------------
//  Simulation tick
// ----------------------------------------------------------------------------
void UpdateFleet(float simDt) {
    for (Drone& d : g_fleet) {
        d.ageMonths += simDt / (3600.0f * 24.0f * 30.0f);

        if (d.active) {
            d.speed = d.cruise;
            float km = d.cruise / 3600.0f * simDt;
            MoveAlong(d, km);

            d.battery -= DRAIN_PER_HOUR * simDt / 3600.0f;
            if (d.battery < 0.0f) d.battery = 0.0f;

            // Small continuous wear: every km flown erodes condition, and the
            // damage label is refreshed from it so all UI / gating sees it.
            d.condition -= DAMAGE_PER_KM * km;
            if (d.condition < 0.0f) d.condition = 0.0f;
            d.damage = DamageFromCondition(d.condition);

            d.histTimer += simDt;
            if (d.histTimer >= 60.0f) {                       // sample once per simulated minute
                d.histTimer = 0.0f;
                d.history.push_back(d.pos);
                if (d.history.size() > 600) d.history.erase(d.history.begin());
            }

            if (d.nextWp >= (int)d.path.size()) Arrive(d);
        } else {
            d.speed = 0.0f;
            // Charging of parked drones is handled entirely by automation.cpp
            // so the autoRecharge switch has a visible effect.
        }
    }
}

// ----------------------------------------------------------------------------
//  Initial fleet
// ----------------------------------------------------------------------------
void InitFleet() {
    // Locations are entered as real WGS84 coordinates and converted into the
    // 50 km simulation grid via LatLonToKm().
    //
    // Drone Facility  = 52°51'07.1"N 6°55'49.9"W  =  52.851972 N, -6.930528 W
    // Other locations are approximate landmarks around Carlow town; tweak the
    // lat/lon here if you want to move any of them.
    g_locations = {
        { "Drone Facility",     LatLonToKm(52.851972, -6.930528, WORLD_KM) },
        { "Central Depot",      LatLonToKm(52.8366,   -6.9341,   WORLD_KM) }, // town centre
        { "Harbour Warehouse",  LatLonToKm(52.8327,   -6.9490,   WORLD_KM) }, // west, Barrow
        { "North Hospital",     LatLonToKm(52.8562,   -6.9310,   WORLD_KM) }, // north of town
        { "East Retail Park",   LatLonToKm(52.8395,   -6.9040,   WORLD_KM) }, // east, R448
        { "Airport Cargo",      LatLonToKm(52.8205,   -6.9260,   WORLD_KM) }, // south
        { "Riverside Hub",      LatLonToKm(52.8280,   -6.9300,   WORLD_KM) }, // riverside
        { "University Campus",  LatLonToKm(52.8460,   -6.9380,   WORLD_KM) }, // west
        { "Tech Park",          LatLonToKm(52.8430,   -6.9450,   WORLD_KM) }, // northwest
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

    g_fleet.clear();
    const int COUNT = 12;
    const int hqIndex = 0;   // Drone Facility is the first entry above.

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
        // Every drone starts parked at the Drone Facility.
        d.locIndex = hqIndex;
        d.pos = ParkPos(d, d.locIndex);
        g_fleet.push_back(d);
    }

    // A few inactive drones with problems, to make the dashboard interesting.
    g_fleet[7].damage  = DMG_MINOR;  g_fleet[7].condition  =  55.0f;
    g_fleet[10].damage = DMG_MAJOR;  g_fleet[10].condition =  18.0f;
    g_fleet[9].battery = 14.0f;
    g_fleet[8].sw      = "2.2.0";
    g_fleet[11].battery = 38.0f;

    // First six drones start out mid-mission from the Drone Facility.
    // Seed a remembered destination so that with randomizeDestination OFF
    // their future auto-launches have something to repeat.
    for (int i = 0; i < 6; i++) {
        Drone& d = g_fleet[i];
        d.damage = DMG_NONE;
        d.condition = 100.0f;
        Dispatch(d);                                // records lastDestLoc
        d.battery = (float)GetRandomValue(45, 95);
        float total = RemainingKm(d);
        MoveAlong(d, total * GetRandomValue(10, 80) / 100.0f);
    }

    // Give the still-parked drones a lastDestLoc too, so the "remember the
    // previous destination" mode has something to replay the first time.
    for (int i = 6; i < COUNT; i++) {
        Drone& d = g_fleet[i];
        int end;
        do { end = GetRandomValue(1, (int)g_locations.size() - 1); } while (end == hqIndex);
        d.lastDestLoc = end;
    }
}