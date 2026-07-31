cmake_minimum_required(VERSION 3.21)

project(Corvo
    VERSION 2.0.2
    DESCRIPTION "Native WhatsApp Web client for Linux (Qt 6 / Qt WebEngine)"
    HOMEPAGE_URL "https://github.com/Artushenko-Sergei/Corvo"
    LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Versioning - the single source of truth for the whole project
#
# Scheme: Semantic Versioning 2.0.0 with the build number as build metadata,
#         MAJOR.MINOR.PATCH+BUILD (e.g. 1.0.0+12).
#
#   MAJOR  incompatible change in behaviour or in stored data (profile layout,
#          settings keys that are not migrated)
#   MINOR  new functionality, backwards compatible
#   PATCH  bug fix only, no new functionality
#   BUILD  strictly monotonic counter, incremented on EVERY change - even one
#          that does not move MAJOR/MINOR/PATCH (docs, packaging, refactoring)
#
# Bump with scripts/bump-version.sh (it also writes debian/changelog):
#   scripts/bump-version.sh build | patch | minor | major
#
# Everything downstream derives from these two lines: the C++ code, the About
# dialog, --version, the log header, the .deb version and its file name.
# ---------------------------------------------------------------------------
# Reverse-DNS application id. Flathub keys everything off it: the .desktop
# file, the icons and the AppStream metadata must all carry this name.
set(CORVO_APP_ID "io.github.artushenko_sergei.Corvo")

set(CORVO_BUILD 33)
set(CORVO_VERSION_FULL "${PROJECT_VERSION}+${CORVO_BUILD}")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# This is a pure Qt Widgets application: it links QtWebEngineWidgets and never
# loads a QML plugin, so importing the QML plugin targets is skipped. It is a
# no-op on a complete Qt installation and works around incomplete distribution
# packaging where the Qt6Qml CMake configs reference QML plugins that are not
# installed ("The imported target Qt6::lottieqtplugin references the file ...").
set(QT_SKIP_AUTO_QML_PLUGIN_INCLUSION ON)

# ---------------------------------------------------------------------------
# Qt 6.11 or newer is required (QWebEnginePermission API, introduced in 6.8,
# plus PersistentPermissionsPolicy from 6.9). Distribution packages are usually
# older - Debian 12 ships 6.4 - so point CMake at the Qt you installed:
#
#   cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=$HOME/opt/Qt/6.11.1/gcc_64
#
# The default below simply saves the typing when Qt sits in the usual place.
# ---------------------------------------------------------------------------
set(CORVO_QT_ROOT "$ENV{HOME}/opt/Qt/6.11.1/gcc_64" CACHE PATH
    "Qt 6.11+ installation to build against")

if(NOT CMAKE_PREFIX_PATH AND IS_DIRECTORY "${CORVO_QT_ROOT}")
    list(PREPEND CMAKE_PREFIX_PATH "${CORVO_QT_ROOT}")
    message(STATUS "Using Qt from ${CORVO_QT_ROOT} (override with -DCMAKE_PREFIX_PATH=...)")
endif()

find_package(Qt6 6.11 REQUIRED COMPONENTS
    Core
    Gui
    Widgets
    Network
    DBus
    WebEngineCore
    WebEngineWidgets
    LinguistTools)

qt_standard_project_setup()

# qt_standard_project_setup() enables AUTOMOC/AUTOUIC but not AUTORCC, and the
# .qrc is listed as a target source below.
set(CMAKE_AUTORCC ON)

set(CORVO_SOURCES
    main.cpp
    src/AppInfo.h
    src/MainWindow.cpp
    src/MainWindow.h
    src/WebPage.cpp
    src/WebPage.h
    src/WebProfile.cpp
    src/WebProfile.h
    src/Settings.cpp
    src/Settings.h
    src/SettingsDialog.cpp
    src/SettingsDialog.h
    src/TrayManager.cpp
    src/TrayManager.h
    src/NotificationManager.cpp
    src/NotificationManager.h
    src/DeviceCheckDialog.cpp
    src/DeviceCheckDialog.h
    src/Logger.cpp
    src/Logger.h
    src/Localization.cpp
    src/Localization.h)

# Icons and the device-check page are compiled in from resources/resources.qrc
# (via AUTORCC), so the binary works before - and without - installation.
qt_add_executable(Corvo
    ${CORVO_SOURCES}
    resources/resources.qrc)

# ---------------------------------------------------------------------------
# Translations
#
# Source strings are Russian; en/de/uk are shipped as .ts files compiled to .qm
# and embedded into the binary under ":/i18n/" (the default resource prefix of
# qt_add_translations). After changing or adding a tr() string run:
#
#   cmake --build build --target update_translations   # refresh the .ts files
#   <edit translations/Corvo_*.ts in Qt Linguist>
#   cmake --build build                                # compiles the .qm files
# ---------------------------------------------------------------------------
qt_add_translations(Corvo
    TS_FILES
        translations/Corvo_en.ts
        translations/Corvo_de.ts
        translations/Corvo_uk.ts)

target_include_directories(Corvo PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(Corvo PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
    Qt6::DBus
    Qt6::WebEngineCore
    Qt6::WebEngineWidgets)

target_compile_definitions(Corvo PRIVATE
    QT_DISABLE_DEPRECATED_BEFORE=0x060000
    CORVO_VERSION="${PROJECT_VERSION}"
    CORVO_BUILD=${CORVO_BUILD}
    CORVO_VERSION_FULL="${CORVO_VERSION_FULL}"
    CORVO_APP_ID="${CORVO_APP_ID}")

target_compile_options(Corvo PRIVATE -Wall -Wextra -Wpedantic)

set_target_properties(Corvo PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF)

# When Qt does not come from the system, the installed binary must be told where
# to find it - otherwise `cmake --install` produces a /usr/bin/Corvo that
# cannot start ("libQt6Core.so.6: cannot open shared object file"). CMake keeps
# the absolute Qt path in the build tree but strips it on install by default.
get_target_property(_qt_core_location Qt6::Core IMPORTED_LOCATION_RELEASE)
if(NOT _qt_core_location)
    get_target_property(_qt_core_location Qt6::Core IMPORTED_LOCATION)
endif()
if(_qt_core_location)
    get_filename_component(CORVO_QT_LIBDIR "${_qt_core_location}" DIRECTORY)
endif()

# Self-contained package (scripts/make-bundle-deb.sh): Qt sits next to the
# binary in /opt/corvo, so the RPATH must be relative rather than absolute.
option(CORVO_BUNDLE "Link for a self-contained package: RPATH \$ORIGIN/../lib" OFF)

if(CORVO_BUNDLE)
    set_target_properties(Corvo PROPERTIES
        INSTALL_RPATH "$ORIGIN/../lib"
        BUILD_WITH_INSTALL_RPATH OFF)
    message(STATUS "Install RPATH set to \$ORIGIN/../lib (self-contained bundle)")
elseif(CORVO_QT_LIBDIR AND NOT CORVO_QT_LIBDIR MATCHES "^/(usr|lib)")
    set_target_properties(Corvo PROPERTIES
        INSTALL_RPATH "${CORVO_QT_LIBDIR}"
        BUILD_WITH_INSTALL_RPATH OFF)
    message(STATUS "Install RPATH set to ${CORVO_QT_LIBDIR} (Qt outside the system prefix)")
endif()

# ---------------------------------------------------------------------------
# Installation
# ---------------------------------------------------------------------------
include(GNUInstallDirs)

install(TARGETS Corvo
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

install(FILES packaging/${CORVO_APP_ID}.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)

foreach(size 16 22 24 32 48 64 128 256)
    install(FILES resources/icons/Corvo-${size}.png
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${size}x${size}/apps
        RENAME ${CORVO_APP_ID}.png)
endforeach()

install(FILES resources/icons/Corvo.svg
    DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
    RENAME ${CORVO_APP_ID}.svg)

# Pixmap fallback for desktop environments that do not read the hicolor theme.
install(FILES resources/icons/Corvo-64.png
    DESTINATION ${CMAKE_INSTALL_DATADIR}/pixmaps
    RENAME ${CORVO_APP_ID}.png)

# AppStream metadata: required by Flathub, also used by GNOME Software / Discover.
install(FILES packaging/${CORVO_APP_ID}.metainfo.xml
    DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)

install(FILES README.md LICENSE
    DESTINATION ${CMAKE_INSTALL_DOCDIR})

# ---------------------------------------------------------------------------
# CPack: Corvo_<version>+<build>_amd64.deb
# ---------------------------------------------------------------------------
# Scope of this package: it installs the application only, not Qt. Since Qt 6.11
# is newer than what distributions ship, the .deb produced here is a LOCAL
# package - it runs on machines where ${CORVO_QT_LIBDIR} exists (that is the
# INSTALL_RPATH baked in above).
#
# It is deliberately NOT declaring distribution Qt packages as dependencies:
# dpkg-shlibdeps would map the sonames to e.g. "libqt6core6 (>= 6.4.0)", apt
# would happily install that, and the binary would then die with
# "libQt6Core.so.6: version `Qt_6.11' not found". Shipping to machines without
# this Qt requires bundling Qt (AppImage or a self-contained /opt package).
set(CORVO_QT_IS_SYSTEM FALSE)
if(CORVO_QT_LIBDIR MATCHES "^/(usr|lib)")
    set(CORVO_QT_IS_SYSTEM TRUE)
endif()

if(CORVO_QT_IS_SYSTEM)
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
else()
    # Only the toolchain runtime is a real package dependency here; Qt is found
    # through the RPATH.
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS OFF)
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "libc6 (>= 2.34), libstdc++6 (>= 5)")
    message(STATUS
        "Package mode: LOCAL - the .deb expects Qt ${Qt6_VERSION} in "
        "${CORVO_QT_LIBDIR} on the target machine")
endif()

set(CPACK_PACKAGE_NAME "Corvo")
# Debian accepts '+' in an upstream version and compares the digits after it
# numerically, so 1.0.0+9 < 1.0.0+10 sorts correctly and apt upgrades cleanly.
set(CPACK_PACKAGE_VERSION "${CORVO_VERSION_FULL}")
set(CPACK_PACKAGE_VENDOR "Corvo")
set(CPACK_PACKAGE_CONTACT "Serhii Artiushenko <artushenko@protonmail.com>")
set(CPACK_PACKAGE_HOMEPAGE_URL "${PROJECT_HOMEPAGE_URL}")
set(CPACK_PACKAGE_FILE_NAME "Corvo_${CPACK_PACKAGE_VERSION}_amd64")
set(CPACK_STRIP_FILES ON)
set(CPACK_GENERATOR "DEB")

set(CPACK_DEBIAN_PACKAGE_NAME "corvo")
set(CPACK_DEBIAN_FILE_NAME "${CPACK_PACKAGE_FILE_NAME}.deb")
set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "amd64")
set(CPACK_DEBIAN_PACKAGE_SECTION "net")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "${CPACK_PACKAGE_CONTACT}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Native WhatsApp Web client for Linux")
# The summary above becomes the first line of the control Description field; this
# is the indented extended part that follows it.
set(CPACK_DEBIAN_PACKAGE_DESCRIPTION
    "Corvo wraps web.whatsapp.com in a Qt 6 WebEngine window with proper
desktop integration: system tray icon with unread badge, freedesktop
notifications, persistent session storage (the QR code is scanned once),
autostart support and camera/microphone diagnostics for video calls.")

# Refresh the desktop/icon caches on install and removal.
set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA
    "${CMAKE_CURRENT_SOURCE_DIR}/packaging/deb/postinst"
    "${CMAKE_CURRENT_SOURCE_DIR}/packaging/deb/postrm")
set(CPACK_DEBIAN_PACKAGE_CONTROL_STRICT_PERMISSION ON)
# Qt dependencies are decided above: real package dependencies when Qt comes
# from the distribution, RPATH otherwise. Do not add them here.
set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "libnotify-bin, pipewire | pulseaudio, fonts-noto-color-emoji")
set(CPACK_DEBIAN_PACKAGE_SUGGESTS "hunspell-ru, hunspell-en-us")
set(CPACK_DEBIAN_COMPRESSION_TYPE "xz")

include(CPack)

# ---------------------------------------------------------------------------
# Build summary
# ---------------------------------------------------------------------------
message(STATUS "")
message(STATUS "Corvo ${PROJECT_VERSION} (build ${CORVO_BUILD})")
message(STATUS "  Qt version    : ${Qt6_VERSION}")
message(STATUS "  Build type    : ${CMAKE_BUILD_TYPE}")
message(STATUS "  Compiler      : ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "  Generator     : ${CMAKE_GENERATOR}")
message(STATUS "  Install prefix: ${CMAKE_INSTALL_PREFIX}")
message(STATUS "  Package       : ${CPACK_DEBIAN_FILE_NAME}")
message(STATUS "")
