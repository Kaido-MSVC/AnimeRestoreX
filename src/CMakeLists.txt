qt_add_library(arxcore STATIC
    core/ImageLimits.h core/ImageLimits.cpp
    core/ImageLoader.h core/ImageLoader.cpp
    core/ImageEncoder.h core/ImageEncoder.cpp
    core/OutputNaming.h core/OutputNaming.cpp
    core/Waifu2xRunner.h core/Waifu2xRunner.cpp
    core/ProcessJob.h core/ProcessJob.cpp
    core/BatchRunner.h core/BatchRunner.cpp
    core/FolderScanner.h core/FolderScanner.cpp
    core/Language.h core/Language.cpp
    core/AppSettings.h core/AppSettings.cpp
    core/ChildProcessGuard.h core/ChildProcessGuard.cpp
    core/JobSpec.h
)
target_include_directories(arxcore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(arxcore PUBLIC Qt6::Core Qt6::Gui)

# GUI pieces shared by the app and the widget tests.
qt_add_library(arxui STATIC
    ui/MainWindow.h ui/MainWindow.cpp ui/MainWindow.ui
    ui/JobWorker.h ui/JobWorker.cpp
    ui/PreviewPane.h ui/PreviewPane.cpp
    ui/CompletionDialog.h ui/CompletionDialog.cpp
    ui/SettingsDialog.h ui/SettingsDialog.cpp
    ui/LanguageManager.h ui/LanguageManager.cpp
)
target_link_libraries(arxui PUBLIC arxcore Qt6::Widgets)

# Traditional Chinese and Japanese; English is the source language.
# .qm files are embedded under :/i18n. Update .ts with: cmake --build <dir> --target update_translations
qt_add_translations(
    TARGETS arxui
    SOURCE_TARGETS arxcore arxui
    TS_FILES
        ${CMAKE_SOURCE_DIR}/translations/animerestorex_zh_TW.ts
        ${CMAKE_SOURCE_DIR}/translations/animerestorex_ja.ts
    NO_GENERATE_PLURALS_TS_FILE
    LUPDATE_OPTIONS -no-obsolete -locations none
)

qt_add_executable(AnimeRestoreX WIN32
    main.cpp
)
target_link_libraries(AnimeRestoreX PRIVATE arxui)
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
