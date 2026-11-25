#!/bin/sh
# Cross-build with mingw-w64 (i686). Output: glide2x.dll (32-bit).
set -e
cd "$(dirname "$0")"
python3 ../tools/gen_glide_thunks.py .
i686-w64-mingw32-gcc -O2 -Wall -shared -static-libgcc -o glide2x.dll glide2x_log.c glide2x.def -Wl,--enable-stdcall-fixup
