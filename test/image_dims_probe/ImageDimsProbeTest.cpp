// Host tests for ImageDimsProbe (lib/Epub/Epub/converters/ImageDimsProbe.cpp):
// the streaming JPEG-marker / PNG-IHDR walker that extracts image dimensions
// from the first bytes of a compressed stream, plus the shared
// ImageToFramebufferDecoder::validateAndStoreDimensions() size policy
// (positive, <= INT16_MAX per axis, <= 8 MP total).
//
// All inputs are synthetic byte streams built in-test: the probe never touches
// entropy-coded data, so real encoder output is not needed to exercise every
// state transition.

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "ImageDimsProbe.h"

namespace {

using Bytes = std::vector<uint8_t>;

void pushBE16(Bytes& b, const uint16_t v) {
  b.push_back(static_cast<uint8_t>(v >> 8));
  b.push_back(static_cast<uint8_t>(v & 0xFF));
}

void pushBE32(Bytes& b, const uint32_t v) {
  b.push_back(static_cast<uint8_t>(v >> 24));
  b.push_back(static_cast<uint8_t>(v >> 16));
  b.push_back(static_cast<uint8_t>(v >> 8));
  b.push_back(static_cast<uint8_t>(v));
}

// A marker segment: FF <marker> <len(payload)+2> <payload>.
void pushSegment(Bytes& b, const uint8_t marker, const Bytes& payload) {
  b.push_back(0xFF);
  b.push_back(marker);
  pushBE16(b, static_cast<uint16_t>(payload.size() + 2));
  b.insert(b.end(), payload.begin(), payload.end());
}

// Realistic single-component SOF payload: precision, height, width, 1 comp.
Bytes sofPayload(const uint16_t width, const uint16_t height) {
  Bytes p;
  p.push_back(8);  // precision
  pushBE16(p, height);
  pushBE16(p, width);
  p.push_back(1);     // component count
  p.push_back(1);     // component id
  p.push_back(0x11);  // sampling factors
  p.push_back(0);     // quant table
  return p;
}

// SOI + APP0(JFIF) + a 1KB APP1 (EXIF-sized) + DQT + SOF<marker> + DHT + SOS.
Bytes jpegStream(const uint16_t width, const uint16_t height, const uint8_t sofMarker = 0xC0) {
  Bytes b = {0xFF, 0xD8};
  Bytes app0 = {'J', 'F', 'I', 'F', 0, 1, 1, 0, 0, 1, 0, 1, 0, 0};
  pushSegment(b, 0xE0, app0);
  Bytes app1(1024);
  for (size_t i = 0; i < app1.size(); i++) app1[i] = static_cast<uint8_t>(i * 7);
  pushSegment(b, 0xE1, app1);
  Bytes dqt(65);
  dqt[0] = 0;
  for (size_t i = 1; i < dqt.size(); i++) dqt[i] = 16;
  pushSegment(b, 0xDB, dqt);
  pushSegment(b, sofMarker, sofPayload(width, height));
  Bytes dht = {0x00, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 42};
  pushSegment(b, 0xC4, dht);
  Bytes sos = {1, 1, 0x00, 0, 63, 0};
  pushSegment(b, 0xDA, sos);
  b.insert(b.end(), {0x12, 0x34, 0x56, 0xFF, 0xD9});  // entropy data + EOI
  return b;
}

// PNG signature + IHDR up to and including the CRC.
Bytes pngStream(const uint32_t width, const uint32_t height, const uint32_t ihdrLenField = 13) {
  Bytes b = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
  pushBE32(b, ihdrLenField);
  b.insert(b.end(), {'I', 'H', 'D', 'R'});
  pushBE32(b, width);
  pushBE32(b, height);
  b.insert(b.end(), {8, 0, 0, 0, 0});  // depth, color, comp, filter, interlace
  pushBE32(b, 0xDEADBEEF);             // CRC (never validated by the probe)
  return b;
}

// Feed the whole stream; returns dims validity.
bool probe(const Bytes& stream, ImageDimensions& dims) {
  ImageDimsProbe p;
  p.write(stream.data(), stream.size());
  return p.getDimensions(dims);
}

bool probeByteByByte(const Bytes& stream, ImageDimensions& dims) {
  ImageDimsProbe p;
  for (const uint8_t b : stream) {
    if (p.write(b) == 0) break;
  }
  return p.getDimensions(dims);
}

bool probeChunked(const Bytes& stream, const size_t chunkSize, ImageDimensions& dims) {
  ImageDimsProbe p;
  for (size_t off = 0; off < stream.size(); off += chunkSize) {
    const size_t n = std::min(chunkSize, stream.size() - off);
    if (p.write(stream.data() + off, n) < n) break;
  }
  return p.getDimensions(dims);
}

// ---------------------------------------------------------------------------
// Happy paths
// ---------------------------------------------------------------------------

TEST(ImageDimsProbe, PngDimensions) {
  ImageDimensions dims{};
  ASSERT_TRUE(probe(pngStream(800, 600), dims));
  EXPECT_EQ(dims.width, 800);
  EXPECT_EQ(dims.height, 600);
}

TEST(ImageDimsProbe, PngByteByByteMatchesWhole) {
  ImageDimensions whole{}, single{};
  ASSERT_TRUE(probe(pngStream(123, 45), whole));
  ASSERT_TRUE(probeByteByByte(pngStream(123, 45), single));
  EXPECT_EQ(single.width, whole.width);
  EXPECT_EQ(single.height, whole.height);
}

TEST(ImageDimsProbe, PngEarlyStopsBeforeConsumingWholeBuffer) {
  // The probe has everything it needs at IHDR byte 23; a longer buffer must
  // come back as a short write (the zip layer's polite early stop).
  Bytes stream = pngStream(64, 32);
  stream.resize(stream.size() + 512, 0xEE);
  ImageDimsProbe p;
  const size_t written = p.write(stream.data(), stream.size());
  EXPECT_LT(written, stream.size());
  ImageDimensions dims{};
  ASSERT_TRUE(p.getDimensions(dims));
  EXPECT_EQ(dims.width, 64);
  EXPECT_EQ(dims.height, 32);
}

TEST(ImageDimsProbe, PngIgnoresIhdrLengthField) {
  // The probe reads IHDR fields at fixed offsets and never interprets the
  // 4-byte length field (documented in ImageDimsProbe.h). Pin that.
  ImageDimensions dims{};
  ASSERT_TRUE(probe(pngStream(31, 17, /*ihdrLenField=*/0xFFFFFFFF), dims));
  EXPECT_EQ(dims.width, 31);
  EXPECT_EQ(dims.height, 17);
}

TEST(ImageDimsProbe, JpegBaselineDimensions) {
  ImageDimensions dims{};
  ASSERT_TRUE(probe(jpegStream(480, 640), dims));
  EXPECT_EQ(dims.width, 480);
  EXPECT_EQ(dims.height, 640);
}

TEST(ImageDimsProbe, JpegProgressiveSof2Dimensions) {
  ImageDimensions dims{};
  ASSERT_TRUE(probe(jpegStream(320, 240, /*sofMarker=*/0xC2), dims));
  EXPECT_EQ(dims.width, 320);
  EXPECT_EQ(dims.height, 240);
}

TEST(ImageDimsProbe, JpegExtendedSequentialSof1Dimensions) {
  ImageDimensions dims{};
  ASSERT_TRUE(probe(jpegStream(100, 200, /*sofMarker=*/0xC1), dims));
  EXPECT_EQ(dims.width, 100);
  EXPECT_EQ(dims.height, 200);
}

TEST(ImageDimsProbe, JpegByteByByteAndChunkedMatchWhole) {
  const Bytes stream = jpegStream(1234, 987);
  ImageDimensions whole{}, single{}, chunk3{}, chunk7{};
  ASSERT_TRUE(probe(stream, whole));
  ASSERT_TRUE(probeByteByByte(stream, single));
  ASSERT_TRUE(probeChunked(stream, 3, chunk3));
  ASSERT_TRUE(probeChunked(stream, 7, chunk7));
  EXPECT_EQ(single.width, 1234);
  EXPECT_EQ(chunk3.width, 1234);
  EXPECT_EQ(chunk7.width, 1234);
  EXPECT_EQ(single.height, 987);
  EXPECT_EQ(chunk3.height, 987);
  EXPECT_EQ(chunk7.height, 987);
}

TEST(ImageDimsProbe, JpegEarlyStopsAtSofBeforeScanData) {
  const Bytes stream = jpegStream(60, 40);
  ImageDimsProbe p;
  const size_t written = p.write(stream.data(), stream.size());
  // Must stop inside the SOF segment, long before the DHT/SOS/entropy tail.
  EXPECT_LT(written, stream.size() - 30);
  ImageDimensions dims{};
  ASSERT_TRUE(p.getDimensions(dims));
  EXPECT_EQ(dims.width, 60);
  EXPECT_EQ(dims.height, 40);
}

TEST(ImageDimsProbe, JpegDhtInCnRangeIsNotTreatedAsSof) {
  // 0xC4 (DHT) sits inside the 0xCn SOF range but is not a frame header. If
  // the probe misread it as SOF, it would report the DHT payload as dims.
  Bytes b = {0xFF, 0xD8};
  Bytes dht = {0x00, 1, 2, 3, 4, 5, 6, 7, 8};
  pushSegment(b, 0xC4, dht);
  pushSegment(b, 0xC0, sofPayload(77, 55));
  ImageDimensions dims{};
  ASSERT_TRUE(probe(b, dims));
  EXPECT_EQ(dims.width, 77);
  EXPECT_EQ(dims.height, 55);
}

TEST(ImageDimsProbe, JpegFillBytesAndRestartMarkersTolerated) {
  Bytes b = {0xFF, 0xD8};
  b.insert(b.end(), {0xFF, 0xFF, 0xFF});  // fill bytes before the marker type
  b.insert(b.end(), {0xFF, 0x01});        // TEM: standalone, no length
  b.insert(b.end(), {0xFF, 0xD0});        // RST0: standalone
  pushSegment(b, 0xC0, sofPayload(21, 12));
  ImageDimensions dims{};
  ASSERT_TRUE(probe(b, dims));
  EXPECT_EQ(dims.width, 21);
  EXPECT_EQ(dims.height, 12);
}

TEST(ImageDimsProbe, JpegEmptyCommentSegmentTolerated) {
  Bytes b = {0xFF, 0xD8};
  pushSegment(b, 0xFE, {});  // COM with segLen == 2 (no payload)
  pushSegment(b, 0xC0, sofPayload(10, 20));
  ImageDimensions dims{};
  ASSERT_TRUE(probe(b, dims));
  EXPECT_EQ(dims.width, 10);
  EXPECT_EQ(dims.height, 20);
}

// ---------------------------------------------------------------------------
// Size policy (validateAndStoreDimensions)
// ---------------------------------------------------------------------------

TEST(ImageDimsProbe, PngZeroDimensionsRejected) {
  ImageDimensions dims{};
  EXPECT_FALSE(probe(pngStream(0, 100), dims));
  EXPECT_FALSE(probe(pngStream(100, 0), dims));
}

TEST(ImageDimsProbe, JpegZeroDimensionsRejected) {
  ImageDimensions dims{};
  EXPECT_FALSE(probe(jpegStream(0, 100), dims));
  // height == 0 is the deferred-DNL form; the probe treats it as unusable.
  EXPECT_FALSE(probe(jpegStream(100, 0), dims));
}

TEST(ImageDimsProbe, PngDimensionOverInt16MaxRejected) {
  ImageDimensions dims{};
  EXPECT_FALSE(probe(pngStream(32768, 8), dims));
  EXPECT_FALSE(probe(pngStream(8, 32768), dims));
  // A 4-byte IHDR width that would truncate to a plausible small uint16.
  EXPECT_FALSE(probe(pngStream(0x00010002, 8), dims));
  EXPECT_FALSE(probe(pngStream(0x80000000u, 8), dims));
}

TEST(ImageDimsProbe, JpegDimensionOverInt16MaxRejected) {
  ImageDimensions dims{};
  EXPECT_FALSE(probe(jpegStream(65500, 8), dims));
  EXPECT_FALSE(probe(jpegStream(8, 65500), dims));
}

TEST(ImageDimsProbe, PixelCountOverEightMegapixelsRejected) {
  ImageDimensions dims{};
  // Each axis fits int16 but the product exceeds MAX_SOURCE_PIXELS (8388608).
  EXPECT_FALSE(probe(jpegStream(20000, 20000), dims));
  EXPECT_FALSE(probe(pngStream(2896, 2897), dims));  // 8390512 pixels
}

TEST(ImageDimsProbe, PixelCountAtEightMegapixelBoundaryAccepted) {
  ImageDimensions dims{};
  ASSERT_TRUE(probe(pngStream(2048, 4096), dims));  // exactly 8388608
  EXPECT_EQ(dims.width, 2048);
  EXPECT_EQ(dims.height, 4096);
  ASSERT_TRUE(probe(jpegStream(2896, 2896), dims));  // 8386816 < cap
  EXPECT_EQ(dims.width, 2896);
}

// ---------------------------------------------------------------------------
// Malformed inputs
// ---------------------------------------------------------------------------

TEST(ImageDimsProbe, WrongMagicRejectedOnFirstByte) {
  ImageDimsProbe p;
  const Bytes gif = {'G', 'I', 'F', '8', '9', 'a'};
  EXPECT_EQ(p.write(gif.data(), gif.size()), 0u);
  ImageDimensions dims{};
  EXPECT_FALSE(p.getDimensions(dims));
}

TEST(ImageDimsProbe, EmptyInputYieldsNoDimensions) {
  ImageDimsProbe p;
  ImageDimensions dims{};
  EXPECT_FALSE(p.getDimensions(dims));
}

TEST(ImageDimsProbe, PngBadSignatureByteRejected) {
  Bytes stream = pngStream(10, 10);
  stream[3] = 'X';  // corrupt the signature
  ImageDimensions dims{};
  EXPECT_FALSE(probe(stream, dims));
}

TEST(ImageDimsProbe, PngWrongIhdrTypeRejected) {
  Bytes stream = pngStream(10, 10);
  stream[12] = 'i';  // "iHDR"
  ImageDimensions dims{};
  EXPECT_FALSE(probe(stream, dims));
}

TEST(ImageDimsProbe, JpegBadSoiSecondByteRejected) {
  const Bytes b = {0xFF, 0xC0, 0x00, 0x0B};
  ImageDimensions dims{};
  EXPECT_FALSE(probe(b, dims));
}

TEST(ImageDimsProbe, JpegSosBeforeSofRejected) {
  Bytes b = {0xFF, 0xD8};
  pushSegment(b, 0xDA, {1, 1, 0, 0, 63, 0});
  ImageDimensions dims{};
  EXPECT_FALSE(probe(b, dims));
}

TEST(ImageDimsProbe, JpegEoiBeforeSofRejected) {
  const Bytes b = {0xFF, 0xD8, 0xFF, 0xD9};
  ImageDimensions dims{};
  EXPECT_FALSE(probe(b, dims));
}

TEST(ImageDimsProbe, JpegSegmentLengthBelowTwoRejected) {
  // Length field 0x0001 is impossible (it includes its own two bytes).
  const Bytes b = {0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x01};
  ImageDimensions dims{};
  EXPECT_FALSE(probe(b, dims));
}

TEST(ImageDimsProbe, JpegSofSegmentTooShortForDimsRejected) {
  // A SOF frame header needs >= 7 bytes of length to carry the dims.
  const Bytes b = {0xFF, 0xD8, 0xFF, 0xC0, 0x00, 0x06, 8, 0, 16, 0, 16};
  ImageDimensions dims{};
  EXPECT_FALSE(probe(b, dims));
}

TEST(ImageDimsProbe, WritesAfterDoneAreRefused) {
  ImageDimsProbe p;
  const Bytes stream = pngStream(12, 34);
  p.write(stream.data(), stream.size());
  const uint8_t extra = 0x55;
  EXPECT_EQ(p.write(&extra, 1), 0u);
  EXPECT_EQ(p.write(extra), 0u);
  ImageDimensions dims{};
  ASSERT_TRUE(p.getDimensions(dims));
  EXPECT_EQ(dims.width, 12);
  EXPECT_EQ(dims.height, 34);
}

TEST(ImageDimsProbe, WritesAfterFailureAreRefused) {
  ImageDimsProbe p;
  const uint8_t bad = 'Z';
  EXPECT_EQ(p.write(&bad, 1), 0u);
  // Even a subsequently valid stream must not resurrect the probe.
  const Bytes good = pngStream(5, 5);
  EXPECT_EQ(p.write(good.data(), good.size()), 0u);
  ImageDimensions dims{};
  EXPECT_FALSE(p.getDimensions(dims));
}

// ---------------------------------------------------------------------------
// Truncation sweeps: for every prefix of a valid stream the probe must either
// report the correct dims (once the header was fully seen) or none at all —
// and never read out of bounds (ASan-checked in the sanitizer build).
// ---------------------------------------------------------------------------

void truncationSweep(const Bytes& stream, const int16_t expectW, const int16_t expectH) {
  bool seenDims = false;
  for (size_t len = 0; len <= stream.size(); len++) {
    ImageDimsProbe p;
    p.write(stream.data(), len);
    ImageDimensions dims{};
    const bool got = p.getDimensions(dims);
    if (seenDims) {
      // Once a prefix yields dimensions, every longer prefix must too.
      ASSERT_TRUE(got) << "prefix " << len << " lost previously-found dims";
    }
    if (got) {
      seenDims = true;
      ASSERT_EQ(dims.width, expectW) << "prefix " << len;
      ASSERT_EQ(dims.height, expectH) << "prefix " << len;
    }
  }
  ASSERT_TRUE(seenDims);
}

TEST(ImageDimsProbe, PngTruncationSweepSafeAndMonotonic) { truncationSweep(pngStream(300, 200), 300, 200); }

TEST(ImageDimsProbe, JpegTruncationSweepSafeAndMonotonic) { truncationSweep(jpegStream(300, 200), 300, 200); }

}  // namespace
