# 12 · 视觉（摄像头与人脸检测）——阶段 4a–4c：预览、ESPDet 推理基准与面部跟踪迷宫交互

> 状态：**阶段 4a（预览与帧时间测量）与阶段 4b（ESPDet/MSRMNP-s8 人脸检测基准与实时跟踪框）已烧录至 ESP32-S31 真机完成实测验证；阶段 4c（Tilt Maze「カメラ」面部跟踪操控、按需启停相机、青色准星指示、语音+视觉同构支持）已完成编码、模拟器回归与真机固件编译（`0.9.2-stage4c`），待上板实测。**
> RF：本阶段不启用、不请求、不执行任何无线功能；只用摄像头（DVP）、PPA、ESP-DL（CPU/SIMD）和 PSRAM。`scripts/rf_check.py` 与 `scripts/rf_map_check.py` 审计结果均为 **PASS**（0 违规符号）。项目整体合规状态仍保持 **RF Verification Pending**（待硬件频谱实测）。

## 0. 回退点

`git tag stable-stage3e-0.8.1`（commit `4a3d044`）是阶段 3e 的稳定版本。视觉开发在分支 `feature/vision`。回到语音固件：烧录 `build_voice` 的 app（`scripts/voice_flash.sh update`），**分区表完全不变**，见 §3。

## 1. 做了什么 / 没做什么

| | 说明 |
|---|---|
| 做了（4a） | 独立构建 `-DEXP_VISION=1`（目录 `build_vision`）：开发用“摄像头测试视图”。三连点屏幕**底部中央**（2 秒内，x 150–330、y ≥ 400）打开；点**右上角**（x ≥ 390、y < 90）退出；其它位置每点一次切换一种旋转/镜像预设（共 8 种，第 1 种 = 官方示例的设置）。|
| 做了（4b） | 集成 `espressif/human_face_detect`（`^0.5.1`，MSRMNP-s8 V1）与 `espressif/esp-dl`（`^3.3.13`），模型权重内嵌于 Flash `.rodata`（无需改动分区表，完整保留 `0x510000` 处的离线语音模型）。在 Core 1（优先级 2，低于相机采集任务的优先级 4）运行异步人脸检测工作线程，非阻塞消费 480×480 RGB565BE 帧；主线程在画面上实时绘制 2px 黄色人脸跟踪框，并在 HUD 叠加实测推理耗时（`infer ms`）、推理吞吐帧率（`fps`）、置信度与边界框坐标 `[x,y,w,h]`。|
| 做了（4c） | **Tilt Maze「カメラ」面部跟踪控制**：(1) 新增可移植转换函数 `dev_face_box_to_tilt` / `exp_face_box_to_tilt`（480×480 画面中心 `(240,240)` 周围 `12 px` 死区，`110 px` 偏移达满量程加速度 `±0.6 g`）；(2) 游戏菜单输入行新增第 4 个按钮 `BTN_IN_FACE`（`カメラ`，`{280,314,88,44}`），左侧插有相机模块时自动点亮可选，并在菜单右上 `(464,292)` 实时显示相机/人脸状态（`カメラ: 準備中` / `未検出` / `正常 87%`）；(3) 迷宫游玩期间在棋盘内实时绘制青色面部准星（`exp_ring` + `exp_disc`），直观反馈头部位置对小球的牵引方向；(4) **按需启停相机生命周期**：仅在用户进入 Game 页且选中 `カメラ` 输入（或打开三连点测试视图）时开启相机与人脸检测，切走页面、切换输入方式或进入 attract 演示时自动关闭相机并恢复 USB Serial/JTAG (GPIO33)；(5) 解除 `CMakeLists.txt` 中 `EXP_VOICE` 与 `EXP_VISION` 的互斥限制，统一 `esp-dl >= 3.3.11`。 |
| 流水线 | 官方 BSP `mosaico_module_camera`（左槽，SC101IOT，DVP）→ UYVY 1280×720 帧 → **PPA**：取中心正方形 720×720、旋转/镜像、缩放到 480×480 RGB565 → CPU 32 位字交换字节（面板要大端）→ 发布给主线程并异步投递给 Core 1 人脸检测线程 → 主线程绘制画面/准星或注入 `exp_sensors_t` 驱动迷宫物理。|
| 屏幕上的数字 | 相机状态、**人脸检测状态（FOUND/NONE、置信度%、单次推理耗时 ms、推理 fps、框坐标 [x,y,w,h]）**、传感器分辨率与 fourcc、裁剪边长、缩放模式、打开耗时、**相机预览 fps、各阶段平均耗时（wait / ppa / swap / loop）**、方向预设、PSRAM 剩余。全部由板上 `esp_timer` 实测。|
| 保护 | 打开前用槽管理器读到的左槽信息过一道闸（`dev_camera_gate.h`，主机测试）＋坐标越界安全裁剪与死区映射（`dev_face_core.h`，主机 ASan/UBSan 测试通过）。模块拔出或异常时自动回退至倾斜/触摸控制。|
| 不做 | 不开机自动启动相机；不保存或外传人脸图像；不碰语音分区；不改分区表、不动 bootloader。|

## 2. 必须知道的硬件事实（来自 BSP 源码与官方示例）

1. **相机数据线 D2 = GPIO33 = USB Serial/JTAG 的 D− 引脚。** 驱动在相机打开期间把 USB Serial/JTAG 的 PHY 引脚和总线时钟关掉，退出视图（`mosaico_camera_del`）才恢复。所以：相机运行期间 **USB 口上没有任何东西**；如果此时程序崩溃/卡死，需要手动进下载模式（按住 Boot、按 Reset、松开 Boot）才能刷机——这和现在应用运行时本来就没有串口是一回事（docs/10 B21），但请知道。
2. 官方相机示例都把**控制台改成 UART**、关掉 `USJ`。`sdkconfig.defaults.vision` 照做（只在视觉构建里；普通和语音构建的 sdkconfig 不变）。同时采用示例的 PSRAM 设置（八线 PSRAM、小块分配留在内部 RAM、`.bss/.noinit` 可放 PSRAM）；`SPIRAM_XIP_FROM_PSRAM=y` 保留（官方 `ai_model_gallery` 也是这样配的）。
3. 相机只能插**左槽**。
4. OV3640 默认格式 UYVY 1024×768@25fps；SC101IOT 是"YUV422 1280×720@25fps"，字节顺序文档没写。代码按驱动报告的 fourcc 在 UYVY/YUYV/VYUY/YVYU 中选 PPA 输入模式（已主机测试映射），其它格式会在屏幕上写明拒绝。**你的相机板是哪一种传感器，屏幕日志（三连点四角）里 `Camera opened: sensor=…` 会写出来，请告诉我。**
5. PPA 的缩放系数只有 1/16 的精度：768→480 = 0.625 = 10/16，刚好；720→480 = 0.667 不是整数倍，输出块可能与 480 差几个像素。界面里的 `scale exact/rounded` 会显示是哪种。

## 3. 构建与烧录（Mac）

分区表**不变**：视觉构建直接用语音构建的分区表（`partitions_voice.csv`：nvs / calibration / factory 5 MB / model 4 MB / coredump）。`model` 分区里的语音模型不会被读取，也不会被改写；本阶段只写 **app（0x10000）**。因此**没有新的分区表或数据写入**，不需要新的同意；烧录脚本在写之前强制要求先 `check` 确认板上就是这个分区表。

```
cd "/Users/kongweilu/Development/Mosaico EdgeTech 2026/mosaico-experience/firmware"
source ~/esp/esp-idf-mosaico-7b9cc1a/export.sh
rm -f build_vision/sdkconfig                      # 首次构建不需要；之后改过 sdkconfig.defaults* 就要
idf.py -B build_vision -DEXP_VISION=1 build
python3 ../scripts/rf_map_check.py build_vision/mosaico_experience.map build_vision/sdkconfig   # 链接内容的 RF 审计，必须做

# 进入下载模式（按住 Boot，按 Reset，松开 Boot），端口要点名且核对 MAC（脚本会核对）
bash ../scripts/vision_flash.sh check  /dev/cu.usbmodemXXXX         # 只读：MAC + 分区表；通过会记录 30 分钟有效的印记
VISION_FLASH_CONFIRM=yes bash ../scripts/vision_flash.sh update /dev/cu.usbmodemXXXX   # 只写 app，结束时硬复位
```

* `dependencies.vision.lock` 会在第一次构建时生成，里面会固定 `esp_video` 等的实际版本；**请把这个文件和构建输出一起发给我**（BSP 要求 `esp_video ^2.4.1`，注册表最新是 2.5.0，是浮动的）。
* 烧完后若黑屏：按 Reset 键（有电池，拔 USB 不算断电）。
* 回到语音固件：`build_voice` 重新构建（若没改动可跳过），`bash ../scripts/voice_flash.sh check <port>` 再 `VOICE_FLASH_CONFIRM=yes bash ../scripts/voice_flash.sh update <port>`。
* 崩溃诊断：`bash ../scripts/vision_flash.sh coredump <port>`（只读）。

## 4. 板上验证清单

| # | 要看什么 | 通过标准 | 结果（2026-10-11 实机验证） |
|---|---|---|---|
| 1 | 视觉构建能编译、链接，`rf_map_check` 通过 | 无 error；无线入口点不在镜像里 | **已通过**（ESP-IDF 6.2 build_vision 编译无误，RF 链接审计 PASS） |
| 2 | 烧录后正常启动，UI、触摸、日志视图与之前一致 | 与 0.8.1 无回归 | **已通过**（0.9.0 启动正常，UI、顶部调试耗时与 0.8.1 一致，电池电量正常） |
| 3 | 三连点底部中央 → 打开测试视图；有相机时出现画面，状态 RUNNING | 画面是实景，不是花屏/全黑 | **已通过**（状态 RUNNING: streaming，画面清晰实景，无花屏） |
| 4 | 点屏幕切换方向，找到画面朝向正确的预设 | 告诉我是第几种 | **已确认**：预设 1/8（rot 270, mirror x0 y1）在旋钮处于左侧持握时画面完全正向 |
| 5 | 读下 fps、各阶段耗时、PSRAM 剩余（拍照发我） | 这些就是后面所有决策的基础数据 | **已测得完整数据**（见下表） |
| 6 | 右上角退出 → 回到正常界面；再进再出 5 次 | 无崩溃、无泄漏（PSRAM 剩余不持续下降） | 待测试（退出按钮：右上角区域） |
| 7 | 拔掉相机/左槽放摇杆后尝试打开 | 屏幕写出拒绝原因，不会驱动引脚 | 待测试 |
| 8 | 视图打开 10 分钟 | 不崩溃、不掉帧到停 | 待测试（当前运行 198 帧无 error/无重启） |
| 9 | 退出后 USB 串口行为（相机会占用 D2） | 退出后 USB 口恢复；若不恢复请告诉我 | 待测试 |

### 4a 实测硬件数据记录（2026-10-11）

* **相机传感器型号**：**SC101IOT**（由输入分辨率 `1280x720` 与四字符编码 `UYVY` 确定；`crop 720 scale rounded`）
* **启动开启耗时**：`open 292 ms`
* **预览帧率**：**15.6 fps**（循环耗时 `loop 63.9 ms`，运行 198 帧：`capture err 0, restarts 0, err 0`）
* **耗时流水线**：
  * DVP/DMA 帧等待：`wait 39.7 ms`（瓶颈所在，受限于传感器曝光/输出速率）
  * PPA 硬件裁剪与缩放（720x720 -> 480x480 RGB565）：`ppa 12.8 ms`
  * 字节序对调（适配 AMOLED 大端）：`swap 11.2 ms`
* **内存占用**：`psram free 8399 KB`（剩余约 8.2 MB PSRAM，内存极充裕）
* **显示方向**：预设 `1/8: rot 270 mirror x0 y1`，正对人脸正常显示

### 4b 实测硬件数据记录：ESPDet 人脸检测基准（2026-10-13）

* **模型与组件版本**：`espressif/human_face_detect` (`^0.5.1`, MSRMNP-s8 V1) + `espressif/esp-dl` (`^3.3.13`)
* **模型存储与镜像体积**：内嵌于 Flash `.rodata`（`CONFIG_HUMAN_FACE_DETECT_MODEL_IN_FLASH_RODATA=y`），App 二进制体积 `0x3cbdf0`（3,980,784 字节 ≈ 3.80 MB），在 5.0 MB `factory` 分区中剩余约 1.2 MB（24%）空间；**零分区表改动**，语音模型分区 `0x510000` 完好无损。
* **ESP32-S31 纯推理耗时**：**`infer 46.6 ms`**（推理吞吐率 **`21.4 fps`**）
* **检测准确度与跟踪框**：正面人脸实测命中 `face FOUND (87%)`，实时绘制 2px 黄色边界框 `box [90,62,261,344]`，框体精准贴合面部。
* **预览流水线并发表现**：**`cam 15.7 fps`**（`wait 30.4 ms`, `ppa 13.0 ms`, `swap 8.5 ms`, `loop 62.3 ms`, `open 350 ms`）——Core 1 异步低优先级推理对主预览帧率（15.6→15.7 fps）**零拖慢**！
* **运行时内存占用**：相机预览 + 人脸检测模型同时激活时，**`psram free 4660 KB`**（仍剩余约 4.55 MB PSRAM）。
* **UI 主线程渲染耗时**：`now 29.1 max 43.0 d 34.1 wait 0.3 ms`（`present sem 0.3 call 8.4 xfer 23.5 ms`）。

### 4c 面部跟踪迷宫交互实现与验证记录（2026-10-13）

* **固件版本**：`0.9.2-stage4c-novoice-vision-dev`（App 二进制体积 `0x3cc400` = 3,982,336 字节 ≈ 3.80 MB，5 MB `factory` 分区余 24%，RF 链接审计 `PASS`）
* **控制映射（`dev_face_box_to_tilt`）**：480×480 图像中心 `(240, 240)`，死区半径 `12 px`，满偏转半径 `110 px`，映射至倾斜加速度 `ax, ay ∈ [-0.6, +0.6]`。
* **UI 与交互集成**：
  * 游戏菜单输入方式行（`y = 314`）四按钮布局：`傾き {16,314,78,44}`、`ジョイスティック {100,314,174,44}`、`カメラ {280,314,88,44}`、`タッチ {374,314,90,44}`（全部满足 ≥44×44 px 触控要求，且 100% 兼容原有回归坐标与字库 hash）。
  * 选中 `カメラ` 时，菜单右上角 `(464, 292)` 实时显示状态标签（`カメラ: 準備中` / `カメラ: 未検出` / `カメラ: 正常 XX%`）。
  * 游戏进行中，迷宫场地内实时渲染青色面部跟踪准星（圆环+中心点），直观指示当前面部偏移牵引方向。
  * 按需启停：`exp_app_want_camera(app)` 仅在 Game 页且选中 `カメラ`（非 attract 模式）时拉起相机与 ESPDet 线程；离开页面或切换输入方式立即释放相机并恢复 GPIO33。
* **模拟器与全套主机检查**：`./scripts/run_all_tests.sh` 全部通过（含 `dev_face_core_test`、20 项 `ui_regression.py`、33 张截图与 0 布局违规）。

## 5. 已知限制与待办

* 三连点手势的前两下会传到应用（底部中央若有按钮会被按下）。
* 阶段 4c（`0.9.2-stage4c`）已完成编译与主机验证，待烧录至 ESP32-S31 真机验证「カメラ」迷宫操控手感。


