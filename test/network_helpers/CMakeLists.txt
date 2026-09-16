# Pure network helpers compiled REAL against a WString stub (FsHelpers.h
# declares Arduino String overloads):
#   - src/util/UrlUtils.cpp           (OPDS/KOSync URL building)
#   - src/network/WebPathUtils.cpp    (web file-browser paths, WebSocket START
#                                      parser, WebDAV header/mime helpers)
#   - src/network/WifiScanUtils.cpp   (scan de-duplication/sorting, RSSI bars)
# WebDAV route behaviour (path normalisation, NUL rejection, 403 on protected
# segments) is covered by test/web_dav_paths and is not repeated here.
add_executable(NetworkHelpersTest
  NetworkHelpersTest.cpp
  ${REPO_ROOT}/src/util/UrlUtils.cpp
  ${REPO_ROOT}/src/network/WebPathUtils.cpp
  ${REPO_ROOT}/src/network/WifiScanUtils.cpp
  ${REPO_ROOT}/lib/FsHelpers/FsHelpers.cpp
)

target_include_directories(NetworkHelpersTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/src
  ${REPO_ROOT}/src/network
  ${REPO_ROOT}/src/util
  ${REPO_ROOT}/lib/FsHelpers
)

target_link_libraries(NetworkHelpersTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(NetworkHelpersTest)
