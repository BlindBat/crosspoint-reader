#include "SmartSyncDecision.h"

#include <cmath>

namespace SmartSync {

DocumentMatchMethod alternateMatchMethod(const DocumentMatchMethod method) {
  return method == DocumentMatchMethod::FILENAME ? DocumentMatchMethod::BINARY : DocumentMatchMethod::FILENAME;
}

const char* matchMethodName(const DocumentMatchMethod method) {
  return method == DocumentMatchMethod::FILENAME ? "filename" : "binary";
}

bool shouldProbeAlternate(const std::string& primaryHash, const std::string& altHash) {
  return !altHash.empty() && altHash != primaryHash;
}

bool preferAlternate(const KOReaderSyncClient::Error primaryResult, const float primaryPercentage,
                     const KOReaderSyncClient::Error altResult, const float altPercentage) {
  if (altResult != KOReaderSyncClient::OK) return false;
  return primaryResult == KOReaderSyncClient::NOT_FOUND || altPercentage > primaryPercentage;
}

Resolution resolve(const float localPercentage, const float remotePercentage) {
  const float delta = localPercentage - remotePercentage;
  if (std::fabs(delta) <= SAME_PROGRESS_EPSILON) return Resolution::ALREADY_SYNCED;
  return delta > 0 ? Resolution::UPLOAD_LOCAL : Resolution::APPLY_REMOTE;
}

int askModeDefaultOption(const float localPercentage, const float remotePercentage) {
  return localPercentage > remotePercentage ? 1 : 0;
}

KOReaderRichPosition buildRichPosition(const float percentage, const int spineIndex, const int page,
                                       const int totalPages, const std::optional<uint16_t> paragraphIndex,
                                       const std::string& xpath) {
  KOReaderRichPosition pos;
  const float pct = percentage < 0.0f ? 0.0f : percentage > 1.0f ? 1.0f : percentage;
  pos.pctQ = static_cast<uint32_t>(pct * static_cast<float>(PCT_Q_SCALE) + 0.5f);
  pos.spineIndex = static_cast<uint16_t>(spineIndex);
  pos.pageNumber = static_cast<uint16_t>(page);
  pos.totalPages = static_cast<uint16_t>(totalPages > 0 ? totalPages : 1);
  pos.paragraphIndex = paragraphIndex;
  pos.xpath = xpath;
  return pos;
}

}  // namespace SmartSync
