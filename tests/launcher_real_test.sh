#!/usr/bin/env bash
# Checks the SHIPPED dist/LbnCoopSA-Iniciar.exe under Wine against the REAL GitHub release (needs internet):
#   R1) old install (files of commit ca65bb4 + LbnCoopSA.version=0.0.9) -> updates to the latest release
#   R2) same folder again -> "ja esta na versao mais recente"
#   R3) offline (unshare -rn: no network at all) -> error logged, game opened with the current version
# Uses a fake gta_sa.exe stub. Usage: tests/launcher_real_test.sh
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
T="${LBN_TOOLS:-/workspace/_tools/lbn}"
LLVM_MINGW="${LLVM_MINGW:-$T/llvm-mingw-20261020-msvcrt-ubuntu-22.04-x86_64}"
WINE="$T/wine-11.20-amd64-wow64/bin/wine"
export WINEPREFIX="${WINEPREFIX:-$T/wineprefix}" WINEDEBUG=-all WINEDLLOVERRIDES="mscoree,mshtml="
unset DISPLAY
W="$ROOT/build-launcherreal"; rm -rf "$W"; mkdir -p "$W"; cd "$W"
FAILS=0
check(){ if eval "$1"; then echo "  [ OK ] $2"; else echo "  [FALHOU] $2"; FAILS=$((FAILS+1)); fi; }
printf '#include <stdio.h>\nint main(int c,char**v){FILE*f=fopen("gta_stub_ran.txt","a");fprintf(f,"STUB gta_sa.exe executado argc=%%d\\n",c);fclose(f);return 0;}\n' > stub.c
"$LLVM_MINGW/bin/i686-w64-mingw32-clang" -O2 -s stub.c -o gta_sa_stub.exe || exit 1
LATEST="$(curl -s https://api.github.com/repos/Jugo26/LBN-Coop-SA/releases/latest | python3 -c 'import json,sys; print(json.load(sys.stdin)["tag_name"])')"
echo "Release mais recente no GitHub: $LATEST"
curl -sSL -o latest.zip "https://github.com/Jugo26/LBN-Coop-SA/releases/download/$LATEST/LbnCoopSA-$LATEST.zip"
run() { local d="$1"; shift; (cd "$d" && timeout 120 "$@" ./LbnCoopSA-Iniciar.exe --sem-pausa 2>&1 | tr -d '\r' > ../"$d".out); sleep 1; sed 's/^/      | /' "$d.out"; }

PREV="${PREV_TAG:-v0.1.0}"
if [ "$PREV" != "$LATEST" ]; then
  echo "== R0) instalacao da release anterior ($PREV, baixada do GitHub) usando o PROPRIO iniciador dela =="
  mkdir P && curl -sSL -o prev.zip "https://github.com/Jugo26/LBN-Coop-SA/releases/download/$PREV/LbnCoopSA-$PREV.zip" && (cd P && unzip -q ../prev.zip)
  cp gta_sa_stub.exe P/gta_sa.exe; sed -i 's/^Nickname=LBN/Nickname=MeuNick/' P/LbnCoopSA.ini; cp P/LbnCoopSA.ini ini.prev
  cp P/LbnCoopSA-Iniciar.exe launcher.prev
  run P "$WINE"
  check '[ "$(tr -d "\r\n" < P/LbnCoopSA.version)" = "${LATEST#v}" ]' "$PREV -> $LATEST pelo iniciador da $PREV"
  check 'cmp -s P/LbnCoopSA.asi <(unzip -p latest.zip LbnCoopSA.asi)' ".asi igual ao da $LATEST"
  check 'cmp -s P/LbnCoopSA-Iniciar.exe <(unzip -p latest.zip LbnCoopSA-Iniciar.exe) && cmp -s P/LbnCoopSA_backup/${PREV#v}/LbnCoopSA-Iniciar.exe launcher.prev' "iniciador trocado pelo novo; o antigo no backup"
  check 'cmp -s P/LbnCoopSA.ini ini.prev' "LbnCoopSA.ini do usuario mantido"
  check 'grep -q STUB P/gta_stub_ran.txt' "jogo (stub) aberto"
fi

echo "== R1) instalacao antiga (0.0.9) -> atualiza pela release real =="
mkdir R && (cd "$ROOT" && git archive ca65bb4 dist) | tar -x -C R --strip-components=1
printf '0.0.9\n' > R/LbnCoopSA.version
cp "$ROOT/dist/LbnCoopSA-Iniciar.exe" R/; cp gta_sa_stub.exe R/gta_sa.exe
sed -i 's/^Nickname=LBN/Nickname=AmigoDoLBN/' R/LbnCoopSA.ini; cp R/LbnCoopSA.ini ini.user; cp R/LbnCoopSA.asi asi.old
run R "$WINE"
check '[ "$(tr -d "\r\n" < R/LbnCoopSA.version)" = "${LATEST#v}" ]' "LbnCoopSA.version = ${LATEST#v}"
check 'cmp -s R/LbnCoopSA.asi <(unzip -p latest.zip LbnCoopSA.asi)' ".asi igual ao da release"
check 'cmp -s R/LbnCoopSA_backup/0.0.9/LbnCoopSA.asi asi.old' "backup do .asi antigo em LbnCoopSA_backup/0.0.9/"
check 'cmp -s R/LbnCoopSA.ini ini.user && [ -f R/LbnCoopSA-exemplo.ini ]' "LbnCoopSA.ini do usuario mantido (+ LbnCoopSA-exemplo.ini)"
check 'cmp -s R/gta_sa.exe gta_sa_stub.exe && grep -q STUB R/gta_stub_ran.txt' "gta_sa.exe (stub) intacto e aberto"
check 'grep -q "SHA-256 ok" R/LbnCoopSA-update.log' "SHA-256 conferido (log)"

echo "== R2) mesma pasta: ja na ultima versao =="
rm -f R/gta_stub_ran.txt
run R "$WINE"
check 'grep -q "versao mais recente" R.out && grep -q STUB R/gta_stub_ran.txt' "'ja esta na versao mais recente' e jogo aberto"

echo "== R3) offline (sem rede nenhuma: unshare -rn) =="
mkdir O && cp -r "$ROOT/dist/." O/ && printf '0.0.9\n' > O/LbnCoopSA.version && cp gta_sa_stub.exe O/gta_sa.exe
(cd O && sha256sum $(find . -type f | sort) > ../O.before)
run O unshare -rn env WINEPREFIX="$WINEPREFIX" "$WINE"
check 'grep -q "sem conexao com o GitHub" O.out' "erro de conexao informado"
check '(cd O && sha256sum -c --quiet ../O.before >/dev/null 2>&1) && [ ! -e O/LbnCoopSA_backup ]' "nada alterado"
check 'grep -q STUB O/gta_stub_ran.txt && grep -q "ERRO" O/LbnCoopSA-update.log' "jogo aberto com a versao atual; erro no LbnCoopSA-update.log"
echo
[ $FAILS -eq 0 ] && echo "LAUNCHER (GitHub real): TODOS OK" || echo "LAUNCHER (GitHub real): $FAILS FALHAS"
[ $FAILS -eq 0 ]
