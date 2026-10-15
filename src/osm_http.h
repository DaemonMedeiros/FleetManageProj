#pragma once
#include <string>

// Download a single OSM tile to localPath.
// Returns true if the file was downloaded successfully.
bool DownloadOsmTile(const std::string& url, const std::string& localPath);
