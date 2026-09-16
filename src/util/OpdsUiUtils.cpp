#include "OpdsUiUtils.h"

#include <cctype>
#include <cstdio>

namespace opds_ui {

std::string normalizeDownloadFolder(std::string v) {
  while (!v.empty() && (v.front() == ' ' || v.front() == '\t')) v.erase(v.begin());
  while (!v.empty() && (v.back() == ' ' || v.back() == '\t')) v.pop_back();
  if (v.empty()) return "";
  if (v.front() != '/') v.insert(v.begin(), '/');
  while (v.size() > 1 && v.back() == '/') v.pop_back();
  if (v == "/") return "";  // a bare slash is SD root, same as empty
  return v;
}

std::string percentEncodeQuery(const std::string& query) {
  std::string out;
  out.reserve(query.size() * 3);
  for (unsigned char c : query) {
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
      out += static_cast<char>(c);
    else {
      char buf[4];
      snprintf(buf, sizeof(buf), "%%%02X", c);
      out += buf;
    }
  }
  return out;
}

std::string buildSearchUrl(const std::string& searchTemplate, const std::string& query) {
  std::string url = searchTemplate;
  static constexpr char PLACEHOLDER[] = "{searchTerms}";
  const size_t pos = url.find(PLACEHOLDER);
  if (pos != std::string::npos) url.replace(pos, sizeof(PLACEHOLDER) - 1, percentEncodeQuery(query));
  return url;
}

void addPaginationRows(std::vector<OpdsEntry>& entries, const std::string& prevUrl, const std::string& nextUrl,
                       const char* prevLabel, const char* nextLabel) {
  entries.reserve(entries.size() + (prevUrl.empty() ? 0 : 1) + (nextUrl.empty() ? 0 : 1));
  if (!prevUrl.empty()) {
    entries.insert(entries.begin(), OpdsEntry{OpdsEntryType::NAVIGATION, prevLabel, "", prevUrl, ""});
  }
  if (!nextUrl.empty()) {
    entries.push_back(OpdsEntry{OpdsEntryType::NAVIGATION, nextLabel, "", nextUrl, ""});
  }
}

}  // namespace opds_ui
