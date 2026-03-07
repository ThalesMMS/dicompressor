cmake_minimum_required(VERSION 3.24)

project(
  transcode_htj2k
  VERSION 0.1.0
  DESCRIPTION "High-performance DICOM to HTJ2K transcoder"
  LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(HTJ2K_TRANSCODER_ENABLE_IPO "Enable interprocedural optimization in release builds" ON)
option(HTJ2K_TRANSCODER_ENABLE_SANITIZERS "Enable sanitizers" OFF)
option(HTJ2K_TRANSCODER_WARNINGS_AS_ERRORS "Treat warnings as errors" OFF)
option(HTJ2K_TRANSCODER_BUILD_TESTS "Build tests" ON)
option(HTJ2K_TRANSCODER_BUILD_BENCH "Build benchmark executable" ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(cmake/PreventInSourceBuild.cmake)
include(cmake/Warnings.cmake)
include(cmake/Sanitizers.cmake)

find_package(openjph REQUIRED CONFIG)
find_package(OpenJPEG REQUIRED CONFIG)
find_package(DCMTK REQUIRED CONFIG)
find_package(Threads REQUIRED)

add_library(transcode_core)
add_library(transcode::core ALIAS transcode_core)

target_sources(
  transcode_core
  PRIVATE
    app/cli.cpp
    codec/htj2k_encoder.cpp
    codec/openjpeg_decoder.cpp
    codec/openjph_source_decoder.cpp
    codec/source_decoder.cpp
    core/file_discovery.cpp
    core/job_scheduler.cpp
    core/patient_zipper.cpp
    core/report.cpp
    core/transcoder.cpp
    dicom/dicom_metadata.cpp
    dicom/dicom_reader.cpp
    dicom/dicom_writer.cpp
    dicom/photometric.cpp
    dicom/pixel_sequence_builder.cpp
    dicom/transfer_syntax.cpp
    platform/fsync.cpp
    util/fs.cpp
    util/logging.cpp
    util/memory_pool.cpp
    util/thread_pool.cpp
    util/timer.cpp
    third_party/miniz/miniz.c
    third_party/miniz/miniz_tdef.c
    third_party/miniz/miniz_tinfl.c
    third_party/miniz/miniz_zip.c)

target_include_directories(
  transcode_core
  PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}")

target_compile_definitions(
  transcode_core
  PUBLIC
    HTJ2K_TRANSCODER_VERSION="${PROJECT_VERSION}")

target_link_libraries(
  transcode_core
  PUBLIC
    DCMTK::dcmdata
    DCMTK::ofstd
    DCMTK::oflog
    openjph
    openjp2
    Threads::Threads
  PRIVATE
    DCMTK::dcmjpeg
    DCMTK::dcmjpls)

htj2k_transcoder_set_warnings(transcode_core)
htj2k_transcoder_enable_sanitizers(transcode_core)

include(CheckIPOSupported)
if(HTJ2K_TRANSCODER_ENABLE_IPO)
  check_ipo_supported(RESULT ipo_supported OUTPUT ipo_error)
  if(ipo_supported)
    set_property(TARGET transcode_core PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
    set_property(TARGET transcode_core PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO TRUE)
  else()
    message(STATUS "IPO/LTO unavailable: ${ipo_error}")
  endif()
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(
    transcode_core
    PRIVATE
      $<$<CONFIG:Release>:-O3>
      $<$<CONFIG:RelWithDebInfo>:-O3>
      $<$<CONFIG:Debug>:-O0>)
endif()

add_executable(transcode_htj2k app/main.cpp)
target_link_libraries(transcode_htj2k PRIVATE transcode::core)
htj2k_transcoder_set_warnings(transcode_htj2k)
htj2k_transcoder_enable_sanitizers(transcode_htj2k)

if(HTJ2K_TRANSCODER_BUILD_TESTS)
  enable_testing()
  add_executable(
    transcode_tests
    tests/test_main.cpp
    tests/test_registry.cpp
    tests/test_cli.cpp
    tests/test_discovery.cpp
    tests/test_encoder.cpp
    tests/test_report.cpp
    tests/test_transcoder.cpp
    tests/test_zip.cpp)
  target_link_libraries(transcode_tests PRIVATE transcode::core)
  target_compile_definitions(transcode_tests PRIVATE HTJ2K_TRANSCODER_ENABLE_TESTS=1)
  htj2k_transcoder_set_warnings(transcode_tests)
  htj2k_transcoder_enable_sanitizers(transcode_tests)
  add_test(NAME transcode_tests COMMAND transcode_tests)
endif()

if(HTJ2K_TRANSCODER_BUILD_BENCH)
  add_executable(transcode_bench bench/main.cpp)
  target_link_libraries(transcode_bench PRIVATE transcode::core)
  htj2k_transcoder_set_warnings(transcode_bench)
  htj2k_transcoder_enable_sanitizers(transcode_bench)
endif()
