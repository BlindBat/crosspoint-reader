#include "FakeHttp.h"

#include <algorithm>

#include "network/FirmwareFlasher.h"
#include "network/HttpDownloader.h"

FakeHttp& FakeHttp::instance() {
  static FakeHttp fake;
  return fake;
}

// Link stub for the production declaration: stream the canned body to the
// callback; a callback abort (return false) fails the transfer like the real
// esp_http_client loop does.
bool HttpDownloader::fetchUrl(const std::string& url, const DataCallback& onData, const std::string&,
                              const std::string&) {
  auto& fake = FakeHttp::instance();
  fake.requestedUrls.push_back(url);
  size_t offset = 0;
  while (offset < fake.body.size()) {
    const size_t n = std::min(fake.chunkSize, fake.body.size() - offset);
    if (!onData(fake.body.data() + offset, n)) return false;
    offset += n;
  }
  return fake.succeed;
}

uint16_t g_testChipId = 0x0005;

namespace firmware_flash {
uint16_t runningPartitionChipId() { return g_testChipId; }
}  // namespace firmware_flash

const char* g_testCurrentVersion = "1.6.0";
