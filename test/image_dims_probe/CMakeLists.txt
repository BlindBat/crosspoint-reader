# ImageDimsProbe: streaming JPEG marker walker / PNG IHDR reader used to get
# image dimensions from the first bytes of a compressed stream. Compiled with
# the real ImageToFramebufferDecoder::validateAndStoreDimensions so the
# overflow / dimension-cap policy is under test too.
add_executable(ImageDimsProbeTest
  ImageDimsProbeTest.cpp
  ${REPO_ROOT}/lib/Epub/Epub/converters/ImageDimsProbe.cpp
  ${REPO_ROOT}/lib/Epub/Epub/converters/ImageToFramebufferDecoder.cpp
  ${REPO_ROOT}/test/support/PlatformHost.cpp
)

target_include_directories(ImageDimsProbeTest PRIVATE
  ${REPO_ROOT}/test/image_common/stubs
  ${REPO_ROOT}/test/image_common
  ${REPO_ROOT}/lib/Epub/Epub/converters
  ${REPO_ROOT}/lib/Platform
  ${REPO_ROOT}/test/support
)

target_link_libraries(ImageDimsProbeTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(ImageDimsProbeTest)
