# Building Musica

The application is written in C++17 and QML. A code editor is optional.

## Linux

Install a C++17 compiler, CMake 3.21+, a supported generator, Qt 6.3+ development packages, and TagLib 2+. Required Qt modules are Core, Gui, Qml, Quick, QuickControls2, Multimedia, Concurrent, Sql, Network, and DBus. Runtime packages must include the SQLite SQL driver, Qt Quick Controls/Dialogs, and the SVG image plugin.

Use CMake's `release` configure preset, or an equivalent Release configuration. Build `musicplayer`; build the default target to include tests. The executable is normally `build/musicplayer`.

CMake discovers TagLib through its package configuration or pkg-config. Use `CMAKE_PREFIX_PATH` for dependencies outside the system directories. Linux MPRIS can be disabled with `MUSICA_ENABLE_MPRIS=OFF`.

## Windows

See [Windows builds](./WINDOWS_BUILD.md) for the MSVC/vcpkg prerequisites, deployment, and Open With registration.

## Checks

Keep `BUILD_TESTING=ON` and run the discovered tests through CMake's test integration. Tests require Qt Test, `ffmpeg`, and `ffprobe`; Linux desktop tests additionally use `dbus-daemon`. GUI tests use an offscreen platform and isolated settings.

The executable supports `--smoke-test` to load the interface and exit. On machines without a display, set `QT_QPA_PLATFORM=offscreen` and `QT_QUICK_BACKEND=software`.

## Releases

The [release workflow](./.github/workflows/release.yml) builds Linux and Windows packages on GitHub. A version tag such as `v1.0.1` must match the version in [CMakeLists.txt](./CMakeLists.txt). Packages are smoke-tested before the workflow publishes a GitHub release with checksums and corresponding application source.

Builds and packages stay out of Git. The Linux package bundles its Qt/QML and TagLib runtime using linuxdeploy; the Windows package uses windeployqt. Dependency notices and source locations are included with the downloads.
