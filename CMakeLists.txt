cmake_minimum_required(VERSION 3.16)

project(MusicPlayerApp VERSION 4.1.3 LANGUAGES CXX)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include(GNUInstallDirs)

find_package(Qt6 6.3 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Multimedia Concurrent DBus)
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(TAGLIB QUIET IMPORTED_TARGET taglib>=2.0)
endif()
if(TARGET PkgConfig::TAGLIB)
    set(MUSICA_TAGLIB_TARGET PkgConfig::TAGLIB)
else()
    # Also accept an unpacked development package when installing system
    # headers isn't possible. The library itself is still linked dynamically.
    find_path(MUSICA_TAGLIB_INCLUDE_DIR fileref.h
        HINTS "${CMAKE_BINARY_DIR}/taglib-sdk/usr/include" PATH_SUFFIXES taglib)
    find_library(MUSICA_TAGLIB_LIBRARY NAMES tag libtag.so.2)
    if(NOT MUSICA_TAGLIB_INCLUDE_DIR OR NOT MUSICA_TAGLIB_LIBRARY)
        message(FATAL_ERROR "TagLib 2 development files are required (e.g. taglib-devel or libtag2-dev). See README.md.")
    endif()
    file(STRINGS "${MUSICA_TAGLIB_INCLUDE_DIR}/taglib.h" _taglib_major_line REGEX "^#define TAGLIB_MAJOR_VERSION [0-9]+")
    string(REGEX MATCH "[0-9]+$" _taglib_major "${_taglib_major_line}")
    if(NOT _taglib_major OR _taglib_major LESS 2)
        message(FATAL_ERROR "TagLib 2 or newer is required for embedded artwork editing.")
    endif()
    add_library(musica_taglib UNKNOWN IMPORTED)
    set_target_properties(musica_taglib PROPERTIES
        IMPORTED_LOCATION "${MUSICA_TAGLIB_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${MUSICA_TAGLIB_INCLUDE_DIR}")
    set(MUSICA_TAGLIB_TARGET musica_taglib)
endif()

add_library(musica_library STATIC tracklibrary.cpp tracklibrary.h libraryfeatures.cpp librarymodels.cpp librarymodels.h albumgrouping.cpp albumgrouping.h listeninghistory.cpp listeninghistory.h mprisservice.cpp mprisservice.h)
target_sources(musica_library PRIVATE audioanalysis.cpp audioanalysis.h musiclibrary.cpp audiomixer.cpp audiomixer.h playbackengine.cpp playbackengine.h)
target_sources(musica_library PRIVATE playbackworker.cpp playbackworker.h playbackstate.h)
target_sources(musica_library PRIVATE filetags.cpp filetags.h metadataediting.cpp)
target_link_libraries(musica_library PUBLIC Qt6::Core Qt6::Gui Qt6::Multimedia Qt6::Concurrent Qt6::DBus)
target_include_directories(musica_library PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(musica_library PRIVATE ${MUSICA_TAGLIB_TARGET})

add_executable(musicplayer
    main.cpp
    qml.qrc
)

target_link_libraries(musicplayer PRIVATE musica_library Qt6::Qml Qt6::Quick Qt6::QuickControls2)
target_compile_definitions(musicplayer PRIVATE MUSICA_VERSION="${PROJECT_VERSION}")

include(CTest)
if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_executable(musica_tests tests/tst_musica.cpp qml.qrc)
    target_link_libraries(musica_tests PRIVATE musica_library Qt6::Test Qt6::Qml Qt6::Quick Qt6::QuickControls2)
    add_test(NAME musica_tests COMMAND musica_tests)
    set_tests_properties(musica_tests PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software"
        TIMEOUT 60)
endif()

install(TARGETS musicplayer
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(FILES assets/musica.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES assets/musica.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
