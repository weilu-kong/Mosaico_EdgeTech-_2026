# 07 构建与烧录

> **更新（Phase 3a）：Phase 0–2 没有构建固件、也没有烧录。用户批准 Phase 3 并选择路线 B 后，已构建并烧录开发固件；以 §5、§6 的实际记录为准。§3、§4 是当初的草案（路线 A / `mosaico.py`），保留供对比，对路线 B 不适用。**

## 1. Host 预览（已实现、云端已验证）

依赖：Python ≥ 3.10、Pillow、C 编译器（gcc 或 clang）、`raylib-lite-engine`（官方）。

```bash
# 1) 取得官方工作区（固定提交）。默认 dry-run，只打印将执行的命令
./scripts/bootstrap_workspace.sh            # 预览
./scripts/bootstrap_workspace.sh --apply    # 真正克隆到 ./workspace（不碰现有 ESP-IDF）

# 2) 指向引擎（bootstrap 之后通常自动找到）
export MOSAICO_ENGINE_ROOT=<path>/raylib-lite-engine

# 3) 全部 Host 检查（字体可复现、gcc/clang -Werror、原生单元测试、RF 静态扫描、
#    行为回归、22 屏截图 + 布局检查）
./scripts/run_all_tests.sh

# 4) 交互预览（仅监听 127.0.0.1）
./scripts/run_preview.sh                    # 浏览器打开终端打印的地址
```

状态：云端 Linux 容器 **模拟器已验证**。在用户 Mac 上运行：**未执行**（Mac 工具链未审计，见 01）。`bootstrap_workspace.sh --apply` 也**尚未运行**。

## 2. 环境前置（Mac）

1. 运行只读脚本 `scripts/mac_env_audit.sh`，把输出交回。
2. 预览只需要 Python 3、Pillow 和 C 编译器（Xcode Command Line Tools 或其他）。是否需要安装任何东西取决于审计结果，**不会未经同意安装**。

## 3. 固件构建草案（Phase 3，未执行）

1. 在**新的独立路径**取得 ESP-IDF 提交 `7b9cc1ac79f865983f59bb8ff3ff43eb74ff1dbe`（可用 `git worktree` 从 `~/esp/esp-idf-master` 对象库建立，或新克隆）；用 `git -C <path> rev-parse HEAD` 核对。**不改动**现有四个 ESP-IDF 目录。工作区 CLAUDE.md 声明本仓库已授权安装该固定版本，但依照本项目约束，仍在 Phase 3 开始前再次向你确认。
2. 目标 `esp32s31`，Python ≥ 3.10。
3. 路线 A（官方工作区）：`python mosaico.py project init <name>`（官方 Hello World 模板）作为应用外壳，把 `main/exp_*.c` 作为应用组件，加入设备适配层；设置 `CONFIG_ESP_IRIS_OTA_DEFAULT_VIA_RECOVERY=y`，使用 utils 的 `esp_mosaico_app_recovery` 与 `iris_ota_support_start()`，并按官方文档保持分区表前缀。
   路线 B（仅 BSP）：见 02 §5，需用户先选择。
4. `sdkconfig` 显式关闭 Wi-Fi/BT/802.15.4/COEX 等，构建后运行 `scripts/rf_check.py` 对生成的 `sdkconfig`（以及链接结果）检查。
5. 编译、记录固件大小；此时才能给出真实的 Flash/PSRAM 占用。

## 4. 烧录草案（路线 A，已被路线 B 取代，未执行；原规则：**仅经 `python mosaico.py`**）

规则：不得直接使用 `idf.py flash`、`esptool` 写入或 ESP-Iris 写入命令；不得猜测 flash 偏移；不得整片擦除；不得更改 eFuse 或安全配置。

```bash
python mosaico.py iris status --all --json
python mosaico.py iris list --details --json
# 新装 / 状态未知：python mosaico.py recover   （影响：写入基础固件；执行前先说明并获同意）
# 新应用/布局变更：python mosaico.py iris system-update --project <proj>
# 仅代码更新且分区表完全一致：python mosaico.py iris app-update --project <proj>
```

* 烧录前后用 `iris device-status`、`operation-status`、`screenshot` 取证，并检查 Device ID 一致、重启后 Boot ID 变化、正常 → Vibe Mode → 正常 的循环。
* **不会在你批准 UI 预览之前烧录最终 Demo。**
* 任何分区表改动或会影响数据的操作，先说明影响并获得同意。

## 5. Phase 3a 实际构建记录（已编译，未烧录）

* IDF：`~/esp/esp-idf-mosaico-7b9cc1a`（`git clone` 自本地 `esp-idf-master`，检出 `7b9cc1ac…`；子模块须先把 `origin` 改为 `https://github.com/espressif/esp-idf.git`，因为 `.gitmodules` 使用相对地址）。`./install.sh esp32s31` 成功，IDF 报告 6.2.0；系统 Python 3.14.5 可用。
* 命令：`. ~/esp/esp-idf-mosaico-7b9cc1a/export.sh && cd firmware && idf.py set-target esp32s31 && idf.py build`。
* 结果：构建成功，`mosaico_experience.bin` 0x8c5b0 字节，最小 app 分区 0x177000，剩余 63%。
* 构建中修过：`file(GLOB … CONFIGURE_DEPENDS)` 在 IDF 脚本模式无效；BSP 的公开头文件需要 `espressif__esp_lcd_touch`（BSP 自己没列入依赖）；`esp_lcd_touch_get_coordinates` 已弃用，改用 `esp_lcd_touch_get_data`。
* 烧录：见 §6（已执行，经用户明确同意）。

## 6. Phase 3a 实际烧录记录（路线 B）

**前提与同意**：用户选择路线 B，允许直接用 `idf.py`/`esptool`。烧录前我说明了会覆盖板上的官方 Recovery；用户回复“不需要备份，直接替换”，随后又明确要求“先擦除再烧录”。**没有备份**。不改 eFuse，不改安全配置。

| 步骤 | 结果 |
|---|---|
| 识别目标板 | 两块板同时插着：`11201` 是 ESP32-S3（别的项目，**未碰**），`11401` 是 ESP32-S31，MAC `1c:29:04:d0:a3:36`。**以芯片型号与 MAC 确认，不凭端口号** |
| 只读读取烧录前内容 | 分区表在 0x9000（官方 Mosaico 布局，见 01 §7.3） |
| 整片擦除 | `esptool --chip esp32s31 … --before no-reset --after no-reset erase-flash`（**不带 `--no-stub`**）：Flash memory erased successfully，24.8 s。带 `--no-stub` 会报 “ROM does not support function erase_flash” |
| 写入 | `write-flash --flash-mode dio --flash-size 16MB --flash-freq 80m 0x2000 bootloader/bootloader.bin 0x8000 partition_table/partition-table.bin 0x10000 mosaico_experience.bin`（`--no-stub`，`-b 460800/921600`） |
| 后续迭代 | 只重写应用：`write-flash … 0x10000 mosaico_experience.bin` |

**踩到的坑（均已解决）**

* **端口会消失**：该板在 ROM 下载模式下是 USB-OTG，`idf.py flash`/`erase-flash` 的默认 `--before default-reset` 会复位板子并重新枚举，端口随之消失（“could not open port”）。解决：直接用 `python -m esptool … --before no-reset`，并在刚 `ls` 确认端口后马上运行。
* 不要把两个 esptool 子命令写在一行（选项会被前一个子命令解析而报错）；擦除和写入分两次连接。
* `idf.py` 必须在 `firmware/` 目录运行，不是 `firmware/build`。
* 进入下载模式：拔 USB，按住 Boot 插线，再松开。
* 应用运行时 Mac 上看不到串口，`idf.py monitor` 拿不到日志（原因未知，见 10 的 B21）。所以开发固件在屏幕上显示帧耗时叠加层。

**恢复路径（未验证）**：官方预编译 Recovery 位于 `esp-mosaico-utils/prebuilt/recovery`（云端读过 manifest）。要恢复需另行评估，**这次没有做**。ROM 下载模式（Boot 键）不受我们固件影响，所以不会变砖。

## 7. 日常迭代流程与屏幕日志（0.3.4 起）

Mac 上的 zsh 函数 `mfw`（定义一次，之后每次 `mfw`）：在 `firmware/` 里 `idf.py build`，确认出现 “Project build complete”，提示进入下载模式（拔 USB，按住 Boot 插线，松开），在 `/dev/cu.usbmodem*`（排除另一块项目的 ESP32-S3 `11201`）里找端口，用 `esptool --chip esp32s31 … write-flash 0x10000 mosaico_experience.bin`，以 “Hash of data verified” 判定成功，最多重试 6 次。**每次仍须核对芯片型号/MAC；只写应用，不动分区表。**烧录后拔 USB（不要按 Boot）重新插上。zsh 里 `#` 在交互模式下不是注释，所以用函数而不是粘贴多行命令。

**读日志**：应用运行时 Mac 上没有串口（见 10 B21），日志在屏幕上看。三连点任意角落（2 秒内）打开日志画面，顶部有版本、启动次数/复位原因、IMU 状态、芯片坐标系的加速度/角速度、陀螺仪积分角、电量计行；点上半屏向上翻、下半屏向下翻；再三连点关闭。

**陀螺仪方向检查**：平放，点传感器行清零，逆时针转 90°，z 应读约 +90；清零后把顶边抬到竖直，x 应读约 +90。

## 8. 磁力计检查（0.5.0 起，日志画面）与罗盘验证（0.5.2/0.5.3）

日志画面（任意角落 2 秒内连点三次）有四行：`mag0 ok  x y z  |B| NN uT  nA eB`、`mag1 …`，以及 0.5.3 起的 `cal …`（`cal ok  off x y z  r R  rms E`：校准偏移、拟合半径、相对残差；未完成时 `cal running/idle  rej N`，N 是被拒绝的拟合次数），以及 0.5.4 起的 `B x y z  mag-head H  fused Y`（校准后磁场分量 µT、只用磁力计+重力算的顶边航向、应用显示的 yaw）。x y z 是 µT（1 LSB = 1 µT），A 成功读数，B 错误数。mag0 约 530 µT、mag1 约 1.6 mT 是板载磁铁造成的，不是故障（见 `docs/10` B38）；mag1 会间歇 overflow，也是正常现象。

**罗盘验证（0.5.2 新增，0.5.3 校准算法更换；需要用户操作；只用 mag0）**
1. 进入动作页（モーション），点方位（罗盘）区域校准（“タップ校正”）。把板子在空中慢慢转遍各个朝向（正反面、四条边朝上、水平转圈），进度条走满，显示“校正済み”。0.5.3 校准成功的标志是日志画面 `cal ok` 且半径 r 约 35–60 µT、rms ≤0.1；若多次 `rej` 增加，说明转动范围不够或附近有磁性物体，请远离金属/手机，转更多姿态。校准完成后请拍一张日志画面（要能看到 mag0 与 cal 行）。校准时请不要拿着手机、钥匙等磁性物体靠近。
2. 用手机的指南针找磁北（记下手机显示的是“磁北”还是“真北”；东京的磁偏角约西偏 7°，所以真北与磁北差约 7°）。
3. （0.5.5 优先做这个，不需要手机）**旋转一致性检查**：手离开手机、钥匙、笔记本、扬声器和铁桌（≥1 米，最好放在木桌上）；校准完成后板子平放、屏幕朝上，打开日志画面，点顶部的传感器行把 `turned` 清零（同时重启 sweep），然后在桌面上**缓慢转一整圈**（约 20–30 秒，不要翘起），拍一张日志画面，要能看到 `turned`、`B … mag-head …`、`sweep …` 与下面一行 12 个数。再做一次：拿掉旋钮模块重新校准，同样转一圈拍一张。另做一个 10 秒小实验：板子平放不动，把手机慢慢靠近到 20 厘米，看 `B` 的三个数是否变化超过 5 µT，拍前后各一张。
4. 然后（需要手机，手机放远处≥1 米，用桌边或一根直尺把北方向画到桌上，再把手机拿走）板子平放、屏幕朝上，顶边依次指向北、东、南、西，各拍一张**日志画面**（0.5.4：要能看到 mag0、cal、B 三行；平放不动 3 秒再拍）和一张动作页照片（要能看到罗盘角度与指针）。再拍一张板子竖起（顶边朝上，屏幕朝向你）、一张倾斜约 30°。
5. 期望：顶边指北约 0°（±15°）、东约 90°、南约 180°、西约 270°，指针方向与手机一致。如果相差一个固定角度或方向相反，照片会让我直接算出应该改的映射。
6. 装上旋钮模块后，需要重新校准一次，再拍北向一张，用来记录模块的影响。

## 9. 0.6.0（Phase 3c）设备检查清单——**全部未执行**

补丁 31 之后第一次编译可能出现真实 IDF 才会暴露的问题（本固件只用伪头文件做过语法检查）；先 `idf.py build`，把错误贴给我。烧录用同样的 `mfw`（固定端口、核对芯片与 MAC）。

1. 日志画面（角落连点三次）→ 点**顶部两行标题**切换到 I/O 页（再点回传感器页）。I/O 页应显示：`mic ok`（reads 增加）、`peak L … R …`（对着板子说话/拍手，看哪一个声道变化，`slot` 是固件选的声道）、`spk …`、`motor …`、`slots ok`、左右槽状态、`rf probe: 0 of 7 …`。
2. 在 I/O 页点标题下面的行：应响一声 1 kHz 测试音，并有一次约 60 ms 的振动。请记录：有没有爆音/底噪（功放的嘶声）、音量是否过大、屏幕/触摸是否被打断。
3. 回到应用：点按钮应有轻提示音和振动；AI & Audio 页的波形应随声音变化（有 `EXP_SENS_MIC` 时）。
4. 插上/拔下一个扩展模块（断电后操作），看左右槽是否显示正确（モジュール名 / なし / 不明なモジュール）。**未知模块要显示“不明なモジュール”。**
5. 磁力计：电机振动与扬声器会干扰指南针，之后应分别在“静止”与“振动中”各看一次 `B` 三个数的变化。
6. 启动崩溃保护：若某一步在上次启动中崩溃/看门狗复位，下次启动会跳过该步并在 I/O 页显示 `skipped (crash guard)`；**断电再上电**才会重新尝试。
7. 若任何一项出现重启循环：把板子断电，改 `EXP_USE_AUDIO=0`（或 `EXP_USE_MOTOR=0`、`EXP_USE_SLOTS=0`）后重新编译，并把 I/O 页或串口日志发给我。

## 11. 0.8.0（Phase 3e）日语离线语音——**全部未执行**

见 `docs/11_VOICE_SETUP.md`（构建命令、分区表、**需要同意的烧录步骤**、板上验证清单）。普通构建 `idf.py build` 不变（版本串 `-novoice`）；语音构建 `idf.py -B build_voice -DEXP_VOICE=1 build`。

## 10. 0.7.0（Phase 3d）与展台镜像——**全部未执行**

**展台（release）镜像**：`idf.py -DEXP_RELEASE=1 build`（改变这个选项需要重新配置；建议先 `idf.py fullclean`，**不会**擦除板上的 flash）。构建后必须运行 `python3 scripts/release_check.py firmware/build/mosaico_experience.bin` 得到 PASS，再按 `scripts/rf_map_check.py` 检查链接表。开发镜像（版本串 `-dev`）不得放上展台。release 镜像没有日志画面，I/O 页、扫描诊断都不可用——所以任何要拍照的测试都用开发镜像。

**0.7.0 新增的设备检查（开发镜像）**
1. 摇杆模块（断电时插入左或右槽，再上电）：I/O 页 `joy …` 行应经 `claiming` → `calibrating: circle`（设备页显示“スティックを回す”，用摇杆画圆约 8 s）→ `calibrating: centre`（松手）→ `ready`；拍 I/O 页：`joy raw … drv … app …` 一行用来决定轴映射。在游戏页选择“ジョイスティック”输入，推右时球应向右，推上时球应向上；若相反或交换，用 `EXP_JOY_INV_X/INV_Y/SWAP_XY` 重新编译。拔掉模块：应显示 `removed`，约 2 秒后回到 `no module`。
2. 烧屏保护：10 分钟不触摸后亮度降到 50%，任何触摸立即恢复。请观察：调光/恢复时画面有没有闪、撕裂或卡顿（I/O 页没有额外行；串口日志有 `brightness -> …`）。想验证得快一点可用 `-DEXP_DIM_AFTER_S=20` 临时编译。
3. 看门狗：正常运行不应复位；日志画面标题里的 `reset:` 若出现 `TASK_WDT`，说明绘制循环或 IMU 任务卡住超过 10 s，请把当时的 I/O 页与串口日志发给我。
4. 磁场判断：校准之后，把一块磁铁/手机的背面靠近板子：罗盘应显示“磁気の乱れ”，Yaw 变为“相対”；拿开后应在几秒内恢复“校正済み”。

