# 3.26: cmake -E copy_directory_if_different (model files next to the app).
cmake_minimum_required(VERSION 3.26)

project(AnimeRestoreX VERSION 2.1.0 LANGUAGES CXX)

# CMake 4.4.4 maps C++23 on MSVC to -std:c++latest; MSVC 19.52 has -std:c++23.
# Remove this once CMake maps C++23 to -std:c++23 natively.
if(MSVC AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 19.52)
    set(CMAKE_CXX23_STANDARD_COMPILE_OPTION "-std:c++23")
    set(CMAKE_CXX23_EXTENSION_COMPILE_OPTION "-std:c++23")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "AnimeRestoreX requires an x64 toolchain.")
endif()

find_package(Qt6 6.12 REQUIRED COMPONENTS Core Gui Widgets LinguistTools)
qt_standard_project_setup()

if(MSVC)
    # C++ only: the C language is enabled just for the third-party zlib files.
    add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:/W4;/permissive-;/utf-8>")
endif()

# AI runtime (ONNX Runtime C/C++ API + DirectML), pinned; see tools/fetch-runtime.ps1.
# Only these packages are used, nothing from the system.
#   windowsml : Microsoft.Windows.AI.MachineLearning 2.5.96 (ONNX Runtime 1.28.3, DirectML 1.15.4;
#               default since v2.2). Only the bundled DirectML and CPU EPs: no Windows ML API
#               call (no ExecutionProviderCatalog), Microsoft.Windows.AI.MachineLearning.dll is
#               shipped and pinned next to the exe (supported layout), but never called.
#   upstream  : Microsoft.ML.OnnxRuntime.DirectML 1.24.4 + Microsoft.AI.DirectML 1.15.4, the v2.0 /
#               v2.1 runtime, kept as a fallback.
set(ARX_ORT_FLAVOR "windowsml" CACHE STRING "AI runtime: windowsml or upstream")
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
    if(NOT WINML_VERSION STREQUAL "2.5.96")
        message(FATAL_ERROR "Windows ML package ${WINML_VERSION} found in ${ARX_WINML_DIR}, expected 2.5.96")
    endif()
    set(ARX_ORT_TARGET WindowsML::OnnxRuntime)
    set(ARX_RUNTIME_DLLS "${WINML_BINARY_DIR}/onnxruntime.dll" "${WINML_DIRECTML_DLL}"
                         "${WINML_BINARY_DIR}/Microsoft.Windows.AI.MachineLearning.dll")
else()
    message(FATAL_ERROR "ARX_ORT_FLAVOR must be windowsml or upstream, got '${ARX_ORT_FLAVOR}'")
endif()
message(STATUS "AI runtime: ${ARX_ORT_FLAVOR}, ONNX Runtime ${ARX_ORT_VERSION}")
# zlib 1.4 source (SHA-256 pinned in tools/fetch-zlib.ps1): only the inflate files are built,
# statically, for PNG iCCP with a hard output limit (src/core/ImageMetadata.cpp).
set(ARX_ZLIB_DIR "${CMAKE_SOURCE_DIR}/third_party/zlib/1.4" CACHE PATH "Extracted zlib 1.4 source")
enable_language(C)
if(NOT EXISTS "${ARX_ZLIB_DIR}/zlib.h")
    message(FATAL_ERROR "zlib source not found in ${ARX_ZLIB_DIR}; run tools/fetch-zlib.ps1")
endif()
file(STRINGS "${ARX_ZLIB_DIR}/zlib.h" ARX_ZLIB_VERSION_LINE REGEX "^#define ZLIB_VERSION \"1\\.4\"$")
if(NOT ARX_ZLIB_VERSION_LINE)
    message(FATAL_ERROR "${ARX_ZLIB_DIR}/zlib.h is not zlib 1.4")
endif()

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
