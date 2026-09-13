cmake_minimum_required(VERSION 3.16)
project(crosspoint_reader_tests C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
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

# Sanitizer build for the host suites (bin/run-tests --asan).
# Applies to the test executables and the production sources they compile;
# gtest itself (FetchContent) stays uninstrumented, which is fine for ASan.
option(CROSSPOINT_SANITIZE "Build tests with AddressSanitizer + UndefinedBehaviorSanitizer" OFF)
if(CROSSPOINT_SANITIZE)
  set(CROSSPOINT_SANITIZE_FLAGS
    -fsanitize=address,undefined
    -fno-omit-frame-pointer
    -fno-sanitize-recover=undefined
  )
  target_compile_options(crosspoint_test_common INTERFACE ${CROSSPOINT_SANITIZE_FLAGS})
  target_link_options(crosspoint_test_common INTERFACE ${CROSSPOINT_SANITIZE_FLAGS})
endif()

enable_testing()
include(GoogleTest)

add_subdirectory(streaming_json_parser)
add_subdirectory(release_json_parser)
add_subdirectory(differential_rounding)
add_subdirectory(hyphenation_eval)
add_subdirectory(utf8_compose)
add_subdirectory(opds_filename)
add_subdirectory(html_to_plain_text)
add_subdirectory(minibidi_arabic)
add_subdirectory(combining_marks)
add_subdirectory(credential_integrity)
add_subdirectory(xml_parser_utils)
add_subdirectory(token_boundary)
add_subdirectory(font_cache_manager)
add_subdirectory(css_parser)
add_subdirectory(chapter_html_slim_parser)
add_subdirectory(ligature_guard)
add_subdirectory(page_link)
add_subdirectory(fb2_metadata_parser)
add_subdirectory(fb2_section_parser)
add_subdirectory(fb2_cover_extractor)
add_subdirectory(fb2_book)
add_subdirectory(fb2_section_cache)
add_subdirectory(fb2_filename)
add_subdirectory(alloc_guards)
add_subdirectory(zip_file)
add_subdirectory(book_metadata_cache)
add_subdirectory(opds_parser)
add_subdirectory(xtc_parser)
add_subdirectory(epub_section)
add_subdirectory(image_dims_probe)
add_subdirectory(direct_pixel_writer)
add_subdirectory(png_decode)
add_subdirectory(jpeg_to_bmp)
