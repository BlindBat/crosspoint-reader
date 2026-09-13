add_executable(XmlParserUtilsTest
  XmlParserUtilsTest.cpp
  XmlParserUtilsCorpusTest.cpp
  # Vendored expat, compiled with the same feature flags as the firmware
  # build (platformio.ini): general entities off, 1KB error context.
  ${REPO_ROOT}/lib/expat/xmlparse.c
  ${REPO_ROOT}/lib/expat/xmlrole.c
  ${REPO_ROOT}/lib/expat/xmltok.c
)

target_include_directories(XmlParserUtilsTest PRIVATE
  ${REPO_ROOT}/lib/XmlParserUtils
  ${REPO_ROOT}/lib/expat
)

target_link_libraries(XmlParserUtilsTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

target_compile_definitions(XmlParserUtilsTest PRIVATE
  XML_GE=0
  XML_CONTEXT_BYTES=1024
  CORPUS_XML_DIR="${REPO_ROOT}/test/corpus/xml"
)

gtest_discover_tests(XmlParserUtilsTest)
