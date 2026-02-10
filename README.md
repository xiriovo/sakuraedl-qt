# SakuraEDL v3.0

**Multi-platform Android Flashing Tool**

Built with Qt 6 / QML + C++17. The UI is written in Qt Quick (QML) and can be visually edited directly in Qt Design Studio or Qt Creator.

---

## Features

### Qualcomm EDL 9008
- Sahara protocol (V1/V2/V3) auto-handshake & chip identification
- Firehose XML protocol: partition R/W, GPT management, IMEI R/W
- Cloud loader auto-matching
- VIP / OnePlus / Xiaomi auth strategies (automatic)

### MediaTek BROM
- XFlash + XML DA V6 protocol
- Smart DA loading & BROM exploit framework

### Spreadtrum / Unisoc
- HDLC + FDL protocol
- PAC firmware parsing & flashing, ISP eMMC access

### Fastboot
- Native USB Fastboot protocol
- Sparse image handling, payload.bin extraction
- Huawei / Honor device support

### General
- 6 languages: Chinese, English, Japanese, Korean, Russian, Spanish
- Modern dark-themed UI
- Static build single-exe deployment

---

## Screenshots

> _Coming soon_

---

## Build

### Requirements

| Dependency | Version |
|---|---|
| Qt | 6.10+ (static recommended) |
| CMake | 3.24+ |
| C++ Compiler | MinGW 13+ or MSVC 2022 |
| Ninja | recommended |
| libusb | 1.0 (static) |
| OpenSSL | 3.x (static) |

### Dynamic Build

```bash
cmake -B build -G Ninja \
    -DCMAKE_PREFIX_PATH="D:/QT/6.10.2/mingw_64" \
    -DSAKURA_STATIC_BUILD=OFF

cmake --build build --config Release
```

### Static Build (Single EXE)

```bash
# 1. Build Qt static first
cd qt-source
./configure -static -release -prefix D:/QT/6.10.2/mingw_64_static \
    -opensource -confirm-license -nomake examples -nomake tests

# 2. Configure project
cmake -B build_static -G Ninja \
    -DCMAKE_PREFIX_PATH="D:/QT/6.10.2/mingw_64_static" \
    -DSAKURA_STATIC_BUILD=ON

# 3. Build
cmake --build build_static --config Release
```

---

## Project Structure

```
src/
├── core/          — Logger, i18n, watchdog, performance config
├── transport/     — USB (libusb) & serial transport
├── common/        — GPT, sparse, CRC, HDLC, LZ4, ext4/EROFS parsers
├── qualcomm/      — Sahara, Firehose, Diag protocols + cloud loader
│   └── auth/      — VIP, OnePlus, Xiaomi auth strategies
├── mediatek/      — BROM, XFlash, XML DA + exploit framework
├── spreadtrum/    — FDL, HDLC, PAC parser + exploits
├── fastboot/      — Fastboot protocol, payload parser, Huawei support
└── app/           — Entry point + QML controllers

qml/
├── Main.qml       — Main window
├── Theme.qml      — Theme definitions
├── components/    — Reusable components
│   ├── DeviceInfoCard.qml
│   ├── FileSelector.qml
│   ├── LogPanel.qml
│   ├── PartitionTable.qml
│   ├── ProgressPanel.qml
│   └── SideNav.qml
└── pages/         — Platform pages
    ├── QualcommPage.qml
    ├── MediatekPage.qml
    ├── SpreadtrumPage.qml
    ├── FastbootPage.qml
    ├── AutoRootPage.qml
    └── SettingsPage.qml
```

---

## Architecture

Each chip platform module is compiled as a **static library** with a clean layered architecture:

| Layer | Responsibility |
|---|---|
| **Protocol** | Low-level device communication (packet framing, command/response) |
| **Service** | Business logic orchestration |
| **Controller** | QObject bridge between C++ backend and QML frontend |

### UI Development

All QML files (Main, Theme, components, pages) are registered in `qt_add_qml_module()`, so the UI can be **directly edited inside Qt**:

- **Qt Creator** — Open the CMake project, navigate to any `.qml` file under `src/app/`, and use the built-in Form Editor or code editor with live preview
- **Qt Design Studio** — Open the project for full visual drag-and-drop editing of all components and pages
- **QML Hot Reload** — Run the app and modify QML files; changes apply instantly without rebuilding

---

## License

This project is licensed under the [GNU General Public License v3.0](LICENSE).

---

## Acknowledgments

- [edl](https://github.com/bkerler/edl) — Qualcomm Firehose / Sahara reference implementation
- [libusb](https://libusb.info/) — USB communication library
- [Qt](https://www.qt.io/) — Cross-platform framework

---

## Contact

- Telegram: [@xiriery](https://t.me/xiriery)
