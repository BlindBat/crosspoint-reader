# Dictionary definition rendering helpers:
#   - src/util/DictHtmlPages.cpp: StarDict HTML -> XHTML normalisation and the
#     entry / retain heap, page-count and element-count gates, compiled against
#     a stub ChapterHtmlSlimParser that reads back the staged file and emits a
#     planned sequence of pages. Heap is shaped via test/support/PlatformHost.
#   - src/util/DictTextUtils.cpp: word-selection tokens, cursor rows and the
#     greedy plain-text wrap extracted from the dictionary activities.
add_executable(DictHtmlPagesTest
  DictHtmlPagesTest.cpp
  DictTextUtilsTest.cpp
  ${REPO_ROOT}/src/util/DictHtmlPages.cpp
  ${REPO_ROOT}/src/util/DictTextUtils.cpp
  ${REPO_ROOT}/test/support/PlatformHost.cpp
)

target_include_directories(DictHtmlPagesTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/test/dict_common/stubs  # stdio HalStorage + Logging
  ${REPO_ROOT}/test/support
  ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/lib/Platform
)

target_link_libraries(DictHtmlPagesTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(DictHtmlPagesTest)
