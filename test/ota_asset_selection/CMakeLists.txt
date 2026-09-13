# OTA asset selection: FirmwareBoardTag (name derivation + streaming scanner)
# and OtaUpdater's selection of a GitHub release asset for the current board,
# on top of ReleaseJsonParser (covered by its own suite).
#
# The binary is built as an X4PRO board (firmware-x4pro.bin path);
# X4VariantTest.cpp additionally compiles the production sources a second
# time as the combined X4/X3 build inside a namespace to pin the plain
# firmware.bin special case. CROSSPOINT_VERSION resolves to a runtime
# variable so isUpdateNewer() can be tested across current-version values.

add_executable(OtaAssetSelectionTest
  OtaAssetSelectionTest.cpp
  X4VariantTest.cpp
  FakeHttp.cpp
  ${REPO_ROOT}/src/network/OtaUpdater.cpp
  ${REPO_ROOT}/src/network/FirmwareBoardTag.cpp
  ${REPO_ROOT}/lib/JsonParser/ReleaseJsonParser.cpp
  ${REPO_ROOT}/lib/JsonParser/StreamingJsonParser.cpp
)

target_include_directories(OtaAssetSelectionTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${REPO_ROOT}/src
  ${REPO_ROOT}/lib/JsonParser
)

target_compile_definitions(OtaAssetSelectionTest PRIVATE
  FREEINK_DEVICE_X4PRO=1
  CROSSPOINT_VERSION=g_testCurrentVersion
)

target_link_libraries(OtaAssetSelectionTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(OtaAssetSelectionTest)
