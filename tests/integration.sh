#!/usr/bin/env bash
# Integration test of the network layer across real processes:
#   - Windows build of the dedicated server (LbnCoopSA-Server.exe) under Wine
#   - one Windows test client (lbn_testclient.exe) under Wine
#   - one native Linux test client
# Usage: tests/integration.sh [port]
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PORT="${1:-7811}"
WINE="${WINE:-/workspace/_tools/lbn/wine-11.20-amd64-wow64/bin/wine}"
export WINEPREFIX="${WINEPREFIX:-/workspace/_tools/lbn/wineprefix}" WINEDEBUG=-all WINEDLLOVERRIDES="mscoree,mshtml="
unset DISPLAY
OUT="$ROOT/build-integration"
mkdir -p "$OUT"; cd "$OUT"; rm -f *.log *.txt

"$WINE" "$ROOT/build-win32/LbnCoopSA-Server.exe" --port "$PORT" --duration-sec 25 > server.txt 2>&1 &
SRV=$!
sleep 4
"$WINE" "$ROOT/build-win32/lbn_testclient.exe" 127.0.0.1 "$PORT" WinBot 12 0 > winclient.txt 2>&1 &
WC=$!
sleep 1
"$ROOT/build-native/lbn_testclient" 127.0.0.1 "$PORT" LinuxBot 12 3.14 > linuxclient.txt 2>&1
LC_RC=$?
wait $WC; WC_RC=$?
wait $SRV; SRV_RC=$?
echo "server rc=$SRV_RC  wine client rc=$WC_RC  linux client rc=$LC_RC"
echo "--- server ---";       tr -d '\r' < server.txt | grep -v "^$" | tail -n 12
echo "--- wine client ---";  tr -d '\r' < winclient.txt | grep -E "CONECTADO|JOIN|CHAT|remoto|RESUMO|LEAVE|FALHA|RECUSADO" | tail -n 8
echo "--- linux client ---"; grep -E "CONECTADO|JOIN|CHAT|remoto|RESUMO|LEAVE|FALHA|RECUSADO" linuxclient.txt | tail -n 8
[ $WC_RC -eq 0 ] && [ $LC_RC -eq 0 ] && [ $SRV_RC -eq 0 ]
