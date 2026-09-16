// lib/Memory/Memory.h: nothrow make_unique and ScopedCleanup.

#include <gtest/gtest.h>

#include <Memory.h>

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

}  // namespace

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
