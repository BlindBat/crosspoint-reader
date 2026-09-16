# KOSync client suite: the real KOReaderDocumentId / KOReaderSyncClient /
# KOReaderCredentialStore / SmartSyncDecision / KOReaderServerUrl code plus the
# real ProgressMapper (for generateXPath's byte fallback and the anchor-id
# attribute scanner), compiled against ArduinoJson 7.4.2 (platformio.ini pin)
# with the SDK/HAL surfaces stubbed in stubs/: an in-memory HalStorage, a
# canned-response SecureHttpClient, a real-digest MD5Builder, and the host
# platform seam for the TLS heap gate.

include(FetchContent)

FetchContent_Declare(
  arduinojson
  GIT_REPOSITORY https://github.com/bblanchon/ArduinoJson.git
  GIT_TAG v7.4.2
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(arduinojson)

add_executable(KosyncClientTest
  KosyncClientTest.cpp
  ${REPO_ROOT}/test/support/PlatformHost.cpp
  ${REPO_ROOT}/lib/KOReaderSync/KOReaderDocumentId.cpp
  ${REPO_ROOT}/lib/KOReaderSync/KOReaderSyncClient.cpp
  ${REPO_ROOT}/lib/KOReaderSync/KOReaderCredentialStore.cpp
  ${REPO_ROOT}/lib/KOReaderSync/SmartSyncDecision.cpp
  ${REPO_ROOT}/lib/KOReaderSync/ProgressMapper.cpp
  ${REPO_ROOT}/lib/KOReaderSync/ChapterXPathResolver.cpp
  ${REPO_ROOT}/lib/Serialization/PersistableStore.cpp
  ${REPO_ROOT}/lib/Serialization/ObfuscationUtils.cpp
  ${REPO_ROOT}/lib/Epub/Epub/htmlEntities.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
  ${REPO_ROOT}/lib/expat/xmlparse.c
  ${REPO_ROOT}/lib/expat/xmlrole.c
  ${REPO_ROOT}/lib/expat/xmltok.c
)

# stubs/ first so <Arduino.h>, <HalStorage.h>, <SecureHttpClient.h>,
# <MD5Builder.h> and <Logging.h> resolve to the host stand-ins; the shared
# kosync_common stubs supply <Epub.h>, <GfxRenderer.h>, <Print.h> and
# <Epub/Section.h> ahead of lib/Epub.
target_include_directories(KosyncClientTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/test/kosync_common/stubs
  ${REPO_ROOT}/test/support
  ${REPO_ROOT}/lib/Platform
  ${REPO_ROOT}/lib/KOReaderSync
  ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/lib/Serialization
  ${REPO_ROOT}/lib/Epub
  ${REPO_ROOT}/lib/Utf8
  ${REPO_ROOT}/lib/XmlParserUtils
  ${REPO_ROOT}/lib/expat
)

target_compile_definitions(KosyncClientTest PRIVATE
  ARDUINOJSON_ENABLE_ARDUINO_STRING=1
  XML_GE=0
  XML_CONTEXT_BYTES=1024
)

# gtest is fetched uninstrumented (test/CMakeLists.txt), so under -fsanitize=address
# libc++ annotates the shared std::vector of registered tests here but not inside
# gtest's own translation units. Past ~50 TEST_F registrations the two disagree and
# static registration aborts with a false heap-buffer-overflow. Turning the libc++
# container annotations off for this target makes both sides agree; every other ASan
# check (including plain heap overflows) stays on.
if(CROSSPOINT_SANITIZE)
  target_compile_definitions(KosyncClientTest PRIVATE __SANITIZER_DISABLE_CONTAINER_OVERFLOW__)
endif()

target_link_libraries(KosyncClientTest PRIVATE
  crosspoint_test_common
  ArduinoJson
  GTest::gtest_main
)

gtest_discover_tests(KosyncClientTest)
