# Rendering suite: GfxRenderer.cpp compiled against a HalDisplay stub that
# exposes a plain 800x480 1-bpp framebuffer, plus Bitmap.cpp header parsing,
# createBmpHeader, the BidiUtils ordering helpers and the UTF-8 decoder.
#
# The renderer's font-side dependencies (FontCacheManager, SdCardFont,
# FontDecompressor) are compiled for real so no production symbol is faked;
# the SD/heap surfaces they reach are stubbed in stubs/.

add_executable(GfxRendererTest
  GfxRendererTest.cpp
  BitmapTest.cpp
  BidiUtf8Test.cpp
  GfxRendererAllocTest.cpp
  ${REPO_ROOT}/test/support/AllocCounter.cpp
  ${REPO_ROOT}/lib/GfxRenderer/GfxRenderer.cpp
  ${REPO_ROOT}/lib/GfxRenderer/Bitmap.cpp
  ${REPO_ROOT}/lib/GfxRenderer/BitmapHelpers.cpp
  ${REPO_ROOT}/lib/GfxRenderer/FontCacheManager.cpp
  ${REPO_ROOT}/lib/EpdFont/EpdFont.cpp
  ${REPO_ROOT}/lib/EpdFont/EpdFontFamily.cpp
  ${REPO_ROOT}/lib/EpdFont/SdCardFont.cpp
  ${REPO_ROOT}/lib/EpdFont/FontDecompressor.cpp
  ${REPO_ROOT}/lib/InflateReader/InflateReader.cpp
  ${REPO_ROOT}/lib/uzlib/src/tinflate.c
  # The vendored uzlib ships no adler32/crc32; the host linker needs them
  # for tinflate.o even though nothing here reaches uzlib_uncompress_chksum.
  ${REPO_ROOT}/test/dict_common/UzlibChecksumLinkStubs.c
  ${REPO_ROOT}/lib/Memory/BuildScratch.cpp
  ${REPO_ROOT}/lib/MiniBidi/BidiUtils.cpp
  ${REPO_ROOT}/lib/MiniBidi/minibidi.c
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
)

# stubs/ first so <HalDisplay.h>, <HalStorage.h>, <BoardConfig.h>, <HalGPIO.h>,
# <Logging.h> and <Arduino.h> resolve to the host stand-ins.
target_include_directories(GfxRendererTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/test/support
  ${REPO_ROOT}/lib/GfxRenderer
  ${REPO_ROOT}/lib/EpdFont
  ${REPO_ROOT}/lib/InflateReader
  ${REPO_ROOT}/lib/uzlib/src
  ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/lib/MiniBidi
  ${REPO_ROOT}/lib/Utf8
)

set(GFX_SANDBOX_DIR ${CMAKE_CURRENT_BINARY_DIR}/sandbox)
set(GFX_CPFONT_DIR ${CMAKE_CURRENT_BINARY_DIR}/resources)
target_compile_definitions(GfxRendererTest PRIVATE
  GFX_SANDBOX_DIR="${GFX_SANDBOX_DIR}"
  GFX_CPFONT_DIR="${GFX_CPFONT_DIR}"
)

# A real .cpfont so the SD-font branches of the renderer (ensureSdCardFontReady)
# run against SdCardFont rather than being skipped. Same generator and
# deterministic output as test/sdcard_font; *.cpfont is gitignored.
find_program(CROSSPOINT_PYTHON3 python3 REQUIRED)
add_custom_command(
  OUTPUT ${GFX_CPFONT_DIR}/valid_basic.cpfont
  COMMAND ${CROSSPOINT_PYTHON3} ${REPO_ROOT}/scripts/generate_test_cpfonts.py ${GFX_CPFONT_DIR}
  DEPENDS
    ${REPO_ROOT}/scripts/generate_test_cpfonts.py
    ${REPO_ROOT}/lib/EpdFont/scripts/cpfont_version.py
  COMMENT "Generating .cpfont fixtures for the renderer suite"
  VERBATIM
)
add_custom_target(gfx_renderer_fixtures DEPENDS ${GFX_CPFONT_DIR}/valid_basic.cpfont)
add_dependencies(GfxRendererTest gfx_renderer_fixtures)

target_link_libraries(GfxRendererTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(GfxRendererTest)
