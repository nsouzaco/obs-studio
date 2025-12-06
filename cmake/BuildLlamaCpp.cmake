# OBS CMake llama.cpp external build module

include_guard(GLOBAL)
include(ExternalProject)

set(LLAMA_CPP_VERSION "b5270" CACHE STRING "llama.cpp version tag")
set(LLAMA_CPP_REPO "https://github.com/ggerganov/llama.cpp.git" CACHE STRING "llama.cpp repository")
set(LLAMA_CLI_OUTPUT_DIR "${CMAKE_BINARY_DIR}/llama-cpp-build" CACHE PATH "llama.cpp build directory")
set(LLAMA_CLI_BINARY "${LLAMA_CLI_OUTPUT_DIR}/bin/llama-cli" CACHE FILEPATH "llama-cli binary path")

if(OS_MACOS)
  set(LLAMA_CMAKE_ARGS
    -DCMAKE_BUILD_TYPE=Release
    -DCMAKE_OSX_ARCHITECTURES=arm64$<SEMICOLON>x86_64
    -DCMAKE_OSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET}
    -DBUILD_SHARED_LIBS=OFF
    -DGGML_METAL=ON
    -DGGML_ACCELERATE=ON
    -DGGML_BLAS=OFF
    -DLLAMA_BUILD_TESTS=OFF
    -DLLAMA_BUILD_EXAMPLES=OFF
    -DLLAMA_BUILD_SERVER=OFF
    -DLLAMA_CURL=OFF
  )
  set(LLAMA_CLI_BUNDLE_PATH "Contents/Helpers")

elseif(OS_WINDOWS)
  set(LLAMA_CMAKE_ARGS
    -DCMAKE_BUILD_TYPE=Release
    -DBUILD_SHARED_LIBS=OFF
    -DGGML_CUDA=OFF
    -DLLAMA_BUILD_TESTS=OFF
    -DLLAMA_BUILD_EXAMPLES=OFF
    -DLLAMA_BUILD_SERVER=OFF
    -DLLAMA_CURL=OFF
  )
  set(LLAMA_CLI_BINARY "${LLAMA_CLI_OUTPUT_DIR}/bin/llama-cli.exe")
  set(LLAMA_CLI_BUNDLE_PATH "bin")

elseif(OS_LINUX)
  set(LLAMA_CMAKE_ARGS
    -DCMAKE_BUILD_TYPE=Release
    -DBUILD_SHARED_LIBS=OFF
    -DLLAMA_BUILD_TESTS=OFF
    -DLLAMA_BUILD_EXAMPLES=OFF
    -DLLAMA_BUILD_SERVER=OFF
    -DLLAMA_CURL=OFF
  )
  set(LLAMA_CLI_BUNDLE_PATH "bin")
endif()

ExternalProject_Add(
  llama-cpp-external
  GIT_REPOSITORY ${LLAMA_CPP_REPO}
  GIT_TAG ${LLAMA_CPP_VERSION}
  GIT_SHALLOW TRUE
  GIT_PROGRESS TRUE

  PREFIX "${LLAMA_CLI_OUTPUT_DIR}"
  SOURCE_DIR "${LLAMA_CLI_OUTPUT_DIR}/src"
  BINARY_DIR "${LLAMA_CLI_OUTPUT_DIR}/build"
  INSTALL_DIR "${LLAMA_CLI_OUTPUT_DIR}"

  CMAKE_ARGS
    ${LLAMA_CMAKE_ARGS}
    -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>

  BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --target llama-cli --config Release --parallel
  INSTALL_COMMAND ${CMAKE_COMMAND} -E make_directory <INSTALL_DIR>/bin
  COMMAND ${CMAKE_COMMAND} -E copy_if_different <BINARY_DIR>/bin/llama-cli${CMAKE_EXECUTABLE_SUFFIX} <INSTALL_DIR>/bin/

  UPDATE_DISCONNECTED TRUE
  BUILD_BYPRODUCTS "${LLAMA_CLI_BINARY}"
  LOG_DOWNLOAD TRUE
  LOG_CONFIGURE TRUE
  LOG_BUILD TRUE
  LOG_INSTALL TRUE
)

add_executable(llama-cli IMPORTED GLOBAL)
set_target_properties(llama-cli PROPERTIES IMPORTED_LOCATION "${LLAMA_CLI_BINARY}")
add_dependencies(llama-cli llama-cpp-external)

# bundle_llama_cli: Copy llama-cli into application bundle
function(bundle_llama_cli target)
  if(OS_MACOS)
    add_custom_command(
      TARGET ${target}
      POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_CONTENT_DIR:${target}>/Helpers"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different "${LLAMA_CLI_BINARY}" "$<TARGET_BUNDLE_CONTENT_DIR:${target}>/Helpers/llama-cli"
      COMMENT "Bundling llama-cli into ${target}"
      DEPENDS llama-cli
    )
  elseif(OS_WINDOWS)
    add_custom_command(
      TARGET ${target}
      POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different "${LLAMA_CLI_BINARY}" "$<TARGET_FILE_DIR:${target}>/llama-cli.exe"
      COMMENT "Bundling llama-cli into ${target}"
      DEPENDS llama-cli
    )
  elseif(OS_LINUX)
    add_custom_command(
      TARGET ${target}
      POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different "${LLAMA_CLI_BINARY}" "$<TARGET_FILE_DIR:${target}>/llama-cli"
      COMMENT "Bundling llama-cli into ${target}"
      DEPENDS llama-cli
    )
  endif()
endfunction()

message(STATUS "llama.cpp ${LLAMA_CPP_VERSION} will be built and bundled as llama-cli")
