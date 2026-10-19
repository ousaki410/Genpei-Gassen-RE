# 《源平合战》PC-98 逆向工程与 Linux 原生移植

> Genpei Kassen (源平合戦, KOEI, 1994, PC-98) — 逆向分析与原生 Linux/SDL 移植工程

本仓库是对 KOEI 1994 年 PC-98 平台历史策略游戏《源平合战》的**逆向分析**与
**原生 Linux 移植**工程：把原版 DOS/PC-98 的图像、文本、调色板、剧本数据逐字节
解析出来，并用一个轻量的 SDL2 引擎在 Linux 上重建其可视化与（逐步完善的）游戏逻辑。

当前进度：**可视化层与基础玩法闭环已跑通**（解压 → 调色板 → 字体 → 剧本 → 地图/立绘渲染 →
内政/移動/出陣/合战/夺城），**音频、存档、势力 AI 与事件脚本尚未实现**。详见文末【进度盘点】。

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
│   ├── message.h           #   Message.gp（LS11 文本）API
│   ├── scenario.h          #   剧本数据结构（武将/城池/势力）
│   ├── turn.h              #   回合/内政状态机（含按势力参数化命令）
│   ├── kassen.h            #   合战状态机（战术/一騎討/夺城/守城）
│   ├── ai.h                #   非玩家势力 AI（内政 + 战略出阵报告）
│   ├── event.h             #   历史事件表（年月 / 落城 → Mainevt 块）
│   ├── save.h              #   原生存档槽（Scenario + TurnState 快照）
│   ├── opening.h           #   片头流程（Logo → シナリオ → 大名）
│   ├── raw_sprite.h        #   裸 4 位平面精灵解码（Hunitpat/Hchikei/Mainobj）
│   └── sdl_compat.h        #   SDL2/SDL3 唯一引入点
├── src/                    # 引擎源码（C11）
│   ├── main.c              #   入口：CLI、资源装载、双模式渲染主循环
│   ├── hal_video.c         #   SDL2 视频/软件帧缓冲/调色板/JIS 字体
│   ├── input.c             #   统一输入层（SDL 键盘/鼠标事件 → InputState）
│   ├── decompress.c        #   NPK016/NUP4、Kaodata 0x728、LS11 解压器
│   ├── message.c           #   Message.gp（LS11 容器 + 字符串表 + 显示清洗）
│   ├── turn.c              #   回合/月份推进 + 开发/征兵/征税/移動
│   ├── kassen.c            #   合战：战术结算、一騎討、夺城、天下統一判定
│   ├── ai.c                #   全势力 AI 月度决策（内政 + 出阵 + 守城触发）
│   ├── event.c             #   历史事件表（源平合战年表 + 落城触发）
│   ├── save.c              #   原生存档槽读写 + 状态哈希
│   ├── opening.c           #   片头/剧本/大名选择状态机与渲染
│   ├── npk.c               #   NPK016 通用容器索引（按需解压单块）
│   ├── raw_sprite.c        #   PC-98 4 位平面裸图解码器
│   └── scenario.c          #   Sndata.gp 剧本解析 + 静态历史数据表
├── tools/                  # Python 逆向工具（Pillow）
│   ├── probe_gp_files.py   #   全部 53 个数据文件的格式/维度分类
│   ├── analyze_raw_and_exe.py  # EXE 内嵌资产目录提取 + 裸图尺寸推导
│   ├── probe_savedata.py   #   Savedata.gp 存档槽结构侦察（对比 Sndata.gp）
│   ├── decode_raw_sprites.py # 裸 4 位平面精灵解码（Hunitpat/Hchikei/Mainobj）
│   └── extractor/          #   各格式的解码参考实现
│       ├── extract_images.py   #   NPK016/NUP4 图像批量解包
│       ├── extract_kaodata.py  #   90 名武将立绘解包
│       ├── extract_text.py     #   LS11 文本流解包（Message.gp）
│       └── extract_palette.py  #   PLD 调色板解码
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
| `--test-msg <n>` | — | — | 在战略地图上弹出 `Message.gp` 第 n 条台词对话框（配合 `--screenshot` 无头验证） |
| `--msg-entry <n>` | — | `0` | `--test-msg` 使用的 LS11 条目编号（Message.gp 共 5 条） |
| `--faction <n>` | — | 鎌倉所属 | 覆盖玩家操作势力（0..14）；默认取鎌倉（30 号城）的势力 |
| `--do-cmd <cmd>` | — | — | 无头执行一次内政命令：`develop` / `recruit` / `tax` / `move` / `officers` / `end`；自动选中该城首个未行动武将并弹出带立绘的结算框（`move` 需配合 `--move-to`） |
| `--move-to <n>` | — | 首个己方邻城 | `--do-cmd move` 的目标城；必须是当前城的己方相邻城 |
| `--attack <from,to>` | — | — | 无头发动合战：从 `from` 城进攻相邻敌城 `to`，自动选择城内武力最高的未行动武将，直接进入合战界面 |
| `--battle-plan <cmds>` | — | — | 与 `--attack` / `--defend` 配合，按帧自动执行战术：`charge` / `volley` / `duel` / `retreat` / `hold`，逗号分隔（如 `volley,charge`）；索引相对合战起始帧，用于无头验证胜利、败北、退却、守城与夺城 |
| `--defend <from,to>` | — | — | 无头**守城战**：`from` 城（AI 势力）进攻相邻的玩家城 `to`，玩家作为守方应战（`1`/`2`/`3`/`4` = 突撃/斉射/一騎討/籠城） |
| `--ai-step` | — | — | 无头推进一个月并运行**全势力 AI**（内政 + 战略出阵），打印 `[AI]` 决策日志；AI 攻打玩家城池时自动切入守城战（可由 `--battle-plan` 驱动） |
| `--trigger-event <n>` | — | — | 立即播放第 n 个历史事件（0..12）：`Mainevt.gp` 对应块 CG + `Message.gp` 台词，配合 `--screenshot` 无头验证 |
| `--save <path>` | — | — | 运行结束时把完整状态（`Scenario` + `TurnState`）写入原生存档槽 |
| `--load <path>` | — | — | 启动时读取原生存档槽（在无头命令之前恢复），随后可继续内政/出陣 |
| `--save-slot <path>` | — | `Savedata.sav` | 游戏内 `F5`/`F9` 使用的存档槽路径 |
| `--dump-state` | — | — | 退出时打印状态摘要与 FNV-1a 哈希（`[STATE] ... hash=0x...`），用于存档往返比对 |
| `--show-roads` | — | — | 子任务 9：以地图模式启动并高亮绘制全部 39 城路网（实线白 + 城编号），同时打印 `[ROADS] castles=39 edges=72` 与每城邻接表 |
| `--show-item <n>` | — | — | 子任务 9：打开家宝面板并选中第 n 件家宝（0..18），打印其归属与加成 `[ITEM] id=.. owner=.. lead+.. war+.. intel+.. pol+..` |
| `--grant-item <n>` | — | — | 子任务 9：把第 n 件家宝赏赐给玩家势力的当主（校验归属），打印 `[ITEM] grant item=.. officer=..` 或 `grant failed` |
| `--import-dos-save <slot>` | — | — | 子任务 10：导入原版 `Savedata.gp` 槽（0..9）：读槽头推断剧本与年月，覆写武将/城池的可变字段，并恢复玩家势力与当主；打印 `[DOS] import slot=.. date=.. scenario=.. leader=.. faction=.. castle=..` |
| `--export-dos-save <slot>` | — | — | 子任务 10：把当前 `Scenario` + `TurnState` 写回原版 `Savedata.gp` 槽（0..9）；目标为空模板时先用 `Sndata.gp` 重建基础快照再覆写，导出前自动生成 `.bak` 备份 |
| `--dos-save-path <path>` | — | `games/yphz/Savedata.gp` | 子任务 10：覆盖 DOS 存档槽文件路径（便于在副本上测试导入/导出） |
| `--preview-raw <file> [tile]` | — | — | 子任务 10：独立预览裸 4 位平面图（`Hunitpat.gp` / `Hchikei.gp` / `Mainobj.gp`）；给出 tile 号时显示该 16×16 图块，否则显示整张 208×236 图集 |
| `--shot-frame <n>` | — | `2` | 指定 `--screenshot` 在第 n 帧抓图（便于截取合战过程/胜利结算/夺城后的地图/事件 CG） |
| `--unified` | — | — | 调试用：把 39 城全部划归玩家势力并立即播放「天下統一」结局 CG（用于无头验证） |
| `--end-turn <n>` | — | `0` | 无头连续推进 n 个月（跨年进位），并弹出最后一月的收入结算框 |
| `--click <[r]x,y[@frame]>` | — | — | 向 `InputState` 注入一次逻辑坐标点击边沿（前缀 `r` 为右键）；可重复，亦可用 `;` 串联（`--click '560,75@1;200,103@2'`），默认从第 1 帧起逐帧分配 |
| `--preview-npk <file> <block>` | — | — | 独立预览任意 NPK016 容器块（居中/全屏渲染），配合 `--screenshot` 无头验证 |
| `--gallery <pack>` | — | — | 直接打开 NPK 画廊并定位到第 n 个包（0=Logo, 1=Mainevt, 2=Mainitem, 3=Mainstl, 4=Maincmd2, 5=Hkeshiki, 6=Hkumi, 7=Hikback） |
| `--frames <n>` | — | `0` | 运行 n 帧后自动退出（`0` = 无限，用于回归/无头测试） |
| `--screenshot <path>` | — | — | 第 2 帧导出 640x400 BMP 并退出（隐含 `--frames 10`） |

无头回归示例：

```bash
SDL_VIDEODRIVER=dummy ./bin/yphz_engine --select-castle 30 --frames 5 --screenshot /tmp/c30.bmp
SDL_VIDEODRIVER=dummy ./bin/yphz_engine --test-msg 1 --frames 5 --screenshot /tmp/msg.bmp

# 子任务 3：内政命令 / 回合结算（stdout 会打印前后数值，便于逐项核对）
SDL_VIDEODRIVER=dummy ./bin/yphz_engine --select-castle 30 --do-cmd develop --frames 2 --screenshot /tmp/dev.bmp
SDL_VIDEODRIVER=dummy ./bin/yphz_engine --select-castle 30 --do-cmd recruit --frames 2 --screenshot /tmp/rec.bmp
SDL_VIDEODRIVER=dummy ./bin/yphz_engine --select-castle 30 --do-cmd tax     --frames 2 --screenshot /tmp/tax.bmp
SDL_VIDEODRIVER=dummy ./bin/yphz_engine --select-castle 30 --do-cmd officers --frames 2 --screenshot /tmp/ros.bmp
SDL_VIDEODRIVER=dummy ./bin/yphz_engine --select-castle 30 --end-turn 5 --frames 2 --screenshot /tmp/end.bmp

# 非己方城池：命令会被拒绝（--faction 0 = 源氏，16 号城属平氏）
SDL_VIDEODRIVER=dummy ./bin/yphz_engine --select-castle 16 --faction 0 --do-cmd tax --frames 2 --screenshot /tmp/deny.bmp

# 子任务 5：无头鼠标点击（菜单项 → 名册行 → 执行开发并弹结算框）
SDL_VIDEODRIVER=dummy ./bin/yphz_engine --select-castle 30 \
    --click '560,75@1;200,103@2' --frames 4 --screenshot /tmp/click_dev.bmp

# NPK016 视觉包预览（任意块）与画廊
SDL_VIDEODRIVER=dummy ./bin/yphz_engine --preview-npk Hkeshiki.gp 0 --frames 2 --screenshot /tmp/hke.bmp
SDL_VIDEODRIVER=dummy ./bin/yphz_engine --preview-npk Mainitem.gp 3 --frames 2 --screenshot /tmp/item3.bmp
SDL_VIDEODRIVER=dummy ./bin/yphz_engine --gallery 2 --frames 3 --screenshot /tmp/gallery_item.bmp

# 子任务 5：EXE 资产目录 / 裸图尺寸推导
python3 tools/analyze_raw_and_exe.py
python3 tools/analyze_raw_and_exe.py --json > /tmp/raw_layouts.json

# 子任务 6：移動 / 出陣 / 合戦
# 移動：開発 → 名册 → 目标城（也可直接命令行）
SDL_VIDEODRIVER=dummy ./bin/yphz_engine --select-castle 30 --do-cmd move --move-to 31 --frames 2 --screenshot /tmp/move.bmp
SDL_VIDEODRIVER=dummy ./bin/yphz_engine --select-castle 30 \
    --click '560,153@1;200,103@2;250,152@3' --frames 6 --shot-frame 5 --screenshot /tmp/move_ui.bmp

# 出陣：初始合战界面（Hkeshiki.gp 背景 + 双方立绘 + 兵力/士気/城防）
SDL_VIDEODRIVER=dummy ./bin/yphz_engine --attack 30,29 --frames 2 --screenshot /tmp/battle.bmp

# 自动战术：斉射 → 突撃，第 3 帧截胜利结算，第 8 帧截夺城后的地图
SDL_VIDEODRIVER=dummy ./bin/yphz_engine --attack 30,29 --battle-plan volley,charge \
    --frames 8 --shot-frame 3 --screenshot /tmp/battle_win.bmp
SDL_VIDEODRIVER=dummy ./bin/yphz_engine --attack 30,29 --battle-plan volley,charge \
    --frames 9 --shot-frame 8 --screenshot /tmp/capture.bmp

# 一騎討特写（Hkumi.gp）/ 退却结算
SDL_VIDEODRIVER=dummy ./bin/yphz_engine --attack 30,29 --battle-plan duel \
    --frames 3 --shot-frame 2 --screenshot /tmp/duel.bmp
SDL_VIDEODRIVER=dummy ./bin/yphz_engine --attack 30,29 --battle-plan retreat \
    --frames 6 --shot-frame 2 --screenshot /tmp/retreat.bmp

# 子任务 7：势力 AI / 守城战 / 历史事件
# 全势力 AI 一个月：打印内政与出阵决策，并在 AI 进攻玩家时切入守城战
SDL_VIDEODRIVER=dummy ./bin/yphz_engine --ai-step --frames 4 --shot-frame 2 --screenshot /tmp/ai.bmp

# 守城战（AI 29 号城 → 玩家 30 号城）：籠城/斉射/一騎討/突撃
SDL_VIDEODRIVER=dummy ./bin/yphz_engine --defend 29,30 --battle-plan hold,volley,volley,charge \
    --frames 12 --shot-frame 4 --screenshot /tmp/defend.bmp

# 历史事件 CG + 台词（0..12）：0=源氏挙兵 1=富士川 2=倶利伽羅峠 3=一ノ谷 4=屋島 5=壇ノ浦 …
SDL_VIDEODRIVER=dummy ./bin/yphz_engine --trigger-event 2 --frames 3 --shot-frame 2 --screenshot /tmp/event_kurikara.bmp

# 端到端：終了 → AI → 结算框 → 右键关闭 → 历史事件 →（自动过场）→ 守城战
SDL_VIDEODRIVER=dummy ./bin/yphz_engine --map \
    --click '560,231@1;r300,300@3' --battle-plan hold,volley,duel,charge \
    --frames 24 --shot-frame 6 --screenshot /tmp/chain.bmp

# 天下統一结局 CG（强制统一）
SDL_VIDEODRIVER=dummy ./bin/yphz_engine --unified --frames 2 --screenshot /tmp/ending.bmp

# 子任务 8：存档 / 读档（内政/换月 → 保存 → 重启 → 读取，状态哈希一致）
SDL_VIDEODRIVER=dummy ./bin/yphz_engine --do-cmd develop --end-turn 1 \
    --save /tmp/save.sav --dump-state --frames 2
SDL_VIDEODRIVER=dummy ./bin/yphz_engine --load /tmp/save.sav --dump-state --frames 2
# 夺城后保存 / 读取（战果一并持久化）
SDL_VIDEODRIVER=dummy ./bin/yphz_engine --attack 30,29 --battle-plan volley,charge,charge \
    --frames 9 --save /tmp/save2.sav --dump-state
SDL_VIDEODRIVER=dummy ./bin/yphz_engine --load /tmp/save2.sav --dump-state --frames 2

# 原版 Savedata.gp 结构侦察（只读）
python3 tools/probe_savedata.py

# 子任务 8：片头流程（标题 → 剧本 → 大名 → 地图），--click 无头驱动
SDL_VIDEODRIVER=dummy ./bin/yphz_engine \
    --click '320,200@1;250,110@2;200,74@3' \
    --frames 8 --shot-frame 7 --screenshot /tmp/opening_map.bmp
# 标题 / 剧本 / 大名三屏截图
SDL_VIDEODRIVER=dummy ./bin/yphz_engine --frames 2 --screenshot /tmp/title.bmp
SDL_VIDEODRIVER=dummy ./bin/yphz_engine --click '320,200@1' --frames 3 \
    --shot-frame 2 --screenshot /tmp/scenario.bmp
SDL_VIDEODRIVER=dummy ./bin/yphz_engine --click '320,200@1;250,110@2' \
    --frames 4 --shot-frame 3 --screenshot /tmp/daimyo.bmp

# 子任务 9：39 城战略路网（--show-roads 实线高亮 + 城编号 + 邻接表）
SDL_VIDEODRIVER=dummy ./bin/yphz_engine --show-roads --frames 2 --shot-frame 1 --screenshot /tmp/roads.bmp
# 移動选城模式：高亮当前城可达的邻接城池路线（先点 移動，再点名册行）
SDL_VIDEODRIVER=dummy ./bin/yphz_engine --map -c 31 --faction 0 \
    --click '560,153@1;100,100@2' --frames 6 --shot-frame 5 --screenshot /tmp/roads_move.bmp
# 出陣选城模式同理（点 出陣 而非 移動）
SDL_VIDEODRIVER=dummy ./bin/yphz_engine --map -c 30 --faction 0 \
    --click '560,179@1;100,100@2' --frames 6 --shot-frame 5 --screenshot /tmp/roads_attack.bmp

# 子任务 9：家宝系统（19 × 64×64 Mainitem.gp 图标）
SDL_VIDEODRIVER=dummy ./bin/yphz_engine --show-item 0 --frames 2 --shot-frame 1 --screenshot /tmp/item0.bmp
SDL_VIDEODRIVER=dummy ./bin/yphz_engine --grant-item 0 --frames 2 --shot-frame 1 --screenshot /tmp/grant.bmp
# 攻陷敌城有 85% 概率缴获守方家宝并弹窗展示图标
SDL_VIDEODRIVER=dummy ./bin/yphz_engine --attack 30,29 \
    --battle-plan charge,charge,charge,charge,charge,charge \
    --frames 12 --shot-frame 9 --screenshot /tmp/seize.bmp

# 一键无头回归（36 项）
tools/run_regression.sh
```

### 键盘操作

| 模式 | 按键 | 功能 |
|---|---|---|
| 通用 | `M` | 战略地图 ⇄ 武将立绘 切换 |
| 片头 | 任意键 / 点击 | 标题画面 → 剧本选择 |
| 剧本选择 | `1`–`4` / `↑↓` / `ENTER` / `ESC` | 直接选定剧本 / 移动 / 确认 / 返回标题 |
| 大名选择 | `↑↓` / 数字 / `ENTER` / `ESC` | 选择势力 / 确认并开始 / 返回剧本选择 |
| 通用 | `F5` | 保存到存档槽（默认 `Savedata.sav`，可用 `--save-slot` 指定） |
| 通用 | `F9` | 从存档槽读取并恢复游戏状态 |
| 通用 | `L` | 打开/关闭 NPK016 画廊（Logo / 事件 CG / 家宝 / 合战景等 8 个包） |
| 通用 | `I` | 直接打开家宝图标画廊（`Mainitem.gp`，19 × 64×64） |
| 地图 | `B` | 开/关**家宝面板**（19 件 64×64 图标 + 归属边框 + 名称/加成/持有势力） |
| 地图·家宝面板 | `←`/`→`/`↑`/`↓` / 左键 | 移动选择光标（5×4 网格） |
| 地图·家宝面板 | `G` | 把选中的己方家宝**赏赐**给当前城池城主（需己方城池且城主在位） |
| 地图·家宝面板 | `B` / `ESC` / 右键 | 关闭面板 |
| 通用 | `ESC` | 依次返回：结局画面 → 画廊 → 家宝面板 → 移动/出陣目标列表 → 结算对话框 → 武将一览 → 指令菜单 → 城池情报卡 → 退出 |
| 地图 | `1`–`7` | **指令菜单打开时**：开发 / 征兵 / 征税 / 移動 / 出陣 / 武将 / 终了 |
| 地图 | `1`–`4` | **指令菜单关闭时**：切换剧本 |
| 地图 | `ESC` / 右键 | 指令菜单 → 城池情报卡 → 退出；移動/出陣目标列表打开时先关闭列表 |
| 合戦 | `1` / `2` / `3` / `4` | 突撃 / 斉射 / 一騎討 / 退却（也可直接点击底部按钮） |
| 合戦 | `ESC` | 从战场退却（等同 `4`） |
| 合戦 | 左键 / 任意键 | 胜利/败北/退却结算框出现后，关闭并结算夺城 |
| 守城戦 | `1` / `2` / `3` / `4` | 突撃 / 斉射 / 一騎討 / **籠城**（AI 进攻玩家城池时，玩家作为守方应战） |
| 事件 | 左键 / 右键 / 任意键 | 关闭历史事件 CG（`Mainevt.gp`）演出 |
| 地图 | `[` / `]` | 上一 / 下一城池 |
| 地图 | `O` / `Enter` | 查看当前城城主（跳转立绘） |
| 地图 | `C` / `Space` | 开/关城池情报卡（己方城池会同时弹出指令菜单） |
| 地图 | `T` | 打开 `Message.gp` 台词对话框（再按翻到下一句） |
| 地图 | `TAB` | 在主要据点间跳转 |
| 画廊 | `←`/`→` | 上一 / 下一块（块索引自动循环钳制） |
| 画廊 | `↑`/`↓` | 上一 / 下一个资产包（Logo → Mainevt → Mainitem → …） |
| 画廊 | `L` / `ESC` / 右键 | 关闭画廊 |
| 地图 | `←`/`→`（或 `A`/`D`） | 地图水平平移 |
| 武将一览 | `↑`/`↓` | 滚动名册（每页 11 人） |
| 武将一览 | 左键点击某行 | 查看该武将立绘与能力；若处于命令选人状态则直接执行该命令 |
| 武将 | `←`/`→` | 上/下一名武将 |
| 武将 | `↑`/`↓` | ±10 名武将 |
| 武将 | `F` | 跳到下一名拥有专属立绘的武将 |
| 武将 | `Space` | 立绘遮罩（透明）开关 |
| 武将 | `TAB` | 切换背景（纯黑 / 军议大殿 / 野战军营） |

### 鼠标操作（战略地图）

| 操作 | 功能 |
|---|---|
| 左键点击城池旗标 | 命中测试选中该城（`camera_x` + 屏幕坐标 → 1696x360 世界坐标）并展开情报卡；己方城池同时弹出指令菜单 |
| 左键点击指令菜单项 | 执行开发 / 征兵 / 征税 / 移動 / 出陣 / 武将一览 / 评定终了 |
| 左键点击名册行 | 查看武将立绘与能力（或作为命令执行者；移動/出陣时进入目标城列表） |
| 左键点击目标城列表行 | 选择移動目的地或出陣目标；出陣立即进入合战界面 |
| 合战界面底部按钮 | 突撃 / 斉射 / 一騎討 / 退却；结算框点击关闭并结算夺城 |
| 左键点击空白处拖拽 | 拖动平移全景地图（地形跟随指针） |
| 指针靠近窗口左右边缘 | 自动平滑卷动地图 |
| 右键点击 | 返回上一级（目标城列表 → 名册 → 指令菜单 → 情报卡） |

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

资产格式侦察（无需第三方依赖，只读）：

```bash
python3 tools/probe_gp_files.py              # 53 个文件的 Magic/格式清单
python3 tools/probe_gp_files.py --json       # 机器可读（stdout）
python3 tools/probe_gp_files.py --csv        # 表格导入
```

---

## 资产格式盘点（`games/yphz/`，53 个文件）

> 由 `tools/probe_gp_files.py` 扫描生成：
>
> ```bash
> python3 tools/probe_gp_files.py            # 人类可读表格
> python3 tools/probe_gp_files.py --json     # 结构化输出（stdout）
> python3 tools/probe_gp_files.py --csv      # CSV
> ```
>
> 类别依据文件头 Magic + 结构启发式：`LS11` / `NPK016` / `MZ` / COM 跳转 /
> `.pld` / 字体 / cp932 文本；其余归为 `raw`，并打印前 4 个 u16、零字节占比、
> 字节熵与唯一字节数，供下一步逆向判断位平面/图块格式。

**汇总**：`raw` 19、`NPK016` 9、`DOS EXE` 6、`text` 6、`DOS COM` 3、
`font` 3、`batch` 2、`config` 2、`palette` 2、`LS11` 1。

| 文件 | 大小 (B) | 类别 | 格式 / 说明 |
|---|---:|---|---|
| `DOSJP.COM` | 3871 | DOS COM | COM binary, first opcode 0xb8 |
| `Fmdrv.com` | 4774 | DOS COM | COM binary, first opcode 0xe9 |
| `Genpei.com` | 1884 | DOS COM | COM binary, first opcode 0xe9 |
| `End.exe` | 92121 | DOS EXE | MZ, header paragraphs=352 |
| `Grpdrv.exe` | 21875 | DOS EXE | MZ, header paragraphs=32 |
| `Install.exe` | 21731 | DOS EXE | MZ, header paragraphs=32 |
| `Main.exe` | 384322 | DOS EXE | MZ, header paragraphs=4 |
| `Main.ori` | 357780 | DOS EXE | MZ, header paragraphs=2 |
| `Open.exe` | 79605 | DOS EXE | MZ, header paragraphs=320 |
| `Message.gp` | 33973 | LS11 | 5 compressed record(s), 0x110 table |
| `Hikback.gp` | 9643 | NPK016 | 1 block(s): 288x176 |
| `Hkeshiki.gp` | 208531 | NPK016 | 4 block(s): 640x400 |
| `Hkumi.gp` | 41563 | NPK016 | 5 block(s): 208x160 |
| `Logo.gp` | 8043 | NPK016 | 5 block(s): 208x112, 208x24, 368x48, 400x24, ... |
| `Maincmd2.gp` | 313971 | NPK016 | 4 block(s): 640x400, 160x128 |
| `Mainevt.gp` | 57891 | NPK016 | 13 block(s): 160x128, 128x128, 32x64 |
| `Mainitem.gp` | 20801 | NPK016 | 19 block(s): 64x64 |
| `Mainmap.gp` | 257913 | NPK016 | 20 block(s): 88x360 |
| `Mainstl.gp` | 60633 | NPK016 | 13 block(s): 64x152, 520x168 |
| `DOSV.BAT` | 26 | batch | dosvfont -f:font.dat -tj |
| `Play.bat` | 52 | batch | @echo off  cls  DOSJP -F:font.dat -TJ  genpei  c |
| `Config.5` | 51 | config | BUFFERS=10  FILES=20  DEVICE=HIM |
| `Install.sys` | 2732 | config | REM	『源平合戦』	ハードＤｉｓｋインストール情報ファイル |
| `FONT.CHR` | 4096 | font | 256 x 8x16 half-width glyphs |
| `FONT.DAT` | 366976 | font | DOS/V font data |
| `JIS.FNT` | 286336 | font | JIS X 0208 16x16 glyph bank |
| `Losepal.pld` | 192 | palette | 64 colors (4 bank(s) x 16) |
| `Mainpal.pld` | 192 | palette | 64 colors (4 bank(s) x 16) |
| `Edmusic.gp` | 3477 | raw | u16[0 0 2666 2666] zero=6.2% H=6.43b/B unique=197 \| FM/PMD ending music (not parsed yet) |
| `Enddat.gp` | 1037151 | raw | u16[0 768 513 7] zero=3.7% H=6.98b/B unique=256 \| ending / epilogue data (not parsed yet) |
| `Haikei.gp` | 47680 | raw | u16[213 21802 43520 85] zero=37.7% H=4.43b/B unique=253 \| battle background (raw, not parsed yet) |
| `Hchikei.gp` | 10060 | raw | u16[0 1 256 257] zero=42.8% H=2.50b/B unique=13 \| battle terrain map (raw, not parsed yet) |
| `Hground.gp` | 114048 | raw | u16[0 0 0 0] zero=49.8% H=3.82b/B unique=247 \| battle ground tiles (raw, not parsed yet) |
| `Hikuchi.gp` | 51840 | raw | u16[0 0 0 0] zero=61.3% H=2.71b/B unique=244 \| battle fortress/base graphics (raw, not parsed yet) |
| `Hkei.gp` | 6912 | raw | u16[255 65535 65280 255] zero=18.1% H=6.23b/B unique=255 \| battle unit/effect graphics (raw, not parsed yet) |
| `Hunitpat.gp` | 151936 | raw | u16[0 4096 16 1030] zero=59.7% H=3.29b/B unique=256 \| battle unit patterns (raw, not parsed yet) |
| `Kaodata.gp` | 495435 | raw | u16[128 160 112 38815] zero=11.0% H=6.41b/B unique=256 \| portrait bank, 90 x 0x728 records (see decompress.c) |
| `Kisetsu.gp` | 66428 | raw | u16[160 128 43637 43524] zero=6.3% H=6.32b/B unique=255 \| seasonal background (raw, not parsed yet) |
| `Mainanm.gp` | 220040 | raw | u16[255 65535 65280 255] zero=33.8% H=4.72b/B unique=256 \| animation frames (raw, not parsed yet) |
| `Maincmd.gp` | 61216 | raw | u16[65450 255 65450 510] zero=48.7% H=4.23b/B unique=256 \| command/UI sprite/bitmap data (raw, not parsed yet) |
| `Mainobj.gp` | 24544 | raw | u16[0 0 0 0] zero=43.6% H=4.64b/B unique=256 \| map object sprites (raw, not parsed yet) |
| `Montage.gp` | 239072 | raw | u16[0 2560 0 234] zero=50.7% H=4.11b/B unique=256 \| montage / ending collage (raw, not parsed yet) |
| `Music.gp` | 44640 | raw | u16[22 0 0 1600] zero=5.3% H=6.53b/B unique=255 \| FM/PMD music data (not parsed yet) |
| `Opendat.gp` | 853233 | raw | u16[0 768 517 2826] zero=6.4% H=6.88b/B unique=256 \| opening / prologue data (not parsed yet) |
| `Opmusic.gp` | 3760 | raw | u16[0 0 608 608] zero=2.8% H=6.27b/B unique=167 \| FM/PMD opening music (not parsed yet) |
| `Savedata.gp` | 434030 | raw | u16[1180 266 0 48533] zero=13.0% \| **10 × 43403 B 存档槽**：u16 LE 年@0 + 月@2 + 标志@3，18 B 处第二份年月头，24 B 起势力表（`01 00..0e 00`）；槽 0/1/2 = 1180/10、1183/1、1184/10，槽 3–9 为未用模板（见 `tools/probe_savedata.py`） |
| `Sndata.gp` | 165064 | raw | u16[1180 521 0 1] zero=36.2% H=5.33b/B unique=256 \| scenario table: 4 x 41266 B (officers/castles/conns) |
| `Disk-1.gp` | 18 | text | 源平合戦 DISK-1 |
| `Disk-2.gp` | 18 | text | 源平合戦 DISK-2 |
| `Disk-3.gp` | 18 | text | 源平合戦 DISK-3 |
| `Disk-4.gp` | 18 | text | 源平合戦 DISK-4 |
| `Disk-5.gp` | 18 | text | 源平合戦 DISK-5 |
| `Version` | 526 | text | /******************************* |

### EXE 内嵌资产目录（`tools/analyze_raw_and_exe.py`）

DOS 可执行文件里保存着完整的资产文件名目录，这解释了每个 `.gp` 在游戏中的角色与加载顺序：

| EXE | 目录偏移 | 条目 | 前缀 | 说明 |
|---|---|---:|---|---|
| `Main.exe` | `0x51741` | 28 | `C:` | 主程序（硬盘安装版）资产目录 |
| `Main.ori` | `0x51721` | 28 | `A:` / `B:` | 原始磁盘版；后 6 个资产来自 B 盘 |
| `Open.exe` | `0x11CB8` | 15 | `A:` / `B:` | 安装/片头加载器（含 `SNDATA1.GP`、`OPENDAT.GP`） |
| `End.exe` | `0x14EA8` | 3 | `A:` | 结局加载器（`ENDDAT.GP` / `LOGO.GP` / `EDMUSIC.GP`） |

`Main.exe` 目录顺序（`analyze_raw_and_exe.py` 可完整打印）：

```
MAINCMD MAINOBJ MAINSTL MESSAGE MUSIC PCM MAINPAL HAIKEI
MAINCMD2 MAINEVT MAINITEM KISETSU SNDATA HCHIKEI HKESHIKI HGROUND
HUNITPAT HIKUCHI HKUMI HIKBACK HKEI LOSEPAL MAINANM MAINMAP
KAODATA MONTAGE LOGO SAVEDATA
```

该目录前 64 字节是一张 16 位远指针形状的表（如 `offset=0x0000, segment=0x2580`），
可与加载缓冲区对应，后续反汇编 `Main.exe` 的资产加载函数即可拿到每块的实际
段地址与解压入口。

### 裸图尺寸推导

PC-98 16 色图像为 4 位平面（2 像素/字节），行按 8 像素（4 字节）对齐，因此
`align4(ceil(w/2)) * h * count == size`。工具同时测试 8bpp 与 1bpp，并对
墓地/标准分辨率加权；无法干净分解的文件标记为「疑似压缩/结构化」，不强行给出假尺寸。

| 文件 | 大小 | 推断 | 依据 |
|---|---:|---|---|
| `Hchikei.gp` | 10060 | **100×100 一字节图块 + 60 B 布阵表** | 10000 字节全为 0..12 的 tile id，尾部 60 字节是低值坐标表 |
| `Hikuchi.gp` | 51840 | 4bpp **288×360**（备选 640×162） | 51840 = 144×360 = 320×162，288 宽与 `Hkumi` 一致 |
| `Hkei.gp` | 6912 | 4bpp **96×144** 或 **288×48**（3 帧 96×48 条带） | 6912 = 48×144 = 144×48 |
| `Mainobj.gp` | 24544 | 4bpp **208×236**（或 208×59×4 帧） | 24544 = 104×236；208 与 `Hkumi` 同宽 |
| `Haikei.gp` | 47680 | 4bpp 320×298 或 640×149 → 疑似压缩 | 非 640×400 标准帧，两种分解都不自然 |
| `Hground.gp` | 114048 | 4bpp 288×792 / 8bpp 288×396 → 疑似压缩 | 行宽与帧数组合过多 |
| `Hunitpat.gp` | 151936 | 无干净分解（1187 为素数） | 4bpp 行宽只能是 4/8/…，h 恒为 1187 |
| `Kisetsu.gp` | 66428 | 头部 u16 = **160×128**，但 4bpp 应为 10240 B/帧 | 66428 不是 10240 的整数倍 → 帧内压缩 |
| `Mainanm.gp` | 220040 | 无干净分解（5501 为素数） | 同上 |
| `Maincmd.gp` | 61216 | 无干净分解（1913 为素数） | 同上 |
| `Montage.gp` | 239072 | 4bpp 496×964 / 8bpp 496×482 | 接近 512×512 结局拼图 |

> 结论：`Hchikei` 已可立即实现（图块地图）；`Hikuchi` / `Hkei` / `Mainobj`
> 有唯一/少数可信布局；其余 7 个图像在解压算法确认前不应按裸位图读取。

### 关键结论

- **NPK016 图形容器（9 个）**：`Mainmap.gp`（20 块 88x360 → 1696x360 全景）、
  `Maincmd2.gp`（4 块，含 640x400 宫殿背景）、`Mainevt.gp`（13 块事件画）、
  `Mainitem.gp`（19 块 64x64 道具）、`Mainstl.gp`（13 块）、`Logo.gp`（5 块）、
  `Hikback.gp`（1 块 288x176）、`Hkeshiki.gp`（4 块 640x400 合战景）、
  `Hkumi.gp`（5 块 208x160 阵形）。**现有 C/Python NPK016 解码器可直接解包。**
- **LS11（1 个）**：仅 `Message.gp`；C 实现已与 Python 逐字节对齐。
- **裸图 / 图块（19 个 raw）**：`Kaodata.gp` 已解析（0x728 立绘）；`Kisetsu.gp`
  开头 u16 = `160,128`，疑似 160x128 图像；`Mainanm` / `Maincmd` / `Mainobj` /
  `Montage` / `Haikei` / `Hchikei` / `Hground` / `Hikuchi` / `Hkei` / `Hunitpat`
  的零字节占比与熵符合 4-bitplane 或 8-bit 图块特征，但**无块头**，需结合
  `Main.exe` 的调用点确认宽高/调色板。
- **合战资产并非 `Btl*.gp`**：本作合战资源使用 `H*` 前缀（`Hikback`、`Hkeshiki`、
  `Hkumi`、`Hground`、`Hchikei`、`Hikuchi`、`Hkei`、`Hunitpat`、`Haikei`），
  其中 3 个为 NPK016 容器、6 个为裸图块。
- **音频（3 个 raw）**：`Music.gp` / `Opmusic.gp` / `Edmusic.gp` 无已知 Magic，
  需结合 `Fmdrv.com` 驱动反汇编确定 FM/PMD 指令流。
- **数据表（4 个 raw）**：`Sndata.gp`（已解析）、`Savedata.gp`（10 × 43403 B 存档槽，
  结构与 `Sndata.gp` 的 6 B 头 + 势力表布局不同、内部偏移漂移，故引擎改用自描述原生
  快照）、`Opendat.gp`（片头脚本）、`Enddat.gp`（结局脚本）。

### 下一步优先级

1. ✅ NPK016 视觉包已可 `--preview-npk` / 画廊预览；下一步把 `Maincmd2` / `Mainstl` /
   `Logo` / `Mainevt` 接入实际 UI（事件 CG、菜单框、开场），替换纯色占位；
2. ✅ 裸图侦察完成：`Hchikei` 已确认 **100×100 tile + 60 B 布阵表**；下一步实现图块
   渲染，并用 `Hikuchi`（288×360）/`Hkei`（96×144）验证位平面约定；
3. 顺着 `Main.exe:0x51741` 目录前的远指针表反汇编资产加载函数，定位合战/行军入口；
4. 音频最后做（`Fmdrv.com` + `Music.gp` / `Opmusic.gp` / `Edmusic.gp`）。

---

## 【当前逆向与移植进度盘点】

### 一、源码文件职责与已实现功能

**`src/main.c`（3211 行）— 入口与渲染主循环**
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
- 截图导出（阶段帧 BMP，`--shot-frame` 指定帧号）、固定 ~60 FPS 主循环。
- **台词对话框**：`Message.gp` 文本经 SJIS 半/全角软换行，绘制于画面底部（光荣
  风格金/白描边深蓝窗），角标显示 `【会話】[entry:index]`。
- **内政 UI（子任务 3/6）**：己方城池情报卡右侧弹出 `指令` 菜单（1-7 / 鼠标悬停高亮），
  名册模态窗（统率/武力/知谋，未行动者高亮、已行动者暗显，↑↓ 滚动），
  带武将立绘的结算对话框（`【結果】`，立绘 128x160 + 右侧战报），顶栏实时显示 `年/月`；
  `移動`/`出陣` 追加目标城列表模态窗。
- **合战 UI（子任务 6）**：`render_battle()` 绘制 `Hkeshiki.gp` 背景、双方立绘与
  兵力/士気/城防条、战况日志、四战术按钮，叠加 `Hkumi.gp`/`Hikback.gp` 一騎討特写
  与胜利/败北/退却结算框；`render_ending()` 绘制天下統一结局 CG。

**`src/turn.c`（497 行）— 回合与内政/军事状态机**
- `TurnState`：玩家势力、`year/month`（与 `Scenario` 双向同步）、401 人 `acted`
  位图（本回合是否已行动）。
- `turn_find_idle_officer()`：优先城主，其次按编号取城内首个未行动武将；
  `..._faction()` 变体供 AI 针对任意势力选人。
- `turn_list_castle_officers()`：枚举某势力驻某城的全部在籍武将（名册/选人用）。
- `turn_list_move_targets()` / `turn_find_attack_officer(_faction)()`：己方邻城列表 /
  城内武力最高的未行动攻方武将（供 `出陣` 与无头 `--attack` 自动选将）。
- `turn_move_officer()`：向己方邻城转移 1/3 兵力（至少 1），消耗行动；城主离开时
  自动改立城内下一名武将。
- 三条内政命令（消耗执行武将的 action）：
  - **開発**：花费金 = `200 + 石高/50`；石高 `+100+政治*3+知謀`，城防 `+1+政治/25`。
  - **徴兵**：花费金 `300+兵力/2`、兵糧 `100+兵力/4`；兵力 `+100+統率*2`，
    上限 `500+石高/8`。
  - **徴税**：金 `+100+政治*20`；士気 `-(3+(100-政治)/25)`（下限 0）。
  - 原始剧本没有独立「民忠」字段，因此以 `morale`（士気）作为民忠代理值。
- `turn_end()`：`month++`（>12 则 `year++`、`month=1`）；**所有城池**按月
  `金 += max(20, 石高/50)`，逢 3/6/9/12 月 `石高 += max(10, 石高/60)`；
  重置全员 `acted`。战报只汇总玩家势力收入。
- `turn_run_command_officer()`：可指定执行武将（名册点选），`-1` 则自动选人。
- `turn_run_faction_command()`（子任务 7）：把内政执行逻辑抽出为按 **势力** 参数化的
  `run_domestic_command()`，供 AI 对任意非玩家势力执行 開発/徴兵/徴税 并消耗行动。

**`src/kassen.c`（560 行）— 合战模块**
- `KassenState`：攻/守城与势力、双方主将、城内初始兵力、野战兵力、士気、城防、
  回合计数、`finished`/`victory`/`retreated`/`player_defends`、最近一次伤亡与一览文本。
- `kassen_list_targets()` / `kassen_can_attack()`：己方城的敌方邻城列表 / 合法性校验。
- `kassen_begin()`：守方守军 = 城内兵力、野战兵力 = 3/4（不足 80 全出），攻方同理；
  随机种子由（攻/守城 ID、攻方武将、月份）确定性生成，便于无头复现。
- `kassen_begin_defense()`（子任务 7）：AI 进攻玩家城时，玩家担任守方，置
  `player_defends = true`；其余初始化同 `kassen_begin()`。
- `kassen_command()` 四战术：
  - **突撃**：高杀伤、自损也高（武力/統率主导）；
  - **斉射**：稳定削减守方兵力与城防；
  - **一騎討**：双方主将按武力差一掷定胜负，败方兵力重创并置一騎討特写标志；
  - **退却**：保留攻方兵力直接结算为撤退；
  - **籠城**（子任务 7）：守方第五战术，稳住城防、减少伤亡。
  - 守方兵力或城防归零 → 攻方胜利；攻方兵力归零 → 败北（守方保留城）。
- **AI 与自动结算**（子任务 7）：
  - `kassen_ai_attack_decide()` / `kassen_ai_defend_decide()`：按兵力/城防/士気差
    以确定性随机选择战术；
  - `kassen_defense_round()`：守城战一个回合 —— 先由 AI 攻方出招，再由玩家守方下令；
  - `kassen_auto_resolve()`：AI vs AI 自动推演（≤40 回合），直接结算夺城。
- `kassen_apply_result()`：胜利则转移城池归属/城主/守军，残存守将退往旧势力其他
  城池（无城则退隐）；攻方残部返回出发城。撤退时守方原状、攻方兵力回城。
- `kassen_is_unified()`：39 城同属一势力则为真（触发天下統一结局）。
- `kassen_command_ascii()` / `kassen_defense_name()` / `kassen_text()`：无头日志与
  SJIS 结算文本。

**`src/ai.c`（174 行）— 非玩家势力 AI（子任务 7）**
- `ai_run_turn()`：玩家 `終了`（或 `--ai-step`）后，除玩家势力外的每个存活势力依次行动。
- **内政**：每城每月至多 2 次，优先「兵力 < 上限 3/4 且金糧充足 → 徴兵」，
  其次「金 < 800 → 徴税」，否则 開発。
- **战略出阵**：城池兵力 `> 400` 且存在「兵力差 > 180」的相邻敌城时，按
  `AI_ATTACK_CHANCE (40%)` 概率出兵；攻方武将自动选武力最高者并消耗行动。
- 攻打 **AI 城** → `kassen_auto_resolve()` 自动推演并计入 `captures`；攻打
  **玩家城** → 压入 `pending[]` 队列（上限 8），由主循环顺序切入守城战。
- 决策随机数由 `(year, month, faction)` 确定性播种（xorshift32），`--ai-step`
  输出逐字节可复现。

**`src/event.c`（75 行）+ `include/event.h` — 历史事件表（子任务 7）**
- `HistoricalEvent`：`year/month`、`Mainevt.gp` 块号、`trigger_castle`、
  SJIS 标题、`Message.gp` 关键词。
- `event_match_date()`：源平年表 —— 1180/9 源氏挙兵、1180/10 富士川、1183/5
  倶利伽羅峠、1184/2 一ノ谷、1185/2 屋島、1185/3 壇ノ浦。
- `event_match_castle_fall()`：落城事件 —— 平泉（38）陷落、屋島（6）陷落。
- 事件触发后主循环叠加 `Mainevt.gp` CG + `Message.gp` 台词（按关键词检索）。

**`src/save.c`（163 行）+ `include/save.h` — 原生存档槽（子任务 8）**
- 格式：`magic "YPHZSNAP"` + `version` + 剧本号/年月/城池数/武将数 + 39 ×
  `ScenarioCastle` + 401 × `ScenarioOfficer` + `TurnState`（势力/年月/401 行动位）+
  `TreasureState`（19 家宝归属）。单槽 63422 字节（v2），`#pragma pack(1)`，小端，
  自描述；v1（63270 B）旧槽仍可读取。
- `save_game()` / `load_game()`：只覆盖可变状态，`title_sjis` 与静态势力/坐标表仍来自
  `Sndata.gp`；校验 magic/版本/数量，截断或损坏的文件安全拒绝。
- `save_state_hash()`：对规范快照做 FNV-1a 32-bit 摘要，`--dump-state` 用它做往返比对。
- 游戏内 `F5` 保存 / `F9` 读取（默认槽 `Savedata.sav`，可用 `--save-slot` 指定），
  并在底部对话框显示「保存/復元 完了/失敗」。

**`src/opening.c`（424 行）+ `include/opening.h` — 片头流程（子任务 8）**
- `OpeningState`：`OPENING_TITLE` → `OPENING_SCENARIO` → `OPENING_DAIMYO` 三屏状态机，
  持有 `Logo.gp` 5 块图、当前剧本/高亮行、可玩势力表与当主 id、当主立绘缓存。
- **标题**：`Logo.gp` 的 KOEI 标志（块 0）、「歴史シミュレーションゲーム」（块 2）、
  「KOEI PRESENTS」（块 1）、「A KOU SHIBUSAWA PRODUCTION」（块 3）、版权（块 4），
  中央以 SJIS 描出「源平合戦」；任意键/点击进入剧本选择。
- **剧本选择**：4 个剧本（1180/9、1183/6、1184/9、1185/9）显示标题与年月，
  `1-4` 直选、`↑↓` 移动、`ENTER` 确认、`ESC` 返回；确认后重新 `scenario_load()`。
- **大名选择**：列出该剧本中占有城池的势力（最多 15），显示旗色、势力名、
  当主姓名与 `Kaodata` 立绘；当主由 `scenario_faction_leader()`（势力最强城主）确定。
  `↑↓`/数字移动、`ENTER`/点击确认 → `turn_init()` 并交回主循环进入地图。
- `--click` 可完整驱动三屏（无头截图验证）；任何“跳转类” CLI 参数都会跳过片头。

**`tools/run_regression.sh` — 36 项无头回归**

一条命令跑完全部子任务的 CLI 断言（片头点击流、内政/合战/守城/退却、AI 一个月、
历史事件、天下統一、画廊/NPK 预览、存档往返哈希一致、路网 `edges=72` 与对称连通
校验、家宝赏赐/缴获、DOS 存档槽导入/导出往返、裸 4 位平面精灵解码）。
任一项失败即非零退出，输出 `FAIL` 及末几行日志。

**`src/treasure.c`（175 行）+ `include/treasure.h` — 家宝系统（子任务 9）**
- `TreasureState`：`owner_faction[19]` + `owner_officer[19]`（-1 表示势力库房未赏赐）。
- 19 件家宝的 SJIS 名称与加成表；`treasure_init()` 按势力轮转 + 强藩优先分配归属；
  `treasure_grant()` 校验归属后记名到武将；`treasure_officer_bonus()` 汇总有效加成；
  `treasure_seize()` 以确定性种子（城/年月/帧）按 85% 概率把手牌转给胜方。
- 图标来自 `Mainitem.gp`（NPK016，19 块 64×64），由 `main.c` 一次性解码缓存。

**`src/dos_save.c` + `include/dos_save.h` — 原版 `Savedata.gp` 槽读写（子任务 10 第一部分）**
- `dos_save_read_info()`：解析 43,403 B 槽的头部（年月 / 剧本提示 / 当主家族与名 /
  势力表），并把武将表（+103）与城池表（+30,895）交给 `Scenario` 解析。
- `dos_save_apply()`：以槽内数据覆写 `Scenario` 的可变字段（武将五维、年龄、
  所属势力/城池、兵力；城池归属、石高、金、兵力、城防、士気），并同步年月。
- `dos_save_export()`：按目标槽的剧本重建基础快照（空模板）或原地 patch，
  再写回当前 `Scenario` + `TurnState`；导出前自动生成 `.bak` 备份。
- 槽头家族名字段存的是**当主家族**（源 / 平 / 藤原），据此反查当主武将与玩家势力。

**`src/raw_sprite.c` + `include/raw_sprite.h` — 裸 4 位平面精灵解码（子任务 10 第二部分）**
- `raw_sprite_decode_4plane()`：按 `[p0][p1][p2][p3]` 的逐行四段布局解码，
  色号 `p0 | p1<<1 | p2<<2 | p3<<3`，与 `decompress.c` 的 NPK 平面转换一致。
- `raw_sprite_decode_tile_bank()` / `raw_sprite_load_tile_bank()`：把
  `Hunitpat.gp` 切成 1187 块 16×16 图案（每块 128 B）。
- `src/kassen.c` 依当前兵力在战场上绘制双方 16×16 队形精灵。

**`src/npk.c`（64 行）— NPK016 通用容器访问**
- `npk_load()` 扫描文件中全部 `NPK016` magic 并记录块偏移；`npk_decode()` 按块
  调用 `nup4_decompress_block()`；`npk_free()` 释放容器。
- 供 `--preview-npk`、`L`/`I` 画廊、合战背景与事件 CG 按需使用，避免一次性占用数 MB 解码缓冲。

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

**`src/input.c`（121 行）— 统一输入层**
- `input_poll(InputState*)`：一次排空全部 SDL 事件，向主循环输出键盘边沿
  （抽象为 `YphzKey`）、鼠标逻辑坐标 640x400 位置与帧间位移、左右键按下/边沿。
- 键盘只取 `SDL_KEYDOWN` 非重复事件；鼠标位置/按键用 `SDL_GetMouseState()` 采样，
  即使某帧无 motion 事件也保持正确。
- `mouse_inside` 同时要求窗口具有鼠标焦点，避免无头（`SDL_VIDEODRIVER=dummy`）时
  指针停在 (0,0) 误触发边缘卷动。

**`src/decompress.c`（411 行）— 解压器**
- **NPK016 / NUP4**（`nup4_decompress_block`）：LSB-first 位流 + 1600B（5 扫描行）
  环形缓冲滑窗 LZ、`0x258c` 平面 nibble 打包模拟、`0x0GRB` 12-bit 调色板读取、
  planar → 8-bit indexed 转换。
- **Kaodata 0x728**（`kaodata_decompress_officer`）：双块解压（A=图像 128x160，
  B=1-bit 遮罩），三种控制码（A：常量 RLE；B：水平拷贝；C：垂直拷贝），
  3-bitplane → 8 色像素 + 遮罩。
- **LS11**（`ls11_decompress_stream`）：MSB-first 位流（一元 1 计数 + 定长段），
  `v<256` 直出字典字节，否则 `(v-256)` 距离 + `value+3` 长度的 LZ 回引。与
  `tools/extractor/extract_text.py` **逐字节一致**（含回引可越过 `dlen` 的行为）。

**`src/message.c`（205 行）— Message.gp 文本**
- LS11 容器解析：`"LS11"` 头、`0x10` 处 256 字节字典、`0x110` 起 12 字节大端记录
  （`clen`/`dlen`/`off`）。
- 每条记录调用 `ls11_decompress_stream()`，再解析字符串偏移表（uint16 LE）。
- `message_get_text()`：按 Python 参考把 `0x05` → `" [WAIT] "`、`0x0A` → `'\n'`，
  并裁剪首尾 NUL/空白，输出可直接交给 SJIS 渲染器的显示串。

**`src/scenario.c`（193 行）— 剧本数据**
- 静态历史表：39 城地图坐标、15 势力（名称/旗号/旗色，SJIS）、8 道/地方名、
  4 剧本标题。
- `scenario_load()`：从 `Sndata.gp` 读取剧本（每剧本 41266 字节），解析年代/月份、
  **401 名武将**（71 字节/条，基址 `0x24`）、**39 城**（76 字节/条，基址 `0x7056`）、
  城池连接关系（`conns[]`）、势力/居城归属。

**`include/`**：`scenario.h`（`FactionDef`/`ScenarioOfficer`/`ScenarioCastle`/
`Scenario`）、`decompress.h`（`ImageDecoded` + `ls11_decompress_stream`）、
`hal_video.h`（`ColorRGBA` + HAL/字体 API）、`input.h`（`YphzKey`/`InputState`）、
`message.h`（`MessageFile`/`MessageEntry`）、`turn.h`（`TurnState`/`TurnCommand`/
`TurnResult`）、`kassen.h`（`KassenState`/`KassenCommand`）、`ai.h`（`AiReport`/
`AiPendingBattle`）、`event.h`（`HistoricalEvent`）、`save.h`（`save_game`/
`load_game`）、`opening.h`（`OpeningState`）、`npk.h`（`NpkFile`）、
`treasure.h`（`TreasureState`/`TreasureBonus`）、
`sdl_compat.h`（SDL2/SDL3 引入切换）。
所有结构体 `#pragma pack(push,1)`。

**`tools/extractor/`（Python 参考实现）**

| 脚本 | 行数 | 职责 |
|---|---|---|
| `extract_text.py` | 146 | KOEI **LS11** 文本流解压（位流 + LZ）、字符串索引表解析、cp932 解码，导出 `entry_*.bin/.txt`、`messages.json` |
| `extract_images.py` | 309 | NPK016/NUP4 解码（`decompress.c` 的 Python 对照实现），批量解包 8 个 `.gp`、生成道具表与片头画面 |
| `extract_kaodata.py` | 201 | Kaodata 0x728 解码（对照实现），导出索引 PNG / 透明 RGBA PNG / 10x9 立绘表 + manifest |
| `extract_palette.py` | 76 | `Mainpal.pld`/`Losepal.pld`（4 bank × 16 色，BGR）→ JSON + 色板 PNG |

`tools/` 根目录另有三个独立侦察脚本（非本目录）：`probe_gp_files.py`（285 行；53 个资产
文件的 Magic/格式侦察，表格 + JSON + CSV）、`analyze_raw_and_exe.py`（459 行；提取
`Main.exe`/`Open.exe`/`End.exe` 内嵌资产目录，并对 11 个裸图按 4bpp/8bpp/1bpp 推导
`(w,h,count)`，自动识别 `Hchikei.gp` 的 100×100 图块地图特例）与 `probe_savedata.py`
（10 × 43403 B 存档槽头/势力表/鎌倉偏移侦察，并对比 `Sndata.gp`）。

### 二、已跑通模块 ✅

- 图像解压：NPK016/NUP4、Kaodata 0x728（C 与 Python 双实现，结果一致）。
- 文本解压：LS11（`Message.gp` 全量对话，Python）。
- 调色板：PLD（`Mainpal`/`Losepal`）解码与渲染。
- 字体渲染：`JIS.FNT` + `FONT.CHR`，16x16 汉字 + 8x16 半角，SJIS 混排。
- 剧本加载：`Sndata.gp` → 401 武将 / 39 城 / 势力 / 连接图。
- 战略路网：39 城邻接图（`Sndata.gp` 偏移 16 的 7 槽，72 条对称边、全连通），
  出阵/移动/AI 目标受路网约束，地图古道渲染 + 选城高亮。
- 家宝系统：`Mainitem.gp` 19 × 64×64 图标，名称/加成/归属/赏赐/缴获 + `B` 面板。
- 全景地图：`Mainmap.gp` 20 块拼接为 1696x360 并滚动渲染 + 旗标/准星/状态条。
- 武将立绘：90 专属立绘 + 通用立绘展示与属性面板。
- 城池情报卡、BMP 截图、CMake 构建（内置 SDL2）。
- 统一输入层：键盘（`YphzKey`）+ 鼠标（点击选城 / 拖拽 / 边缘卷动 / 右键关闭情报卡）。
- LS11 文本解压（`Message.gp`，C 实现，与 Python 逐字节一致）+ 底部光荣风格台词对话框。
- 回合与内政闭环：指令菜单（開発/徴兵/徴税/移動/出陣/武将/終了）、武将名册与点选执行、
  带立绘的结算对话框、按月推进与跨年、全城收入/季度石高增长、全员行动重置。
- 合战闭环：`Hkeshiki.gp` 全屏战场背景 + 双方 `Kaodata.gp` 立绘/兵力/士気/城防条，
  突撃/斉射/一騎討/退却四战术、`Hkumi.gp`/`Hikback.gp` 一騎討特写、夺城与势力转移、
  天下統一结局 CG（`Mainevt.gp`）。
- 势力 AI（子任务 7）：`終了` 后所有非玩家势力自动内政 + 战略出阵；AI 互相攻伐由
  `kassen_auto_resolve()` 自动结算（`〇〇軍が××城を攻略！`），AI 进攻玩家则切入
  **守城战**（玩家守方，突撃/斉射/一騎討/籠城）。
- 历史事件（子任务 7）：按年月或落城触发 `Mainevt.gp` CG + `Message.gp` 台词叠加层。
- 存档/读档（子任务 8）：`F5`/`F9` 与 `--save`/`--load` 使用 63422 B 原生快照
  （`Scenario` + `TurnState` + 家宝表），`--dump-state` 哈希验证往返完全一致。
- 片头流程（子任务 8）：`Logo.gp` 标题 → 4 剧本选择 → 15 势力大名选择（旗色 +
  当主立绘）→ 地图；无跳转类 CLI 参数时自动进入。

### 三、尚未实现 / TODO ⛔

| 领域 | 现状 |
|---|---|
| **事件循环/输入** | ✅ 已抽出统一输入层 `src/input.c`（`input_poll(InputState*)`）：抽象键盘 `YphzKey` + 鼠标（逻辑坐标/位移/左右键边沿）；地图支持点击选城、拖拽/边缘/键盘卷动。仍未实现文本输入、手柄、快捷键重映射 |
| **游戏逻辑** | ✅ 回合/月份推进与内政（开发/征兵/征税/换月）、城内武将移動、出陣与夺城、**非玩家势力 AI（内政 + 战略出阵）** 已完成；仍无外交与跨城行军路线 |
| **合战系统** | ✅ 基础闭环已完成：战场背景、双方立绘与兵力/士気/城防、突撃/斉射/一騎討/退却、一騎討特写、夺城/势力转移、天下統一结局；**AI 攻方自动出招、玩家守城战、AI vs AI 自动推演** 已完成；仍无部队编成与战术地图 |
| **势力 AI** | ✅ 已完成：每月按势力执行内政（徴兵/徴税/開発）与战略出阵（兵力优势判定 + 概率出兵），确定性随机可复现 |
| **历史事件** | ✅ 年月表（1180/9～1185/3 六个源平合战节点）与落城事件（平泉/屋島）触发 `Mainevt.gp` CG + `Message.gp` 台词；`Mainevt.gp` 事件脚本引擎仍未逆向 |
| **存档** | ✅ `Savedata.gp` 结构已侦察（10 × 43403 B：年/月头 + 势力表 + 漂移的场景快照）；引擎用自描述原生槽（`Scenario` + `TurnState`，63422 B），支持 `F5`/`F9` 与 `--save`/`--load`，往返状态哈希一致；**原版 `Savedata.gp` 字节读写已实现**（`--import-dos-save` / `--export-dos-save`，槽布局：武将 @ +103、运行块 2070 B、城池 @ +30895、合计 +2137 B 漂移），导入→导出→再导入哈希一致 |
| **片头/选势力** | ✅ `Logo.gp` 标题 → 4 剧本选择 → 15 势力大名选择（旗色 + 当主 `Kaodata` 立绘）→ 地图；无跳转类 CLI 参数时自动进入，所有既有 CLI 路径保持不变 |
| **音频** | **完全未实现**：`Music.gp`/`Opmusic.gp`/`Edmusic.gp` 未解析，FM 驱动 `Fmdrv.com` 未逆向，无 MIDI/音效 |
| **游戏内文本** | ✅ C 引擎已加载 `Message.gp`（LS11，5 条 / 2350 句）并在底部弹框显示；`Mainevt.gp` 事件脚本逻辑仍未逆向 |
| **未解析的数据文件** | `Maincmd.gp`、`Kisetsu.gp`、`Montage.gp`、`Opendat.gp`、`Enddat.gp`、`Disk-*.gp` 等裸格式未解压；`Mainitem.gp` 已接入 19 件家宝系统（名称/加成/赏赐/缴获）；`Hunitpat.gp` / `Hchikei.gp` / `Mainobj.gp` 已接入裸 4 位平面解码器（16×16 图块 / 208×236 图集），合战画面按兵力绘制双方队形 |
| **未逆向的程序** | `Main.exe`/`Genpei.com`/`Open.exe`/`End.exe` 的主循环、菜单、合战逻辑 |

### 四、2200G 上的子任务（1–10 已完成）

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

**子任务 2 — 把 `Message.gp`（LS11）与事件文本接入 C 引擎 — ✅ 已完成**
- 将 `extract_text.py` 的 LS11 解压移植为 C：`decompress.h`/`decompress.c` 的
  `ls11_decompress_stream()`。**逐字节验证**：`Message.gp` 5 条记录的解压输出与
  Python `entry_*.bin` 完全一致，2350 句文本（cp932→UTF-8 后）与 Python `.txt` 完全一致。
- 新增 `include/message.h` / `src/message.c`：加载并解压 `Message.gp` 全部条目，
  解析字符串表，`message_get_text()` 输出可直接渲染的 SJIS 显示串。
- 复用现有 JIS 渲染器，绘制底部光荣风格对话框（半/全角软换行）。
- 新增 `--test-msg <n>`（配 `--msg-entry`）与地图内 `T` 键，无头截图可直接验证。
- 验收：`SDL_VIDEODRIVER=dummy ./bin/yphz_engine --test-msg 1 --frames 5
  --screenshot /tmp/msg.bmp` 得到正确台词框；`-Wall -Wextra` 零警告。

**子任务 3 — 回合推进 + 最小内政闭环（含武将头像联动）— ✅ 已完成**
- 新增 `include/turn.h` / `src/turn.c`：维护玩家势力、`year`/`month`（与 `Scenario`
  同步）、401 人 `acted` 标记；提供首个未行动武将选择、城内武将枚举、命令执行。
- 己方城池情报卡右侧弹出光荣风格 `指令` 菜单（`1`–`7` 或鼠标点击；`ESC`/右键返回）。
  - `開発`：金 → 石高 + 城防；`徴兵`：金 + 兵糧 → 兵力（受 `500+石高/8` 上限）；
  - `徴税`：金 +，士気 −（原数据无民忠字段，以士気代理）；
  - `移動`：选执行武将 → 己方邻城列表 → 转移该将 1/3 兵力（至少 1）；
  - `出陣`：选执行武将 → 敌方邻城列表 → 进入合战；
  - `武将`：城内武将名册（统率/武力/知谋，未行动者高亮），点选查看立绘；
  - `終了`：`month++`、跨年进位、全城月度金收入 + 季度石高增长、全员行动重置。
- 命令执行前先进入名册选择执行武将；结果对话框调用 `Kaodata.gp` 0x728 解码器，
  左侧渲染 128x160 立绘，右侧显示 `源頼朝 / 開発 完了 / 石高 +457 城防 +4 金 -1120`。
- 新增 `--do-cmd <develop|recruit|tax|officers|end>` 与 `--end-turn <N>` 无头参数；
  stdout 同时打印 **BEFORE/AFTER** 数值，便于逐项核对（含跨年 `1180/12 → 1181/1`）。
- 验收：`--do-cmd develop` 石高 `46000→46457`、金 `40000→38880`；`recruit`
  兵力 `220→450`；`tax` 金 `40000→42000`、士気 `230→227`；`--end-turn 5` 到
  `1181/2`，第 3 个月石高 `+3532`。`-Wall -Wextra` 零警告。

**子任务 5 — 无头鼠标点击注入 + 9 个 NPK016 视觉包 + 裸图尺寸推导 — ✅ 已完成**
- 新增 `--click <x,y[@frame]>`：在帧循环内直接改写 `InputState` 的鼠标位置与左键
  边沿（`InputState.synthetic` 标记，跳过边缘自动卷动），支持重复传参与 `;` 串联。
  验收：`--click '560,75@1;200,103@2'` 精确复现「点指令菜单 → 点名册首行」，
  stdout 输出 `[TURN] DEVELOP ... gold=-1120 rice=+457 def=+4`，与 `--do-cmd develop`
  数值完全一致。
- 新增 `include/npk.h` / `src/npk.c`：通用 NPK016 容器索引（扫描全部 `NPK016`
  magic、按需解压单块，避免一次性展开 `Hkeshiki` 的 4×640×400）。
- 新增 `--preview-npk <file> <block>` 独立预览；游戏内 `L` 打开 8 包画廊
  （Logo / Mainevt / Mainitem / Mainstl / Maincmd2 / Hkeshiki / Hkumi / Hikback），
  `I` 直取 `Mainitem.gp` 的 19 个 64×64 家宝图标；`←/→` 换块、`↑/↓` 换包、`ESC` 关闭。
  标题/说明统一转大写 ASCII（PC-98 `FONT.CHR` 小写不可靠）。
- 新增 `tools/analyze_raw_and_exe.py`：
  - 提取 `Main.exe`/`Main.ori`/`Open.exe`/`End.exe` 内嵌资产目录（28/28/15/3 条）
    及目录前 64 字节的远指针常量；
  - 对 11 个裸图按 4bpp/8bpp/1bpp 分解 `(w,h,count)` 并打分；
  - 识别图块地图特例：`Hchikei.gp` = **100×100 tile + 60 B 布阵表**。
- 验收：`--preview-npk Hkeshiki.gp 0` 渲染 4 张合战景之一（640×400，含左右上角
  头像框与底部指令框）；`--gallery 2` 渲染家宝图标画廊；`-Wall -Wextra` 零警告。

**子任务 6 — 移動 / 出陣 + 合战模块（战场、战术、一騎討、夺城、天下統一）— ✅ 已完成**
- 内政菜单由 5 项扩为 7 项：`開発 / 徴兵 / 徴税 / 移動 / 出陣 / 武将 / 終了`，键位 `1`–`7`，
  保持“菜单打开时优先于剧本切换”的既有约定。選擇执行武将后弹出**目标城列表**
  （`移動`=己方邻城，`出陣`=敌方邻城），支持 `1`–`8` 与鼠标点选，`ESC`/右键取消。
- `include/turn.h` / `src/turn.c`：新增 `TURN_CMD_MOVE`/`TURN_CMD_ATTACK`、
  `turn_list_move_targets()`、`turn_find_attack_officer()`、`turn_move_officer()`；
  移動按 1/3 兵力转移并消耗行动；若城主离开则自动改立城内下一名武将。
- 新增 `include/kassen.h` / `src/kassen.c`（约 380 行）：
  - `KassenState`：攻/守势力、主将、兵力、士気、城防、回合数、决胜/退却标志；
  - 守方野战兵力 = 城内 3/4（不足 80 时全出），攻方同理；随机种子由城 ID/武将/月份
   确定性生成，便于无头复现；
  - `突撃`（高杀伤、高自损）、`斉射`（稳定削守方）、`一騎討`（按武力差一掷定胜负，
    败方损失惨重）、`退却`（保留兵力撤退）；
  - 守方兵力或城防归零 → 攻方胜利；攻方兵力归零 → 败北；
  - `kassen_apply_result()`：夺城后转移城池归属、城主与守军，残存守将退往旧势力
    其他城池（无城则退隐）；攻方部队返回出发城；
  - `kassen_is_unified()`：39 城同属一个势力 → 进入「天下統一」结局 CG。
- 合战界面（`render_battle`）：`Hkeshiki.gp` 4 张 640×400 战场背景轮换；顶部双方
  主将名/势力；左右 `Kaodata.gp` 128×160 立绘；兵力/士気/城防条；中部战况日志；
  底部 4 个战术按钮。一騎討时按回合奇偶叠加 `Hkumi.gp`（208×160）或
  `Hikback.gp`（288×176）特写，并显示「攻方/守方 勝利」。
- 新增 CLI：`--attack <from,to>`（自动选城内武力最高的未行动武将）、
  `--battle-plan <charge|volley|duel|retreat,...>`（每帧执行一条）、
  `--move-to <n>`、`--shot-frame <n>`（默认 2）、`--unified`（强制统一以验证结局）。
- 验收（`SDL_VIDEODRIVER=dummy`，零警告）：
  - `--attack 30,29`：鎌倉 猪俣則綱（165） vs 伊豆 大庭景親（200），城防 450；
  - `--battle-plan volley,charge`：斉射 48/118 → 突撃 98/82，守军归零，
    `victory=1`，伊豆城防 407、归属转为势力 0、城主 猪俣則綱、守军 19；
  - `--battle-plan duel`：一騎討特写正常渲染（本次守方胜，攻方 165→4）；
  - `--battle-plan retreat`：`retreated=1`，伊豆仍属势力 14、兵力 200 不变；
  - 鼠标链路 `--click '560,153@1;200,103@2;250,152@3'` → `[TURN] MOVE 30 -> 31`，
    兵力 220→147、目标城 200→273；`560,179@1` 同链路 → 出陣 30→27 并进入合战；
  - `--unified`：39 城全部划归势力 0，显示 `Mainevt.gp` block 4 + 「天下統一」结局。

**子任务 7 — 非玩家势力 AI + 玩家守城战 + 历史事件 CG — ✅ 已完成**
- `include/turn.h` / `src/turn.c`：把内政执行抽出为按势力参数化的
  `run_domestic_command()`，新增 `turn_run_faction_command()`（仅開発/徴兵/徴税）、
  `turn_find_idle_officer_faction()`、`turn_find_attack_officer_faction()`；
  原有玩家接口改为对 `player_faction` 的薄封装。
- 新增 `include/kassen.h` / `src/kassen.c` 扩展：
  - `KASSEN_CMD_HOLD`（籠城，守方第五战术）与 `KassenState.player_defends`；
  - `kassen_begin_defense()`、`kassen_ai_attack_decide()`、`kassen_ai_defend_decide()`、
    `kassen_defense_round()`（AI 攻方先出招、玩家守方后应招）、
    `kassen_auto_resolve()`（AI vs AI ≤40 回合自动推演并结算夺城）。
- 新增 `include/ai.h` / `src/ai.c`：`ai_run_turn()` 在玩家 `終了` 后驱动全部非玩家势力：
  - **内政**：每城至多 2 次，兵力不足上限 3/4 且金糧足 → 徴兵；金 < 800 → 徴税；否则 開発；
  - **战略出阵**：兵力 > 400 且存在兵力差 > 180 的相邻敌城时按 40% 概率出兵；
  - 攻 AI 城 → `kassen_auto_resolve()`（成功打印 `faction=N CAPTURED castle=.. (from ..)`）；
    攻玩家城 → 压入 `pending[]`（≤8），主循环顺序切入守城战；
  - 随机数由 `(year, month, faction)` 确定性播种，`--ai-step` 输出可逐字节复现。
- 新增 `include/event.h` / `src/event.c`：历史事件表 `event_match_date()` /
  `event_match_castle_fall()` —— 1180/9 源氏挙兵、1180/10 富士川、1183/5 倶利伽羅峠、
  1184/2 一ノ谷、1185/2 屋島、1185/3 壇ノ浦，以及平泉（38）/屋島（6）落城事件；
  触发后叠加 `Mainevt.gp` 对应块 CG + 按关键词检索的 `Message.gp` 台词。
- `src/main.c` 集成：
  - `終了` → `turn_end` → `ai_run_turn` → 结算框（含 AI 诸势力动静静摘要）→ 事件 CG →
    依次进入 `pending[]` 守城战（模态顺序：结算框 → 事件 → 守城战）；
  - 合战界面守方按钮组切换为 **突撃/斉射/一騎討/籠城**，战果标题按攻守视角反转；
  - `--battle-plan` 新增 `hold`，且计划索引改为**相对合战起始帧**（队列守城战亦对齐）；
  - `--click` 新增右键注入（`r x,y[@frame]`），可无头关闭结算框以串联整条链路。
- 新增 CLI：`--ai-step`、`--trigger-event <0..12>`（`--screenshot` 抓事件 CG）、
  `--defend <from,to>`（AI 攻玩家城的守城战）。
- 验收（`SDL_VIDEODRIVER=dummy`，零警告）：
  - `--ai-step`：`1180/10` 全势力内政 62 次、出阵 5 次、落城 4 座、1 次扑向玩家城，
    确定性可复现；
  - `--defend 29,30 --battle-plan hold,volley,volley,charge`：AI 29→玩家 30 守城战，
    攻方 150 vs 守军 220；`charge/volley` 可击退、`duel` 会失守，双结局可达；
  - `--trigger-event 2`：`Mainevt.gp` block 2（160×128 倶利伽羅峠）渲染成功；
  - 端到端 `--click '560,231@1;r300,300@3'`：`終了` → AI → 结算框 → 右键关闭 →
    历史事件 →（自动过场）→ 守城战，全链路 `[AI]`/`[EVENT]`/`[KASSEN]` 日志连贯。

**子任务 8 — 存档 / 读档 + 片头流程 — ✅ 已完成**
- **原版 `Savedata.gp` 结构侦察**（`tools/probe_savedata.py`，只读）：
  - 434030 B = **10 × 43403 B** 定长槽；槽头 `u16 LE 年@0`、`u8 月@2`、`u8 标志@3`，
    18 B 处有第二份年月头；24 B 起为 15 项势力表（`01 00 02 00 … 0e 00 ff ff`）；
  - 槽 0/1/2 分别保存 **1180/10、1183/1、1184/10**，槽 3–9 为未使用模板（年字段非法）；
  - 与 `Sndata.gp` 对比：`Sndata` 是 6 B 头 + 偏移 6 的势力表，而存档在偏移 24；
    且 `鎌倉` 在存档中位于 33175、`Sndata` 中位于 31038（**+2137 漂移**），说明内部
    字段偏移并非简单平移，原版槽**无法**直接喂给现有 `Sndata` 解析器。
- **原生快照**（`include/save.h` / `src/save.c`）：`magic "YPHZSNAP"` + 版本 +
  剧本号/年月/数量 + 39 × `ScenarioCastle` + 401 × `ScenarioOfficer` + `TurnState`，
  单槽 **63422 B**（v2；子任务 9 追加了 152 B 家宝归属表，v1 的 63270 B 槽仍可读）、
  `#pragma pack(1)`、小端自描述；读取时只覆盖可变状态，静态表仍来自
  `Sndata.gp`，并校验 magic/版本/数量。
- **游戏内**：`F5` 保存 / `F9` 读取（默认槽 `Savedata.sav`，可用 `--save-slot <path>`），
  底部对话框显示「保存/復元 完了/失敗」，成功后同步指令菜单与窗口标题。
- **CLI**：`--save <path>`（退出时写槽）、`--load <path>`（启动时读槽）、
  `--dump-state`（打印 `[STATE] ... hash=0x...`）。
- 验收（`SDL_VIDEODRIVER=dummy`，零警告）：
  - `--do-cmd develop --end-turn 1 --save /tmp/a.sav --dump-state --frames 2`
    → `[SAVE] wrote … hash=0x66acba6e`；重启 `--load /tmp/a.sav --dump-state`
    → **同一哈希 0x66acba6e**（内政 + 换月往返 100% 一致）；
  - `--attack 30,29 --battle-plan volley,charge,charge --frames 9 --save /tmp/b.sav
    --dump-state` → `hash=0x9b9dc3bc`（含缴获的平家納経）；`--load /tmp/b.sav
    --dump-state` → 同哈希（夺城结果一并持久化）；
  - 损坏/缺失存档被安全拒绝（截断、magic 不符、无法打开均打印 `[ERROR]` 后继续）。

**子任务 8（第二部分）— 片头标题 / 剧本选择 / 大名选择**
- 新增 `include/opening.h` / `src/opening.c`（424 行）：
  - **标题屏**：`Logo.gp` 5 块（KOEI 标志、歴史シミュレーションゲーム、
    KOEI PRESENTS、A KOU SHIBUSAWA PRODUCTION、版权）+ SJIS「源平合戦」标题，
    任意键/点击 → 剧本选择；
  - **剧本选择**：4 个剧本（1180/9 源氏挙兵、1183/6 倶利伽羅峠、1184/9 一ノ谷、
    1185/9 壇ノ浦）显示年月与标题，`1-4`/`↑↓`/`ENTER`/点击/`ESC`；
  - **大名选择**：该剧本中占有城池的势力（最多 15），显示旗色、势力名、当主姓名
    与 `Kaodata` 128×160 立绘；`↑↓`/数字/`ENTER`/点击/`ESC`。
- `scenario.c` 新增 `scenario_get_title/year/month()` 与
  `scenario_faction_leader()`：**当主 = 该势力最强城主**（原始剧本无显式大名字段），
  与史实一致 —— 鎌倉源氏→源頼朝、平氏→平清盛、木曽源氏→源義仲、
  奥州藤原氏→藤原秀衡、甲斐源氏→武田信義、比叡山僧兵→大庭景親 等。
- `src/main.c` 集成：仅当**没有**任何跳转类参数（`--map`/`--select-castle`/
  `--attack`/`--defend`/`--do-cmd`/`--end-turn`/`--ai-step`/`--trigger-event`/
  `--test-msg`/`--gallery`/`--preview-npk`/`--unified`/`--load`/`-s`/`-c`/`-o`/
  `--faction`）时进入片头；确认大名后设置 `player_faction`、当前城（当主居城）、
  武将立绘与相机，直接进入地图并打开指令菜单。其余路径行为完全不变。
- 验收（`SDL_VIDEODRIVER=dummy`，零警告）：
  - `--frames 2 --screenshot`：标题屏（KOEI 标志 + 歴史シミュレーションゲーム +
    源平合戦 + 版权）渲染成功；
  - `--click '320,200@1;250,110@2;200,74@3'`：标题 → 剧本 0 → 大名 0 →
    `[OPENING] enter map: faction=0 castle=30 officer=0 date=1180/9`（鎌倉・源頼朝）；
  - `--click '320,200@1;250,110@2;200,84@3'`：选 **平氏** →
    `faction=1 castle=16 officer=45`（福原・平清盛），地图/指令菜单正确；
  - `--click '320,200@1;250,166@2;200,74@3'`：选剧本 2 →
    `date=1183/6`，地图切换到寿永二年；
  - 全部既有回归路径（`--map`、`--select-castle`、`--attack`、`--do-cmd`、
    `--end-turn`、`--ai-step`、`--trigger-event`、`--defend`、`--unified`、
    `--gallery`、`--preview-npk`、`--test-msg`、`--scenario`、`--save`/`--load`、
    鼠标指令链路）均**跳过片头**并保持原输出。

**子任务 9 — 39 城战略路网 + `Mainitem.gp` 家宝系统 — ✅ 已完成**
- **路网数据来源（无需臆造）**：`Sndata.gp` 的 39 × 76 B 城池记录在偏移 **16** 处
  就带有 8 个 `u16 LE` 邻接槽：槽 0 为保留标志，槽 1–7 为邻城 id（`0xFFFF` 空）。
  解析后得到 **72 条无向边**、度数 1–7、**全图连通且完全对称**（无自环/重复）。
  `scenario.c` 新增 `scenario_are_adjacent()` / `scenario_road_edge_count()`；
  所有出阵/移动/AI 目标原本就走 `conns[]`，现在再由 `battle_enter()` 统一校验，
  非邻接的 `--attack 30,0` 会直接拒绝：`[ERROR] battle: castles 30 and 0 are not adjacent`。
- **路网渲染**：`draw_map_roads()` 以 Bresenham 虚线把 72 条古道画在地图全景上
  （常态金色虚线；`--show-roads` 为白色实线 + 城编号 + 节点方块）；进入 **移動 /
  出陣** 选城模式时，当前城到列表内所有可达城的路线以白色实线高亮并加方框，
  同时隐藏情报卡以免遮挡。
- **家宝系统**（`include/treasure.h` / `src/treasure.c`，`Mainitem.gp` 19 × 64×64）：
  - 19 件家宝均有 SJIS 名称与四项加成（統率/武力/知略/政治，+3…+10）：
    八咫鏡、八尺瓊勾玉、草薙剣、平家琵琶、源氏太刀、河内弓、菊一文字、小烏丸、
    蜻蛉切、村正、三日月宗近、小竜景光、一期一振、骨喰藤四郎、平家納経、
    厳島絵巻、源平合戦図屏風、菊水の盃、名馬磨墨。
  - 初始归属：19 件按 15 势力轮转分配，余下 4 件给城池最多的 4 家；归属与
    “已赏赐给某武将”分层记录（`owner_faction[19]` + `owner_officer[19]`）。
  - **查看/赏赐**：地图 `B` 打开 5×4 家宝面板（归属势力旗色边框、选中项名称/
    加成/持有势力）；`G` 把己方家宝赏赐给当前城城主，武将名册详情同步显示
    “家宝 …”与 `統率 65(+8)` 形式的**有效能力**。
  - **缴获**：玩家攻陷敌城时按确定性种子以 85% 概率夺取守方一件家宝，并在
    结算后用对话框展示 64×64 图标（“家宝：平家納経”）。
- **存档**：`TurnState` 内嵌 `TreasureState`，原生槽升级为 **v2 / 63422 B**；
  仍兼容读取 v1 的 63270 B 槽（家宝表按当前剧本重新初始化）。
- **CLI**：`--show-roads`（打印 39 城邻接表 + 高亮路网）、`--show-item <0..18>`
  （面板定位 + `[ITEM] id/owner/加成`）、`--grant-item <0..18>`（无头赏赐校验）。
- **回归**：新增 `tools/run_regression.sh`，一条命令跑完 **32 项**无头检查
  （片头/内政/合战/AI/事件/存档往返/路网对称连通/家宝赏赐/缴获），全部 PASS；
  `-Wall -Wextra` 零警告。

**子任务 10 — 原版 `Savedata.gp` 导入/导出 + 裸 4 位平面精灵 — ✅ 已完成**

**第一部分：DOS 存档槽读写**
- **槽布局**（由 `tools/probe_savedata.py` 的滑动窗口结构签名比对确定）：
  `Savedata.gp` = 434,030 B = **10 × 43,403 B** 定长槽。槽内偏移：
  `0` 年（u16 LE）、`2` 月（u8）、`3` 标志、`4` 剧本提示（u16）、
  `6` 当主家族名（7 B SJIS）、`13` 当主名（5 B SJIS）、`18` 第二份年月头、
  `24` 15 项势力表（u16 × 15）；**武将表 @ +103**（401 × 71 B）、
  其后 **2,070 B 运行时/动作块**、**城池表 @ +30,895**（39 × 76 B）、
  尾部 9,544 B 事件区。相对 `Sndata.gp` 合计 **+2,137 B 漂移**
  = 表头 +67 + 运行块 +2,070；记录步长（71 / 76 B）与尾部长度完全不变。
  实测槽 0/1/2 分别保存 1180/10、1183/1、1184/10，槽 3–9 为空模板。
- **引擎实现**：`include/dos_save.h` / `src/dos_save.c`
  - `dos_save_read_info()`：读槽头 → 年月 / 剧本号 / 当主名 / 势力；
  - `dos_save_apply()`：把槽内可变字段（武将的年龄·五维·所属·城池·兵力、
    城池的归属·石高·金·兵力·城防·士気、年月）覆写进 `Scenario`；
  - `dos_save_export()`：把当前 `Scenario` + `TurnState` 写回目标槽
    （目标为空模板时先用 `Sndata.gp` 重建基础快照再覆写；已有槽原地 patch）。
- **CLI**：`--import-dos-save <0..9>`、`--export-dos-save <0..9>`、
  `--dos-save-path <path>`（默认 `games/yphz/Savedata.gp`；导出前自动 `.bak` 备份）。
  导入会先按槽头推断剧本再加载 `Sndata.gp`，并把玩家势力/当主/初始城恢复为存档状态。
- **验收**：`import 0 → export 4 → import 4` 状态哈希一致（`0xb16846bc`）；
  跨剧本 `slot2 → export 0 → import 0` 同样一致（`0x61a3d6ef`）；
  槽头家族名修正为**当主家族**（源/平/藤原）而非势力名。

**第二部分：裸 4 位平面（4‑plane）精灵解码**
- **格式**：非 NPK016 容器的裸图，每条扫描线按
  `[plane0][plane1][plane2][plane3]` 四段顺序存储，
  每段 `ceil(w/8)` 字节（1 bit/像素，bit 7 为最左像素），
  像素色号 = `p0 | p1<<1 | p2<<2 | p3<<3`（与 `decompress.c` 的 NPK 平面
  转换完全一致，因此共用同一套 PC‑98 16 色调色板）。
  据此：
  - `Hunitpat.gp` 151,936 B = **1187 × 128 B** → **1187 块 16×16** 单位图案；
  - `Hchikei.gp` 10,060 B = **76 B + 78 × 128 B** → 78 块 16×16
    （同长度亦可解读为 100×100 图块 id 网格 + 60 B 布阵表，二者都成立）；
  - `Mainobj.gp` 24,544 B = **104 B/行 × 236 行** → **208×236** 图集
    （每行 26 B/平面 × 4 平面 = 104 B）。
- **引擎实现**：`include/raw_sprite.h` / `src/raw_sprite.c`
  （`raw_sprite_decode_4plane()` / `raw_sprite_decode_tile_bank()` /
  `raw_sprite_load_tile_bank()`）。
- **合战集成**：`src/kassen.c` 载入 `Hunitpat.gp` 图案库，
  战场上按当前兵力绘制双方队形（每 50 兵 1 个 16×16 精灵，
  每侧最多 6×4 = 24 个；图案按当主 id 确定性选取）。
- **工具**：`tools/decode_raw_sprites.py` 输出尺寸报告与预览图
  （`Hunitpat_sheet.png` / `Hchikei_grid.png` / `Mainobj_sheet.png`）；
  C 与 Python 解码结果**逐像素一致**（已交叉验证）。
- **CLI**：`--preview-raw <file> [tile]` 独立预览裸 4 位平面图。
- **回归**：`tools/run_regression.sh` 增至 **36 项**（新增 DOS 存档往返 +
  3 项裸图解码检查），全部 PASS；`-Wall -Wextra` 零警告。

补充（非本期）：**音频**（先逆向 `Fmdrv.com` + `Music.gp` 的 FM 音源格式）仍属后续方向。
