qt_add_library(arxcore STATIC
    core/ImageLimits.h core/ImageLimits.cpp
    core/ImageLoader.h core/ImageLoader.cpp
    core/ImageEncoder.h core/ImageEncoder.cpp
    core/OutputNaming.h core/OutputNaming.cpp
    core/Waifu2xRunner.h core/Waifu2xRunner.cpp
    core/ProcessJob.h core/ProcessJob.cpp
    core/ChildProcessGuard.h core/ChildProcessGuard.cpp
    core/JobSpec.h
)
target_include_directories(arxcore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(arxcore PUBLIC Qt6::Core Qt6::Gui)

qt_add_executable(AnimeRestoreX WIN32
    main.cpp
    ui/MainWindow.h ui/MainWindow.cpp ui/MainWindow.ui
    ui/JobWorker.h ui/JobWorker.cpp
)
target_link_libraries(AnimeRestoreX PRIVATE arxcore Qt6::Widgets)
target_compile_definitions(AnimeRestoreX PRIVATE PROJECT_VERSION_STRING="${PROJECT_VERSION}")

# Copy the pinned waifu2x binary and the cunet models next to the app.
if(EXISTS "${ARX_WAIFU2X_DIR}/waifu2x-ncnn-vulkan.exe")
    add_custom_command(TARGET AnimeRestoreX POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:AnimeRestoreX>/waifu2x"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${ARX_WAIFU2X_DIR}/waifu2x-ncnn-vulkan.exe"
            "${ARX_WAIFU2X_DIR}/vcomp140.dll"
            "${ARX_WAIFU2X_DIR}/LICENSE"
            "$<TARGET_FILE_DIR:AnimeRestoreX>/waifu2x"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${ARX_WAIFU2X_DIR}/models-cunet"
            "$<TARGET_FILE_DIR:AnimeRestoreX>/waifu2x/models-cunet"
        VERBATIM)
else()
    message(WARNING "waifu2x not found in ${ARX_WAIFU2X_DIR}; run tools/fetch-waifu2x.ps1")
endif()
