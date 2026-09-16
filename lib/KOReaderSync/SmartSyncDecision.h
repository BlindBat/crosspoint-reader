#pragma once
#include <cstdint>
#include <optional>
#include <string>

#include "KOReaderCredentialStore.h"
#include "KOReaderSyncClient.h"

/**
 * Pure decision logic behind KOReaderSyncActivity: which remote record to
 * adopt, how Smart sync resolves local vs remote progress, which row Ask mode
 * preselects, and how the CrossPoint rich position is built for an upload.
 * No I/O, so the activity stays thin and the table is testable on the host.
 */
namespace SmartSync {

// Smart-sync outcome once a remote record exists.
enum class Resolution : uint8_t {
  ALREADY_SYNCED,  // |local - remote| within SAME_PROGRESS_EPSILON
  UPLOAD_LOCAL,    // local is ahead: upload under the primary document id
  APPLY_REMOTE,    // remote is ahead: apply the mapped remote position
};

// 0.1 percentage points: closer than this counts as the same position.
constexpr float SAME_PROGRESS_EPSILON = 0.001f;

// Rich-position percentage is quantized to millionths (pctQ 0..1,000,000).
constexpr uint32_t PCT_Q_SCALE = 1000000;

DocumentMatchMethod alternateMatchMethod(DocumentMatchMethod method);
const char* matchMethodName(DocumentMatchMethod method);

// The alternate id is only worth a request when it hashed and differs from the primary.
bool shouldProbeAlternate(const std::string& primaryHash, const std::string& altHash);

// The alternate record replaces the primary when it exists and either the
// primary has none or the alternate is further along.
bool preferAlternate(KOReaderSyncClient::Error primaryResult, float primaryPercentage,
                     KOReaderSyncClient::Error altResult, float altPercentage);

Resolution resolve(float localPercentage, float remotePercentage);

// Ask-mode default row: 1 (Upload local) when local is ahead, else 0 (Apply remote).
int askModeDefaultOption(float localPercentage, float remotePercentage);

// Percentage clamped to 0..1 and rounded to pctQ; a non-positive page count becomes 1.
KOReaderRichPosition buildRichPosition(float percentage, int spineIndex, int page, int totalPages,
                                       std::optional<uint16_t> paragraphIndex, const std::string& xpath);

}  // namespace SmartSync
