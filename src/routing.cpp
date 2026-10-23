// routing.cpp - position tracking and route / path handling
#include "routing.h"

#include "ui.h"        // g_app.settings

#include "raymath.h"

float RemainingKm(const Drone& d) {
    if (!d.active || d.nextWp >= (int)d.path.size()) return 0.0f;
    float r = Vector2Distance(d.pos, d.path[d.nextWp]);
    for (size_t i = d.nextWp + 1; i < d.path.size(); i++)
        r += Vector2Distance(d.path[i - 1], d.path[i]);
    return r;
}

void MoveAlong(Drone& d, float km) {
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

// Dispatch from d.locIndex to a chosen destination.
// Precondition: d.locIndex >= 0 and end != d.locIndex.
//
// Adds 1..3 random jitter waypoints to the flight path.  This is only used
// for "start a new mission from a parked drone at a named location" - it is
// NOT suitable for a return-to-base leg, because the jitter waypoints can
// fall past the destination and make it look like the drone overshot.
// Arrive() below deliberately does not use this function for the return.
void DispatchTo(Drone& d, int end) {
    int start = d.locIndex;

    d.startLoc = start;
    d.endLoc = end;
    d.lastDestLoc = end;
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
        float jitter = GetRandomValue(-60, 60) / 10.0f;
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

// Manual "Dispatch" button: pick a fresh random destination.
void Dispatch(Drone& d) {
    int n = (int)g_locations.size();
    int end;
    do { end = GetRandomValue(0, n - 1); } while (end == d.locIndex);
    DispatchTo(d, end);
}

// Active drone: fly in a straight line to a location and land.
// No jitter waypoints - used for emergency diversions and for the return leg.
void DivertTo(Drone& d, int loc) {
    if (!d.active || loc < 0) return;
    d.endLoc = loc;
    d.returning = true;
    d.path.clear();
    d.path.push_back(d.pos);
    d.path.push_back(g_locations[loc].pos);
    d.nextWp = 1;
}

void Recall(Drone& d) {
    if (!d.active || d.startLoc < 0) return;
    DivertTo(d, d.startLoc);
}

int NearestLocation(Vector2 posKm) {
    int best = -1;
    float bestDist = 0.0f;
    for (int i = 0; i < (int)g_locations.size(); i++) {
        float dist = Vector2Distance(posKm, g_locations[i].pos);
        if (best < 0 || dist < bestDist) { best = i; bestDist = dist; }
    }
    return best;
}

void ApplyFasterRoute(Drone& d) {
    if (!d.active || d.endLoc < 0) return;
    d.path.clear();
    d.path.push_back(d.pos);
    d.path.push_back(g_locations[d.endLoc].pos);
    d.nextWp = 1;
}

// Arrive - park at the destination.
//
// The return-to-facility leg is gated on BOTH the master switch and the
// autoReturn setting.  With the master switch off, a drone that is already
// en route when the switch is flipped will still finish its current mission
// but will then park at the destination - it will NOT auto-return, because
// the master switch means "no automation of any kind".
void Arrive(Drone& d) {
    const int landedAt      = d.endLoc;
    const int launchAt      = d.startLoc;
    const bool wasReturning = d.returning;

    const bool doReturn = g_app.settings.enabled
                       && g_app.settings.autoReturn
                       && !wasReturning
                       && landedAt >= 0
                       && landedAt != launchAt
                       && d.damage != DMG_MAJOR;

    if (doReturn) {
        // Return leg: start the return from the exact landing point, not from
        // a scattered parking slot, so the path is a clean straight line back
        // to the facility.  The drone stays active so UpdateFleet continues
        // draining battery and applying wear while it flies home.
        const int FACILITY = 0;

        d.startLoc  = FACILITY;                        // for display: "returning to facility"
        d.endLoc    = FACILITY;
        d.returning = true;
        d.active    = true;
        d.speed     = d.cruise;
        d.locIndex  = -1;

        d.path.clear();
        d.path.push_back(g_locations[landedAt].pos);   // exact landing point
        d.path.push_back(g_locations[FACILITY].pos);   // exact facility point
        d.nextWp = 1;

        d.history.clear();
        d.history.push_back(g_locations[landedAt].pos);
        d.histTimer = 0.0f;
        return;
    }

    // No return: park at the destination in a scattered slot.
    d.active    = false;
    d.speed     = 0.0f;
    d.locIndex  = landedAt;
    d.pos       = landedAt >= 0 ? ParkPos(d, landedAt) : d.pos;
    d.path.clear();
    d.nextWp    = 0;
    d.returning = false;
}