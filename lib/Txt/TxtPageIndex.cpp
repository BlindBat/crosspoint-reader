#include "TxtPageIndex.h"

#include <Logging.h>
#include <Memory.h>
#include <PlatformSeam.h>

#include <algorithm>
#include <utility>

namespace TxtPageIndex {

namespace {

template <typename T>
bool readPod(ByteReader& in, T& value) {
  return in.read(&value, sizeof(T)) == sizeof(T);
}

template <typename T>
void writePod(ByteWriter& out, const T& value) {
  out.write(&value, sizeof(T));
}

}  // namespace

bool layoutPage(const uint8_t* chunk, const size_t chunkSize, const size_t offset, const size_t fileSize,
                const Layout& layout, TextMeasurer& measurer, std::vector<std::string>& outLines, size_t& nextOffset) {
  outLines.clear();
  const int linesPerPage = layout.linesPerPage;
  const int viewportWidth = layout.viewportWidth;

  // Parse lines from buffer
  size_t pos = 0;

  while (pos < chunkSize && static_cast<int>(outLines.size()) < linesPerPage) {
    // Find end of line
    size_t lineEnd = pos;
    while (lineEnd < chunkSize && chunk[lineEnd] != '\n') {
      lineEnd++;
    }

    // Check if we have a complete line
    const bool lineComplete = (lineEnd < chunkSize) || (offset + lineEnd >= fileSize);

    if (!lineComplete && static_cast<int>(outLines.size()) > 0) {
      // Incomplete line and we already have some lines, stop here
      break;
    }

    const size_t lineContentLen = lineEnd - pos;
    const bool hasCR = (lineContentLen > 0 && chunk[pos + lineContentLen - 1] == '\r');
    const size_t displayLen = hasCR ? lineContentLen - 1 : lineContentLen;

    std::string line(reinterpret_cast<const char*>(chunk + pos), displayLen);
    size_t lineBytePos = 0;

    do {
      if (line.empty()) {
        outLines.emplace_back();
        break;
      }

      const int lineWidth = measurer.advanceX(line.c_str());

      if (lineWidth <= viewportWidth) {
        outLines.push_back(line);
        lineBytePos = displayLen;
        line.clear();
        break;
      }

      // Find break point
      size_t breakPos = line.length();
      while (breakPos > 0 && measurer.advanceX(line.substr(0, breakPos).c_str()) > viewportWidth) {
        // Try to break at space
        const size_t spacePos = line.rfind(' ', breakPos - 1);
        if (spacePos != std::string::npos && spacePos > 0) {
          breakPos = spacePos;
        } else {
          // Break at character boundary for UTF-8
          breakPos--;
          while (breakPos > 0 && (line[breakPos] & 0xC0) == 0x80) {
            breakPos--;
          }
        }
      }

      if (breakPos == 0) {
        breakPos = 1;
      }

      outLines.push_back(line.substr(0, breakPos));

      size_t skipChars = breakPos;
      if (breakPos < line.length() && line[breakPos] == ' ') {
        skipChars++;
      }
      lineBytePos += skipChars;
      line = line.substr(skipChars);
    } while (!line.empty() && static_cast<int>(outLines.size()) < linesPerPage);

    if (line.empty()) {
      pos = lineEnd + 1;
    } else {
      pos = pos + lineBytePos;
      break;
    }
  }

  if (pos == 0 && !outLines.empty()) {
    pos = 1;
  }

  nextOffset = offset + pos;
  if (nextOffset > fileSize) {
    nextOffset = fileSize;
  }

  return !outLines.empty();
}

bool loadPageAtOffset(ContentReader& content, const size_t fileSize, const size_t offset, const Layout& layout,
                      TextMeasurer& measurer, std::vector<std::string>& outLines, size_t& nextOffset) {
  outLines.clear();

  if (offset >= fileSize) {
    return false;
  }

  // Read a chunk from file
  const size_t chunkSize = std::min(CHUNK_SIZE, fileSize - offset);
  auto buffer = makeUniqueNoThrow<uint8_t[]>(chunkSize + 1);
  if (!buffer) {
    LOG_ERR("TRS", "Failed to allocate %zu bytes", chunkSize);
    return false;
  }

  if (!content.readContent(buffer.get(), offset, chunkSize)) {
    return false;
  }
  buffer[chunkSize] = '\0';

  measurer.prepare(reinterpret_cast<const char*>(buffer.get()));

  return layoutPage(buffer.get(), chunkSize, offset, fileSize, layout, measurer, outLines, nextOffset);
}

int buildPageIndex(ContentReader& content, const size_t fileSize, const Layout& layout, TextMeasurer& measurer,
                   std::vector<size_t>& pageOffsets) {
  pageOffsets.clear();
  pageOffsets.push_back(0);  // First page starts at offset 0

  size_t offset = 0;

  while (offset < fileSize) {
    std::vector<std::string> tempLines;
    size_t nextOffset = offset;

    if (!loadPageAtOffset(content, fileSize, offset, layout, measurer, tempLines, nextOffset)) {
      break;
    }

    if (nextOffset <= offset) {
      // No progress made, avoid infinite loop
      break;
    }

    offset = nextOffset;
    if (offset < fileSize) {
      pageOffsets.push_back(offset);
    }

    // Yield to other tasks periodically
    if (pageOffsets.size() % 20 == 0) {
      platform::yield();
    }
  }

  return static_cast<int>(pageOffsets.size());
}

bool loadPageIndexCache(ByteReader& in, const CacheKey& key, std::vector<size_t>& pageOffsets) {
  uint32_t magic = 0;
  if (!readPod(in, magic)) {
    LOG_ERR("TRS", "Cache header truncated at magic, rebuilding");
    return false;
  }
  if (magic != CACHE_MAGIC) {
    LOG_DBG("TRS", "Cache magic mismatch, rebuilding");
    return false;
  }

  uint8_t version = 0;
  if (!readPod(in, version)) {
    LOG_ERR("TRS", "Cache header truncated at version, rebuilding");
    return false;
  }
  if (version != CACHE_VERSION) {
    LOG_DBG("TRS", "Cache version mismatch (%d != %d), rebuilding", version, CACHE_VERSION);
    return false;
  }

  uint32_t fileSize = 0;
  if (!readPod(in, fileSize)) {
    LOG_ERR("TRS", "Cache header truncated at file size, rebuilding");
    return false;
  }
  if (fileSize != key.fileSize) {
    LOG_DBG("TRS", "Cache file size mismatch, rebuilding");
    return false;
  }

  int32_t cachedWidth = 0;
  if (!readPod(in, cachedWidth)) {
    LOG_ERR("TRS", "Cache header truncated at viewport width, rebuilding");
    return false;
  }
  if (cachedWidth != key.viewportWidth) {
    LOG_DBG("TRS", "Cache viewport width mismatch, rebuilding");
    return false;
  }

  int32_t cachedLines = 0;
  if (!readPod(in, cachedLines)) {
    LOG_ERR("TRS", "Cache header truncated at lines per page, rebuilding");
    return false;
  }
  if (cachedLines != key.linesPerPage) {
    LOG_DBG("TRS", "Cache lines per page mismatch, rebuilding");
    return false;
  }

  int32_t fontId = 0;
  if (!readPod(in, fontId)) {
    LOG_ERR("TRS", "Cache header truncated at font ID, rebuilding");
    return false;
  }
  if (fontId != key.fontId) {
    LOG_DBG("TRS", "Cache font ID mismatch (%d != %d), rebuilding", fontId, key.fontId);
    return false;
  }

  int32_t margin = 0;
  if (!readPod(in, margin)) {
    LOG_ERR("TRS", "Cache header truncated at screen margin, rebuilding");
    return false;
  }
  if (margin != key.screenMargin) {
    LOG_DBG("TRS", "Cache screen margin mismatch, rebuilding");
    return false;
  }

  uint8_t alignment = 0;
  if (!readPod(in, alignment)) {
    LOG_ERR("TRS", "Cache header truncated at alignment, rebuilding");
    return false;
  }
  if (alignment != key.paragraphAlignment) {
    LOG_DBG("TRS", "Cache paragraph alignment mismatch, rebuilding");
    return false;
  }

  uint32_t numPages = 0;
  if (!readPod(in, numPages)) {
    LOG_ERR("TRS", "Cache header truncated at page count, rebuilding");
    return false;
  }

  // Bound the count before reserving: the entries are fixed-size records, so a
  // cache file of N bytes cannot hold more than (N - header) / record of them.
  const size_t cacheBytes = in.size();
  if (cacheBytes < INDEX_HEADER_BYTES) {
    LOG_ERR("TRS", "Cache shorter than its header (%zu bytes), rebuilding", cacheBytes);
    return false;
  }
  const size_t maxEntries = (cacheBytes - INDEX_HEADER_BYTES) / INDEX_ENTRY_BYTES;
  if (numPages > maxEntries) {
    LOG_ERR("TRS", "Cache claims %u pages but holds %zu, rebuilding", numPages, maxEntries);
    return false;
  }

  // A page starts at a distinct byte of the text, so the text bounds the count.
  const size_t maxPages = key.fileSize > 0 ? key.fileSize : 1;
  if (numPages > maxPages) {
    LOG_ERR("TRS", "Cache claims %u pages for %u bytes of text, rebuilding", numPages, key.fileSize);
    return false;
  }
  if (numPages == 0 && key.fileSize > 0) {
    LOG_ERR("TRS", "Cache has no pages for %u bytes of text, rebuilding", key.fileSize);
    return false;
  }

  std::vector<size_t> offsets;
  offsets.reserve(numPages);

  size_t previous = 0;
  for (uint32_t i = 0; i < numPages; i++) {
    uint32_t offset = 0;
    if (!readPod(in, offset)) {
      LOG_ERR("TRS", "Cache truncated at page %u of %u, rebuilding", i, numPages);
      return false;
    }
    // The writer emits page 0 at offset 0 and only advances, so anything that
    // repeats, goes backwards or starts past the text is corrupt.
    if (i == 0 ? offset != 0 : offset <= previous) {
      LOG_ERR("TRS", "Cache page %u offset %u out of order, rebuilding", i, offset);
      return false;
    }
    if (key.fileSize > 0 && offset >= key.fileSize) {
      LOG_ERR("TRS", "Cache page %u offset %u past %u bytes of text, rebuilding", i, offset, key.fileSize);
      return false;
    }
    previous = offset;
    offsets.push_back(offset);
  }

  pageOffsets = std::move(offsets);
  return true;
}

void savePageIndexCache(ByteWriter& out, const CacheKey& key, const std::vector<size_t>& pageOffsets) {
  writePod(out, CACHE_MAGIC);
  writePod(out, CACHE_VERSION);
  writePod(out, key.fileSize);
  writePod(out, key.viewportWidth);
  writePod(out, key.linesPerPage);
  writePod(out, key.fontId);
  writePod(out, key.screenMargin);
  writePod(out, key.paragraphAlignment);
  writePod(out, static_cast<uint32_t>(pageOffsets.size()));

  for (const size_t offset : pageOffsets) {
    writePod(out, static_cast<uint32_t>(offset));
  }
}

void encodeProgress(const int currentPage, uint8_t out[PROGRESS_SIZE]) {
  out[0] = currentPage & 0xFF;
  out[1] = (currentPage >> 8) & 0xFF;
  out[2] = 0;
  out[3] = 0;
}

int decodeProgress(const uint8_t data[PROGRESS_SIZE], const int totalPages) {
  int page = data[0] + (data[1] << 8);
  if (page >= totalPages) {
    page = totalPages - 1;
  }
  if (page < 0) {
    page = 0;
  }
  return page;
}

}  // namespace TxtPageIndex
