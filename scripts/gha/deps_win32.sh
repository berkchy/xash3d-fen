#!/bin/bash

. scripts/lib.sh

curl -L "http://libsdl.org/release/SDL2-devel-$SDL_VERSION-VC.zip" -o SDL2.zip
unzip -q SDL2.zip
mv "SDL2-$SDL_VERSION" SDL2_VC

if [ "$GH_CPU_ARCH" = "i386" ]; then
	rustup target add i686-pc-windows-msvc
fi

curl -L https://github.com/FWGS/potential-meme/releases/download/prebuilts/mingw-w64-x86_64-pkgconf-1.2.3.0-1-any.pkg.tar.zst -o pkgconf.tar.zst
7z x pkgconf.tar.zst
7z x pkgconf.tar
rm pkgconf.tar*
mv mingw64 pkgconf

# ffmpeg is optional, the engine is built without it if this fails
FFMPEG_ARCHIVE=$(get_ffmpeg_archive)
curl -fL "https://github.com/FWGS/FFmpeg-Builds/releases/download/latest/$FFMPEG_ARCHIVE.zip" -o ffmpeg.zip || true
if unzip -tq ffmpeg.zip > /dev/null 2>&1; then
	unzip -q ffmpeg.zip
	mv "$FFMPEG_ARCHIVE" ffmpeg
else
	echo "WARNING: no usable ffmpeg archive ($FFMPEG_ARCHIVE), building without ffmpeg support"
	rm -f ffmpeg.zip
fi

exit 0
