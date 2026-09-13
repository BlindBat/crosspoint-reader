add_executable(StreamingJsonParserTest
  StreamingJsonParserTest.cpp
  StreamingJsonParserCorpusTest.cpp
  ${REPO_ROOT}/lib/JsonParser/StreamingJsonParser.cpp
)

target_include_directories(StreamingJsonParserTest PRIVATE
  ${REPO_ROOT}/lib/JsonParser
)

target_link_libraries(StreamingJsonParserTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

target_compile_definitions(StreamingJsonParserTest PRIVATE
  CORPUS_JSON_DIR="${REPO_ROOT}/test/corpus/json"
)

gtest_discover_tests(StreamingJsonParserTest)
