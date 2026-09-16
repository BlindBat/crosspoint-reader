# Settings persistence on the OTHER board class. PersistableStoresTest compiles
# CrossPointSettings for a plain button board; this binary compiles the same
# production sources for a touch board with a home key and an IMU, so the
# board-gated entries in SettingsList.h are pinned from both sides. The stubs,
# link stubs and generated I18n tables come from test/persistable_stores/, with
# the board capabilities selected by the CROSSPOINT_TEST_BOARD_* macros below.

include(FetchContent)

FetchContent_Declare(
  arduinojson
  GIT_REPOSITORY https://github.com/bblanchon/ArduinoJson.git
  GIT_TAG v7.4.2
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(arduinojson)

set(STORES_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../persistable_stores)

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

add_executable(SettingsBoardVariantsTest
  SettingsBoardVariantsTest.cpp
  ${REPO_ROOT}/test/support/AllocCounter.cpp
  ${STORES_DIR}/LinkStubs.cpp
  ${I18N_GEN_DIR}/I18nStrings.cpp
  ${I18N_GEN_DIR}/I18n.cpp
  ${REPO_ROOT}/lib/Serialization/PersistableStore.cpp
  ${REPO_ROOT}/lib/Serialization/ObfuscationUtils.cpp
  ${REPO_ROOT}/lib/KOReaderSync/KOReaderCredentialStore.cpp
  ${REPO_ROOT}/src/CrossPointSettings.cpp
  ${REPO_ROOT}/src/ReaderFontSizes.cpp
)

target_include_directories(SettingsBoardVariantsTest PRIVATE
  ${STORES_DIR}/stubs
  ${REPO_ROOT}/test/support
  ${I18N_GEN_DIR}
  ${REPO_ROOT}/src
  ${REPO_ROOT}/lib/Serialization
  ${REPO_ROOT}/lib/EpdFont
  ${REPO_ROOT}/lib/KOReaderSync
  ${REPO_ROOT}/lib/Epub
)

target_compile_definitions(SettingsBoardVariantsTest PRIVATE
  ARDUINOJSON_ENABLE_ARDUINO_STRING=1
  CROSSPOINT_TEST_BOARD_TOUCH=1
  CROSSPOINT_TEST_BOARD_HOME_KEY=1
  CROSSPOINT_TEST_BOARD_TILT=1
  FREEINK_CAP_TOUCH=1
)

target_link_libraries(SettingsBoardVariantsTest PRIVATE
  crosspoint_test_common
  ArduinoJson
  GTest::gtest_main
)

gtest_discover_tests(SettingsBoardVariantsTest)
