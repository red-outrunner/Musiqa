# This file is included only for native Windows builds. QML stays embedded in
# qml.qrc; windeployqt scans the source QML to collect its imported Qt modules.
set(MUSICA_RUNTIME_SEARCH_DIRS "" CACHE STRING "Extra directories containing TagLib/codec runtime DLLs")
set(MUSICA_DEPENDENCY_LICENSE_DIRS "" CACHE STRING "Extra license/notice directories to include in the Windows package")
get_filename_component(_musica_qt_prefix "${Qt6_DIR}/../../.." ABSOLUTE)
find_program(MUSICA_WINDEPLOYQT NAMES windeployqt windeployqt6
             HINTS "${_musica_qt_prefix}/bin" REQUIRED)

set(_musica_runtime_dirs "$<TARGET_FILE_DIR:Qt6::Core>" "$<TARGET_FILE_DIR:${MUSICA_TAGLIB_TARGET}>"
    ${MUSICA_RUNTIME_SEARCH_DIRS})
foreach(_musica_vcpkg_root "${VCPKG_INSTALLED_DIR}" "${_VCPKG_INSTALLED_DIR}")
    if(_musica_vcpkg_root AND VCPKG_TARGET_TRIPLET)
        list(APPEND _musica_runtime_dirs "${_musica_vcpkg_root}/${VCPKG_TARGET_TRIPLET}/$<$<CONFIG:Debug>:debug/>bin")
    endif()
endforeach()
install(RUNTIME_DEPENDENCY_SET musica_runtime
        DIRECTORIES ${_musica_runtime_dirs}
        PRE_EXCLUDE_REGEXES "[Aa][Pp][Ii]-[Mm][Ss]-.*" "[Ee][Xx][Tt]-[Mm][Ss]-.*"
                            "[Vv][Cc][Rr][Uu][Nn][Tt][Ii][Mm][Ee].*" "[Mm][Ss][Vv][Cc][Pp].*" "[Cc][Oo][Nn][Cc][Rr][Tt].*"
        POST_EXCLUDE_REGEXES [=[.*[/\\][Ww][Ii][Nn][Dd][Oo][Ww][Ss][/\\][Ss][Yy][Ss][Tt][Ee][Mm]32[/\\].*]=]
                            [=[.*[/\\][Ww][Ii][Nn][Dd][Oo][Ww][Ss][/\\][Ss][Yy][Ss][Ww][Oo][Ww]64[/\\].*]=]
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/DeployWindows.cmake.in"
               "${CMAKE_CURRENT_BINARY_DIR}/DeployWindows.cmake" @ONLY)
install(SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/DeployWindows.cmake")
# windeployqt normally selects qwindows only. Preserve the optional offscreen
# platform for --smoke-test/--screenshot and the clean-PATH deployment check.
if(EXISTS "${_musica_qt_prefix}/plugins/platforms/qoffscreen.dll")
    install(FILES "${_musica_qt_prefix}/plugins/platforms/qoffscreen.dll"
            DESTINATION "${CMAKE_INSTALL_BINDIR}/platforms" CONFIGURATIONS Release RelWithDebInfo MinSizeRel)
endif()
if(EXISTS "${_musica_qt_prefix}/plugins/platforms/qoffscreend.dll")
    install(FILES "${_musica_qt_prefix}/plugins/platforms/qoffscreend.dll"
            DESTINATION "${CMAKE_INSTALL_BINDIR}/platforms" CONFIGURATIONS Debug)
endif()

# The base dependency license texts ship even with SDKs that omit a licenses
# directory. Preserve any SDK-specific third-party notices supplied by users.
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/packaging/windows/licenses/"
        DESTINATION "${CMAKE_INSTALL_DOCDIR}/licenses")
install(FILES "${CMAKE_BINARY_DIR}/DEPENDENCY-VERSIONS.txt"
        DESTINATION "${CMAKE_INSTALL_DOCDIR}" OPTIONAL)
foreach(_musica_license_dir ${MUSICA_DEPENDENCY_LICENSE_DIRS})
    if(NOT IS_DIRECTORY "${_musica_license_dir}")
        message(FATAL_ERROR "Missing dependency license directory: ${_musica_license_dir}")
    endif()
    get_filename_component(_musica_license_name "${_musica_license_dir}" NAME)
    install(DIRECTORY "${_musica_license_dir}/" DESTINATION "${CMAKE_INSTALL_DOCDIR}/licenses/${_musica_license_name}")
endforeach()
if(VCPKG_INSTALLED_DIR AND VCPKG_TARGET_TRIPLET)
    set(_musica_vcpkg_share "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share")
elseif(_VCPKG_INSTALLED_DIR AND VCPKG_TARGET_TRIPLET)
    set(_musica_vcpkg_share "${_VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share")
endif()
foreach(_musica_dependency taglib zlib)
    if(EXISTS "${_musica_vcpkg_share}/${_musica_dependency}/copyright")
        install(FILES "${_musica_vcpkg_share}/${_musica_dependency}/copyright"
                DESTINATION "${CMAKE_INSTALL_DOCDIR}/licenses/${_musica_dependency}")
    endif()
endforeach()

# The package target produces a zip. This additional target is convenient in
# CMake Tools and uses the same install/deployment rules as CPack.
add_custom_target(windows-deploy
    COMMAND "${CMAKE_COMMAND}" --install "${CMAKE_BINARY_DIR}" --config "$<CONFIG>"
            --prefix "${CMAKE_BINARY_DIR}/windows-package"
    DEPENDS musicplayer
    COMMENT "Collecting the Windows executable, Qt/QML plugins, runtime DLLs and notices"
    VERBATIM)
