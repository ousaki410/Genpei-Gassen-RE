# 《源平合战》PC-98 逆向工程与 Linux 原生移植

> Genpei Kassen (源平合戦, KOEI, 1994, PC-98) — 逆向分析与原生 Linux/SDL 移植工程

本仓库是对 KOEI 1994 年 PC-98 平台历史策略游戏《源平合战》的**逆向分析**与
**原生 Linux 移植**工程：把原版 DOS/PC-98 的图像、文本、调色板、剧本数据逐字节
解析出来，并用一个轻量的 SDL2 引擎在 Linux 上重建其可视化与（逐步完善的）游戏逻辑。

当前进度：**只读可视化层已跑通**（解压 → 调色板 → 字体 → 剧本 → 地图/立绘渲染），
**游戏逻辑层（内政 / 行军 / 合战 / 音频）尚未实现**。详见文末【进度盘点】。

---

## 免责声明

游戏本体（`games/yphz/` 下的 `.gp`/`.exe`/`.com` 等）及其美术、文本版权归
**KOEI（コーエー）** 所有。本仓库仅用于技术研究、逆向学习与互操作性移植，
不含任何破解、盗版分发意图。请自行持有正版，并在法律允许范围内使用。

---

## 快速开始

```bash
# 1. 构建（无需系统 SDL2，仓库自带 third_party/）
cmake -S . -B build
cmake --build build -j

# 2. 运行（在仓库根目录执行，数据默认取 games/yphz）
./bin/yphz_engine --map                 # 直接进入战略地图
./bin/yphz_engine --scenario 1          # 剧本 1，武将立绘浏览
./bin/yphz_engine --officer 20 --frames 60
```

无显示器时可用于回归测试：

```bash
SDL_VIDEODRIVER=dummy ./bin/yphz_engine --frames 5 --screenshot /tmp/shot.bmp
```

---

## 目录结构约定

仓库**只跟踪 DOS 逆向目标本身**（Windows/DOSBox 重打包外壳、修改器、编译产物均被
`.gitignore` 排除）：

```
.
├── CMakeLists.txt          # 主构建入口
├── Makefile                # 薄封装（等价于内置 SDL2 的简易构建）
├── .gitignore
├── include/                # 引擎公共头文件（DOS 紧凑布局结构体）
│   ├── decompress.h        #   解压器 API + ImageDecoded
│   ├── hal_video.h         #   视频/字体 HAL API + ColorRGBA
│   ├── input.h             #   统一输入层 API（InputState / YphzKey）
│   ├── scenario.h          #   剧本数据结构（武将/城池/势力）
│   └── sdl_compat.h        #   SDL2/SDL3 唯一引入点
├── src/                    # 引擎源码（C11）
│   ├── main.c              #   入口：CLI、资源装载、双模式渲染主循环
│   ├── hal_video.c         #   SDL2 视频/软件帧缓冲/调色板/JIS 字体
│   ├── input.c             #   统一输入层（SDL 键盘/鼠标事件 → InputState）
│   ├── decompress.c        #   NPK016/NUP4 与 Kaodata 0x728 解压器
│   └── scenario.c          #   Sndata.gp 剧本解析 + 静态历史数据表
├── tools/extractor/        # Python 逆向工具（Pillow）
│   ├── extract_images.py   #   NPK016/NUP4 图像批量解包
│   ├── extract_kaodata.py  #   90 名武将立绘解包
│   ├── extract_text.py     #   LS11 文本流解包（Message.gp）
│   └── extract_palette.py  #   PLD 调色板解码
├── games/yphz/             # 原版游戏数据（PC-98，**不修改**）
├── third_party/            # 内置 SDL2 头文件 + libSDL2.so（构建依赖）
└── Plugins/                # 码表：koei.txt / Big5.txt / GBK.txt
```

被忽略（保留在本地但不入库）：`build/`、`bin/`、`extracted_assets/`、`*.bmp`/`*.png`
截图、`dosbox.exe`、`glide*`/`d3dx9_*`/`inpout*` 等 Windows 包装 DLL、`SHADERS/`、
`TOOLS/`、`DOCS/`、`FONTS/`、`win9x-drv/`、`Uninstall/`、EmuCheat 插件等。
完整规则见 `.gitignore`。

> 说明：`third_party/`（内置 SDL2）是**构建依赖**，故意保留入库，保证
> `git clone` 后即可编译（本工程不假设系统已装 SDL2）。

---

## 构建

### 依赖

| 依赖 | 版本 / 说明 |
|---|---|
| C 编译器 | GCC 或 Clang（C11） |
| CMake | ≥ 3.16 |
| SDL2 | 系统安装，或使用仓库自带 `third_party/`（默认回退） |
| SDL3 | 可选，`-DYPHZ_USE_SDL3=ON`（当前仅接通探测，API 迁移待办） |
| Python 3 + Pillow | 仅运行 `tools/extractor/` 时需要 |

### 命令

```bash
cmake -S . -B build
cmake --build build -j
```

### 构建选项

| 选项 | 取值 | 默认 | 说明 |
|---|---|---|---|
| `YPHZ_ARCH` | `native` \| `32` \| `64` | `native` | 主机字长，对应 `-m32` / `-m64` |
| `YPHZ_USE_SDL3` | `ON` \| `OFF` | `OFF` | 链接 SDL3 而非 SDL2 |
| `YPHZ_PREFER_BUNDLED_SDL2` | `ON` \| `OFF` | `OFF` | 强制使用 `third_party/` 内置 SDL2 |
| `CMAKE_BUILD_TYPE` | `Debug`/`Release`/`RelWithDebInfo`/`MinSizeRel` | `RelWithDebInfo` | 构建类型 |

统一启用 `-Wall -Wextra`；所有结构体使用 `#pragma pack(push, 1)` 还原 DOS 16 位
紧凑内存布局。

---

## 运行

编译产物位于 `bin/yphz_engine`（与 `games/`、`third_party/` 同级）。
**建议在仓库根目录运行**，以便默认数据路径 `games/yphz` 生效。

### CLI 参数

| 参数 | 缩写 | 默认 | 说明 |
|---|---|---|---|
| `--data <dir>` | `-d` | `games/yphz` | 游戏数据目录（亦可用环境变量 `YPHZ_DATA_DIR`） |
| `--scenario <n>` | `-s` | `0` | 剧本，接受 `1..4` 或 `0..3`（1180/1183/1184/1185） |
| `--officer <n>` | `-o` | `0` | 起始武将编号（0..400） |
| `--castle <n>` | `-c` | `30` | 起始城池编号（0..38，默认鎌倉） |
| `--select-castle <n>` | — | — | 直接以地图模式启动，选中第 n 城并展开其情报卡（配合 `--frames`/`--screenshot` 做无头截屏回归） |
| `--map` | `-m` | 关 | 直接以战略地图模式启动 |
| `--frames <n>` | — | `0` | 运行 n 帧后自动退出（`0` = 无限，用于回归/无头测试） |
| `--screenshot <path>` | — | — | 第 2 帧导出 640x400 BMP 并退出（隐含 `--frames 10`） |

无头回归示例：

```bash
SDL_VIDEODRIVER=dummy ./bin/yphz_engine --select-castle 30 --frames 5 --screenshot /tmp/c30.bmp
```

### 键盘操作

| 模式 | 按键 | 功能 |
|---|---|---|
| 通用 | `M` | 战略地图 ⇄ 武将立绘 切换 |
| 通用 | `ESC` | 地图：关闭城池情报卡；无卡时（或武将模式）退出 |
| 地图 | `1`–`4` | 切换剧本 |
| 地图 | `[` / `]` | 上一 / 下一城池 |
| 地图 | `O` / `Enter` | 查看当前城城主（跳转立绘） |
| 地图 | `C` / `Space` | 开/关城池情报卡 |
| 地图 | `TAB` | 在主要据点间跳转 |
| 地图 | `←`/`→`（或 `A`/`D`） | 地图水平平移 |
| 武将 | `←`/`→` | 上/下一名武将 |
| 武将 | `↑`/`↓` | ±10 名武将 |
| 武将 | `F` | 跳到下一名拥有专属立绘的武将 |
| 武将 | `Space` | 立绘遮罩（透明）开关 |
| 武将 | `TAB` | 切换背景（纯黑 / 军议大殿 / 野战军营） |

### 鼠标操作（战略地图）

| 操作 | 功能 |
|---|---|
| 左键点击城池旗标 | 命中测试选中该城（`camera_x` + 屏幕坐标 → 1696x360 世界坐标）并展开情报卡 |
| 左键点击空白处拖拽 | 拖动平移全景地图（地形跟随指针） |
| 指针靠近窗口左右边缘 | 自动平滑卷动地图 |
| 右键点击 | 关闭城池情报卡 |

> 输入实现：`src/input.c` 独占所有 `SDL_PollEvent`/`SDL_GetMouseState` 调用，向主循环
> 输出一份 `InputState`（键盘边沿、逻辑坐标 640x400 的鼠标位置/位移、左右键边沿）。
> 窗口 ⇄ 逻辑坐标换算与鼠标焦点查询由 `hal_video_window_to_logical()` /
> `hal_video_has_mouse_focus()` 提供；原先未被调用的 `hal_video_poll_event()` 死代码
> 已删除。

---

## 逆向工具链（`tools/extractor/`）

均需 `Pillow`，默认从仓库根目录运行，输出到 `extracted_assets/`（已 gitignore）。

```bash
python3 tools/extractor/extract_images.py    # NPK016/NUP4 图像 -> extracted_assets/images
python3 tools/extractor/extract_kaodata.py   # 90 名武将立绘 -> extracted_assets/images/kaodata
python3 tools/extractor/extract_text.py      # Message.gp 对话 -> extracted_assets/text
python3 tools/extractor/extract_palette.py   # Mainpal/Losepal -> extracted_assets/palettes
```

---

## 【当前逆向与移植进度盘点】

### 一、源码文件职责与已实现功能

**`src/main.c`（760 行）— 入口与渲染主循环**
- CLI 参数解析（见上表）+ `YPHZ_DATA_DIR` 环境变量。
- 资源装载 `load_file()`：`Mainpal.pld`、`JIS.FNT`/`FONT.CHR`、`Main.exe`（读
  `0x4D3BB` 处 90 个立绘偏移表）、`Kaodata.gp`、`Hikback.gp`、`Maincmd2.gp`、
  `Mainmap.gp`、`Sndata.gp`。
- `Mainmap.gp`：扫描全部 `NPK016` 块（20 块）**拼接为 1696x360 全景日本地图**，
  并对第 4/8/12/16 块做 -16px 重叠接缝消除。
- **战略地图模式**：地图水平滚动（`camera_x`）、39 城势力旗标、选中城金色准星
  动画、顶部/底部状态条、城池情报卡（势力/城主/兵力/防御/士气/训练/石高/兵糧/
  街道接续）。
- **地图鼠标交互**：左键点击旗标命中测试选城（屏幕坐标 + `camera_x` → 世界坐标）、
  空白处拖拽平移、指针贴近窗口左右边缘自动卷动、右键关闭情报卡。
- **武将立绘模式**：401 名武将逐个浏览、立绘（含遮罩透明）、3 种背景、完整属性
  面板（姓名/假名/年龄/忠节/兵力/武力/知才/优雅/弓術/兵質/身份/势力/居城）、
  16 色调色板色块。
- 截图导出（阶段帧 BMP）、固定 ~60 FPS 主循环。

**`src/hal_video.c`（377 行）— 视频 / 字体 HAL**
- SDL2 窗口/渲染器/纹理初始化（640x400 逻辑分辨率，nearest 缩放，加速失败回退软件）。
- 软件 8-bit 索引帧缓冲 `s_vram` → 32-bit RGBA 调色板查找 → `SDL_UpdateTexture`。
- 调色板 API：`set_palette_entry`、`load_pld_bank`（PC-98 GBR 通道序）。
- 绘制 API：`clear`/`draw_pixel`/`fill_rect`/`draw_rect`/`blit_indexed`/
  `blit_transparent`/`blit_masked`（mask=7 透明）。
- BMP 截图导出（24-bit bottom-up）。
- **JIS 字体子系统**：`load_fonts`（`JIS.FNT`+`FONT.CHR`）、`draw_char_8x16`
  （半角）、SJIS→区点（kuten）换算、`draw_kanji_16x16`（`112 + (ku-1)*94 + (ten-1)`
  索引 32 字节字形）、`draw_text_sjis`（半角/全角混排）。
- 输入支撑 helper：`window_to_logical()`（`SDL_RenderWindowToLogical`，窗口 → 640x400
  逻辑坐标）、`has_mouse_focus()`（`SDL_WINDOW_MOUSE_FOCUS`）。原先未被调用的
  `poll_event()` 死代码已移除。

**`src/input.c`（114 行）— 统一输入层**
- `input_poll(InputState*)`：一次排空全部 SDL 事件，向主循环输出键盘边沿
  （抽象为 `YphzKey`）、鼠标逻辑坐标 640x400 位置与帧间位移、左右键按下/边沿。
- 键盘只取 `SDL_KEYDOWN` 非重复事件；鼠标位置/按键用 `SDL_GetMouseState()` 采样，
  即使某帧无 motion 事件也保持正确。
- `mouse_inside` 同时要求窗口具有鼠标焦点，避免无头（`SDL_VIDEODRIVER=dummy`）时
  指针停在 (0,0) 误触发边缘卷动。

**`src/decompress.c`（301 行）— 解压器**
- **NPK016 / NUP4**（`nup4_decompress_block`）：LSB-first 位流 + 1600B（5 扫描行）
  环形缓冲滑窗 LZ、`0x258c` 平面 nibble 打包模拟、`0x0GRB` 12-bit 调色板读取、
  planar → 8-bit indexed 转换。
- **Kaodata 0x728**（`kaodata_decompress_officer`）：双块解压（A=图像 128x160，
  B=1-bit 遮罩），三种控制码（A：常量 RLE；B：水平拷贝；C：垂直拷贝），
  3-bitplane → 8 色像素 + 遮罩。

**`src/scenario.c`（193 行）— 剧本数据**
- 静态历史表：39 城地图坐标、15 势力（名称/旗号/旗色，SJIS）、8 道/地方名、
  4 剧本标题。
- `scenario_load()`：从 `Sndata.gp` 读取剧本（每剧本 41266 字节），解析年代/月份、
  **401 名武将**（71 字节/条，基址 `0x24`）、**39 城**（76 字节/条，基址 `0x7056`）、
  城池连接关系（`conns[]`）、势力/居城归属。

**`include/`**：`scenario.h`（`FactionDef`/`ScenarioOfficer`/`ScenarioCastle`/
`Scenario`）、`decompress.h`（`ImageDecoded`）、`hal_video.h`（`ColorRGBA` + HAL/字体
API）、`input.h`（`YphzKey`/`InputState`）、`sdl_compat.h`（SDL2/SDL3 引入切换）。
所有结构体 `#pragma pack(push,1)`。

**`tools/extractor/`（Python 参考实现）**

| 脚本 | 行数 | 职责 |
|---|---|---|
| `extract_text.py` | 146 | KOEI **LS11** 文本流解压（位流 + LZ）、字符串索引表解析、cp932 解码，导出 `entry_*.bin/.txt`、`messages.json` |
| `extract_images.py` | 309 | NPK016/NUP4 解码（`decompress.c` 的 Python 对照实现），批量解包 8 个 `.gp`、生成道具表与片头画面 |
| `extract_kaodata.py` | 201 | Kaodata 0x728 解码（对照实现），导出索引 PNG / 透明 RGBA PNG / 10x9 立绘表 + manifest |
| `extract_palette.py` | 76 | `Mainpal.pld`/`Losepal.pld`（4 bank × 16 色，BGR）→ JSON + 色板 PNG |

### 二、已跑通模块 ✅

- 图像解压：NPK016/NUP4、Kaodata 0x728（C 与 Python 双实现，结果一致）。
- 文本解压：LS11（`Message.gp` 全量对话，Python）。
- 调色板：PLD（`Mainpal`/`Losepal`）解码与渲染。
- 字体渲染：`JIS.FNT` + `FONT.CHR`，16x16 汉字 + 8x16 半角，SJIS 混排。
- 剧本加载：`Sndata.gp` → 401 武将 / 39 城 / 势力 / 连接图。
- 全景地图：`Mainmap.gp` 20 块拼接为 1696x360 并滚动渲染 + 旗标/准星/状态条。
- 武将立绘：90 专属立绘 + 通用立绘展示与属性面板。
- 城池情报卡、BMP 截图、CMake 构建（内置 SDL2）。
- 统一输入层：键盘（`YphzKey`）+ 鼠标（点击选城 / 拖拽 / 边缘卷动 / 右键关闭情报卡）。

### 三、尚未实现 / TODO ⛔

| 领域 | 现状 |
|---|---|
| **事件循环/输入** | ✅ 已抽出统一输入层 `src/input.c`（`input_poll(InputState*)`）：抽象键盘 `YphzKey` + 鼠标（逻辑坐标/位移/左右键边沿）；地图支持点击选城、拖拽/边缘/键盘卷动。仍未实现文本输入、手柄、快捷键重映射 |
| **游戏逻辑** | 无回合/月份推进、无内政（开发/征兵/征税）、无行军/移动、无外交 |
| **合战系统** | **完全未实现**：无战术地图、部队编成、战斗结算、AI |
| **势力 AI** | 未实现 |
| **存档** | `Savedata.gp` 未解析/未写入 |
| **音频** | **完全未实现**：`Music.gp`/`Opmusic.gp`/`Edmusic.gp` 未解析，FM 驱动 `Fmdrv.com` 未逆向，无 MIDI/音效 |
| **游戏内文本** | C 引擎未加载 `Message.gp` 对话；`Mainevt.gp` 仅解出图像，无事件脚本逻辑 |
| **未解析的数据文件** | `Maincmd.gp`（菜单图，仅作背景）、`Mainobj.gp`、`Mainitem.gp`（道具）、`Kisetsu.gp`、`Montage.gp`、`Opendat.gp`、`Enddat.gp`、`Disk-*.gp`、NPC 立绘（64x80 通用）等 |
| **未逆向的程序** | `Main.exe`/`Genpei.com`/`Open.exe`/`End.exe` 的主循环、菜单、合战逻辑 |

### 四、接下来可直接在 2200G 上推进的 3 个子任务

> 说明：当前全部渲染为软件 blit（640x400），2200G（Ryzen 3 2200G + Vega 8）完全
> 够用；以下均不涉及重 GPU/着色器开发，先打通**逻辑层**。

**子任务 1 — 统一输入层（键盘 + 鼠标）+ 事件循环重构 — ✅ 已完成**
- 新增 `include/input.h` / `src/input.c`：把 `main.c` 内联的事件处理抽离为
  `input_poll(InputState*)`；删除未使用的 `hal_video_poll_event()` 死代码。
- 加入鼠标命中测试：屏幕坐标 + `camera_x` 映射回 1696x360 地图坐标，实现**左键点击
  城池**选中并展开情报卡；空白拖拽/贴近边缘/方向键三种卷动；右键关闭情报卡。
- 新增 `--select-castle <n>`：无头 `--frames`/`--screenshot` 下直接渲染指定城的
  选中态与情报卡，用于回归。
- 验收：`SDL_VIDEODRIVER=dummy ./bin/yphz_engine --select-castle 30 --frames 5
  --screenshot /tmp/c30.bmp` 得到选中鎌倉（源氏/白旗）情报卡；切换 16 得福原
  （平氏/赤旗）；键盘行为不回归，`-Wall -Wextra` 零警告。

**子任务 2 — 把 `Message.gp`（LS11）与事件文本接入 C 引擎**
- 将 `extract_text.py` 的 LS11 解压移植为 C，加入 `decompress.c`/`decompress.h`
  （如 `ls11_decompress_stream()`）。
- 用已跑通的 SJIS 字体渲染器，在地图上弹框显示城池/武将台词与事件文本。
- 验收：地图模式下按某键弹出当前城的对话/事件文本（cp932 → 字形正确渲染）。
- 理由：文本解压与字体渲染两条链都已单独跑通，只差把它们接起来，产出立即可见。

**子任务 3 — 回合推进 + 最小内政闭环**
- 基于 `Scenario`（已有 `year`/`month` 与城池 `rice`/`gold`/`troops`/`morale`/
  `training`/`defense`）实现「结束回合」：按月推进、结算收入。
- 增加最小内政命令面板：选中己方城池 → **开发 / 征兵 / 征税**，修改对应数值并即时
  在情报卡反映。
- 验收：可连续结束 N 个月，城池数值按规则变化，`年/月` 正确递增。
- 理由：完全复用现有 `Sndata.gp` 数据与坐标/连接图，不依赖合战与 AI，适合在 2200G
  上快速迭代，并为后续行军/合战铺路。

补充（非本期）：**音频**（先逆向 `Fmdrv.com` + `Music.gp` 的 FM 音源格式）与
**合战战术界面**属于更大体量的独立方向，建议在以上 3 项之后再立项。
