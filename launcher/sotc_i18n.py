"""Launcher texts in Russian, Chinese (Simplified) and Japanese, by their English text
(sotc_launcher_win.py keeps Portuguese and English next to the code). A text missing here
shows in English."""

LANGUAGES = ('ru', 'zh', 'ja')

TEXTS = {
    'Move forward': (
        'Вперёд',
        '向前移动',
        '前進',
    ),
    'Move back': (
        'Назад',
        '向后移动',
        '後退',
    ),
    'Move left': (
        'Влево',
        '向左移动',
        '左へ移動',
    ),
    'Move right': (
        'Вправо',
        '向右移动',
        '右へ移動',
    ),
    'Camera up': (
        'Камера вверх',
        '镜头向上',
        'カメラ上',
    ),
    'Camera down': (
        'Камера вниз',
        '镜头向下',
        'カメラ下',
    ),
    'Camera left': (
        'Камера влево',
        '镜头向左',
        'カメラ左',
    ),
    'Camera right': (
        'Камера вправо',
        '镜头向右',
        'カメラ右',
    ),
    'Touchpad (left)': (
        'Тачпад (левая часть)',
        '触摸板（左）',
        'タッチパッド（左）',
    ),
    'Touchpad (right)': (
        'Тачпад (правая часть)',
        '触摸板（右）',
        'タッチパッド（右）',
    ),
    'D-pad ↑': (
        'Крестовина ↑',
        '方向键 ↑',
        '方向キー ↑',
    ),
    'D-pad ↓': (
        'Крестовина ↓',
        '方向键 ↓',
        '方向キー ↓',
    ),
    'D-pad ←': (
        'Крестовина ←',
        '方向键 ←',
        '方向キー ←',
    ),
    'D-pad →': (
        'Крестовина →',
        '方向键 →',
        '方向キー →',
    ),
    'Automatic (on with NVIDIA)': (
        'Автоматически (вкл. на NVIDIA)',
        '自动（NVIDIA 上开启）',
        '自動（NVIDIA ではオン）',
    ),
    'On with any card (AMD/Intel: experimental)': (
        'Вкл. на любой карте (AMD/Intel: экспериментально)',
        '所有显卡均开启（AMD/Intel：实验性）',
        'すべてのGPUでオン（AMD/Intel：実験的）',
    ),
    'Off': (
        'Выкл.',
        '关闭',
        'オフ',
    ),
    'Vignette (darkened corners)': (
        'Виньетка (затемнённые углы)',
        '暗角（画面四角变暗）',
        'ビネット（四隅を暗く）',
    ),
    'Atmospheric fog': (
        'Атмосферный туман',
        '大气雾',
        '大気の霧',
    ),
    'Ambient occlusion (SSAO)': (
        'Затенение окружения (SSAO)',
        '环境光遮蔽（SSAO）',
        'アンビエントオクルージョン（SSAO）',
    ),
    '1080p (native, as on PS4)': (
        '1080p (родное, как на PS4)',
        '1080p（原生，与 PS4 相同）',
        '1080p（ネイティブ、PS4と同じ）',
    ),
    '900p (weak GPUs)': (
        '900p (слабые видеокарты)',
        '900p（低端显卡）',
        '900p（非力なGPU）',
    ),
    '720p (very weak GPU)': (
        '720p (очень слабая видеокарта)',
        '720p（极低端显卡）',
        '720p（とても非力なGPU）',
    ),
    'Automatic (the device in use)': (
        'Автоматически (по используемому устройству)',
        '自动（按当前使用的设备）',
        '自動（使用中のデバイス）',
    ),
    'Always Xbox': (
        'Всегда Xbox',
        '始终 Xbox',
        '常に Xbox',
    ),
    'Always PlayStation': (
        'Всегда PlayStation',
        '始终 PlayStation',
        '常に PlayStation',
    ),
    'Always keyboard and mouse': (
        'Всегда клавиатура и мышь',
        '始终键盘和鼠标',
        '常にキーボードとマウス',
    ),
    'FSR 1 (AMD, any GPU)': (
        'FSR 1 (AMD, любой GPU)',
        'FSR 1（AMD，任意显卡）',
        'FSR 1（AMD、すべてのGPU）',
    ),
    'Bilinear (plain)': (
        'Билинейный (простой)',
        '双线性（简单）',
        'バイリニア（シンプル）',
    ),
    'High': (
        'Высокая',
        '高',
        '高',
    ),
    'Medium (default)': (
        'Средняя (по умолчанию)',
        '中（默认）',
        '中（デフォルト）',
    ),
    'Low': (
        'Низкая',
        '低',
        '低',
    ),
    '0.5 (slow)': (
        '0,5 (медленно)',
        '0.5（慢）',
        '0.5（遅い）',
    ),
    '1.0 (default)': (
        '1,0 (по умолчанию)',
        '1.0（默认）',
        '1.0（デフォルト）',
    ),
    '1.5': (
        '1,5',
        '1.5',
        '1.5',
    ),
    '2.0 (fast)': (
        '2,0 (быстро)',
        '2.0（快）',
        '2.0（速い）',
    ),
    'Windowed': (
        'Оконный',
        '窗口',
        'ウィンドウ',
    ),
    'Borderless fullscreen': (
        'Полноэкранный в окне',
        '无边框全屏',
        'ボーダーレス全画面',
    ),
    'Fullscreen': (
        'Полноэкранный',
        '全屏',
        '全画面',
    ),
    'Mailbox (low latency, no tearing)': (
        'Mailbox (малая задержка, без разрывов)',
        'Mailbox（低延迟，无撕裂）',
        'Mailbox（低遅延、ティアリングなし）',
    ),
    'Immediate (tearing)': (
        'Immediate (с разрывами кадра)',
        'Immediate（会撕裂）',
        'Immediate（ティアリングあり）',
    ),
    "Automatic (Windows' language)": (
        'Автоматически (язык Windows)',
        '自动（Windows 语言）',
        '自動（Windows の言語）',
    ),
    'Portuguese (Portugal)': (
        'Португальский (Португалия)',
        '葡萄牙语（葡萄牙）',
        'ポルトガル語（ポルトガル）',
    ),
    'Portuguese (Brazil)': (
        'Португальский (Бразилия)',
        '葡萄牙语（巴西）',
        'ポルトガル語（ブラジル）',
    ),
    'English': (
        'Английский',
        '英语',
        '英語',
    ),
    'Spanish': (
        'Испанский',
        '西班牙语',
        'スペイン語',
    ),
    'French': (
        'Французский',
        '法语',
        'フランス語',
    ),
    'German': (
        'Немецкий',
        '德语',
        'ドイツ語',
    ),
    'Italian': (
        'Итальянский',
        '意大利语',
        'イタリア語',
    ),
    'Russian': (
        'Русский',
        '俄语',
        'ロシア語',
    ),
    'Japanese': (
        'Японский',
        '日语',
        '日本語',
    ),
    'Precise (default; the game needs them)': (
        'Точные (по умолчанию; игре они нужны)',
        '精确（默认；游戏需要）',
        '正確（デフォルト、ゲームに必要）',
    ),
    'Relaxed (may hang)': (
        'Упрощённые (может зависнуть)',
        '宽松（可能卡死）',
        '簡略（フリーズする場合あり）',
    ),
    'Automatic': (
        'Автоматически',
        '自动',
        '自動',
    ),
    'left stick': (
        'левый стик',
        '左摇杆',
        '左スティック',
    ),
    'arrows': (
        'стрелки',
        '方向键',
        '矢印キー',
    ),
    'right stick (camera)': (
        'правый стик (камера)',
        '右摇杆（镜头）',
        '右スティック（カメラ）',
    ),
    'd-pad': (
        'крестовина',
        '十字键',
        '方向キー',
    ),
    'touchpad (left / right)': (
        'тачпад (лево / право)',
        '触摸板（左 / 右）',
        'タッチパッド（左 / 右）',
    ),
    '• Checking the graphics card…': (
        '• Проверка видеокарты…',
        '• 正在检测显卡…',
        '• グラフィックカードを確認中…',
    ),
    'Browse…': (
        'Обзор…',
        '浏览…',
        '参照…',
    ),
    'Open': (
        'Открыть',
        '打开',
        '開く',
    ),
    'native port · Windows': (
        'нативный порт · Windows',
        '原生移植 · Windows',
        'ネイティブ移植 · Windows',
    ),
    'Play': (
        'Играть',
        '开始',
        'プレイ',
    ),
    'Game log': (
        'Журнал игры',
        '游戏日志',
        'ゲームログ',
    ),
    'Set up again': (
        'Настроить заново',
        '重新设置',
        '再設定',
    ),
    'PLAY': (
        'ИГРАТЬ',
        '开始游戏',
        'プレイ',
    ),
    'Stop': (
        'Остановить',
        '停止',
        '停止',
    ),
    'Ready check': (
        'Проверка готовности',
        '就绪检查',
        '準備チェック',
    ),
    'Quick settings': (
        'Быстрые настройки',
        '快速设置',
        'クイック設定',
    ),
    'Render': (
        'Рендер',
        '渲染',
        'レンダー',
    ),
    'Output': (
        'Вывод',
        '输出',
        '出力',
    ),
    'Upscaler': (
        'Апскейлер',
        '放大算法',
        'アップスケーラー',
    ),
    '60 FPS (experimental)': (
        '60 FPS (экспериментально)',
        '60 FPS（实验性）',
        '60 FPS（実験的）',
    ),
    'Sharp textures (16x filtering)': (
        'Чёткие текстуры (фильтрация 16x)',
        '清晰纹理（16x 过滤）',
        'シャープなテクスチャ（16x フィルタリング）',
    ),
    'Mode': (
        'Режим',
        '模式',
        'モード',
    ),
    'Choose your game folder (Game & saves)': (
        'Укажите папку игры (Игра и сохранения)',
        '请选择游戏文件夹（游戏与存档）',
        'ゲームフォルダーを選択してください（ゲームとセーブ）',
    ),
    '{} · game version {}': (
        '{} · версия игры {}',
        '{} · 游戏版本 {}',
        '{} · ゲームバージョン {}',
    ),
    'Game folder not set': (
        'Папка игры не указана',
        '未设置游戏文件夹',
        'ゲームフォルダー未設定',
    ),
    'Game & saves': (
        'Игра и сохранения',
        '游戏与存档',
        'ゲームとセーブ',
    ),
    "Your game dump, the saves folder and, if you like, your console's sound modules.": (
        'Дамп игры, папка сохранений и, по желанию, звуковые модули вашей консоли.',
        '您的游戏转储、存档文件夹，以及（可选）您主机的音频模块。',
        'ゲームのダンプ、セーブフォルダー、必要ならコンソールのサウンドモジュール。',
    ),
    'Game library': (
        'Библиотека игр',
        '游戏库',
        'ゲームライブラリ',
    ),
    'As in shadPS4: add the folders that hold your games and pick Shadow of the Colossus in the list (double click).': (
        'Как в shadPS4: добавьте папки с играми и выберите Shadow of the Colossus в списке (двойной щелчок).',
        '与 shadPS4 相同：添加存放游戏的文件夹，然后在列表中选择 Shadow of the Colossus（双击）。',
        'shadPS4 と同じく、ゲームのあるフォルダーを追加し、一覧で Shadow of the Colossus を選びます（ダブルクリック）。',
    ),
    'Version': (
        'Версия',
        '版本',
        'バージョン',
    ),
    'Folder': (
        'Папка',
        '文件夹',
        'フォルダー',
    ),
    'Use this game': (
        'Использовать эту игру',
        '使用此游戏',
        'このゲームを使用',
    ),
    'Add a games folder…': (
        'Добавить папку с играми…',
        '添加游戏文件夹…',
        'ゲームフォルダーを追加…',
    ),
    'Remove folder': (
        'Убрать папку',
        '移除文件夹',
        'フォルダーを削除',
    ),
    'Search this PC': (
        'Искать на этом ПК',
        '搜索此电脑',
        'このPCを検索',
    ),
    'Game': (
        'Игра',
        '游戏',
        'ゲーム',
    ),
    'Game folder': (
        'Папка игры',
        '游戏文件夹',
        'ゲームフォルダー',
    ),
    'Folder with eboot.bin': (
        'Папка с eboot.bin',
        '包含 eboot.bin 的文件夹',
        'eboot.bin のあるフォルダー',
    ),
    'The CUSA08809 dump (EU version 1.01) with eboot.bin, sce_module and sce_sys.': (
        'Дамп CUSA08809 (версия EU 1.01) с eboot.bin, sce_module и sce_sys.',
        'CUSA08809 转储（欧版 1.01），包含 eboot.bin、sce_module 和 sce_sys。',
        'eboot.bin、sce_module、sce_sys を含む CUSA08809 のダンプ（EU版 1.01）。',
    ),
    'Sound': (
        'Звук',
        '声音',
        'サウンド',
    ),
    'Sound modules': (
        'Звуковые модули',
        '音频模块',
        'サウンドモジュール',
    ),
    'Folder with libSceNgs2.sprx and libSceUlt.sprx': (
        'Папка с libSceNgs2.sprx и libSceUlt.sprx',
        '包含 libSceNgs2.sprx 和 libSceUlt.sprx 的文件夹',
        'libSceNgs2.sprx と libSceUlt.sprx のあるフォルダー',
    ),
    'DLC folder': (
        'Папка DLC',
        'DLC 文件夹',
        'DLC フォルダー',
    ),
    "Folder with the game's DLC": (
        'Папка с DLC игры',
        '游戏 DLC 所在文件夹',
        'ゲームの DLC のあるフォルダー',
    ),
    "As in shadPS4: a folder with one subfolder per DLC (each with sce_sys/param.sfo), or shadPS4's addcont folder (which holds the CUSA08809 subfolder). Empty = no DLC.": (
        'Как в shadPS4: папка с подпапкой на каждое DLC (в каждой sce_sys/param.sfo) или папка addcont из shadPS4 (в ней подпапка CUSA08809). Пусто = без DLC.',
        '与 shadPS4 相同：每个 DLC 一个子文件夹（各含 sce_sys/param.sfo）的文件夹，或 shadPS4 的 addcont 文件夹（其中有 CUSA08809 子文件夹）。留空 = 无 DLC。',
        'shadPS4 と同じく、DLC ごとにサブフォルダー（それぞれ sce_sys/param.sfo を含む）があるフォルダー、または shadPS4 の addcont フォルダー（CUSA08809 サブフォルダーを含む）。空欄 = DLC なし。',
    ),
    'Saves': (
        'Сохранения',
        '存档',
        'セーブ',
    ),
    'Data folder': (
        'Папка данных',
        '数据文件夹',
        'データフォルダー',
    ),
    'Saves folder': (
        'Папка сохранений',
        '存档文件夹',
        'セーブフォルダー',
    ),
    'Saves, shader cache and screenshots. Empty = the user folder next to the port.': (
        'Сохранения, кэш шейдеров и скриншоты. Пусто = папка user рядом с портом.',
        '存档、着色器缓存和截图。留空 = 移植旁的 user 文件夹。',
        'セーブ、シェーダーキャッシュ、スクリーンショット。空欄 = 移植の隣の user フォルダー。',
    ),
    'Import a shadPS4 save…': (
        'Импортировать сохранение shadPS4…',
        '导入 shadPS4 存档…',
        'shadPS4 のセーブをインポート…',
    ),
    "Copies shadPS4's savedata/CUSA08809 folder here.": (
        'Копирует сюда папку savedata/CUSA08809 из shadPS4.',
        '将 shadPS4 的 savedata/CUSA08809 文件夹复制到这里。',
        'shadPS4 の savedata/CUSA08809 フォルダーをここにコピーします。',
    ),
    'System': (
        'Система',
        '系统',
        'システム',
    ),
    'System language': (
        'Язык системы',
        '系统语言',
        'システム言語',
    ),
    'The language the game sees on the "console".': (
        'Язык, который игра видит на «консоли».',
        '游戏在“主机”上看到的语言。',
        'ゲームが「コンソール」で認識する言語。',
    ),
    'Player name': (
        'Имя игрока',
        '玩家名称',
        'プレイヤー名',
    ),
    'Display & FPS': (
        'Изображение и FPS',
        '画面与 FPS',
        '画面と FPS',
    ),
    'The modes come from the shadPS4 SotC fork and apply to EU version 1.01.': (
        'Режимы взяты из форка shadPS4 для SotC и работают с версией EU 1.01.',
        '这些模式来自 shadPS4 的 SotC 分支，适用于欧版 1.01。',
        'モードは shadPS4 の SotC フォーク由来で、EU版 1.01 に適用されます。',
    ),
    'Modes': (
        'Режимы',
        '模式',
        'モード',
    ),
    'Sharp textures (16x anisotropic filtering)': (
        'Чёткие текстуры (16x анизотропная фильтрация)',
        '清晰纹理（16x 各向异性过滤）',
        'シャープなテクスチャ（16x 異方性フィルタリング）',
    ),
    'Ground, grass and rocks stay sharp at a distance, at almost no cost.': (
        'Земля, трава и скалы остаются чёткими вдали почти без потерь в скорости.',
        '地面、草地和岩石在远处依然清晰，几乎没有性能开销。',
        '地面・草・岩が遠くでもシャープなまま。負荷はほぼありません。',
    ),
    "Removes the game's 30 FPS cap; vblank at 120 Hz. Measured 38 FPS in the open world, ~25 at the temple.": (
        'Снимает ограничение игры в 30 FPS; vblank 120 Гц. Замерено: 38 FPS в открытом мире, ~25 у храма.',
        '解除游戏的 30 FPS 上限；垂直同步 120 Hz。实测开放世界 38 FPS，神殿约 25 FPS。',
        'ゲームの 30 FPS 上限を解除。vblank は 120 Hz。計測：オープンワールドで 38 FPS、神殿で約 25 FPS。',
    ),
    'Resolution': (
        'Разрешение',
        '分辨率',
        '解像度',
    ),
    'Render resolution': (
        'Разрешение рендера',
        '渲染分辨率',
        'レンダー解像度',
    ),
    'Below 1080p the game itself scales the frame up to 1080p (as the PS4 Pro does from 1440p to 4K): less work for weak cards (GTX 1050: 810p, 1050 Ti: 900p), same textures and fog, a slightly softer image. EU 1.01 only.': (
        'Ниже 1080p игра сама растягивает кадр до 1080p (как PS4 Pro из 1440p в 4K): меньше нагрузки на слабые карты (GTX 1050: 810p, 1050 Ti: 900p), те же текстуры и туман, картинка чуть мягче. Только EU 1.01.',
        '低于 1080p 时，游戏会自行将画面放大到 1080p（就像 PS4 Pro 从 1440p 放大到 4K）：降低低端显卡负担（GTX 1050：810p，1050 Ti：900p），纹理和雾不变，画面稍软。仅限欧版 1.01。',
        '1080p 未満ではゲーム自身がフレームを 1080p に拡大します（PS4 Pro が 1440p から 4K にするのと同様）。非力なGPUの負荷が減り（GTX 1050：810p、1050 Ti：900p）、テクスチャと霧はそのまま、画像はやや柔らかくなります。EU版 1.01 のみ。',
    ),
    'The game renders at 1080p, as on PS4; the image is scaled to the output. On a monitor smaller than the output the window fits the screen and fullscreen uses the whole monitor.': (
        'Игра рендерит в 1080p, как на PS4; картинка масштабируется под вывод. Если монитор меньше вывода, окно вписывается в экран, а полноэкранный режим занимает весь монитор.',
        '游戏以 1080p 渲染（与 PS4 相同），画面缩放到输出分辨率。若显示器小于输出分辨率，窗口会适应屏幕，全屏则占满整个显示器。',
        'ゲームは PS4 と同じく 1080p でレンダリングし、画像は出力解像度に拡大縮小されます。モニターが出力より小さい場合、ウィンドウは画面に収まり、全画面はモニター全体を使います。',
    ),
    'FSR 1 upscales with sharp edges (above 1080p).': (
        'FSR 1 увеличивает с чёткими краями (выше 1080p).',
        'FSR 1 放大时边缘清晰（高于 1080p 时）。',
        'FSR 1 はエッジをシャープに保って拡大します（1080p 超）。',
    ),
    'Sharpening (RCAS)': (
        'Резкость (RCAS)',
        '锐化（RCAS）',
        'シャープネス（RCAS）',
    ),
    'PS4 Pro mode': (
        'Режим PS4 Pro',
        'PS4 Pro 模式',
        'PS4 Pro モード',
    ),
    'Present as a PS4 Pro': (
        'Представляться как PS4 Pro',
        '模拟为 PS4 Pro',
        'PS4 Pro として動作',
    ),
    'The game then offers, in Options > Presentation, the PS4 Pro graphics mode: Cinematic (1440p, more detail) or Performance. Heavier: ~25 FPS in the open world against ~38 normally.': (
        'Тогда игра предлагает в Options > Presentation графический режим PS4 Pro: Cinematic (1440p, больше деталей) или Performance. Тяжелее: ~25 FPS в открытом мире против ~38 обычно.',
        '之后游戏会在“选项 > 画面”中提供 PS4 Pro 图形模式：电影（1440p，更多细节）或性能。负载更高：开放世界约 25 FPS，平时约 38。',
        'するとゲームの「オプション > 表示」に PS4 Pro のグラフィックモード（シネマティック：1440p・高精細、またはパフォーマンス）が現れます。重くなります：オープンワールドで約 25 FPS（通常は約 38）。',
    ),
    'Performance': (
        'Производительность',
        '性能',
        'パフォーマンス',
    ),
    'Culling without the GPU (recommended)': (
        'Отсечение без GPU (рекомендуется)',
        '不使用 GPU 剔除（推荐）',
        'GPU を使わないカリング（推奨）',
    ),
    'Turns off the GPU index-buffer culling (Pink-shadPS4): same image, far fewer GPU waits. Sanctuary: ~22 → ~28 FPS.': (
        'Отключает GPU-отсечение по индексному буферу (Pink-shadPS4): та же картинка, гораздо меньше ожиданий GPU. Святилище: ~22 → ~28 FPS.',
        '关闭 GPU 索引缓冲剔除（Pink-shadPS4）：画面相同，GPU 等待大幅减少。神殿：约 22 → 约 28 FPS。',
        'GPU のインデックスバッファカリングをオフにします（Pink-shadPS4）。画像は同じで、GPU の待ちが大幅に減ります。神殿：約 22 → 約 28 FPS。',
    ),
    'Vulkan commands on their own thread (recommended)': (
        'Команды Vulkan в отдельном потоке (рекомендуется)',
        'Vulkan 命令使用独立线程（推荐）',
        'Vulkan コマンドを専用スレッドで（推奨）',
    ),
    'Records and submits the GPU commands on another thread (cp_record_thread of Pink-shadPS4). Canyon bridge in the demo: ~24 → ~28 FPS. Turn it off if the game hangs.': (
        'Записывает и отправляет команды GPU в другом потоке (cp_record_thread из Pink-shadPS4). Мост в каньоне в демо: ~24 → ~28 FPS. Выключите, если игра зависает.',
        '在另一个线程中录制并提交 GPU 命令（Pink-shadPS4 的 cp_record_thread）。演示中的峡谷桥：约 24 → 约 28 FPS。若游戏卡死请关闭。',
        'GPU コマンドを別スレッドで記録・送信します（Pink-shadPS4 の cp_record_thread）。デモの峡谷の橋：約 24 → 約 28 FPS。ゲームが固まる場合はオフに。',
    ),
    'Full water reflections (direct memory access)': (
        'Полные отражения воды (прямой доступ к памяти)',
        '完整水面反射（直接内存访问）',
        '完全な水面反射（ダイレクトメモリアクセス）',
    ),
    'Lets shaders read tables straight from game memory (as Pink-shadPS4 does): the lake ripples and reflects far away. Off gains ~10% FPS, but distant water is flatter. After a change the shaders are compiled again the first time.': (
        'Шейдеры читают таблицы прямо из памяти игры (как в Pink-shadPS4): озеро рябит и отражает вдаль. Без этого +~10% FPS, но дальняя вода площе. После смены шейдеры при первом запуске компилируются заново.',
        '让着色器直接从游戏内存读取表（与 Pink-shadPS4 相同）：湖面有波纹并能反射远处。关闭可提升约 10% FPS，但远处水面更平。更改后首次运行会重新编译着色器。',
        'シェーダーがゲームのメモリから直接テーブルを読みます（Pink-shadPS4 と同様）。湖が波立ち遠くまで映り込みます。オフにすると FPS が約 10% 上がりますが、遠くの水面は平坦になります。変更後の初回はシェーダーを再コンパイルします。',
    ),
    'GPU work submission': (
        'Отправка работы на GPU',
        'GPU 任务提交',
        'GPU への作業送信',
    ),
    'Sends work to the GPU every 0.5 ms and reads results through a shortcut (Pink-shadPS4 techniques). Measured on NVIDIA (+10–15%); on AMD/Intel pick "On" to try it and go back to "Automatic" if the screen turns black.': (
        'Отправляет работу на GPU каждые 0,5 мс и читает результаты коротким путём (приёмы Pink-shadPS4). Замерено на NVIDIA (+10–15%); на AMD/Intel выберите «Вкл. на любой карте», чтобы попробовать, и верните «Автоматически», если экран станет чёрным.',
        '每 0.5 毫秒向 GPU 提交一次任务，并通过捷径读取结果（Pink-shadPS4 的技术）。在 NVIDIA 上实测（+10–15%）；AMD/Intel 可选择“所有显卡均开启”尝试，若黑屏请改回“自动”。',
        '0.5 ms ごとに GPU へ作業を送り、結果を近道で読み取ります（Pink-shadPS4 の手法）。NVIDIA で計測（+10〜15%）。AMD/Intel では「すべてのGPUでオン」で試し、画面が黒くなったら「自動」に戻してください。',
    ),
    'Effects': (
        'Эффекты',
        '效果',
        'エフェクト',
    ),
    "Motion blur, brightness and filters are in the game's own menu (Options > Presentation).": (
        'Размытие в движении, яркость и фильтры — в меню самой игры (Options > Presentation).',
        '动态模糊、亮度和滤镜位于游戏自身的菜单中（选项 > 画面）。',
        'モーションブラー、明るさ、フィルターはゲーム内メニューにあります（オプション > 表示）。',
    ),
    'Display': (
        'Экран',
        '显示',
        'ディスプレイ',
    ),
    'Window mode': (
        'Режим окна',
        '窗口模式',
        'ウィンドウモード',
    ),
    'Borderless fullscreen covers the monitor without changing its resolution (quick Alt+Tab); fullscreen switches the monitor to the output resolution.': (
        'Полноэкранный в окне закрывает монитор, не меняя его разрешение (быстрый Alt+Tab); полноэкранный переключает монитор на разрешение вывода.',
        '无边框全屏会覆盖显示器且不改变其分辨率（Alt+Tab 切换快）；全屏会将显示器切换到输出分辨率。',
        'ボーダーレス全画面はモニターの解像度を変えずに画面を覆います（Alt+Tab が速い）。全画面はモニターを出力解像度に切り替えます。',
    ),
    'Presentation': (
        'Вывод кадров',
        '呈现方式',
        'プレゼンテーション',
    ),
    'Controls': (
        'Управление',
        '操作',
        '操作',
    ),
    'A gamepad (DualShock 4, DualSense, Xbox…) is used automatically.': (
        'Геймпад (DualShock 4, DualSense, Xbox…) подхватывается автоматически.',
        '自动使用手柄（DualShock 4、DualSense、Xbox…）。',
        'ゲームパッド（DualShock 4、DualSense、Xbox…）は自動で使われます。',
    ),
    'Gamepad': (
        'Геймпад',
        '手柄',
        'ゲームパッド',
    ),
    'Preferred gamepad': (
        'Предпочтительный геймпад',
        '首选手柄',
        '優先するゲームパッド',
    ),
    'Name (or part of it) of the gamepad to use when several are connected; empty = the first.': (
        'Имя (или его часть) геймпада, если подключено несколько; пусто = первый.',
        '连接多个手柄时要使用的手柄名称（或部分名称）；留空 = 第一个。',
        '複数接続時に使うゲームパッドの名前（または一部）。空欄 = 最初のもの。',
    ),
    'Button icons': (
        'Иконки кнопок',
        '按键图标',
        'ボタンアイコン',
    ),
    'The game shows the buttons of what is in use: Xbox with an Xbox (or generic) gamepad, the PlayStation ones with a DualShock or DualSense, the bound keys and mouse buttons on the keyboard, in the Options > Controls diagram too. It switches as soon as another device is connected or used.': (
        'Игра показывает кнопки используемого устройства: Xbox с геймпадом Xbox (или другим), PlayStation с DualShock или DualSense, назначенные клавиши и кнопки мыши на клавиатуре — и на схеме в Options > Controls тоже. Переключение происходит сразу при подключении или использовании другого устройства.',
        '游戏会显示当前所用设备的按键：Xbox（或通用）手柄显示 Xbox 按键，DualShock 或 DualSense 显示 PlayStation 按键，键盘则显示绑定的按键和鼠标按钮，“选项 > 操作”中的示意图也一样。连接或使用其他设备时立即切换。',
        'ゲームは使用中のデバイスのボタンを表示します：Xbox（または汎用）ゲームパッドなら Xbox、DualShock や DualSense なら PlayStation、キーボードなら割り当てたキーとマウスボタン。「オプション > 操作」の図も同様です。別のデバイスを接続・使用するとすぐに切り替わります。',
    ),
    'Mouse': (
        'Мышь',
        '鼠标',
        'マウス',
    ),
    'Mouse camera': (
        'Камера мышью',
        '鼠标控制镜头',
        'マウスでカメラ',
    ),
    'Click the game window to capture the mouse; Esc releases it. Left button = R1 (grip), right = □ (attack), middle = L1. Remap with mouse.<input>= in sotc.ini.': (
        'Щёлкните по окну игры, чтобы захватить мышь; Esc отпускает её. Левая кнопка = R1 (хват), правая = □ (атака), средняя = L1. Переназначение: mouse.<input>= в sotc.ini.',
        '点击游戏窗口以捕获鼠标；按 Esc 释放。左键 = R1（抓握），右键 = □（攻击），中键 = L1。可在 sotc.ini 中用 mouse.<input>= 重新映射。',
        'ゲームウィンドウをクリックするとマウスを捕捉し、Esc で解放します。左ボタン = R1（つかむ）、右 = □（攻撃）、中央 = L1。sotc.ini の mouse.<input>= で割り当てを変更できます。',
    ),
    'Sensitivity': (
        'Чувствительность',
        '灵敏度',
        '感度',
    ),
    'Invert the vertical axis': (
        'Инвертировать вертикальную ось',
        '反转垂直轴',
        '縦軸を反転',
    ),
    'Keyboard (works next to a gamepad)': (
        'Клавиатура (работает вместе с геймпадом)',
        '键盘（可与手柄同时使用）',
        'キーボード（ゲームパッドと併用可）',
    ),
    'Click the key of an action and press the new key (Esc cancels). Applies the next time the game starts.': (
        'Щёлкните по клавише действия и нажмите новую (Esc — отмена). Применится при следующем запуске игры.',
        '点击某个动作的按键，然后按下新按键（Esc 取消）。下次启动游戏时生效。',
        'アクションのキーをクリックし、新しいキーを押します（Esc でキャンセル）。次回ゲーム起動時に反映されます。',
    ),
    'Restore the default keys': (
        'Вернуть клавиши по умолчанию',
        '恢复默认按键',
        'デフォルトのキーに戻す',
    ),
    'The gamepad can be remapped in sotc.ini too, e.g. pad.circle=b (SDL button names).': (
        'Геймпад тоже переназначается в sotc.ini, например pad.circle=b (имена кнопок SDL).',
        '手柄也可在 sotc.ini 中重新映射，例如 pad.circle=b（SDL 按键名称）。',
        'ゲームパッドも sotc.ini で変更できます（例：pad.circle=b、SDL のボタン名）。',
    ),
    'New key': (
        'Новая клавиша',
        '新按键',
        '新しいキー',
    ),
    'Press the key for: {}': (
        'Нажмите клавишу для: {}',
        '请按下用于以下动作的按键：{}',
        '次の操作のキーを押してください：{}',
    ),
    'Esc cancels': (
        'Esc — отмена',
        'Esc 取消',
        'Esc でキャンセル',
    ),
    'Unsupported key: {}': (
        'Неподдерживаемая клавиша: {}',
        '不支持的按键：{}',
        '非対応のキー：{}',
    ),
    'Advanced': (
        'Дополнительно',
        '高级',
        '詳細設定',
    ),
    'Launcher options and diagnostics.': (
        'Настройки лаунчера и диагностика.',
        '启动器选项与诊断。',
        'ランチャーのオプションと診断。',
    ),
    'Launcher': (
        'Лаунчер',
        '启动器',
        'ランチャー',
    ),
    'Launcher language': (
        'Язык лаунчера',
        '启动器语言',
        'ランチャーの言語',
    ),
    'Close the launcher when the game starts': (
        'Закрывать лаунчер при запуске игры',
        '游戏启动时关闭启动器',
        'ゲーム起動時にランチャーを閉じる',
    ),
    'Desktop shortcut': (
        'Ярлык на рабочем столе',
        '桌面快捷方式',
        'デスクトップのショートカット',
    ),
    'Port folder': (
        'Папка порта',
        '移植文件夹',
        '移植フォルダー',
    ),
    'Clear shader cache': (
        'Очистить кэш шейдеров',
        '清除着色器缓存',
        'シェーダーキャッシュを消去',
    ),
    'Diagnostics': (
        'Диагностика',
        '诊断',
        '診断',
    ),
    'FPS in the log (every 5 s)': (
        'FPS в журнале (каждые 5 с)',
        '在日志中记录 FPS（每 5 秒）',
        'ログに FPS（5 秒ごと）',
    ),
    'GPU readbacks': (
        'Чтение из GPU (readbacks)',
        'GPU 回读',
        'GPU リードバック',
    ),
    'Engine cvars': (
        'Переменные движка (cvars)',
        '引擎 cvar',
        'エンジンの cvar',
    ),
    "Advanced: NAME=value separated by spaces, added to the game's command line.": (
        'Для опытных: NAME=value через пробел, добавляются в командную строку игры.',
        '高级：以空格分隔的 NAME=value，添加到游戏的命令行。',
        '上級者向け：スペース区切りの NAME=value をゲームのコマンドラインに追加します。',
    ),
    'Extra variables': (
        'Дополнительные переменные',
        '额外变量',
        '追加の変数',
    ),
    'NAME=value pairs separated by spaces (see the README).': (
        'Пары NAME=value через пробел (см. README).',
        '以空格分隔的 NAME=value 对（见 README）。',
        'スペース区切りの NAME=value（README 参照）。',
    ),
    'Log': (
        'Журнал',
        '日志',
        'ログ',
    ),
    'Copy': (
        'Копировать',
        '复制',
        'コピー',
    ),
    'Clear': (
        'Очистить',
        '清除',
        'クリア',
    ),
    '✗ No eboot.bin in the game folder (Game & saves)': (
        '✗ В папке игры нет eboot.bin (Игра и сохранения)',
        '✗ 游戏文件夹中没有 eboot.bin（游戏与存档）',
        '✗ ゲームフォルダーに eboot.bin がありません（ゲームとセーブ）',
    ),
    '✓ {} version {}: every mode available': (
        '✓ {} версии {}: доступны все режимы',
        '✓ {} 版本 {}：所有模式可用',
        '✓ {} バージョン {}：すべてのモードが使用可能',
    ),
    '⚠ {} version {}: the port targets {} {}; the modes do not apply': (
        '⚠ {} версии {}: порт рассчитан на {} {}; режимы не применяются',
        '⚠ {} 版本 {}：本移植针对 {} {}；这些模式不适用',
        '⚠ {} バージョン {}：移植の対象は {} {} です。モードは適用されません',
    ),
    "✓ Sound: your console's modules": (
        '✓ Звук: модули вашей консоли',
        '✓ 声音：您主机的模块',
        '✓ サウンド：あなたのコンソールのモジュール',
    ),
    "✓ Sound: the port's own library": (
        '✓ Звук: собственная библиотека порта',
        '✓ 声音：移植自带的库',
        '✓ サウンド：移植独自のライブラリ',
    ),
    '✓ Saves in {}': (
        '✓ Сохранения в {}',
        '✓ 存档位于 {}',
        '✓ セーブの場所：{}',
    ),
    '• No saves yet: the game creates them in {}': (
        '• Сохранений пока нет: игра создаст их в {}',
        '• 尚无存档：游戏将在 {} 中创建',
        '• セーブはまだありません：ゲームが {} に作成します',
    ),
    '✓ DLC: {} installed': (
        '✓ DLC: установлено {}',
        '✓ DLC：已安装 {}',
        '✓ DLC：{} 件インストール済み',
    ),
    '• No DLC': (
        '• Нет DLC',
        '• 无 DLC',
        '• DLC なし',
    ),
    'Choose the game folder first.': (
        'Сначала укажите папку игры.',
        '请先选择游戏文件夹。',
        '先にゲームフォルダーを選択してください。',
    ),
    '30 FPS (as on PS4)': (
        '30 FPS (как на PS4)',
        '30 FPS（与 PS4 相同）',
        '30 FPS（PS4 と同じ）',
    ),
    '• Graphics card: not checked': (
        '• Видеокарта: не проверена',
        '• 显卡：未检测',
        '• グラフィックカード：未確認',
    ),
    '✓ Graphics card: {}': (
        '✓ Видеокарта: {}',
        '✓ 显卡：{}',
        '✓ グラフィックカード：{}',
    ),
    '✗ No Vulkan 1.3 graphics card found (update the driver)': (
        '✗ Не найдена видеокарта с Vulkan 1.3 (обновите драйвер)',
        '✗ 未找到支持 Vulkan 1.3 的显卡（请更新驱动）',
        '✗ Vulkan 1.3 対応のグラフィックカードが見つかりません（ドライバーを更新してください）',
    ),
    ' — {} render chosen for this card (Display & FPS)': (
        ' — для этой карты выбран рендер {} (Изображение и FPS)',
        ' — 已为此显卡选择 {} 渲染（画面与 FPS）',
        ' — このカード向けに {} レンダーを選択（画面と FPS）',
    ),
    "shadPS4's savedata/CUSA08809 folder": (
        'Папка savedata/CUSA08809 из shadPS4',
        'shadPS4 的 savedata/CUSA08809 文件夹',
        'shadPS4 の savedata/CUSA08809 フォルダー',
    ),
    'There are saves in {} already. Replace them? (a copy stays in {}.bak)': (
        'В {} уже есть сохранения. Заменить их? (копия останется в {}.bak)',
        '{} 中已有存档。要替换吗？（副本保留在 {}.bak）',
        '{} には既にセーブがあります。置き換えますか？（コピーは {}.bak に残ります）',
    ),
    'Could not import the save: {}': (
        'Не удалось импортировать сохранение: {}',
        '无法导入存档：{}',
        'セーブをインポートできませんでした：{}',
    ),
    'Save imported to {}.': (
        'Сохранение импортировано в {}.',
        '存档已导入到 {}。',
        'セーブを {} にインポートしました。',
    ),
    'Welcome. Just tell us where the game is.': (
        'Добро пожаловать. Укажите, где находится игра.',
        '欢迎使用。请指定游戏所在位置。',
        'ようこそ。ゲームの場所を指定してください。',
    ),
    'Choose the game folder…': (
        'Выбрать папку игры…',
        '选择游戏文件夹…',
        'ゲームフォルダーを選択…',
    ),
    "The game's main folder, as in shadPS4: the game (CUSA08809), update (-UPDATE/-patch) and DLC subfolders are found on their own.": (
        'Главная папка игры, как в shadPS4: подпапки игры (CUSA08809), обновления (-UPDATE/-patch) и DLC находятся сами.',
        '游戏的主文件夹（与 shadPS4 相同）：游戏（CUSA08809）、更新（-UPDATE/-patch）和 DLC 子文件夹会自动找到。',
        'shadPS4 と同じくゲームのメインフォルダー：ゲーム（CUSA08809）、アップデート（-UPDATE/-patch）、DLC のサブフォルダーは自動で見つかります。',
    ),
    'Looking for the game on this PC…': (
        'Поиск игры на этом ПК…',
        '正在此电脑上查找游戏…',
        'このPCでゲームを探しています…',
    ),
    'Found: {}   —   click here to use it': (
        'Найдено: {}   —   щёлкните здесь, чтобы использовать',
        '已找到：{}   —   点击此处使用',
        '見つかりました：{}   —   ここをクリックして使用',
    ),
    'Packed dump (.pkg) in {}   —   click here to unpack it': (
        'Упакованный дамп (.pkg) в {}   —   щёлкните здесь, чтобы распаковать',
        '{} 中有打包的转储（.pkg）   —   点击此处解包',
        '{} にパックされたダンプ（.pkg）   —   ここをクリックして展開',
    ),
    'tools\\PkgTool\\PkgTool.exe is missing from the port folder.': (
        'В папке порта нет tools\\PkgTool\\PkgTool.exe.',
        '移植文件夹中缺少 tools\\PkgTool\\PkgTool.exe。',
        '移植フォルダーに tools\\PkgTool\\PkgTool.exe がありません。',
    ),
    'Found the packed game (.pkg) in\n{}\n(game: {}, updates: {}, DLC: {}).\n\nUnpack it into {}? It needs {:.1f} GB free and takes a few minutes.': (
        'Найдена упакованная игра (.pkg) в\n{}\n(игра: {}, обновления: {}, DLC: {}).\n\nРаспаковать в {}? Нужно {:.1f} ГБ свободного места, займёт несколько минут.',
        '在以下位置找到打包的游戏（.pkg）：\n{}\n（游戏：{}，更新：{}，DLC：{}）。\n\n要解包到 {} 吗？需要 {:.1f} GB 可用空间，耗时几分钟。',
        'パックされたゲーム（.pkg）が見つかりました：\n{}\n（ゲーム：{}、アップデート：{}、DLC：{}）。\n\n{} に展開しますか？ {:.1f} GB の空き容量が必要で、数分かかります。',
    ),
    'Unpacking…': (
        'Распаковка…',
        '正在解包…',
        '展開中…',
    ),
    'Unpacking the game: {:.0f}% ({:.1f} of {:.1f} GB)': (
        'Распаковка игры: {:.0f}% ({:.1f} из {:.1f} ГБ)',
        '正在解包游戏：{:.0f}%（{:.1f} / {:.1f} GB）',
        'ゲームを展開中：{:.0f}%（{:.1f} / {:.1f} GB）',
    ),
    'Could not unpack:\n{}': (
        'Не удалось распаковать:\n{}',
        '无法解包：\n{}',
        '展開できませんでした：\n{}',
    ),
    'Shadow of the Colossus game folder': (
        'Папка игры Shadow of the Colossus',
        'Shadow of the Colossus 游戏文件夹',
        'Shadow of the Colossus のゲームフォルダー',
    ),
    'The game (eboot.bin of CUSA08809) is not in\n{}\n\nChoose the folder of your dump.': (
        'Игры (eboot.bin из CUSA08809) нет в\n{}\n\nУкажите папку вашего дампа.',
        '以下位置没有游戏（CUSA08809 的 eboot.bin）：\n{}\n\n请选择您的转储文件夹。',
        '次の場所にゲーム（CUSA08809 の eboot.bin）がありません：\n{}\n\nダンプのフォルダーを選択してください。',
    ),
    'Setting up…': (
        'Настройка…',
        '正在设置…',
        '設定中…',
    ),
    'Game: {} (version {})': (
        'Игра: {} (версия {})',
        '游戏：{}（版本 {}）',
        'ゲーム：{}（バージョン {}）',
    ),
    'Update: {} (next to the game)': (
        'Обновление: {} (рядом с игрой)',
        '更新：{}（在游戏旁）',
        'アップデート：{}（ゲームの隣）',
    ),
    'Update: no -UPDATE or -patch folder': (
        'Обновление: нет папки -UPDATE или -patch',
        '更新：没有 -UPDATE 或 -patch 文件夹',
        'アップデート：-UPDATE または -patch フォルダーなし',
    ),
    'DLC: {} found': (
        'DLC: найдено {}',
        'DLC：找到 {}',
        'DLC：{} 件見つかりました',
    ),
    'DLC: none found in the game folder': (
        'DLC: в папке игры не найдено',
        'DLC：游戏文件夹中未找到',
        'DLC：ゲームフォルダーに見つかりません',
    ),
    'Saves: {}': (
        'Сохранения: {}',
        '存档：{}',
        'セーブ：{}',
    ),
    '  (copied from shadPS4)': (
        '  (скопированы из shadPS4)',
        '  （从 shadPS4 复制）',
        '  （shadPS4 からコピー）',
    ),
    "Sound: your console's modules": (
        'Звук: модули вашей консоли',
        '声音：您主机的模块',
        'サウンド：あなたのコンソールのモジュール',
    ),
    "Sound: the port's own library": (
        'Звук: собственная библиотека порта',
        '声音：移植自带的库',
        'サウンド：移植独自のライブラリ',
    ),
    'Start': (
        'Начать',
        '开始',
        '開始',
    ),
    'Change the game folder…': (
        'Сменить папку игры…',
        '更改游戏文件夹…',
        'ゲームフォルダーを変更…',
    ),
    'Sound ready.': (
        'Звук готов.',
        '声音已就绪。',
        'サウンドの準備ができました。',
    ),
    'That folder does not have libSceNgs2.sprx and libSceUlt.sprx.': (
        'В этой папке нет libSceNgs2.sprx и libSceUlt.sprx.',
        '该文件夹中没有 libSceNgs2.sprx 和 libSceUlt.sprx。',
        'そのフォルダーには libSceNgs2.sprx と libSceUlt.sprx がありません。',
    ),
    'Library folders: {}': (
        'Папки библиотеки: {}',
        '游戏库文件夹：{}',
        'ライブラリのフォルダー：{}',
    ),
    'none yet': (
        'пока нет',
        '暂无',
        'まだありません',
    ),
    'Folder with your PS4 games': (
        'Папка с вашими играми PS4',
        '存放 PS4 游戏的文件夹',
        'PS4 ゲームのあるフォルダー',
    ),
    'Remove {} from the library? (nothing is deleted from the disk)': (
        'Убрать {} из библиотеки? (с диска ничего не удаляется)',
        '要从游戏库中移除 {} 吗？（不会从磁盘删除任何文件）',
        '{} をライブラリから削除しますか？（ディスクからは何も削除されません）',
    ),
    'Looking for the game, the sound modules and saves on this PC…': (
        'Поиск игры, звуковых модулей и сохранений на этом ПК…',
        '正在此电脑上查找游戏、音频模块和存档…',
        'このPCでゲーム、サウンドモジュール、セーブを探しています…',
    ),
    'game in {}': (
        'игра в {}',
        '游戏位于 {}',
        'ゲーム：{}',
    ),
    'DLC in {}': (
        'DLC в {}',
        'DLC 位于 {}',
        'DLC：{}',
    ),
    'Found a save of the game in\n{}\nImport it into the port?': (
        'Найдено сохранение игры в\n{}\nИмпортировать его в порт?',
        '在以下位置找到游戏存档：\n{}\n要导入到移植中吗？',
        '次の場所にゲームのセーブが見つかりました：\n{}\n移植にインポートしますか？',
    ),
    'save imported': (
        'сохранение импортировано',
        '存档已导入',
        'セーブをインポート済み',
    ),
    'Set up automatically: {}': (
        'Настроено автоматически: {}',
        '已自动设置：{}',
        '自動設定：{}',
    ),
    'Nothing new found.': (
        'Ничего нового не найдено.',
        '未发现新内容。',
        '新しいものは見つかりませんでした。',
    ),
    'Choose the game folder with eboot.bin (CUSA08809).': (
        'Укажите папку игры с eboot.bin (CUSA08809).',
        '请选择包含 eboot.bin 的游戏文件夹（CUSA08809）。',
        'eboot.bin のあるゲームフォルダー（CUSA08809）を選択してください。',
    ),
    'Could not write the keys to sotc.ini: {}': (
        'Не удалось записать клавиши в sotc.ini: {}',
        '无法将按键写入 sotc.ini：{}',
        'キーを sotc.ini に書き込めませんでした：{}',
    ),
    'Could not start: {}': (
        'Не удалось запустить: {}',
        '无法启动：{}',
        '起動できませんでした：{}',
    ),
    'Preparing the game; it opens in its own window…': (
        'Подготовка игры; она откроется в своём окне…',
        '正在准备游戏，它将在自己的窗口中打开…',
        'ゲームを準備中です。専用のウィンドウで開きます…',
    ),
    '\n— the game exited (code {}) —\n': (
        '\n— игра завершилась (код {}) —\n',
        '\n— 游戏已退出（代码 {}）—\n',
        '\n— ゲームが終了しました（コード {}）—\n',
    ),
    'The game is running.': (
        'Игра запущена.',
        '游戏正在运行。',
        'ゲームを実行中です。',
    ),
    'Shader cache cleared. The next start stutters for a few minutes while it is rebuilt.': (
        'Кэш шейдеров очищен. Следующий запуск несколько минут будет подтормаживать, пока кэш собирается заново.',
        '着色器缓存已清除。下次启动时重建缓存，会卡顿几分钟。',
        'シェーダーキャッシュを消去しました。次回起動時は再構築のため数分間カクつきます。',
    ),
    'Shortcut created on the desktop.': (
        'Ярлык создан на рабочем столе.',
        '已在桌面创建快捷方式。',
        'デスクトップにショートカットを作成しました。',
    ),
    'Shadow of the Colossus — native port': (
        'Shadow of the Colossus — нативный порт',
        'Shadow of the Colossus — 原生移植',
        'Shadow of the Colossus — ネイティブ移植',
    ),
    'Switches at once (while the game runs: the next time the launcher opens).': (
        'Переключается сразу (если игра запущена — при следующем открытии лаунчера).',
        '立即切换（游戏运行时：下次打开启动器时生效）。',
        'すぐに切り替わります（ゲーム実行中は次回ランチャー起動時）。',
    ),
    '{} render': (
        'рендер {}',
        '渲染 {}',
        'レンダー {}',
    ),
    "Optional: the port has its own sound library, which plays every sound. With libSceNgs2.sprx and libSceUlt.sprx from YOUR console (decrypted; shadPS4 keeps them in user/sys_modules) the game uses the console's instead. They are Sony firmware and never come with the port.": (
        'Необязательно: у порта есть собственная звуковая библиотека, которая воспроизводит все звуки. При наличии libSceNgs2.sprx и libSceUlt.sprx с ВАШЕЙ консоли (расшифрованных; shadPS4 хранит их в user/sys_modules) игра использует консольную библиотеку. Это прошивка Sony, и в состав порта она никогда не входит.',
        '可选：本移植自带音频库，可播放所有声音。若提供来自您自己主机（已解密）的 libSceNgs2.sprx 和 libSceUlt.sprx（shadPS4 将其保存在 user/sys_modules 中），游戏将改用主机的音频库。它们属于 Sony 固件，绝不会随本移植提供。',
        '任意：この移植には、すべての音を再生する独自のサウンドライブラリがあります。お使いのコンソールの libSceNgs2.sprx と libSceUlt.sprx（復号済み。shadPS4 では user/sys_modules に保存されます）がある場合、ゲームはコンソールのライブラリを使用します。これらは Sony のファームウェアであり、移植には一切含まれません。',
    ),
}


def translate(en, lang):
    texts = TEXTS.get(en)
    return texts[LANGUAGES.index(lang)] if texts and lang in LANGUAGES else en
