# firmware_flash suite: SD-card firmware image validation and raw OTA-slot
# flashing (src/network/FirmwareFlasher.cpp) plus the otadata boot switch
# (src/network/OtaBootSwitch.cpp), compiled against stubs/ models of the
# ESP-IDF partition API that log every erase/write. FirmwareBoardTag.cpp is the
# real board-tag scanner, built as the combined X4/X3 image.
add_executable(FirmwareFlashTest
  FirmwareFlashTest.cpp
  stubs/EspFlashStub.cpp
  ${REPO_ROOT}/src/network/FirmwareFlasher.cpp
  ${REPO_ROOT}/src/network/OtaBootSwitch.cpp
  ${REPO_ROOT}/src/network/FirmwareBoardTag.cpp
  ${REPO_ROOT}/test/support/PlatformHost.cpp
)

# stubs/ first so <HalStorage.h>, <esp_*.h> and <mbedtls/sha256.h> resolve to the
# host stand-ins; the scheduler yield comes from the real platform seam.
target_include_directories(FirmwareFlashTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/src/network
  ${REPO_ROOT}/lib/Platform
  ${REPO_ROOT}/test/support
)

target_compile_definitions(FirmwareFlashTest PRIVATE
  FREEINK_DEVICE_X4=1
)

target_link_libraries(FirmwareFlashTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(FirmwareFlashTest)
