find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets Svg Network Concurrent Test LinguistTools)

set(TLM_VKFRAMELESS_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/vkframeless")
if(EXISTS "${TLM_VKFRAMELESS_SOURCE_DIR}/CMakeLists.txt")
    set(VKFRAMELESS_BUILD_EXAMPLES OFF CACHE BOOL "Build VKFrameless examples" FORCE)
    set(VKFRAMELESS_BUILD_TESTS OFF CACHE BOOL "Build VKFrameless tests" FORCE)
    add_subdirectory(
        "${TLM_VKFRAMELESS_SOURCE_DIR}"
        "${CMAKE_CURRENT_BINARY_DIR}/external/vkframeless"
        EXCLUDE_FROM_ALL
    )
elseif(NOT TARGET VKFrameless::VKFrameless)
    find_package(VKFrameless CONFIG REQUIRED)
endif()

qt_standard_project_setup()
