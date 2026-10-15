// routing.h - position tracking and route / path handling
#pragma once

#include "fleet.h"

float RemainingKm(const Drone& d);                // distance left along the current path
void  MoveAlong(Drone& d, float km);              // advance a drone along its path by km

void  Dispatch(Drone& d);                         // create a new random mission from its location
void  DispatchTo(Drone& d, int endLoc);           // dispatch from d.locIndex to a chosen destination
void  DivertTo(Drone& d, int locIndex);           // active drone: fly straight to a location and land
int   NearestLocation(Vector2 posKm);             // index of closest location, -1 if none
void  Recall(Drone& d);                           // abort and fly back to the start address
void  ApplyFasterRoute(Drone& d);                 // replace remaining route with a direct line
void  Arrive(Drone& d);                           // finish mission and park at destination