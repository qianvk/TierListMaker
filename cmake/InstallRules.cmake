include(GNUInstallDirs)

install(TARGETS TierListMaker
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# Build a self-contained install tree before CPack turns it into a platform installer.
set(TLM_MACOS_CODESIGN_IDENTITY "" CACHE STRING
    "Developer ID Application identity used by macdeployqt (empty creates an unsigned bundle)")
set(_tlm_deploy_tool_options)
if(APPLE AND TLM_MACOS_CODESIGN_IDENTITY)
    list(APPEND _tlm_deploy_tool_options
        "-sign-for-notarization=${TLM_MACOS_CODESIGN_IDENTITY}")
endif()
qt_generate_deploy_app_script(
    TARGET TierListMaker
    OUTPUT_SCRIPT TLM_QT_DEPLOY_SCRIPT
    NO_UNSUPPORTED_PLATFORM_ERROR
    DEPLOY_TOOL_OPTIONS ${_tlm_deploy_tool_options}
)
install(SCRIPT "${TLM_QT_DEPLOY_SCRIPT}")

set(CPACK_PACKAGE_NAME "TierListMaker")
set(CPACK_PACKAGE_VENDOR "qianvk")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "A native desktop tier-list editor")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/qianvk/TierListMaker")
set(CPACK_PACKAGE_VERSION "${TLM_PACKAGE_VERSION}")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "TierListMaker")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_PACKAGE_FILE_NAME
    "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")

if(APPLE)
    # A DMG with the standard Applications symlink matches the platform's expected drag-install flow.
    set(CPACK_GENERATOR "DragNDrop")
    set(CPACK_DMG_VOLUME_NAME "TierListMaker ${CPACK_PACKAGE_VERSION}")
    set(CPACK_DMG_FORMAT "UDZO")
    set(CPACK_DMG_FILESYSTEM "APFS")
elseif(WIN32)
    # NSIS provides a familiar per-user wizard, Start Menu entry, repair-safe upgrades,
    # Apps & Features metadata, and an optional launch action without modifying PATH.
    file(TO_NATIVE_PATH
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/windows/app-icon.ico"
        _tlm_nsis_icon)
    file(TO_NATIVE_PATH
        "${CMAKE_CURRENT_SOURCE_DIR}/packaging/windows/installer-welcome.bmp"
        _tlm_nsis_welcome_bitmap)
    file(TO_NATIVE_PATH
        "${CMAKE_INSTALL_BINDIR}/TierListMaker.exe"
        _tlm_nsis_installed_executable)

    set(CPACK_GENERATOR "NSIS")
    set(CPACK_VERBATIM_VARIABLES ON)
    set(CPACK_NSIS_INSTALL_ROOT "$LOCALAPPDATA")
    set(CPACK_NSIS_DISPLAY_NAME "TierListMaker")
    set(CPACK_NSIS_PACKAGE_NAME "TierListMaker")
    set(CPACK_NSIS_MUI_ICON "${_tlm_nsis_icon}")
    set(CPACK_NSIS_MUI_UNIICON "${_tlm_nsis_icon}")
    set(CPACK_NSIS_MUI_WELCOMEFINISHPAGE_BITMAP "${_tlm_nsis_welcome_bitmap}")
    set(CPACK_NSIS_MUI_UNWELCOMEFINISHPAGE_BITMAP "${_tlm_nsis_welcome_bitmap}")
    set(CPACK_NSIS_INSTALLED_ICON_NAME "${_tlm_nsis_installed_executable}")
    set(CPACK_NSIS_EXECUTABLES_DIRECTORY "${CMAKE_INSTALL_BINDIR}")
    set(CPACK_PACKAGE_EXECUTABLES "TierListMaker" "TierListMaker")
    set(CPACK_NSIS_MUI_FINISHPAGE_RUN "TierListMaker.exe")
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
    set(CPACK_NSIS_MODIFY_PATH OFF)
    set(CPACK_NSIS_MANIFEST_DPI_AWARE ON)
    set(CPACK_NSIS_URL_INFO_ABOUT "https://github.com/qianvk/TierListMaker")
    set(CPACK_NSIS_HELP_LINK "https://github.com/qianvk/TierListMaker/issues")
    set(CPACK_NSIS_BRANDING_TEXT "TierListMaker")
endif()

include(CPack)
