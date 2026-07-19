include(GNUInstallDirs)

install(TARGETS TierListMaker
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(MSVC)
    # Qt and the application import only these ABI-compatible VC runtime libraries. Resolve the
    # active toolchain instead of relying on CMake's compiler-version table, which can lag MSVC.
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(AMD64|amd64|x86_64)$")
        set(_tlm_redist_arch x64)
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(ARM64|arm64|aarch64)$")
        set(_tlm_redist_arch arm64)
    else()
        message(FATAL_ERROR "Unsupported Windows package architecture: ${CMAKE_SYSTEM_PROCESSOR}")
    endif()

    set(_tlm_redist_root "$ENV{VCToolsRedistDir}")
    if(_tlm_redist_root)
        cmake_path(CONVERT "${_tlm_redist_root}" TO_CMAKE_PATH_LIST _tlm_redist_root NORMALIZE)
    endif()
    if(NOT _tlm_redist_root)
        get_filename_component(_tlm_compiler_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
        get_filename_component(_tlm_vc_root
            "${_tlm_compiler_dir}/../../../../../.." ABSOLUTE)
        set(_tlm_redist_root "${_tlm_vc_root}/Redist/MSVC")
    endif()
    file(GLOB _tlm_crt_directories LIST_DIRECTORIES TRUE
        "${_tlm_redist_root}/*/${_tlm_redist_arch}/Microsoft.VC*.CRT"
        "${_tlm_redist_root}/${_tlm_redist_arch}/Microsoft.VC*.CRT")
    list(SORT _tlm_crt_directories COMPARE NATURAL ORDER DESCENDING)
    if(NOT _tlm_crt_directories)
        message(FATAL_ERROR "The active MSVC redistributable directory was not found")
    endif()
    list(GET _tlm_crt_directories 0 _tlm_crt_directory)

    set(_tlm_runtime_names
        msvcp140.dll
        msvcp140_1.dll
        msvcp140_2.dll
        vcruntime140.dll
        vcruntime140_1.dll
    )
    foreach(_tlm_runtime_name IN LISTS _tlm_runtime_names)
        set(_tlm_runtime_path "${_tlm_crt_directory}/${_tlm_runtime_name}")
        if(NOT EXISTS "${_tlm_runtime_path}")
            message(FATAL_ERROR "Required MSVC runtime was not found: ${_tlm_runtime_path}")
        endif()
        install(FILES "${_tlm_runtime_path}" DESTINATION "${CMAKE_INSTALL_BINDIR}")
    endforeach()
endif()

# Build a self-contained install tree before CPack turns it into a platform installer.
set(TLM_MACOS_CODESIGN_IDENTITY "" CACHE STRING
    "Developer ID Application identity used by macdeployqt (empty creates an unsigned bundle)")
set(_tlm_deploy_tool_options)
set(_tlm_deploy_plugin_options)
if(APPLE AND TLM_MACOS_CODESIGN_IDENTITY)
    list(APPEND _tlm_deploy_tool_options
        "-sign-for-notarization=${TLM_MACOS_CODESIGN_IDENTITY}")
endif()
if(WIN32)
    list(APPEND _tlm_deploy_tool_options
        --no-opengl-sw
        --no-system-d3d-compiler
        --no-system-dxc-compiler
        --no-compiler-runtime
        --no-translations
    )
    list(APPEND _tlm_deploy_plugin_options
        EXCLUDE_PLUGINS
            qtuiotouchplugin
            qicns
            qtga
            qwbmp
            qnetworklistmanager
    )
endif()
qt_generate_deploy_app_script(
    TARGET TierListMaker
    OUTPUT_SCRIPT TLM_QT_DEPLOY_SCRIPT
    NO_TRANSLATIONS
    NO_COMPILER_RUNTIME
    NO_UNSUPPORTED_PLATFORM_ERROR
    ${_tlm_deploy_plugin_options}
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
    # NSIS provides a familiar machine-wide wizard, Start Menu entry, repair-safe upgrades,
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
    # A machine-wide application belongs under the native 64-bit Program Files directory.
    # CPack's NSIS template already requests elevation and handles an existing installation.
    set(CPACK_NSIS_INSTALL_ROOT "$PROGRAMFILES64")
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
    set(CPACK_NSIS_EXTRA_INSTALL_COMMANDS
        "WriteRegStr HKLM \"Software\\qianvk\\TierListMaker\" \"RuntimeVersion\" \"${TLM_UPDATE_RUNTIME_VERSION}\"")
    set(CPACK_NSIS_URL_INFO_ABOUT "https://github.com/qianvk/TierListMaker")
    set(CPACK_NSIS_HELP_LINK "https://github.com/qianvk/TierListMaker/issues")
    set(CPACK_NSIS_BRANDING_TEXT "TierListMaker")

    if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(AMD64|amd64|x86_64)$")
        set(_tlm_update_arch AMD64)
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(ARM64|arm64|aarch64)$")
        set(_tlm_update_arch ARM64)
    else()
        message(FATAL_ERROR "Unsupported Windows update architecture: ${CMAKE_SYSTEM_PROCESSOR}")
    endif()

    find_program(TLM_MAKENSIS_EXECUTABLE NAMES makensis makensis.exe
        HINTS "C:/Program Files (x86)/NSIS" "C:/Program Files/NSIS")
    if(TLM_MAKENSIS_EXECUTABLE)
        set(TLM_WINDOWS_UPDATE_PACKAGE
            "${CMAKE_BINARY_DIR}/updates/TierListMaker-${TLM_PACKAGE_VERSION}-WinUpdate-${_tlm_update_arch}.exe")
        set(_tlm_update_payload "${CMAKE_BINARY_DIR}/updates/payload/TierListMaker.exe")
        file(TO_NATIVE_PATH "${_tlm_update_payload}" _tlm_update_payload_native)
        add_custom_command(
            OUTPUT "${TLM_WINDOWS_UPDATE_PACKAGE}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_BINARY_DIR}/updates/payload"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "$<TARGET_FILE:TierListMaker>" "${_tlm_update_payload}"
            COMMAND "${TLM_MAKENSIS_EXECUTABLE}"
                "/INPUTCHARSET" "UTF8"
                "/DAPP_EXECUTABLE=${_tlm_update_payload_native}"
                "/DOUTPUT_FILE=${TLM_WINDOWS_UPDATE_PACKAGE}"
                "/DAPP_VERSION=${TLM_PACKAGE_VERSION}"
                "/DNUMERIC_VERSION=${TLM_NUMERIC_VERSION}"
                "/DRUNTIME_VERSION=${TLM_UPDATE_RUNTIME_VERSION}"
                "${CMAKE_CURRENT_SOURCE_DIR}/packaging/windows/update.nsi"
            DEPENDS TierListMaker "${CMAKE_CURRENT_SOURCE_DIR}/packaging/windows/update.nsi"
            COMMENT "Creating executable-only Windows update package"
            VERBATIM
        )
        add_custom_target(TierListMakerUpdatePackage DEPENDS "${TLM_WINDOWS_UPDATE_PACKAGE}")
    else()
        message(STATUS "makensis was not found; the Windows update package target is unavailable")
    endif()

    file(WRITE "${CMAKE_BINARY_DIR}/TierListMaker-Windows-runtime-version.txt"
        "${TLM_UPDATE_RUNTIME_VERSION}\n")
endif()

include(CPack)
