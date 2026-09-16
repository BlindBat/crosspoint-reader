# Line-breaking/layout suite for the typography core: ParsedText computes the
# breaks and word positions, TextBlock stores the resulting line. Both are
# compiled REAL, together with the full hyphenation stack (Hyphenator, Liang
# patterns, language registry), against a fixed-metric GfxRenderer stub
# (10 px/codepoint regular, 12 bold, 11 italic, 5 px gaps) so every expected
# break and x-position in the tests is hand-computable.
add_executable(TextBlockLayoutTest
  TextBlockLayoutTest.cpp
  TextBlockRenderTest.cpp
  LayoutLinkStubs.cpp
  ${REPO_ROOT}/test/support/AllocCounter.cpp
  ${REPO_ROOT}/lib/Epub/Epub/ParsedText.cpp
  ${REPO_ROOT}/lib/Epub/Epub/blocks/TextBlock.cpp
  ${REPO_ROOT}/lib/Epub/Epub/hyphenation/Hyphenator.cpp
  ${REPO_ROOT}/lib/Epub/Epub/hyphenation/LanguageRegistry.cpp
  ${REPO_ROOT}/lib/Epub/Epub/hyphenation/LiangHyphenation.cpp
  ${REPO_ROOT}/lib/Epub/Epub/hyphenation/HyphenationCommon.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
)

target_include_directories(TextBlockLayoutTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/test/fb2_common/stubs
  ${REPO_ROOT}/test/support
  ${REPO_ROOT}/lib
  ${REPO_ROOT}/lib/Epub
  ${REPO_ROOT}/lib/EpdFont
  ${REPO_ROOT}/lib/Utf8
  ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/lib/MiniBidi
  ${REPO_ROOT}/lib/Serialization
)

target_link_libraries(TextBlockLayoutTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(TextBlockLayoutTest)
