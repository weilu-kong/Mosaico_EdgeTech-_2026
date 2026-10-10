# 01 环境审计（Phase 0）

状态标签说明（全套文档统一使用，互不混用）：

| 标签 | 含义 |
|---|---|
| 计划 | 仅写在设计里，尚无代码 |
| 已编码 | 代码已写，未经编译或未经运行 |
| 已编译 | 在云端容器用 gcc/clang `-Werror` 编译通过 |
| 模拟器已验证 | 在 Host 模拟器（合成数据）里运行并通过测试 |
| 设备已验证 | 在真实 ESP-Mosaico 上运行并观察到 |
| RF已审计(静态) | 仅做过源码静态扫描，**不是 RF 测量，也不是法规结论** |
| 未执行 | 没有做过 |

**状态更新（Phase 3a）：Phase 0–2 时没有任何设备项，也没有烧录。现在已在一块 ESP-Mosaico 上烧录开发固件，并“设备已验证”了少量项目（识别芯片/Flash、显示、触摸有响应、帧耗时，见 §7、04、08）。其余设备项仍为“未执行”。**

## 1. 审计范围与方法

本次审计分两处进行，二者能力不同，不能混为一谈：

| 位置 | 能做 | 不能做 |
|---|---|---|
| 云端容器（Linux） | 克隆官方仓库并只读阅读、gcc/clang 编译、Python/Pillow、Playwright Chromium | 串口、USB、真机、macOS 工具链检查、访问 `dl.espressif.com` |
| 用户 Mac（经 `device_bash`，实际是 Linux 虚拟机挂载目录） | 读写已授权的两个文件夹 | 网络、串口、检查 macOS 本机工具（cmake/ninja/brew 等） |

因此：**Mac 本机工具链状态为“未执行”**。已提供只读脚本 `scripts/mac_env_audit.sh`，需要用户在 Mac 终端自行运行并把输出交回。

## 2. 已确认事实（带证据）

| 项目 | 结果 | 证据 | 状态 |
|---|---|---|---|
| Mac 架构 | darwin arm64 | `get_device_info` 返回 | 已确认 |
| 开发目录 | `/Users/kongweilu/Development/Mosaico EdgeTech 2026`，原本仅有 `.DS_Store` | `device_list_dir` | 已确认 |
| 已有 ESP-IDF 目录 | `~/esp/esp-idf-v6.1`、`esp-idf-v6.1_release`、`esp-idf-v6.0.2`、`esp-idf-master` | `device_list_dir ~/esp` | 已确认（只读，未改动） |
| 官方要求的 IDF 提交 | `7b9cc1ac79f865983f59bb8ff3ff43eb74ff1dbe`（描述为 `v6.2-dev-2221-g7b9cc1ac79f`） | `esp-mosaico-utils/esp-mosaico-recovery/firmware/recovery/README.md:62`；`prebuilt/recovery/manifest.json:2` | 已确认 |
| BSP 最低 IDF | ≥ 6.2 | `esp-mosaico-bsp` 组件 manifest | 已确认 |
| 该提交对象是否存在于本机 | 存在于 `~/esp/esp-idf-master` 的对象库中（该目录当前 HEAD 为 `4d59230d…`，并非该提交） | Mac 上 `git cat-file -e` | 已确认 |
| **ESP-IDF v6.1 是否足够** | **不够**（BSP 要求 ≥ 6.2） | 同上 | 已确认 |
| 目标芯片 | `esp32s31` | 官方 workspace | 已确认 |
| 官方设备操作入口 | `python mosaico.py`（仓库 AGENTS.md 规则） | `esp-mosaico-utils` | 已确认 |
| GSP 组件服务器 | `dl.espressif.com` 从云端不可达，GSP/GSPC **无法验证** | 连接被拒 | 未执行 |

## 3. 官方仓库版本锁定

见 `versions.lock.json`。云端检出的提交：

| 仓库 | 提交 |
|---|---|
| esp-mosaico-vibe | `95469e63b621fc6ece3b0e313af8c1ba59dcd066` |
| esp-mosaico-bsp | `a2c985ad698b3f04a21c63e006b43e7e948bc1d7` |
| esp-mosaico-utils | `fc63d43f3ac72aa54e701b07b07fbed07a16b3ca` |
| raylib-lite-engine | `ca3200a6b7f63e2bdea07851e19fdbd3a42eb742` |

这是云端观察到的提交；在 Mac 上 Phase 3 开始前应以 `scripts/bootstrap_workspace.sh`（默认 dry-run）重新核对。

## 4. 对 ESP-IDF 的处理决定

* 不覆盖、不修改、不 checkout 用户现有的任何 ESP-IDF 目录。
* Phase 3 若获批准，需要在**新的独立路径**用 worktree/clone 取得固定提交 `7b9cc1ac…`，并为其单独安装工具链。
* 安装工具链会下载大量文件，属于“安装软件”，需要在 Phase 3 开始前再次征得同意。**（已更新：用户批准 Phase 3 后已按 §7 安装。）**

## 5. 未执行 / 未验证项

* Mac 本机的 cmake、ninja、Python 版本（官方要求 ≥ 3.10）、串口驱动、Xcode CLT：未执行。
* 在 Mac 上运行预览：未执行（云端已验证，见 08）。
* 板子是否已连接、固件现状、eFuse 板型：未执行（Phase 3 才需要，且只会读，不会写 eFuse）。

## 6. 主要风险（详见 06、10）

**Vibe Mode / Recovery 固件在启动时会启动 Wi-Fi 并扫描**，与“展示固件不得启用任何无线功能”的最高优先级约束直接冲突。这是 Phase 3 的重大决策点，在用户决定之前不作选择。

## 7. Phase 3a 更新（Mac 环境与设备识别）

### 7.1 Mac 本机环境（用户在 Terminal 运行 `scripts/mac_env_audit.sh` 的输出）

| 项 | 结果 |
|---|---|
| 系统 / 架构 | macOS 26.6.2，arm64 |
| Python | 3.14.5（含 Pillow、pyserial、esptool） |
| 编译器 | Apple clang 21（会因 `-Werror=misleading-indentation` 拒绝上游引擎的一处缩进，已用不改上游的包装器绕过，见 10） |
| 构建工具 | cmake 4.2.3、ninja 1.13.2、git 2.50.1、node 26 |
| 串口节点 | 工程开发板为 `/dev/cu.usbmodem11401`（端口号在复位/重新枚举后可能变化）。另有一块**不属于本项目**的 ESP32-S3 开发板在 `/dev/cu.usbmodem11201`，**从未被写入** |

### 7.2 ESP-IDF（独立路径，不碰现有目录）

* 路径：`~/esp/esp-idf-mosaico-7b9cc1a`，由本地 `esp-idf-master` 克隆，检出 `7b9cc1ac79f865983f59bb8ff3ff43eb74ff1dbe`；子模块须先把 `origin` 改为 `https://github.com/espressif/esp-idf.git`（`.gitmodules` 用相对地址）。`./install.sh esp32s31` 成功，IDF 报告 6.2.0。
* `esp-idf-master` 的 HEAD 为 `4d59230ddff`，比固定提交多 866 个提交，且含未跟踪的 `examples/wifi_prov/`；这些**未被改动**。其他三个 IDF 目录同样未改动。

### 7.3 目标板识别（esptool v5.5.0，只读）

| 项 | 读到的值 | 状态 |
|---|---|---|
| 芯片 | ESP32-S31，revision v0.0，Wi-Fi 6 / BT 5.4 / IEEE 802.15.4 能力，双核 + LP 核，300 MHz | 设备已验证（esptool 输出） |
| MAC | `1c:29:04:d0:a3:36` | 设备已验证 |
| Flash | 16 MB（厂商 c8，型号 4018） | 设备已验证 |
| USB 模式 | ROM 下载模式下为 USB-OTG | 设备已验证 |
| 烧录前内容 | 官方 Mosaico 布局：bootloader 0x2000；分区表 0x9000（nvs 0xa000、otadata 0x10000、phy_init 0x12000、factory 0x20000 共 8000K、ui_apps 0x7f0000 3200K、system 0xb10000 3000K） | 设备已验证（只读读取前 256 KB） |

## 8. 仍未执行的环境项

* eFuse 板型（V1.0/V1.2）读取：未执行。
* 触摸 IC 型号读取：未执行。
