// routing.h - position tracking and route / path handling
#pragma once

#include "fleet.h"

float RemainingKm(const Drone& d);                // distance left along the current path
void  MoveAlong(Drone& d, float km);              // advance a drone along its path by km

void  Dispatch(Drone& d);                         // create a new random mission from its location
void  Recall(Drone& d);                           // abort and fly back to the start address
void  ApplyFasterRoute(Drone& d);                 // replace remaining route with a direct line
void  Arrive(Drone& d);                           // finish mission and park at destination
