# Developer tests; not part of the distributed package.
set(ARX_QT_BIN "${Qt6_DIR}/../../../bin")
cmake_path(NORMAL_PATH ARX_QT_BIN)

function(arx_add_test name)
    qt_add_executable(${name} ${name}.cpp TestImages.h)
    target_link_libraries(${name} PRIVATE ${ARGN} Qt6::Test)
    target_compile_definitions(${name} PRIVATE
        ARX_WAIFU2X_DIR="${ARX_WAIFU2X_DIR}"
        ARX_TEST_VERSION="${PROJECT_VERSION}"
        ARX_TEST_NOTICES="${CMAKE_BINARY_DIR}/THIRD_PARTY_NOTICES.md")
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${ARX_QT_BIN}")
endfunction()

arx_add_test(tst_core arxcore)
arx_add_test(tst_pipeline arxcore)
set_tests_properties(tst_pipeline PROPERTIES TIMEOUT 600)
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
