# In-tree expat is compiled with the same flags the device build uses
# (-DXML_GE=0 -DXML_CONTEXT_BYTES=1024, see platformio.ini) so parser
# behavior matches the firmware, not the host's system libexpat.
add_executable(Fb2MetadataParserTest
  Fb2MetadataParserTest.cpp
  ${REPO_ROOT}/lib/Fb2/Fb2/Fb2MetadataParser.cpp
  ${REPO_ROOT}/lib/Fb2/Fb2/Fb2XmlEncoding.cpp
  ${REPO_ROOT}/lib/expat/xmlparse.c
  ${REPO_ROOT}/lib/expat/xmlrole.c
  ${REPO_ROOT}/lib/expat/xmltok.c
)

target_include_directories(Fb2MetadataParserTest PRIVATE
  ${REPO_ROOT}/test/fb2_common/stubs
  ${REPO_ROOT}/test/fb2_common
  ${REPO_ROOT}/lib/expat
  ${REPO_ROOT}/lib/Fb2
)

target_compile_definitions(Fb2MetadataParserTest PRIVATE
  XML_GE=0
  XML_CONTEXT_BYTES=1024
  FB2_FIXTURE_DIR="${REPO_ROOT}/test/fb2"
)

target_link_libraries(Fb2MetadataParserTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(Fb2MetadataParserTest)
