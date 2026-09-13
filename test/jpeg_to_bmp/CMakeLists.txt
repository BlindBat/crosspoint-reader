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

# Third-party TU: JPEGDEC is not held to this repo's warning bar. Two UBSan
# checks are disabled for this ONE file (ASan and every other UBSan check
# stay on) because upstream JPEGDEC trips them on every decode, valid input
# included -- both are pre-existing upstream defects, benign-by-accident on
# the device, documented in the QA report:
#   * alignment: with ALLOWS_UNALIGNED (set for x86-64/arm64 hosts AND the
#     ESP32 Arduino build) JPEGDEC deliberately does misaligned uint16/32
#     loads (INTELSHORT/MOTOSHORT in JPEGDEC.h).
#   * shift: jpeg.inl:1115 computes `cc >> (iBitNum-5)` with iBitNum < 5
#     during Huffman table init -> negative shift exponent (UB; RISC-V/arm
#     mask the count, so the following == 0x1f test happens to work out).
set_source_files_properties(${jpegdec_SOURCE_DIR}/src/JPEGDEC.cpp
  PROPERTIES COMPILE_OPTIONS "-w;-fno-sanitize=alignment;-fno-sanitize=shift")

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
