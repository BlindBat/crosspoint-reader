# StarDict Dictionary (.idx / .syn / sampled-offset sidecar) lookup suite.
# Compiles the production Dictionary, the DictionaryRegistry folder scan it
# resolves through, and the DictZip reader its definitions stream through,
# over the stdio/dirent HalStorage stub shared with test/dict_zip.
# Fixtures: scripts/generate_test_dict.py -> test/dict_common, plus byte
# builders in the tests for the registry, .ifo, stemming and yield cases.
add_executable(DictionaryTest
  DictionaryTest.cpp
  DictionaryRegistryTest.cpp
  ${REPO_ROOT}/test/support/AllocCounter.cpp
  ${REPO_ROOT}/src/util/Dictionary.cpp
  ${REPO_ROOT}/src/util/DictionaryRegistry.cpp
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
