# Contributing to Musica

Musica is created by **weo sikho fuzile**. Contributions are welcome under
**GPL-3.0-or-later**, the same license as the project. You retain copyright in
your contributions. No copyright assignment or separate contributor agreement
is required.

Describe the problem and intended behavior in an issue or pull request. Keep
changes focused, use the existing C++17 and QML conventions, and include a
regression check for a bug when it helps verify the behavior. See [BUILDING.md](./BUILDING.md)
for dependencies, building, and running the test suite.

Use temporary, generated audio in tests. Never commit personal music, embedded
artwork from a music collection, library snapshots, credentials, caches, build
directories, or compiled binaries. The project’s original SVG assets belong
in the source tree.

Preserve manual library scanning and offline metadata editing. Avoid introducing
startup analysis, network lookups, or work on the audio thread that blocks
playback. For third-party code or assets, include their origin and compatible
license in [THIRD_PARTY_NOTICES.md](./THIRD_PARTY_NOTICES.md).

Donations and commercial redistribution are permitted by the GPL. They do not
remove the obligations to provide the corresponding source and preserve the
license and recipients’ freedoms when distributing Musica.
