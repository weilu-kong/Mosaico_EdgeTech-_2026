# 11 · 日语离线语音操作（Phase 3e，0.8.0）——**全部未执行**

> 状态：**已编码；语法检查（含 esp-sr 2.5.3 真实头文件）+ 主机测试 + 模拟器截图**。**没有用真实 ESP-IDF 编译或链接，没有在板子上运行过，没有任何识别率数据。** 设备页/AI 页只会显示引擎**真正报告**的状态；不得向观众宣称设备能识别日语，直到板上验证完成。
> RF：ESP-SR 是纯离线（片上）语音栈，不含网络/无线代码；本功能没有启用、请求或执行任何无线功能。状态仍是 **RF Verification Pending**。

## 1. 做法（照用户的 Full Demo 项目 `korvo1_yokai_demo/main/voice_service.c`）

| 项目 | Full Demo | 本固件 |
|---|---|---|
| 语音栈 | ESP-SR 2.5.3：AFE + WakeNet + MultiNet7（英语量化模型） | 同（`espressif/esp-sr ==2.5.3`、`esp-dl ==3.3.11`、`dl_fft ==0.7.0` 与其依赖锁一致） |
| 日语命令词 | MultiNet 只有英语模型，所以日语指令用**音译的英文单词**登记（例：ホーム→`HOH MOO`），命令 id = 100 + 命令号 | 同；命令表在 `firmware/voice_components/exp_voice/include/dev_voice_core.h`（`DEV_VOICE_PHRASES`），英语短语同时登记（id = 命令号） |
| 阈值 | MultiNet 0.23（创建）→0.26（登记后）；VAD_MODE_3，最短语音 80 ms，最短静音 300 ms；MultiNet 超时 5 s | 同 |
| 麦克风增益 | 34 dB | 语音构建 34 dB（`EXP_MIC_GAIN_DB`），普通构建仍 30 dB |
| 唤醒词 | 官方 Full Demo 实际只加载日语唤醒词 `wn9l_ja_konnichihaesp_tts3`（“こんにちは ESP”） | **一次只加载一个**，默认就是这个日语唤醒词（`dev_voice.c` 的 `EXP_VOICE_WAKE`：0=日语[默认]，1=“Hi, ESP”，2=两个同时）。**同时加载两个已在板子上崩溃**（见 §4.2），所以“保留两个”做不到；要换成 “Hi, ESP” 改 `EXP_VOICE_WAKE` 为 1 重新编译（模型分区里两个模型都在，不用重烧模型）。日语唤醒词在 S31 上的识别率和误唤醒率：**未验证** |
| 回声消除/参考通道 | AFE "MR" + AEC + ASRC（44.1 kHz 播放参考） | AFE "M"（单麦、无 AEC）。本展示只播放很短的提示音：提示音播放期间及之后 250 ms，给麦克风喂**静音**（`dev_vhold_*`），避免设备被自己的提示音唤醒 |
| 采样率 | 44.1 kHz → ASRC 到 16 kHz | 官方 BSP 编解码器是 48 kHz 立体声 → **63 阶低通 + 3:1 抽取**到 16 kHz（`dev_vdec_*`；主机测试：通带平坦，≥9.8 kHz 衰减约 −59 dB；电平表仍用原来的 3 点平均，没动） |
| 蓝牙/Wi-Fi/A2DP | Full Demo 的 sdkconfig 里有 | **一个都没有复制**；`sdkconfig.defaults` 的 RF 关闭项照旧，语音构建只追加 `sdkconfig.defaults.voice`（模型/分区/CPU 频率） |

支持的指令（都在“ウェイクワード → 指令”之后说）：**ホーム / モーション / オーディオ（音声・AI）/ ゲーム（迷路）/ デバイス**，英语 HOME / MOTION / AUDIO / GAME / DEVICE 也可。例如：先说“Hi ESP”（或“こんにちは ESP”），再说“ゲーム”。游戏进行中语音不会切页（AI 页显示“ゲーム中は無効”）。唤醒词会像第一次触摸一样结束自动演示并回到首页。

## 2. 流程

`mic_task`（dev_audio.c，48 kHz 立体声）→ `dev_voice_feed_stereo()`：选 I2S 声道（有迟滞，8 个块内持续明显更响才切换）→ 低通+抽取 → 16 kHz 环形缓冲（PSRAM，约 1 s）→ feed 任务 → AFE → fetch 任务：WakeNet 命中 → 打开指令窗口（≤5 s，另有 7 s 保险）→ MultiNet 返回命令 id → `dev_vstate_t`（序号+事件）→ `fill_sensors()` → 应用核心 `voice_update()`。全部在核心 1（UI 在核心 0）。**不录音、不保存、不发送**；日志里会打印 MultiNet 的“heard”音素串，用来调音译拼写。

状态机（`dev_voice_core.h`，已主机测试）：未就绪前的唤醒/命令一律忽略；`READY` 只在模型与命令表真正加载成功后设置；加载失败给出原因（模型分区缺失、没有唤醒词模型、AFE/MultiNet 创建失败〔内存〕、命令表全部被拒……）；个别短语被 MultiNet 拒绝时继续运行并在 I/O 页显示“N of M phrases rejected”。崩溃保护：模型加载及“就绪后 15 秒”算一个步骤（`DEV_STEP_VOICE`）；若这段时间内发生复位，语音在下次**上电**前保持关闭，不会造成重启循环。

## 3. 构建（独立目录、独立 sdkconfig，不动现有构建）

```
cd firmware
idf.py -B build_voice -DEXP_VOICE=1 build                  # 开发镜像：版本串 0.8.0-stage3e-voice-dev
idf.py -B build_voice -DEXP_VOICE=1 -DEXP_RELEASE=1 build  # 展台镜像：…-voice-rel（再跑 release_check.py）
```

* 语音构建使用 `build_voice/sdkconfig` 与 `firmware/dependencies.voice.lock`（首次构建由组件管理器生成），**不会改** `firmware/sdkconfig`、`dependencies.lock`、`build/`。
* 分区表 `firmware/partitions_voice.csv`：`nvs 0x9000/24K`、`phy_init 0xf000/4K`、`factory 0x10000/5 MB`、`model 0x510000/4 MB`（label 必须是 `model`）。**app 仍在 0x10000**。模型约 3.4 MB（MultiNet7 英语 ≈2.8 MB + 两个 WakeNet9 ≈0.6 MB），4 MB 留有余量。
* 构建会生成 `build_voice/srmodels/srmodels.bin`（esp-sr 的 CMake 负责）。
* 首次构建可能要处理的点（**未验证**）：(a) esp-dl 在新版 GCC 下的 `-Wnon-c-typedef-for-linkage` 报错——顶层 CMake 已对 esp-dl 加 `-Wno-error=…`（编译器认识时）；(b) `sdkconfig.defaults.voice` 里的 `CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_320=y` 来自 Full Demo，若屏幕出现异常先去掉这一行；(c) 构建后用 `scripts/rf_map_check.py` 检查链接表（语音镜像也要）。

## 4. 烧录——会改分区表并写入新分区

> **用户已于 2026-10-09 书面同意**：改分区表并写入模型分区；保留两个唤醒词（Hi ESP + こんにちは ESP）；接受语音构建的 CPU 320 MHz 设置。**2026-10-09 已按下述方式烧录一次（见 §4.1 的实测记录）。**用 `scripts/voice_flash.sh`（先 `check`，只读；再 `write`，需要 `VOICE_FLASH_CONFIRM=yes`，固定端口、核对 MAC、拒绝 `11201`、不整片擦除）。该脚本 2026-10-09 在真机上跑通过 `check` 和 `write`（修过四处脚本问题，见 §4.1）。

影响说明：
1. 在 0x8000 写入新的分区表（现有表是 single-app-large：factory 0x10000 起约 1.7 MB 之后空）。新表的 `nvs` 位置/大小与旧表相同（0x9000/24K），NVS 内容保留；
2. 在 0x510000 起写入约 3.4 MB 模型（目前该区域是空闲/未使用的）；
3. 在 0x10000 写入语音版应用（比普通版大，旧表放不下，所以必须先/同时更新分区表）；
4. **不整片擦除**，不动 bootloader（0x2000）、eFuse、安全配置。回退：把普通构建的 `partition-table.bin` 写回 0x8000 + 普通 app 写回 0x10000 即可（模型分区留着无害）。

建议顺序（始终固定端口，先核对芯片型号与 MAC `1c:29:04:d0:a3:36`；**/dev/cu.usbmodem11201 是另一个项目的 ESP32-S3，绝不能写**）：

```
# ① 只读：读出板上当前分区表并解码（不写任何东西）
esptool --chip esp32s31 -p <端口> --no-stub read-flash 0x8000 0x1000 /tmp/pt_now.bin
python3 $IDF_PATH/components/partition_table/gen_esp32part.py /tmp/pt_now.bin
# ② （同意后）一次写入：分区表 + 应用 + 模型
esptool --chip esp32s31 -p <端口> --no-stub write-flash --flash-mode dio --flash-size 16MB --flash-freq 80m \
   0x8000 build_voice/partition_table/partition-table.bin \
   0x10000 build_voice/mosaico_experience.bin \
   0x510000 build_voice/srmodels/srmodels.bin
```
（以 `build_voice/flash_args` 为准；`idf.py -B build_voice -p <端口> flash` 也会写同样三项加 bootloader。）**写完要按板上的 Reset 键**（板子有电池，拔 USB 不等于断电；脚本现在最后会自动硬复位，仍不亮就按 Reset）。

### 4.1 2026-10-09 真机烧录记录（设备已验证的只有“烧录与启动”，语音本身没有跑通）

* `check`：MAC 1c:29:04:d0:a3:36，旧分区表 nvs/phy_init/factory(1500K)。`write`：分区表、应用 3,715,616 B、模型 3,344,836 B 三段均 `Hash of data verified`。另外把语音版 bootloader 写到 0x2000（与板上旧 bootloader 内容不同，已补写）。
* 脚本问题（均已修复）：① esptool 默认读完就硬复位，板子离开下载模式、端口消失；② 两次 esptool 调用之间端口会掉，读 MAC 和读分区表必须在**同一次连接**；③ esptool 5.5 不支持把 `read-mac read-flash` 写在一条命令里；④ `strings | grep -q` 在 `pipefail` 下因 SIGPIPE 误判。
* **烧录后屏幕不亮不是故障**：脚本曾用 `--after no-reset`，板子停在下载模式；板子有电池，拔 USB 不会断电，需要按 Reset 键。
* **语音版在 Reset 后会 PANIC**：日志画面标题 `reset: PANIC`、`prev stage 'loop'`（两次拍照都是）；崩溃保护随后把语音关掉：I/O 页 `voice skipped (crash guard)`，屏幕日志 `W dev_voice: voice init skipped: it was running when the previous boot ended abnormally`。**原因未知**，应用运行时没有串口（B21）。AI 页因此显示红色 `エラー`。其余功能（麦克风电平/频谱、帧耗时 40 ms）正常。
* 诊断：补丁给分区表加了 512 KB `coredump` 分区（0x910000）并打开 `CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH`，崩溃现场存进 flash；板子进下载模式后 `scripts/voice_flash.sh coredump <端口>` 只读取出并用 `esp-coredump` 解码。**这要改分区表（新增 512 KB 区，其余分区位置和大小不变），需用户同意后才烧；同意前不烧。**


### 4.2 崩溃原因（coredump 解码，2026-10-09，设备实测）

* 崩溃任务 `voice_fetch`，原因：`assert failed: model_detect_mfcc wakenet9_quantized.c:1008 (cq->n == model->layers[0]->n)`；调用链 `fetch_task (dev_voice.c) → afe_fetch_with_delay → afe_wn_run_single_mic → model_detect_mfcc`。
* 解读（**推断，未用第二块板子或官方示例复现**）：两个 WakeNet 模型共用一份 MFCC 特征，`wn9_hiesp` 与 `wn9l_ja_konnichihaesp_tts3` 的第一层输入大小不同，所以第二个模型检测时断言失败。其他任务栈都有余量（voice_fetch 用了 784/5984 字节，mic 1056/4092），所以不是栈溢出。
* 处理：默认只加载日语唤醒词（与 Full Demo 一致）；`EXP_VOICE_WAKE=2` 仅用来复现。**改后在板上是否正常：未验证。**
* 板子有电池：拔 USB 不是断电；烧录脚本现在结尾自动硬复位，仍不亮就按 Reset。崩溃保护只在上电复位（含 Reset 键）后放行语音。

### 4.3 改成单唤醒词后的第一次板上试用（2026-10-10，用户人工试用，未量化）

* 启动：`reset: power-on`，I/O 页 `voice ready  wake2 no  load 871 ms`，AI 页“音声操作：待機中”；没有再 PANIC。
* “こんにちは ESP”能唤醒（用户确认“有”），AI 页变“聞いています”并有提示音。
* 指令：**ホーム、ゲーム 识别得不错；デバイス 识别不了**。モーション / オーディオ 本次未报告（**未验证**）。
* 计数器（用户拍的 I/O 页，一次会话累计）：`wake 10  cmd 4  none 0  tmo 4  last 1/27%  fetch 4945  drop 0`；日志 `[HEARD] id=1 prob=0.275 matched='hbM'`（id=1 是英文 HOME；概率只比阈值 0.26 高一点）。10 次唤醒里 4 次得到指令、4 次超时：超时有多少是“没说/说了没识别/误唤醒”，**没有区分**。
* 因此：日语唤醒词和 ホーム/ゲーム 在板上**能用**；整体识别率、误唤醒率、デバイス、モーション、オーディオ、展台噪声下表现：**未验证**。
* 0.8.1 的改动：给 デバイス 增加 5 个音译拼写（`DEH BAH EE SOO` / `DEE BAH EE SOO` / `DAY BYE SOO` / `DEV EYE SOO` / `DEH BUY SOO`），是否改善：**未验证**。
* 下一轮试用建议（用同一个 I/O 页计数器）：每个指令各说 5 次（唤醒词 → 指令），记录每次是否成功，I/O 页 `wake/cmd/tmo` 增量与 `[HEARD]` 行（`id`、`prob`）；`id` 为 100+N 表示按日语拼写命中，1–5 表示按英文词命中。

### 4.4 0.8.1 板上试用（2026-10-10，用户只发了 I/O 页照片，没有文字说明）

* 启动：`reset: power-on`，`fw 0.8.1-stage3e-voice-dev`，`voice ready  wake2 no  load 913 ms`；没有 PANIC。
* 计数器：`wake 8  cmd 8  none 0  tmo 0  last 5/35%  fetch 4987  drop 0`；日志 `[HEARD] id=105 prob=0.347 matched='Dd Bi So'`。**id=105 = 日语拼写命中的 デバイス**（100+5），所以 デバイス 现在能识别（上一版 10 次唤醒里 4 次超时）。
* 不能下结论的部分：照片没说明这 8 次各说了哪个指令、是否有误唤醒；命中的是哪一个拼写变体（`matched` 只显示音素 `Dd Bi So`）；モーション / オーディオ 是否可用；展台噪声下表现。以上**均未验证**。8/8 只是一次人工试用，不是识别率。

## 5. 板上验证清单（**全部未执行**）

1. 启动后 I/O 页最后两行：`voice loading` → `voice ready … wake2 yes/no … load N ms`；失败则看原因。AI 页“音声操作”：読込中 → 待機中。
2. 说“こんにちは ESP”（默认唤醒词）：AI 页变“聞いています”，有轻响+短振；说“ホーム/ゲーム/…”：切换页面，AI 页显示指令名与百分比。记下 I/O 页的 `heard` 行与串口 `[HEARD]` 日志，用来调 `DEV_VOICE_PHRASES` 的音译拼写（这是最可能需要迭代的部分）。
3. 唤醒词是否稳定触发（想换成 “Hi, ESP”：`EXP_VOICE_WAKE` 改 1 后重新编译，只需 `update`）；误唤醒（环境噪声、设备自己的提示音）次数；展台噪声下的识别率。
4. 麦克风声道（I/O 页 `slot L/R`）与电平：确认语音路径用的是有信号的那个声道。
5. **帧率**：语音引擎运行时，动作页帧时间是否恶化（此前 44–45 ms）；若明显变差，先降低 AFE 优先级/改用 `AFE_MODE_LOW_COST`（需要改源码并说明）。
6. 内存：I/O 页/日志里的 internal/PSRAM 剩余；`ring_dropped` 应保持 0。
7. 崩溃保护：人为制造一次加载期复位，确认语音保持关闭且设备正常启动。
