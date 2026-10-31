#!/usr/bin/env bash
# Builds everything, runs every test that can run on Linux, and assembles dist/.
#   scripts/build_all.sh            -> build + tests + dist
#   SKIP_WINE=1 scripts/build_all.sh -> skip the Wine-based tests
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
T="${LBN_TOOLS:-/workspace/_tools/lbn}"
export LLVM_MINGW="${LLVM_MINGW:-$T/llvm-mingw-20261020-msvcrt-ubuntu-22.04-x86_64}"
export PATH="$T/venv/bin:$PATH"
WINE="$T/wine-11.20-amd64-wow64/bin/wine"
cd "$ROOT"

echo "== Build nativo (Linux) =="
CC="$T/bin/zcc" CXX="$T/bin/zcxx" cmake -S . -B build-native -G Ninja \
  -DCMAKE_AR="$T/bin/zar" -DCMAKE_RANLIB="$T/bin/zranlib" > /dev/null
cmake --build build-native
echo "== Testes unitarios + loopback (Linux) =="
./build-native/lbn_tests

echo "== Build Windows x86 (ASI, servidor, testes) =="
cmake -S . -B build-win32 -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-i686-mingw.cmake > /dev/null
cmake --build build-win32

if [ -z "${SKIP_WINE:-}" ]; then
  export WINEPREFIX="${WINEPREFIX:-$T/wineprefix}" WINEDEBUG=-all WINEDLLOVERRIDES="mscoree,mshtml="
  unset DISPLAY
  [ -d "$WINEPREFIX/drive_c" ] || "$T/wine-11.20-amd64-wow64/bin/wineboot" -i > /dev/null 2>&1 || true
  echo "== Testes (build Windows sob Wine) =="
  (cd build-win32 && "$WINE" lbn_tests.exe | tr -d '\r' | tail -n 2)
  echo "== Harness de jogo falso (ASI carregado sob Wine) =="
  mkdir -p build-harness && cp build-win32/LbnCoopSA.asi build-harness/
  (cd build-harness && "$WINE" ../build-win32/fake_game_harness.exe "Z:${ROOT//\//\\}\\build-harness\\LbnCoopSA.asi" 7899 | tr -d '\r' | tail -n 3)
  echo "== Integracao: servidor .exe (Wine) + cliente .exe (Wine) + cliente Linux =="
  tests/integration.sh 7811
fi

echo "== Montando dist/ =="
mkdir -p dist/extras dist/licencas
cp build-win32/LbnCoopSA.asi dist/
cp build-win32/LbnCoopSA-Server.exe dist/extras/
cp build-win32/lbn_testclient.exe dist/extras/LbnCoopSA-TesteRede.exe
cp "$T/uasi/dinput8.dll" dist/dinput8.dll
cp "$T/uasi_license.txt" dist/licencas/Ultimate-ASI-Loader-LICENSE.txt
cp third_party/enet/LICENSE dist/licencas/ENet-LICENSE.txt
(cd dist && sha256sum LbnCoopSA.asi dinput8.dll extras/*.exe > SHA256SUMS.txt)
echo "Pronto: $ROOT/dist"
