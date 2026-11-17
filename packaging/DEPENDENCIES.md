# Bundled runtime dependencies

Musica's source for this release is supplied alongside the application download
and at https://github.com/red-outrunner/Musiqa. The application is GPL-3.0-or-later.
Its shared libraries remain replaceable; the source build instructions are in
BUILDING.md in that source archive.

- Qt 6.8.3: unmodified shared runtime and QML/plugins from the Qt open-source SDK.
  Corresponding source: https://download.qt.io/archive/qt/6.8/6.8.3/single/
  Module and third-party notices: https://doc.qt.io/qt-6.8/licenses-used-in-qt.html
- Linux TagLib 2.1.1: unmodified shared library.
  Corresponding source: https://github.com/taglib/taglib/tree/v2.1.1
- Windows TagLib and its dependencies: built by vcpkg. The package includes the
  installed port copyright files and a dependency-version manifest.
  Sources and patches: https://github.com/microsoft/vcpkg/tree/master/ports/taglib
- Qt's codec plugins may use FFmpeg shared libraries supplied with the SDK.
  Source and license details: https://ffmpeg.org/legal.html and the Qt notices above.

Qt is available under LGPL/GPL and commercial terms; TagLib under LGPL/MPL.
License texts are included with the package. This release does not modify these
dependencies or bundle a standalone FFmpeg command-line program. System graphics,
audio services, and the Microsoft C++ runtime remain operating-system dependencies.
