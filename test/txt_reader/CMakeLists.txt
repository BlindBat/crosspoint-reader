# TXT reader: the pure pagination / index.bin / progress.bin unit
# (lib/Txt/TxtPageIndex) compiled against the host platform seam, plus the
# real lib/Txt/Txt.cpp (title, cover discovery, cover BMP generation) and
# FsHelpers against stdio-backed HAL stubs in stubs/.
add_executable(TxtReaderTest
  TxtReaderTest.cpp
  ${REPO_ROOT}/lib/Txt/TxtPageIndex.cpp
  ${REPO_ROOT}/lib/Txt/Txt.cpp
  ${REPO_ROOT}/lib/FsHelpers/FsHelpers.cpp
  ${REPO_ROOT}/test/support/PlatformHost.cpp
)

# stubs/ first so <HalStorage.h>, <Logging.h>, <WString.h> and
# <JpegToBmpConverter.h> resolve to the host stand-ins.
target_include_directories(TxtReaderTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/lib/Txt
  ${REPO_ROOT}/lib/FsHelpers
  ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/lib/Platform
  ${REPO_ROOT}/test/support
)

target_link_libraries(TxtReaderTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(TxtReaderTest)
