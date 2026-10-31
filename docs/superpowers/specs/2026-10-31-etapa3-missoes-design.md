# LBN Coop SA, etapa 3: missões da campanha com o host como autoridade

Data: 2026-10-31. Estado: aprovada pelo usuário. Versão alvo: **v0.1.5, protocolo 4** (antes vêm a v0.1.3, teste num PC, e a v0.1.4, combate e carros).

## Intenção

Jogar a campanha em co-op: o host joga a missão de verdade e os amigos acompanham dentro da mesma missão, vendo os inimigos, os aliados, os carros, os textos e os blips, e ajudando a lutar. A primeira missão é **Tagging Up Turf** (Sweet, pichar 6 tags, Ballas). Depois dela, cada missão nova é validada uma por uma no jogo real antes de ser liberada.

## Decisões do usuário (2026-10-31)

1. O amigo **vê e ajuda a lutar**: fere e mata inimigos da missão. Só o host cumpre objetivos (pichar, chegar em pontos, levar alguém).
2. **Só o host avança**: dinheiro, respeito e progresso ficam no jogo do host, que salva quando quiser. O jogo e os saves do amigo não mudam.
3. Primeira missão: **Tagging Up Turf**.
4. Abordagem: **entidades por leitura da lista da missão + interface por repetição de comandos** (a "abordagem 3").
5. Os inimigos **atacam os dois**: o host e o amigo.

## Escopo

Dentro:
- Início e fim da missão no host refletidos nos amigos.
- Fantoches de bonecos, carros e objetos que a missão cria, controlados pelo host.
- Textos de objetivo, título, "missão cumprida/falhou", ajuda e blips do radar repetidos no jogo do amigo.
- Dano do amigo nos fantoches aplicado no host; dano dos inimigos no boneco do amigo (no host) aplicado no amigo.
- Cutscene do host: tela preta no amigo.
- Levar o amigo para perto do host no início da missão, depois da cutscene e pelo comando `/host` (também no menu F8).
- Opção `MissionSync=1` em `[Mundo]` (vale o ini do host) para desligar tudo isso sem nova versão.

Fora (fica para depois):
- Repetir a cutscene no jogo do amigo.
- Tags pichadas mudando de cor no jogo do amigo.
- Tiros, mira e clarão dos inimigos visíveis no amigo (mesmo limite das armas na v0.1.2).
- O amigo cumprir objetivos ou ter progresso no próprio jogo.
- Outras missões além da Tagging Up Turf (a lista de comandos cresce missão a missão).
- Migrar a missão para outro jogador se o host cair.

## Critérios de sucesso

1. Testes unitários, loopback, jogo simulado e Wine passam, incluindo todos os testes atuais (nada da v0.1.2 quebra).
2. No jogo real, host + 1 amigo, Tagging Up Turf do começo até "missão cumprida", duas vezes seguidas, sem crash:
   - o amigo vê o título, os textos de objetivo e os blips das tags e do Sweet;
   - o Sweet e os Ballas aparecem para o amigo e se movem como no host;
   - o amigo mata pelo menos um Balla, e ele morre também no host;
   - um Balla fere o amigo, e a vida do amigo cai no jogo dele;
   - durante a cutscene a tela do amigo fica preta, e no fim ele está perto do host;
   - o amigo morre, volta pelo hospital e o `/host` o leva de volta;
   - "missão cumprida" aparece para os dois e os fantoches somem.
3. Host fecha o jogo no meio da missão: no amigo, fantoches e blips somem e o controle volta em até 10 s.
4. Nenhum arquivo de save é escrito pelo mod, em nenhum dos dois PCs.

## Pré-requisito

Na sessão de teste da v0.1.2 (2026-10-31 15:44), o amigo **caiu por timeout** 24 s depois de ganhar 1 estrela. Antes de qualquer código da etapa 3, descobrir com o `LbnCoopSA.log` do PC do amigo se foi crash. Se for, a correção é a primeira tarefa do plano.

## Arquitetura

| Parte | Onde roda | Função |
|---|---|---|
| `src/plugin/mission_host.cpp` | jogo do host (autoridade) | detecta início/fim, lê a lista da missão, envia entidades e estado, captura comandos de interface, aplica acertos, faz os inimigos odiarem o boneco do amigo |
| `src/plugin/mission_client.cpp` | jogo dos amigos | cria/move/remove fantoches, roda os comandos recebidos, relata acertos, tela preta da cutscene, `/host` |
| `src/plugin/mission_puppet.cpp` | jogo dos amigos | fantoche de boneco, carro ou objeto: posição, rotação, animação de movimento, arma na mão, vida, colisão (mesmas regras dos bonecos de jogador) |
| `src/plugin/script_reader.cpp` | jogo do host | lê os parâmetros de um comando do script da missão (o inverso do `scm::Call`) |
| `src/common/protocol` | todos | mensagens novas, validação e a **lista fixa de comandos sincronizados** (`lbn::isSyncedMissionOpcode`), usada pelo servidor e pelo cliente |
| `src/common/net_server` | servidor | aceita mensagens de missão só da autoridade e repassa; `MissionHit` vai só para a autoridade |

### Detecção da missão (host)

- Em missão: `$ONMISSION != 0`, lido como `int32` em `0xA49960 + *(int32*)0xA476AC` (`CTheScripts::ScriptSpace` + `CTheScripts::OnAMissionFlag`).
- Entidades da missão: `CTheScripts::MissionCleanUp` em `0xA90850`. São 75 entradas de 8 bytes (`int8 type; int32 handle`) mais `int8 m_Count`, com o tipo 1 = carro, 2 = boneco e 3 = objeto. Os outros tipos são ignorados.
- Cutscene: `CCutsceneMgr::ms_running` em `0xB5F851`.

### Captura de comandos (host)

- `CRunningScript::ProcessOneCommand` (`0x469EB0`) despacha cada comando por uma tabela de funções em **`0x8A6168`**, uma por faixa de 100 opcodes (`mov edx, [edx*4+0x8A6168]; push esi; call edx`, com `ecx` = script). Isso foi conferido nos bytes do `gta_sa.exe` 1.0 US do usuário.
- O mod troca só as entradas das faixas que contêm comandos da lista por uma função `__thiscall (script, opcode)`, que:
  1. ignora o próprio script privado do mod (`scm::Call`) e qualquer script que não seja de missão (`CRunningScript+0xDC == 0`);
  2. se o opcode está na lista, lê os parâmetros já resolvidos no IP atual (`script_reader`);
  3. chama a função original;
  4. para `ADD_BLIP_*`, lê depois a variável de saída (o handle do blip no host).
- Handles de boneco, carro ou objeto da missão viram ids de rede. Um handle que não é da missão faz o comando não ser sincronizado (fica no log).
- Ao sair (shutdown) ou com `MissionSync=0`, as entradas originais são restauradas. As entradas são conferidas ao iniciar; se não apontarem para o código esperado, a captura fica desligada e isso vai para o log.

Lista inicial (Tagging Up Turf), com as assinaturas conferidas na biblioteca do Sanny Builder:

| Opcode | Comando | Parâmetros |
|---|---|---|
| 00BA | PRINT_BIG | key, time, style |
| 00BB | PRINT | key, time, flag |
| 00BC | PRINT_NOW | key, time, flag |
| 00BE | CLEAR_PRINTS | — |
| 01E3 | PRINT_WITH_NUMBER_BIG | key, num, duration, style |
| 01E4 | PRINT_WITH_NUMBER | key, num, duration, flag |
| 01E5 | PRINT_WITH_NUMBER_NOW | key, num, duration, flag |
| 03D5 | CLEAR_THIS_PRINT | key |
| 03E5 | PRINT_HELP | key |
| 03E6 | CLEAR_HELP | — |
| 0512 | PRINT_HELP_FOREVER | key |
| 0186 | ADD_BLIP_FOR_CAR | car → blip |
| 0187 | ADD_BLIP_FOR_CHAR | char → blip |
| 0188 | ADD_BLIP_FOR_OBJECT | object → blip |
| 018A | ADD_BLIP_FOR_COORD | x, y, z → blip |
| 02A8 | ADD_SPRITE_BLIP_FOR_COORD | x, y, z, sprite → blip |
| 0164 | REMOVE_BLIP | blip |
| 0165 | CHANGE_BLIP_COLOUR | blip, color |
| 0168 | CHANGE_BLIP_SCALE | blip, size |
| 018B | CHANGE_BLIP_DISPLAY | blip, display |
| 08FB | SET_COORD_BLIP_APPEARANCE | blip, color |

O texto viaja como **chave GXT** (por exemplo `TAG_1`), e o jogo do amigo mostra o texto do próprio GXT dele. Nenhum texto do jogo é copiado para o repositório.

### Protocolo 4 (mensagens novas)

| Tipo | Sentido | Canal | Conteúdo |
|---|---|---|---|
| `MissionBegin` | autoridade → todos | confiável | número da sessão de missão (u16) |
| `MissionEnd` | autoridade → todos | confiável | número da sessão de missão |
| `MissionSpawn` | autoridade → todos | confiável | id de rede (u16), tipo (boneco/carro/objeto), modelo, posição, rotação, cores do carro, arma na mão |
| `MissionDespawn` | autoridade → todos | confiável | id de rede |
| `MissionStates` | autoridade → todos | estado (~15 Hz) | relógio do host, flags (bit 0 = cutscene), e por entidade: id, posição, rotação (4×int16), velocidade, animação, vida (0 = morto), carro e banco (boneco dentro de carro da missão) |
| `MissionCommand` | autoridade → todos | confiável | número de sequência, opcode, parâmetros tipados (int, float, chave de texto ≤ 16, entidade = id de rede, blip = id de rede de blip) |
| `MissionHit` | amigo → servidor → autoridade | confiável | id de rede do alvo, dano × 10, arma |
| `DamageS2C` (já existe) | autoridade → amigo | confiável | atacante especial `0xFE` = inimigo da missão; vale mesmo com `PvP=0` |

Todas as mensagens são idempotentes: um spawn com id que já existe e um blip com id que já existe são ignorados. Por isso o host pode reenviar tudo quando alguém entra, sem lógica especial no servidor.

## Comportamento no jogo do amigo

- **Início:** `MissionBegin` faz aparecer "<apelido do host> começou uma missão". Se o amigo estiver a mais de 150 m do host: tela escurece (`DO_FADE`), `LOAD_SCENE` na posição do host, busca de lugar livre a ~3 m do host (a mesma do `/soltar`), `SET_CHAR_COORDINATES` e a tela volta.
- **Fantoches:** boneco criado com `CREATE_CHAR` de tipo neutro (`PED_TYPE_MISSION2`), com o modelo do host, sem IA e com as mesmas proteções e regras de colisão dos bonecos de jogador. Carro com `CREATE_CAR` e cores; objeto com `CREATE_OBJECT`. A posição vem do estado do host com interpolação. Fantoches a mais de ~250 m do amigo só são criados quando ele chega perto.
- **Ajudar a lutar:** os fantoches mantêm vida 1000 (`kAvatarHealth`, como os bonecos de jogador), e cada queda de vida causada pelo jogador local vira `MissionHit`, o mesmo esquema do PvP na v0.1.2. O host aplica o dano no boneco real com o jogador do host como autor, para que mortes que contam no objetivo sejam contadas. A morte chega ao amigo pelo estado (vida 0).
- **Inimigos atacando o amigo:** no host, o boneco do amigo passa a ser alvo de ódio (`SET_RELATIONSHIP` 0746, tipo 4 = ódio) para cada tipo de boneco da missão que odeia o jogador do host. Isso é desfeito com `CLEAR_RELATIONSHIP` (0747) no fim da missão. Quando o boneco do amigo perde vida no host e o autor (`m_pLastEntityDamage`, `CPed+0x764`) é um boneco da missão, o host manda `DamageS2C` com atacante `0xFE`, e o jogo do amigo aplica vida e colete como no PvP.
- **Cutscene:** com o bit de cutscene ligado, a tela do amigo fica preta com "O host está vendo uma cena" e os controles ficam travados. Quando o bit desliga, a tela volta e, se ele estiver longe, é levado para perto do host.
- **Comandos:** os da lista são rodados com `scm::Call`, trocando ids de rede pelos handles locais. O id de blip de rede é mapeado para o handle do blip local.
- **Morte do amigo:** o jogo dele cuida (hospital). `/host` leva de volta para perto do host, com espera de 5 s entre usos.
- **Missão própria do amigo:** se o `$ONMISSION` do amigo ligar durante a sessão, aparece um aviso no chat e no log ("sua missão não é sincronizada"). Ela não é bloqueada.
- **Fim:** `MissionEnd` remove fantoches e blips. O texto de cumprida ou falhou chega pelos comandos de texto.

## Erros e limites

- **Amigo entra no meio:** o host guarda as entidades vivas, os blips ativos (com o comando que os criou e o último ajuste) e o último texto ainda no tempo, e reenvia tudo quando um jogador novo entra.
- **Pacote de estado perdido:** o próximo corrige. Spawn, despawn e comandos vão pelo canal confiável.
- **Comando citando um fantoche que ainda não existe:** fica numa fila por até 5 s e depois é descartado (com log).
- **Comando fora da lista:** o amigo ignora; o host registra no log uma vez por opcode por missão ("comando 0xXXXX da missão não sincronizado").
- **Host cai ou sai:** no amigo, `Disconnected` ou a perda da autoridade remove fantoches e blips e destrava os controles.
- **Limites:** até 75 entidades (o tamanho da lista do jogo); parâmetros de comando com no máximo 8 valores; chaves de texto com até 16 caracteres.
- **Segurança:** o amigo só roda opcodes da lista fixa, compilada no mod, e o servidor confere a mesma lista e os tamanhos antes de repassar. Mensagens de missão de quem não é a autoridade são descartadas.
- **Saves:** o mod não lê nem escreve arquivos de save.

## Testes

- **Unitários (`tests/test_protocol.cpp` e um novo `tests/test_mission.cpp`):** codificar e ler cada mensagem nova; fuzz de entradas malformadas; limites de 75 entidades e de parâmetros; lista de comandos; leitor de parâmetros com números imediatos, variável global, local de missão, chave de texto curta e longa.
- **Loopback (`tests/test_loopback.cpp`):** mensagens de missão aceitas só da autoridade; `MissionHit` entregue só à autoridade; reenvio a quem entra no meio; `DamageS2C` de missão chega mesmo com `PvP=0`.
- **Jogo simulado (`tests/fake_game_harness.cpp`):**
  - host com `$ONMISSION` falso, lista de missão com 1 boneco e 1 carro, e tabela de comandos falsa em `0x8A6168`; um `PRINT_NOW "TAG_1"` e um `ADD_BLIP_FOR_CHAR` chegam ao cliente de rede com a chave certa e o id de rede no lugar do handle;
  - cliente: spawn vira `CREATE_CHAR`, estado move o fantoche, comando vira `PRINT_NOW` local, queda de vida no fantoche vira `MissionHit`, comando fora da lista é recusado;
  - o executor privado do mod (`scm::Call`) não é capturado.
- **Wine:** `scripts/build_all.sh` completo, com os testes atuais.
- **Jogo real:** roteiro da Tagging Up Turf cobrindo os itens do critério 2 e a queda do host do critério 3, com os logs dos dois PCs.

## Itens a verificar no começo do plano (com plano B)

1. **Tabela `0x8A6168`:** conferir o número de entradas e que os scripts de missão passam por ela. Plano B: gancho no `CALL` do handler da faixa, se houver um ponto de chamada direto.
2. **Leitor de parâmetros:** tipos de parâmetro do SA (imediatos 0x01/0x04/0x05/0x06, variáveis 0x02/0x03, textos 0x09/0x0E/0x0F e variáveis de texto) e locais de missão em `CTheScripts::LocalVariablesForCurrentMission` (`0xA48960`). Plano B: tipo desconhecido faz o comando não ser sincronizado e vai para o log.
3. **Aplicar dano com o jogador do host como autor:** achar a função do jogo (candidata: `CWeapon::GenerateDamageEvent`). Plano B: `SET_CHAR_HEALTH`, sem autoria; nesse caso mortes que só contam se feitas pelo jogador podem não contar.
4. **Tipo de boneco do amigo no host e relação de ódio:** os bonecos de jogador hoje são `PED_TYPE_MISSION1` (24). Conferir se a Tagging Up Turf usa esse tipo para alguém; se usar, trocar o tipo dos bonecos de jogador. Para saber quais tipos odeiam o jogador do host, ler a tabela de relações (`CPedType`, endereço a confirmar). Plano B: dar ódio só aos tipos de bonecos da missão que já feriram o host.
5. **`CREATE_OBJECT` e modelos da missão no amigo:** carregar o modelo antes (como já é feito com bonecos e carros).

## Riscos

- **Endereço errado derruba o jogo.** Mitigação: conferir os bytes de cada endereço ao iniciar, desligar só a parte afetada e registrar no log; `MissionSync=0` desliga tudo sem nova versão.
- **O script da missão pode mandar inimigos atacarem especificamente o CJ**, e esses não vão atrás do amigo. Só o teste real mostra quantos atacam o amigo.
- **Mudar relações de ódio no host pode alterar o comportamento da missão** (por exemplo, inimigos parando para atirar no boneco do amigo). Mitigação: só durante a missão, desfeito no fim, com log.
- **Cada missão nova pode usar comandos fora da lista.** Mitigação: o log do host lista o que faltou, e a lista cresce missão a missão, com teste real de cada uma.
