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

// Real-world <-> simulation-grid conversion.
//
// The drone simulation uses a linear 0..WORLD_KM grid anchored at
// INITIAL_LAT / INITIAL_LON (Carlow town centre).  These helpers convert
// between that grid and WGS84 lat/lon so locations can be entered as
// coordinates rather than as hand-picked km values.
//
// Round-trip accuracy across the 50 km world is within a few metres at
// Carlow's latitude - well below the resolution of the grid itself.
Vector2 LatLonToKm(double lat, double lon, float worldKm);
void    KmToLatLon(Vector2 km, float worldKm, double& outLat, double& outLon);

int GetOsmZoom();