# EPUB Section pagination-engine suite: drives the real Section incremental
# build (startBuild/buildSomeMore/suspendBuild/loadPage) end to end over the
# real ChapterHtmlSlimParser, ParsedText layout, and Page/TextBlock
# serialization, with in-tree expat at device-parity flags. The Epub object,
# storage, and font metrics are test fakes (stubs/ + test/fb2_common/stubs);
# the image pipeline is link-stubbed (fixtures are text-only).
add_executable(EpubSectionTest
  EpubSectionBuildTest.cpp
  EpubSectionAnchorTest.cpp
  EpubSectionResumeTest.cpp
  EpubSectionCacheTest.cpp
  SectionLinkStubs.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../support/AllocCounter.cpp
  ${REPO_ROOT}/lib/Epub/Epub/Section.cpp
  ${REPO_ROOT}/lib/Epub/Epub/Page.cpp
  ${REPO_ROOT}/lib/Epub/Epub/ParsedText.cpp
  ${REPO_ROOT}/lib/Epub/Epub/blocks/TextBlock.cpp
  ${REPO_ROOT}/lib/Epub/Epub/parsers/ChapterHtmlSlimParser.cpp
  ${REPO_ROOT}/lib/Epub/Epub/css/CssParser.cpp
  ${REPO_ROOT}/lib/Epub/Epub/converters/ImageDimsProbe.cpp
  ${REPO_ROOT}/lib/Epub/Epub/htmlEntities.cpp
  ${REPO_ROOT}/lib/Epub/Epub/hyphenation/HyphenationCommon.cpp
  ${REPO_ROOT}/lib/Epub/Epub/hyphenation/Hyphenator.cpp
  ${REPO_ROOT}/lib/Epub/Epub/hyphenation/LanguageRegistry.cpp
  ${REPO_ROOT}/lib/Epub/Epub/hyphenation/LiangHyphenation.cpp
  ${REPO_ROOT}/lib/MiniBidi/BidiUtils.cpp
  ${REPO_ROOT}/lib/MiniBidi/minibidi.c
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
  ${REPO_ROOT}/lib/expat/xmlparse.c
  ${REPO_ROOT}/lib/expat/xmlrole.c
  ${REPO_ROOT}/lib/expat/xmltok.c
  ${REPO_ROOT}/test/support/PlatformHost.cpp
)

target_include_directories(EpubSectionTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/test/fb2_common/stubs  # fixed-metric GfxRenderer fake
  ${CMAKE_CURRENT_SOURCE_DIR}/../support
  ${REPO_ROOT}/lib/expat
  ${REPO_ROOT}/lib/Epub
  ${REPO_ROOT}/lib/Epub/Epub
  ${REPO_ROOT}/lib/Epub/Epub/parsers
  ${REPO_ROOT}/lib/Epub/Epub/css
  ${REPO_ROOT}/lib/EpdFont
  ${REPO_ROOT}/lib/Utf8
  ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/lib/MiniBidi
  ${REPO_ROOT}/lib/XmlParserUtils
  ${REPO_ROOT}/lib/Serialization
  ${REPO_ROOT}/lib/Platform
  ${REPO_ROOT}/test/support
)

target_compile_definitions(EpubSectionTest PRIVATE
  XML_GE=0
  XML_CONTEXT_BYTES=1024
)

target_link_libraries(EpubSectionTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(EpubSectionTest)
