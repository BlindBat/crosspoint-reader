# serialization suite: the binary cache primitives (lib/Serialization/
# Serialization.h, BufferedFile.h) over a call-counting stdio HalFile, the
# bounded PersistableStoreBase::extractPassword overload (real
# PersistableStore.cpp + ObfuscationUtils.cpp against ArduinoJson 7.4.2), the
# OPDS UI helpers extracted into src/util/OpdsUiUtils.cpp, and
# InflateReader::initWithRing over the vendored uzlib.

include(FetchContent)

FetchContent_Declare(
  arduinojson
  GIT_REPOSITORY https://github.com/bblanchon/ArduinoJson.git
  GIT_TAG v7.4.2
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(arduinojson)

add_executable(SerializationTest
  SerializationTest.cpp
  BufferedFileTest.cpp
  OpdsUiUtilsTest.cpp
  CredentialBoundsTest.cpp
  InflateRingTest.cpp
  ${REPO_ROOT}/test/support/AllocCounter.cpp
  ${REPO_ROOT}/lib/Serialization/PersistableStore.cpp
  ${REPO_ROOT}/lib/Serialization/ObfuscationUtils.cpp
  ${REPO_ROOT}/src/util/OpdsUiUtils.cpp
  ${REPO_ROOT}/lib/InflateReader/InflateReader.cpp
  ${REPO_ROOT}/lib/uzlib/src/tinflate.c
  ${REPO_ROOT}/test/dict_common/UzlibChecksumLinkStubs.c
  ${REPO_ROOT}/lib/miniz/third_party/miniz.c
)

# stubs/ first so <Arduino.h>, <HalStorage.h>, <Print.h>, <base64.h>,
# <esp_mac.h> and <mbedtls/base64.h> resolve to the host stand-ins. lib/expat
# is on the path only for the expat.h that OpdsParser.h declares against; the
# parser itself is not compiled here. lib/miniz/third_party supplies a deflate
# compressor for the test side only (the firmware never compresses) -- it is
# built here WITHOUT MinizConfig.h, so nothing of miniz's inflate path used by
# the firmware is exercised or renamed.
target_include_directories(SerializationTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/lib/Serialization
  ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/lib/OpdsParser
  ${REPO_ROOT}/lib/expat
  ${REPO_ROOT}/lib/InflateReader
  ${REPO_ROOT}/lib/uzlib/src
  ${REPO_ROOT}/lib/miniz/third_party
  ${REPO_ROOT}/src/util
)

# Third-party C sources: not held to the suite's warning bar.
set_source_files_properties(
  ${REPO_ROOT}/lib/uzlib/src/tinflate.c
  ${REPO_ROOT}/lib/miniz/third_party/miniz.c
  PROPERTIES COMPILE_OPTIONS "-w"
)

target_compile_definitions(SerializationTest PRIVATE
  ARDUINOJSON_ENABLE_ARDUINO_STRING=1
  XML_GE=0
  XML_CONTEXT_BYTES=1024
  MINIZ_NO_STDIO
  MINIZ_NO_TIME
  MINIZ_NO_ARCHIVE_APIS
  MINIZ_NO_ARCHIVE_WRITING_APIS
  MINIZ_NO_ZLIB_COMPATIBLE_NAMES
)

target_link_libraries(SerializationTest PRIVATE
  crosspoint_test_common
  ArduinoJson
  GTest::gtest_main
)

gtest_discover_tests(SerializationTest)
