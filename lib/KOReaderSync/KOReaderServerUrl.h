#pragma once
#include <string>

// Sync Server URL handling shared by the KOReader settings row and its
// keyboard entry. Scheme/default normalisation for API calls lives in
// KOReaderCredentialStore::getBaseUrl(); these only shape what the user
// types and sees.
namespace KOReaderServerUrl {

// Keyboard prefill: a bare https:// saves typing when nothing is configured.
inline std::string prefillForEntry(const std::string& configured) {
  return configured.empty() ? "https://" : configured;
}

// A bare scheme left over from the prefill means "use the default server".
inline std::string normalizeEntered(const std::string& entered) {
  return (entered == "https://" || entered == "http://") ? "" : entered;
}

// Scheme-less form for the settings row ("Default: host").
inline std::string stripScheme(std::string url) {
  const auto schemeEnd = url.find("://");
  if (schemeEnd != std::string::npos) {
    url.erase(0, schemeEnd + 3);
  }
  return url;
}

}  // namespace KOReaderServerUrl
