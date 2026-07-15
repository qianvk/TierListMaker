cmake_minimum_required(VERSION 3.24)

project(TierListMaker
    VERSION 0.1.0
    DESCRIPTION "A native Qt 6 desktop tier-list editor"
    HOMEPAGE_URL "https://github.com/qianvk/TierListMaker"
    LANGUAGES CXX
)

if(APPLE)
    enable_language(OBJCXX)
endif()

option(TLM_BUILD_TESTS "Build TierListMaker tests" ON)
option(TLM_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(ProjectOptions)
include(CompilerWarnings)
include(Dependencies)
include(Version)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE BOOL "Generate compile_commands.json for clangd" FORCE)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

set(TLM_CORE_SOURCES
    src/assets/AssetManager.cpp
    src/assets/AssetManager.h
    src/assets/ImageLoader.cpp
    src/assets/ImageLoader.h
    src/assets/ThumbnailCache.cpp
    src/assets/ThumbnailCache.h
    src/export/ExportOptions.cpp
    src/export/ExportOptions.h
    src/export/TierListExporter.cpp
    src/export/TierListExporter.h
    src/i18n/LanguageManager.cpp
    src/i18n/LanguageManager.h
    src/logging/Logger.cpp
    src/logging/Logger.h
    src/logging/UiPerformanceMonitor.cpp
    src/logging/UiPerformanceMonitor.h
    src/persistence/AtomicFileWriter.cpp
    src/persistence/AtomicFileWriter.h
    src/persistence/ProjectRepository.cpp
    src/persistence/ProjectRepository.h
    src/persistence/ProjectSerializer.cpp
    src/persistence/ProjectSerializer.h
    src/persistence/RecentProjectsStore.cpp
    src/persistence/RecentProjectsStore.h
    src/persistence/Result.h
    src/platform/Platform.cpp
    src/platform/Platform.h
    src/settings/AppSettings.cpp
    src/settings/AppSettings.h
    src/settings/SettingsKeys.cpp
    src/settings/SettingsKeys.h
    src/theme/Theme.cpp
    src/theme/Theme.h
    src/theme/ThemeManager.cpp
    src/theme/ThemeManager.h
    src/theme/ThemeTokens.h
    src/tier/TierImage.cpp
    src/tier/TierImage.h
    src/tier/TierProject.cpp
    src/tier/TierProject.h
    src/tier/TierRow.cpp
    src/tier/TierRow.h
)

set(TLM_APP_SOURCES
    src/app/Application.cpp
    src/app/Application.h
    src/app/main.cpp
    src/navigation/SidebarDelegate.cpp
    src/navigation/SidebarDelegate.h
    src/navigation/SidebarModel.cpp
    src/navigation/SidebarModel.h
    src/navigation/SidebarView.cpp
    src/navigation/SidebarView.h
    src/pages/EditPage.cpp
    src/pages/EditPage.h
    src/pages/PreferencesPage.cpp
    src/pages/PreferencesPage.h
    src/pages/ProjectLocationDialog.cpp
    src/pages/ProjectLocationDialog.h
    src/pages/ProjectsPage.cpp
    src/pages/ProjectsPage.h
    src/preview/PreviewOverlay.cpp
    src/preview/PreviewOverlay.h
    src/tier/ImageGalleryPopover.cpp
    src/tier/ImageGalleryPopover.h
    src/tier/CropEditorWidget.cpp
    src/tier/CropEditorWidget.h
    src/tier/ImageEditDialog.cpp
    src/tier/ImageEditDialog.h
    src/tier/TierRowEditDialog.cpp
    src/tier/TierRowEditDialog.h
    src/tier/TierBoardWidget.cpp
    src/tier/TierBoardWidget.h
    src/tier/TierDragController.cpp
    src/tier/TierDragController.h
    src/tier/TierListDelegate.cpp
    src/tier/TierListDelegate.h
    src/tier/TierListModel.cpp
    src/tier/TierListModel.h
    src/tier/TierListView.cpp
    src/tier/TierListView.h
    src/update/AppUpdater.cpp
    src/update/AppUpdater.h
    src/widgets/DestructiveActionDialog.cpp
    src/widgets/DestructiveActionDialog.h
    src/widgets/EmptyStateWidget.cpp
    src/widgets/EmptyStateWidget.h
    src/widgets/FlowLayout.cpp
    src/widgets/FlowLayout.h
    src/widgets/SectionHeader.cpp
    src/widgets/SectionHeader.h
    src/window/AppDialog.cpp
    src/window/AppDialog.h
    src/window/AppMessageDialog.cpp
    src/window/AppMessageDialog.h
    src/window/AppTitleBar.cpp
    src/window/AppTitleBar.h
    src/window/MainWindow.cpp
    src/window/MainWindow.h
    src/window/RootWidget.cpp
    src/window/RootWidget.h
    src/window/SidebarToggleButton.cpp
    src/window/SidebarToggleButton.h
    resources/resources.qrc
)

if(APPLE)
    set(TLM_APP_ICON_MACOS "${CMAKE_CURRENT_SOURCE_DIR}/resources/macos/app-icon.icns")
    list(APPEND TLM_APP_SOURCES "${TLM_APP_ICON_MACOS}")
    set_source_files_properties("${TLM_APP_ICON_MACOS}" PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources"
    )
endif()

if(WIN32)
    configure_file(
        resources/windows/TierListMaker.rc.in
        "${CMAKE_CURRENT_BINARY_DIR}/generated/TierListMaker.rc"
        @ONLY
    )
    list(APPEND TLM_APP_SOURCES "${CMAKE_CURRENT_BINARY_DIR}/generated/TierListMaker.rc")
endif()

if(APPLE)
    list(APPEND TLM_CORE_SOURCES src/platform/PlatformMac.mm)
elseif(WIN32)
    list(APPEND TLM_CORE_SOURCES src/platform/PlatformWindows.cpp)
else()
    list(APPEND TLM_CORE_SOURCES src/platform/PlatformLinux.cpp)
endif()

add_library(tierlistmaker_core STATIC ${TLM_CORE_SOURCES})
target_include_directories(tierlistmaker_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(tierlistmaker_core
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Concurrent
        VkUI::Core
)
tlm_apply_project_options(tierlistmaker_core)
tlm_apply_compiler_warnings(tierlistmaker_core)

qt_add_executable(TierListMaker ${TLM_APP_SOURCES})
target_include_directories(TierListMaker PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(TierListMaker
    PRIVATE
        tierlistmaker_core
        Qt6::Network
        Qt6::Svg
        QWindowKit::Widgets
        VkUI::Widgets
)
if(WIN32)
    target_link_libraries(TierListMaker PRIVATE dwmapi)
endif()
target_compile_definitions(TierListMaker PRIVATE
    TLM_APP_VERSION="${PROJECT_VERSION}"
    TLM_GIT_COMMIT="${TLM_GIT_COMMIT}"
)
qt_add_translations(TierListMaker
    TS_FILES
        resources/translations/TierListMaker_en.ts
        resources/translations/TierListMaker_zh_CN.ts
    RESOURCE_PREFIX "/translations"
)
tlm_apply_project_options(TierListMaker)
tlm_apply_compiler_warnings(TierListMaker)

set_target_properties(TierListMaker PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_BUNDLE_NAME "TierListMaker"
    MACOSX_BUNDLE_GUI_IDENTIFIER "org.tierlistmaker.app"
    MACOSX_BUNDLE_ICON_FILE "app-icon.icns"
)

include(InstallRules)

if(TLM_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests/unit)
    add_subdirectory(tests/integration)
endif()
