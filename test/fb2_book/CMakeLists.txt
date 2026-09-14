# Full Fb2 book class over the POSIX-backed HalStorage stub: metadata parse,
# book.bin cache round-trip, cover generation via the recording converter stub.
add_executable(Fb2BookTest
  Fb2BookTest.cpp
  ${REPO_ROOT}/test/support/AllocCounter.cpp
  ${REPO_ROOT}/lib/Fb2/Fb2.cpp
  ${REPO_ROOT}/lib/Fb2/Fb2/Fb2MetadataParser.cpp
  ${REPO_ROOT}/lib/Fb2/Fb2/Fb2CoverExtractor.cpp
  ${REPO_ROOT}/lib/Fb2/Fb2/Fb2XmlEncoding.cpp
  ${REPO_ROOT}/lib/expat/xmlparse.c
  ${REPO_ROOT}/lib/expat/xmlrole.c
  ${REPO_ROOT}/lib/expat/xmltok.c
)

target_include_directories(Fb2BookTest PRIVATE
  ${REPO_ROOT}/test/fb2_common/stubs
  ${REPO_ROOT}/test/fb2_common
  ${REPO_ROOT}/test/support
  ${REPO_ROOT}/lib/expat
  ${REPO_ROOT}/lib/Fb2
  ${REPO_ROOT}/lib/Serialization
)

target_compile_definitions(Fb2BookTest PRIVATE
  XML_GE=0
  XML_CONTEXT_BYTES=1024
  FB2_FIXTURE_DIR="${REPO_ROOT}/test/fb2"
)

target_link_libraries(Fb2BookTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(Fb2BookTest)
