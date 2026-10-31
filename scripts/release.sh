#!/usr/bin/env bash
# Publishes a release of LBN Coop SA on GitHub (Jugo26/LBN-Coop-SA):
#   1. builds + tests everything (scripts/build_all.sh)  [SKIP_BUILD=1 to reuse dist/]
#   2. zips dist/ -> release/LbnCoopSA-vX.Y.Z.zip and writes release/LbnCoopSA-vX.Y.Z.zip.sha256
#   3. gh release create vX.Y.Z with both files (the launcher looks for exactly these two assets)
# The version comes from src/common/version.h (LBN_MOD_VERSION). The commit must already be pushed.
#   DRY_RUN=1 scripts/release.sh   -> everything except "gh release create"
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
REPO="${LBN_REPO:-Jugo26/LBN-Coop-SA}"
cd "$ROOT"
VER="$(sed -n 's/^#define LBN_MOD_VERSION  *"\([0-9.]*\)".*/\1/p' src/common/version.h)"
[ -n "$VER" ] || { echo "LBN_MOD_VERSION nao encontrado em version.h"; exit 1; }
TAG="v$VER"
echo "== LBN Coop SA $TAG =="

[ -n "${SKIP_BUILD:-}" ] || scripts/build_all.sh
[ "$(tr -d '\r\n' < dist/LbnCoopSA.version)" = "$VER" ] || { echo "dist/LbnCoopSA.version != $VER (rode o build)"; exit 1; }
for f in LbnCoopSA.asi LbnCoopSA-Iniciar.exe LbnCoopSA.version LbnCoopSA.ini dinput8.dll LEIA-ME.txt SHA256SUMS.txt; do
  [ -f "dist/$f" ] || { echo "faltando dist/$f"; exit 1; }
done
(cd dist && sha256sum -c --quiet SHA256SUMS.txt)

if [ -z "${DRY_RUN:-}" ]; then
  if ! git diff --quiet HEAD -- || [ -n "$(git status --porcelain -- dist src)" ]; then
    echo "Ha mudancas sem commit (dist/ ou src/). Faca commit e push antes de publicar."; exit 1
  fi
  git fetch -q origin
  [ "$(git rev-parse HEAD)" = "$(git rev-parse '@{u}' 2>/dev/null || echo none)" ] || { echo "HEAD nao esta no origin (git push primeiro)"; exit 1; }
  if gh release view "$TAG" --repo "$REPO" > /dev/null 2>&1; then echo "Release $TAG ja existe em $REPO"; exit 1; fi
fi

mkdir -p release
ZIP="release/LbnCoopSA-$TAG.zip"
rm -f "$ZIP" "$ZIP.sha256"
python3 scripts/make_zip.py dist "$ZIP"
(cd release && sha256sum "LbnCoopSA-$TAG.zip" > "LbnCoopSA-$TAG.zip.sha256")
cat "$ZIP.sha256"

NOTES="release/notes-$TAG.md"
cat > "$NOTES" <<NOTES_EOF
LBN Coop SA $TAG - mod cooperativo original para GTA San Andreas 1.0 US (PC).

**Como instalar:** extraia o \`LbnCoopSA-$TAG.zip\` na pasta do \`gta_sa.exe\` e abra o jogo pelo **\`LbnCoopSA-Iniciar.exe\`**
(ele atualiza o mod sozinho nas próximas versões e depois abre o jogo). Leia o \`LEIA-ME.txt\`.

- Host e amigo precisam da MESMA versão (a conexão é recusada se forem diferentes).
- Não contém nenhum arquivo do GTA San Andreas / Rockstar Games: você precisa do jogo original.
- SHA-256 do zip: \`$(cut -d' ' -f1 "$ZIP.sha256")\`
NOTES_EOF

if [ -n "${DRY_RUN:-}" ]; then echo "DRY_RUN: nao publicado."; exit 0; fi
gh release create "$TAG" "$ZIP" "$ZIP.sha256" --repo "$REPO" --target "$(git rev-parse HEAD)" \
  --title "LBN Coop SA $TAG" --notes-file "$NOTES" --latest
echo "Publicado: https://github.com/$REPO/releases/tag/$TAG"
