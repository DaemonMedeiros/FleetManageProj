// automation.cpp - automatic repair, software update, recharge and mission resume
#include "automation.h"

#include "fleet.h"
#include "routing.h"
#include "ui.h"        // g_app.settings, g_app.automation

#include "raylib.h"

// True if the drone cannot finish its current route and still land with the reserve.
static bool CannotFinishRoute(const Drone& d) {
    float needed = RemainingKm(d) / d.cruise * DRAIN_PER_HOUR;
    return d.battery - needed < AUTO_RESERVE_BATTERY;
}

static void BeginRepairing(Drone& d) {
    d.autoState       = AUTO_REPAIRING;
    d.autoTimer       = AUTO_REPAIR_SECONDS;
    d.autoRepairStart = d.condition;
}

static void BeginUpdating(Drone& d) {
    d.autoState = AUTO_UPDATING;
    d.autoTimer = AUTO_UPDATE_SECONDS;
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

// Automation processes exactly one task per drone at a time.  When a task
// finishes the drone returns to AUTO_IDLE, and the next tick chooses the next
// task by priority: repair -> recharge -> update software -> auto-dispatch.
//
// The individual task toggles live in g_app.settings.  When a toggle is OFF
// the drone is not queued for that task; it simply parks in AUTO_IDLE (which
// also unlocks the manual Dispatch button on the dashboard).
void UpdateAutomation(float simDt, int selectedDrone) {
    const Settings& cfg = g_app.settings;

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
            } else if (cfg.autoRepair && d.damage != DMG_NONE) {
                BeginRepairing(d);                              // 1st priority: repair
            } else if (cfg.autoRecharge && d.battery < AUTO_LOW_BATTERY) {
                BeginCharging(d);                               // 2nd priority: recharge
            } else if (cfg.autoUpdate && SwOutdated(d)) {
                BeginUpdating(d);                               // 3rd priority: software update
            } else if (cfg.randomizeDestination
                       && d.battery >= MIN_DISPATCH_BATTERY
                       && d.damage == DMG_NONE
                       && !SwOutdated(d)
                       && i != selectedDrone) {
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
            if (!d.active) d.autoState = AUTO_IDLE;   // Arrive() parked it; next tick picks the task
            break;

        case AUTO_REPAIRING: {
            d.autoTimer -= simDt;

            // Ramp condition from its entry value up to 100 over the repair
            // window.  We only commit the new value once it has grown by at
            // least 2 % since the last commit, so the bar fills in visible
            // chunks instead of jumping straight to 100.
            float elapsed = AUTO_REPAIR_SECONDS - d.autoTimer;
            float t = elapsed / AUTO_REPAIR_SECONDS;
            if (t < 0.0f) t = 0.0f;
            if (t > 1.0f) t = 1.0f;
            float target = d.autoRepairStart + (100.0f - d.autoRepairStart) * t;
            if (target - d.condition >= 2.0f) {
                d.condition = (target > 100.0f) ? 100.0f : target;
                d.damage    = DamageFromCondition(d.condition);
            }

            if (d.autoTimer <= 0.0f) {
                d.condition = 100.0f;
                d.damage    = DMG_NONE;
                d.autoState = AUTO_IDLE;
            }
            break;
        }

        case AUTO_UPDATING:
            d.autoTimer -= simDt;
            if (d.autoTimer <= 0.0f) {
                d.sw        = LATEST_SW;
                d.autoState = AUTO_IDLE;
            }
            break;

        case AUTO_CHARGING:
            d.autoTimer -= simDt;
            if (d.autoTimer <= 0.0f) {
                d.battery   = 100.0f;
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
    case AUTO_REPAIRING: return TextFormat("Repairing (%.0f s left)",
                                            d.autoTimer / BASE_TIME_SCALE);
    case AUTO_UPDATING:  return TextFormat("Updating SW (%.0f s left)",
                                            d.autoTimer / BASE_TIME_SCALE);
    case AUTO_CHARGING:
        if (d.savedMission.valid)
            return TextFormat("Charging (%.0f s, will resume)",
                              d.autoTimer / BASE_TIME_SCALE);
        return TextFormat("Charging (%.0f s left)", d.autoTimer / BASE_TIME_SCALE);
    default:
        if (d.idleTimer > 0.0f)
            return TextFormat("Idle (dispatch in %.0f s)",
                              (AUTO_IDLE_DELAY - d.idleTimer) / BASE_TIME_SCALE);
        return "Idle";
    }
}

void ReleaseAllFromAutomation() {
    for (Drone& d : g_fleet) d.autoState = AUTO_IDLE;
}