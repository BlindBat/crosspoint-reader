# Reader helpers: bookmark path/summary utilities, per-book bookmark
# persistence, crash-safe progress writes, progress byte layout, link hit
# testing, percent wrap, reader input/refresh helpers, menu and end-of-book
# row order, and QR rendering.
#
# Production sources compile against the real ArduinoJson 7.4.2 and QRCode
# 0.0.1 (the versions pinned in platformio.ini), the generated StrId table,
# and the real FreeInkUI tap-zone header; Arduino/HAL/settings/input surfaces
# are stubbed in stubs/.

include(FetchContent)

FetchContent_Declare(
  arduinojson
  GIT_REPOSITORY https://github.com/bblanchon/ArduinoJson.git
  GIT_TAG v7.4.2
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(arduinojson)

# ricmoo/QRCode ships no CMakeLists; SOURCE_SUBDIR points at src/ so
# MakeAvailable only populates the sources and qrcode.c is compiled here.
FetchContent_Declare(
  qrcode
  GIT_REPOSITORY https://github.com/ricmoo/QRCode.git
  GIT_TAG v0.0.1
  GIT_SHALLOW TRUE
  SOURCE_SUBDIR src
)
FetchContent_MakeAvailable(qrcode)

# The StrId enum comes from the checked-in YAML translations, exactly as the
# firmware build generates it.
set(I18N_GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/i18n_gen)
if(NOT EXISTS ${I18N_GEN_DIR}/I18nKeys.h)
  find_package(Python3 COMPONENTS Interpreter REQUIRED)
  file(MAKE_DIRECTORY ${I18N_GEN_DIR})
  execute_process(
    COMMAND ${Python3_EXECUTABLE} ${REPO_ROOT}/scripts/gen_i18n.py
            ${REPO_ROOT}/lib/I18n/translations ${I18N_GEN_DIR}
    WORKING_DIRECTORY ${REPO_ROOT}
    RESULT_VARIABLE I18N_GEN_RC
    OUTPUT_QUIET
  )
  if(NOT I18N_GEN_RC EQUAL 0)
    message(FATAL_ERROR "gen_i18n.py failed (rc=${I18N_GEN_RC})")
  endif()
endif()

add_executable(ReaderHelpersTest
  ReaderHelpersTest.cpp
  ${REPO_ROOT}/src/util/BookmarkUtil.cpp
  ${REPO_ROOT}/src/util/BookmarkFile.cpp
  ${REPO_ROOT}/src/util/QrUtils.cpp
  ${REPO_ROOT}/lib/Serialization/PersistableStore.cpp
  ${REPO_ROOT}/lib/Serialization/ObfuscationUtils.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
  ${qrcode_SOURCE_DIR}/src/qrcode.c
)

# stubs/ must come first so <Arduino.h>, <HalStorage.h>, <GfxRenderer.h>,
# <CrossPointSettings.h>, <Epub.h>, "MappedInputManager.h" and
# "activities/ActivityManager.h" resolve to the host stand-ins. lib/Epub
# follows only for <Epub/PageLink.h>.
target_include_directories(ReaderHelpersTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${I18N_GEN_DIR}
  ${REPO_ROOT}/src
  ${REPO_ROOT}/lib/Serialization
  ${REPO_ROOT}/lib/Epub
  ${REPO_ROOT}/lib/Utf8
  ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/freeink-sdk/libs/ui/FreeInkUI/include
  ${qrcode_SOURCE_DIR}/src
)

target_compile_definitions(ReaderHelpersTest PRIVATE
  ARDUINOJSON_ENABLE_ARDUINO_STRING=1
)

target_link_libraries(ReaderHelpersTest PRIVATE
  crosspoint_test_common
  ArduinoJson
  GTest::gtest_main
)

gtest_discover_tests(ReaderHelpersTest)
