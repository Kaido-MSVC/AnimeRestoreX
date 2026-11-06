# Developer tests; not part of the distributed package.
set(ARX_QT_BIN "${Qt6_DIR}/../../../bin")
cmake_path(NORMAL_PATH ARX_QT_BIN)

function(arx_add_test name)
    qt_add_executable(${name} ${name}.cpp TestImages.h)
    target_link_libraries(${name} PRIVATE ${ARGN} Qt6::Test)
    target_compile_definitions(${name} PRIVATE
        ARX_TEST_MANIFEST="${CMAKE_SOURCE_DIR}/models/manifest.json"
        ARX_TEST_VERSION="${PROJECT_VERSION}"
        ARX_TEST_NOTICES="${CMAKE_BINARY_DIR}/THIRD_PARTY_NOTICES.md")
    add_test(NAME ${name} COMMAND ${name} ${ARX_TEST_ARGS_${name}})
    set_tests_properties(${name} PROPERTIES
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${ARX_QT_BIN}")
endfunction()

arx_add_test(tst_core arxcore)

# Inference core (no Qt, no GPU): fake engine, real models/manifest.json, scratch folder
# for the hash / path tests. Moved from tools/winml-probe in P3.
add_executable(tst_inference_core tst_inference_core.cpp)
target_link_libraries(tst_inference_core PRIVATE arxinfer)
add_test(NAME tst_inference_core COMMAND tst_inference_core
    "${CMAKE_SOURCE_DIR}/models/manifest.json" "${CMAKE_CURRENT_BINARY_DIR}/tst_inference_core-scratch")
# The pipeline test (fake backend, plus one real-runtime test when the DLLs and models are
# deployed) also writes its full log to tests/tst_pipeline-last.txt in the build folder,
# so a one-off failure can be traced even if the console output is lost.
set(ARX_TEST_ARGS_tst_pipeline -o -,txt -o "${CMAKE_CURRENT_BINARY_DIR}/tst_pipeline-last.txt,txt")
arx_add_test(tst_pipeline arxcore)
set_tests_properties(tst_pipeline PROPERTIES TIMEOUT 600)
# P3 verification tool (real runtime and models next to the exe); run by hand, not by ctest.
qt_add_executable(arx_verify arx_verify.cpp)
target_link_libraries(arx_verify PRIVATE arxcore psapi)

arx_add_test(tst_ui arxui)
set_tests_properties(tst_ui PROPERTIES TIMEOUT 600
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# Window size / layout checks need real Windows font metrics, so they run on
# the windows platform (windows flash briefly on the desktop).
add_test(NAME tst_ui_layout COMMAND tst_ui
    windowFitsCommonScreens minimumSizeWithinBudgetAllLanguages
    windowGeometryRestoresAndFallsBack detailColumnFollowsListWidth themeFollowsColorScheme)
set_tests_properties(tst_ui_layout PROPERTIES TIMEOUT 120
    ENVIRONMENT "QT_QPA_PLATFORM=windows"
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${ARX_QT_BIN}")
