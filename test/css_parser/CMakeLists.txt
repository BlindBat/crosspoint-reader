add_executable(CssParserTest
  CssParserTest.cpp
  CssParserCorpusTest.cpp
  ${REPO_ROOT}/lib/Epub/Epub/css/CssParser.cpp
)

target_include_directories(CssParserTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/lib/Epub/Epub/css
  ${REPO_ROOT}/lib/Memory
)

target_link_libraries(CssParserTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

target_compile_definitions(CssParserTest PRIVATE
  CORPUS_CSS_DIR="${REPO_ROOT}/test/corpus/css"
)

gtest_discover_tests(CssParserTest)
