# Shadow of the Colossus — PC (port nativo / native port) · v1.0.0 · MMadmer fork

**[Português](#português)** · **[English](#english)** · [Releases](../../releases) · [Changelog](CHANGELOG.md)

> **A separate project / Отдельный проект.** This is [MMadmer](https://github.com/MMadmer)'s
> fork of [AjncSystem/ShadowOfTheColossus-PC](https://github.com/AjncSystem/ShadowOfTheColossus-PC),
> the original native port. Its focus is optimizing the port as a whole: less CPU and GPU work
> per frame, steadier frame times, less video memory, faster startup, and good play on modest
> hardware too (4-core CPUs, 2 GB cards); plus quality-of-life such as `.pkg` dump unpacking (see
> [CHANGELOG](CHANGELOG.md), 0.57-opt6 and later). It builds on upstream pull requests that are
> still open there: #32 (Gabriell-os), #46 (Lucas-blip-png), #81 (jownatan) and #83
> (Oussama268). Improvements are offered back to the original as pull requests. Report problems
> with this fork's builds here, not in the original repository.
>
> Это форк [MMadmer](https://github.com/MMadmer) оригинального нативного порта
> [AjncSystem/ShadowOfTheColossus-PC](https://github.com/AjncSystem/ShadowOfTheColossus-PC),
> посвящённый оптимизации порта в целом (меньше работы CPU и GPU на кадр, ровнее кадры, меньше
> видеопамяти, быстрее запуск) — в том числе чтобы игра хорошо шла и на слабом железе. Основан также на ещё не принятых
> там пул-реквестах #32, #46, #81 и #83 их авторов. Улучшения предлагаются в оригинал пул-реквестами;
> о проблемах сборок этого форка — сюда, а не в оригинальную репу.

> **Nenhum arquivo do jogo nem da Sony está incluído / No game or Sony files are included.**
> Você precisa do seu próprio dump do jogo e, para ter som, de dois módulos do seu próprio
> console. / You need your own game dump and, for sound, two modules from your own console.
> Este projeto não tem ligação com a Sony Interactive Entertainment nem com a Bluepoint Games. /
> This project is not affiliated with Sony Interactive Entertainment or Bluepoint Games.

---

## Português

Port nativo do **Shadow of the Colossus** de PS4 (Bluepoint, 2018; CUSA08809, versão EU 1.01)
para **Windows 10/11**. O código x86-64 do próprio jogo roda direto na CPU do PC e a GPU do PS4
é traduzida para Vulkan. Não é um emulador completo: é um carregador com as funções de sistema
de que o jogo precisa, no estilo do [bloodborne_pc](https://github.com/Supermedo/bloodborne_pc).

### Novidades

| Versão | Melhorias e correções |
|---|---|
| **1.0.0** | **Primeiro release do fork:** **controle Xbox completo** (ícones do Xbox, ou das teclas e do mouse, nítidos e desenhados por shader, também no esquema de Controles, troca na hora ao conectar ou usar outro dispositivo, vibração igual à do PS4), instalador e zip, compilação com um comando (`build.cmd`), som com a biblioteca própria do port, tudo da 0.57-opt6 e 0.57-opt7. |
| **0.57-opt7** | **Menos CPU por quadro para i5/GTX 1050:** o processador de comandos não gira mais esperando (−20% do tempo dele no santuário), o thread de desenho reaproveita os render targets e não repete push descriptors iguais, o mixer de som gasta menos no reverb; o launcher escolhe 810p numa GTX 1050 e 900p numa 1050 Ti. |
| **0.57-opt6** | **PCs de 4 núcleos e placas fracas:** os workers do jogo não tomam mais os núcleos do render (7 → 3,5 núcleos ocupados no santuário), barycentrics via geometry shader passthrough (GTX 10: −14% de GPU), resolução de render 900/810/720p, shaders compilados uma vez a partir de um pacote, o launcher desempacota dumps .pkg (jogo, atualização e todas as DLC). |
| **0.57-opt5** | **Sessões longas estáveis:** memória de buffers sem uso devolvida à GPU (30 minutos de demo: pico 3,4 GB estável, antes 4,3–4,4 GB e subindo). **Mundo aberto ~2% mais rápido** (envio antecipado). **Correção de queda rara** ao carregar o save ("write-only"). |
| **0.57-opt4** | **Mais FPS:** a thread de comandos gasta bem menos tempo em travas, proteções de página, cópias e alocações (santuário 29 → 35 FPS; demo 47 → 53,5 FPS, 1% low 29 → 38, quadros acima de 50 ms 129 → 26). |
| **0.57-opt3** | **Menos travadas na primeira visita a uma área:** pipelines gráficos montados em segundo plano; o objeto novo aparece um instante depois em vez de o jogo parar (cache vazio: 1% low 23,9 → 29,1 FPS, quadros acima de 50 ms 235 → 156). |
| **0.57-opt2** | **Muito menos engasgos (placas de 6 GB):** texturas guardam só os mips usados (memória de vídeo no demo ~6,1 → ~3,4 GB). **Leituras da GPU assíncronas** (santuário 29 → 31 FPS). **Memória de buffers devolvida** ao desmapear (sessões longas). Demo: 49,9 FPS, 47 quadros acima de 50 ms (antes 100–150). **Quedas deixam log e minidump** em `user/crashes/`. |
| **0.57** | **Placas AMD:** correção das cores verdes/rosas na névoa e nos personagens (as AMD passam a usar o caminho de 32 threads da NVIDIA; diagnóstico da issue #7, ainda não testado numa AMD). **Opção nova** no lançador para desligar o acesso direto à memória: ~10% mais FPS, água distante mais lisa. |
| **0.56** | **Água do mundo aberto com ondas:** lagos e rios abaixo das cascatas não ficam mais lisos como espelho (uma instrução de shader, `S_WQM_B64`, era ignorada). **Lago de cima ondula e reflete até longe** (acesso direto à memória ligado por padrão, como no Pink-shadPS4). Árvores distantes sem "lixo colorido". |
| **0.0.13** | **Sem quedas:** corrigida a queda na abertura/menu da 0.0.12 (o jogo pedia mais texturas do que cabe no pool). Testado ~40 min pelo mapa, com dois carregamentos de save. **Reflexos do lago** em quase toda a superfície, com ondulação (uma trava de laços dos shaders cortava os raios do reflexo). |
| **0.0.12** | **Texturas nítidas de volta, sem o jogo fechar:** a corrupção de memória que derrubava o jogo depois de carregar um save foi corrigida, e o streaming de texturas voltou (pedras e paredes bem mais nítidas). **Água:** respingos, anéis e espuma ao nadar; o lago reflete o céu e o brilho do sol; reflexos de ambiente com o *cubemap* certo de cada *probe* (correção do Pink-shadPS4). |
| **0.0.11** | Corrigida a grama com textura de "lixo colorido" da 0.0.10. |

Detalhes de cada versão no [CHANGELOG](CHANGELOG.md).

### Problemas conhecidos

- Placas **AMD**: a 0.57 traz a correção das cores verdes/rosas ([#7](../../issues/7)), ainda sem
  teste numa AMD. Se continuar, mande o seu `user/last_run.log` na issue.

### Download e uso

1. Na página [Releases](../../releases), baixe o instalador `SotC-PC-Setup-<versão>.exe`
   (instala para o seu usuário, sem administrador, com atalhos) ou o `SotC-PC-<versão>.zip`
   (descompacte numa pasta).
2. Abra o **`SotC.exe`**. Na primeira vez o lançador procura sozinho, nos discos do PC:
   - a pasta do jogo (o dump CUSA08809, com `eboot.bin`, `sce_module` e `sce_sys`), ou os
     pacotes `.pkg` do jogo, da atualização e das DLC, que ele desempacota;
   - um save do jogo no shadPS4, que ele oferece importar;
   - as DLCs do jogo, se houver: a pasta `addcont` do shadPS4, ou uma pasta com uma subpasta por DLC.
     Também dá para escolher em **Jogo e saves > Pasta de DLC**.
3. Confira a página **Jogo e saves**. A **biblioteca de jogos** funciona como a do shadPS4:
   adicione as pastas dos seus jogos e escolha o jogo na lista.
4. Ajuste a imagem em **Imagem e FPS** e clique em **JOGAR**. Na primeira vez os shaders são
   montados uma vez, numa janela de progresso.

Requisitos: Windows 10/11 64-bit, placa de vídeo com **Vulkan 1.3** (NVIDIA, AMD ou Intel
com driver recente). Não precisa instalar Python. Guia em russo: [docs/README_RU.md](docs/README_RU.md).

#### Os arquivos que você precisa ter

| O quê | De onde | Para quê |
|---|---|---|
| Dump do jogo CUSA08809 (EU 1.01 de preferência) | o seu PS4, pelo GoldHEN | o jogo |
| `libSceNgs2.sprx`, `libSceUlt.sprx` decriptados (opcional) | o seu PS4 (`/system/common/lib/`, FTP do GoldHEN), ou o `user/sys_modules` do shadPS4 | som do próprio console em vez da biblioteca do port |

Os modos de patch (60 FPS, filtro 16x) só valem para a versão EU 1.01; nas outras o jogo roda
normalmente, a 30 FPS.

### O lançador

- **Jogar:** confere a versão do jogo, o som, os saves e a placa de vídeo antes de abrir;
  opções rápidas de saída, upscaler, 60 FPS, filtro 16x e tela cheia.
- **Jogo e saves:** biblioteca de jogos, busca automática no PC, importação de saves do shadPS4.
- **Imagem e FPS:** resolução de render 720p a 1080p (automática em placas fracas), saída 720p a
  4K (FSR 1 + RCAS amplia),
  modo PS4 Pro (o jogo oferece Cinematográfico/Desempenho no próprio menu), efeitos (vinheta,
  névoa, SSAO) e opções de desempenho.
- **Controles:** controle (DualShock 4, DualSense, Xbox) automático, com os ícones dos botões do
  dispositivo em uso (também as teclas e o mouse) e vibração; câmera no mouse;
  **troca de teclas** clicando na ação e apertando a tecla nova.
- **Avançado:** cvars do motor Bluepoint, diagnóstico, atalho na área de trabalho (com o ícone
  do seu jogo). O registro de cada sessão fica em `user/last_run.log`.

### O que foi feito

| Área | Resultado |
|---|---|
| Boot | O jogo inicia com o carregador do bbport: imports por NID, fibras, patches de CPU (SSE4a), imagem abaixo de 2 GiB (o jogo guarda ponteiros em 31 bits). |
| Vídeo | Núcleo de vídeo do fork shadPS4 SotC dentro do port: logos, abertura, menus, campo aberto sem polígonos pretos, sombras com iluminação correta. |
| Texturas | Streaming de texturas (o jogo carrega os mips mais finos), sem corrupção de memória. |
| Água | Reflexos em espaço de tela, reflexos de ambiente por *probe*, respingos e anéis ao nadar. |
| Saves | Gravação, carregamento e exclusão pela lista de saves na tela. |
| Som | Áudio real com os módulos Ngs2/Ult do próprio usuário. |
| Controles | Teclado e controle juntos, câmera no mouse, troca de teclas no lançador. |
| Modos | 60 FPS (patch de illusion), filtro anisotrópico 16x, modo PS4 Pro, saída até 4K. |
| Desempenho | No demo da tela de título, em modo 60 FPS: média de **37–45 → ~67 FPS**, trecho mais pesado (ponte do cânion) **~17 → 29–34 FPS**. |

### Perguntas comuns

- **DirectX 11/12?** Não. O núcleo de vídeo traduz a GPU do PS4 para Vulkan (shaders em SPIR-V).
  Toda placa com DirectX 12 moderno também tem Vulkan 1.3: basta o driver atualizado.
- **AMD e Intel?** Quase todas as otimizações valem para qualquer placa. Duas só ligam
  sozinhas na NVIDIA: o envio periódico à GPU e as leituras antecipadas. Em **Imagem e FPS >
  Envio de trabalho à GPU**, escolha **Ligado** para usá-las numa AMD ou Intel. Se a tela ficar
  preta, volte para **Automático**.
- **O jogo fechou sozinho.** Abra uma *issue* com o arquivo `user/last_run.log`.

---

## English

A native Windows port of the PS4 **Shadow of the Colossus** (Bluepoint, 2018; CUSA08809, EU 1.01)
for **Windows 10/11**. The game's own x86-64 code runs directly on the PC's CPU and the PS4 GPU
is translated to Vulkan. It is not a full emulator: it is a loader with the system functions the
game needs, in the style of [bloodborne_pc](https://github.com/Supermedo/bloodborne_pc).

### What's new

| Version | Improvements and fixes |
|---|---|
| **1.0.0** | **First release of the fork:** **full Xbox controller support** (sharp shader-drawn Xbox, or key and mouse, button icons, the Controls diagram included, switching as soon as another device is connected or used, PS4-accurate rumble), installer and zip, one-command build (`build.cmd`), sound through the port's own library, everything from 0.57-opt6 and 0.57-opt7. |
| **0.57-opt7** | **Less CPU per frame for i5/GTX 1050:** the command processor no longer spins while it waits (−20% of its time at the shrine), the draw thread keeps its render targets and drops repeated push descriptors, the sound mixer spends less on reverb; the launcher picks 810p on a GTX 1050 and 900p on a 1050 Ti. |
| **0.57-opt6** | **4-core PCs and weak cards:** the game's workers no longer take the render cores (7 → 3.5 cores busy at the shrine), barycentrics through a passthrough geometry shader (GTX 10: −14% GPU), 900/810/720p render resolution, shaders built once from a pack, the launcher unpacks .pkg dumps (game, update and every DLC). |
| **0.57-opt5** | **Stable long sessions:** unused buffer memory is given back to the GPU (30-minute demo: peak 3.4 GB and flat, before 4.3–4.4 GB and rising). **Open world ~2% faster** (early submit). **Fix for a rare crash** while loading the save ("write-only"). |
| **0.57-opt4** | **More FPS:** the command thread spends far less time in locks, page protection, copies and allocations (shrine 29 → 35 FPS; demo 47 → 53.5 FPS, 1% low 29 → 38, frames over 50 ms 129 → 26). |
| **0.57-opt3** | **Less freezing on the first visit to an area:** graphics pipelines are built in the background; a new object appears a moment later instead of the game stopping (empty cache: 1% low 23.9 → 29.1 FPS, frames over 50 ms 235 → 156). |
| **0.57-opt2** | **Much less stutter (6 GB cards):** textures keep only the mips they use (video memory in the demo ~6.1 → ~3.4 GB). **Asynchronous GPU readbacks** (shrine 29 → 31 FPS). **Buffer memory given back** on unmap (long sessions). Demo: 49.9 FPS, 47 frames over 50 ms (before 100–150). **Crashes leave a log and a minidump** in `user/crashes/`. |
| **0.57** | **AMD cards:** fix for the green/pink fog and characters (AMD now uses the NVIDIA 32-lane path; diagnosis from issue #7, not yet tested on an AMD card). **New launcher option** to turn off direct memory access: ~10% more FPS, flatter distant water. |
| **0.56** | **Open-world water with waves:** lakes and rivers below the waterfalls are no longer flat as a mirror (a shader instruction, `S_WQM_B64`, was ignored). **The upper lake ripples and reflects far away** (direct memory access on by default, as in Pink-shadPS4). Distant trees without "colored garbage". |
| **0.0.13** | **No more crashes:** fixed the 0.0.12 crash in the intro/menu (the game asked for more textures than its pool holds). Tested ~40 min across the map with two save loads. **Lake reflections** over almost the whole surface, with ripples (a shader loop guard cut the reflection rays short). |
| **0.0.12** | **Sharp textures back, without crashes:** fixed the memory corruption that closed the game after loading a save, and texture streaming is back (much sharper rocks and walls). **Water:** splashes, rings and foam while swimming; the lake reflects the sky and the sun's glint; environment reflections use each probe's correct cubemap (fix from Pink-shadPS4). |
| **0.0.11** | Fixed the "colored garbage" grass texture of 0.0.10. |

Full details in the [CHANGELOG](CHANGELOG.md) (in Portuguese).

### Known issues

- **AMD** GPUs: 0.57 has the fix for the green/pink tint ([#7](../../issues/7)), not yet tested
  on an AMD card. If it persists, attach your `user/last_run.log` to the issue.

### Download and use

1. From [Releases](../../releases), get the installer `SotC-PC-Setup-<version>.exe` (installs
   for your user, no administrator rights, with shortcuts) or `SotC-PC-<version>.zip` (unzip it
   into a folder).
2. Run **`SotC.exe`**. On first start the launcher searches the PC's drives for:
   - the game folder (the CUSA08809 dump with `eboot.bin`, `sce_module` and `sce_sys`), or the
     `.pkg` files of the game, update and DLC, which it unpacks;
   - a shadPS4 save of the game, which it offers to import;
   - the game's DLCs, if any (shadPS4's `addcont` folder, or a folder with one subfolder per DLC).
3. Check the **Game and saves** page. The **game library** works like shadPS4's.
4. Set the picture up under **Display & FPS** and click **PLAY**. On first start the shaders are
   built once, behind a progress window.

Requirements: Windows 10/11 64-bit and a **Vulkan 1.3** GPU (NVIDIA, AMD or Intel with a recent
driver). No Python install needed. Russian guide: [docs/README_RU.md](docs/README_RU.md).

| What | Where from | What for |
|---|---|---|
| CUSA08809 game dump (EU 1.01 preferred) | your PS4, through GoldHEN | the game |
| decrypted `libSceNgs2.sprx`, `libSceUlt.sprx` (optional) | your PS4 (`/system/common/lib/`, GoldHEN FTP), or shadPS4's `user/sys_modules` | the console's own sound instead of the port's library |

The patch modes (60 FPS, 16x filtering) only apply to EU 1.01; other versions run at 30 FPS.

### Launcher features

Game library and automatic search, `.pkg` unpacking, shadPS4 save import, 720p–1080p render
resolution (automatic on weak cards), 720p–4K output (FSR 1 + RCAS), PS4 Pro mode, 60 FPS and 16x AF modes, controller support
(DualShock 4, DualSense, Xbox; the game shows the buttons of the device in use, keys and mouse included) with rumble and mouse camera, key remapping, Bluepoint engine cvars, desktop
shortcut and a session log in `user/last_run.log`.

In 60 FPS mode the title-screen attract demo averages ~67 FPS on an RTX 2060 SUPER, with
29–34 FPS in its heaviest section. Vulkan only (no DirectX). AMD and Intel can enable the
NVIDIA-only submission options under **Image and FPS > GPU work submission**.

**The game closed by itself?** Open an issue with `user/last_run.log`.

---

## Compilar / Building

Um comando, no Windows 10/11 / one command on Windows 10/11 (Git for Windows to clone):

```
git clone https://github.com/MMadmer/ShadowOfTheColossus-PC
cd ShadowOfTheColossus-PC
build.cmd
```

`build.cmd` baixa um MSYS2 próprio em `.deps` (nada é instalado no sistema; ~1,5 GB na primeira
vez), instala os pacotes, os submódulos e o PkgTool, compila `out/bb-probe.exe` e abre o lançador
direto do código / downloads a private MSYS2 into `.deps` (nothing is installed system-wide;
~1.5 GB the first time), the packages, submodules and PkgTool, builds `out/bb-probe.exe` and starts
the launcher from the source tree. Depois só recompila o que mudou / later runs only rebuild what
changed.

- `build.cmd -NoLaunch`: só compilar / build only.
- `build.cmd -Package`: também / also `dist/SotC-PC-<versão>.zip` e / and
  `dist/SotC-PC-Setup-<versão>.exe` (precisa de / needs Python for Windows for PyInstaller).
- `MSYS2_ROOT=<pasta>`: usar o seu MSYS2 / use your own MSYS2 (packages:
  [packaging/windows/README.md](packaging/windows/README.md)).

Por onde começar / where to start: [CLAUDE.md](CLAUDE.md) (arquitetura / architecture overview:
loader, runtime, GPU library), [CHANGELOG](CHANGELOG.md) (o que cada variável `SOTC_*` faz /
what each `SOTC_*` switch does), `tools/bench/` (benchmark com cenários / scenario benchmark:
shrine, colossus fight, attract demo; `tools/profile_tree.py` for call trees).

O empacotamento recusa qualquer `.sprx`/`.prx` / packaging refuses any `.sprx`/`.prx`: Sony
firmware never goes into the package. Ferramentas de teste / test tools: `tools/`. Variáveis de
diagnóstico / diagnostic variables: [docs/sotc/README-SotC.md](docs/sotc/README-SotC.md).

## Créditos / Credits

- [AjncSystem/ShadowOfTheColossus-PC](https://github.com/AjncSystem/ShadowOfTheColossus-PC)
  (AjncSystem): o port original / the original port this fork is based on.
- Pull requests do original incluídos / upstream pull requests included:
  [#32](https://github.com/AjncSystem/ShadowOfTheColossus-PC/pull/32) Gabriell-os (AMD),
  [#46](https://github.com/AjncSystem/ShadowOfTheColossus-PC/pull/46) Lucas-blip-png,
  [#81](https://github.com/AjncSystem/ShadowOfTheColossus-PC/pull/81) jownatan (Intel Arc),
  [#83](https://github.com/AjncSystem/ShadowOfTheColossus-PC/pull/83) Oussama268 (0.57-opt5).
- [bloodborne_pc](https://github.com/Supermedo/bloodborne_pc) (Supermedo, deadinside28 and
  forks): carregador, runtime nativo e lançador / loader, native runtime and launcher. README
  original: [docs/bbport](docs/bbport/README.md).
- [shadPS4](https://github.com/shadps4-emu/shadPS4) e o fork / and the shadPS4 SotC fork:
  núcleo de vídeo Vulkan / Vulkan video core.
- [Pink-shadPS4](https://github.com/luizgustavs/Pink-shadPS4) (luizgustavs): cvars de culling /
  culling cvars, BPE heap guard, envio periódico / periodic submission, gravador de comandos em
  thread própria / command recording thread, correções de shader / shader fixes (fetch-shader
  constants, `V_ALIGNBIT`, `V_CVT_PK_U8_F32`), tabelas de texturas indexadas / dynamic T# tables
  (`dynamic_tsharp_array_size`), a abordagem das estatísticas de LOD / the LOD statistics
  approach e o acesso direto à memória para o SotC / and direct memory access for SotC.
- Patch de 60 FPS / 60 FPS patch: illusion (GoldHEN cheats).

Licença / License: **GPL-2.0-or-later** ([LICENSE](LICENSE)), como o original / as the original.

- Modificações deste fork / this fork's modifications: © 2026 MMadmer, GPL-2.0-or-later. O que
  mudou e quando / what changed and when: [CHANGELOG](CHANGELOG.md) (0.57-opt6 and later) and the
  git history; changed code in the vendored shadPS4 is marked with `sotc:` comments.
- Os binários das Releases são compilados deste código / Release binaries are built from this
  source: the exact source of every release is its git tag in this repository.
- Componentes de terceiros / third-party components keep their own licenses: shadPS4
  (GPL-2.0-or-later, [gpu/shadps4/LICENSE](gpu/shadps4/LICENSE)), the submodules in
  `.gitmodules` (LibAtrac9, FSR-Vulkan, imgui, magic_enum, miniz, xbyak), the files under
  `gpu/third_party` with their license files, and PkgTool / LibOrbisPkg (LGPL-3.0, source:
  <https://github.com/maxton/LibOrbisPkg>), shipped unmodified in Release packages under
  `tools/PkgTool` together with its `LICENSE.txt`.
- Nenhum arquivo do jogo ou firmware da Sony é distribuído / no game files or Sony firmware are
  distributed.
