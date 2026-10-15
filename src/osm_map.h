#pragma once

#include "raylib.h"

void InitOsmMap();
void UnloadOsmMap();

// Handles map pan/zoom while enforcing the Carlow operating-area boundary.
bool UpdateOsmMap(Rectangle viewport);

// Draws only tiles that intersect both the viewport and the permitted
// Carlow operating area.
void DrawOsmMap(Rectangle viewport);

// Temporary bridge for the existing 0..WORLD_KM simulation.
Vector2 LegacyWorldToScreen(Vector2 km, float worldKm, Rectangle viewport);

int GetOsmZoom();
