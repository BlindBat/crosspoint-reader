#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

struct WifiNetworkInfo {
  std::string ssid;
  int32_t rssi;
  bool isEncrypted;
  bool hasSavedPassword;             // Whether we have saved credentials for this network
  bool isHiddenPlaceholder = false;  // Synthetic "Add hidden network..." list entry
};

// Pure scan-result and signal helpers shared by the Wi-Fi activities.
namespace WifiScanUtils {

// Named MAX_SSID_BYTES, not MAX_SSID_LEN: esp_wifi_types_generic.h defines that as a
// macro, and a macro ignores the namespace.
constexpr size_t MAX_SSID_BYTES = 32;

// Merge one scan result into `networks`. The SSID is truncated to MAX_SSID_BYTES bytes; hidden
// (empty-SSID) results are skipped; a repeated SSID keeps the strongest signal and that
// result's encryption flag. Returns true when a new entry was appended (with
// hasSavedPassword = false, for the caller to fill in).
bool mergeScanResult(std::vector<WifiNetworkInfo>& networks, std::string_view ssid, int32_t rssi, bool isEncrypted);

// Saved-password networks first, then strongest signal first.
void sortScannedNetworks(std::vector<WifiNetworkInfo>& networks);

// 0..4 bars from RSSI (dBm), with 3 dBm hysteresis on currentBars to suppress flicker.
int barsForRssi(int rssi, int currentBars);

}  // namespace WifiScanUtils
