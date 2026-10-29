#!/bin/sh
# Builds ErintesGFX-<version>-x86_64.AppImage on Ubuntu 24.04 (CI, or the erintes-dev box).
# linuxdeploy + its Qt plugin are expected in build-appimage/tools (.github/workflows/appimage.yml fetches them).
set -e
export PATH=/usr/bin:/bin CC=/usr/bin/gcc CXX=/usr/bin/g++
cd "$(dirname "$0")/.."
ROOT=$PWD B=$PWD/build-appimage
VERSION=$(sed -n 's/^project(ErintesGFX VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)
cmake -S . -B $B/release -G Ninja -DCMAKE_BUILD_TYPE=Release > /dev/null
cmake --build $B/release --target erintesgfx
rm -rf $B/AppDir
mkdir -p $B/AppDir/usr/bin
cp tools/ruby_compat.rb $B/AppDir/usr/bin/ # the app loads it from its own folder
cd $B
export QMAKE=/usr/lib/qt6/bin/qmake APPIMAGE_EXTRACT_AND_RUN=1 VERSION
export EXTRA_PLATFORM_PLUGINS="libqwayland-egl.so;libqwayland-generic.so;libqoffscreen.so"
tools/linuxdeploy-x86_64.AppImage --appdir AppDir --executable release/erintesgfx \
  --desktop-file $ROOT/packaging/erintesgfx.desktop --icon-file $ROOT/packaging/erintesgfx.png \
  --plugin qt --output appimage
ls -l ErintesGFX-*.AppImage
