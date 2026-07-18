function(add_tlm_unit_test target)
    qt_add_executable(${target} ${ARGN})
    target_link_libraries(${target} PRIVATE tierlistmaker_core Qt6::Test)
    target_include_directories(${target} PRIVATE "${PROJECT_SOURCE_DIR}/src")
    add_test(NAME ${target} COMMAND ${target})
endfunction()

add_tlm_unit_test(tst_ProjectSerializer tst_ProjectSerializer.cpp)
add_tlm_unit_test(tst_RecentProjectsStore tst_RecentProjectsStore.cpp)
add_tlm_unit_test(tst_AssetManager tst_AssetManager.cpp)
add_tlm_unit_test(tst_ThemeManager tst_ThemeManager.cpp)
add_tlm_unit_test(tst_AppUpdater
    tst_AppUpdater.cpp
    "${PROJECT_SOURCE_DIR}/src/update/AppUpdater.cpp"
    "${PROJECT_SOURCE_DIR}/src/update/AppUpdater.h"
)
target_link_libraries(tst_AppUpdater PRIVATE Qt6::Network)
add_tlm_unit_test(tst_TierListLayout
    tst_TierListLayout.cpp
    "${PROJECT_SOURCE_DIR}/src/tier/TierDragController.cpp"
    "${PROJECT_SOURCE_DIR}/src/tier/TierDragController.h"
    "${PROJECT_SOURCE_DIR}/src/tier/TierListDelegate.cpp"
    "${PROJECT_SOURCE_DIR}/src/tier/TierListDelegate.h"
    "${PROJECT_SOURCE_DIR}/src/tier/TierListModel.cpp"
    "${PROJECT_SOURCE_DIR}/src/tier/TierListModel.h"
    "${PROJECT_SOURCE_DIR}/src/tier/TierListView.cpp"
    "${PROJECT_SOURCE_DIR}/src/tier/TierListView.h"
)

qt_add_executable(tst_WindowLifecycle
    tst_WindowLifecycle.cpp
    "${PROJECT_SOURCE_DIR}/src/preview/PreviewOverlay.cpp"
    "${PROJECT_SOURCE_DIR}/src/preview/PreviewOverlay.h"
    "${PROJECT_SOURCE_DIR}/src/window/AppDialog.cpp"
    "${PROJECT_SOURCE_DIR}/src/window/AppDialog.h"
)
target_link_libraries(tst_WindowLifecycle PRIVATE
    tierlistmaker_core
    QWindowKit::Widgets
    Qt6::Test
)
target_include_directories(tst_WindowLifecycle PRIVATE "${PROJECT_SOURCE_DIR}/src")
add_test(NAME tst_WindowLifecycle COMMAND tst_WindowLifecycle)

qt_add_executable(tst_PreviewOverlay
    tst_PreviewOverlay.cpp
    "${PROJECT_SOURCE_DIR}/src/preview/PreviewOverlay.cpp"
    "${PROJECT_SOURCE_DIR}/src/preview/PreviewOverlay.h"
)
target_link_libraries(tst_PreviewOverlay PRIVATE tierlistmaker_core QWindowKit::Widgets Qt6::Test)
target_include_directories(tst_PreviewOverlay PRIVATE "${PROJECT_SOURCE_DIR}/src")
add_test(NAME tst_PreviewOverlay COMMAND tst_PreviewOverlay)

qt_add_executable(tst_WindowResizePolicy tst_WindowResizePolicy.cpp)
target_link_libraries(tst_WindowResizePolicy PRIVATE QWindowKit::Widgets Qt6::Test)
if(APPLE)
    set_source_files_properties(tst_WindowResizePolicy.cpp PROPERTIES LANGUAGE OBJCXX)
endif()
add_test(NAME tst_WindowResizePolicy COMMAND tst_WindowResizePolicy)

qt_add_executable(tst_Resources tst_Resources.cpp "${PROJECT_SOURCE_DIR}/resources/resources.qrc")
target_link_libraries(tst_Resources PRIVATE VkUI::Core Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test)
add_test(NAME tst_Resources COMMAND tst_Resources)
