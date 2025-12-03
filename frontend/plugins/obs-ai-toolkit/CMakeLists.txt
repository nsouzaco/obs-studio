cmake_minimum_required(VERSION 3.28...3.30)

find_package(Qt6 REQUIRED Widgets Multimedia MultimediaWidgets)
find_package(FFmpeg REQUIRED COMPONENTS avcodec avformat avutil swresample)

add_library(obs-ai-toolkit MODULE)
add_library(OBS::obs-ai-toolkit ALIAS obs-ai-toolkit)

if(NOT TARGET OBS::qt-wrappers)
  add_subdirectory("${CMAKE_SOURCE_DIR}/shared/qt/wrappers" "${CMAKE_BINARY_DIR}/shared/qt/wrappers")
endif()

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
    Qt::MultimediaWidgets
    FFmpeg::avcodec
    FFmpeg::avformat
    FFmpeg::avutil
    FFmpeg::swresample
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

