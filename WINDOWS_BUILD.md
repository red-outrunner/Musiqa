# Windows builds

Musica builds as `musicplayer.exe` for native Windows 10/11 x64 using Qt 6,
TagLib 2 and C++17. The project keeps Qt libraries dynamically linked. The
Linux executable cannot be renamed to `.exe`; Windows needs a matching Windows
Qt/TagLib toolchain. The [Windows workflow](.github/workflows/windows.yml)
builds, tests, deploys and archives a native executable on GitHub.
Published packages are available from [Releases](https://github.com/red-outrunner/Musiqa/releases).

## Prerequisites

- Visual Studio 2022 Build Tools with Desktop development with C++, a Windows 10/11 SDK including C++/WinRT headers,
  CMake 3.21 or newer, and Ninja.
- A Windows **MSVC 2022 x64 Qt 6** SDK (6.8.3 in CI), including Qt Quick,
  Quick Controls, Multimedia, SQL/SQLite, Network, SVG and development tools.
  Qt 6.3 is the source compatibility minimum; newer supported Qt builds are
  preferable for current Windows multimedia support.
- TagLib **2 or newer**, matching MSVC/x64. The provided presets use vcpkg's
  dynamic `taglib:x64-windows` package. Install this package in vcpkg and set
  `VCPKG_ROOT` to the vcpkg folder. Ensure the selected Qt SDK is discoverable
  through `CMAKE_PREFIX_PATH`.
- `ffmpeg` and `ffprobe` on PATH for the metadata/audio integration tests.
  End users can play supported formats using Qt's deployed multimedia backend
  without the standalone FFmpeg executable.

Use matching MSVC Qt and TagLib libraries with the same architecture. Native
Windows media controls use the Windows SDK's C++/WinRT interfaces. The included
Windows CI and presets validate MSVC x64.

## Build and package

1. Use an **x64 Native Tools Command Prompt for VS 2022**, or a build environment
   configured for the same MSVC compiler.
2. Configure CMake with the **windows-release** preset, the matching Qt SDK,
   and the vcpkg toolchain.
3. Build the default target. The result is `build-windows/musicplayer.exe`.
4. Run the discovered CMake tests. They include Unicode metadata filenames,
   Open With forwarding, playback, QML interfaces and high-DPI checks.
5. Build the **windows-deploy** target to collect a runnable folder under
   `build-windows/windows-package/`. Start `bin/musicplayer.exe` there.
6. Build **package** to create a versioned Windows ZIP through CPack. The
   GitHub workflow follows the same rules and uploads a ZIP artifact.

The deployment step runs Qt's `windeployqt` with the source QML directory and
collects Qt DLLs, QML imports, SVG/image plugins, platform and multimedia
plugins. CMake separately gathers TagLib and its non-system DLL dependencies.
`MUSICA_RUNTIME_SEARCH_DIRS` can supply extra DLL locations for a manually
installed TagLib SDK. Missing required runtime dependencies fail packaging.

Unzip the **whole package**, preserving its folders; the executable alone is
insufficient. MSVC users need the matching official **Microsoft Visual C++
2015–2022 x64 Redistributable** installed. Musica does not copy individual
compiler runtime DLLs out of the developer's Visual Studio installation.

## Open With

Place the extracted application in its final folder. Run the included
`bin/Register-OpenWith.ps1` in PowerShell to add **Musica** to Windows' **Open
with** choices for the current user. It requires no administrator privileges
and leaves default applications unchanged. Alternatively, use **Open with →
Choose another app** and browse to `bin/musicplayer.exe`. Run
`bin/Unregister-OpenWith.ps1` to remove only Musica's registration. Re-register
if the extracted folder moves.

Opening a file forwards it to the existing player through a per-user named
pipe. File paths containing spaces or Unicode are preserved. Drag and drop
and Ctrl+O use the same local-file playback flow.

## Platform behavior

- Windows uses Qt's normal shared audio output and the audio devices exposed
  by Windows. System equalizers/APOs and virtual audio devices can process
  that shared stream when configured for the selected device.
- MPRIS and the desktop email attachment portal are Linux protocols. Windows
  builds neither require nor link Qt DBus. Copying a song/screenshot and
  saving a shareable copy remain available on Windows.
- User settings, the library database and artwork cache use Qt's per-user
  Windows locations. Music is scanned manually; packaging never includes a
  developer's music or private app state.
- Linux abandoned-audio cleanup uses `/proc` to verify old processes have
  exited. On other platforms those temporary files are not deleted by that
  Linux-specific check; normal playback files are removed when their owning
  player closes.

## Dependency notices and source

The ZIP includes Musica's [GPL license](LICENSE),
[third-party notices](THIRD_PARTY_NOTICES.md), and the principal Qt/TagLib
open-source license texts. With vcpkg, TagLib/zlib notices are also collected.
Set `MUSICA_DEPENDENCY_LICENSE_DIRS` to a semicolon-separated list of your
actual Qt/codec SDK license directories when distributing a release. Include
the version-specific notices and corresponding source required by the
dependencies you ship; the base license texts are not a substitute for those
SDK-specific materials.

References: [Qt Windows deployment](https://doc.qt.io/qt-6/windows-deployment.html),
[TagLib CMake package](https://github.com/taglib/taglib/blob/master/taglib-config.cmake.in),
[vcpkg TagLib port](https://github.com/microsoft/vcpkg/tree/master/ports/taglib),
[Microsoft C++ Redistributable](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist).
