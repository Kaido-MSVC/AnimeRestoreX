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

# AI runtime (ONNX Runtime C/C++ API + DirectML), pinned; see tools/fetch-runtime.ps1.
# Only these packages are used, nothing from the system.
#   upstream  : Microsoft.ML.OnnxRuntime.DirectML 1.24.4 + Microsoft.AI.DirectML 1.15.4 (default)
#   windowsml : Microsoft.Windows.AI.MachineLearning 2.5.96 (ONNX Runtime 1.28.3), for switching
#               back; tools/package.ps1 refuses it, and models need checking on it first.
set(ARX_ORT_FLAVOR "upstream" CACHE STRING "AI runtime: upstream or windowsml")
set_property(CACHE ARX_ORT_FLAVOR PROPERTY STRINGS upstream windowsml)
if(ARX_ORT_FLAVOR STREQUAL "upstream")
    # ARX_ORT_VERSION: what the app requires from the loaded onnxruntime.dll.
    set(ARX_ORT_VERSION "1.24.4")
    set(ARX_ORT_PKG "${CMAKE_SOURCE_DIR}/third_party/onnxruntime/1.24.4"
        CACHE PATH "Extracted Microsoft.ML.OnnxRuntime.DirectML package")
    set(ARX_DML_PKG "${CMAKE_SOURCE_DIR}/third_party/directml/1.15.4"
        CACHE PATH "Extracted Microsoft.AI.DirectML package")
    # The package has no CMake config.
    add_library(arx_ort_runtime INTERFACE)
    target_include_directories(arx_ort_runtime INTERFACE "${ARX_ORT_PKG}/build/native/include")
    target_link_libraries(arx_ort_runtime INTERFACE "${ARX_ORT_PKG}/runtimes/win-x64/native/onnxruntime.lib")
    set(ARX_ORT_TARGET arx_ort_runtime)
    set(ARX_RUNTIME_DLLS "${ARX_ORT_PKG}/runtimes/win-x64/native/onnxruntime.dll"
                         "${ARX_DML_PKG}/bin/x64-win/DirectML.dll")
elseif(ARX_ORT_FLAVOR STREQUAL "windowsml")
    set(ARX_ORT_VERSION "1.28.3")
    set(ARX_WINML_DIR "${CMAKE_SOURCE_DIR}/third_party/windowsml/2.5.96"
        CACHE PATH "Extracted Microsoft.Windows.AI.MachineLearning package")
    find_package(microsoft.windows.ai.machinelearning CONFIG REQUIRED
        PATHS "${ARX_WINML_DIR}/build/cmake" NO_DEFAULT_PATH)
    set(ARX_ORT_TARGET WindowsML::OnnxRuntime)
    set(ARX_RUNTIME_DLLS "${WINML_BINARY_DIR}/onnxruntime.dll" "${WINML_DIRECTML_DLL}")
else()
    message(FATAL_ERROR "ARX_ORT_FLAVOR must be upstream or windowsml, got '${ARX_ORT_FLAVOR}'")
endif()
message(STATUS "AI runtime: ${ARX_ORT_FLAVOR}, ONNX Runtime ${ARX_ORT_VERSION}")

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
