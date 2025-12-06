cmake_minimum_required(VERSION 3.28...3.30)

find_package(Qt6 REQUIRED Widgets Multimedia Network Concurrent)
find_package(FFmpeg REQUIRED COMPONENTS avcodec avformat avutil swresample swscale)

# Build whisper.cpp as a static library (for transcription only)
set(WHISPER_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_SERVER OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(WHISPER_SDL2 OFF CACHE BOOL "" FORCE)
set(WHISPER_CURL OFF CACHE BOOL "" FORCE)
set(WHISPER_ALL_WARNINGS OFF CACHE BOOL "" FORCE)
set(WHISPER_FATAL_WARNINGS OFF CACHE BOOL "" FORCE)
set(GGML_FATAL_WARNINGS OFF CACHE BOOL "" FORCE)
set(GGML_ALL_WARNINGS OFF CACHE BOOL "" FORCE)

# Disable CoreML and BLAS to avoid compatibility issues
set(WHISPER_COREML OFF CACHE BOOL "" FORCE)
set(GGML_BLAS OFF CACHE BOOL "" FORCE)
set(GGML_ACCELERATE OFF CACHE BOOL "" FORCE)
set(GGML_METAL OFF CACHE BOOL "" FORCE)

# Temporarily remove -Werror from compile flags for whisper subdirectory
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-error -Wno-unreachable-code -Wno-unguarded-availability-new -Wno-quoted-include-in-framework-header")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-error -Wno-unreachable-code -Wno-unguarded-availability-new -Wno-quoted-include-in-framework-header")
set(CMAKE_OBJC_FLAGS "${CMAKE_OBJC_FLAGS} -Wno-error -Wno-objc-protocol-method-implementation -Wno-quoted-include-in-framework-header")

add_subdirectory("${CMAKE_SOURCE_DIR}/deps/whisper.cpp" "${CMAKE_BINARY_DIR}/deps/whisper.cpp" EXCLUDE_FROM_ALL)

# llama.cpp built via ExternalProject to avoid ggml symbol conflicts with whisper.cpp
include("${CMAKE_SOURCE_DIR}/cmake/BuildLlamaCpp.cmake")

# =============================================================================
# Main plugin
# =============================================================================
add_library(obs-ai-toolkit MODULE)
add_library(OBS::obs-ai-toolkit ALIAS obs-ai-toolkit)

if(NOT TARGET OBS::qt-wrappers)
  add_subdirectory("${CMAKE_SOURCE_DIR}/shared/qt/wrappers" "${CMAKE_BINARY_DIR}/shared/qt/wrappers")
endif()

# Plugin sources - using LlamaRunner (subprocess) instead of linked llama
target_sources(
  obs-ai-toolkit
  PRIVATE
    obs-ai-toolkit.c
    ai-health-dock.cpp
    ai-health-dock.hpp
    quality-advisor.cpp
    quality-advisor.hpp
    video-analyzer-dialog.cpp
    video-analyzer-dialog.hpp
    video-player.cpp
    video-player.hpp
    ffmpeg-utils.cpp
    ffmpeg-utils.hpp
    whisper-utils.cpp
    whisper-utils.hpp
    llama-runner.cpp
    llama-runner.hpp
)

target_sources(
  obs-ai-toolkit
  PRIVATE
    forms/ai-health-dock.ui
)

target_link_libraries(
  obs-ai-toolkit
  PRIVATE
    OBS::frontend-api
    OBS::libobs
    OBS::qt-wrappers
    Qt::Widgets
    Qt::Multimedia
    Qt::Network
    Qt::Concurrent
    FFmpeg::avcodec
    FFmpeg::avformat
    FFmpeg::avutil
    FFmpeg::swresample
    FFmpeg::swscale
    whisper
)

# Include whisper headers only (no llama - we use subprocess)
target_include_directories(obs-ai-toolkit PRIVATE 
    "${CMAKE_SOURCE_DIR}/deps/whisper.cpp/include"
    "${CMAKE_SOURCE_DIR}/deps/whisper.cpp/ggml/include"
)

if(OS_WINDOWS)
  configure_file(cmake/windows/obs-module.rc.in obs-ai-toolkit.rc)
  target_sources(obs-ai-toolkit PRIVATE obs-ai-toolkit.rc)
  set_property(TARGET obs-ai-toolkit APPEND PROPERTY AUTORCC_OPTIONS --format-version 1)
endif()

set_target_properties_obs(
  obs-ai-toolkit
  PROPERTIES FOLDER frontend
             PREFIX ""
             AUTOMOC ON
             AUTOUIC ON
             AUTORCC ON
             AUTOUIC_SEARCH_PATHS forms
)

target_include_directories(obs-ai-toolkit PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_dependencies(obs-ai-toolkit llama-cpp-external)

# Note: llama-cli bundling is handled by post-build install step
# The bundle_llama_cli function must be called from the same directory that creates obs-studio target
# See frontend/CMakeLists.txt for the bundle step

# =============================================================================
# Tests (optional, controlled by ENABLE_AI_TOOLKIT_TESTS)
# =============================================================================
if(ENABLE_AI_TOOLKIT_TESTS)
  add_subdirectory(test)
endif()

