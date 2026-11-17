# Licenses and third-party components

Musica is Copyright (C) 2026 **weo sikho fuzile** and contributors, licensed
under **GNU GPL version 3 or, at your option, any later version**
(`GPL-3.0-or-later`). See [LICENSE](./LICENSE). This covers Musica’s own source,
documentation, and original artwork. Dependencies and user-provided music and
artwork retain their own copyrights and licenses.

## Qt 6

Musica dynamically links Qt Core, Gui, Qml, Quick, QuickControls2, Multimedia,
Concurrent, Sql, and Network; Linux desktop integration additionally links
DBus. Windows builds also link Svg and deploy the Qt plugins and QML imports
needed by the application. Tests additionally use Qt Test. Qt's SQL SQLite
driver supplies database support. Qt is Copyright The Qt
Company and other contributors and is offered under commercial and open-source
licenses, including LGPLv3 and GPLv3. The exact options and third-party notices
depend on the Qt version, module, plugins, and distribution used.

- [Qt licensing](https://doc.qt.io/qt-6/licensing.html)
- [Qt LGPL obligations](https://www.qt.io/development/open-source-lgpl-obligations)
- [Qt source](https://code.qt.io/)
- [Qt third-party code](https://doc.qt.io/qt-6/licenses-used-in-qt.html)

## TagLib

Musica dynamically links TagLib 2 or newer to read and write portable metadata
and embedded covers. TagLib is Copyright Scott Wheeler and contributors and
is available under LGPL 2.1 or, at your option, later versions, or MPL 1.1.
Musica does not modify TagLib.

- [TagLib project and source](https://taglib.org/)
- [TagLib licenses](https://github.com/taglib/taglib)

## FFmpeg and system multimedia libraries

When installed, Musica can launch the system’s `ffmpeg` executable to decode
audio. Qt Multimedia may also use FFmpeg and platform multimedia plugins.
FFmpeg builds have different LGPL/GPL licensing options depending on enabled
components. No FFmpeg executable or library is included in this source tree.

- [FFmpeg legal information](https://ffmpeg.org/legal.html)

## Distributing builds

The repository does not bundle Qt, TagLib, audio codecs, or a music collection.
Windows deployment collects dynamically linked dependencies from the selected
SDK; its principal license texts are included in the package, and additional
SDK-specific notices can be supplied as described in [Windows builds](./WINDOWS_BUILD.md).
When distributing binaries or bundled dependencies, supply the applicable
license texts, notices, corresponding source and installation/relinking
information required by the versions actually shipped. Include Musica’s
corresponding source and GPL license. A donation button or paid distribution
does not waive those requirements and does not by itself require a commercial
Qt license. If you use Qt under a separate commercial agreement, that agreement
must also be checked.
