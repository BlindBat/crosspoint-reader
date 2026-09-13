#pragma once

// Shared fake transport + link stubs for the OTA suite. The production
// HttpDownloader::fetchUrl(url, DataCallback, ...) is implemented in
// FakeHttp.cpp against this state, streaming `body` to the callback in
// `chunkSize` pieces (chunk-boundary handling in the parsers/scanner is part
// of what the suite exercises).

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct FakeHttp {
  bool succeed = true;        // fetchUrl result after streaming
  std::vector<uint8_t> body;  // response bytes
  size_t chunkSize = 7;       // stream granularity
  std::vector<std::string> requestedUrls;

  void reset() { *this = FakeHttp{}; }
  void setBody(const std::string& s) { body.assign(s.begin(), s.end()); }

  static FakeHttp& instance();
};

// Chip id reported by the firmware_flash::runningPartitionChipId() link stub.
extern uint16_t g_testChipId;

// Runtime-settable current firmware version; OtaUpdater.cpp is compiled with
// -DCROSSPOINT_VERSION=g_testCurrentVersion (declared for it in stubs/Logging.h).
extern const char* g_testCurrentVersion;

// A minimal GitHub release JSON document.
struct FakeAsset {
  std::string name;
  std::string url;
  size_t size = 0;
};

inline std::string makeReleaseJson(const std::string& tagName, const std::vector<FakeAsset>& assets,
                                   const bool includeTag = true) {
  std::string json = "{";
  if (includeTag) json += "\"tag_name\":\"" + tagName + "\",";
  json += "\"name\":\"Release " + tagName + "\",\"prerelease\":false,\"assets\":[";
  for (size_t i = 0; i < assets.size(); i++) {
    if (i) json += ",";
    json += "{\"name\":\"" + assets[i].name +
            "\",\"content_type\":\"application/octet-stream\",\"size\":" + std::to_string(assets[i].size) +
            ",\"browser_download_url\":\"" + assets[i].url + "\"}";
  }
  json += "],\"body\":\"notes\"}";
  return json;
}
