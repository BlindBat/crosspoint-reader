# Font-system suite: EpdFontFamily style fallback, SdCardFontManager,
# FontInstaller, SdCardFontSystem, ReaderFontSizes, the SdCardFont mini-arena
# retry/hysteresis, and the fonts.json manifest parser extracted from
# FontDownloadActivity.
#
# The renderer is replaced by a registration-recording stub (stubs/GfxRenderer.h);
# SdCardFont/SdCardFontRegistry run for real over a POSIX-backed HalStorage stub.
# FontInstaller.cpp and SdCardFontSystem.cpp include "CrossPointSettings.h" by
# quoted name, which the preprocessor resolves next to the including file first,
# so they are compiled from copies placed beside the stub settings header.

include(FetchContent)

FetchContent_Declare(
  arduinojson
  GIT_REPOSITORY https://github.com/bblanchon/ArduinoJson.git
  GIT_TAG v7.4.2
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(arduinojson)

set(FONT_SYSTEM_GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/gen)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/stubs/CrossPointSettings.h
               ${FONT_SYSTEM_GEN_DIR}/CrossPointSettings.h COPYONLY)
configure_file(${REPO_ROOT}/src/FontInstaller.cpp ${FONT_SYSTEM_GEN_DIR}/FontInstaller.cpp COPYONLY)
configure_file(${REPO_ROOT}/src/SdCardFontSystem.cpp ${FONT_SYSTEM_GEN_DIR}/SdCardFontSystem.cpp COPYONLY)

add_executable(FontSystemTest
  FontSystemTest.cpp
  FontInstallerTest.cpp
  FontArenaTest.cpp
  FontManifestTest.cpp
  ${FONT_SYSTEM_GEN_DIR}/FontInstaller.cpp
  ${FONT_SYSTEM_GEN_DIR}/SdCardFontSystem.cpp
  ${REPO_ROOT}/src/ReaderFontSizes.cpp
  ${REPO_ROOT}/src/util/FontManifest.cpp
  ${REPO_ROOT}/lib/EpdFont/EpdFont.cpp
  ${REPO_ROOT}/lib/EpdFont/EpdFontFamily.cpp
  ${REPO_ROOT}/lib/EpdFont/SdCardFont.cpp
  ${REPO_ROOT}/lib/EpdFont/SdCardFontRegistry.cpp
  ${REPO_ROOT}/lib/EpdFont/SdCardFontManager.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
  ${REPO_ROOT}/test/support/PlatformHost.cpp
)

# stubs/ first so <GfxRenderer.h>, <HalStorage.h>, <Logging.h> and <Arduino.h>
# resolve to the host stand-ins.
target_include_directories(FontSystemTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/src
  ${REPO_ROOT}/lib/EpdFont
  ${REPO_ROOT}/lib/Utf8
  ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/lib/Platform
  ${REPO_ROOT}/test/support
)

set(FONT_SYSTEM_RESOURCES_DIR ${CMAKE_CURRENT_BINARY_DIR}/resources)
set(FONT_SYSTEM_SANDBOX_DIR ${CMAKE_CURRENT_BINARY_DIR}/sandbox)
target_compile_definitions(FontSystemTest PRIVATE
  FONT_SYSTEM_RESOURCES_DIR="${FONT_SYSTEM_RESOURCES_DIR}"
  FONT_SYSTEM_SANDBOX_DIR="${FONT_SYSTEM_SANDBOX_DIR}"
)

find_program(CROSSPOINT_PYTHON3 python3 REQUIRED)
add_custom_command(
  OUTPUT ${FONT_SYSTEM_RESOURCES_DIR}/valid_basic.cpfont
         ${FONT_SYSTEM_RESOURCES_DIR}/cjk.cpfont
         ${FONT_SYSTEM_RESOURCES_DIR}/wide.cpfont
  COMMAND ${CROSSPOINT_PYTHON3} ${REPO_ROOT}/scripts/generate_test_cpfonts.py ${FONT_SYSTEM_RESOURCES_DIR}
  COMMAND ${CROSSPOINT_PYTHON3} ${CMAKE_CURRENT_SOURCE_DIR}/gen_font_fixtures.py ${FONT_SYSTEM_RESOURCES_DIR}
  DEPENDS
    ${REPO_ROOT}/scripts/generate_test_cpfonts.py
    ${CMAKE_CURRENT_SOURCE_DIR}/gen_font_fixtures.py
    ${REPO_ROOT}/lib/EpdFont/scripts/cpfont_version.py
  COMMENT "Generating font-system .cpfont fixtures"
  VERBATIM
)
add_custom_target(font_system_fixtures
  DEPENDS ${FONT_SYSTEM_RESOURCES_DIR}/valid_basic.cpfont
          ${FONT_SYSTEM_RESOURCES_DIR}/cjk.cpfont
          ${FONT_SYSTEM_RESOURCES_DIR}/wide.cpfont)
add_dependencies(FontSystemTest font_system_fixtures)

target_link_libraries(FontSystemTest PRIVATE
  crosspoint_test_common
  ArduinoJson
  GTest::gtest_main
)

gtest_discover_tests(FontSystemTest)
