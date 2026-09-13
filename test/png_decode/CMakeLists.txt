# PngToBmpConverter compiled against the real miniz InflateStream and the
# real GfxRenderer dithering helpers; only the device HAL (storage, display,
# FreeRTOS yields, logging) is stubbed. Fixtures come from
# scripts/generate_test_images.py and are committed under resources/.
add_executable(PngDecodeTest
  PngDecodeTest.cpp
  ${REPO_ROOT}/lib/PngToBmpConverter/PngToBmpConverter.cpp
  ${REPO_ROOT}/lib/GfxRenderer/BitmapHelpers.cpp
  ${REPO_ROOT}/lib/miniz/src/InflateStream.cpp
  ${REPO_ROOT}/lib/miniz/src/miniz_impl.c
  ${REPO_ROOT}/lib/Memory/BuildScratch.cpp
)

target_include_directories(PngDecodeTest PRIVATE
  ${REPO_ROOT}/test/image_common/stubs
  ${REPO_ROOT}/test/image_common
  ${REPO_ROOT}/lib/PngToBmpConverter
  ${REPO_ROOT}/lib/GfxRenderer
  ${REPO_ROOT}/lib/miniz/src
  ${REPO_ROOT}/lib/Memory
)

target_compile_definitions(PngDecodeTest PRIVATE
  PNG_RESOURCES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/resources"
)

target_link_libraries(PngDecodeTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
  ${CMAKE_DL_LIBS}
)

gtest_discover_tests(PngDecodeTest)
