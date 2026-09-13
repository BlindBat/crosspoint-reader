add_executable(XtcParserTest
  XtcParserTest.cpp
  ${REPO_ROOT}/lib/Xtc/Xtc/XtcParser.cpp
)

target_include_directories(XtcParserTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/lib/Xtc
)

target_compile_definitions(XtcParserTest PRIVATE
  XTC_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
)

target_link_libraries(XtcParserTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(XtcParserTest)
