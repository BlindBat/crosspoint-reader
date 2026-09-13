# PersistableStore family: JSON persistence for settings, runtime state,
# recent books, OPDS servers, WiFi and KOReader credentials.
#
# Compiles the production stores directly against the real ArduinoJson 7.4.2
# (the version pinned in platformio.ini) plus the real generated I18n tables,
# with Arduino/HAL surfaces stubbed in stubs/.

include(FetchContent)

FetchContent_Declare(
  arduinojson
  GIT_REPOSITORY https://github.com/bblanchon/ArduinoJson.git
  GIT_TAG v7.4.2
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(arduinojson)

# The real StrId/Language enums and translation tables are generated from the
# checked-in YAML translations, exactly as the firmware build does.
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

add_executable(PersistableStoresTest
  PersistableStoresTest.cpp
  LinkStubs.cpp
  ${I18N_GEN_DIR}/I18nStrings.cpp
  ${REPO_ROOT}/lib/Serialization/PersistableStore.cpp
  ${REPO_ROOT}/lib/Serialization/ObfuscationUtils.cpp
  ${REPO_ROOT}/lib/I18n/I18n.cpp
  ${REPO_ROOT}/lib/KOReaderSync/KOReaderCredentialStore.cpp
  ${REPO_ROOT}/src/CrossPointSettings.cpp
  ${REPO_ROOT}/src/CrossPointState.cpp
  ${REPO_ROOT}/src/RecentBooksStore.cpp
  ${REPO_ROOT}/src/OpdsServerStore.cpp
  ${REPO_ROOT}/src/WifiCredentialStore.cpp
  ${REPO_ROOT}/src/ReaderFontSizes.cpp
)

# stubs/ must come first so <Arduino.h>, <HalStorage.h>, <BoardConfig.h> and
# the UI activity headers resolve to the host stand-ins.
target_include_directories(PersistableStoresTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${I18N_GEN_DIR}
  ${REPO_ROOT}/src
  ${REPO_ROOT}/lib/Serialization
  ${REPO_ROOT}/lib/I18n
  ${REPO_ROOT}/lib/EpdFont
  ${REPO_ROOT}/lib/KOReaderSync
  ${REPO_ROOT}/lib/Epub
)

target_compile_definitions(PersistableStoresTest PRIVATE
  ARDUINOJSON_ENABLE_ARDUINO_STRING=1
)

target_link_libraries(PersistableStoresTest PRIVATE
  crosspoint_test_common
  ArduinoJson
  GTest::gtest_main
)

gtest_discover_tests(PersistableStoresTest)
