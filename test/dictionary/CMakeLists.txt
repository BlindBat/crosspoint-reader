# StarDict Dictionary (.idx / .syn / sampled-offset sidecar) lookup suite.
# Compiles the production Dictionary plus the DictZip reader its definitions
# stream through, over the stdio HalStorage stub shared with test/dict_zip.
# DictionaryRegistry is link-stubbed (the real one needs HalFile directory
# iteration). Fixtures: scripts/generate_test_dict.py -> test/dict_common.
add_executable(DictionaryTest
  DictionaryTest.cpp
  DictRegistryLinkStub.cpp
  ${REPO_ROOT}/test/support/AllocCounter.cpp
  ${REPO_ROOT}/src/util/Dictionary.cpp
  ${REPO_ROOT}/src/util/DictZip.cpp
  ${REPO_ROOT}/lib/InflateReader/InflateReader.cpp
  ${REPO_ROOT}/lib/uzlib/src/tinflate.c
  ${REPO_ROOT}/test/dict_common/UzlibChecksumLinkStubs.c
)

target_include_directories(DictionaryTest PRIVATE
  ${REPO_ROOT}/test/dict_common/stubs
  ${REPO_ROOT}/lib/InflateReader
  ${REPO_ROOT}/lib/uzlib/src
  ${REPO_ROOT}/lib/Memory
)

# Third-party C decompressor: not held to the suite's warning bar.
set_source_files_properties(${REPO_ROOT}/lib/uzlib/src/tinflate.c PROPERTIES COMPILE_OPTIONS "-w")

target_compile_definitions(DictionaryTest PRIVATE
  DICT_RESOURCES_DIR="${REPO_ROOT}/test/dict_common/resources"
)

target_link_libraries(DictionaryTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(DictionaryTest)
