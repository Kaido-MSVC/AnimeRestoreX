# 3.26: cmake -E copy_directory_if_different (model files next to the app).
cmake_minimum_required(VERSION 3.26)

project(AnimeRestoreX VERSION 2.0.0 LANGUAGES CXX)

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

# End-user licence agreement (first-run consent; shipped as EULA.txt and embedded in the app).
# Appendix A is the Microsoft Windows ML Runtime licence, copied from licenses/WindowsML/license.txt
# (byte-identical to the pinned package, see tools/collect-licenses.ps1); the generated file
# uses LF line endings throughout (the licence's CRLF are normalized).
# Raise ARX_EULA_VERSION whenever the agreement text changes: users are then asked again.
set(ARX_EULA_VERSION "2.0.0-1")
set(ARX_WINML_LICENSE_FILE "${CMAKE_SOURCE_DIR}/licenses/WindowsML/license.txt")
file(READ "${ARX_WINML_LICENSE_FILE}" ARX_WINML_LICENSE_TEXT)
string(REPLACE "\r\n" "\n" ARX_WINML_LICENSE_TEXT "${ARX_WINML_LICENSE_TEXT}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${ARX_WINML_LICENSE_FILE}")
configure_file(packaging/EULA.txt.in "${CMAKE_BINARY_DIR}/EULA.txt" @ONLY NEWLINE_STYLE LF)

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
