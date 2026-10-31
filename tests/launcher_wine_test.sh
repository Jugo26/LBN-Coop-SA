#!/usr/bin/env bash
# Tests LbnCoopSA-Iniciar.exe under Wine against a LOCAL HTTPS server (tests/launcher_test_server.py) that imitates
# the GitHub API + release downloads, with a fake gta_sa.exe stub. Uses the test-only build lbn_launcher_testtls.exe
# (same code, but trusts the local self-signed certificate). The real-GitHub checks are in launcher_real_test.sh.
# Usage: tests/launcher_wine_test.sh   (after scripts/build_all.sh)
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
T="${LBN_TOOLS:-/workspace/_tools/lbn}"
WINE="$T/wine-11.20-amd64-wow64/bin/wine"
export WINEPREFIX="${WINEPREFIX:-$T/wineprefix}" WINEDEBUG=-all WINEDLLOVERRIDES="mscoree,mshtml="
unset DISPLAY
W="$ROOT/build-launchertest"; rm -rf "$W"; mkdir -p "$W"; cd "$W"
PORT=18443
FAILS=0
CUR="$(tr -d '\r\n' < "$ROOT/dist/LbnCoopSA.version")"   # installed version in the fake GTA folders
ok()   { echo "  [ OK ] $*"; }
bad()  { echo "  [FALHOU] $*"; FAILS=$((FAILS+1)); }
check(){ if eval "$1"; then ok "$2"; else bad "$2"; fi; }

# --- fake gta_sa.exe ---
cat > stub.c <<'C'
#include <windows.h>
#include <stdio.h>
int main(int argc, char** argv) {
    FILE* f = fopen("gta_stub_ran.txt", "a"); char cwd[MAX_PATH]; GetCurrentDirectoryA(MAX_PATH, cwd);
    fprintf(f, "STUB gta_sa.exe executado cwd=%s argc=%d", cwd, argc);
    for (int i = 1; i < argc; ++i) fprintf(f, " [%s]", argv[i]);
    fprintf(f, "\n"); fclose(f); return 0;
}
C
"$LLVM_MINGW/bin/i686-w64-mingw32-clang" -O2 -s stub.c -o gta_sa_stub.exe || { echo "stub nao compilou"; exit 1; }

# --- release v0.2.0 built from dist/ (with changed files + hostile entries that must be ignored) ---
mkdir -p srv && python3 - "$ROOT/dist" srv/LbnCoopSA-v0.2.0.zip <<'PY'
import os, sys, zipfile
src, out = sys.argv[1], sys.argv[2]
with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
    for root, _, names in os.walk(src):
        for n in sorted(names):
            full = os.path.join(root, n); rel = os.path.relpath(full, src).replace(os.sep, "/")
            data = open(full, "rb").read()
            if rel == "LbnCoopSA.version": data = b"0.2.0\n"
            elif rel in ("LbnCoopSA.asi", "LbnCoopSA-Iniciar.exe", "dinput8.dll", "extras/LbnCoopSA-Server.exe"): data += b"\0LBN-v0.2.0"
            elif rel == "LbnCoopSA.ini": data = data.replace(b"TimeoutMs=10000", b"TimeoutMs=10000\r\nNovaOpcao=1")
            elif rel == "LEIA-ME.txt": data += "\nNovidades da v0.2.0.\n".encode()
            z.writestr(rel, data)
    z.writestr("gta_sa.exe", b"NAO DEVE SER INSTALADO")
    z.writestr("models/gta3.img", b"NAO DEVE SER INSTALADO")
    z.writestr("../fora.txt", b"NAO DEVE SER INSTALADO")
    z.writestr("scripts/LbnCoopSA.asi", b"NAO DEVE SER INSTALADO")
PY
(cd srv && sha256sum LbnCoopSA-v0.2.0.zip > LbnCoopSA-v0.2.0.zip.sha256)
openssl req -x509 -newkey rsa:2048 -nodes -keyout srv/key.pem -out srv/cert.pem -days 2 -subj "/CN=127.0.0.1" > /dev/null 2>&1
python3 "$ROOT/tests/launcher_test_server.py" $PORT srv/cert.pem srv/key.pem srv 2> server.log &
SRVPID=$!
trap 'kill $SRVPID 2>/dev/null' EXIT
sleep 1.5

# fresh "GTA folder" with v$CUR installed and a customised user ini
mkgame() {
  rm -rf "$1"; mkdir -p "$1"; cp -r "$ROOT/dist/." "$1/"
  cp "$ROOT/build-win32/lbn_launcher_testtls.exe" "$1/LbnCoopSA-Iniciar.exe"
  cp gta_sa_stub.exe "$1/gta_sa.exe"
  sed -i 's/^Nickname=LBN/Nickname=MeuNickPersonalizado/' "$1/LbnCoopSA.ini"
  (cd "$1" && sha256sum LbnCoopSA.asi LbnCoopSA-Iniciar.exe dinput8.dll extras/*.exe > SHA256SUMS.txt)
  (cd "$1" && sha256sum $(find . -type f | sort) > ../"$1".before)
}
run() { local d="$1"; shift; (cd "$d" && timeout 120 "$WINE" ./LbnCoopSA-Iniciar.exe --sem-pausa "$@" 2>&1 | tr -d '\r' > ../"$d".out); sleep 1; sed 's/^/      | /' "$d.out"; }
unchanged() { (cd "$1" && sha256sum -c --quiet ../"$1".before > /dev/null 2>&1); }
API="--api-url=https://127.0.0.1:$PORT/api"

echo "== A) atualizacao $CUR -> 0.2.0 (servidor local imitando o GitHub) =="
mkgame A; cp A/LbnCoopSA.asi asi.old; cp A/LbnCoopSA-Iniciar.exe launcher.old; cp A/LbnCoopSA.ini ini.user
run A "$API/ok" --abc
NEWASI="$(unzip -p srv/LbnCoopSA-v0.2.0.zip LbnCoopSA.asi | sha256sum | cut -d' ' -f1)"
check '[ "$(tr -d "\r\n" < A/LbnCoopSA.version)" = 0.2.0 ]' "LbnCoopSA.version = 0.2.0"
check '[ "$(sha256sum < A/LbnCoopSA.asi | cut -d" " -f1)" = "$NEWASI" ]' "LbnCoopSA.asi trocado pelo da v0.2.0"
check 'cmp -s A/LbnCoopSA_backup/$CUR/LbnCoopSA.asi asi.old' "backup do .asi antigo em LbnCoopSA_backup/$CUR/"
check 'cmp -s A/LbnCoopSA_backup/$CUR/LbnCoopSA-Iniciar.exe launcher.old' "backup do iniciador antigo (que estava rodando)"
check 'tail -c 11 A/LbnCoopSA-Iniciar.exe | grep -q LBN-v0.2.0' "iniciador substituido pela versao nova"
check 'cmp -s A/LbnCoopSA.ini ini.user' "LbnCoopSA.ini do usuario NAO foi alterado"
check 'grep -q NovaOpcao=1 A/LbnCoopSA-exemplo.ini' "ini novo salvo como LbnCoopSA-exemplo.ini"
check 'tail -c 11 A/dinput8.dll | grep -q LBN-v0.2.0' "dinput8.dll (o do mod, hash no SHA256SUMS) atualizado"
check 'cmp -s A/gta_sa.exe gta_sa_stub.exe' "gta_sa.exe do zip IGNORADO"
check '[ ! -e A/models ] && [ ! -e fora.txt ] && [ ! -e A/scripts ]' "entradas proibidas/inseguras do zip ignoradas"
check 'grep -q "STUB gta_sa.exe executado" A/gta_stub_ran.txt && grep -q "\[--abc\]" A/gta_stub_ran.txt' "gta_sa.exe aberto (parametro repassado)"
check 'grep -q "SHA-256 ok" A/LbnCoopSA-update.log' "log: SHA-256 ok"
check '[ -z "$(find A -name "*.lbnnovo")" ]' "sem arquivos temporarios sobrando"

echo "== A2) mesma pasta, ja na ultima versao =="
cp "$ROOT/build-win32/lbn_launcher_testtls.exe" A/LbnCoopSA-Iniciar.exe   # (o novo do zip e o build normal)
rm -f A/gta_stub_ran.txt; (cd A && find . -type f ! -name "*.log" | sort | xargs sha256sum > ../A2.before)
run A "$API/ok"
check 'grep -q "versao mais recente (v0.2.0)" A.out' "mensagem 'ja esta na versao mais recente'"
check '(cd A && sha256sum -c --quiet ../A2.before >/dev/null 2>&1)' "nada alterado"
check '[ ! -e A/LbnCoopSA_backup/0.2.0 ]' "nenhum backup novo"
check 'grep -q STUB A/gta_stub_ran.txt' "gta_sa.exe aberto"

echo "== B) hash errado =="
mkgame B; run B "$API/badhash"
check 'grep -q "NAO confere" B.out' "erro de SHA-256 informado"
check 'unchanged B' "nenhum arquivo do mod alterado"
check 'grep -q STUB B/gta_stub_ran.txt' "jogo aberto com a versao atual"

echo "== C) limite da API do GitHub (HTTP 403) =="
mkgame C; run C "$API/ratelimit"
check 'grep -q "limite de consultas da API" C.out' "erro de limite da API informado"
check 'unchanged C && grep -q STUB C/gta_stub_ran.txt' "nada alterado e jogo aberto"

echo "== D) resposta que nao e JSON =="
mkgame D; run D "$API/garbage"
check 'grep -q "JSON invalido" D.out && unchanged D && grep -q STUB D/gta_stub_ran.txt' "erro informado, nada alterado, jogo aberto"

echo "== E) falha no meio da troca -> desfaz pelo backup =="
mkgame E; mkdir -p E/extras/LbnCoopSA-Server.exe.lbnnovo   # pasta no lugar do temporario: a gravacao falha
run E "$API/ok"
check 'grep -q "Desfazendo" E.out' "rollback iniciado"
check '[ "$(tr -d "\r\n" < E/LbnCoopSA.version)" = "$CUR" ]' "continua na v$CUR"
check '(cd E && grep -v -e LbnCoopSA-exemplo.ini ../E.before | sha256sum -c --quiet >/dev/null 2>&1)' "arquivos restaurados (identicos ao original)"
check 'grep -q STUB E/gta_stub_ran.txt' "jogo aberto com a versao atual"

echo "== F) dinput8.dll de outro mod e .asi na pasta scripts\\ =="
mkgame F; printf 'outro loader' > F/dinput8.dll; mkdir -p F/scripts; mv F/LbnCoopSA.asi F/LbnCoopSA.ini F/scripts/
cp F/scripts/LbnCoopSA.ini ini.userF
run F "$API/ok"
check '[ "$(cat F/dinput8.dll)" = "outro loader" ]' "dinput8.dll de terceiros mantido"
check '[ "$(sha256sum < F/scripts/LbnCoopSA.asi | cut -d" " -f1)" = "$NEWASI" ] && [ ! -e F/LbnCoopSA.asi ]' ".asi atualizado em scripts\\ (sem duplicar na raiz)"
check 'cmp -s F/scripts/LbnCoopSA.ini ini.userF && [ -f F/scripts/LbnCoopSA-exemplo.ini ]' "ini em scripts\\ mantido + exemplo ao lado"

echo "== G) sem servidor (porta fechada) e nome inexistente =="
mkgame G; run G --api-url=https://127.0.0.1:1/x --timeout-ms=3000
check 'grep -q "sem conexao com o GitHub" G.out && unchanged G && grep -q STUB G/gta_stub_ran.txt' "porta fechada: erro, nada alterado, jogo aberto"
rm -f G/gta_stub_ran.txt; run G --api-url=https://lbn-nao-existe.invalid/x --timeout-ms=3000
check 'grep -q "nao foi possivel verificar" G.out && grep -q STUB G/gta_stub_ran.txt' "DNS inexistente: erro, jogo aberto"

echo "== H) sem gta_sa.exe na pasta =="
mkgame H; mv H/gta_sa.exe H/outro.exe; run H --sem-atualizar
check 'grep -q "gta_sa.exe nao encontrado" H.out' "erro claro quando o gta_sa.exe nao esta na pasta"

echo
[ $FAILS -eq 0 ] && echo "LAUNCHER (servidor local): TODOS OK" || echo "LAUNCHER (servidor local): $FAILS FALHAS"
[ $FAILS -eq 0 ]
