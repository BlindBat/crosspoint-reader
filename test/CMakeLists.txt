cmake_minimum_required(VERSION 3.16)
project(crosspoint_reader_tests C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Sanitizer build for the host suites (bin/run-tests --asan).
#
# Declared before the FetchContent calls and applied directory-wide, so
# googletest and JPEGDEC are instrumented too. libc++'s ASan container
# annotations only agree when the code that grows a std::vector and the code
# that reads it were built the same way; an uninstrumented gtest reports a false
# heap-buffer-overflow from inside its own static test registration.
option(CROSSPOINT_SANITIZE "Build tests with AddressSanitizer + UndefinedBehaviorSanitizer" OFF)
if(CROSSPOINT_SANITIZE)
  set(CROSSPOINT_SANITIZE_FLAGS
    -fsanitize=address,undefined
    -fno-omit-frame-pointer
    -fno-sanitize-recover=undefined
  )
  add_compile_options(${CROSSPOINT_SANITIZE_FLAGS})
  add_link_options(${CROSSPOINT_SANITIZE_FLAGS})
endif()

include(FetchContent)

FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG v1.17.0
)

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

set(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")

# JPEGDEC at the same pinned commit the firmware builds against
# (platformio.ini lib_deps), with CrossPoint's scripts/jpegdec_patches/
# applied by the same git-apply idempotency rules as the PlatformIO
# pre-build hook. SOURCE_SUBDIR points at src/ (no CMakeLists there), so
# MakeAvailable only populates the sources; test/jpeg_to_bmp compiles
# JPEGDEC.cpp directly.
FetchContent_Declare(
  jpegdec
  GIT_REPOSITORY https://github.com/bitbank2/JPEGDEC.git
  GIT_TAG 86282979224c8a32fd51e091ed5a35b0c699a52b
  SOURCE_SUBDIR src
  PATCH_COMMAND bash ${REPO_ROOT}/scripts/apply_jpegdec_patches.sh <SOURCE_DIR>
                ${REPO_ROOT}/scripts/jpegdec_patches
)
FetchContent_MakeAvailable(jpegdec)

add_library(crosspoint_test_common INTERFACE)
target_include_directories(crosspoint_test_common INTERFACE
  ${REPO_ROOT}
  ${REPO_ROOT}/lib
)
target_compile_options(crosspoint_test_common INTERFACE
  -Wall
  -Wextra
  -pedantic
)

enable_testing()
include(GoogleTest)

# Suites. CROSSPOINT_SUITE_DIRS (semicolon list, from bin/run-tests --filter)
# restricts the configure to the named directories so an in-progress suite
# elsewhere in the tree cannot break a filtered build.
set(CROSSPOINT_SUITE_DIRS "" CACHE STRING "Only configure these suite directories (empty = all)")
function(crosspoint_suite dir)
  if(CROSSPOINT_SUITE_DIRS STREQUAL "" OR dir IN_LIST CROSSPOINT_SUITE_DIRS)
    add_subdirectory(${dir})
  endif()
endfunction()

crosspoint_suite(streaming_json_parser)
crosspoint_suite(release_json_parser)
crosspoint_suite(differential_rounding)
crosspoint_suite(hyphenation_eval)
crosspoint_suite(utf8_compose)
crosspoint_suite(opds_filename)
crosspoint_suite(html_to_plain_text)
crosspoint_suite(minibidi_arabic)
crosspoint_suite(combining_marks)
crosspoint_suite(credential_integrity)
crosspoint_suite(xml_parser_utils)
crosspoint_suite(token_boundary)
crosspoint_suite(font_cache_manager)
crosspoint_suite(css_parser)
crosspoint_suite(chapter_html_slim_parser)
crosspoint_suite(ligature_guard)
crosspoint_suite(page_link)
crosspoint_suite(fb2_metadata_parser)
crosspoint_suite(fb2_section_parser)
crosspoint_suite(fb2_cover_extractor)
crosspoint_suite(fb2_book)
crosspoint_suite(fb2_section_cache)
crosspoint_suite(fb2_filename)
crosspoint_suite(alloc_guards)
crosspoint_suite(zip_file)
crosspoint_suite(book_metadata_cache)
crosspoint_suite(opds_parser)
crosspoint_suite(xtc_parser)
crosspoint_suite(epub_section)
crosspoint_suite(image_dims_probe)
crosspoint_suite(direct_pixel_writer)
crosspoint_suite(png_decode)
crosspoint_suite(jpeg_to_bmp)
crosspoint_suite(chapter_xpath_resolver)
crosspoint_suite(progress_mapper)
crosspoint_suite(dictionary)
crosspoint_suite(dict_zip)
crosspoint_suite(persistable_stores)
crosspoint_suite(settings_board_variants)
crosspoint_suite(ota_asset_selection)
crosspoint_suite(font_decompressor)
crosspoint_suite(sdcard_font)
crosspoint_suite(text_block_layout)
crosspoint_suite(web_dav_paths)
crosspoint_suite(content_opf_parser)
crosspoint_suite(epub_orchestration)
crosspoint_suite(txt_reader)
crosspoint_suite(reader_helpers)
crosspoint_suite(library_helpers)
crosspoint_suite(gfx_renderer)
crosspoint_suite(font_system)
crosspoint_suite(settings_input)
crosspoint_suite(network_helpers)
crosspoint_suite(firmware_flash)
crosspoint_suite(serialization)
crosspoint_suite(kosync_client)
crosspoint_suite(dict_html_pages)
crosspoint_suite(platform_helpers)
crosspoint_suite(xtc_fb2_readers)
