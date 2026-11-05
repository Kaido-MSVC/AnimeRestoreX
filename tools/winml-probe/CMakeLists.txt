# Stand-alone Windows ML / ONNX Runtime probe for AnimeRestoreX v2.0 (P1).
# Not part of the application build; no Qt.
cmake_minimum_required(VERSION 3.21)
project(winml_probe LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Which ORT package the headers and import library come from (see fetch-deps.ps1):
#   upstream : Microsoft.ML.OnnxRuntime.DirectML 1.24.4 + Microsoft.AI.DirectML 1.15.4
#   winml    : Microsoft.Windows.AI.MachineLearning 2.5.96
set(ARX_ORT_FLAVOR "upstream" CACHE STRING "upstream or winml")
set_property(CACHE ARX_ORT_FLAVOR PROPERTY STRINGS upstream winml)

if(ARX_ORT_FLAVOR STREQUAL "upstream")
    set(UPSTREAM_ORT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/_work/pkg/ort-dml-1.24.4" CACHE PATH
        "Root of the extracted Microsoft.ML.OnnxRuntime.DirectML package")
    set(UPSTREAM_DML_DIR "${CMAKE_CURRENT_SOURCE_DIR}/_work/pkg/directml-1.15.4" CACHE PATH
        "Root of the extracted Microsoft.AI.DirectML package")
    set(WINML_EXPECTED_ORT_VERSION "1.24.4" CACHE STRING
        "ORT version the probe requires from the loaded onnxruntime.dll")
    # The package has no CMake config. compat/winml forwards <winml/...> includes.
    add_library(arx_ort_runtime INTERFACE)
    target_include_directories(arx_ort_runtime INTERFACE
        "${CMAKE_CURRENT_SOURCE_DIR}/compat" "${UPSTREAM_ORT_DIR}/build/native/include")
    target_link_libraries(arx_ort_runtime INTERFACE
        "${UPSTREAM_ORT_DIR}/runtimes/win-x64/native/onnxruntime.lib")
    set(ARX_ORT_TARGET arx_ort_runtime)
    set(ARX_ORT_INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/compat")
    set(ARX_RUNTIME_DLLS "${UPSTREAM_ORT_DIR}/runtimes/win-x64/native/onnxruntime.dll"
                         "${UPSTREAM_DML_DIR}/bin/x64-win/DirectML.dll")
elseif(ARX_ORT_FLAVOR STREQUAL "winml")
    # Extracted Microsoft.Windows.AI.MachineLearning nupkg.
    set(WINML_PKG_DIR "${CMAKE_CURRENT_SOURCE_DIR}/_work/pkg/2.5.96" CACHE PATH
        "Root of the extracted Microsoft.Windows.AI.MachineLearning package")
    set(WINML_EXPECTED_ORT_VERSION "1.28.3" CACHE STRING
        "ORT version the probe requires from the loaded onnxruntime.dll")
    # Use the CMake config shipped in the package (build/cmake), nothing from the system.
    find_package(microsoft.windows.ai.machinelearning CONFIG REQUIRED
        PATHS "${WINML_PKG_DIR}/build/cmake" NO_DEFAULT_PATH)
    message(STATUS "Windows ML package ${WINML_VERSION}: ${WINML_BINARY_DIR}")
    set(ARX_ORT_TARGET WindowsML::OnnxRuntime)
    set(ARX_ORT_INCLUDE "${WINML_INCLUDE_DIR}")
    set(ARX_RUNTIME_DLLS "${WINML_BINARY_DIR}/onnxruntime.dll" "${WINML_DIRECTML_DLL}")
else()
    message(FATAL_ERROR "ARX_ORT_FLAVOR must be upstream or winml, got '${ARX_ORT_FLAVOR}'")
endif()

add_executable(winml_probe src/main.cpp)
target_compile_definitions(winml_probe PRIVATE
    UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN
    WINML_EXPECTED_ORT_VERSION="${WINML_EXPECTED_ORT_VERSION}")
target_compile_options(winml_probe PRIVATE /W4 /permissive- /utf-8 /EHsc)
# Only the ORT C/C++ API is used (no EP catalog, no experimental WinML runtime API).
target_link_libraries(winml_probe PRIVATE ${ARX_ORT_TARGET} dxgi delayimp)
# WindowsML::OnnxRuntime exposes only include/winml, while the package README
# documents #include <winml/onnxruntime_cxx_api.h>; add include/ for that form
# (without linking WindowsML::Api, which the probe does not use).
target_include_directories(winml_probe PRIVATE "${ARX_ORT_INCLUDE}")
# Delay-load ORT so main() can pin the exact DLL file (app directory) before any
# ORT call. Windows 11 ships an older onnxruntime.dll in System32.
target_link_options(winml_probe PRIVATE /DELAYLOAD:onnxruntime.dll)

add_custom_command(TARGET winml_probe POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${ARX_RUNTIME_DLLS}
        $<TARGET_FILE_DIR:winml_probe>
    VERBATIM)

# ---- P2: core module and the validation runner ----
# The module moved to src/core/inference in P3 (its tests are tests/tst_inference_core.cpp);
# the runner builds the same sources from there.
set(ARX_WARN /W4 /permissive- /utf-8 /EHsc)
set(ARX_DEFS UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
set(ARX_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
set(ARX_INF "${ARX_SRC}/core/inference")

# Pure core: no ORT, no Qt.
add_library(arx_core STATIC ${ARX_INF}/Json.cpp ${ARX_INF}/Sha256.cpp ${ARX_INF}/ModelContract.cpp
    ${ARX_INF}/TileProcessor.cpp)
target_compile_definitions(arx_core PUBLIC ${ARX_DEFS})
target_compile_options(arx_core PRIVATE ${ARX_WARN})
target_include_directories(arx_core PUBLIC "${ARX_SRC}")
target_link_libraries(arx_core PUBLIC bcrypt)

# ORT engine. ORT_API_MANUAL_INIT everywhere: Ort::InitApi() is
# called only after the DLL path has been pinned.
add_library(arx_ort STATIC ${ARX_INF}/OrtTileEngine.cpp)
target_compile_definitions(arx_ort PUBLIC ORT_API_MANUAL_INIT)
target_compile_options(arx_ort PRIVATE ${ARX_WARN})
target_include_directories(arx_ort PUBLIC "${ARX_ORT_INCLUDE}")
target_link_libraries(arx_ort PUBLIC arx_core ${ARX_ORT_TARGET})

add_executable(tile_runner runner/tile_runner.cpp)
target_compile_definitions(tile_runner PRIVATE WINML_EXPECTED_ORT_VERSION="${WINML_EXPECTED_ORT_VERSION}")
target_compile_options(tile_runner PRIVATE ${ARX_WARN})
target_link_libraries(tile_runner PRIVATE arx_ort dxgi delayimp)
target_link_options(tile_runner PRIVATE /DELAYLOAD:onnxruntime.dll)
add_custom_command(TARGET tile_runner POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${ARX_RUNTIME_DLLS}
        $<TARGET_FILE_DIR:tile_runner>
    VERBATIM)
