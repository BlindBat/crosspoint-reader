# User-supplied path handling in the web stack, host-compiled slice:
#   - lib/FsHelpers (normalisePath / decodeUriEscapes) compiled REAL -- the
#     primitives every web/WebDAV path funnels through, and the module the
#     upstream security fix #3353 extends;
#   - src/network/WebDAVHandler.cpp compiled REAL behind a fake WebServer and
#     a POSIX-sandbox HalStorage that records every path the handler passes
#     to the filesystem boundary.
# CrossPointWebServer.cpp itself is NOT compilable host-side (WiFi/ArduinoJson/
# WebSockets/esp_efuse/generated-HTML dependencies); its route handlers are
# covered upstream by #3353 and out of scope here.
add_executable(WebDavPathsTest
  WebDavPathsTest.cpp
  ${REPO_ROOT}/src/network/WebDAVHandler.cpp
  ${REPO_ROOT}/lib/FsHelpers/FsHelpers.cpp
)

target_include_directories(WebDavPathsTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/src
  ${REPO_ROOT}/lib/FsHelpers
)

target_link_libraries(WebDavPathsTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(WebDavPathsTest)
