#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Pure pagination, index.bin and progress.bin logic for the TXT reader.
// TxtReaderActivity supplies the renderer, file access and cache I/O through
// the small interfaces below, so this unit also compiles in the host test suite.
namespace TxtPageIndex {

constexpr size_t CHUNK_SIZE = 8 * 1024;
constexpr uint32_t CACHE_MAGIC = 0x54585449;  // "TXTI"
constexpr uint8_t CACHE_VERSION = 3;          // Increment when cache format changes
constexpr size_t PROGRESS_SIZE = 4;           // u16 page + two zero bytes

// Text width oracle for the reader font (GfxRenderer::getTextAdvanceX on the device).
class TextMeasurer {
 public:
  virtual ~TextMeasurer() = default;
  // Called once per chunk before layout with the NUL-terminated chunk text
  // (SD-card font glyph prewarm on the device).
  virtual void prepare(const char* /*chunkText*/) {}
  virtual int advanceX(const char* text) = 0;
};

// Random-access source of the TXT bytes (Txt::readContent on the device).
class ContentReader {
 public:
  virtual ~ContentReader() = default;
  virtual bool readContent(uint8_t* buffer, size_t offset, size_t length) = 0;
};

struct Layout {
  int viewportWidth = 0;
  int linesPerPage = 1;
};

// Lays out one page from `chunk` (chunkSize bytes read from file position `offset`).
// Lines split on LF (a trailing CR is dropped) and wrap at the last space or a
// UTF-8 boundary. Returns true if at least one line was produced; nextOffset is
// the file position where the following page starts.
bool layoutPage(const uint8_t* chunk, size_t chunkSize, size_t offset, size_t fileSize, const Layout& layout,
                TextMeasurer& measurer, std::vector<std::string>& outLines, size_t& nextOffset);

// Reads the chunk at `offset` and lays out one page. Returns false at EOF, on
// OOM or on a read failure.
bool loadPageAtOffset(ContentReader& content, size_t fileSize, size_t offset, const Layout& layout,
                      TextMeasurer& measurer, std::vector<std::string>& outLines, size_t& nextOffset);

// Fills pageOffsets with the start offset of every page and returns the page count.
int buildPageIndex(ContentReader& content, size_t fileSize, const Layout& layout, TextMeasurer& measurer,
                   std::vector<size_t>& pageOffsets);

// Settings that key index.bin; any mismatch means the index must be rebuilt.
struct CacheKey {
  uint32_t fileSize = 0;
  int32_t viewportWidth = 0;
  int32_t linesPerPage = 0;
  int32_t fontId = 0;
  int32_t screenMargin = 0;
  uint8_t paragraphAlignment = 0;
};

// Byte-level view of index.bin (HalFile on the device, a buffer on the host).
class ByteReader {
 public:
  virtual ~ByteReader() = default;
  // Returns the number of bytes actually read.
  virtual size_t read(void* buffer, size_t count) = 0;
  // Total size of the underlying file.
  virtual size_t size() = 0;
};

class ByteWriter {
 public:
  virtual ~ByteWriter() = default;
  virtual size_t write(const void* buffer, size_t count) = 0;
};

// Parses index.bin. Returns false when the header does not match `key`, leaving
// pageOffsets untouched; on success pageOffsets holds the stored page starts.
bool loadPageIndexCache(ByteReader& in, const CacheKey& key, std::vector<size_t>& pageOffsets);

void savePageIndexCache(ByteWriter& out, const CacheKey& key, const std::vector<size_t>& pageOffsets);

// progress.bin payload: u16 little-endian page followed by two zero bytes.
void encodeProgress(int currentPage, uint8_t out[PROGRESS_SIZE]);

// Decodes a progress.bin payload and clamps the page into [0, totalPages - 1].
int decodeProgress(const uint8_t data[PROGRESS_SIZE], int totalPages);

}  // namespace TxtPageIndex
