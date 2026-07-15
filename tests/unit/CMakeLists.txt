function(add_tlm_unit_test target source)
    qt_add_executable(${target} ${source})
    target_link_libraries(${target} PRIVATE tierlistmaker_core Qt6::Test)
    target_include_directories(${target} PRIVATE "${PROJECT_SOURCE_DIR}/src")
    add_test(NAME ${target} COMMAND ${target})
endfunction()

add_tlm_unit_test(tst_ProjectSerializer tst_ProjectSerializer.cpp)
add_tlm_unit_test(tst_RecentProjectsStore tst_RecentProjectsStore.cpp)
add_tlm_unit_test(tst_AssetManager tst_AssetManager.cpp)
add_tlm_unit_test(tst_ThemeManager tst_ThemeManager.cpp)

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
