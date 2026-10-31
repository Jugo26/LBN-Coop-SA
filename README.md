# LBN Coop SA (etapa 1, v0.1.1)

Mod cooperativo **original** para GTA San Andreas 1.0 US (gta_sa.exe de 14.383.616 bytes, Windows, 32 bits).
Não contém código de SACoop/CoopAndreas/SA:COOP.

Instruções para jogadores: [`dist/LEIA-ME.txt`](dist/LEIA-ME.txt).

## Download

Baixe o `.zip` da [última versão](https://github.com/Jugo26/LBN-Coop-SA/releases/latest), extraia na pasta do
`gta_sa.exe` e **abra o jogo pelo `LbnCoopSA-Iniciar.exe`**. O iniciador confere se há versão nova, atualiza sozinho
(com backup em `LbnCoopSA_backup\<versão>\`, sem nunca sobrescrever o seu `LbnCoopSA.ini`) e abre o jogo.

**Este repositório não contém nenhum arquivo do GTA San Andreas nem material da Rockstar Games.** Você precisa ter o
jogo original (1.0 US). Host e cliente precisam da mesma versão do mod: a conexão é recusada se forem diferentes.

## Estrutura

| Pasta | Conteúdo |
|---|---|
| `src/common/` | Código independente do jogo (compila em Linux e Windows): protocolo binário, interpolação, ini, log, servidor e cliente ENet |
| `src/plugin/` | ASI (`LbnCoopSA.asi`): hooks por *call-site*, executor de comandos SCM, avatares remotos, overlay (CFont), chat/teclas (subclass da janela) |
| `src/server/` | Servidor dedicado (`LbnCoopSA-Server`) |
| `src/launcher/` | Iniciador/atualizador `LbnCoopSA-Iniciar.exe` (WinHTTP + miniz, sem DLLs extras) e núcleo testável (versão, SHA-256, JSON, regras de arquivos) |
| `src/tools/` | Cliente de teste sem jogo (`lbn_testclient` → `LbnCoopSA-TesteRede.exe`) |
| `tests/` | Testes unitários + loopback UDP, harness de "jogo falso" (Windows/Wine), script de integração |
| `third_party/enet/` | ENet 1.3.18 (MIT) |
| `third_party/miniz/` | miniz 3.0.2 (MIT), só leitura de .zip no iniciador |
| `dist/` | Pacote final |

## Arquitetura (resumo)

- **Rede**: ENet/UDP, porta 7810, 2 canais (0 confiável: controle/chat; 1 não-confiável sequenciado: estado 20 Hz).
  Servidor relay autoritativo para ids/nomes; até 5 jogadores. O host roda o servidor numa thread dentro do jogo e
  conecta em 127.0.0.1. O cliente roda numa thread própria (`AsyncClient`), então a conexão sobrevive ao menu de pausa.
- **Estado do jogador**: posição, velocidade, heading, animação (parado/andar/correr/sprint), vida/colete; no veículo:
  chave do veículo, modelo, posição, quaternion (4×int16), velocidade, cores, vida, banco. Passageiro em carro de outro
  jogador envia só "dono + banco".
- **Interpolação**: buffer de snapshots com relógio do remetente (offset estimado pelo pacote menos atrasado),
  Hermite cúbico (posição+velocidade), slerp (rotação), extrapolação limitada a 250 ms, *snap* em teletransporte ou
  troca de veículo.
- **Jogo**: hooks nos CALLs de `CGame::Process` (0x53E981), desenho 2D (0x53E293), shutdown RW (0x53D910) e
  `CTheScripts::Init` (3 sites). Entidades remotas são criadas com os próprios comandos de script do jogo
  (CREATE_CHAR/CREATE_CAR/WARP_…), executados por um `CRunningScript` privado. Transformação aplicada direto na matriz +
  `CPhysical::RemoveAndAdd`.

## Build no Linux

```bash
scripts/setup_tools.sh        # baixa llvm-mingw, zig/cmake/ninja (pip), Wine, Ultimate ASI Loader
sudo sysctl -w vm.overcommit_memory=1   # só para o Wine nesta VM
scripts/build_all.sh          # build nativo + testes, build win32, testes sob Wine, harness, integração, dist/
```

## Versão e publicação

- A versão fica em `src/common/version.h` (`LBN_MOD_VERSION`); o `scripts/build_all.sh` gera `dist/LbnCoopSA.version`.
- O *handshake* manda a versão do mod; o servidor recusa cliente com versão diferente (mensagem em pt-BR no chat/log
  dos dois lados).
- `scripts/release.sh` compila tudo, gera `release/LbnCoopSA-vX.Y.Z.zip` + `.zip.sha256` e roda
  `gh release create vX.Y.Z` (o iniciador procura exatamente esses dois arquivos na release mais recente).
  Para publicar uma versão nova: altere a versão em `version.h`, faça commit/push e rode `scripts/release.sh`.

## Iniciador (`LbnCoopSA-Iniciar.exe`)

1. Lê `LbnCoopSA.version`; consulta `https://api.github.com/repos/Jugo26/LBN-Coop-SA/releases/latest` (timeout curto).
2. Se a release for mais nova: baixa o `.zip` e o `.sha256`, confere o hash, confere o `LbnCoopSA.version` de dentro do zip.
3. Copia os arquivos que vão mudar para `LbnCoopSA_backup\<versão antiga>\` (nunca apaga nada).
4. Troca **só** arquivos do mod (lista fixa: `LbnCoopSA.asi`, `LbnCoopSA-Iniciar.exe`, `LbnCoopSA.version`,
   `LEIA-ME.txt`, `SHA256SUMS.txt`, `extras\LbnCoopSA-*`, `licencas\*.txt`). `.ini` existente nunca é sobrescrito
   (o novo vira `*-exemplo.ini`); `dinput8.dll` só é trocado se o hash bater com o `SHA256SUMS.txt` instalado; se o `.asi`
   estiver em `scripts\`, é atualizado lá. Se algo falhar no meio, restaura do backup.
5. Grava `LbnCoopSA-update.log` e abre `gta_sa.exe` da mesma pasta. Qualquer erro (offline, limite da API, hash errado)
   => abre o jogo com a versão atual.

Opções: `--sem-atualizar`, e para testes `--nao-abrir-jogo`, `--sem-pausa`, `--api-url=URL`, `--timeout-ms=N`.

## Testes

- `build-native/lbn_tests` — 45 testes (protocolo, fuzz, interpolação, ini, rede real em loopback, timeout, servidor cheio,
  versão de protocolo/mod errada, nick duplicado, núcleo do atualizador (SHA-256, versão, JSON, caminhos), cliente assíncrono sobrevivendo à "pausa").
- `build-win32/lbn_tests.exe` — os mesmos testes, binário Windows, sob Wine.
- `fake_game_harness.exe` — carrega o `.asi` num espaço de endereços simulado do GTA SA 1.0 US e confere hooks,
  bytecode SCM (assinaturas conferidas com a biblioteca do Sanny Builder), criação/warp de avatar e carro, sessão host
  com um cliente de rede real, shutdown.
- `tests/integration.sh` — servidor `.exe` (Wine) + cliente `.exe` (Wine) + cliente Linux.
- `tests/launcher_wine_test.sh` — iniciador sob Wine contra um servidor HTTPS local que imita o GitHub (atualização,
  já atualizado, hash errado, limite da API, JSON inválido, falha no meio com restauração do backup, `dinput8.dll` de
  terceiros, `.asi` em `scripts\`, sem rede, sem `gta_sa.exe`), com um `gta_sa.exe` falso. Usa o build de teste
  `lbn_launcher_testtls.exe` (aceita o certificado local; não vai no `dist/`).
- `tests/launcher_real_test.sh` — o `LbnCoopSA-Iniciar.exe` do `dist/` sob Wine contra a release REAL do GitHub
  (atualizar de 0.0.9, já atualizado, e offline de verdade com `unshare -rn`).

**Nada disso substitui o teste dentro do GTA SA real**, que ainda não foi feito.
