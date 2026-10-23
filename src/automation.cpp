// automation.cpp - automatic repair, software update, recharge, launch and return
#include "automation.h"

#include "fleet.h"
#include "routing.h"
#include "ui.h"        // g_app.settings

#include "raylib.h"

// The Drone Facility is always location 0 in g_locations.
static constexpr int FACILITY = 0;

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

// True when a yellow status will be fixed by automation before launch, so the
// drone must be held at the facility until servicing completes.  If the
// corresponding task is OFF, that status does NOT block launch - the user has
// chosen not to manage it, so automation treats the drone as fit to fly.
static bool BlockedByServicing(const Drone& d, const Settings& cfg) {
    if (d.damage != DMG_NONE        && cfg.autoRepair)   return true;
    if (d.battery < AUTO_LOW_BATTERY && cfg.autoRecharge) return true;
    if (SwOutdated(d)                && cfg.autoUpdate)   return true;
    return false;
}

// Automation processes exactly one task per drone at a time.  When a task
// finishes the drone returns to AUTO_IDLE, and the next tick chooses the next
// task by priority:
//
//   repair -> recharge -> update software -> auto-launch
//
// Every task is gated on its own Settings switch.  The master switch is
// checked by the caller (main.cpp) so we do not re-check it here.
//
// Auto-launch rules (matching the "yellow" rule):
//   * Any yellow status WITH its respective task ON blocks launch - the drone
//     is held at the facility, serviced, then launched once clean.
//   * Any yellow status WITH its respective task OFF does NOT block launch -
//     the user opted out of managing it, so the drone flies as-is.
//   * Auto-launch is immediate once the drone is eligible - no idle delay.
//
// randomizeDestination decides the destination:
//   - ON  : pick a fresh random location each launch
//   - OFF : re-use d.lastDestLoc; if there is no previous destination yet the
//           drone stays docked (nothing to fly to)
void UpdateAutomation(float simDt, int selectedDrone) {
    (void)selectedDrone;   // reserved for future "hold this drone" behaviour
    const Settings& cfg = g_app.settings;

    for (int i = 0; i < (int)g_fleet.size(); i++) {
        Drone& d = g_fleet[i];

        // Passive trickle charge while parked - gated on the recharge switch.
        if (!d.active && cfg.autoRecharge) {
            d.battery += CHARGE_PER_HOUR * simDt / 3600.0f;
            if (d.battery > 100.0f) d.battery = 100.0f;
        }

        switch (d.autoState) {
        case AUTO_IDLE:
            if (d.active) {
                if (!d.returning && CannotFinishRoute(d)) {
                    d.savedMission = { true, d.startLoc, d.endLoc };
                    DivertTo(d, NearestLocation(d.pos));
                    d.autoState = AUTO_DIVERTING;
                }
                break;
            }

            // Parked at the facility (or wherever it ended up after a mission).
            // Priority 1: repair.  2: recharge.  3: update.  4: launch.
            //
            // The yellow-status rule is implemented via BlockedByServicing:
            // a yellow status with its task ON will be handled here first and
            // is therefore a blocker; a yellow status with its task OFF is
            // ignored and the drone goes on to the launch branch.

            if (cfg.autoRepair && d.damage != DMG_NONE) {
                BeginRepairing(d);
            } else if (cfg.autoRecharge && d.battery < AUTO_LOW_BATTERY) {
                BeginCharging(d);
            } else if (cfg.autoUpdate && SwOutdated(d)) {
                BeginUpdating(d);
            } else if (cfg.autoLaunch
                       && d.locIndex == FACILITY
                       && !BlockedByServicing(d, cfg)) {
                // Launch immediately.  Every eligible drone dispatches on the
                // same frame - there is no per-frame cap and no idle timer.
                int dest = -1;
                if (cfg.randomizeDestination) {
                    do {
                        dest = GetRandomValue(0, (int)g_locations.size() - 1);
                    } while (dest == d.locIndex);
                } else if (d.lastDestLoc >= 0 && d.lastDestLoc != d.locIndex) {
                    dest = d.lastDestLoc;
                } else {
                    // randomize off, no previous destination set: nothing to fly to
                    break;
                }
                if (dest >= 0) DispatchTo(d, dest);
            }
            break;

        case AUTO_DIVERTING:
            if (!d.active) d.autoState = AUTO_IDLE;
            break;

        case AUTO_REPAIRING: {
            d.autoTimer -= simDt;

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
        return "Idle";
    }
}

void ReleaseAllFromAutomation() {
    for (Drone& d : g_fleet) d.autoState = AUTO_IDLE;
}