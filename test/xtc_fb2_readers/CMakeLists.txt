# Xtc cover/thumbnail conversion compiled from the real lib/Xtc sources against
# a stdio HalStorage stub and the host platform seam; the XTC/FB2 reader math
# units and Fb2XmlEncoding (in-tree expat, device flags) share the executable.
add_executable(XtcFb2ReadersTest
  XtcCoverThumbTest.cpp
  XtcReaderMathTest.cpp
  Fb2ReaderMathTest.cpp
  Fb2XmlEncodingTest.cpp
  ReaderProgressGuardTest.cpp
  ${REPO_ROOT}/lib/Xtc/Xtc.cpp
  ${REPO_ROOT}/lib/Xtc/Xtc/XtcParser.cpp
  ${REPO_ROOT}/lib/GfxRenderer/BitmapHelpers.cpp
  ${REPO_ROOT}/lib/Fb2/Fb2/Fb2XmlEncoding.cpp
  ${REPO_ROOT}/src/activities/reader/XtcReaderMath.cpp
  ${REPO_ROOT}/src/activities/reader/Fb2ReaderMath.cpp
  ${REPO_ROOT}/src/activities/reader/ReaderProgressGuard.cpp
  ${REPO_ROOT}/test/support/PlatformHost.cpp
  ${REPO_ROOT}/lib/expat/xmlparse.c
  ${REPO_ROOT}/lib/expat/xmlrole.c
  ${REPO_ROOT}/lib/expat/xmltok.c
)

target_include_directories(XtcFb2ReadersTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${REPO_ROOT}/test/support
  ${REPO_ROOT}/lib/Platform
  ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/lib/Xtc
  ${REPO_ROOT}/lib/GfxRenderer
  ${REPO_ROOT}/lib/Fb2
  ${REPO_ROOT}/lib/expat
  ${REPO_ROOT}/src/activities/reader
)

target_compile_definitions(XtcFb2ReadersTest PRIVATE
  XML_GE=0
  XML_CONTEXT_BYTES=1024
)

target_link_libraries(XtcFb2ReadersTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(XtcFb2ReadersTest)
