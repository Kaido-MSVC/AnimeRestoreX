# Stand-alone Windows ML / ONNX Runtime probe for AnimeRestoreX v2.0 (P1).
# Not part of the application build; no Qt.
cmake_minimum_required(VERSION 3.21)
project(winml_probe LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Extracted Microsoft.Windows.AI.MachineLearning nupkg (see fetch-deps.ps1).
set(WINML_PKG_DIR "${CMAKE_CURRENT_SOURCE_DIR}/_work/pkg/2.5.96" CACHE PATH
    "Root of the extracted Microsoft.Windows.AI.MachineLearning package")
set(WINML_EXPECTED_ORT_VERSION "1.28.3" CACHE STRING
    "ORT version the probe requires from the loaded onnxruntime.dll")

# Use the CMake config shipped in the package (build/cmake), nothing from the system.
find_package(microsoft.windows.ai.machinelearning CONFIG REQUIRED
    PATHS "${WINML_PKG_DIR}/build/cmake" NO_DEFAULT_PATH)
message(STATUS "Windows ML package ${WINML_VERSION}: ${WINML_BINARY_DIR}")

add_executable(winml_probe src/main.cpp)
target_compile_definitions(winml_probe PRIVATE
    UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN
    WINML_EXPECTED_ORT_VERSION="${WINML_EXPECTED_ORT_VERSION}")
target_compile_options(winml_probe PRIVATE /W4 /permissive- /utf-8 /EHsc)
# Only the ORT C/C++ API is used (no EP catalog, no experimental WinML runtime API).
target_link_libraries(winml_probe PRIVATE WindowsML::OnnxRuntime dxgi delayimp)
# WindowsML::OnnxRuntime exposes only include/winml, while the package README
# documents #include <winml/onnxruntime_cxx_api.h>; add include/ for that form
# (without linking WindowsML::Api, which the probe does not use).
target_include_directories(winml_probe PRIVATE "${WINML_INCLUDE_DIR}")
# Delay-load ORT so main() can pin the exact DLL file (app directory) before any
# ORT call. Windows 11 ships an older onnxruntime.dll in System32.
target_link_options(winml_probe PRIVATE /DELAYLOAD:onnxruntime.dll)

add_custom_command(TARGET winml_probe POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${WINML_BINARY_DIR}/onnxruntime.dll" "${WINML_DIRECTML_DLL}"
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

# ORT engine (Windows ML package). ORT_API_MANUAL_INIT everywhere: Ort::InitApi() is
# called only after the DLL path has been pinned.
add_library(arx_ort STATIC ${ARX_INF}/OrtTileEngine.cpp)
target_compile_definitions(arx_ort PUBLIC ORT_API_MANUAL_INIT)
target_compile_options(arx_ort PRIVATE ${ARX_WARN})
target_include_directories(arx_ort PUBLIC "${WINML_INCLUDE_DIR}")
target_link_libraries(arx_ort PUBLIC arx_core WindowsML::OnnxRuntime)

add_executable(tile_runner runner/tile_runner.cpp)
target_compile_definitions(tile_runner PRIVATE WINML_EXPECTED_ORT_VERSION="${WINML_EXPECTED_ORT_VERSION}")
target_compile_options(tile_runner PRIVATE ${ARX_WARN})
target_link_libraries(tile_runner PRIVATE arx_ort dxgi delayimp)
target_link_options(tile_runner PRIVATE /DELAYLOAD:onnxruntime.dll)
add_custom_command(TARGET tile_runner POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${WINML_BINARY_DIR}/onnxruntime.dll" "${WINML_DIRECTML_DLL}"
        $<TARGET_FILE_DIR:tile_runner>
    VERBATIM)
