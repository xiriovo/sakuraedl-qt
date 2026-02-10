# SakuraEDL v3.0

**多平台安卓设备刷机工具 | Multi-platform Android Flashing Tool**

使用 Qt 6 / QML + C++17 构建，UI 基于 Qt Quick，可直接在 Qt Design Studio 或 Qt Creator 中可视化编辑界面。

Built with Qt 6 / QML + C++17. The UI is written in Qt Quick (QML) and can be visually edited directly in Qt Design Studio or Qt Creator.

---

## 功能 | Features

### 高通 Qualcomm EDL 9008
- Sahara 协议 (V1/V2/V3) 自动握手与芯片识别
- Firehose XML 协议：分区读写、GPT 管理、IMEI 读写
- 云端 Loader 自动匹配 (Cloud Loader matching)
- VIP / OnePlus / Xiaomi 认证策略 (自动执行)
- Sahara protocol (V1/V2/V3) auto-handshake & chip identification
- Firehose XML protocol: partition R/W, GPT management, IMEI R/W
- Cloud loader auto-matching
- VIP / OnePlus / Xiaomi auth strategies (automatic)

### 联发科 MediaTek BROM
- XFlash + XML DA V6 协议
- DA 智能加载与 BROM exploit 框架
- XFlash + XML DA V6 protocol
- Smart DA loading & BROM exploit framework

### 展讯 Spreadtrum / Unisoc
- HDLC + FDL 协议
- PAC 固件解析与刷写、ISP eMMC 访问
- HDLC + FDL protocol
- PAC firmware parsing & flashing, ISP eMMC access

### Fastboot
- 原生 USB Fastboot 协议
- Sparse 镜像处理、payload.bin 提取
- 华为/荣耀设备支持
- Native USB Fastboot protocol
- Sparse image handling, payload.bin extraction
- Huawei / Honor device support

### 通用 | General
- 6 种语言：中文、英文、日文、韩文、俄文、西班牙文
- 6 languages: Chinese, English, Japanese, Korean, Russian, Spanish
- 现代深色 UI | Modern dark-themed UI
- 静态编译单文件部署 | Static build single-exe deployment

---

## 截图 | Screenshots

> _Coming soon_

---

## 构建 | Build

### 环境要求 | Requirements

| 依赖 Dependency | 版本 Version |
|---|---|
| Qt | 6.10+ (推荐静态版 / static recommended) |
| CMake | 3.24+ |
| C++ 编译器 Compiler | MinGW 13+ 或 MSVC 2022 |
| Ninja | 推荐 / recommended |
| libusb | 1.0 (静态库 / static) |
| OpenSSL | 3.x (静态库 / static) |

### 动态构建 | Dynamic Build

```bash
cmake -B build -G Ninja \
    -DCMAKE_PREFIX_PATH="D:/QT/6.10.2/mingw_64" \
    -DSAKURA_STATIC_BUILD=OFF

cmake --build build --config Release
```

### 静态构建（单文件）| Static Build (Single EXE)

```bash
# 1. 先构建 Qt 静态版 | Build Qt static first
cd qt-source
./configure -static -release -prefix D:/QT/6.10.2/mingw_64_static \
    -opensource -confirm-license -nomake examples -nomake tests

# 2. 配置项目 | Configure project
cmake -B build_static -G Ninja \
    -DCMAKE_PREFIX_PATH="D:/QT/6.10.2/mingw_64_static" \
    -DSAKURA_STATIC_BUILD=ON

# 3. 编译 | Build
cmake --build build_static --config Release
```

---

## 项目结构 | Project Structure

```
src/
├── core/          — 日志、国际化、看门狗 | Logger, i18n, watchdog
├── transport/     — USB (libusb) 与串口通信 | USB & serial transport
├── common/        — GPT、Sparse、CRC、HDLC、LZ4、ext4/EROFS 解析
├── qualcomm/      — Sahara、Firehose、Diag 协议 + 云 Loader
│   └── auth/      — VIP、OnePlus、Xiaomi 认证策略
├── mediatek/      — BROM、XFlash、XML DA + exploit
├── spreadtrum/    — FDL、HDLC、PAC 解析 + exploit
├── fastboot/      — Fastboot 协议、payload 解析、华为支持
└── app/           — 入口 + QML 控制器 | Entry + QML controllers

qml/
├── Main.qml       — 主界面 | Main window
├── Theme.qml      — 主题定义 | Theme definitions
├── components/    — 可复用组件 | Reusable components
│   ├── DeviceInfoCard.qml
│   ├── FileSelector.qml
│   ├── LogPanel.qml
│   ├── PartitionTable.qml
│   ├── ProgressPanel.qml
│   └── SideNav.qml
└── pages/         — 各平台页面 | Platform pages
    ├── QualcommPage.qml
    ├── MediatekPage.qml
    ├── SpreadtrumPage.qml
    ├── FastbootPage.qml
    ├── AutoRootPage.qml
    └── SettingsPage.qml
```

---

## 架构 | Architecture

每个芯片平台模块编译为**静态库**，采用清晰的分层架构：

Each chip platform module is compiled as a **static library** with a clean layered architecture:

| 层级 Layer | 职责 Responsibility |
|---|---|
| **Protocol** | 底层设备通信（数据帧、命令/响应）<br/>Low-level device communication (packet framing, command/response) |
| **Service** | 业务逻辑编排 <br/> Business logic orchestration |
| **Controller** | QObject 桥接 C++ 后端与 QML 前端 <br/> QObject bridge between C++ backend and QML frontend |

### UI 开发 | UI Development

界面使用 **Qt Quick (QML)** 编写，支持：

The UI is written in **Qt Quick (QML)** and supports:

- 在 **Qt Design Studio** 中可视化拖拽编辑
- 在 **Qt Creator** 中实时预览与代码编辑
- QML 热重载 (Hot Reload) 加速开发迭代
- Visual drag-and-drop editing in **Qt Design Studio**
- Live preview and code editing in **Qt Creator**
- QML Hot Reload for rapid development iteration

---

## 许可证 | License

本项目使用 [GNU General Public License v3.0](LICENSE) 开源协议。

This project is licensed under the [GNU General Public License v3.0](LICENSE).

---

## 致谢 | Acknowledgments

- [edl](https://github.com/bkerler/edl) — Qualcomm Firehose / Sahara 参考实现 | Reference implementation
- [libusb](https://libusb.info/) — USB 通信库 | USB communication library
- [Qt](https://www.qt.io/) — 跨平台框架 | Cross-platform framework

---

## 联系 | Contact

- Telegram: [@xiriery](https://t.me/xiriery)
