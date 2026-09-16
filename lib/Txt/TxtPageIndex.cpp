#include "TxtPageIndex.h"

#include <Logging.h>
#include <Memory.h>
#include <PlatformSeam.h>

#include <algorithm>

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
                const Layout& layout, TextMeasurer& measurer, std::vector<std::string>& outLines,
                size_t& nextOffset) {
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
  readPod(in, magic);
  if (magic != CACHE_MAGIC) {
    LOG_DBG("TRS", "Cache magic mismatch, rebuilding");
    return false;
  }

  uint8_t version = 0;
  readPod(in, version);
  if (version != CACHE_VERSION) {
    LOG_DBG("TRS", "Cache version mismatch (%d != %d), rebuilding", version, CACHE_VERSION);
    return false;
  }

  uint32_t fileSize = 0;
  readPod(in, fileSize);
  if (fileSize != key.fileSize) {
    LOG_DBG("TRS", "Cache file size mismatch, rebuilding");
    return false;
  }

  int32_t cachedWidth = 0;
  readPod(in, cachedWidth);
  if (cachedWidth != key.viewportWidth) {
    LOG_DBG("TRS", "Cache viewport width mismatch, rebuilding");
    return false;
  }

  int32_t cachedLines = 0;
  readPod(in, cachedLines);
  if (cachedLines != key.linesPerPage) {
    LOG_DBG("TRS", "Cache lines per page mismatch, rebuilding");
    return false;
  }

  int32_t fontId = 0;
  readPod(in, fontId);
  if (fontId != key.fontId) {
    LOG_DBG("TRS", "Cache font ID mismatch (%d != %d), rebuilding", fontId, key.fontId);
    return false;
  }

  int32_t margin = 0;
  readPod(in, margin);
  if (margin != key.screenMargin) {
    LOG_DBG("TRS", "Cache screen margin mismatch, rebuilding");
    return false;
  }

  uint8_t alignment = 0;
  readPod(in, alignment);
  if (alignment != key.paragraphAlignment) {
    LOG_DBG("TRS", "Cache paragraph alignment mismatch, rebuilding");
    return false;
  }

  uint32_t numPages = 0;
  readPod(in, numPages);

  pageOffsets.clear();
  pageOffsets.reserve(numPages);

  for (uint32_t i = 0; i < numPages; i++) {
    uint32_t offset = 0;
    readPod(in, offset);
    pageOffsets.push_back(offset);
  }

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
