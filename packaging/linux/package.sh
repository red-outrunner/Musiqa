#!/usr/bin/env bash
set -euo pipefail

source_dir=$(pwd)
build_dir=${1:?Pass the build directory}
version=$(awk '/^project\(MusicPlayerApp VERSION / { print $3 }' CMakeLists.txt)
[[ "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]
package_name="Musica-$version-linux-x86_64"
package_dir="$source_dir/dist/$package_name"
tools_dir="${RUNNER_TEMP:?This script packages the GitHub Linux build}/musica-deploy"
mkdir -p "$package_dir/usr/bin" "$tools_dir"

# Keep downloads and SDKs outside the source archive.
for name in linuxdeploy linuxdeploy-plugin-qt; do
    curl --fail --location --retry 3 \
        "https://github.com/linuxdeploy/$name/releases/download/continuous/$name-x86_64.AppImage" \
        --output "$tools_dir/$name-x86_64.AppImage"
    chmod +x "$tools_dir/$name-x86_64.AppImage"
done
export PATH="$tools_dir:$PATH"
export APPIMAGE_EXTRACT_AND_RUN=1
export QMAKE="$QT_ROOT_DIR/bin/qmake"
export QML_SOURCES_PATHS="$source_dir"
export EXTRA_QT_MODULES="svg;multimedia;sql"
export EXTRA_PLATFORM_PLUGINS="libqoffscreen.so"

linuxdeploy-x86_64.AppImage --appdir "$package_dir" \
    --executable "$build_dir/musicplayer" \
    --desktop-file assets/musica.desktop --icon-file assets/musica.png --plugin qt

# The Qt plugin supplies qt.conf and relocatable library paths. A plain launcher
# makes this an extract-and-run archive without requiring FUSE.
install -m 755 packaging/linux/Musica "$package_dir/Musica"
cp README.md LICENSE THIRD_PARTY_NOTICES.md "$package_dir/"
mkdir -p "$package_dir/licenses"
cp packaging/windows/licenses/*.txt "$package_dir/licenses/"
cp packaging/DEPENDENCIES.md "$package_dir/"

env -u LD_LIBRARY_PATH -u QT_PLUGIN_PATH -u QT_QPA_PLATFORM_PLUGIN_PATH \
    -u QML2_IMPORT_PATH -u QML_IMPORT_PATH \
    QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software \
    XDG_CONFIG_HOME="$RUNNER_TEMP/musica-package-config" \
    XDG_CACHE_HOME="$RUNNER_TEMP/musica-package-cache" \
    "$package_dir/Musica" --smoke-test
tar -C dist -czf "dist/$package_name.tar.gz" "$package_name"
