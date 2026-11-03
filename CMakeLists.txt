# 3.26: cmake -E copy_directory_if_different (model files next to the app).
cmake_minimum_required(VERSION 3.26)

project(AnimeRestoreX VERSION 1.2.0 LANGUAGES CXX)

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

# Windows ML package (ONNX Runtime C/C++ API + DirectML), pinned; see tools/fetch-runtime.ps1.
# Only its own build/cmake config is used, nothing from the system.
set(ARX_WINML_DIR "${CMAKE_SOURCE_DIR}/third_party/windowsml/2.5.96"
    CACHE PATH "Extracted Microsoft.Windows.AI.MachineLearning package")
set(ARX_ORT_VERSION "1.28.3"
    CACHE STRING "ONNX Runtime version the app requires from the loaded onnxruntime.dll")
find_package(microsoft.windows.ai.machinelearning CONFIG REQUIRED
    PATHS "${ARX_WINML_DIR}/build/cmake" NO_DEFAULT_PATH)
message(STATUS "Windows ML package ${WINML_VERSION}: ${WINML_BINARY_DIR}")

# Rewritten nunif models checked against models/manifest.json; see tools/fetch-models.ps1.
set(ARX_MODELS_DIR "${CMAKE_SOURCE_DIR}/third_party/models"
    CACHE PATH "Folder holding the models listed in models/manifest.json")

add_subdirectory(src)

# THIRD_PARTY_NOTICES.md is generated next to the executable with the project
# version filled in; tools/package.ps1 ships this generated copy.
configure_file(THIRD_PARTY_NOTICES.md.in "${CMAKE_BINARY_DIR}/THIRD_PARTY_NOTICES.md" @ONLY)

option(ARX_BUILD_TESTS "Build developer tests (not shipped)" ON)
if(ARX_BUILD_TESTS)
    enable_testing()
    find_package(Qt6 6.12 REQUIRED COMPONENTS Test)
    add_subdirectory(tests)
endif()
