cmake_minimum_required(VERSION 3.24)

project(LumaCore
    VERSION 0.1.0
    DESCRIPTION "Educational C++ computational lighting engine"
    LANGUAGES CXX)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "LumaCore supports 64-bit builds only (the C# layer is x64).")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# All executables and DLLs land in build/bin/<Config> so the C# projects and
# the test runner find lumacore.dll without PATH manipulation.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

option(LUMACORE_BUILD_TESTS "Build the GoogleTest unit tests" ON)
option(LUMACORE_ENABLE_CUDA "Build the optional CUDA backend (Milestone 7)" OFF)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
include(CompilerWarnings)

add_subdirectory(native)

if(LUMACORE_ENABLE_CUDA)
    message(WARNING "LUMACORE_ENABLE_CUDA is ON but the CUDA backend is not implemented yet (Milestone 7).")
endif()

if(LUMACORE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(native/tests)
endif()
