add_executable(ZipFileTest
  ZipFileTest.cpp
  ${REPO_ROOT}/lib/ZipFile/ZipFile.cpp
  ${REPO_ROOT}/lib/miniz/src/InflateStream.cpp
  ${REPO_ROOT}/test/support/PlatformHost.cpp
  ${REPO_ROOT}/lib/miniz/src/miniz_impl.c
  ${REPO_ROOT}/lib/Memory/BuildScratch.cpp
)

target_include_directories(ZipFileTest PRIVATE
  ${REPO_ROOT}/test/support
  ${REPO_ROOT}/lib/Platform
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/lib/ZipFile
  ${REPO_ROOT}/lib/miniz/src
  ${REPO_ROOT}/lib/Memory
)

target_compile_definitions(ZipFileTest PRIVATE
  ZIP_RESOURCES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/resources"
)

target_link_libraries(ZipFileTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
  ${CMAKE_DL_LIBS}
)

gtest_discover_tests(ZipFileTest)
