add_executable(ReleaseJsonParserTest
  ReleaseJsonParserTest.cpp
  ReleaseJsonParserCorpusTest.cpp
  ${REPO_ROOT}/lib/JsonParser/ReleaseJsonParser.cpp
  ${REPO_ROOT}/lib/JsonParser/StreamingJsonParser.cpp
)

target_include_directories(ReleaseJsonParserTest PRIVATE
  ${REPO_ROOT}/lib/JsonParser
)

target_link_libraries(ReleaseJsonParserTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

target_compile_definitions(ReleaseJsonParserTest PRIVATE
  CORPUS_JSON_DIR="${REPO_ROOT}/test/corpus/json"
)

gtest_discover_tests(ReleaseJsonParserTest)
