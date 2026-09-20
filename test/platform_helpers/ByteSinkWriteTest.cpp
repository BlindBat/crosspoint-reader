// platform::writeAll: the retry loop behind CMD:SCREENSHOT, which must deliver
// the whole framebuffer over a sink that accepts only part of each write
// (FR-186).

#include <PlatformHost.h>
#include <PlatformSeam.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <numeric>
#include <vector>

namespace {

// Accepts at most `chunk` bytes per call, as the USB CDC endpoint does; a
// `chunk` of 0 stands in for a host that has stopped reading.
class ChunkedSink : public platform::ByteSink {
 public:
  explicit ChunkedSink(const size_t chunk) : chunk_(chunk) {}

  size_t write(const uint8_t byte) override { return write(&byte, 1); }

  size_t write(const uint8_t* data, const size_t length) override {
    calls++;
    const size_t n = length < chunk_ ? length : chunk_;
    received.insert(received.end(), data, data + n);
    return n;
  }

  void flush() override {}

  std::vector<uint8_t> received;
  size_t calls = 0;

 private:
  size_t chunk_;
};

class ByteSinkWriteTest : public ::testing::Test {
 protected:
  void SetUp() override {
    platform_host::setClock(1000);
    platform_host::resetCounters();
    payload.resize(48000);
    std::iota(payload.begin(), payload.end(), 0u);
  }

  void TearDown() override { platform_host::useRealClock(); }

  std::vector<uint8_t> payload;
};

}  // namespace

TEST_F(ByteSinkWriteTest, DeliversEveryByteThroughAPartiallyAcceptingSink) {
  ChunkedSink sink(5713);  // the measured first-write size over USB CDC
  EXPECT_EQ(platform::writeAll(sink, payload.data(), payload.size(), 2000), payload.size());
  EXPECT_EQ(sink.received, payload);
  EXPECT_GT(sink.calls, 1u) << "a single bulk write cannot deliver 48000 bytes";
}

TEST_F(ByteSinkWriteTest, YieldsBetweenAttemptsSoTheLoopIsNotABusySpin) {
  ChunkedSink sink(8);
  platform::writeAll(sink, payload.data(), 64, 2000);
  EXPECT_EQ(platform_host::yieldCount(), 7u);  // one per attempt but the last
}

TEST_F(ByteSinkWriteTest, GivesUpAfterTheTimeoutWhenTheSinkStopsAccepting) {
  ChunkedSink sink(0);
  // yield() does not advance the pinned clock, so the test does it instead.
  platform_host::clock() = 1000;
  struct Advancer : platform::ByteSink {
    size_t write(uint8_t) override { return 0; }
    size_t write(const uint8_t*, size_t) override {
      platform_host::clock() += 100;
      return 0;
    }
    void flush() override {}
  } stalled;
  EXPECT_EQ(platform::writeAll(stalled, payload.data(), payload.size(), 500), 0u);
  EXPECT_LE(platform_host::clock(), 1700u) << "loop ran well past the timeout";
}

TEST_F(ByteSinkWriteTest, AZeroLengthWriteNeverTouchesTheSink) {
  ChunkedSink sink(16);
  EXPECT_EQ(platform::writeAll(sink, payload.data(), 0, 2000), 0u);
  EXPECT_EQ(sink.calls, 0u);
}

TEST_F(ByteSinkWriteTest, ASinkThatTakesEverythingWritesOnce) {
  ChunkedSink sink(payload.size());
  EXPECT_EQ(platform::writeAll(sink, payload.data(), payload.size(), 2000), payload.size());
  EXPECT_EQ(sink.calls, 1u);
  EXPECT_EQ(platform_host::yieldCount(), 0u);
}
