#!/usr/bin/env bash
# Downloads the Linux build/test toolchain used for LBN Coop SA into $LBN_TOOLS (default /workspace/_tools/lbn):
#   - llvm-mingw (clang, i686-w64-mingw32 target)      https://github.com/mstorsjo/llvm-mingw
#   - zig (as a native C/C++ compiler), cmake, ninja    via pip in a venv
#   - Wine (Kron4ek build, wow64) for running Windows tests
#   - Ultimate ASI Loader (ThirteenAG, MIT) dinput8.dll for dist/
set -euo pipefail
T="${LBN_TOOLS:-/workspace/_tools/lbn}"
mkdir -p "$T" && cd "$T"
LLVM_VER=20261020
if [ ! -d llvm-mingw-$LLVM_VER-msvcrt-ubuntu-22.04-x86_64 ]; then
  curl -sSL -o llvm-mingw.tar.xz https://github.com/mstorsjo/llvm-mingw/releases/download/$LLVM_VER/llvm-mingw-$LLVM_VER-msvcrt-ubuntu-22.04-x86_64.tar.xz
  tar xf llvm-mingw.tar.xz
fi
if [ ! -x venv/bin/cmake ]; then
  python3 -m venv venv
  venv/bin/pip install -q ziglang cmake ninja
fi
mkdir -p bin
for t in cc c++ ar ranlib; do
  n=$t; [ $t = cc ] && n=zcc; [ $t = c++ ] && n=zcxx; [ $t = ar ] && n=zar; [ $t = ranlib ] && n=zranlib
  printf '#!/bin/sh\nexec %s/venv/bin/python -m ziglang %s "$@"\n' "$T" "$t" > bin/$n; chmod +x bin/$n
done
if [ ! -d wine-11.20-amd64-wow64 ]; then
  curl -sSL -o wine.tar.xz https://github.com/Kron4ek/Wine-Builds/releases/download/11.20/wine-11.20-amd64-wow64.tar.xz
  tar xf wine.tar.xz
fi
if [ ! -f uasi/dinput8.dll ]; then
  curl -sSL -o uasi.zip https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases/download/v10.0.2/Ultimate-ASI-Loader-NoPDB.zip
  mkdir -p uasi && (cd uasi && python3 -c "import zipfile; zipfile.ZipFile('../uasi.zip').extractall('.')")
  curl -sSL -o uasi_license.txt https://raw.githubusercontent.com/ThirteenAG/Ultimate-ASI-Loader/master/license
fi
echo "Ferramentas prontas em $T"
echo "Obs.: o Wine precisa de vm.overcommit_memory=1 nesta VM (sudo sysctl -w vm.overcommit_memory=1)."
