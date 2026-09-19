# SdCardFont suite: .cpfont v4 loading/validation, prewarm glyph residency,
# overflow loading, kern/ligature mini tables, advance table, and
# SdCardFontRegistry discovery — over a POSIX HalStorage stub.
#
# Fixtures are generated at build time (deterministic script output);
# *.cpfont is gitignored, so they cannot be committed.
add_executable(SdCardFontTest
  SdCardFontTest.cpp
  ${REPO_ROOT}/lib/EpdFont/SdCardFont.cpp
  ${REPO_ROOT}/lib/EpdFont/SdCardFontRegistry.cpp
  ${REPO_ROOT}/lib/EpdFont/EpdFont.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
  ${REPO_ROOT}/test/support/PlatformHost.cpp
)

target_include_directories(SdCardFontTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/lib/EpdFont
  ${REPO_ROOT}/lib/Utf8
  ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/lib/Platform
  ${REPO_ROOT}/test/support
)

set(CPFONT_RESOURCES_DIR ${CMAKE_CURRENT_BINARY_DIR}/resources)
set(CPFONT_SANDBOX_DIR ${CMAKE_CURRENT_BINARY_DIR}/sandbox)

target_compile_definitions(SdCardFontTest PRIVATE
  CPFONT_RESOURCES_DIR="${CPFONT_RESOURCES_DIR}"
  CPFONT_SANDBOX_DIR="${CPFONT_SANDBOX_DIR}"
)

find_program(CROSSPOINT_PYTHON3 python3 REQUIRED)

add_custom_command(
  OUTPUT ${CPFONT_RESOURCES_DIR}/valid_basic.cpfont
  COMMAND ${CROSSPOINT_PYTHON3} ${REPO_ROOT}/scripts/generate_test_cpfonts.py ${CPFONT_RESOURCES_DIR}
  DEPENDS
    ${REPO_ROOT}/scripts/generate_test_cpfonts.py
    ${REPO_ROOT}/lib/EpdFont/scripts/cpfont_version.py
  COMMENT "Generating .cpfont test fixtures"
  VERBATIM
)
add_custom_target(sdcard_font_fixtures DEPENDS ${CPFONT_RESOURCES_DIR}/valid_basic.cpfont)
add_dependencies(SdCardFontTest sdcard_font_fixtures)

target_link_libraries(SdCardFontTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(SdCardFontTest)
