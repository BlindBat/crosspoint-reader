// lib/Memory/Memory.h: nothrow make_unique and ScopedCleanup.

#include <Memory.h>
#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <string>

namespace {

struct Tracked {
  static inline int alive = 0;
  int a;
  std::string b;
  Tracked(int a, std::string b) : a(a), b(std::move(b)) { alive++; }
  ~Tracked() { alive--; }
};

// Default-constructible counterpart, for the array helpers.
struct Counted {
  static inline int alive = 0;
  Counted() { alive++; }
  ~Counted() { alive--; }
};

}  // namespace

// ForOverwriteReturnsNullWhenTheByteCountOverflows relies on the non-throwing array new handing
// back nullptr for an unsatisfiable size. AddressSanitizer aborts on such a request unless it is
// told it may return null; that setting only affects allocation failure, not error detection.
extern "C" const char* __asan_default_options() { return "allocator_may_return_null=1"; }

TEST(MemoryHelpersTest, MakeUniqueNoThrowForwardsConstructorArguments) {
  Tracked::alive = 0;
  {
    auto obj = makeUniqueNoThrow<Tracked>(7, "seven");
    ASSERT_NE(obj, nullptr);
    EXPECT_EQ(obj->a, 7);
    EXPECT_EQ(obj->b, "seven");
    EXPECT_EQ(Tracked::alive, 1);
  }
  EXPECT_EQ(Tracked::alive, 0);
}

TEST(MemoryHelpersTest, MakeUniqueNoThrowArrayIsValueInitialized) {
  auto buf = makeUniqueNoThrow<uint8_t[]>(64);
  ASSERT_NE(buf, nullptr);
  for (size_t i = 0; i < 64; i++) EXPECT_EQ(buf[i], 0) << i;
  buf[63] = 0xAB;
  EXPECT_EQ(buf.get()[63], 0xAB);
}

TEST(MemoryHelpersTest, MakeUniqueNoThrowArrayOfObjectsDefaultConstructs) {
  auto arr = makeUniqueNoThrow<std::string[]>(3);
  ASSERT_NE(arr, nullptr);
  EXPECT_TRUE(arr[0].empty());
  arr[2] = "z";
  EXPECT_EQ(arr[2], "z");
}

TEST(MemoryHelpersTest, ZeroLengthArrayIsStillAllocated) {
  auto buf = makeUniqueNoThrow<uint32_t[]>(0);
  EXPECT_NE(buf, nullptr);
}

// makeUniqueNoThrowForOverwrite: same nothrow contract, but no value-initialisation. The
// contents are indeterminate by design, so the only thing that can be pinned about them is that
// the whole requested extent is writable and reads back what the caller wrote.
TEST(MemoryHelpersTest, ForOverwriteArrayIsWritableAcrossItsWholeExtent) {
  constexpr size_t kCount = 4096;
  auto buf = makeUniqueNoThrowForOverwrite<uint8_t[]>(kCount);
  ASSERT_NE(buf, nullptr);
  for (size_t i = 0; i < kCount; i++) buf[i] = static_cast<uint8_t>(i * 31 + 7);
  for (size_t i = 0; i < kCount; i++) ASSERT_EQ(buf[i], static_cast<uint8_t>(i * 31 + 7)) << i;
}

TEST(MemoryHelpersTest, ForOverwriteZeroLengthArrayIsStillAllocated) {
  auto buf = makeUniqueNoThrowForOverwrite<uint32_t[]>(0);
  EXPECT_NE(buf, nullptr);
}

// An element count whose byte size is not representable must yield nullptr, not a wrapped-around
// under-allocation. The non-throwing array new is required to return null in that case.
TEST(MemoryHelpersTest, ForOverwriteReturnsNullWhenTheByteCountOverflows) {
  auto buf = makeUniqueNoThrowForOverwrite<uint32_t[]>(SIZE_MAX / 2 + 1);
  EXPECT_EQ(buf, nullptr);
}

// Default-initialisation still runs the default constructor for class types, and the array
// deleter still runs every destructor at scope exit (no leak, no double free).
TEST(MemoryHelpersTest, ForOverwriteConstructsAndDestroysClassElements) {
  Counted::alive = 0;
  {
    auto arr = makeUniqueNoThrowForOverwrite<Counted[]>(5);
    ASSERT_NE(arr, nullptr);
    EXPECT_EQ(Counted::alive, 5);
  }
  EXPECT_EQ(Counted::alive, 0);
}

TEST(MemoryHelpersTest, ScopedCleanupRunsOnlyAtScopeExit) {
  int runs = 0;
  {
    ScopedCleanup cleanup{[&runs] { runs++; }};
    EXPECT_EQ(runs, 0);
  }
  EXPECT_EQ(runs, 1);
}

TEST(MemoryHelpersTest, ScopedCleanupsRunInReverseDeclarationOrder) {
  std::string order;
  {
    ScopedCleanup first{[&order] { order += '1'; }};
    ScopedCleanup second{[&order] { order += '2'; }};
  }
  EXPECT_EQ(order, "21");
}

TEST(MemoryHelpersTest, ScopedCleanupRunsOnEarlyReturn) {
  int runs = 0;
  auto fn = [&runs](bool early) {
    ScopedCleanup cleanup{[&runs] { runs++; }};
    if (early) return 1;
    return 2;
  };
  EXPECT_EQ(fn(true), 1);
  EXPECT_EQ(runs, 1);
  EXPECT_EQ(fn(false), 2);
  EXPECT_EQ(runs, 2);
}
