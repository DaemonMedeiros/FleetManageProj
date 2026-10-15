// osm_http.cpp - Windows HTTPS download code only.
// Deliberately does not include raylib.h because Windows and raylib use
// several conflicting function/type names.

#include "osm_http.h"

#include <windows.h>
#include <wininet.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

bool DownloadOsmTile(const std::string& url, const std::string& localPath) {
    // OSM requires a distinct, stable User-Agent for native applications.
    HINTERNET internet = InternetOpenA(
        "CarlowDroneFleetManager/0.2 (university student prototype)",
        INTERNET_OPEN_TYPE_PRECONFIG,
        nullptr,
        nullptr,
        0
    );

    if (!internet) return false;

    HINTERNET request = InternetOpenUrlA(
        internet,
        url.c_str(),
        nullptr,
        0,
        INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_SECURE,
        0
    );

    if (!request) {
        InternetCloseHandle(internet);
        return false;
    }

    DWORD statusCode = 0;
    DWORD statusSize = sizeof(statusCode);
    HttpQueryInfoA(
        request,
        HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER,
        &statusCode,
        &statusSize,
        nullptr
    );

    if (statusCode != 200) {
        InternetCloseHandle(request);
        InternetCloseHandle(internet);
        return false;
    }

    std::vector<unsigned char> bytes;
    unsigned char buffer[8192];
    DWORD bytesRead = 0;

    while (InternetReadFile(request, buffer, sizeof(buffer), &bytesRead) && bytesRead > 0) {
        bytes.insert(bytes.end(), buffer, buffer + bytesRead);
    }

    InternetCloseHandle(request);
    InternetCloseHandle(internet);

    if (bytes.empty()) return false;

    std::filesystem::path p(localPath);
    if (p.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(p.parent_path(), ec);
    }

    std::ofstream file(localPath, std::ios::binary);
    if (!file) return false;

    file.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));

    return file.good();
}
