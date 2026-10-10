# ESP-Mosaico Experience – Offline Interactive Showcase

ESP32-S31 / ESP-Mosaico V1.2 向けの、オフライン展示デモ（日本語 480×480 UI）。
A: モーション/HMI、B: オフライン音声処理・AI、C: インタラクティブゲーム（傾き迷路）。

## 当前状态（请先读这一节）

| 阶段 | 状态 |
|---|---|
| Phase 0 环境/仓库/RF 审计 | 完成；Mac 本机环境已审计（见 `docs/01` §7） |
| Phase 1 架构 | 完成 |
| Phase 2 Host 交互式 UI 预览 | 完成，**模拟器已验证**（合成传感器数据） |
| Phase 3a 真机固件（显示 + 触摸 + UI，路线 B） | 已编译、已烧录到一块 Mosaico；显示、触摸有响应（**设备已验证**，范围很窄） |
| 性能优化 | 约 35 ms/帧（≈28–29 fps），目标 33 ms 未达到；瓶颈已测，见 `docs/08` §7 |
| Phase 3b-1 IMU + 电量计 + 屏幕日志 | **设备已验证**（0.4.3）：BMI270 读数、加速度计/陀螺仪 X、Z 方向，BQ27220 读数（官方驱动每次上电都改写其配置，0.4.4 起改为只读，`docs/10` B32） |
| Phase 3b-2 磁力计/罗盘 | 已编码、模拟器测试；设备结果仍偏差大，原因未确定（`docs/10` B38） |
| Phase 3d 摇杆模块、烧屏保护（调光）、任务看门狗、磁场干扰判断、展台发布配置（`-DEXP_RELEASE=1`） | **已编码、语法检查 + 主机测试**（0.7.0）；**设备验证未执行**（`docs/10` B40） |
| Phase 3c 麦克风、扬声器、马达、扩展槽探测、运行时 RF 探针 | **已编码、语法检查 + 主机测试**（0.6.0）；**设备验证未执行**（用户暂时没有连接开发板，见 `docs/10` B39） |
| Phase 3e 日语离线语音操作（ESP-SR，照 Full Demo；`-DEXP_VOICE=1` 独立构建，需要新分区表） | **已编码、语法检查（含 esp-sr 真实头文件）+ 主机测试 + 模拟器**（0.8.0）；**未编译、未上板、未烧录**；烧录需要你同意改分区表（`docs/11`） |
| Phase 4–5 设备验证、RF/运营 | **未开始** |

* 开发固件 `0.8.0-stage3e-novoice-dev`（普通构建；语音构建为 `-voice-`，见 `docs/11`；0.8.0 = 3e，**同样没有上板、没有用真实 ESP-IDF 编译**；以下为 0.7.0 的说明：展台镜像用 `idf.py -DEXP_RELEASE=1` 构建，版本串以 `-rel` 结尾，并用 `scripts/release_check.py` 检查；0.7.0 = 3d，**同样没有上板、没有用真实 ESP-IDF 编译**；0.6.0 = 3c：麦克风、扬声器、马达、扩展槽只读探测、运行时 RF 探针，**全部尚未在板子上运行过，也没有用真实 ESP-IDF 编译**；以下为 0.5.5 的说明：0.4.7 已烧录，用户确认动作页 3D 模型“没什么问题了”；0.5.0–0.5.3 磁力计/罗盘待烧录与验证；0.5.2 实测航向偏 88°；0.5.3 改球面拟合后设备仍偏约 100°；0.5.4 增加诊断行；0.5.5 增加不依赖手机的旋转一致性检查）。**这块板上原有的官方 Recovery 已被擦除（用户决定，无备份）**，见 `docs/07`、`docs/10` B19。
* 固件读真实 IMU、磁力计与电量计；0.6.0 起还会尝试麦克风/扬声器/马达/扩展槽（**未验证**，驱动没有报告前状态一律为“starting/未確認”）；带调试叠加层、屏幕日志手势和启动基准，**不能上展台**（展出前 `EXP_TIMING_OVERLAY`/`EXP_DBGLOG`/`EXP_BOOT_BENCH` 全部设为 0）。
* 没有 RF 测量、没有日本法规结论。状态：**RF Verification Pending**。
* Host 预览窗口右上角 `SIM` 标签表示全部传感器数据为合成。

## 目录

```
app/mosaico_experience/
  main/    可移植 C 核心（姿态融合、音频 DSP、迷宫、页面、文字/图形层、字形数据）
  host/    Host 模拟器适配（合成传感器、引擎模块）——不会链接进固件
  tools/   字体生成、Host 运行包装、截图/布局检查、本地预览服务器
  tests/   原生单元测试、行为回归（19 项）、绘制 shim 与官方引擎逐像素对比
firmware/  路线 B 的 ESP-IDF 工程（main/device_main.c、components/exp_surface）
docs/      01–11 文档（简体中文）
scripts/   run_all_tests.sh · run_preview.sh · rf_check.py · rf_map_check.py · check_build.sh ·
           mac_env_audit.sh（只读）· bootstrap_workspace.sh（默认 dry-run）
evidence/  测试日志、截图（22 屏）、布局报告、RF 静态扫描结果
LICENSES/  字体许可证（Noto Sans CJK, SIL OFL 1.1）
versions.lock.json  上游提交与已知版本约束
```

## 快速开始（Host 预览）

```bash
./scripts/mac_env_audit.sh                  # 只读：查看 Mac 上缺什么（把输出交回）
./scripts/bootstrap_workspace.sh            # dry-run：显示将克隆的固定提交
./scripts/bootstrap_workspace.sh --apply    # 克隆官方 esp-mosaico-vibe（含子模块）到 ./workspace/esp-mosaico-vibe（固定提交，不碰现有 ESP-IDF）
export MOSAICO_ENGINE_ROOT=<...>/esp-mosaico-vibe/submodule/raylib-lite-engine   # bootstrap 之后通常可自动找到
./scripts/run_all_tests.sh                  # 全部 Host 检查
./scripts/run_preview.sh                    # 本机浏览器交互预览（127.0.0.1）
```

## 重要约束

* **RF 最优先**：展示固件、启动流程及扩展设备不得启用/请求/执行任何无线功能。已知冲突：官方 Vibe Mode 固件启动即启用 Wi-Fi（见 `docs/06`）。
* 不修改上游 SDK/BSP，不覆盖现有 ESP-IDF，不改 eFuse，不改安全配置。
* **整片擦除**：默认不做；开发用的那块板上，用户明确要求后做过一次（07 §6）。
* 设备操作：原规定只通过 `python mosaico.py`；用户选择路线 B 后，明确允许用 `idf.py`/`esptool` 烧录（07 §6）。烧录必须写死端口并核对芯片型号/MAC（同时插着别的项目的 ESP32-S3 板）。
* 不伪造传感器/AI/性能数据；未测试项一律标“未执行”。

## 固件构建与烧录（摘要，详见 `docs/07`）

```bash
. ~/esp/esp-idf-mosaico-7b9cc1a/export.sh
cd firmware && idf.py build
python3 ../scripts/rf_map_check.py build/mosaico_experience.map sdkconfig
# 烧录：按住 Boot 插线进入下载模式，ls /dev/cu.usbmodem* 确认端口，然后：
cd build && python -m esptool --chip esp32s31 -p <端口> -b 921600 --before no-reset --after hard-reset --no-stub \
  write-flash --flash-mode dio --flash-size 16MB --flash-freq 80m 0x10000 mosaico_experience.bin   # 仅应用
```

## 下一步

1. Phase 3b 其余传感器：磁力计（含校准）、麦克风、扬声器/马达、扩展槽只读探测（未知/无线模块失败即关闭）；2. 运行时 RF 检查；3. RF 实测与日本法规确认；4. 可选的进一步性能优化（需批准可见变化）。
