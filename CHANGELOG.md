# Changelog

## 1.1.0 — não publicada (fork MMadmer)
Junta ao fork tudo da 0.58 até a 0.60.4 do projeto original e as pull requests abertas dele, com
correções de issues ainda abertas, NVIDIA DLSS e otimizações. Obrigado a **Lucas-blip-png,
Oussama268, DUDUYG, jownatan e AjncSystem**, cujos commits entram com a autoria de cada um.

**Projeto original e pull requests**
- **0.58 a 0.60.4 do AjncSystem/ShadowOfTheColossus-PC** (thread de desenhos, tradução de shaders
  e pipelines de compute em threads auxiliares, geração de quadros DLSS-G e FSR 3, menu dentro do
  jogo, texturas na Pascal, memória de vídeo, volume, atualização automática e o resto das notas
  abaixo), somada ao que é próprio do fork (ícones de botões do dispositivo em uso, NGS2/Audio3d,
  modos de janela, idiomas do lançador, instalador). O lançador procura atualizações nos releases
  deste fork.
- **Shaders após atualizar o driver:** a pré-carga do cache monta as pipelines nas threads
  auxiliares (PR #215, Oussama268: 107 s → 6,5 s de tela de carregamento numa RTX 3060).
- **AMD GCN (RX 470–590, Vega):** barreiras de memória compartilhada (PR #205) e a conversão para
  32 lanes por padrão, que acabou com o branco/verde piscando numa RX 570 (#152, #187, #201;
  `SOTC_AMD_WAVE32=0` desliga), de Lucas-blip-png.
- **CPUs de 4 a 8 threads:** a thread de gravação de comandos não fica em espera ativa (PR #198,
  Lucas-blip-png).
- **CPUs sem BMI1/LZCNT/MOVBE** (Ivy Bridge e anteriores, #38, #40, #70, #88): as instruções são
  emuladas em vez de o jogo não abrir (Oussama268, Lucas-blip-png). O `bb-probe.exe` deste fork é
  compilado para x86-64-v3 (AVX2), que essas CPUs não têm: o pacote leva também o
  `bb-probe-compat.exe` (x86-64-v2), que o `sotc.py` abre sozinho nelas (`SOTC_PROBE_COMPAT=1`
  força).
- Leituras da GPU que caem numa janela já em leitura esperam por ela (PR #69, DUDUYG;
  `SOTC_RB_JOIN=0` desliga); temporizador de alta resolução no ritmo dos quadros; chave exata no
  cache de formatos de imagem; cache de shaders próprio sem as ramificações uniformes do wave64
  (PR #93); `tools/SotC-Shaders.ps1` atualizado (PR #81); mensagem clara para arquivos ainda
  criptografados (PR #212); lançador que sobrevive a uma saída padrão recusada (PR #207).

**Travamentos (stutter)**
- **Shaders traduzidos de novo a cada abertura:** os binários de shader eram guardados só pelo
  hash do programa; os de outra versão do tradutor (uma versão anterior ou um pacote de shaders
  antigo) ocupavam os nomes e os desta versão nunca eram guardados, sendo traduzidos de novo em
  pleno jogo a cada sessão. O cache agora leva a versão dos binários e um cache de outra versão é
  separado uma vez.
- **Pacote de shaders de outro perfil** era copiado de novo a cada abertura (19 s) e descartado em
  seguida, junto com o cache montado no jogo; agora é copiado uma vez só.
- **Cache do driver** (VkPipelineCache) guardado com o cache de shaders: as pipelines que o driver
  compilou voltam prontas na próxima abertura, mesmo com o cache do driver da NVIDIA cheio.

**NVIDIA DLSS**
- **DLSS Super Resolution** (placas GeForce RTX): nos modos de resolução de render abaixo de 1080p,
  a cena do jogo é ampliada pelo DLSS em vez do esticamento bilinear do próprio jogo, com a
  profundidade, os vetores de movimento e a câmera do jogo; a interface continua desenhada pelo
  jogo em 1080p. Modos Qualidade (720p), Equilibrado (630p), Desempenho (540p) e Ultra Desempenho
  (360p) (`SOTC_DLSS`). Pode ser ligado e desligado no menu dentro do jogo. O runtime oficial
  NVIDIA Streamline 2.14.16 vai no pacote.
- **Geração de quadros só onde funciona:** DLSS-G aparece para as RTX 40/50; FSR 3 para as demais.
  Uma DLSS-G salva numa placa sem ela passa a automática (FSR 3).

**Lançador, menu e controles**
- **Menu dentro do jogo** (Insert / F1 / L3+R3) e textos do port na tela nos 7 idiomas do
  lançador; chinês, japonês e coreano usam uma fonte do próprio Windows. Saíram as opções que não
  faziam nada neste port (escolha de upscaler, preset, resolução de saída e nitidez). O DLSS e a
  tela cheia (F11) mudam na hora; os efeitos do jogo (vinheta, névoa, SSAO, reflexos completos da
  água) são gravados nas configurações do lançador e valem ao abrir o jogo de novo.
- **Vibração do controle** de 0 a 100% (#210).
- **Botões do mouse** também se trocam na tela de teclas: clicar na ação e apertar o botão (#192).
- **Atualização do jogo** encontrada também em `update` ou `patch` dentro da pasta do jogo ou ao
  lado dela, quando o `param.sfo` é de uma atualização do jogo (#66).

**Correções**
- **Memória de vídeo esgotada** (placas de 4–6 GB, #12, #62, #137): a coleta de texturas
  aprofunda em três níveis (sem travar a thread que segura o cache de texturas) e o pedido é
  repartido em blocos menores; sem memória nenhuma o trecho fica sem memória até a próxima
  tentativa, em vez de fechar o jogo.
- **Jogo fechando ao abrir se o Windows recusar 9152 MB** (#194) e **geração de quadros em PCs com
  AMD e NVIDIA** (#194), de Lucas-blip-png; **FSR 1 sem sincronia na AMD** (#209).
- **Cache de shaders com chaves repetidas** fechava o jogo a cada abertura até ser apagado (#88).
- **Barras da interface cheias ou pretas em 2560x1080** (#202).
- **Relógio do jogo:** a frequência do TSC é medida com amostras pareadas ao longo de 100 ms
  (descompasso entre som e imagem nas cenas longas, #91).
- **Sem som quando o dispositivo de saída ainda não existe ao abrir** (fone Bluetooth, #214): o
  port tenta de novo a cada segundo.
- **Salvar no menu dentro do jogo apagava do `sotc.ini`** as teclas, os remapeamentos do controle
  e os ajustes do `sotc.py`; agora só as linhas do menu são reescritas.

**Desempenho**
- **GTX 10 (Pascal):** o geometry shader que entrega as barycentrics passa os vértices adiante sem
  copiá-los (VK_NV_geometry_shader_passthrough), sobre a implementação da PR #169
  (`SOTC_GS_PASSTHROUGH=0` desliga).
- As tabelas que o jogo replica a cada quadro são comparadas nas threads de cópia.

O cache de shaders é refeito na primeira abertura.

## 0.60.4 — 2026-10-20
- **Blocos ciano nas pedras e flores piscando** (com "Texturas em alta qualidade"): o ajuste da
  0.60.3 que evita a queda trocava o nível de detalhe das texturas a cada um ou dois segundos nas
  áreas grandes (4, 3, 2, 3, 2…), e cada troca fazia o jogo descartar ou recarregar texturas que
  estavam sendo desenhadas. Agora ele só baixa o detalhe com menos de 8% de memória livre e só
  volta acima de 16%. Testado do Novo Jogo+ até o 2º colosso: 3 trocas em vez de ~8, sem blocos
  ciano, sem flores piscando e sem quedas.

## 0.60.3 — 2026-10-20
**O jogo é zerável do início ao fim.** Jogado do save antes do último colosso até a luta com o
Malus, as cenas finais e os créditos, com "Texturas em alta qualidade", modo PS4 Pro e 60 FPS
ligados. Obrigado a **Lucas-blip-png**.

- **Queda com "Texturas em alta qualidade" em vistas amplas** (espada erguida contra o sol, área
  do último colosso; `0x1a371`, #186): quando a memória de texturas do jogo começa a acabar, as
  texturas vistas pedem um nível de detalhe a menos (dois quando está quase cheia) e o jogo libera
  espaço antes de travar. Continua mais nítido que com a opção desligada (PR #191, Lucas-blip-png).
- **Queda no carregamento depois dos créditos** (`0x49500`): a proteção da memória do jogo não
  enxergava os reservatórios que o mundo cria, porque eles crescem em pedaços de 1 MB espalhados;
  agora cada um é protegido onde estão os seus blocos livres (#146).
- **+2,7% de FPS no templo:** leituras da GPU em janelas de 64 KB (PR #196, Lucas-blip-png).

## 0.60.2 — 2026-10-20
- **Ajuste rápido dentro do jogo:** no menu (Insert, F1 ou L3+R3), sem sair do jogo: 60 FPS,
  geração de quadros (desligada / DLSS-G / FSR 3), texturas em alta qualidade, reaproveitar
  sombras distantes e volume. Valem na hora e ficam salvos no lançador para a próxima vez. AF 16x,
  modo PS4 Pro, resolução e uso de memória de vídeo continuam no lançador (precisam reiniciar).
- **Mouse no menu:** com o menu aberto o mouse fica solto e visível para clicar; ao fechar, volta
  a controlar a câmera.
- **Tela de carregamento animada também na montagem dos shaders** ao abrir o jogo (antes a janela
  ficava parada, às vezes por minutos depois de atualizar o driver), com fundo preto opaco e
  animação no centro (círculo girando, título pulsando, "Carregando…").
- Menu com título e Ajuste rápido no idioma do lançador.

## 0.60.1 — 2026-10-20
Obrigado a **nosekefik, Oussama268, plassardewen-cpu e lunar-me**.

- **Menu dentro do jogo** (Insert, F1 ou L3+R3): FPS, placa e memória de vídeo, tela cheia,
  resolução, escala e nitidez, câmera e mouse, efeitos (névoa, vinheta, oclusão, reflexos da
  água). Era o menu do Bloodborne, escondido no port; agora é do SotC e vem ligado
  (`BB_OVERLAY_MENU=0` desliga). Textos em inglês (PR #94, nosekefik).
- **Sobras do Bloodborne removidas** (~11.700 linhas: lançador antigo, patches e mods do
  Bloodborne, ícones, documentos), a partir da PR #60 do Oussama268. O executável do jogo
  (`bb-probe.exe`) passa a ter o ícone do SotC.
- A busca pelo jogo ignora a pasta `WpSystem` do Windows (PR #110, plassardewen-cpu).
- Página do GitHub só em inglês.

## 0.60 — 2026-10-20
Obrigado a **Lucas-blip-png, Einar e KingJhoss**.

**Correções**
- **Texturas nas GTX 1060/1070/1080 (Pascal):** superfícies sem textura, cores chapadas ou quase
  pretas (#57, #68, #102). A Pascal não tem o recurso de interpolação que o jogo usa; ele é
  imitado com um geometry shader (PR #169, Lucas-blip-png).
- **Jogo travando ou fechando na tela de título** em alguns PCs (núcleos da CPU com relógios
  diferentes): o relógio do jogo passa a vir do Windows (PR #174, Einar).
- **Não abria em PCs com pouca memória** (regressão da 0.59): se o Windows recusar os 9152 MB,
  o jogo usa o tamanho do console e avisa no registro.
- **FSR 3: quadrados pretos nos quadros gerados** em AMD RX 7000 (PR #177, KingJhoss).
- **Botão JOGAR cortado** com escala do Windows acima de 100% (PR #171).
- Correção no código herdado do shadPS4: mínimo/máximo atômico com decimais em placas sem
  `VK_EXT_shader_atomic_float2` executava duas operações em vez de uma.

**Novidades**
- **Modo 60 FPS usando o modo Desempenho do próprio jogo** (o do PS4 Pro): o jogo passa a mirar
  60 FPS, então o tempo dele acompanha. Deve corrigir física errada em 60 FPS (#100, #120); ainda
  em teste nesses colossos (PR #183, Lucas-blip-png). `SOTC_MODES=af16` volta ao modo antigo.
- **Cada cópia do port tem a própria pasta `user`** (saves, cache, registros e configurações do
  lançador em `user\launcher_settings.json`). Antes todas usavam a pasta da primeira cópia. Na
  primeira vez, a cópia nova herda as configurações e **copia** os saves e módulos de som.
- **+6% de FPS no templo:** a proteção da memória do jogo não é mais refeita a cada quadro
  (PR #168, Lucas-blip-png).
- Geração de quadros sem a interface (barra de vida sem tremer), **experimental e desligada**:
  `SOTC_FG_HUDLESS=1` (PR #154, Lucas-blip-png).
- Jogando pelo `SotC.bat` o registro também vai para `user\last_run.log` (PR #175, Einar);
  ferramentas de medição corrigidas (PR #181, Einar).

**Conhecido:** "Texturas em alta qualidade" ainda pode fechar o jogo em vistas muito amplas
(espada erguida contra o sol, área do último colosso; #186): desligue-a se acontecer. Personagens
brancos em AMD GCN (RX 470–590, Vega, APUs): em investigação.

## 0.59.1 — 2026-10-20
Obrigado a **Lucas-blip-png e Oussama268**.

- **Sombras distantes piscando** (#178, #157, #163; AMD e NVIDIA GTX 1080, RTX 40/50): a
  reutilização de sombras (desde a 0.58) redesenhava as sombras distantes só a cada dois quadros e
  o jogo não guarda a do quadro pulado. Agora vem desligada (PR #170, Lucas-blip-png). Medido no
  demo inteiro: desligada 61,8 FPS de média / 37,2 de mínima, ligada 59,5 / 35,6, ou seja não
  ganhava FPS. Nova opção **Sombras distantes** no lançador (Desligado / Ligado / Automático).
- **AMD: listras nas sombras e na água** (fork do Oussama268, opt8): o acesso direto à memória
  vem desligado por padrão em placas AMD (dá para ligar no lançador).
- **Intel, correções parciais** (Oussama268): tamanhos de dispatch e layouts válidos, sem
  inteiros de 64 bits quando a GPU não tem, tamanho de subgroup fixo.
- **O registro mostra a placa de vídeo e o driver** (linha `GPU:`), para os relatos nas issues;
  `SOTC_GPU_VENDOR=10de|1002|8086` escolhe a GPU em notebooks com duas (Oussama268).
- Correções menores de revisão (Oussama268).

## 0.59 — 2026-10-20
Obrigado a **Oussama268, Lucas-blip-png e EdouardSence**.

**Desempenho**
- **Mais FPS e menos travadas** (fork do Oussama268, opt5–opt7): os desenhos são preparados numa
  thread própria (#99 Lucas-blip-png), a memória direta é enviada aos poucos e os shaders e
  pipelines de compute são traduzidos em threads auxiliares (o objeto espera alguns quadros em vez
  de a tela travar). Demo da tela de título: média 63 → 65–67 FPS, mínima 30 → 34–37, ponte
  30–32 → 37–40.

**Estabilidade**
- **Queda no final do jogo e ao carregar saves** (`0x1a371` / `0x49500`): a GPU às vezes devolvia
  dados velhos para memória que o jogo já tinha liberado, corrompendo o alocador. A proteção do
  Pink-shadPS4 cuidava de um só dos reservatórios de memória do jogo; agora cobre todos os do
  mesmo tipo, inclusive os criados quando o mundo carrega (área do último colosso). O jogo também
  recebe 9152 MB de memória direta em todos os modos (#146, #149). Causa achada com logs de
  quedas reais; a passagem completa até os créditos ainda está em teste.
- **Placas de 6 GB** fechando por falta de memória de vídeo: tenta o tamanho exato e a RAM do PC
  antes de desistir (#137).
- **Lançador em Windows chinês** quebrava com o "™" do título (#151).

**Novidades**
- **F11** alterna janela / tela cheia (#118, EdouardSence).
- **Uso de memória de vídeo:** Econômico, Padrão, Alto e Máximo (antes o jogo limpava texturas a
  partir de 60% da placa, o "limite de 6 GB" das placas de 8–16 GB) (#143).
- **Volume:** 100 a 300%, sem estourar (#95).
- **Atualização automática:** o lançador avisa quando sai versão nova e instala sozinho, mantendo
  saves, cache de shaders e sotc.ini (#136, Oussama268). Vale a partir desta versão.
- **Ultrawide com o modo PS4 Pro:** 21:9 e 32:9 ficavam com bordas pretas com o modo Pro ligado;
  agora a tela larga tem prioridade (#142, #145).

## 0.58.1 — 2026-10-19
- **Texturas em alta qualidade** (ligada por padrão nos Ajustes rápidos): desde a 0.0.13 o port
  nunca informava ao jogo quais texturas estavam em uso, e ele não carregava as versões de alta
  resolução (folhas da árvore da abertura e pedras borradas). Desligada, volta ao comportamento
  antigo, para placas com pouca memória de vídeo.
- **Ícone da janela do jogo:** um desenho do port (o menino erguendo a espada), para não ficar igual
  ao do lançador na barra de tarefas. Em Imagem e FPS: padrão, o ícone do jogo ou uma imagem sua.

## 0.58 — 2026-10-19
Versão da comunidade: junta as pull requests e forks de várias pessoas, revisados e testados juntos.
Obrigado a **Lucas-blip-png, Gabriell-os, Oussama268, jownatan, xeodeo, nakaionyt/y7ngboy e tabnul**.

**Estabilidade**
- **Quedas por falta de memória de vídeo** (`EnsureResident: Assertion Failed`, placas AMD e de
  4–6 GB): a memória dos buffers vem em blocos de 64 MB (antes milhares de alocações, acima do
  limite dos drivers) e a memória parada é devolvida com o uso, conferindo os dados antes
  (Gabriell-os #98, Oussama268 #59/#83/#103).
- **Jogo congelando** ("GPU hook queue overflow"): a fila de avisos de memória cresce sob demanda.
- **Queda depois do 1º e do 2º colosso no modo PS4 Pro** (`0x1a371`): o modo Pro recebe a
  memória de um PS4 Pro (Lucas-blip-png #106).
- Pacote de preenchimento na fila de compute que corrompia o pacote seguinte; arredondamento RTZ
  desligado na NVIDIA (o driver travava); trava de regiões sem deadlock no Windows (jownatan #81).

**Desempenho:** demo da tela de título 54–55 → **62 FPS de média**, mínima 26 → 31; memória de
vídeo ~4,3 → ~2,7 GB (cortes no processador de comandos, pipelines compilados em segundo plano,
caches lidos em paralelo, sombras distantes reaproveitadas).

**AMD:** bloco preto no canto da tela nas RDNA3/RDNA4 (#43) e texturas BC5/BC6H (xeodeo);
ramos uniformes de wave64 e envio periódico ligados na AMD (Gabriell-os #98). Ainda sem teste
numa AMD aqui: conte nas issues.

**Novidades**
- **Widescreen / ultrawide:** 16:10, 21:9 e 32:9 com imagem mais larga, sem esticar (tabnul #54).
- **FPS: 30 / 60 / ilimitado** no lançador.
- **Ícones dos botões Xbox** no jogo (A B X Y, RB, LT/RT, Menu, View), desenhados para o port;
  opção PlayStation / Xbox em Controles (mecanismo de troca de texturas do xeodeo).
- **Geração de quadros (experimental):** FSR 3 em qualquer placa e DLSS-G nas RTX 40/50 (runtime
  oficial da NVIDIA no pacote, com as licenças), com o jogo a 30 → 60 na tela (nakaionyt/y7ngboy).
  Só faz diferença com o FPS em 30, e a barra de vida/stamina pode tremer.
- Overlay de desempenho (F10), F7 liga/desliga o limite de 30, F8 troca a geração de quadros.
- **Lançador em espanhol, chinês, coreano e russo.**
- Janela com o ícone do jogo e, no título, a versão e o FPS.
- **Lista de saves com a imagem de cada save**, como no PS4 / shadPS4.
- **Tela de carregamento** com barra de porcentagem enquanto o cache de shaders é lido.
- Idioma escolhido já na primeira tela do lançador; Modo PS4 Pro e Nitidez (RCAS) nos Ajustes rápidos.
- O cache de shaders é refeito na primeira abertura.

## 1.0.1 — 2026-10-18 (fork MMadmer)
- **Som do port por padrão:** o lançador não copia mais sozinho os módulos de som de um shadPS4
  instalado (`user/sys_modules`) nem os ativa: a biblioteca de som própria do port toca todos os
  sons. Com os módulos copiados automaticamente foi relatada a falta da maior parte dos efeitos
  sonoros (golpes, cascos, flechas), restando a música e as vozes. As configurações em que um
  lançador anterior fez essa cópia voltam à biblioteca do port; uma pasta escolhida manualmente
  continua valendo.
- **libSceAudio3d e UserFx do NGS2:** o port implementa o libSceAudio3d (objetos posicionados em
  estéreo pelo azimute e pela abertura, tocados no dispositivo de saída padrão) e chama os UserFx
  das vozes do sampler NGS2, pelos quais o motor de som do jogo (sndx) entrega os objetos de áudio
  3D quando esse modo está ativo. Os sons que o PS4 envia ao alto-falante do controle, ao headset
  ou à saída auxiliar também tocam no dispositivo padrão.
- **Modo da janela:** janela, tela cheia em janela (sem bordas, na resolução do monitor) ou tela
  cheia exclusiva (o monitor passa para a resolução de saída), no lugar da caixa "Tela cheia"
  (`SOTC_WINDOW_MODE=windowed|borderless|fullscreen`; a caixa marcada vira tela cheia em janela).
- **Idiomas do lançador:** inglês, russo, chinês (simplificado), japonês e português, com troca
  imediata e fontes do Windows para chinês e japonês; o idioma automático segue o do Windows.
- Textos em russo da documentação revisados em estilo formal.

## 1.0.0 — 2026-10-18 (fork MMadmer)
Primeiro release do fork [MMadmer/ShadowOfTheColossus-PC](https://github.com/MMadmer/ShadowOfTheColossus-PC),
com tudo da 0.57-opt6 e 0.57-opt7.
- **Controle Xbox completo e os botões do dispositivo em uso:** com um controle Xbox (ou
  genérico), o jogo mostra os botões do Xbox (A/B/X/Y, LB/RB, LT/RT, LS/RS, Menu, View, direcional),
  brancos num círculo preto; no teclado e mouse, as teclas e os botões do mouse configurados, no
  mesmo estilo (o direcional e o analógico esquerdo como as quatro teclas, com a da direção acesa;
  a câmera como o mouse); com DualShock ou DualSense, os do jogo. O esquema de Opções > Controles
  vira um controle Xbox, ou as teclas e o mouse de cada linha. Tudo é desenhado por um shader na
  partida (com a montagem dos shaders), bem mais fino que o do jogo e nítido em qualquer
  resolução. As texturas do jogo são reconhecidas pelo conteúdo quando sobem para a GPU e a troca
  é feita no bind; a memória e os arquivos do jogo não mudam. A troca é na hora: conectar,
  desconectar ou usar outro dispositivo troca os botões mostrados e o controle
  (`SOTC_PAD_GLYPHS=xbox|ps|keyboard` fixa; opção **Ícones dos botões** em Controles).
- **Vibração** igual à do PS4 em qualquer controle: o valor que o jogo pede fica até ele mudar
  (antes acabava em 1 s) e passa para um controle conectado no meio.
- **Instalador e zip:** `SotC-PC-Setup-<versão>.exe` instala para o usuário (sem administrador),
  com atalhos e desinstalador que mantém saves e configurações; o zip é a mesma pasta, portátil.
- **Compilar com um comando:** `build.cmd` baixa um MSYS2 próprio em `.deps`, instala os pacotes,
  os submódulos e o PkgTool, compila e abre o lançador; `build.cmd -Package` gera o zip e o
  instalador. Banco de testes em `tools/bench`.
- O som usa a biblioteca própria do port; os módulos de som do console são opcionais.
- Guia em russo: `docs/README_RU.md` (vai no pacote como `README_RU.md`).

## 0.57-opt7 — 2026-10-18
- **Processador de comandos sem espera ativa:** quando todos os pacotes esperam (WAIT_REG_MEM,
  semáforos, contadores CE/DE), ele girava a 100% (relógio e lock a cada volta: um terço do seu
  tempo). Agora, depois de poucas voltas, ele dorme até o thread de desenho executar a próxima
  operação, ou cede o núcleo (SwitchToThread) e cochila quando não há desenho na fila
  (`SOTC_CP_IDLE=0` volta ao anterior). Santuário: 17,6 → 14,3 ms de parse por quadro.
- **Thread de desenho:** os render targets de um desenho com os mesmos registros de alvo e o mesmo
  conjunto de imagens são reaproveitados sem nova busca (`SOTC_RT_MEMO=0` desliga); o teste de
  thread nas leituras de registros não chama mais o winpthreads (~2,5% do thread); marcar de novo
  um buffer já escrito pela GPU não pega o lock.
- **Push descriptors repetidos descartados:** um bind reaproveitado mantém a cópia do flat buffer no
  mesmo command buffer, e o gravador descarta push descriptors e push constants iguais aos últimos
  (`SOTC_PUSH_DEDUPE=0` desliga). Menos chamadas ao driver no thread de gravação.
- **Até 4 núcleos:** o gravador cede o núcleo enquanto espera trabalho (`SOTC_REC_POLITE`) em vez
  de girar com prioridade alta.
- **Som:** o reverb do mixer NGS2 mantém o estado dos filtros em registradores e não aloca buffers a
  cada bloco (mesmo resultado, bit a bit).
- **Resolução de render automática:** na primeira abertura com uma placa o launcher escolhe 810p para
  GTX 1050, MX e placas de 2 GB, 900p para GTX 1050 Ti e placas de até 3 GB, 720p para integradas;
  uma vez por placa, e a escolha feita em Gráficos fica.
- Um T# inválido lido para compilar um shader (escrita via FMask) não encerra mais o jogo.
- Testado (4 núcleos sem SMT + orçamento de 2 GB emulados, RTX 3070 Laptop): santuário 30 FPS
  parado, 29,6 de média andando (antes 29,0); colosso 13 a 810p com corrida e câmera girando
  30,0 FPS (mínimo 30,0), 1,85 núcleos ocupados (antes 2,1), GPU 5,1 ms a 2100 MHz e 1,77 GB de
  VRAM; 62% dos push descriptors do santuário descartados.
- Diagnóstico: `SOTC_PROFILE="A|B|C"` amostra vários threads e `SOTC_PROFILE_STACKS=<arquivo>`
  grava as pilhas inteiras (`tools/profile_tree.py` monta a árvore de chamadas);
  `SOTC_CENSUS_DUMP=<arquivo>` lista as imagens do cache.

## 0.57-opt6 — 2026-10-18
- **PCs de 4 núcleos:** os quatro workers do jogo ficavam consultando a fila de tarefas sem parar
  (no PS4 cada um tem o seu núcleo) e tomavam os núcleos das threads do render. Agora, depois de
  uma sequência de consultas vazias, eles dormem 0,1 ms (`SOTC_POLL_NAP_US`, 0 desliga). Os
  mutexes do PS4 usam SRW locks do Windows. Santuário: 7 → 3,5 núcleos ocupados, 27,4 → 29,9 FPS.
  Em até 4 núcleos as threads de comandos e de desenho rodam com prioridade alta, e o mixer de som
  também (`SOTC_GPU_PRIORITY`).
- **GTX 10 (Pascal):** as barycentrics que os shaders de pixel do jogo leem passam por um geometry
  shader *passthrough* (VK_NV_geometry_shader_passthrough): o shader de pixel resolve as
  barycentrics a partir da posição. Santuário: GPU 16,1 → 13,8 ms no perfil Pascal
  (`SOTC_GS_PASSTHROUGH=0` volta ao anterior). O cache de shaders é refeito na primeira abertura.
- **Resolução de render** (launcher, modos `res900`/`res810`/`res720`/`res960`): o jogo renderiza
  abaixo de 1080p e amplia para 1080p ele mesmo, como o PS4 Pro faz de 1440p para 4K. Texturas e
  névoa iguais.
- **Shaders compilados uma vez:** a pasta `shaderpack` traz o cache de pipelines gravado no jogo;
  na primeira abertura ele vira o cache do jogador e todas as pipelines são montadas na tela de
  carregamento, sem compilar durante o jogo.
- **O launcher desempacota dumps .pkg:** jogo, atualização e todas as DLC encontradas viram as
  pastas `CUSA08809`, `CUSA08809-UPDATE` e `CUSA08809-DLC` ao lado dos pacotes (PkgTool, do
  LibOrbisPkg, LGPL-3.0, em `tools/PkgTool`). DLC que só liberam conteúdo (licença) também.
- **Menos trabalho por desenho:** dois terços dos binds repetem o anterior e reaproveitam os
  descritores (`SOTC_BIND_REUSE=0` desliga); a pesquisa da permutação de shader é memorizada; a
  proteção do heap do jogo nas leituras da GPU percorre a árvore livre no máximo a cada 250 ms.
- **Placas com menos de 3 GB:** a memória de buffers fica em ~45% do orçamento (blocos parados há
  8 s voltam). `SOTC_VRAM_LIMIT_MB` (placa emulada) não recusa mais alocações: a linha `SOTC VRAM`
  mostra quanto passou do tamanho emulado.
- O idioma do sistema segue o idioma do Windows quando o launcher deixa "Automático"; russo na
  lista.
- Testado (4 núcleos sem SMT + orçamento de 2 GB emulados, RTX 3070 Laptop): santuário 30 FPS
  (limite do jogo), demo da tela de título 30,0 FPS (mínimo 29,9).

## 0.57-opt5 — 2026-10-17 (fork Oussama268)
- **Sessões longas sem a memória de vídeo crescer:** o jogo mantém mapeado o seu bloco de 4 GB
  de memória e reaproveita os endereços o tempo todo, então a memória de buffers na GPU só
  crescia (~45 MB por minuto no demo da tela de título: 1,6 → 4,3–4,4 GB em 30 minutos). Numa
  placa de 6 GB isso passava do orçamento depois de ~40 minutos de jogo e os engasgos voltavam.
  Agora, quando a memória de buffers passa de 1,5 GB, os blocos que não foram usados há 30
  segundos e não têm dados escritos pela GPU são devolvidos (no máximo 64 MB a cada 2 segundos,
  ~20 ms cada vez). Antes de devolver, o port lê o bloco de volta e compara com a memória do jogo,
  então a imagem não muda. Em 30 minutos de demo: pico de memória de vídeo **3,4 GB, estável**
  (antes 4,3–4,4 GB e subindo), nenhuma coleta de emergência. `SOTC_ARENA_EVICT_MB=0` desliga;
  `SOTC_ARENA_EVICT_MB`, `SOTC_ARENA_EVICT_IDLE_S` e `SOTC_ARENA_EVICT_STEP_MB` ajustam.
- **Mundo aberto um pouco mais rápido:** quando um comando escreve numa página que o jogo vai ler
  de volta, o port já manda o trabalho para a GPU (`SOTC_RB_EARLY`, agora padrão). Demo: 55,0 →
  56,2 FPS (+2%); santuário igual.
- **Correção de queda rara:** a 0.57-opt4 às vezes fechava ao carregar o save com o erro
  "Attempted to protect region as write-only" (2 vezes em ~16 testes no santuário). A causa
  provável foi corrigida e, se ainda acontecer, a página é protegida por inteiro e o jogo segue;
  o log (`user/last_run.log`) registra a linha "Write-only protection" para investigação.
- Perfil PGO refeito para o código novo.
- Diagnóstico: com `SOTC_FPS=1` o log mostra a linha `SOTC ARENA` (memória devolvida).
- Testado: 30 minutos de demo (memória estável), 15 minutos com o build final, demo e santuário
  sem erro (pares intercalados contra a 0.57-opt4), imagens iguais (folha de capturas).

## 0.57-opt4 — 2026-10-16 (fork Oussama268)
- **Mais FPS em todo lugar:** a thread que transforma os comandos do jogo em comandos Vulkan (o
  gargalo do port) gastava ~42% do tempo dentro do Windows e da biblioteca C: travas, mudanças de
  proteção de página, cópias e alocações. Medido contra a 0.57-opt3, no mesmo PC e na mesma hora
  (RTX 3060 Laptop, pares intercalados):
  - Santuário: 29,3 / 29,1 → **35,0 / 34,1 FPS** (+18%).
  - Demo da tela de título: 47,4 → **53,5 FPS**, 1% low 29,4 → **37,9**, quadros acima de 50 ms
    129 → **26**.
- **O que mudou:**
  - A trava do cache de texturas é pega uma vez por grupo de texturas, não duas por textura (62
    mil vezes por quadro), e do lado da thread de comandos ficou sem instrução atômica. Falhas de
    escrita do jogo em páginas sem textura nem passam pela trava. `SOTC_TC_BATCH=0`,
    `SOTC_TC_BIASED=0` e `SOTC_TC_SKIP_EMPTY=0` desligam as partes.
  - As tabelas que o jogo preenche todo quadro com um shader de cópia (feito na CPU pelo port) são
    comparadas página por página, e só as páginas que mudaram são escritas, direto na memória da
    GPU, sem marcar a memória como alterada: os envios de buffers por quadro caíram de 21 para
    ~13 MB. `SOTC_FILL_HLE_PAGES=0` volta ao jeito antigo.
  - Um par de imagens de profundidade 960x540 era criado e destruído todo quadro; agora é
    reaproveitado ("depth twin", que já existia como opção e passou a ser o padrão).
    `SOTC_DEPTH_TWIN=0` desliga.
  - Buscas de memória mais rápidas: cache maior das regiões de memória do jogo por thread
    (`SOTC_REGION_DIRECT=0` desliga), cache das faixas de buffer já residentes, relógio lido a cada
    8 comandos.
  - Perfil PGO refeito para o código novo.
- Diagnóstico: com `SOTC_CMD_STATS=1` o log mostra a linha `SOTC sys/frame` (proteções de página,
  travas, alocações de memória de vídeo por quadro).
- Testado: santuário e demo sem erro, imagens iguais (folha de capturas), 15 minutos de demo.
## 0.57-opt3 — 2026-10-16 (fork Oussama268)
- **Menos travadas na primeira visita a uma área:** os pipelines gráficos novos agora são
  montados em threads separadas, e o objeto que precisa de um pipeline ainda em montagem não é
  desenhado por alguns quadros (aparece um instante depois) em vez de o jogo inteiro parar. Com o
  cache de shaders vazio, no demo da tela de título: quadros acima de 50 ms 235 → 156, 1% low
  23,9 → 29,1 FPS. Passes de tela cheia (pós-processamento) continuam sendo montados na hora,
  para não aparecer quadro preto. `SOTC_ASYNC_SHADERS=0` volta ao comportamento antigo.
- A tradução dos shaders (a maior parte do que sobrou da travada) ainda roda na thread principal
  de comandos; levá-la para segundo plano é o próximo passo.
- A pasta de patches de shader é listada uma vez só, em vez de duas consultas ao disco por shader.
- Diagnóstico: com `SOTC_FPS=1`, a linha `SOTC COMPILE` mostra onde vai o tempo de compilação.
- Testado: 15 minutos de demo com cache vazio sem erro; com cache cheio, santuário 32,4 FPS e
  demo 50,0 FPS (sem perda).

## 0.57-opt2 — 2026-10-16 (fork Oussama268)
- **Muito menos engasgos ao entrar em áreas novas (placas de 6 GB):** texturas cujo T# começa num
  nível de mip acima de 0 nunca usam os mips mais finos, mas o port guardava a cadeia inteira na
  memória de vídeo. Agora essas texturas só de leitura guardam apenas os mips usados ("mip tail").
  No demo da tela de título o pico de memória de vídeo caiu de ~6,1 GB para ~3,4 GB numa placa
  de 6 GB, os quadros acima de 50 ms caíram de 150 para ~105 e o pior quadro ao carregar uma área
  de 460 ms para ~280 ms. Imagem idêntica. `SOTC_MIP_TAIL=0` desliga.
- **Leituras da GPU assíncronas por padrão:** quando o jogo lê memória escrita pela GPU (todo
  quadro), só a thread do jogo espera a GPU; o processador de comandos segue trabalhando. Santuário
  29,2 → 30,7 FPS; no demo, ~25% menos quadros acima de 50 ms. `SOTC_RB_ASYNC=0` desliga.
- **Memória de buffers devolvida ao desmapear:** antes ela nunca voltava e a memória de vídeo
  crescia ~90 MB por minuto (1,6 → 6,0 GB em 48 minutos no demo), até faltar memória. Agora cresce
  bem mais devagar (24 minutos: 4,1 GB em vez de 4,8–5,0 GB). Era opcional por causa de uma queda
  antiga ao carregar save, que não se repetiu nos testes (3 carregamentos, 25 minutos de demo).
  `SOTC_ARENA_RELEASE=0` volta ao comportamento antigo.
- Com tudo isso ligado, o demo rodou a 49,9 FPS de média com 47 quadros acima de 50 ms (antes:
  ~47 FPS e 100–150).
- **Quedas deixam registro:** o lançador guarda os logs das últimas 5 sessões (`last_run.1.log`
  …) e copia o log de uma sessão que terminou com erro para `user/crashes/`; o bb-probe grava ali
  um minidump (`.dmp`) quando o jogo cai. `SOTC_MINIDUMP=0` desliga o dump.
- **Relatório de engasgos para desenvolvedores:** com `SOTC_FPS=1` o log mostra a cada 5 s o
  tempo de quadro (mediana, 1% low, pior quadro) e onde foi o tempo dos quadros lentos;
  `tools/frame_report.py` resume um log.
- Memória de buffers em blocos de 4 MB em vez de uma alocação por pedido (`SOTC_ARENA_SLAB_MB`),
  cópia de envios grandes em paralelo (`SOTC_COPY_BATCH`).
- **Ainda não resolvido:** na primeira vez em cada área os shaders são compilados e o jogo engasga
  (até 1–2 s); da segunda vez em diante fica liso. Compilação em segundo plano é o próximo passo.
- Na primeira abertura alguns shaders novos são compilados (poucos segundos); o cache existente
  continua valendo.

## 0.57 — 2026-10-14
- **Placas AMD: correção das cores verdes/rosas** na névoa, no Wander e no Agro (issues #6, #7, #9,
  possivelmente #10). A AMD informa grupos de 64 threads, mas só deixa fixar esse tamanho nos
  shaders de compute; o port exigia 64 em todos os estágios e deixava de converter os shaders de
  compute. Agora as placas AMD usam o mesmo caminho de 32 threads da NVIDIA (compute fixado em 32,
  barreiras de memória compartilhada, sem a otimização de ramos uniformes que deixava os
  personagens pretos na sombra). Diagnóstico de raulbezerro-dev na issue #7. Na NVIDIA nada muda.
  **Ainda não testado numa placa AMD:** conte na issue se melhorou.
- **Opção nova no lançador:** "Reflexos completos da água (acesso direto à memória)", ligada por
  padrão. Desligada, ganha ~10% de FPS (demo da tela de título: 54 → 59), mas a água distante
  fica mais lisa. Cada modo tem a sua própria pasta de cache de shaders.
- O cache de shaders é refeito na primeira abertura.

## 0.56 — 2026-10-13
- **Água do mundo aberto com ondas:** lagos e rios abaixo das cascatas ficavam lisos como espelho,
  sem movimento. O shader dessa água guarda a máscara de threads ativas com `s_wqm_b64 vcc, exec`
  e depois decide por ela se aplica o mapa de fluxo (as ondas). O recompilador tratava
  `S_WQM_B64` como "não faça nada", o VCC ficava com lixo e o shader sempre escrevia uma normal
  reta para cima. Agora a instrução faz o que o hardware faz: expande a máscara para quads
  inteiros de 2×2 pixels. Uma simples cópia corrigia a água, mas deixava "lixo colorido" nas copas
  das árvores distantes, que usam a expansão no teste de transparência. Detalhes em
  `docs/sotc/AGUA.md`.
- **Acesso direto à memória (DMA) ligado por padrão,** como na configuração do Pink-shadPS4 para
  o SotC. Leituras de tabelas com índice calculado no shader (os registros de *probe* usados pelo
  reflexo em espaço de tela) liam sempre a posição 0; agora leem o endereço certo. Com isso o
  lago de cima ondula e reflete até longe. `BB_DIRECT_MEMORY_ACCESS=0` desliga.
- O cache de shaders é refeito na primeira abertura (o primeiro carregamento engasga).

## 0.0.13 — 2026-10-13
- **Correção de queda da 0.0.12:** o jogo podia fechar na abertura ou no menu (falha em `0x1a371`,
  1 em 3 testes). Causa: o port marcava cada textura vista como "ainda em uso" por ~3 segundos, e o
  jogo tentava carregar mais texturas do que cabe no seu pool. Agora só contam as texturas usadas
  desde o último pedido (como no Pink-shadPS4). Testado: 0 quedas em 5 rodadas da abertura e uma
  sessão de quase 40 minutos andando pelo mapa, com dois carregamentos de save.
- **Reflexos da água bem maiores:** o lago agora reflete paredões, templos e vegetação em quase
  toda a superfície, com ondulação. Um limite de segurança de laços nos shaders cortava os raios
  do reflexo em espaço de tela; agora o limite é por laço.
- Em investigação: onde a água deveria refletir o céu, ela ainda fica escura e lisa.

## 0.0.12 — 2026-10-13
- **Texturas nítidas de volta, sem o jogo fechar:** a corrupção de memória que fazia o jogo fechar
  logo depois de carregar um save (falha em `0x1a371`) e deixava texturas com "lixo colorido" vinha
  do emulador copiando dados da GPU para a memória que o jogo estava liberando. Isso foi corrigido,
  e o streaming de texturas voltou ligado no nível 2 (texturas de 2048 até 512×512): pedras e
  paredes bem mais nítidas. Testado por mais de 25 minutos perto da cascata, sem queda.
- **Água:** respingos, anéis e espuma em volta do Wander ao nadar; o lago reflete o céu e o
  brilho do sol. Os reflexos de ambiente (passada de IBL local) agora usam o *cubemap* certo de
  cada *probe* — correção trazida do Pink-shadPS4 (`dynamic_tsharp_array_size`).
- Em investigação: a água perto da câmera ainda fica sem ondulação.
- Opções para teste: `SOTC_LOD_STATS=0` (streaming desligado), `SOTC_LOD_LEVEL`,
  `SOTC_UNMAP_READBACK=1`, `SOTC_TSHARP_ARRAY=0`.

## 0.0.11 — 2026-10-13
- **Correção da 0.0.10 — grama com textura de "lixo colorido":** no nível 3 do streaming o
  jogo liberava mips que a GPU ainda usava (visto na grama perto da cascata). O streaming de
  texturas agora vem **desligado**, como na 0.0.9: grama normal, cortinas das cascatas visíveis e
  sem as quedas ao carregar um save. Para testar o streaming: `SOTC_LOD_STATS=1` (com
  `SOTC_LOD_LEVEL=2` fica mais nítido, mas pode fechar o jogo).
- Em investigação: a água parada (lago como espelho, sem ondas) e o borrifo das cascatas.

## 0.0.10 — 2026-10-13
- **Gravação corrigida:** o jogo nunca conseguia gravar ("sem espaço livre"); agora grava.
- **Lista de saves na tela:** "Carregar jogo" e "Gravação manual" mostram os saves (região,
  tempo jogado, data) para escolher, com "+ Nova gravação" ao gravar. Setas/direcional para
  escolher, Enter/X para confirmar, Esc/O para voltar, duplo clique com o mouse.
- **Excluir save:** na lista, Delete (teclado) ou Triângulo/Y (controle), com confirmação.
- **Texturas de alta resolução (streaming):** o port passou a responder ao comando de
  estatísticas de textura da GPU (`GET_LOD_STATS`) que o jogo usa para decidir o que carregar.
  No nível 2 as texturas de 2048 chegavam a 512×512 e as cascatas ganhavam as cortinas de água.
- **Jogo fechando sozinho depois de carregar um save:** o streaming no nível 2 corrompia a
  memória do jogo (3 quedas em 4 testes perto da cascata). O padrão agora é o nível 3, que rodou
  4 de 4 testes sem queda; nele as texturas de 2048 ficam em 256×256, como antes do streaming
  (as cortinas das cascatas ainda não foram conferidas neste nível). `SOTC_LOD_LEVEL=2` volta ao
  nível mais nítido, com o risco de queda.
- **Sombras pretas corrigidas:** grama, pedras e paredes em sombra ficavam pretas. Três correções
  do recompilador de shaders trazidas do Pink-shadPS4 (constantes 1.0 do fetch shader que eram
  ignoradas, `V_ALIGNBIT`/`V_ALIGNBYTE` com deslocamento 0, `V_CVT_PK_U8_F32`).
- **Partículas da GPU:** barreiras de memória compartilhada em grupos menores que uma wave; um
  sistema de partículas que nunca aparecia voltou. O borrifo e a espuma das cascatas ainda não
  aparecem (em investigação).
- O lançador grava o registro do jogo em `user/last_run.log`, com o código de saída.
- Ferramentas de diagnóstico: F12 captura um quadro quando o jogo roda sob o RenderDoc.
- Ícone novo (rosto de um colosso).
- **Atenção:** se trocar de save na mesma sessão fechar o jogo, reinicie antes de carregar outro.
- Revisão completa do projeto em `docs/sotc/REVISAO-DO-PROJETO.md`.

## 0.0.9 — 2026-10-12
- **Configuração inicial automática:** na primeira vez o `SotC.exe` abre numa tela de boas-vindas
  que só pede a pasta do jogo (ou já oferece a que encontrou no PC). Depois ele configura sozinho
  as DLCs (dentro da pasta do jogo e ao lado dela), os saves (copia um save do shadPS4 se o port
  ainda não tiver) e o som, e mostra o resultado. **Configurar de novo** repete o processo.
- **Pasta principal como no shadPS4:** você aponta a pasta principal do jogo e ele acha as
  subpastas do jogo base (CUSA08809), da atualização e das DLCs.
- **Atualização:** a pasta `CUSA08809-UPDATE` ou `CUSA08809-patch` ao lado do jogo é usada como no
  shadPS4: os arquivos dela têm prioridade sobre os do jogo base (`SOTC_UPDATE_DIR`). Se ela tiver
  `eboot.bin`, é ele que roda.
- Tela principal mais limpa: o registro do jogo virou um link discreto; a verificação mostra as DLCs.
- **Pasta de DLC** no lançador (Jogo e saves), como no shadPS4. Também é encontrada na busca
  automática: a pasta `addcont` do shadPS4 ou uma pasta com uma subpasta por DLC.
- O runtime lista ao jogo as DLCs instaladas (`sceAppContentGetAddcontInfoList`), lendo o
  `CONTENT_ID` do `param.sfo` de cada uma (`SOTC_DLC_DIR`).
- **DLC em .pkg sem instalar:** os pacotes de DLC (tipo `0x1b`) são lidos direto do cabeçalho,
  sem extrair. Isso basta para as DLCs do jogo, que só desbloqueiam itens (arco antigo, capa,
  espada, cavalos). A busca olha na pasta escolhida, na pasta do jogo e nas pastas vizinhas.
- **Modo PS4 Pro:** o registro do jogo mostra se ele foi aberto como PS4 Pro
  (`sceKernelIsNeoMode -> 1`). A opção só libera o Modo Grafismo do PS4 Pro: é preciso escolher
  **Cinematográfico** ou **Desempenho** no jogo, em Opções > Apresentação.

## 0.0.8 — 2026-10-12

Primeira versão pública.

### Lançador (`SotC.exe`)
- **Busca automática** ao abrir: encontra nos discos do PC o jogo (CUSA08809), os módulos de som
  do usuário num shadPS4 instalado (copiados para a pasta de dados do port) e saves do shadPS4
  (pergunta antes de importar). **Procurar no PC** repete a busca.
- **Biblioteca de jogos** como a do shadPS4: pastas de jogos, lista com ícone, título, ID e versão.
- **Troca de teclas:** clique na ação e aperte a tecla nova (gravado como `key.<entrada>=` no
  `sotc.ini`).
- **Ícone novo** (arte original). A janela e o atalho na área de trabalho usam o `icon0.png` do
  seu dump.
- **Envio de trabalho à GPU:** Automático (NVIDIA), Ligado em qualquer placa (AMD/Intel,
  experimental) ou Desligado.

### Desempenho
Demo da tela de título, modo 60 FPS, i9-13900K + RTX 2060 SUPER: média de 37–45 → **~67 FPS**,
trecho mais pesado ~17 → **29–34 FPS**.
- Comandos Vulkan gravados e enviados numa thread própria (`cp_record_thread` do Pink-shadPS4).
- Envio periódico de trabalho à GPU por tempo (a cada 0,5 ms), em vez de por número de comandos.
- Draws indiretos com argumentos na CPU viram draws diretos.
- Caches: descrição de imagens por T#, buscas de imagens, informações de binário de shader e
  ponteiro de memória por thread.
- Verificação de sincronia de buffers sem trava quando nada mudou; `RangeSet::Add` não reinsere
  intervalos já marcados.
- Falhas de escrita: a página que falhou e as seguintes, sem dados da GPU, são liberadas juntas
  (~50 mil → ~4 mil falhas por segundo).
- Readbacks:
  - faixas lidas recentemente são baixadas juntas;
  - leituras "limpas" nas tabelas de recursos dos shaders, com o bitmap acompanhando a proteção
    real das páginas;
  - o preenchimento de tabelas de descritores (compute `0x50784801`) é feito na CPU;
  - leituras antecipadas.
- Do Pink-shadPS4: cvars `doIndexBufferCulling=0 doSunShadowIndexBufferCulling=0` e a proteção
  do heap BPE contra escritas da GPU.
- Opcionais, para teste: readback assíncrono (`SOTC_RB_ASYNC=1`) e marcador de espera
  (`SOTC_WAIT_MARKER=2`).

### Correções
- Travamento da fila da GPU com a gravação em thread própria: a apresentação espera o quadro
  chegar à fila.
- Morte silenciosa por proteção de página na pilha de uma fibra.
- Tela preta no boot; crash no modo PS4 Pro (`alt_tile` nos buffers de vídeo).

## Antes (desenvolvimento, 07–08/10/2026)
- Boot do jogo no carregador do bbport (imports por NID, fibras, SSE4a, imagem abaixo de 2 GiB).
- Núcleo de vídeo do fork shadPS4 SotC; som com os módulos do usuário.
- Modos 60 FPS e 16x AF como patches do carregador; lançador Tkinter congelado (`SotC.exe`).
- Teclado e controle juntos, câmera no mouse, saída 720p–4K, modo PS4 Pro, efeitos do motor.
