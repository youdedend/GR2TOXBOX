#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# CI-only MSYS2 build. Desktop FFmpeg prebuilts contain DXVA2/GetDesktopWindow.
# Keep software codecs/containers; no desktop devices, COM capture or GPU APIs.
set -euo pipefail
trap 'status=$?; echo "::error::UWP FFmpeg source build failed (exit $status)"; if [[ -f ffbuild/config.log ]]; then tail -n 18 ffbuild/config.log | while IFS= read -r line; do echo "::error::FFmpeg configure: ${line//%/%25}"; done; fi; exit "$status"' ERR
revision=db69d06eeeab4f46da15030a80d539efb4503ca8 # FFmpeg n7.1.1
prefix=$(cygpath -u "$RUNNER_TEMP")/gr2-ffmpeg-uwp
work=$(cygpath -u "$RUNNER_TEMP")/gr2-ffmpeg-work
mkdir -p "$work" "$prefix"
cd "$work"
if [[ ! -f source/configure ]]; then
    curl --fail --location --retry 3 "https://codeload.github.com/FFmpeg/FFmpeg/tar.gz/$revision" -o source.tar.gz
    mkdir -p source
    tar -xzf source.tar.gz --strip-components=1 -C source
fi
mkdir -p objects
cd objects
# /MT throughout, matching the title, Abseil and protobuf. The final executable
# selects OneCore CRT archives and WindowsApp.lib; these archives have no DLLs.
../source/configure \
    --prefix="$prefix" --target-os=win32 --arch=x86_64 --enable-cross-compile \
    --toolchain=msvc --cc=clang-cl --cxx=clang-cl --ld=lld-link \
    --ar=llvm-ar --nm=llvm-nm --ranlib=llvm-ranlib \
    --extra-cflags='-MT -DWINAPI_FAMILY=WINAPI_FAMILY_APP -D_WIN32_WINNT=0x0A00' \
    --extra-ldflags='-APPCONTAINER WindowsApp.lib' \
    --enable-static --disable-shared --disable-doc --disable-debug --disable-programs \
    --disable-autodetect --disable-everything --disable-network --disable-devices \
    --disable-hwaccels --disable-dxva2 --disable-d3d11va --disable-d3d12va \
    --disable-mediafoundation --disable-vulkan --enable-w32threads \
    --enable-runtime-cpudetect --enable-swscale --enable-swresample --enable-avfilter \
    --enable-decoder=aac,aac_latm,atrac3,atrac3p,atrac9,mp3,pcm_s16le,pcm_s8,h264,mpeg4,mpeg2video,mjpeg,mjpegb,hevc \
    --enable-encoder=pcm_s16le,ffv1,mpeg4,ljpeg,mjpeg --enable-muxer=avi \
    --enable-demuxer=hevc,h264,m4v,mp3,mpegvideo,mpegps,mjpeg,mov,avi,aac,pmp,oma,pcm_s16le,pcm_s8,wav \
    --enable-parser=h264,hevc,mpeg4video,mpegaudio,mpegvideo,mjpeg,aac,aac_latm \
    --enable-protocol=file --enable-bsf=mjpeg2jpeg
# Ensure an SDK/option drift cannot silently restore desktop hardware paths.
grep -q '#define HAVE_UWP 1' config.h
for option in H264_DECODER HEVC_DECODER AAC_DECODER MP3_DECODER; do
    grep -q "#define CONFIG_${option} 1" config.h config_components.h
done
for option in DXVA2 D3D11VA D3D12VA MEDIAFOUNDATION; do
    grep -q "#define CONFIG_${option} 0" config.h
done
make -j"${NUMBER_OF_PROCESSORS:-2}"
make install
# FFmpeg's msvc toolchain uses lib*.a names; COFF archives are linker-compatible.
for lib in avformat avcodec swscale avutil avfilter swresample; do
    test -s "$prefix/lib/lib$lib.a"
done
printf 'UWP software FFmpeg built from %s\n' "$revision"
