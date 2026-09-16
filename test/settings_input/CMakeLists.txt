# Settings and input suite: MappedInputManager (logical-to-physical button
# mapping, axis swap, long-press suppression, gesture meaning, hint labels),
# KeyboardLayoutSet, the clock UTC-offset codec, the Quick Resume timeout sync,
# the keyboard UTF-8 cursor helpers, CrossPointSettings derived getters, I18n
# lookup and HalTiltSensor's gesture state machine.
#
# Compiles the production sources against the same ArduinoJson / generated
# I18n setup as persistable_stores, with HalGPIO, GfxRenderer, HalFrontlight
# and the SDK IMU replaced by settable host stubs in stubs/.

include(FetchContent)

FetchContent_Declare(
  arduinojson
  GIT_REPOSITORY https://github.com/bblanchon/ArduinoJson.git
  GIT_TAG v7.4.2
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(arduinojson)

# Real StrId/Language enums and translation tables, generated from the
# checked-in YAML exactly as the firmware build does (see persistable_stores
# for why I18n.h/.cpp are compiled from copies next to the generated files).
set(I18N_GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/i18n_gen)
if(NOT EXISTS ${I18N_GEN_DIR}/I18nStrings.cpp)
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
configure_file(${REPO_ROOT}/lib/I18n/I18n.h ${I18N_GEN_DIR}/I18n.h COPYONLY)
configure_file(${REPO_ROOT}/lib/I18n/I18n.cpp ${I18N_GEN_DIR}/I18n.cpp COPYONLY)

add_executable(SettingsInputTest
  SettingsInputTest.cpp
  LinkStubs.cpp
  ${I18N_GEN_DIR}/I18nStrings.cpp
  ${I18N_GEN_DIR}/I18n.cpp
  ${REPO_ROOT}/lib/Serialization/PersistableStore.cpp
  ${REPO_ROOT}/lib/Serialization/ObfuscationUtils.cpp
  ${REPO_ROOT}/lib/KOReaderSync/KOReaderCredentialStore.cpp
  ${REPO_ROOT}/lib/hal/HalTiltSensor.cpp
  ${REPO_ROOT}/src/CrossPointSettings.cpp
  ${REPO_ROOT}/src/ReaderFontSizes.cpp
  ${REPO_ROOT}/src/MappedInputManager.cpp
  ${REPO_ROOT}/src/activities/util/KeyboardLayoutSet.cpp
)

# stubs/ first so <Arduino.h>, <HalGPIO.h>, <GfxRenderer.h>, <HalFrontlight.h>,
# <Imu.h>, <FreeInkUI.h> and <HalStorage.h> resolve to the host stand-ins;
# lib/hal follows only for the real <HalTiltSensor.h>. FreeInkUICore.h (swipe
# geometry) is the real, self-contained SDK header.
target_include_directories(SettingsInputTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${I18N_GEN_DIR}
  ${REPO_ROOT}/src
  ${REPO_ROOT}/lib/Serialization
  ${REPO_ROOT}/lib/EpdFont
  ${REPO_ROOT}/lib/KOReaderSync
  ${REPO_ROOT}/lib/Epub
  ${REPO_ROOT}/lib/hal
  ${REPO_ROOT}/freeink-sdk/libs/ui/FreeInkUI/include
)

# FREEINK_CAP_TOUCH=1 compiles the touch-capable variant of
# MappedInputManager, so wasPowerConfirmClick() and its two branches in
# wasPressed()/wasReleased() are covered rather than preprocessed away.
target_compile_definitions(SettingsInputTest PRIVATE
  ARDUINOJSON_ENABLE_ARDUINO_STRING=1
  FREEINK_CAP_TOUCH=1
)

target_link_libraries(SettingsInputTest PRIVATE
  crosspoint_test_common
  ArduinoJson
  GTest::gtest_main
)

gtest_discover_tests(SettingsInputTest)
