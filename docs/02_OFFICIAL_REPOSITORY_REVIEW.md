# 02 官方仓库审阅

所有上游仓库**只读**，未做任何修改。证据路径相对于云端检出的 `esp-mosaico-vibe/submodule/`。

## 1. 仓库角色

| 仓库 | 角色 | 与本 Demo 的关系 |
|---|---|---|
| esp-mosaico-vibe | 工作区：把 BSP、utils、引擎固定为子模块，固定 ESP-IDF 版本 | Route A 的外壳 |
| esp-mosaico-bsp | 板级支持包：显示（CO5300）、触摸、IMU/磁力计、音频、扩展槽、电源 | 设备端必须依赖 |
| esp-mosaico-utils | `mosaico.py` CLI、ESP-Iris、Recovery/Vibe Mode 固件 | 唯一合规的设备操作入口 |
| raylib-lite-engine | Raylib Lite 软渲染引擎 + Host 模拟器（`host/run_game.py`、`GenericHostRuntime`），游戏模块 ABI v1，`mosaico-game-sim/v1` manifest | Phase 2 预览直接使用 |

## 2. 仓库规则（AGENTS.md）与本项目的对应

| 规则 | 本项目做法 |
|---|---|
| 设备操作只经 `python mosaico.py` | Phase 0–2 未接触设备。**Phase 3a：用户选择路线 B（不需要 Vibe Mode），明确允许直接用 `idf.py`/`esptool` 烧录**；仍不得整片擦除之外未经同意的破坏、不得改 eFuse（整片擦除是用户在本次会话中明确要求的，见 07、10） |
| 不得改上游源码 | 全部代码在 `app/mosaico_experience/`，只通过引擎 ABI 接入 |
| Vibe Mode 下 OTA 写入只在 Vibe Mode | 见 §4 冲突 |

## 3. 重要发现

### 3.1 触摸 IC 文档不一致（文档问题，待设备确认）

* BSP README 写的是 **CST9217**：`esp-mosaico-bsp/README.md:30`
* BSP 实际代码使用 **CST9220** 的配置宏：`components/esp-mosaico-bsp/onboard/display.c:347`
  `ESP_LCD_TOUCH_IO_I2C_CST9220_CONFIG()`
* `include/bsp/display.h:57` 的注释又提到 CST9217 触摸玻璃。

结论：触摸 IC 型号**未能在不接设备的情况下确定**；Demo 不依赖具体型号，只用 BSP 提供的触摸接口。设备页的“触摸”行在设备端实现前显示“--”。状态：未执行（设备确认）。

### 3.2 Vibe Mode / Recovery 固件在启动时启动 Wi-Fi 并扫描（RF 重大冲突）

调用链（云端已读源码）：

* `esp-mosaico-recovery/firmware/recovery/main/main.c:86` — `factory_network_start()` 在启动流程里被无条件调用
* `.../main/factory_network.c:380` — `esp_wifi_init()`
* `.../main/factory_network.c:391` — `esp_wifi_start()`
* `.../main/factory_network.c:446` — `esp_wifi_scan_start(NULL, false)`

这与“展示固件、启动流程及扩展设备不得启用/请求/执行任何无线功能”冲突。这里的 Recovery 是官方固件、位于出厂分区，**不是**本 Demo 的代码；本 Demo 不会也不应修改它。是否、如何处理，属于 Phase 3 决定，见 06 与 10。

### 3.3 Recovery 契约

* OTA 写入器只在 Vibe Mode 中存在；`iris system-update` 与 `app-update` 是不同通道。
* 应用不得破坏 Recovery 分区；本 Demo 的任何分区表改动都必须事先说明影响并取得同意。

## 4. 为什么 Phase 2 使用“自带渲染器 + 引擎表面”而不是 GSP

* GSP（官方图形/UI 组件服务）的下载源 `dl.espressif.com` 在云端不可达，**无法验证**其能力与许可。
* 日文 480×480 UI 需要可控的中日文字形渲染、布局检查与重叠检测；因此 Demo 自带一个小型文本/形状层（`exp_text`/`exp_gfx`），字形来自 Noto Sans CJK JP 子集（SIL OFL 1.1）。
* 只依赖引擎的 RGB565 表面与 `BeginDrawing/EndDrawing`，因此同一份 C 核心可以在 Host 和设备上复用。

## 5. 路线对比（**已决定：路线 B**，见 5.1）

| | Route A：Vibe 工作区 | Route B：仅 BSP |
|---|---|---|
| 做法 | 在官方 workspace 里做应用组件，经 Recovery/Iris 部署 | 独立 ESP-IDF 工程，直接依赖 BSP |
| 优点 | 符合官方 `mosaico.py` 流程，保留 Recovery | 固件内**没有** Vibe Mode 的 Wi-Fi 代码 |
| 缺点 | 部署/回收途径依赖 Vibe Mode，其启动会开 Wi-Fi（见 3.2） | 需自行解决烧录与恢复路径，偏离官方“只用 mosaico.py”的规则；需要分区表影响评估 |
| 当前态度 | 不预设 | 不预设；**需要用户在 Phase 3 前决定** |

## 6. ESP-SR 参考（未验证）

网页摘要（`components.espressif.com` esp-sr 2.5.5 readme，仅为二手信息）：MultiNet 命令词仅中/英；列有日语唤醒词 “こんにちは ESP”；目标表里出现 ESP32-S31。**这些没有在设备上验证，也没有读取一手源码**；因此 UI 中“离线 AI/唤醒词”一直显示“実機モデル検証待ち”，不展示任何识别结果。

### 5.1 决定记录（Phase 3a）

用户回复：不需要 Vibe Mode、选路线 B。因此：固件是独立 ESP-IDF 工程，依赖 BSP（`override_path` 指向工作区内的 BSP，不改上游），不含 Vibe Mode / Recovery / Iris / GSP / LVGL / raylib 引擎。这条路线在固件层面避免了 3.2 的 Wi-Fi 启动代码，**代价**是这块板上原有的官方 Recovery 被擦除（见 07、10 的 B19）。

## 7. Phase 3a 补充发现

* 官方示例 `sky_hop`、`raylib_shooter` 使用 `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y`，与我们一致；但本板上应用运行时 Mac 看不到串口，原因**未知**（10 的 B21）。
* BSP 创建面板 IO 时设置了 `psram_dma_direct = true`（`onboard/display.c`），即允许屏幕 DMA 直接读 PSRAM 帧缓冲。我们的帧缓冲在 PSRAM 中，真机上显示正常。
* 官方 `mosaico_game_app` 送显时用 `swap_bytes = true`（`raylib-lite-engine/components/mosaico_game_app/mosaico_game_app.c:106`）。我们最初没有对调字节，真机颜色错乱；加上对调后颜色正常（见 03、08）。
* 触摸 IC 型号仍未读取，CST9217/CST9220 之争（3.1）仍未解决；BSP 的触摸初始化在真机上没有报告失败，且点按底部标签有响应（见 04）。
