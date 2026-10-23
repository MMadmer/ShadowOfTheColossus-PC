# Shadow of the Colossus PC

[English](#english) · [Русский](#русский) · [Download / Скачать](https://github.com/MMadmer/ShadowOfTheColossus-PC/releases/latest)

## English

A native Windows port of **Shadow of the Colossus** for PlayStation 4 (Bluepoint, 2018). The
game's own code runs directly on your PC's processor and its graphics are translated to Vulkan.
It is not an emulator, so it needs far less power: a laptop with a GeForce GTX 1050 and a 4-core
i5 runs the game at a steady 30 FPS.

This is Madmer's fork of [AjncSystem/ShadowOfTheColossus-PC](https://github.com/AjncSystem/ShadowOfTheColossus-PC),
focused on performance, weak hardware and ease of use.

> **No game files are included.** You need your own copy of the game, dumped from your own
> PlayStation 4. This project is not affiliated with Sony Interactive Entertainment or Bluepoint
> Games.

### What you get

- **The whole game, start to finish**, including the ending, the credits and the DLC.
- **Smooth play on modest PCs**: the launcher picks a suitable render resolution for weak graphics
  cards, shaders are prepared once and never compiled during play.
- **NVIDIA DLSS and DLAA** on GeForce RTX cards and **frame generation** (AMD FSR 3.1 on any card,
  NVIDIA DLSS-G on RTX 40/50) with a sharp interface.
- **30 or 60 FPS**, resolutions from 720p to 4K, widescreen 16:10, 21:9 and 32:9, a wider field of
  view (up to 130%), PS4 Pro mode, high quality textures, 16x texture filtering, HDR
  (experimental).
- **Any controller or keyboard and mouse**: Xbox, DualShock 4, DualSense and others. The game shows
  the buttons of the device you use, Xbox and keyboard ones included. Keys and mouse buttons can
  be remapped, rumble strength adjusted.
- **A launcher that sets everything up**: it finds the game on your drives, unpacks `.pkg` dumps
  (game, update and DLC), imports shadPS4 saves and updates itself.
- **An in-game menu** (Insert, F1 or L3+R3) to change FPS, frame generation, DLSS, textures and
  volume while playing.
- Launcher and in-game menu in English, Russian, Spanish, Portuguese, Chinese, Japanese and Korean.

### Requirements

| | Minimum | Recommended |
|---|---|---|
| System | Windows 10 or 11, 64-bit | Windows 11 |
| Processor | 4 threads (e.g. Core i5-7300HQ) | 6 cores or more |
| Graphics | Vulkan 1.3, 2 GB (e.g. GeForce GTX 1050, Radeon RX 470) | 6 GB or more; GeForce RTX for DLSS |
| Memory | 8 GB RAM and a **16 GB page file** | 16 GB RAM |
| Disk | 15 GB free (30 GB to unpack `.pkg` files) | SSD |

Keep your graphics driver up to date. Processors without AVX2 (Sandy/Ivy Bridge, AMD FX) are
supported: a compatible build is selected automatically.

**What you need from your console:** the game **CUSA08809**, preferably the European version with
update **1.01** (60 FPS and some options need it). Either an unpacked folder (with `eboot.bin`
inside) or the `.pkg` files of the game, the update and the DLC.

### Installation

1. Download from the [latest release](https://github.com/MMadmer/ShadowOfTheColossus-PC/releases/latest):
   - **`SotC-PC-Setup-<version>.exe`**: the installer. No administrator rights needed; you choose
     the folder; it creates shortcuts and an uninstaller.
   - **`SotC-PC-<version>.zip`**: the same port without installation. Unpack it into any folder.
2. Run **`SotC.exe`**. On the first start the launcher looks for the game, its `.pkg` files, DLC
   and shadPS4 saves on your drives. If the game is packed, it shows how much space is needed
   and unpacks it next to the packages.
3. Check the **Game & saves** page. If something was not found, pick the folder yourself or
   press **Search this PC**.
4. Press **PLAY**. The first start prepares the shaders once (a few minutes, with a counter on
   the loading screen); later starts are fast.

**Updating:** the launcher tells you when a new version is out. Install it over the old one:
saves and settings are kept. Saves, the shader cache and logs live in the `user` folder of the
port.

### Controls

| | Controller | Keyboard and mouse |
|---|---|---|
| Camera | Right stick | Mouse (click the window to capture it, Esc releases it) |
| Pause menu | Start / Options | Enter |
| Skip a cutscene | Hold A / ✕ | Hold Space |
| In-game port menu | L3 + R3 | Insert or F1 |
| Frame generation on/off | | F8 |
| Fullscreen on/off | | F11 |

Every key and mouse button can be remapped on the **Controls** page of the launcher. Controllers
connect at any time, also while playing.

### Settings for weak PCs

- **Render resolution** is the main option for a weak graphics card: the 3D scene is drawn at
  810p, 900p or 720p and scaled to the screen; textures, fog and effects stay. The launcher picks
  it on the first start (GTX 1050: 810p, GTX 1050 Ti: 900p).
- **DLSS** (GeForce RTX) gives a sharper picture than a lower render resolution at the same speed.
- **Frame generation** shows 60 frames per second while the game runs at 30. It feels best when
  the game holds 30 FPS steadily.
- **60 FPS** needs a noticeably stronger PC; the original game runs at 30 FPS on PS4.
- With 2–4 GB of video memory, set **Video memory usage** to **Low** and, if needed, turn off
  **High quality textures**.
- On a 4-thread processor, close background programs: the game uses every core.

### Troubleshooting

- **"Virtual memory" warning in the launcher, or the game closes after a few minutes:** the page
  file is too small. Set it to 16 GB: *System → About → Advanced system settings → Performance →
  Settings → Advanced → Virtual memory*.
- **The first start takes long:** the shaders are being prepared, which happens once and again
  after a port or graphics driver update.
- **No sound effects or music on a weak PC:** update to the latest version; close background
  programs.
- **The game froze or closed:** open an [issue](https://github.com/MMadmer/ShadowOfTheColossus-PC/issues)
  and attach `user\last_run.log`. If the game froze, wait 20 seconds before closing it: the log
  then records what every thread was doing.

### Building from source

You need Windows 10/11 and [Git for Windows](https://git-scm.com/download/win). Fork the
repository on GitHub if you plan to change it, then:

```
git clone https://github.com/MMadmer/ShadowOfTheColossus-PC
cd ShadowOfTheColossus-PC
build.cmd
```

`build.cmd` downloads its own MSYS2 toolchain into `.deps` (about 4 GB the first time; nothing is
installed system-wide), builds the port and starts the launcher from the source tree. Later runs
only rebuild what changed.

- `build.cmd -NoLaunch`: build only.
- `build.cmd -Package`: also the zip and the installer in `dist\` (needs Python for Windows).
- `MSYS2_ROOT=<folder>`: use your own MSYS2 installation (packages are listed in
  [packaging/windows/README.md](packaging/windows/README.md)).

To run a build you also need the game, as above. An overview of the code (loader, runtime, the
Vulkan video core) is in [CLAUDE.md](CLAUDE.md); diagnostic switches are listed in
[docs/sotc/README-SotC.md](docs/sotc/README-SotC.md). The packaging refuses any Sony firmware
file, so no `.sprx`/`.prx` can end up in a package by mistake.

### Credits

- [AjncSystem/ShadowOfTheColossus-PC](https://github.com/AjncSystem/ShadowOfTheColossus-PC): the
  original port this fork is based on.
- [bloodborne_pc](https://github.com/Supermedo/bloodborne_pc) (Supermedo, deadinside28): the loader,
  native runtime and launcher the port grew from.
- [shadPS4](https://github.com/shadps4-emu/shadPS4) and
  [Pink-shadPS4](https://github.com/luizgustavs/Pink-shadPS4) (luizgustavs): the Vulkan video core
  and many game-specific fixes.
- Community contributors: Oussama268, Lucas-blip-png, Gabriell-os, jownatan, DUDUYG, xeodeo,
  nakaionyt / y7ngboy, tabnul, EdouardSence, Einar, KingJhoss, nosekefik, raulbezerro-dev and
  everyone who reports problems with logs. 60 FPS patch: illusion (GoldHEN cheats).

### License

**GPL-2.0-or-later** ([LICENSE](LICENSE)), as the original port. Changes of this fork © 2026
Madmer. Third-party components keep their own licenses: shadPS4 (GPL-2.0-or-later), the
submodules listed in `.gitmodules`, the files under `gpu/third_party`, NVIDIA Streamline, and
PkgTool / LibOrbisPkg (LGPL-3.0, [source](https://github.com/maxton/LibOrbisPkg)), shipped
unmodified in `tools/PkgTool`. Release packages are built from this source; each release's exact
source is its git tag.

---

## Русский

Нативный порт **Shadow of the Colossus** для PlayStation 4 (Bluepoint, 2018) под Windows. Код
самой игры выполняется непосредственно на процессоре компьютера, а графика преобразуется в Vulkan.
Это не эмулятор, поэтому мощности требуется значительно меньше: ноутбук с GeForce GTX 1050 и
четырёхъядерным i5 обеспечивает стабильные 30 FPS.

Проект является форком [AjncSystem/ShadowOfTheColossus-PC](https://github.com/AjncSystem/ShadowOfTheColossus-PC)
от Madmer и посвящён производительности, поддержке слабых компьютеров и удобству.

> **Игровые файлы не входят в состав проекта.** Требуется собственная копия игры, снятая с
> собственной PlayStation 4. Проект не связан с Sony Interactive Entertainment и Bluepoint Games.

### Возможности

- **Игра проходится полностью**, включая финал, титры и DLC.
- **Плавная игра на скромных компьютерах**: лаунчер подбирает подходящее разрешение рендера для
  слабых видеокарт, шейдеры подготавливаются один раз и не компилируются во время игры.
- **NVIDIA DLSS и DLAA** на видеокартах GeForce RTX и **генерация кадров** (AMD FSR 3.1 на любой
  видеокарте, NVIDIA DLSS-G на RTX 40/50) с чётким интерфейсом.
- **30 или 60 FPS**, разрешения от 720p до 4K, широкие экраны 16:10, 21:9 и 32:9, расширенное поле
  зрения (до 130%), режим PS4 Pro, текстуры высокого качества, анизотропная фильтрация 16x, HDR
  (экспериментально).
- **Любой геймпад или клавиатура и мышь**: Xbox, DualShock 4, DualSense и другие. Игра показывает
  кнопки используемого устройства, в том числе кнопки Xbox и клавиши клавиатуры. Клавиши и кнопки
  мыши переназначаются, сила вибрации настраивается.
- **Лаунчер, который всё настраивает сам**: находит игру на дисках, распаковывает дампы `.pkg`
  (игру, обновление и DLC), импортирует сохранения shadPS4 и обновляется самостоятельно.
- **Меню в игре** (Insert, F1 или L3+R3) для изменения FPS, генерации кадров, DLSS, текстур и
  громкости во время игры.
- Лаунчер и меню в игре на русском, английском, испанском, португальском, китайском, японском и
  корейском языках.

### Системные требования

| | Минимальные | Рекомендуемые |
|---|---|---|
| Система | Windows 10 или 11, 64-разрядная | Windows 11 |
| Процессор | 4 потока (например, Core i5-7300HQ) | 6 ядер и более |
| Видеокарта | Vulkan 1.3, 2 ГБ (например, GeForce GTX 1050, Radeon RX 470) | 6 ГБ и более; GeForce RTX для DLSS |
| Память | 8 ГБ ОЗУ и **файл подкачки 16 ГБ** | 16 ГБ ОЗУ |
| Диск | 15 ГБ (30 ГБ для распаковки `.pkg`) | SSD |

Рекомендуется использовать актуальный драйвер видеокарты. Процессоры без AVX2 (Sandy/Ivy Bridge,
AMD FX) поддерживаются: совместимая сборка выбирается автоматически.

**Что требуется с консоли:** игра **CUSA08809**, предпочтительно европейская версия с обновлением
**1.01** (оно необходимо для 60 FPS и некоторых настроек). Подойдёт распакованная папка (с файлом
`eboot.bin` внутри) или файлы `.pkg` игры, обновления и DLC.

### Установка

1. Скачайте из [последнего релиза](https://github.com/MMadmer/ShadowOfTheColossus-PC/releases/latest):
   - **`SotC-PC-Setup-<версия>.exe`** — установщик. Права администратора не требуются; папка
     установки выбирается при установке; создаются ярлыки и программа удаления.
   - **`SotC-PC-<версия>.zip`** — тот же порт без установки. Распакуйте архив в любую папку.
2. Запустите **`SotC.exe`**. При первом запуске лаунчер ищет на дисках игру, её файлы `.pkg`, DLC и
   сохранения shadPS4. Если игра упакована, лаунчер сообщит, сколько места требуется, и распакует
   её рядом с пакетами.
3. Проверьте вкладку **«Игра и сохранения»**. Если что-либо не найдено, укажите папку вручную или
   нажмите **«Искать на этом ПК»**.
4. Нажмите **«ИГРАТЬ»**. При первом запуске один раз подготавливаются шейдеры (несколько минут,
   счётчик отображается на экране загрузки); последующие запуски проходят быстро.

**Обновление:** лаунчер сообщает о выходе новой версии. Установите её поверх старой: сохранения и
настройки сохраняются. Сохранения, кэш шейдеров и журналы хранятся в папке `user` порта.

### Управление

| | Геймпад | Клавиатура и мышь |
|---|---|---|
| Камера | Правый стик | Мышь (щелчок по окну захватывает курсор, Esc освобождает его) |
| Меню паузы | Start / Options | Enter |
| Пропуск ролика | Удерживать A / ✕ | Удерживать пробел |
| Меню порта в игре | L3 + R3 | Insert или F1 |
| Генерация кадров вкл./выкл. | | F8 |
| Полноэкранный режим вкл./выкл. | | F11 |

Все клавиши и кнопки мыши переназначаются на вкладке **«Управление»** лаунчера. Геймпады
подключаются в любой момент, в том числе во время игры.

### Настройки для слабых компьютеров

- **Разрешение рендера** — основной параметр для слабой видеокарты: трёхмерная сцена отрисовывается
  в 810p, 900p или 720p и масштабируется до разрешения экрана; текстуры, туман и эффекты не
  меняются. Лаунчер выбирает его при первом запуске (GTX 1050 — 810p, GTX 1050 Ti — 900p).
- **DLSS** (GeForce RTX) даёт более чёткое изображение, чем пониженное разрешение рендера, при той
  же скорости.
- **Генерация кадров** выводит 60 кадров в секунду, когда игра работает с частотой 30. Лучше всего
  она работает, когда игра стабильно держит 30 FPS.
- **60 FPS** требуют заметно более мощного компьютера; оригинальная игра на PS4 работает с частотой
  30 FPS.
- При 2–4 ГБ видеопамяти установите **«Использование видеопамяти»** в значение **«Низкий»** и при
  необходимости отключите **«Текстуры высокого качества»**.
- На процессоре с 4 потоками закройте фоновые программы: игра использует все ядра.

### Устранение неполадок

- **Предупреждение «Виртуальная память» в лаунчере или вылет игры через несколько минут:** файл
  подкачки слишком мал. Установите его размер 16 ГБ: *Система → О системе → Дополнительные
  параметры системы → Быстродействие → Параметры → Дополнительно → Виртуальная память*.
- **Первый запуск идёт долго:** подготавливаются шейдеры. Это происходит один раз, а также после
  обновления порта или драйвера видеокарты.
- **На слабом компьютере пропадают звуковые эффекты или музыка:** обновитесь до последней версии и
  закройте фоновые программы.
- **Игра зависла или закрылась:** создайте [обращение](https://github.com/MMadmer/ShadowOfTheColossus-PC/issues)
  и приложите файл `user\last_run.log`. Если игра зависла, подождите 20 секунд перед закрытием:
  тогда в журнал записывается состояние всех потоков.

### Сборка из исходного кода

Требуются Windows 10/11 и [Git for Windows](https://git-scm.com/download/win). Если вы планируете
вносить изменения, сначала создайте форк репозитория на GitHub, затем выполните:

```
git clone https://github.com/MMadmer/ShadowOfTheColossus-PC
cd ShadowOfTheColossus-PC
build.cmd
```

`build.cmd` загружает собственный набор инструментов MSYS2 в папку `.deps` (около 4 ГБ при первом
запуске; в систему ничего не устанавливается), собирает порт и запускает лаунчер из исходного
кода. Последующие запуски пересобирают только изменённое.

- `build.cmd -NoLaunch` — только сборка.
- `build.cmd -Package` — дополнительно архив и установщик в папке `dist\` (требуется Python для
  Windows).
- `MSYS2_ROOT=<папка>` — использование собственной установки MSYS2 (список пакетов приведён в
  [packaging/windows/README.md](packaging/windows/README.md)).

Для запуска сборки также требуется игра, как описано выше. Обзор кода (загрузчик, среда выполнения,
видеоядро Vulkan) приведён в [CLAUDE.md](CLAUDE.md); диагностические переключатели перечислены в
[docs/sotc/README-SotC.md](docs/sotc/README-SotC.md). Упаковка отклоняет любые файлы прошивки Sony,
поэтому файлы `.sprx`/`.prx` не могут случайно попасть в пакет.

### Благодарности

- [AjncSystem/ShadowOfTheColossus-PC](https://github.com/AjncSystem/ShadowOfTheColossus-PC) —
  оригинальный порт, на котором основан этот форк.
- [bloodborne_pc](https://github.com/Supermedo/bloodborne_pc) (Supermedo, deadinside28) —
  загрузчик, нативная среда выполнения и лаунчер, из которых вырос порт.
- [shadPS4](https://github.com/shadps4-emu/shadPS4) и
  [Pink-shadPS4](https://github.com/luizgustavs/Pink-shadPS4) (luizgustavs) — видеоядро Vulkan и
  многие исправления для игры.
- Участники сообщества: Oussama268, Lucas-blip-png, Gabriell-os, jownatan, DUDUYG, xeodeo,
  nakaionyt / y7ngboy, tabnul, EdouardSence, Einar, KingJhoss, nosekefik, raulbezerro-dev и все,
  кто сообщает о проблемах с приложенными журналами. Патч 60 FPS — illusion (GoldHEN cheats).

### Лицензия

**GPL-2.0-or-later** ([LICENSE](LICENSE)), как и у оригинального порта. Изменения этого форка
© 2026 Madmer. Сторонние компоненты распространяются под собственными лицензиями: shadPS4
(GPL-2.0-or-later), подмодули из `.gitmodules`, файлы в `gpu/third_party`, NVIDIA Streamline, а
также PkgTool / LibOrbisPkg (LGPL-3.0, [исходный код](https://github.com/maxton/LibOrbisPkg)),
поставляемый без изменений в `tools/PkgTool`. Пакеты релизов собираются из этого исходного кода;
точный исходный код каждого релиза соответствует его git-тегу.
