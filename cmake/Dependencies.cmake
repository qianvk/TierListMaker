# QWindowKit deliberately uses Qt private platform integration APIs. Installers deploy the exact
# Qt build used here, so suppress the generic warning after making that version lock explicit.
set(QT_NO_PRIVATE_MODULE_WARNING ON)
find_package(Qt6 6.10.1 REQUIRED COMPONENTS
    Core CorePrivate
    Gui GuiPrivate
    Widgets WidgetsPrivate
    Svg Network Concurrent Test LinguistTools
)

set(TLM_THIRD_PARTY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party")
set(TLM_QWINDOWKIT_DIR "${TLM_THIRD_PARTY_DIR}/qwindowkit")
set(TLM_VKUI_DIR "${TLM_THIRD_PARTY_DIR}/vkui")

if(NOT EXISTS "${TLM_QWINDOWKIT_DIR}/CMakeLists.txt" OR
   NOT EXISTS "${TLM_QWINDOWKIT_DIR}/qmsetup/CMakeLists.txt")
    message(FATAL_ERROR
        "QWindowKit is incomplete. Run: git submodule update --init --recursive "
        "third_party/qwindowkit")
endif()
if(NOT EXISTS "${TLM_VKUI_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "VkUI is missing. Run: git submodule update --init third_party/vkui")
endif()

# Embed both UI dependencies statically so platform installers only need to deploy Qt runtime
# libraries. QWindowKit is pinned by the parent repository to its dev branch commit.
set(QWINDOWKIT_BUILD_STATIC ON CACHE BOOL "Build QWindowKit statically" FORCE)
set(QWINDOWKIT_BUILD_WIDGETS ON CACHE BOOL "Build QWindowKit Widgets" FORCE)
set(QWINDOWKIT_BUILD_QUICK OFF CACHE BOOL "Build QWindowKit Quick" FORCE)
set(QWINDOWKIT_BUILD_EXAMPLES OFF CACHE BOOL "Build QWindowKit examples" FORCE)
set(QWINDOWKIT_BUILD_DOCUMENTATIONS OFF CACHE BOOL "Build QWindowKit docs" FORCE)
set(QWINDOWKIT_INSTALL OFF CACHE BOOL "Install embedded QWindowKit" FORCE)
add_subdirectory("${TLM_QWINDOWKIT_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/third_party/qwindowkit"
                 EXCLUDE_FROM_ALL)

set(VKUI_BUILD_SHARED OFF CACHE BOOL "Build VkUI statically" FORCE)
set(VKUI_BUILD_EXAMPLES OFF CACHE BOOL "Build VkUI examples" FORCE)
set(VKUI_BUILD_TESTS OFF CACHE BOOL "Build VkUI tests" FORCE)
set(VKUI_INSTALL OFF CACHE BOOL "Install embedded VkUI" FORCE)
set(VKUI_ENABLE_WARNINGS OFF CACHE BOOL "Use parent warning policy" FORCE)
set(VKUI_EXPORT_COMPILE_COMMANDS ON CACHE BOOL "Export clangd database" FORCE)
set(VKUI_SYNC_COMPILE_COMMANDS OFF CACHE BOOL "Do not copy nested clangd database" FORCE)
add_subdirectory("${TLM_VKUI_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/third_party/vkui"
                 EXCLUDE_FROM_ALL)

qt_standard_project_setup()
