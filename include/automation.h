// automation.h - automatic repair, recharge, mission resume and idle dispatch
#pragma once

#include "fleet.h"

// ----------------------------------------------------------------------------
//  Automation constants
//  Timers are in simulated seconds.  1 real second at x1 speed is
//  BASE_TIME_SCALE simulated seconds, so 15 * BASE_TIME_SCALE ~= 15 real
//  seconds at x1 speed (and proportionally less at higher sim multipliers).
// ----------------------------------------------------------------------------
constexpr float AUTO_RESERVE_BATTERY = 10.0f;                    // % that must remain on landing
constexpr float AUTO_LOW_BATTERY     = 50.0f;                    // % below which a parked drone charges
constexpr float AUTO_READY_BATTERY   = 80.0f;                    // % at which a charged drone can fly
constexpr float AUTO_SERVICE_SECONDS = 15.0f * BASE_TIME_SCALE;  // repair / update duration
constexpr float AUTO_CHARGE_SECONDS  = 15.0f * BASE_TIME_SCALE;  // charge-up duration
constexpr float AUTO_IDLE_DELAY      =  5.0f * BASE_TIME_SCALE;  // wait before auto-dispatch

// Advance automation by simDt simulated seconds.  selectedDrone is the index of
// the drone the user is currently inspecting - it is skipped by the automatic
// dispatcher so the user can hold it for a manual route.  Pass -1 for none.
void        UpdateAutomation(float simDt, int selectedDrone = -1);

// Short status text describing what automation is doing with a drone.
const char* AutoStateText(const Drone& d);

// Reset every drone to AUTO_IDLE (used when automation is switched off so no
// drone is left locked in a servicing/charging state).
void        ReleaseAllFromAutomation();