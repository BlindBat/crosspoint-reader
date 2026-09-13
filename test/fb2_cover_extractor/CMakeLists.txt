# In-tree expat compiled with device-parity flags (see fb2_metadata_parser).
# JpegToBmpConverter is replaced by a recording pass-through stub so the
# base64 decode is assertable byte-for-byte.
add_executable(Fb2CoverExtractorTest
  Fb2CoverExtractorTest.cpp
  ${REPO_ROOT}/lib/Fb2/Fb2/Fb2CoverExtractor.cpp
  ${REPO_ROOT}/lib/expat/xmlparse.c
  ${REPO_ROOT}/lib/expat/xmlrole.c
  ${REPO_ROOT}/lib/expat/xmltok.c
)

target_include_directories(Fb2CoverExtractorTest PRIVATE
  ${REPO_ROOT}/test/fb2_common/stubs
  ${REPO_ROOT}/test/fb2_common
  ${REPO_ROOT}/lib/expat
  ${REPO_ROOT}/lib/Fb2
)

target_compile_definitions(Fb2CoverExtractorTest PRIVATE
  XML_GE=0
  XML_CONTEXT_BYTES=1024
  FB2_FIXTURE_DIR="${REPO_ROOT}/test/fb2"
)

target_link_libraries(Fb2CoverExtractorTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(Fb2CoverExtractorTest)
