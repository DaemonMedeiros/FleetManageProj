// automation.cpp - automatic repair, recharge and mission resume for drones
#include "automation.h"

#include "fleet.h"
#include "routing.h"

#include "raylib.h"

static bool NeedsService(const Drone& d) { return d.damage != DMG_NONE || SwOutdated(d); }

// True if the drone cannot finish its current route and still land with the reserve.
static bool CannotFinishRoute(const Drone& d) {
    float needed = RemainingKm(d) / d.cruise * DRAIN_PER_HOUR;
    return d.battery - needed < AUTO_RESERVE_BATTERY;
}

static void BeginServicing(Drone& d) {
    d.autoState = AUTO_SERVICING;
    d.autoTimer = AUTO_SERVICE_SECONDS;
}

static void BeginCharging(Drone& d) {
    d.autoState       = AUTO_CHARGING;
    d.autoTimer       = AUTO_CHARGE_SECONDS;
    d.autoChargeStart = d.battery;
}

static void ResumeSavedMission(Drone& d) {
    SavedMission m = d.savedMission;
    d.savedMission = SavedMission{};
    if (!m.valid || d.locIndex < 0 || m.endLoc == d.locIndex) return;
    DispatchTo(d, m.endLoc);
}

void UpdateAutomation(float simDt, int selectedDrone) {
    for (int i = 0; i < (int)g_fleet.size(); i++) {
        Drone& d = g_fleet[i];
        switch (d.autoState) {
        case AUTO_IDLE:
            if (d.active) {
                d.idleTimer = 0.0f;
                if (!d.returning && CannotFinishRoute(d)) {
                    d.savedMission = { true, d.startLoc, d.endLoc };
                    DivertTo(d, NearestLocation(d.pos));
                    d.autoState = AUTO_DIVERTING;
                }
            } else if (NeedsService(d)) {
                BeginServicing(d);
            } else if (d.battery < AUTO_LOW_BATTERY) {
                BeginCharging(d);
            } else if (d.battery >= MIN_DISPATCH_BATTERY && i != selectedDrone) {
                // Healthy, idle, not being inspected -> auto-dispatch after a delay.
                d.idleTimer += simDt;
                if (d.idleTimer >= AUTO_IDLE_DELAY) {
                    d.idleTimer = 0.0f;
                    Dispatch(d);
                }
            } else {
                d.idleTimer = 0.0f;
            }
            break;

        case AUTO_DIVERTING:
            if (!d.active) BeginServicing(d);          // Arrive() has parked it
            break;

        case AUTO_SERVICING:
            d.autoTimer -= simDt;
            if (d.autoTimer <= 0.0f) {
                Service(d);                            // repairs damage + updates software
                BeginCharging(d);
            }
            break;

        case AUTO_CHARGING:
            // A linear ramp overrides the natural charge so the drone visibly
            // fills up over the charging timer.
            d.autoTimer -= simDt;
            if (d.autoTimer <= 0.0f) {
                d.battery = 100.0f;
                d.autoState = AUTO_IDLE;
                ResumeSavedMission(d);
            } else {
                float t = 1.0f - d.autoTimer / AUTO_CHARGE_SECONDS;
                d.battery = d.autoChargeStart + (100.0f - d.autoChargeStart) * t;
            }
            break;
        }
    }
}

const char* AutoStateText(const Drone& d) {
    switch (d.autoState) {
    case AUTO_DIVERTING: return "Diverting to land";
    case AUTO_SERVICING: return TextFormat("Servicing (%.0f s left)",
                                            d.autoTimer / BASE_TIME_SCALE);
    case AUTO_CHARGING:
        if (d.savedMission.valid)
            return TextFormat("Charging (%.0f s, will resume)",
                              d.autoTimer / BASE_TIME_SCALE);
        return TextFormat("Charging (%.0f s)", d.autoTimer / BASE_TIME_SCALE);
    default:
        if (!d.active && d.battery >= MIN_DISPATCH_BATTERY && d.damage == DMG_NONE
            && !SwOutdated(d))
            return TextFormat("Idle (dispatch in %.0f s)",
                              (AUTO_IDLE_DELAY - d.idleTimer) / BASE_TIME_SCALE);
        return "Idle";
    }
}

void ReleaseAllFromAutomation() {
    for (Drone& d : g_fleet) d.autoState = AUTO_IDLE;
}