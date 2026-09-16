# Platform helper suite: the small device-facing units that are pure enough
# to run on the host once Arduino/ESP-IDF are stubbed (stubs/ first on the
# include path). Compiles the real Logging ring buffer, BuildScratch, ZipFile
# (+miniz), HalSystem panic classification, HalClock formatting, ScreenshotUtil
# and the sleep-image validators extracted from SleepActivity.
# LoggingRomTransportTest.cpp compiles Logging.cpp a second time in a namespace
# under the ROM-console transport (FR-185, Sticky).
add_executable(PlatformHelpersTest
  HostStubs.cpp
  LoggingRingTest.cpp
  LoggingRomTransportTest.cpp
  BuildScratchTest.cpp
  MemoryHelpersTest.cpp
  ZipFileCursorTest.cpp
  HalSystemTest.cpp
  HalClockTest.cpp
  ScreenshotUtilTest.cpp
  SleepImageUtilsTest.cpp
  ${REPO_ROOT}/lib/Logging/Logging.cpp
  ${REPO_ROOT}/lib/Memory/BuildScratch.cpp
  ${REPO_ROOT}/lib/ZipFile/ZipFile.cpp
  ${REPO_ROOT}/lib/miniz/src/InflateStream.cpp
  ${REPO_ROOT}/lib/miniz/src/miniz_impl.c
  ${REPO_ROOT}/lib/hal/HalSystem.cpp
  ${REPO_ROOT}/lib/hal/HalClock.cpp
  ${REPO_ROOT}/lib/FsHelpers/FsHelpers.cpp
  ${REPO_ROOT}/lib/GfxRenderer/BitmapHelpers.cpp
  ${REPO_ROOT}/src/util/ScreenshotUtil.cpp
  ${REPO_ROOT}/src/activities/boot_sleep/SleepImageUtils.cpp
)

target_include_directories(PlatformHelpersTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${REPO_ROOT}/lib/Logging
  ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/lib/ZipFile
  ${REPO_ROOT}/lib/miniz/src
  ${REPO_ROOT}/lib/hal
  ${REPO_ROOT}/lib/FsHelpers
  ${REPO_ROOT}/lib/GfxRenderer
  ${REPO_ROOT}/src
  ${REPO_ROOT}/src/util
  ${REPO_ROOT}/src/activities/boot_sleep
)

target_compile_definitions(PlatformHelpersTest PRIVATE
  ENABLE_SERIAL_LOG=1
  LOG_LEVEL=2
  CROSSPOINT_VERSION="host-test"
  PLATFORM_HELPERS_TMP_DIR="${CMAKE_CURRENT_BINARY_DIR}/tmp"
)

target_link_libraries(PlatformHelpersTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(PlatformHelpersTest)
