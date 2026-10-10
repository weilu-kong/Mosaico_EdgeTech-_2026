# 06 RF 安全审计

## 1. 约束（最高优先级，原文保留）

展示固件、启动流程及扩展设备**不得启用、请求或执行任何无线功能**：Wi-Fi、蓝牙、ESP-NOW、IEEE 802.15.4 及其他。要求“失败即关闭”（fail-closed）。软件审计不等于 RF 测量。在真实测量并完成日本法规确认之前，状态必须是 **“RF Verification Pending”**；只有全部完成后才能是 “Exhibition Ready”。

## 2. 当前状态

**RF Verification Pending。** 没有任何 RF 测量，也没有任何法规结论。

## 3. 本次实际做了什么

| 检查 | 结果 | 范围 | 标签 |
|---|---|---|---|
| `scripts/rf_check.py` 静态扫描 Demo 源码（26 个文件） | 0 个发现，PASS | 仅 `app/mosaico_experience/main` 与 `host`（不含 tests/tools） | RF已审计(静态) |
| 扫描器自检（负向测试） | 对官方 Recovery 的 Wi-Fi 代码能报出发现 | 证明扫描器不是空转 | RF已审计(静态) |
| 设备端生成的 `sdkconfig` 检查 | Phase 3a 已有 `sdkconfig`；规则已按 §7 修正（BT/802.15.4/OpenThread 必须关，Wi-Fi/COEX 无法关） | Phase 3 | 见 §7 |

扫描器检测的内容：Wi-Fi、蓝牙/BLE、ESP-NOW、802.15.4/Thread/Zigbee、网络栈（esp_netif、lwIP、socket、HTTP、MQTT、mDNS）、PHY/共存/射频测试 API、Matter/BluFi 等的源码引用，以及 sdkconfig 中相关选项；并要求最终生成的 `sdkconfig` 中 Wi-Fi、BT、802.15.4、COEX 这几项**显式关闭**（仅“缺省未写”不算证据）。

### 范围限制（必须一并陈述）

**不覆盖**：ESP-IDF、BSP、Recovery/Vibe Mode 固件、ROM 代码、一级引导程序、扩展模块固件、芯片内部射频硬件行为。静态扫描通过**不能**推出“整机不发射”。

## 4. 重大冲突：Vibe Mode 启动即启用 Wi-Fi

| 位置 | 内容 |
|---|---|
| `esp-mosaico-recovery/firmware/recovery/main/main.c:86` | `factory_network_start()` 启动流程里被调用 |
| `.../factory_network.c:380` | `esp_wifi_init()` |
| `.../factory_network.c:391` | `esp_wifi_start()` |
| `.../factory_network.c:446` | `esp_wifi_scan_start()` |

后果：只要设备按官方保留 Recovery 契约运行（`CONFIG_ESP_IRIS_OTA_DEFAULT_VIA_RECOVERY=y`、OTA 写入器仅在 Vibe Mode），**进入 Vibe Mode 时会启动 Wi-Fi 并扫描**。这与最高优先级约束冲突。

选项（**这是用户的决定，我没有擅自选择**；详见 10 和 Phase 3 提案）：

1. 展示设备上不保留带 Wi-Fi 的 Vibe Mode 固件（需要评估分区/恢复影响，涉及“不得擅自改分区表、不得整片擦除”的限制，必须先获得同意）。
2. 保留官方契约，但展示期间物理上避免进入 Vibe Mode，并把“Vibe Mode 会发射 Wi-Fi”写入操作规程；此时“展示固件本身无无线”成立，但整机设备并不满足严格表述。
3. 向官方/维护者确认是否存在可关闭 `factory_network` 的 Kconfig 或构建选项（**未调查**；不修改上游源码）。

## 5. 其他需要在 Phase 3 解决的 RF 点

* 展示应用 `sdkconfig` 必须显式关闭 Wi-Fi/BT/802.15.4 等；构建后用 `rf_check.py` 检查生成的配置与 map 文件是否链入无线库。
* 扩展模块：未知或带无线能力的模块 → 失败即关闭（不初始化）。目前仅在 UI 上有“未知”显示，设备侧未实现。
* 设备页的 `rf` 字段：Host 报告 `EXP_RF_UNKNOWN`；设备侧将根据运行时可见状态报告，**但“软件可见状态为关”不等于“RF 测量为无发射”**，界面文字已区分。
* 运行时检查（Phase 3，计划）：启动后读取各无线子系统是否被初始化；任何异常即显示 `EXP_RF_SOFT_CHECK_FAIL` 并阻止“展示”流程。

## 6. 达到 “Exhibition Ready” 所需条件（全部未执行）

1. 在最终固件上做实际 RF 测量（频谱/发射检测）。
2. 日本法规（电波法/技适相关）适用性确认。
3. 确认 Vibe Mode/Recovery 行为已被消除或被运营流程完全排除。
4. 扩展模块清单与禁用策略验证。

## 7. Phase 3a 构建审计（固件 `firmware/`，已编译，未烧录）

范围：一次具体构建（IDF `7b9cc1ac`，esp32s31，应用 0x8c5b0 字节），**不是 RF 测量**。工具：`scripts/rf_map_check.py`。

| 项 | 结果 |
|---|---|
| `CONFIG_BT_ENABLED` / `IEEE802154_ENABLED` / `OPENTHREAD_ENABLED` | 关闭 |
| `CONFIG_ESP_WIFI_ENABLED`、`CONFIG_ESP_COEX_ENABLED` | **=y，且无法通过配置关闭**：它们是由 SoC 推导的隐藏符号（`components/esp_wifi/Kconfig`，`default y if SOC_WIFI_SUPPORTED`）。因此 `sdkconfig.defaults` 里写 `=n` 无效，**配置不是这两项的证据** |
| 链接进镜像的 Wi-Fi MAC / net80211 / pp / coex / BT / NimBLE / 802.15.4 / OpenThread / lwIP 代码 | 无（对应归档成员数均为 0） |
| `libesp_wifi.a` / `libwpa_supplicant.a` / `libesp_netif.a` | 各只有 1 个 `*_esp_err_codes.c.obj`（错误码字符串表） |
| **PHY（射频前端）驱动** | **已链接**：`libphy.a` 15 个成员、`libesp_phy.a` 5 个成员，符号 `esp_phy_enable` 在镜像中。由 IDF 核心的 `esp_hw_support/sleep_phy.c`（睡眠管理）等拉入，不是 Demo 代码调用 |

结论（只限于以上事实）：本次构建没有链接 Wi-Fi/蓝牙/802.15.4 协议栈；但 PHY 驱动在镜像里，**是否会在运行时被启用未验证**。需要：(a) 运行时检查（计划）；(b) RF 实测；(c) 日本法规确认。状态仍是 **RF Verification Pending**。

`rf_check.py` 的 sdkconfig 规则已据此修正：不再要求 Wi-Fi/COEX 为关闭（做不到），改为要求 BT、802.15.4、OpenThread 关闭，其余交给 `rf_map_check.py`。

## 8. Phase 3a 设备状态（RF 相关）

| 项 | 事实 | 标签 |
|---|---|---|
| 这块板原有的官方 Recovery / Vibe Mode 固件 | **已被整片擦除**（用户明确要求；未备份）。板上现在只有本 Demo 的开发固件（bootloader 0x2000、分区表 0x8000、应用 0x10000）。这在**这一块板子**上消除了 §4 的 Wi-Fi 冲突，**但不证明整机不发射** | 设备已验证（擦除/烧录成功） |
| 开发固件的无线 API 调用 | 无（源码扫描 `rf_check.py`：`app/…/main`、`host`、`firmware` 共 35 个文件，0 发现） | RF已审计(静态) |
| 链接内容 | 无 Wi-Fi MAC / BT / 802.15.4 / coex / lwIP 代码；PHY 驱动被链接（见 §7） | RF已审计(静态)，一次具体构建 |
| 运行时是否启用了 PHY/射频 | **未验证**。3a 固件的 `exp_sensors_t.rf` 固定报告 `EXP_RF_UNKNOWN`，界面上显示 “RF Verification Pending” | 未执行 |
| ROM / 一级 bootloader / 二级 bootloader（IDF 构建的）行为 | 未审计 | 未执行 |
| 芯片能力 | esptool 报告该芯片具备 Wi-Fi 6 / BT 5.4 / IEEE 802.15.4 能力（硬件能力，不代表启用） | 设备已验证（esptool） |
| 另一块 ESP32-S3 板 | 与项目无关，**从未被写入或读取内容**（只读识别了一次芯片信息） | — |

## 9. Phase 3b-1 增量（IMU、电量计、屏幕日志）

| 项 | 事实 | 标签 |
|---|---|---|
| 新增代码的无线 API 调用 | 无。`dev_sensors.c`（BMI270/BQ27220，仅 I2C）、`exp_dbglog`（RAM 缓冲 + vprintf 钩子，不输出到任何传输）、present 任务均不触及无线 | RF已审计(静态)：`rf_check.py` 43 个文件 0 发现 |
| 屏幕日志 | 只写 RAM、只显示在面板上；**没有**使用官方 Iris 的 USB/TCP 传输 | 已编码；设备已验证可用 |
| BQ27220 配置改写 | 属于电量计数据存储器，**不是无线配置**；见 10 B32 | 设备已验证 |
| BSP 启动警告 `GPIO60` | VCC_3V3 软启动（LEDC）相关，与无线无关；见 10 B30 | 设备观察 |
| 运行时是否启用了 PHY/射频 | 仍**未验证**；`exp_sensors_t.rf` 仍固定报告 `EXP_RF_UNKNOWN` | 未执行 |

状态仍是 **RF Verification Pending**。§6 的四个条件全部未完成。

## 10. Phase 3b-2 增量（磁力计 BMM150 ×2，只读诊断，0.5.0）

* 新增代码：`dev_sensors.c` 中经官方 BSP `bsp_magnetometer_init()/read()` 通过共享 I2C（地址 0x11 / 0x12）读取两颗 BMM150。BMM150 是片上磁传感器，没有射频功能；新增调用与无线无关，不新增任何 Wi-Fi / 蓝牙 / ESP-NOW / 802.15.4 / 协处理器依赖。
* BSP 初始化只会把传感器设为 normal 模式与 regular preset（传感器自身寄存器，上电即丢失），不写入任何持久配置。
* 初始化与轮询都在同一个传感器任务里顺序执行（避免 0.4.0 的 I2C 总线创建竞争）。
* 磁场数据默认**不**送入应用（`EXP_MAG_FEED_APP=0`），只在日志画面显示；因此罗盘与 9 轴 yaw 的行为与 0.4.7 完全相同。
* 静态扫描：43 个文件，0 发现（`evidence/rf_static_scan.json`）。**这仍是软件审计，不是 RF 测量**；状态保持 RF Verification Pending。
* 0.5.1：仅更正磁力计换算比例（1 LSB = 1 µT）与文档，没有新增依赖或调用；静态扫描重跑结果见 `evidence/rf_static_scan.json`。
* 0.5.2：把磁力计（mag0）接入应用的罗盘/9 轴 yaw，增加轴映射和单传感器复制；只有片上磁传感器的 I2C 读数和纯计算，没有新增无线相关依赖或调用。静态扫描 43 个文件，0 发现；仍是软件审计，不是 RF 测量。
* 0.5.5：新增纯计算的旋转一致性检查（日志画面两行），没有新增依赖或无线相关调用。
* 0.5.4：新增诊断行与纯计算的航向，没有新增依赖或无线相关调用。
* 0.5.3：磁力计校准改为纯计算（球面拟合）并新增日志行；没有新增依赖或无线相关调用。

## 11. Phase 3c 增量（麦克风、扬声器、马达、扩展槽探测、运行时 RF 探针，0.6.0）

* 新增代码与调用：`dev_audio.c`（BSP `bsp_audio_codec_microphone_init/speaker_init`、`esp_codec_dev` 读写、LEDC/GPIO 马达）、`dev_slots.c`（官方 `mosaico_module_mgr`：只读 AT24C02 EEPROM 描述符，使用 GPIO0/GPIO1 子板 I2C 与 GPIO14/39 地址选择）。这些都是编解码器/总线/GPIO 操作，不新增任何 Wi-Fi / 蓝牙 / ESP-NOW / 802.15.4 / 协处理器依赖；`mosaico_module_mgr` 的源码已检查：只用 `driver/i2c_master.h`、FreeRTOS、`bsp/subboard.h`，没有无线相关引用（它在 BSP 中另有 `mosaico_network`/`mosaico_peer_link` 组件，**本工程没有依赖它们**）。
* 隐私：麦克风样本只做电平/VAD/频谱显示；**不录音、不保存、不发送**。
* **运行时 RF 探针（`dev_rf_probe.c`）与 `scripts/rf_check.py` 的一处窄豁免**：探针用“弱未定义符号是否存在”检查 7 个无线入口点（`esp_wifi_init/start`、`esp_now_init`、`esp_bt_controller_init/enable`、`esp_ieee802154_enable/receive`）是否被链接进运行镜像；**它从不调用其中任何一个**。静态扫描因此对 `dev_rf_probe.c` 做了豁免，且只放行两种固定形状的行（`extern const char NAME __attribute__((weak));` 与 `{"NAME", &NAME},`，NAME 必须在上述 7 个之中）。已验证：在探针文件里加入真正的调用 `esp_wifi_init();` → 报告 1 项；加入不在白名单里的弱声明 → 1 项；在其它文件里放同样的弱声明 → 1 项；正常情况 50 个文件、0 项。
* **探针的限度（必须在口头与文档里说明）**：它只说明这些入口点“不在本镜像里”。它不覆盖 ROM 代码、PHY 驱动（IDF 核心会链接 libphy，`rf_map_check.py` 已按此处理）、Bootloader、附加模块或实际发射；`SOFT_OFF_OK` 在设备页显示为“RF無効（ソフト設定を確認）”，**不是 RF 测量，也不是合规结论**。
* 设备页 RF 行：没有探针结果时显示“確認不可”（不再写“シミュレータ”）。
* 附加模块风险：扩展槽读到**未知模块或描述符校验失败**时一律显示“不明なモジュール”（视为未审查，RF 无法保证）；本固件不会启动任何模块驱动。已知模块类型（摇杆/交互/按键灯/相机）在 3c 只被识别，不被使用。
* 状态仍是 **RF Verification Pending**。§6 的四个条件全部未完成。静态扫描：50 个文件，0 发现（`evidence/rf_static_scan.json`）。

## 13. Phase 3e 增量（日语离线语音，0.8.0）

* 新增依赖（只在 `-DEXP_VOICE=1` 构建里）：`espressif/esp-sr 2.5.3`、`esp-dl 3.3.11`、`dl_fft 0.7.0`（及其传递依赖 esp-dsp、cjson）。esp-sr 是片上离线语音栈（AFE/WakeNet/MultiNet），**没有网络、Wi-Fi、蓝牙、ESP-NOW、802.15.4 相关接口**；模型从本机 flash 的 `model` 分区读取，不联网。
* **没有复制** Full Demo 的任何无线配置（BT/A2DP/Wi-Fi）；`sdkconfig.defaults` 的 RF 关闭项不变，语音构建只追加模型/分区/CPU 频率。
* 麦克风音频只在 RAM 里处理，不录音、不保存、不发送。
* 静态扫描现在覆盖 `firmware/voice_components/`；语音镜像构建后仍须运行 `scripts/rf_map_check.py`（链接表审计）。**这些都不是 RF 测量**；状态仍是 **RF Verification Pending**。

## 12. Phase 3d 增量（摇杆模块、调光、看门狗、磁场判断、发布配置，0.7.0）

* 新增依赖：`mosaico_module_joystick`（官方，源码已检查：只用 `esp_adc`、`esp_driver_gpio` 与 `mosaico_module_mgr`，没有无线相关引用）。只有在模块管理器给出**有效描述符且类型为 Handle** 时才认领槽位；未知模块、描述符无效的模块、其它已知类型一律不认领。
* `bsp_display_brightness_set`（CO5300 亮度寄存器）、任务看门狗（`esp_task_wdt_*`）、磁场判断都与无线无关。
* 展台发布配置：`idf.py -DEXP_RELEASE=1` 去掉日志画面、帧时间叠加层、启动基准；`scripts/release_check.py` 检查镜像版本串（`-rel`）与调试字符串。**这是配置门禁，不是 RF 结论。**
* 静态扫描：54 个文件，0 发现。状态仍是 **RF Verification Pending**。

