cmake_minimum_required(VERSION 3.21)

project(AnimeRestoreX VERSION 1.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "AnimeRestoreX requires an x64 toolchain.")
endif()

find_package(Qt6 6.12 REQUIRED COMPONENTS Core Gui Widgets LinguistTools)
qt_standard_project_setup()

if(MSVC)
    add_compile_options(/W4 /permissive- /utf-8)
endif()

set(ARX_WAIFU2X_DIR "${CMAKE_SOURCE_DIR}/third_party/waifu2x"
    CACHE PATH "Directory holding waifu2x-ncnn-vulkan.exe and models-cunet")

add_subdirectory(src)

option(ARX_BUILD_TESTS "Build developer tests (not shipped)" ON)
if(ARX_BUILD_TESTS)
    enable_testing()
    find_package(Qt6 6.12 REQUIRED COMPONENTS Test)
    add_subdirectory(tests)
endif()
