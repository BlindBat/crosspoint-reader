#include "WifiScanUtils.h"

#include <algorithm>

namespace WifiScanUtils {

bool mergeScanResult(std::vector<WifiNetworkInfo>& networks, std::string_view ssid, const int32_t rssi,
                     const bool isEncrypted) {
  if (ssid.size() > MAX_SSID_BYTES) {
    ssid = ssid.substr(0, MAX_SSID_BYTES);
  }
  if (ssid.empty()) {
    return false;
  }

  auto it = std::find_if(networks.begin(), networks.end(), [ssid](const WifiNetworkInfo& n) { return n.ssid == ssid; });
  if (it != networks.end()) {
    if (rssi > it->rssi) {
      it->rssi = rssi;
      it->isEncrypted = isEncrypted;
    }
    return false;
  }

  WifiNetworkInfo network;
  network.ssid = std::string(ssid);
  network.rssi = rssi;
  network.isEncrypted = isEncrypted;
  network.hasSavedPassword = false;
  networks.push_back(std::move(network));
  return true;
}

void sortScannedNetworks(std::vector<WifiNetworkInfo>& networks) {
  std::sort(networks.begin(), networks.end(), [](const WifiNetworkInfo& a, const WifiNetworkInfo& b) {
    if (a.hasSavedPassword != b.hasSavedPassword) {
      return a.hasSavedPassword;
    }
    return a.rssi > b.rssi;
  });
}

int barsForRssi(const int rssi, const int currentBars) {
  static constexpr int RISE_DBM[] = {-85, -75, -65, -55};
  static constexpr int FALL_DBM[] = {-88, -78, -68, -58};
  int bars = std::clamp(currentBars, 0, 4);
  while (bars < 4 && rssi >= RISE_DBM[bars]) bars++;
  while (bars > 0 && rssi < FALL_DBM[bars - 1]) bars--;
  return bars;
}

}  // namespace WifiScanUtils
