# LBN Coop SA (etapa 1)

Mod cooperativo **original** para GTA San Andreas 1.0 US (gta_sa.exe de 14.383.616 bytes, Windows, 32 bits).
Não contém código de SACoop/CoopAndreas/SA:COOP.

Instruções para jogadores: [`dist/LEIA-ME.txt`](dist/LEIA-ME.txt).

## Estrutura

| Pasta | Conteúdo |
|---|---|
| `src/common/` | Código independente do jogo (compila em Linux e Windows): protocolo binário, interpolação, ini, log, servidor e cliente ENet |
| `src/plugin/` | ASI (`LbnCoopSA.asi`): hooks por *call-site*, executor de comandos SCM, avatares remotos, overlay (CFont), chat/teclas (subclass da janela) |
| `src/server/` | Servidor dedicado (`LbnCoopSA-Server`) |
| `src/tools/` | Cliente de teste sem jogo (`lbn_testclient` → `LbnCoopSA-TesteRede.exe`) |
| `tests/` | Testes unitários + loopback UDP, harness de "jogo falso" (Windows/Wine), script de integração |
| `third_party/enet/` | ENet 1.3.18 (MIT) |
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

## Testes

- `build-native/lbn_tests` — 38 testes (protocolo, fuzz, interpolação, ini, rede real em loopback, timeout, servidor cheio,
  versão errada, nick duplicado, cliente assíncrono sobrevivendo à "pausa").
- `build-win32/lbn_tests.exe` — os mesmos testes, binário Windows, sob Wine.
- `fake_game_harness.exe` — carrega o `.asi` num espaço de endereços simulado do GTA SA 1.0 US e confere hooks,
  bytecode SCM (assinaturas conferidas com a biblioteca do Sanny Builder), criação/warp de avatar e carro, sessão host
  com um cliente de rede real, shutdown.
- `tests/integration.sh` — servidor `.exe` (Wine) + cliente `.exe` (Wine) + cliente Linux.

**Nada disso substitui o teste dentro do GTA SA real**, que ainda não foi feito.
