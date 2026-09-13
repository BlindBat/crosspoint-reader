# DirectPixelWriter / DirectCacheWriter: the branch-free framebuffer and
# cache-band pixel writers on the image render hot path, plus the
# DitherUtils / BitmapHelpers quantizers and error-diffusion ditherers that
# produce the 2-bit values they pack. The writers are header-only and are
# compiled against the minimal GfxRenderer stub in stubs/ (the real header
# drags in fonts and the display HAL); BitmapHelpers.cpp is the real
# production source.
add_executable(DirectPixelWriterTest
  DirectPixelWriterTest.cpp
  ${REPO_ROOT}/lib/GfxRenderer/BitmapHelpers.cpp
)

target_include_directories(DirectPixelWriterTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/test/image_common/stubs
  ${REPO_ROOT}/lib/Epub/Epub/converters
)

target_link_libraries(DirectPixelWriterTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(DirectPixelWriterTest)
