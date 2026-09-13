# ProgressMapper suite: the real KOReader xpointer <-> CrossPoint position
# mapping code (ProgressMapper + ChapterXPathResolver + htmlEntities + Utf8 +
# in-tree expat with device-parity flags), driven through an in-memory Epub
# stub and a registry-backed Section pagination-LUT stub.
add_executable(ProgressMapperTest
  ProgressMapperTest.cpp
  ${REPO_ROOT}/lib/KOReaderSync/ProgressMapper.cpp
  ${REPO_ROOT}/lib/KOReaderSync/ChapterXPathResolver.cpp
  ${REPO_ROOT}/lib/Epub/Epub/htmlEntities.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
  ${REPO_ROOT}/lib/expat/xmlparse.c
  ${REPO_ROOT}/lib/expat/xmlrole.c
  ${REPO_ROOT}/lib/expat/xmltok.c
)

target_include_directories(ProgressMapperTest PRIVATE
  ${REPO_ROOT}/test/kosync_common/stubs
  ${REPO_ROOT}/lib/KOReaderSync
  ${REPO_ROOT}/lib/Epub
  ${REPO_ROOT}/lib/Utf8
  ${REPO_ROOT}/lib/XmlParserUtils
  ${REPO_ROOT}/lib/expat
)

target_compile_definitions(ProgressMapperTest PRIVATE
  XML_GE=0
  XML_CONTEXT_BYTES=1024
)

target_link_libraries(ProgressMapperTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(ProgressMapperTest)
