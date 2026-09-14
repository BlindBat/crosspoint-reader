# JpegToBmpConverter compiled against the real JPEGDEC library (FetchContent,
# same pinned commit + patches as the firmware build; see test/CMakeLists.txt)
# and the real GfxRenderer dithering helpers. Only the device HAL is stubbed.
# Fixtures come from scripts/generate_test_images.py; truncation and
# dimension-lying variants are byte surgery on those bases inside the test.
add_executable(JpegToBmpTest
  JpegToBmpTest.cpp
  ${REPO_ROOT}/lib/JpegToBmpConverter/JpegToBmpConverter.cpp
  ${REPO_ROOT}/lib/GfxRenderer/BitmapHelpers.cpp
  ${jpegdec_SOURCE_DIR}/src/JPEGDEC.cpp
)

# Third-party TU: JPEGDEC is not held to this repo's warning bar. ONE UBSan
# check is disabled for this ONE file (ASan and every other UBSan check --
# including shift -- stay on):
#   * alignment: with ALLOWS_UNALIGNED (set for x86-64/arm64 hosts AND the
#     ESP32 Arduino build) JPEGDEC DELIBERATELY does misaligned uint16/32
#     loads (INTELSHORT/MOTOSHORT in JPEGDEC.h) as a vendor design choice
#     for cores that support them. Do not "fix" these upstream reads; this
#     opt-out is the permanent documentation of that design.
# The former shift opt-out (negative shift exponent in Huffman table init,
# hit on every decode) is fixed by scripts/jpegdec_patches/0003-*.patch, so
# the shift check now runs -- a regressed patch fails the ASan/UBSan suite.
# JPEGDEC gets memset/memcpy/NULL through Arduino.h on device; off-device,
# <cstring> is force-included so the TU builds against libstdc++ as well as libc++.
set_source_files_properties(${jpegdec_SOURCE_DIR}/src/JPEGDEC.cpp
  PROPERTIES COMPILE_OPTIONS "-w;-fno-sanitize=alignment;-include;cstring")

target_include_directories(JpegToBmpTest PRIVATE
  ${REPO_ROOT}/test/image_common/stubs
  ${REPO_ROOT}/test/image_common
  ${REPO_ROOT}/lib/JpegToBmpConverter
  ${REPO_ROOT}/lib/GfxRenderer
  ${REPO_ROOT}/lib/Memory
  ${jpegdec_SOURCE_DIR}/src
)

target_compile_definitions(JpegToBmpTest PRIVATE
  JPEG_RESOURCES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/resources"
)

target_link_libraries(JpegToBmpTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
  ${CMAKE_DL_LIBS}
)

gtest_discover_tests(JpegToBmpTest)
