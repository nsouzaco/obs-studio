cmake_minimum_required(VERSION 3.28...3.30)

project(obs-ai-toolkit-tests)

# Find Qt6 - use only Core and Widgets (Test not available in bundled Qt)
find_package(Qt6 REQUIRED COMPONENTS Core Widgets)
find_package(FFmpeg REQUIRED COMPONENTS avcodec avformat avutil swresample swscale)

# =============================================================================
# Unit Tests (no heavy dependencies)
# =============================================================================
add_executable(test-ai-toolkit-utils
    test-utils.cpp
)

target_link_libraries(test-ai-toolkit-utils PRIVATE
    Qt::Core
    Qt::Widgets
)

target_include_directories(test-ai-toolkit-utils PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

set_target_properties(test-ai-toolkit-utils PROPERTIES
    AUTOMOC ON
    CXX_STANDARD 17
)

# =============================================================================
# Integration Tests (requires FFmpeg)
# =============================================================================
add_executable(test-ai-toolkit-integration
    test-integration.cpp
    ../ffmpeg-utils.cpp
)

target_link_libraries(test-ai-toolkit-integration PRIVATE
    Qt::Core
    Qt::Widgets
    FFmpeg::avcodec
    FFmpeg::avformat
    FFmpeg::avutil
    FFmpeg::swresample
    FFmpeg::swscale
)

target_include_directories(test-ai-toolkit-integration PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

set_target_properties(test-ai-toolkit-integration PROPERTIES
    AUTOMOC ON
    CXX_STANDARD 17
)

# Register tests with CTest
enable_testing()
add_test(NAME obs-ai-toolkit-utils COMMAND test-ai-toolkit-utils)
add_test(NAME obs-ai-toolkit-integration COMMAND test-ai-toolkit-integration)
