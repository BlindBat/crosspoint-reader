# Library helpers: file-list ordering and extension predicates (lib/FsHelpers),
# end-of-book suggestions (NextBookFinder), cache-clear dispatch (BookCacheUtils),
# list navigation (ButtonNavigator), the home menu index map and UITheme's
# cover-path / file-icon helpers.
#
# The production sources compile REAL against stubs/ (Arduino String, a
# POSIX-backed HalStorage with directory iteration, a scriptable
# MappedInputManager, call-recording Epub/Fb2/Xtc/Txt, and the HomeMenuItem
# enum). stubs/ must come first so those names win over src/ and lib/.
add_executable(LibraryHelpersTest
  LibraryHelpersTest.cpp
  ${REPO_ROOT}/lib/FsHelpers/FsHelpers.cpp
  ${REPO_ROOT}/src/util/NextBookFinder.cpp
  ${REPO_ROOT}/src/util/BookCacheUtils.cpp
  ${REPO_ROOT}/src/util/ButtonNavigator.cpp
)

target_include_directories(LibraryHelpersTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/src
  ${REPO_ROOT}/lib/FsHelpers
  ${REPO_ROOT}/lib/Memory
)

target_link_libraries(LibraryHelpersTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(LibraryHelpersTest)
