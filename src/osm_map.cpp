// osm_map.cpp - bounded dynamic OpenStreetMap renderer.
//
// Important behaviour:
// - Real OSM zoom levels 11..18.
// - Only visible tiles are requested.
// - Panning is geographically limited to the Carlow operating area.
// - Tiles outside that operating area are never requested.
// - Existing downloaded tiles remain harmlessly cached on disk.
//
// Windows/WinINet code remains isolated in osm_http.cpp.

#include "osm_map.h"
#include "osm_http.h"

#include "raymath.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <map>
#include <string>

namespace {

constexpr int TILE_SIZE = 256;
constexpr int MIN_ZOOM = 11;
constexpr int MAX_ZOOM = 18;

constexpr double INITIAL_LAT = 52.8365;
constexpr double INITIAL_LON = -6.9341;
constexpr int INITIAL_ZOOM = 14;

constexpr std::size_t MAX_MEMORY_TILES = 96;

// -----------------------------------------------------------------------------
// Carlow operating-area boundary
// -----------------------------------------------------------------------------
// This is deliberately a rectangular project operating area rather than an
// administrative county/town boundary. It gives the prototype useful context
// around Carlow while preventing accidental exploration/downloads across
// Ireland or the rest of the world.
//
// Approximate extent:
//   north: 53.02
//   south: 52.65
//   west : -7.22
//   east : -6.65
//
// Roughly a few tens of kilometres around Carlow town.
constexpr double AREA_NORTH = 53.02;
constexpr double AREA_SOUTH = 52.65;
constexpr double AREA_WEST  = -7.22;
constexpr double AREA_EAST  = -6.65;

// Legacy simulation overlay reference.
constexpr int LEGACY_REFERENCE_ZOOM = 13;
constexpr int LEGACY_REFERENCE_TILES = 3;

struct TileKey {
    int z;
    int x;
    int y;

    bool operator<(const TileKey& other) const {
        if (z != other.z) return z < other.z;
        if (x != other.x) return x < other.x;
        return y < other.y;
    }
};

struct CachedTexture {
    Texture2D texture{};
    unsigned long long lastUsed = 0;
};

std::map<TileKey, CachedTexture> g_tiles;

int g_zoom = INITIAL_ZOOM;
double g_centerWorldX = 0.0;
double g_centerWorldY = 0.0;
unsigned long long g_useCounter = 0;

bool g_dragging = false;
Vector2 g_lastMouse{};

double ClampLatitude(double lat) {
    return std::clamp(lat, -85.05112878, 85.05112878);
}

double WorldScale(int zoom) {
    return static_cast<double>(TILE_SIZE) * static_cast<double>(1 << zoom);
}

double LonToWorldX(double lon, int zoom) {
    return (lon + 180.0) / 360.0 * WorldScale(zoom);
}

double LatToWorldY(double lat, int zoom) {
    lat = ClampLatitude(lat);
    const double radians = lat * 3.14159265358979323846 / 180.0;
    return (1.0 - std::asinh(std::tan(radians)) / 3.14159265358979323846)
           * 0.5 * WorldScale(zoom);
}

struct WorldBounds {
    double left;
    double top;
    double right;
    double bottom;
};

WorldBounds GetAllowedBounds(int zoom) {
    return {
        LonToWorldX(AREA_WEST, zoom),
        LatToWorldY(AREA_NORTH, zoom),
        LonToWorldX(AREA_EAST, zoom),
        LatToWorldY(AREA_SOUTH, zoom)
    };
}

// Clamp the viewport centre so that the user cannot pan beyond the allowed
// operating area.
//
// If the viewport is larger than the permitted area at a low zoom, that axis
// is locked to the middle of the allowed area instead of allowing pointless
// movement beyond it.
void ClampCenterToAllowedArea(Rectangle viewport) {
    const WorldBounds b = GetAllowedBounds(g_zoom);

    const double halfW = viewport.width * 0.5;
    const double halfH = viewport.height * 0.5;

    const double areaWidth = b.right - b.left;
    const double areaHeight = b.bottom - b.top;

    if (areaWidth <= viewport.width) {
        g_centerWorldX = (b.left + b.right) * 0.5;
    } else {
        g_centerWorldX = std::clamp(g_centerWorldX,
                                    b.left + halfW,
                                    b.right - halfW);
    }

    if (areaHeight <= viewport.height) {
        g_centerWorldY = (b.top + b.bottom) * 0.5;
    } else {
        g_centerWorldY = std::clamp(g_centerWorldY,
                                    b.top + halfH,
                                    b.bottom - halfH);
    }
}

std::string TilePath(int z, int x, int y) {
    return "resources/osm_cache/" + std::to_string(z) + "/" +
           std::to_string(x) + "_" + std::to_string(y) + ".png";
}

std::string TileUrl(int z, int x, int y) {
    return "https://tile.openstreetmap.org/" + std::to_string(z) + "/" +
           std::to_string(x) + "/" + std::to_string(y) + ".png";
}

void RemoveBadFile(const std::string& path) {
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

// Returns true only when this tile intersects the project's allowed geographic
// area. This is the second line of defence: even if viewport maths changes
// later, an out-of-area tile will not be requested.
bool TileIntersectsAllowedArea(int z, int x, int y) {
    const WorldBounds b = GetAllowedBounds(z);

    const double tileLeft   = static_cast<double>(x * TILE_SIZE);
    const double tileTop    = static_cast<double>(y * TILE_SIZE);
    const double tileRight  = tileLeft + TILE_SIZE;
    const double tileBottom = tileTop + TILE_SIZE;

    return tileRight > b.left &&
           tileLeft < b.right &&
           tileBottom > b.top &&
           tileTop < b.bottom;
}

Texture2D* GetTileTexture(int z, int x, int y) {
    const int tileCount = 1 << z;

    // Unlike the previous version, do not wrap X around the planet.
    if (x < 0 || x >= tileCount || y < 0 || y >= tileCount) return nullptr;

    // Hard download boundary.
    if (!TileIntersectsAllowedArea(z, x, y)) return nullptr;

    TileKey key{z, x, y};

    auto existing = g_tiles.find(key);
    if (existing != g_tiles.end()) {
        existing->second.lastUsed = ++g_useCounter;
        return &existing->second.texture;
    }

    const std::string path = TilePath(z, x, y);

    if (!std::filesystem::exists(path)) {
        if (!DownloadOsmTile(TileUrl(z, x, y), path)) return nullptr;
    }

    Image image = LoadImage(path.c_str());
    if (!IsImageValid(image)) {
        RemoveBadFile(path);
        return nullptr;
    }

    Texture2D texture = LoadTextureFromImage(image);
    UnloadImage(image);

    if (!IsTextureValid(texture)) return nullptr;

    CachedTexture cached;
    cached.texture = texture;
    cached.lastUsed = ++g_useCounter;

    auto inserted = g_tiles.emplace(key, cached);
    return &inserted.first->second.texture;
}

void TrimMemoryCache() {
    while (g_tiles.size() > MAX_MEMORY_TILES) {
        auto oldest = g_tiles.begin();

        for (auto it = g_tiles.begin(); it != g_tiles.end(); ++it) {
            if (it->second.lastUsed < oldest->second.lastUsed) oldest = it;
        }

        UnloadTexture(oldest->second.texture);
        g_tiles.erase(oldest);
    }
}

void ChangeZoom(int newZoom, Vector2 mouse, Rectangle viewport) {
    newZoom = std::clamp(newZoom, MIN_ZOOM, MAX_ZOOM);
    if (newZoom == g_zoom) return;

    // Keep the geographic point under the cursor as stable as possible.
    const double oldLeft = g_centerWorldX - viewport.width * 0.5;
    const double oldTop  = g_centerWorldY - viewport.height * 0.5;

    const double mouseWorldX = oldLeft + (mouse.x - viewport.x);
    const double mouseWorldY = oldTop  + (mouse.y - viewport.y);

    const double factor = std::pow(2.0, newZoom - g_zoom);

    const double newMouseWorldX = mouseWorldX * factor;
    const double newMouseWorldY = mouseWorldY * factor;

    g_zoom = newZoom;

    g_centerWorldX =
        newMouseWorldX - (mouse.x - viewport.x) + viewport.width * 0.5;
    g_centerWorldY =
        newMouseWorldY - (mouse.y - viewport.y) + viewport.height * 0.5;

    ClampCenterToAllowedArea(viewport);
}

} // namespace

void InitOsmMap() {
    g_zoom = INITIAL_ZOOM;
    g_centerWorldX = LonToWorldX(INITIAL_LON, g_zoom);
    g_centerWorldY = LatToWorldY(INITIAL_LAT, g_zoom);
    g_dragging = false;
}

void UnloadOsmMap() {
    for (auto& pair : g_tiles) {
        if (IsTextureValid(pair.second.texture)) {
            UnloadTexture(pair.second.texture);
        }
    }

    g_tiles.clear();
}

bool UpdateOsmMap(Rectangle viewport) {
    // Also clamps the initial position and handles any viewport-size changes.
    ClampCenterToAllowedArea(viewport);

    const Vector2 mouse = GetMousePosition();
    const bool inside = CheckCollisionPointRec(mouse, viewport);
    bool consumed = false;

    if (inside) {
        const float wheel = GetMouseWheelMove();

        if (wheel > 0.0f) {
            ChangeZoom(g_zoom + 1, mouse, viewport);
            consumed = true;
        } else if (wheel < 0.0f) {
            ChangeZoom(g_zoom - 1, mouse, viewport);
            consumed = true;
        }

        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            g_dragging = true;
            g_lastMouse = mouse;
        }
    }

    if (g_dragging && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
        const Vector2 now = GetMousePosition();
        const Vector2 delta = Vector2Subtract(now, g_lastMouse);

        g_centerWorldX -= delta.x;
        g_centerWorldY -= delta.y;

        ClampCenterToAllowedArea(viewport);

        g_lastMouse = now;
        consumed = true;
    }

    if (g_dragging && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
        g_dragging = false;
        consumed = true;
    }

    return consumed;
}

void DrawOsmMap(Rectangle viewport) {
    DrawRectangleRec(viewport, Color{31, 38, 46, 255});

    BeginScissorMode(
        static_cast<int>(viewport.x),
        static_cast<int>(viewport.y),
        static_cast<int>(viewport.width),
        static_cast<int>(viewport.height)
    );

    const double left   = g_centerWorldX - viewport.width * 0.5;
    const double top    = g_centerWorldY - viewport.height * 0.5;
    const double right  = left + viewport.width;
    const double bottom = top + viewport.height;

    const int firstTileX = static_cast<int>(std::floor(left / TILE_SIZE));
    const int lastTileX =
        static_cast<int>(std::floor((right - 0.001) / TILE_SIZE));
    const int firstTileY = static_cast<int>(std::floor(top / TILE_SIZE));
    const int lastTileY =
        static_cast<int>(std::floor((bottom - 0.001) / TILE_SIZE));

    for (int ty = firstTileY; ty <= lastTileY; ++ty) {
        for (int tx = firstTileX; tx <= lastTileX; ++tx) {
            // This check prevents even asking GetTileTexture for tiles outside
            // the project's permitted geographic area.
            if (!TileIntersectsAllowedArea(g_zoom, tx, ty)) continue;

            Texture2D* texture = GetTileTexture(g_zoom, tx, ty);
            if (!texture) continue;

            const float screenX =
                viewport.x + static_cast<float>(tx * TILE_SIZE - left);
            const float screenY =
                viewport.y + static_cast<float>(ty * TILE_SIZE - top);

            DrawTexture(
                *texture,
                static_cast<int>(std::round(screenX)),
                static_cast<int>(std::round(screenY)),
                WHITE
            );
        }
    }

    // Required OSM attribution.
    const char* attribution = "© OpenStreetMap contributors";
    const int fontSize = 12;
    const int textWidth = MeasureText(attribution, fontSize);
    const int ax =
        static_cast<int>(viewport.x + viewport.width - textWidth - 8);
    const int ay =
        static_cast<int>(viewport.y + viewport.height - 18);

    DrawRectangle(ax - 4, ay - 2, textWidth + 8, 16, Fade(BLACK, 0.65f));
    DrawText(attribution, ax, ay, fontSize, RAYWHITE);

    EndScissorMode();

    TrimMemoryCache();
}

Vector2 LegacyWorldToScreen(Vector2 km, float worldKm, Rectangle viewport) {
    const double referenceCenterX =
        LonToWorldX(INITIAL_LON, LEGACY_REFERENCE_ZOOM);
    const double referenceCenterY =
        LatToWorldY(INITIAL_LAT, LEGACY_REFERENCE_ZOOM);

    const double referenceSpan = LEGACY_REFERENCE_TILES * TILE_SIZE;

    const double refX =
        referenceCenterX - referenceSpan * 0.5 +
        (static_cast<double>(km.x) / worldKm) * referenceSpan;

    const double refY =
        referenceCenterY - referenceSpan * 0.5 +
        (static_cast<double>(km.y) / worldKm) * referenceSpan;

    const double factor =
        std::pow(2.0, g_zoom - LEGACY_REFERENCE_ZOOM);

    const double worldX = refX * factor;
    const double worldY = refY * factor;

    const double left = g_centerWorldX - viewport.width * 0.5;
    const double top  = g_centerWorldY - viewport.height * 0.5;

    return {
        viewport.x + static_cast<float>(worldX - left),
        viewport.y + static_cast<float>(worldY - top)
    };
}

int GetOsmZoom() {
    return g_zoom;
}
