# 12 · 视觉（摄像头）——阶段 4a：预览与帧时间测量（0.9.0）——**全部未执行**

> 状态：**已编码；主机单元测试通过（ASan/UBSan）；设备端 C 代码用"桩头文件"做过语法/类型检查（桩按真实 IDF `ppa.h` 与 BSP `mosaico_module_camera.h` 的定义写成）；RF 静态扫描通过。**
> **没有用真实 ESP-IDF 编译或链接（云端没有 IDF），没有烧录，没有在板子上运行过，没有任何实测数字。** 下面所有"预期"都是推断，不是结果。
> RF：本阶段不启用、不请求、不执行任何无线功能；只用摄像头（DVP）、PPA 和 PSRAM。新增依赖 `espressif/esp_video`（及其依赖 `esp_cam_sensor`、`esp_h264`、`esp_ipa`、`usb_host_uvc`）来自组件库，注册表页面的依赖列表里没有无线组件，但**必须在构建后用 `scripts/rf_map_check.py` 审计链接内容**。状态仍是 **RF Verification Pending**。

## 0. 回退点

`git tag stable-stage3e-0.8.1`（commit `4a3d044`）是阶段 3e 的稳定版本。视觉开发在分支 `feature/vision`。回到语音固件：烧录 `build_voice` 的 app（`scripts/voice_flash.sh update`），**分区表不变**，见 §3。

## 1. 做了什么 / 没做什么

| | 说明 |
|---|---|
| 做了 | 独立构建 `-DEXP_VISION=1`（目录 `build_vision`）：一个**开发用"摄像头测试视图"**。三连点屏幕**底部中央**（2 秒内，x 150–330、y ≥ 400）打开；点**右上角**（x ≥ 390、y < 90）退出；其它位置每点一次切换一种旋转/镜像预设（共 8 种，第 1 种 = 官方示例的设置）。前两下点击仍会传到应用（和日志视图的角落手势同样的限制）。|
| 流水线 | 官方 BSP `mosaico_module_camera`（左槽，OV3640 / SC101IOT，DVP）→ 帧（默认 OV3640 UYVY 1024×768）→ **PPA**：取中心正方形 768×768、旋转/镜像、缩放到 480×480 RGB565 → CPU 32 位字交换字节（面板要大端）→ 发布给主线程 → 主线程拷到帧缓冲后叠加测量数字。照官方示例 `camera_lcd_preview`，增加了字节交换和计时。|
| 屏幕上的数字 | 相机状态/错误原因、传感器分辨率与 fourcc、裁剪边长、缩放系数是否为 1/16 的整数倍、打开耗时、**fps、帧数、采集错误数、重启数、各阶段平均耗时（等传感器 / PPA / 字节交换 / 一圈）**、方向预设、PSRAM 剩余。全部由板上 `esp_timer` 测得；没有任何估计值。|
| 保护 | 打开前用槽管理器读到的左槽信息过一道闸（`dev_camera_gate.h`，主机测试）：只放行**已识别的相机模块**；左槽有"在位但描述符读不出"的板子（官方说出厂相机板的 EEPROM 可能没写）才以"调试例外"打开并在日志里警告；**别的已识别模块（摇杆等）、有有效描述符但不是相机的未知板子、空槽、尚未探测——一律拒绝并在屏幕上写出原因。**|
| 不做 | 不开机自动启动相机；不识别人脸；不存图、不传输图像；不接入正式界面页面；不碰语音；不改分区表、不动模型分区、不动 bootloader。|

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

## 4. 板上验证清单（全部"未执行"）

| # | 要看什么 | 通过标准 | 结果 |
|---|---|---|---|
| 1 | 视觉构建能编译、链接，`rf_map_check` 通过 | 无 error；无线入口点不在镜像里 | 未执行 |
| 2 | 烧录后正常启动，UI、触摸、日志视图与之前一致 | 与 0.8.1 无回归 | 未执行 |
| 3 | 三连点底部中央 → 打开测试视图；有相机时出现画面，状态 RUNNING | 画面是实景，不是花屏/全黑 | 未执行 |
| 4 | 点屏幕切换方向，找到画面朝向正确的预设 | 告诉我是第几种 | 未执行 |
| 5 | 读下 fps、各阶段耗时、PSRAM 剩余（拍照发我） | 这些就是后面所有决策的基础数据 | 未执行 |
| 6 | 右上角退出 → 回到正常界面；再进再出 5 次 | 无崩溃、无泄漏（PSRAM 剩余不持续下降） | 未执行 |
| 7 | 拔掉相机/左槽放摇杆后尝试打开 | 屏幕写出拒绝原因，不会驱动引脚 | 未执行 |
| 8 | 视图打开 10 分钟 | 不崩溃、不掉帧到停 | 未执行 |
| 9 | 退出后 USB 串口行为（相机会占用 D2） | 退出后 USB 口恢复；若不恢复请告诉我 | 未执行 |

## 5. 已知限制

* 云端没有 IDF，所有设备端代码只做过桩头文件检查。首次真机编译可能出现链接/组件依赖问题（尤其是 `esp_video` 的 Kconfig 与 SC101IOT 默认格式未在 `sdkconfig.defaults.vision` 里指定）。
* 相机在板子上的安装方向没有文档，所以用 8 种预设现场确定。
* 三连点手势的前两下会传到应用（底部中央若有按钮会被按下）。
* 相机与语音不能同时构建（`EXP_VOICE` 与 `EXP_VISION` 互斥，4c 阶段再决定分区与并发）。
* 人脸检测（4b）尚未开始：它需要 `human_face_detect` 与 esp-dl（官方 `ai_model_gallery` 把模型放在 flash rodata 里，不需要单独模型分区；是否仍需要分区取决于 4b 的 app 大小，到时再征求同意）。S31 上的推理速度完全未知。
