# Windows dependency licenses

These unmodified license texts cover the main open-source license options for
the dynamically linked Qt and TagLib dependencies. Musica's GPL is installed
beside THIRD_PARTY_NOTICES.md. Versions actually distributed can include other
components; include the corresponding SDK notices with
`MUSICA_DEPENDENCY_LICENSE_DIRS` when making a release.

- Qt: [license information](https://doc.qt.io/qt-6/licensing.html),
  [third-party notices](https://doc.qt.io/qt-6/licenses-used-in-qt.html),
  [corresponding source](https://download.qt.io/archive/qt/).
- TagLib: [source and licenses](https://github.com/taglib/taglib).
- FFmpeg: [license information and source](https://ffmpeg.org/legal.html).

Keep dynamically loaded Qt libraries replaceable and provide the corresponding
source for the versions shipped when distributing a binary. The Windows build
does not bundle a standalone FFmpeg executable. Qt Multimedia's deployed codec
DLLs and their notices depend on the selected Qt distribution.
