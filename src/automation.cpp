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

// A drone is "ready to launch" when it is parked at the Drone Facility, has
// no active task, no damage, no outdated software, sufficient battery, and
// has at least one destination to fly to.
static bool ReadyToLaunch(const Drone& d) {
    return !d.active
        && d.autoState == AUTO_IDLE
        && d.locIndex == FACILITY
        && d.damage == DMG_NONE
        && !SwOutdated(d)
        && d.battery >= MIN_DISPATCH_BATTERY;
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
// autoLaunch is the ONLY thing that starts a mission from automation.
// randomizeDestination only decides the destination:
//   - ON  : pick a fresh random location (excluding the facility)
//   - OFF : re-use d.lastDestLoc (with a random fallback if unset)
void UpdateAutomation(float simDt, int selectedDrone) {
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
                d.idleTimer = 0.0f;
                if (!d.returning && CannotFinishRoute(d)) {
                    d.savedMission = { true, d.startLoc, d.endLoc };
                    DivertTo(d, NearestLocation(d.pos));
                    d.autoState = AUTO_DIVERTING;
                }
                break;
            }

            // Parked: priority 1 repair, 2 recharge, 3 update.
            if (cfg.autoRepair && d.damage != DMG_NONE) {
                BeginRepairing(d);
            } else if (cfg.autoRecharge && d.battery < AUTO_LOW_BATTERY) {
                BeginCharging(d);
            } else if (cfg.autoUpdate && SwOutdated(d)) {
                BeginUpdating(d);
            } else if (cfg.autoLaunch
                       && i != selectedDrone
                       && ReadyToLaunch(d)) {
                // Docked at the facility, healthy - wait the usual idle delay,
                // then launch to either a fresh random location or the same
                // destination as last time.
                d.idleTimer += simDt;
                if (d.idleTimer >= AUTO_IDLE_DELAY) {
                    d.idleTimer = 0.0f;

                    int dest = -1;
                    if (cfg.randomizeDestination) {
                        do {
                            dest = GetRandomValue(0, (int)g_locations.size() - 1);
                        } while (dest == FACILITY);
                    } else if (d.lastDestLoc >= 0 && d.lastDestLoc != FACILITY) {
                        dest = d.lastDestLoc;
                    } else {
                        do {
                            dest = GetRandomValue(0, (int)g_locations.size() - 1);
                        } while (dest == FACILITY);
                    }

                    if (dest >= 0) DispatchTo(d, dest);
                }
            } else {
                d.idleTimer = 0.0f;
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
        if (d.idleTimer > 0.0f)
            return TextFormat("Idle (launch in %.0f s)",
                              (AUTO_IDLE_DELAY - d.idleTimer) / BASE_TIME_SCALE);
        return "Idle";
    }
}

void ReleaseAllFromAutomation() {
    for (Drone& d : g_fleet) d.autoState = AUTO_IDLE;
}