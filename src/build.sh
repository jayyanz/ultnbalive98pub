#!/bin/sh
# Builds the Main and Debug releases with mingw-w64 (i686, 32-bit).
#   ./build.sh            both builds
#   ./build.sh main       Main only
#   ./build.sh debug      Debug only
# Output: dist/Main/ and dist/Debug/ (complete game-folder file sets: launcher,
# DLLs, SDL3.dll, nba98.ini.default) and dist/NBALive98Fix-<version>-<Main|Debug>.zip.
# Releases never contain nba98.ini: the launcher makes it from nba98.ini.default on
# the first start, so copying a new release over an install keeps the user's settings.
# SDL3.dll (32-bit, 3.2.x) and its licence are taken from third_party/.
# nba98_cutplayers.dat and nba98_jordan.dat are taken from nba98db/out/ (nba98_jordan.dat is
# regenerated with nba98db/build_jordan.py; see nba98db/README.md).
set -e
cd "$(dirname "$0")"
SRC=$(pwd)
CC=i686-w64-mingw32-g++   # C++ since 0.7.15 (restricted subset, see README "C++ rules")
RC=i686-w64-mingw32-windres
VERSION=$(cat VERSION)
MAJ=$(echo $VERSION | cut -d. -f1); MIN=$(echo $VERSION | cut -d. -f2); PAT=$(echo $VERSION | cut -d. -f3)
STAMP=$(date -u +%Y%m%d.%H%M)
python3 tools/gen_glide_stubs.py glide2x

build() {   # $1 = Main or Debug
    V=$1
    if [ $V = Debug ]; then DBG=1; OPT="-O1 -g -fno-omit-frame-pointer"; STRIP=""; else DBG=0; OPT="-O2"; STRIP="-s"; fi
    DEFS="-DNBA98_DEBUG=$DBG -DNBA98_VERSION_MAJOR=$MAJ -DNBA98_VERSION_MINOR=$MIN -DNBA98_VERSION_PATCH=$PAT -DNBA98_BUILD_STAMP=\"$STAMP\""
    B=$SRC/build/$V; O=$SRC/dist/$V
    rm -rf $B $O; mkdir -p $B $O
    LINE="$VERSION $V ($STAMP)"
    res() {   # $1 name, $2 description, $3 file type (1 exe, 2 dll)
        sed -e "s|@DESC@|$2|" -e "s|@NAME@|$1|" -e "s|@TYPE@|$3|" -e "s|@LINE@|$LINE|" -e "s|@BUILD@|$V|" \
            -e "s|@MAJ@|$MAJ|g" -e "s|@MIN@|$MIN|g" -e "s|@PAT@|$PAT|g" -e "s|@DEBUGFLAG@|$( [ $DBG = 1 ] && echo 1 || echo 0 )|" \
            common/version.rc.in > $B/$1.rc
        $RC -O coff -o $B/$1.res $B/$1.rc
    }
    CF="$OPT -std=gnu++17 -fno-exceptions -fno-rtti -fno-threadsafe-statics -Wall -I$SRC/common $DEFS $STRIP -static-libgcc"
    res NBALive98.exe "NBA Live 98 launcher" 1
    $CC $CF -mwindows -o $O/NBALive98.exe launcher/launcher.cpp $B/NBALive98.exe.res -lversion
    res nba98core.dll "NBA Live 98 core fixes" 2
    $CC $CF -shared -o $O/nba98core.dll nba98core/nba98core.cpp $B/nba98core.dll.res -lwinmm -lversion
    res glide2x.dll "NBA Live 98 Glide 2 on Direct3D 11" 2
    $CC $CF -Wno-unused-function -shared -o $O/glide2x.dll glide2x/gl_api.cpp glide2x/gl_d3d.cpp glide2x/gl_win.cpp \
        glide2x/gl_stubs.cpp glide2x/glide2x.def $B/glide2x.dll.res -ld3d11 -ldxgi -luuid -lgdi32
    res eacsnd.dll "NBA Live 98 sound on SDL3" 2
    $CC $CF -shared -o $O/eacsnd.dll eacsnd/eacsnd.cpp eacsnd/eacsnd.def $B/eacsnd.dll.res
    cp third_party/SDL3.dll third_party/SDL3-LICENSE.txt $O/   # SDL3 ships with its zlib licence
    cp third_party/CRT-SHADERS-LICENSE.txt $O/              # MIT notice for the CRT shaders in glide2x.dll
    cp nba98db/out/nba98_cutplayers.dat $O/   # cut players as free agents (our values, see nba98db/README.md)
    cp nba98db/out/nba98_jordan.dat $O/       # Jordan in the Roster Player slot (built by nba98db/build_jordan.py)
    python3 tools/make_ini.py common/nba98.ini.in $V > $O/nba98.ini.default   # never nba98.ini (user settings)
    # no personal art in a release, ever: only these files may be in dist (mj.dat, pictures,
    # face packages and VIV copies are refused). nba98.ini is refused too: a release that
    # carried it would overwrite the user's settings.
    for f in $O/*; do
        case "$(basename "$f")" in
            NBALive98.exe|SDL3.dll|SDL3-LICENSE.txt|CRT-SHADERS-LICENSE.txt|eacsnd.dll|glide2x.dll|nba98core.dll|nba98.ini.default|nba98_cutplayers.dat|nba98_jordan.dat) ;;
            *) echo "build: refusing to package $f (not a release file; personal art never ships)"; exit 1 ;;
        esac
    done
    rm -f dist/NBALive98Fix-$VERSION-$V.zip
    (cd $O && zip -q -9 ../NBALive98Fix-$VERSION-$V.zip *)
    echo "$V $VERSION ($STAMP): $(ls $O | tr '\n' ' ')"
}

case "${1:-all}" in
    main) build Main ;;
    debug) build Debug ;;
    all) build Main; build Debug ;;
    *) echo "usage: $0 [main|debug|all]"; exit 1 ;;
esac
