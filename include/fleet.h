// fleet.h - drone data model, fleet "database", and status/simulation logic
#pragma once

#include "raylib.h"

#include <string>
#include <vector>

// ----------------------------------------------------------------------------
//  Simulation constants
// ----------------------------------------------------------------------------
constexpr float WORLD_KM   = 50.0f;               // map is 50 km x 50 km
constexpr float PX_PER_KM  = 20.0f;               // world units (pixels) per km
constexpr float WORLD_PX   = WORLD_KM * PX_PER_KM;

constexpr float BASE_TIME_SCALE      = 120.0f;    // simulated seconds per real second
constexpr float DRAIN_PER_HOUR       = 20.0f;     // battery % lost per flight hour
constexpr float CHARGE_PER_HOUR      = 60.0f;     // battery % gained per hour parked
constexpr float MIN_DISPATCH_BATTERY = 50.0f;     // minimum battery to start a mission
constexpr const char* LATEST_SW      = "2.4.0";   // newest software version

// ----------------------------------------------------------------------------
//  Data model
// ----------------------------------------------------------------------------
enum Damage { DMG_NONE = 0, DMG_MINOR, DMG_MAJOR };

enum AutoState { AUTO_IDLE = 0, AUTO_DIVERTING, AUTO_SERVICING, AUTO_CHARGING };

struct SavedMission {        // route interrupted by automation, to resume later
    bool valid    = false;
    int  startLoc = -1;
    int  endLoc   = -1;
};

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

    // --- automation (owned by automation.cpp) ---
    AutoState    autoState       = AUTO_IDLE;
    float        autoTimer       = 0.0f;    // simulated seconds left for servicing/charging
    float        autoChargeStart = 100.0f;  // battery at the start of an automated charge
    float        idleTimer       = 0.0f;    // simulated seconds spent idle (for auto-dispatch)
    SavedMission savedMission;
};

// ----------------------------------------------------------------------------
//  Fleet state (defined in fleet.cpp)
// ----------------------------------------------------------------------------
extern std::vector<Location> g_locations;
extern std::vector<Drone>    g_fleet;

// ----------------------------------------------------------------------------
//  Fleet API
// ----------------------------------------------------------------------------
void    InitFleet();                          // create locations + drones
void    UpdateFleet(float simDt);             // advance simulation by simDt simulated seconds

Vector2 ParkPos(const Drone& d, int loc);     // where a parked drone sits near a location

bool        SwOutdated(const Drone& d);
bool        CanDispatch(const Drone& d);
const char* DispatchBlockReason(const Drone& d);
void        Service(Drone& d);                // repair + update software