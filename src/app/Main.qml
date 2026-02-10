import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

ApplicationWindow {
    id: root
    width: 1360
    height: 860
    minimumWidth: 1060
    minimumHeight: 640
    visible: true
    title: "SakuraEDL"
    color: bg0

    // ─── Palette ───────────────────────────────────────────────────────
    // Neutral slate tones — clean, not "AI neon"
    readonly property color bg0:    "#111318"   // deepest background
    readonly property color bg1:    "#171b22"   // sidebar / footer
    readonly property color bg2:    "#1d222b"   // cards
    readonly property color bg3:    "#252b37"   // inputs / hover
    readonly property color bg4:    "#2f3746"   // active hover
    readonly property color bdr:    "#2a3040"   // borders
    readonly property color bdr2:   "#363f52"   // lighter border

    readonly property color tx0:    "#d8dce5"   // primary text
    readonly property color tx1:    "#9ba3b5"   // secondary text
    readonly property color tx2:    "#636b7e"   // muted text
    readonly property color acc:    "#5b8def"   // accent (calm blue)
    readonly property color accH:   "#4a7bde"   // accent hover
    readonly property color accDim: "#3d5a9e"   // accent dimmed
    readonly property color green:  "#59b876"   // success
    readonly property color amber:  "#e0a145"   // warning
    readonly property color red:    "#d95757"   // error
    readonly property color purple: "#8b6ec0"   // auth

    // ─── i18n ──────────────────────────────────────────────────────────
    property int curLang: 0
    onCurLangChanged: { qualcommController.language = curLang; mediatekController.language = curLang; spreadtrumController.language = curLang; fastbootController.language = curLang }

    property var activeCtrl: curPage===0 ? qualcommController : curPage===1 ? mediatekController : curPage===2 ? spreadtrumController : curPage===3 ? fastbootController : qualcommController

    function t(key) {
        var zh = {
            "qualcomm":"高通", "mediatek":"联发科", "spreadtrum":"展讯",
            "fastboot":"Fastboot", "autoroot":"Root", "settings":"设置",
            "connect":"连接", "disconnect":"断开", "port":"端口",
            "loader":"引导", "clickLoader":"选择Loader...",
            "chip":"芯片", "serial":"序列号", "storage":"存储",
            "disconnected":"未连接", "readGpt":"读取分区", "autoLoader":"自动Loader",
            "readImei":"读取IMEI", "reboot":"重启", "powerOff":"关机", "fixGpt":"修复GPT",
            "flash":"刷写", "firmware":"固件",
            "partName":"分区", "partStart":"起始扇区", "partSize":"大小", "partLun":"LUN",
            "connectToRead":"连接设备后读取分区表",
            "log":"日志", "clear":"清除",
            "language":"语言", "about":"关于",
            "mtkTitle":"联发科平台", "mtkDesc":"BROM / Preloader / DA",
            "spdTitle":"展讯/紫光展锐", "spdDesc":"HDLC / FDL / PAC",
            "fbTitle":"Fastboot", "fbDesc":"USB Fastboot 协议",
            "rootTitle":"Root", "rootDesc":"boot.img 补丁",
            "rootSoon":"即将推出",
            "settingsTitle":"设置",
            "aboutLine1":"SakuraEDL v3.0  Qt/C++ 版",
            "aboutLine2":"多平台安卓设备工具",
            "aboutLine3":"Qt 6 + C++17 + MinGW 13"
        }
        var en = {
            "qualcomm":"Qualcomm", "mediatek":"MediaTek", "spreadtrum":"Spreadtrum",
            "fastboot":"Fastboot", "autoroot":"Root", "settings":"Settings",
            "connect":"Connect", "disconnect":"Disconnect", "port":"Port",
            "loader":"Loader", "clickLoader":"Select loader...",
            "chip":"Chip", "serial":"Serial", "storage":"Storage",
            "disconnected":"Disconnected", "readGpt":"Read GPT", "autoLoader":"Auto Loader",
            "readImei":"Read IMEI", "reboot":"Reboot", "powerOff":"Power Off", "fixGpt":"Fix GPT",
            "flash":"Flash", "firmware":"Firmware",
            "partName":"Name", "partStart":"Start", "partSize":"Size", "partLun":"LUN",
            "connectToRead":"Connect device to read partitions",
            "log":"Log", "clear":"Clear",
            "language":"Language", "about":"About",
            "mtkTitle":"MediaTek", "mtkDesc":"BROM / Preloader / DA",
            "spdTitle":"Spreadtrum / Unisoc", "spdDesc":"HDLC / FDL / PAC",
            "fbTitle":"Fastboot", "fbDesc":"USB Fastboot protocol",
            "rootTitle":"Root", "rootDesc":"Automatic boot.img patching",
            "rootSoon":"Coming Soon",
            "settingsTitle":"Settings",
            "aboutLine1":"SakuraEDL v3.0  Qt/C++ Edition",
            "aboutLine2":"Multi-platform Android flashing tool",
            "aboutLine3":"Qt 6 + C++17 + MinGW 13"
        }
        return (curLang===0 ? zh : en)[key] || key
    }

    // ─── Nav ───────────────────────────────────────────────────────────
    property int curPage: 0
    property var navItems: [
        {label: "Qualcomm",   zh: "高通",    ico: "Q"},
        {label: "MediaTek",   zh: "联发科",  ico: "M"},
        {label: "Spreadtrum", zh: "展讯",    ico: "S"},
        {label: "Fastboot",   zh: "Fastboot", ico: "F"},
        {label: "Root",       zh: "Root",    ico: "R"},
        {label: "Settings",   zh: "设置",    ico: "\u2699"}
    ]

    // ─── Reusable button component ────────────────────────────────────
    component Btn : Rectangle {
        id: _btn
        property string label: ""
        property bool enabled: true
        property bool primary: false
        property bool danger: false
        signal clicked()

        height: 32; radius: 6
        color: {
            if (!enabled) return Qt.rgba(bg3.r, bg3.g, bg3.b, 0.4)
            if (danger) return _bma.containsMouse ? "#5a2828" : "#4a2020"
            if (primary) return _bma.containsMouse ? accH : acc
            return _bma.containsMouse ? bg4 : bg3
        }
        border.color: {
            if (!enabled) return "transparent"
            if (danger) return red
            if (primary) return "transparent"
            return bdr2
        }
        opacity: enabled ? 1.0 : 0.45

        Text {
            anchors.centerIn: parent
            text: _btn.label
            color: {
                if (!_btn.enabled) return tx2
                if (_btn.primary) return "#ffffff"
                if (_btn.danger) return red
                return tx0
            }
            font.pixelSize: 12; font.weight: _btn.primary ? Font.DemiBold : Font.Normal
        }
        MouseArea {
            id: _bma; anchors.fill: parent; hoverEnabled: true
            cursorShape: _btn.enabled ? Qt.PointingHandCursor : Qt.ForbiddenCursor
            onClicked: if (_btn.enabled) _btn.clicked()
        }
        Behavior on color { ColorAnimation { duration: 80 } }
    }

    // ─── Reusable file-pick button ────────────────────────────────────
    component FilePick : Rectangle {
        property string label: ""
        property bool ready: false
        signal clicked()
        width: Math.max(82, _fpt.implicitWidth + 26); height: 28; radius: 5
        color: {
            if (ready) return Qt.rgba(green.r, green.g, green.b, 0.12)
            return _fpm.containsMouse ? bg4 : bg3
        }
        border.color: ready ? Qt.rgba(green.r, green.g, green.b, 0.5) : bdr
        Row { anchors.centerIn: parent; spacing: 4
            Text { text: ready ? "\u2713" : ""; color: green; font.pixelSize: 11; visible: ready; anchors.verticalCenter: parent.verticalCenter }
            Text { id: _fpt; text: label; color: ready ? green : tx1; font.pixelSize: 11; anchors.verticalCenter: parent.verticalCenter }
        }
        MouseArea { id: _fpm; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: parent.clicked() }
    }

    // ─── Reusable chip toggle ─────────────────────────────────────────
    component ChipToggle : Rectangle {
        property string label: ""
        property bool active: false
        property color activeColor: acc
        signal clicked()
        width: Math.max(48, _ct.implicitWidth + 18); height: 24; radius: 4
        color: active ? activeColor : (_ctm.containsMouse ? bg4 : bg3)
        border.color: active ? activeColor : bdr
        Text { id: _ct; anchors.centerIn: parent; text: label; color: active ? "#fff" : tx1; font.pixelSize: 11 }
        MouseArea { id: _ctm; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: parent.clicked() }
    }

    // ─── Reusable checkbox toggle ─────────────────────────────────────
    component ChkToggle : Rectangle {
        property string label: ""
        property bool checked: false
        signal toggled()
        width: _chkRow.implicitWidth + 10; height: 24; radius: 4; color: _chkMa.containsMouse ? bg4 : "transparent"
        Row { id: _chkRow; anchors.centerIn: parent; spacing: 4
            Rectangle { width: 14; height: 14; radius: 3; anchors.verticalCenter: parent.verticalCenter
                color: checked ? acc : "transparent"; border.color: checked ? acc : tx2; border.width: 1.5
                Text { anchors.centerIn: parent; text: "\u2713"; color: "#fff"; font.pixelSize: 9; font.bold: true; visible: checked }
            }
            Text { text: label; color: tx1; font.pixelSize: 11; anchors.verticalCenter: parent.verticalCenter }
        }
        MouseArea { id: _chkMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: toggled() }
    }

    // ─── Status dot ───────────────────────────────────────────────────
    component StatusDot : Rectangle {
        property int state: 0 // 0=off, 1=watching, 2=connected
        width: 8; height: 8; radius: 4
        color: state === 2 ? green : state === 1 ? amber : tx2
        SequentialAnimation on opacity {
            running: state === 1; loops: Animation.Infinite
            NumberAnimation { to: 0.3; duration: 700; easing.type: Easing.InOutSine }
            NumberAnimation { to: 1.0; duration: 700; easing.type: Easing.InOutSine }
        }
    }

    // ═══════════════════════════════════════════════════════════════════
    // LAYOUT
    // ═══════════════════════════════════════════════════════════════════
    RowLayout {
        anchors.fill: parent; spacing: 0

        // ─── SIDEBAR ──────────────────────────────────────────────────
        Rectangle {
            Layout.fillHeight: true; Layout.preferredWidth: 170; color: bg1
            ColumnLayout {
                anchors.fill: parent; spacing: 0

                // Logo
                Item { Layout.fillWidth: true; Layout.preferredHeight: 54
                    Row { anchors.centerIn: parent; spacing: 6
                        Rectangle { width: 24; height: 24; radius: 6; color: acc; anchors.verticalCenter: parent.verticalCenter
                            Text { anchors.centerIn: parent; text: "S"; color: "#fff"; font.pixelSize: 13; font.bold: true }
                        }
                        Text { text: "SakuraEDL"; color: tx0; font.pixelSize: 15; font.weight: Font.DemiBold; anchors.verticalCenter: parent.verticalCenter }
                    }
                }
                Rectangle { Layout.fillWidth: true; height: 1; color: bdr; Layout.leftMargin: 14; Layout.rightMargin: 14 }
                Item { Layout.preferredHeight: 8 }

                // Nav items
                Repeater {
                    model: navItems.length
                    Rectangle {
                        Layout.fillWidth: true; Layout.preferredHeight: 38
                        Layout.leftMargin: 8; Layout.rightMargin: 8; radius: 6
                        color: curPage === index ? Qt.rgba(acc.r, acc.g, acc.b, 0.12) : _nma.containsMouse ? bg3 : "transparent"

                        RowLayout {
                            anchors.fill: parent; anchors.leftMargin: 10; spacing: 10

                            // Letter badge instead of ugly emoji
                            Rectangle {
                                width: 26; height: 26; radius: 5
                                color: curPage === index ? acc : bg3
                                Text { anchors.centerIn: parent; text: navItems[index].ico; color: curPage === index ? "#fff" : tx2; font.pixelSize: 12; font.weight: Font.Medium }
                            }
                            Text {
                                text: curLang === 0 ? navItems[index].zh : navItems[index].label
                                color: curPage === index ? acc : tx1
                                font.pixelSize: 13; font.weight: curPage === index ? Font.DemiBold : Font.Normal
                                Layout.fillWidth: true
                            }
                        }

                        // Active indicator line
                        Rectangle {
                            width: 3; height: 18; radius: 2; color: acc
                            anchors.left: parent.left; anchors.leftMargin: 1; anchors.verticalCenter: parent.verticalCenter
                            visible: curPage === index
                        }
                        MouseArea { id: _nma; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: curPage = index }
                        Behavior on color { ColorAnimation { duration: 100 } }
                    }
                }
                Item { Layout.fillHeight: true }
                Text { Layout.alignment: Qt.AlignHCenter; Layout.bottomMargin: 12; text: "v3.0"; color: tx2; font.pixelSize: 10 }
            }
        }

        // Sidebar / content separator
        Rectangle { Layout.fillHeight: true; width: 1; color: bdr }

        // ─── MAIN CONTENT ─────────────────────────────────────────────
        ColumnLayout {
            Layout.fillWidth: true; Layout.fillHeight: true; spacing: 0

            StackLayout {
                Layout.fillWidth: true; Layout.fillHeight: true; currentIndex: curPage

                // ══ PAGE 0: Qualcomm ══════════════════════════════════
                Item {
                    Loader { id: xmlDlgLoader; active: false; sourceComponent: Component {
                        FileDialog { fileMode: FileDialog.OpenFiles; nameFilters: ["XML/GPT (*.xml *.bin *.gpt)", "All (*)"]
                            onAccepted: { var p=[]; for(var i=0;i<selectedFiles.length;i++) p.push(selectedFiles[i].toString().replace("file:///","")); qualcommController.loadMultipleFiles(p); xmlDlgLoader.active=false }
                            onRejected: xmlDlgLoader.active=false; Component.onCompleted: open() } }}
                    Loader { id: fwDlgLoader; active: false; sourceComponent: Component {
                        FolderDialog { onAccepted: { qualcommController.loadFirmwareDir(selectedFolder.toString().replace("file:///","")); fwDlgLoader.active=false }
                            onRejected: fwDlgLoader.active=false; Component.onCompleted: open() } }}
                    Loader { id: loaderDlgLoader; active: false; sourceComponent: Component {
                        FileDialog { nameFilters: ["Loader (*.mbn *.elf *.bin)", "All (*)"]
                            onAccepted: { qualcommController.loadLoader(selectedFile.toString().replace("file:///","")); loaderDlgLoader.active=false }
                            onRejected: loaderDlgLoader.active=false; Component.onCompleted: open() } }}
                    Loader { id: digestDlgLoader; active: false; sourceComponent: Component {
                        FileDialog { nameFilters: ["Digest (*.bin *.hex)", "All (*)"]
                            onAccepted: { var p=selectedFile.toString().replace("file:///",""); qualcommController.vipDigestPath=p; digestDlgLoader.active=false }
                            onRejected: digestDlgLoader.active=false; Component.onCompleted: open() } }}
                    Loader { id: signDlgLoader; active: false; sourceComponent: Component {
                        FileDialog { nameFilters: ["Signature (*.bin *.sig)", "All (*)"]
                            onAccepted: { var p=selectedFile.toString().replace("file:///",""); qualcommController.vipSignPath=p; signDlgLoader.active=false }
                            onRejected: signDlgLoader.active=false; Component.onCompleted: open() } }}
                    Loader { id: imgDlgLoader; active: false; sourceComponent: Component {
                        FileDialog { fileMode: FileDialog.OpenFiles; nameFilters: ["Images (*.img *.bin *.mbn *.raw *.sparse)", "All (*)"]
                            onAccepted: { var p=[]; for(var i=0;i<selectedFiles.length;i++) p.push(selectedFiles[i].toString().replace("file:///","")); qualcommController.assignImageFiles(p); imgDlgLoader.active=false }
                            onRejected: imgDlgLoader.active=false; Component.onCompleted: open() } }}

                    Rectangle {
                        anchors.fill: parent; color: bg0
                        ColumnLayout {
                            anchors.fill: parent; anchors.margins: 14; spacing: 10

                            // ── Toolbar row ──
                            RowLayout {
                                spacing: 8
                                StatusDot { state: qualcommController.connectionState===2 ? 2 : qualcommController.isWatching ? 1 : 0 }
                                Text { text: qualcommController.statusHint; color: qualcommController.connectionState===2 ? green : qualcommController.isWatching ? amber : tx1; font.pixelSize: 12 }

                                Btn { width: 56; label: t("disconnect"); danger: true; visible: qualcommController.connectionState===2; onClicked: qualcommController.disconnect() }
                                Btn { width: 48; label: curLang===0?"停止":"Stop"; danger: true; visible: qualcommController.isBusy; onClicked: qualcommController.stopOperation() }

                                Item { Layout.fillWidth: true }

                                FilePick { label: curLang===0?"引导":"Loader"; ready: qualcommController.loaderReady; onClicked: loaderDlgLoader.active=true }
                                FilePick { label: "XML/GPT"; ready: qualcommController.xmlReady; onClicked: xmlDlgLoader.active=true }
                                FilePick { label: curLang===0?"固件":"FW Dir"; onClicked: fwDlgLoader.active=true }
                            }

                            // ── Options row ──
                            RowLayout {
                                spacing: 6
                                ChipToggle { label: "UFS"; active: qualcommController.storageType==="ufs"; onClicked: qualcommController.storageType="ufs" }
                                ChipToggle { label: "eMMC"; active: qualcommController.storageType==="emmc"; onClicked: qualcommController.storageType="emmc" }
                                Rectangle { width: 1; height: 18; color: bdr }
                                ChipToggle { label: curLang===0?"无认证":"None"; active: qualcommController.authMode==="none"; onClicked: qualcommController.authMode="none" }
                                ChipToggle { label: "OnePlus"; activeColor: purple; active: qualcommController.authMode==="demacia"; onClicked: qualcommController.authMode="demacia" }
                                ChipToggle { label: "VIP"; activeColor: purple; active: qualcommController.authMode==="vip"; onClicked: qualcommController.authMode="vip" }
                                ChipToggle { label: "Xiaomi"; activeColor: purple; active: qualcommController.authMode==="xiaomi"; onClicked: qualcommController.authMode="xiaomi" }
                                Rectangle { width: 1; height: 18; color: bdr }
                                ChkToggle { label: curLang===0?"跳过引导":"SkipSahara"; checked: qualcommController.skipSahara; onToggled: qualcommController.skipSahara=!qualcommController.skipSahara }
                                ChkToggle { label: curLang===0?"自动重启":"AutoReboot"; checked: qualcommController.autoReboot; onToggled: qualcommController.autoReboot=!qualcommController.autoReboot }
                                ChkToggle { label: curLang===0?"保护分区":"Protect"; checked: qualcommController.protectPartitions; onToggled: qualcommController.protectPartitions=!qualcommController.protectPartitions }
                            }

                            // ── VIP auth row (auto-executed on connect) ──
                            RowLayout {
                                spacing: 8; visible: qualcommController.authMode==="vip"
                                Text { text: "Digest:"; color: tx2; font.pixelSize: 11 }
                                Rectangle { Layout.fillWidth: true; height: 26; radius: 4; color: bg3; border.color: bdr
                                    Text { anchors.fill: parent; anchors.margins: 4; color: qualcommController.vipDigestPath ? tx1 : tx3; font.pixelSize: 11; elide: Text.ElideMiddle
                                        text: qualcommController.vipDigestPath || (curLang===0?"点击选择摘要文件":"Click to select digest") }
                                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: digestDlgLoader.active=true }
                                }
                                Text { text: "Sign:"; color: tx2; font.pixelSize: 11 }
                                Rectangle { Layout.fillWidth: true; height: 26; radius: 4; color: bg3; border.color: bdr
                                    Text { anchors.fill: parent; anchors.margins: 4; color: qualcommController.vipSignPath ? tx1 : tx3; font.pixelSize: 11; elide: Text.ElideMiddle
                                        text: qualcommController.vipSignPath || (curLang===0?"点击选择签名文件":"Click to select signature") }
                                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: signDlgLoader.active=true }
                                }
                                Text { text: curLang===0?"连接时自动验证":"Auto on connect"; color: green; font.pixelSize: 10; font.italic: true }
                            }

                            // ── Main split: left panel + partition table ──
                            Item {
                                Layout.fillWidth: true; Layout.fillHeight: true

                                // LEFT panel
                                ColumnLayout {
                                    id: qcLeft; anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom; width: 250; spacing: 8

                                    // Device card
                                    Rectangle {
                                        Layout.fillWidth: true; Layout.preferredHeight: _qcCardCol.implicitHeight + 24; radius: 8; color: bg2; border.color: bdr
                                        ColumnLayout {
                                            id: _qcCardCol; anchors.fill: parent; anchors.margins: 12; spacing: 6
                                            RowLayout {
                                                Text { text: "Qualcomm EDL"; color: tx0; font.pixelSize: 13; font.weight: Font.DemiBold }
                                                Item { Layout.fillWidth: true }
                                                Rectangle { width: 7; height: 7; radius: 4; color: qualcommController.connectionState>=4 ? green : tx2 }
                                            }
                                            Rectangle { Layout.fillWidth: true; height: 1; color: bdr }
                                            GridLayout {
                                                Layout.fillWidth: true; columns: 2; columnSpacing: 10; rowSpacing: 4
                                                Text { text: curLang===0?"品牌:":"Brand:"; color: tx2; font.pixelSize: 11; Layout.preferredWidth: 56 }
                                                Text { text: qualcommController.deviceBrand || "-"; color: tx0; font.pixelSize: 11; Layout.fillWidth: true; elide: Text.ElideRight }
                                                Text { text: curLang===0?"型号:":"Model:"; color: tx2; font.pixelSize: 11; Layout.preferredWidth: 56 }
                                                Text { text: qualcommController.deviceModel || "-"; color: tx0; font.pixelSize: 11; Layout.fillWidth: true; elide: Text.ElideRight }
                                                Text { text: curLang===0?"芯片:":"Chip:"; color: tx2; font.pixelSize: 11; Layout.preferredWidth: 56 }
                                                Text { text: qualcommController.deviceChip || "-"; color: tx0; font.pixelSize: 11; Layout.fillWidth: true; elide: Text.ElideRight }
                                                Text { text: curLang===0?"序列号:":"Serial:"; color: tx2; font.pixelSize: 11; Layout.preferredWidth: 56 }
                                                Text { text: qualcommController.deviceSerial || "-"; color: tx0; font.pixelSize: 11; Layout.fillWidth: true; elide: Text.ElideRight }
                                                Text { text: curLang===0?"存储:":"Storage:"; color: tx2; font.pixelSize: 11; Layout.preferredWidth: 56 }
                                                Text { text: qualcommController.deviceStorage || "-"; color: tx0; font.pixelSize: 11; Layout.fillWidth: true; elide: Text.ElideRight }
                                            }
                                        }
                                    }

                                    // Actions
                                    GridLayout {
                                        Layout.fillWidth: true; columns: 2; columnSpacing: 6; rowSpacing: 6
                                        Btn { Layout.fillWidth: true; label: curLang===0?"读取分区表":"Read GPT"; enabled: qualcommController.loaderReady; onClicked: qualcommController.readPartitionTable() }
                                        Btn { Layout.fillWidth: true; label: curLang===0?"读取分区":"Read"; enabled: qualcommController.xmlReady && qualcommController.hasCheckedPartitions; onClicked: qualcommController.readPartitions() }
                                        Btn { Layout.fillWidth: true; label: curLang===0?"写入分区":"Write"; enabled: qualcommController.xmlReady && qualcommController.hasCheckedPartitions; onClicked: qualcommController.writePartitions() }
                                        Btn { Layout.fillWidth: true; label: curLang===0?"擦除分区":"Erase"; enabled: qualcommController.xmlReady && qualcommController.hasCheckedPartitions; onClicked: qualcommController.erasePartitions() }
                                        Btn { Layout.fillWidth: true; label: curLang===0?"选择镜像":"Images"; enabled: qualcommController.xmlReady && qualcommController.hasCheckedPartitions; onClicked: imgDlgLoader.active=true }
                                        Btn { Layout.fillWidth: true; label: curLang===0?"重启":"Reboot"; enabled: qualcommController.isDeviceReady; onClicked: qualcommController.reboot() }
                                        Btn { Layout.fillWidth: true; label: curLang===0?"切换槽位":"Slot"; enabled: qualcommController.isDeviceReady; onClicked: qualcommController.switchSlot("a") }
                                        Btn { Layout.fillWidth: true; label: "EDL"; enabled: qualcommController.isDeviceReady; onClicked: qualcommController.rebootToEdl() }
                                    }

                                    // Search
                                    Rectangle {
                                        Layout.fillWidth: true; height: 30; radius: 6; color: bg3; border.color: bdr
                                        RowLayout { anchors.fill: parent; anchors.leftMargin: 8; anchors.rightMargin: 8; spacing: 6
                                            Text { text: "\u2315"; font.pixelSize: 14; color: tx2 }
                                            TextInput { Layout.fillWidth: true; color: tx0; font.pixelSize: 12; clip: true
                                                onTextChanged: qualcommController.searchPartition(text)
                                            }
                                        }
                                    }

                                    Item { Layout.fillHeight: true }

                                    // Flash
                                    Btn {
                                        Layout.fillWidth: true; height: 40; primary: true; radius: 8
                                        label: curLang===0?"刷写固件":"Flash Firmware"
                                        enabled: qualcommController.xmlReady && qualcommController.hasCheckedPartitions && !qualcommController.isBusy
                                        onClicked: qualcommController.flashFirmwarePackage()
                                    }
                                }

                                // RIGHT: Partition table
                                Rectangle {
                                    anchors.left: qcLeft.right; anchors.leftMargin: 12; anchors.right: parent.right; anchors.top: parent.top; anchors.bottom: parent.bottom
                                    radius: 8; color: bg2; border.color: bdr; clip: true

                                    ColumnLayout {
                                        anchors.fill: parent; spacing: 0

                                        // Header
                                        Rectangle {
                                            Layout.fillWidth: true; height: 34; color: bg1; radius: 0
                                            // Top corners only
                                            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: parent.height / 2; color: bg1 }
                                            RowLayout {
                                                anchors.fill: parent; anchors.leftMargin: 8; anchors.rightMargin: 12; spacing: 0
                                                Rectangle { width: 22; height: 22; radius: 4; color: _saM.containsMouse ? bg4 : bg3; border.color: bdr
                                                    Text { anchors.centerIn: parent; text: qualcommController.firmwareEntryCount>0?"\u2611":"\u2610"; color: acc; font.pixelSize: 13 }
                                                    MouseArea { id: _saM; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                                        onClicked: qualcommController.selectAll(qualcommController.firmwareEntryCount===0) }
                                                }
                                                Item { width: 6 }
                                                Text { text: t("partName"); color: tx2; font.pixelSize: 11; font.weight: Font.Medium; Layout.preferredWidth: 120 }
                                                Text { text: t("partStart"); color: tx2; font.pixelSize: 11; font.weight: Font.Medium; Layout.preferredWidth: 90 }
                                                Text { text: t("partSize"); color: tx2; font.pixelSize: 11; font.weight: Font.Medium; Layout.preferredWidth: 65 }
                                                Text { text: "LUN"; color: tx2; font.pixelSize: 11; font.weight: Font.Medium; Layout.preferredWidth: 30 }
                                                Text { text: curLang===0?"文件":"File"; color: tx2; font.pixelSize: 11; font.weight: Font.Medium; Layout.preferredWidth: 120 }
                                                Text { text: curLang===0?"来源":"Source"; color: tx2; font.pixelSize: 11; font.weight: Font.Medium; Layout.fillWidth: true }
                                                Text { text: qualcommController.partitions.length>0 ? (qualcommController.firmwareEntryCount+"/"+qualcommController.partitions.length) : ""; color: acc; font.pixelSize: 11; font.weight: Font.Medium }
                                            }
                                        }
                                        Rectangle { Layout.fillWidth: true; height: 1; color: bdr }

                                        Item {
                                            Layout.fillWidth: true; Layout.fillHeight: true
                                            // Empty state
                                            Column {
                                                anchors.centerIn: parent; spacing: 10; visible: qualcommController.partitions.length===0
                                                Rectangle { width: 48; height: 48; radius: 12; color: bg3; anchors.horizontalCenter: parent.horizontalCenter
                                                    Text { anchors.centerIn: parent; text: "\u2630"; color: tx2; font.pixelSize: 22 }
                                                }
                                                Text { text: t("connectToRead"); color: tx2; font.pixelSize: 12; anchors.horizontalCenter: parent.horizontalCenter }
                                            }

                                            ListView {
                                                anchors.fill: parent; clip: true; visible: qualcommController.partitions.length>0
                                                model: qualcommController.partitions
                                                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                                                delegate: Rectangle {
                                                    required property int index
                                                    required property var modelData
                                                    width: ListView.view ? ListView.view.width : 100; height: 28
                                                    color: _prm.containsMouse ? bg3 : (index % 2 === 0 ? "transparent" : Qt.rgba(0,0,0,0.08))

                                                    RowLayout {
                                                        anchors.fill: parent; anchors.leftMargin: 8; anchors.rightMargin: 12; spacing: 0
                                                        Rectangle { width: 18; height: 18; radius: 4
                                                            color: modelData.checked ? Qt.rgba(acc.r,acc.g,acc.b,0.15) : bg3; border.color: modelData.checked ? acc : bdr; border.width: 1.5
                                                            Text { anchors.centerIn: parent; text: "\u2713"; color: acc; font.pixelSize: 10; font.bold: true; visible: modelData.checked }
                                                            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: qualcommController.togglePartition(index) }
                                                        }
                                                        Item { width: 6 }
                                                        Text { text: modelData.name||""; color: modelData.checked ? tx0 : tx2; font.pixelSize: 12; Layout.preferredWidth: 120; elide: Text.ElideRight }
                                                        Text { text: modelData.start||""; color: tx1; font.pixelSize: 11; font.family: "Consolas"; Layout.preferredWidth: 90 }
                                                        Text { text: modelData.size||""; color: tx1; font.pixelSize: 11; Layout.preferredWidth: 65 }
                                                        Text { text: modelData.lun||""; color: tx1; font.pixelSize: 11; Layout.preferredWidth: 30 }
                                                        Text { text: modelData.file||""; color: modelData.fileMissing?red:(modelData.file?tx1:tx2); font.pixelSize: 11; Layout.preferredWidth: 120; elide: Text.ElideMiddle; font.strikeout: modelData.fileMissing===true }
                                                        Text { text: modelData.sourceXml||""; color: tx2; font.pixelSize: 10; Layout.fillWidth: true; elide: Text.ElideMiddle }
                                                    }
                                                    MouseArea { id: _prm; anchors.fill: parent; hoverEnabled: true; acceptedButtons: Qt.NoButton }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                    Connections { target: qualcommController; function onLogMessage(msg) { lm.append({msg:msg}) } }
                }

                // ══ PAGE 1: MediaTek ══════════════════════════════════
                Item {
                    Loader { id: mtkDaDlg; active: false; sourceComponent: Component {
                        FileDialog { nameFilters: ["DA (MTK_AllInOne_DA*.bin *.img)", "All (*)"]
                            onAccepted: { mediatekController.loadDaFile(selectedFile.toString().replace("file:///","")); mtkDaDlg.active=false }
                            onRejected: mtkDaDlg.active=false; Component.onCompleted: open() } }}
                    Loader { id: mtkScatDlg; active: false; sourceComponent: Component {
                        FileDialog { nameFilters: ["Scatter (*.txt)", "All (*)"]
                            onAccepted: { mediatekController.loadScatterFile(selectedFile.toString().replace("file:///","")); mtkScatDlg.active=false }
                            onRejected: mtkScatDlg.active=false; Component.onCompleted: open() } }}
                    Loader { id: mtkFwDlg; active: false; sourceComponent: Component {
                        FolderDialog { onAccepted: { mediatekController.loadFirmwareDir(selectedFolder.toString().replace("file:///","")); mtkFwDlg.active=false }
                            onRejected: mtkFwDlg.active=false; Component.onCompleted: open() } }}

                    Rectangle { anchors.fill: parent; color: bg0
                    ColumnLayout { anchors.fill: parent; anchors.margins: 14; spacing: 10
                        RowLayout { spacing: 8
                            StatusDot { state: mediatekController.isDeviceReady ? 2 : mediatekController.isWatching ? 1 : 0 }
                            Text { text: mediatekController.statusHint; color: mediatekController.isDeviceReady?green:mediatekController.isWatching?amber:tx1; font.pixelSize: 12 }
                            Btn { width: 56; label: t("disconnect"); danger: true; visible: mediatekController.isDeviceReady; onClicked: mediatekController.disconnect() }
                            Btn { width: 48; label: curLang===0?"停止":"Stop"; danger: true; visible: mediatekController.isBusy; onClicked: mediatekController.stopOperation() }
                            Item { Layout.fillWidth: true }
                            FilePick { label: curLang===0?"选择DA":"DA"; ready: mediatekController.daReady; onClicked: mtkDaDlg.active=true }
                            FilePick { label: "Scatter"; ready: mediatekController.scatterReady; onClicked: mtkScatDlg.active=true }
                            FilePick { label: curLang===0?"固件":"FW Dir"; onClicked: mtkFwDlg.active=true }
                        }
                        Item { Layout.fillWidth: true; Layout.fillHeight: true
                            ColumnLayout { id: mtkLeft; anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom; width: 250; spacing: 8
                                Rectangle { Layout.fillWidth: true; implicitHeight: 90; radius: 8; color: bg2; border.color: bdr
                                    ColumnLayout { anchors.fill: parent; anchors.margins: 12; spacing: 6
                                        RowLayout { Text { text: "MediaTek BROM"; color: tx0; font.pixelSize: 13; font.weight: Font.DemiBold } Item { Layout.fillWidth: true }
                                            Rectangle { width: 7; height: 7; radius: 4; color: mediatekController.isDeviceReady?green:tx2 } }
                                        Rectangle { Layout.fillWidth: true; height: 1; color: bdr }
                                        GridLayout { columns: 2; columnSpacing: 10; rowSpacing: 4
                                            Text { text: curLang===0?"芯片:":"Chip:"; color: tx2; font.pixelSize: 11; Layout.preferredWidth: 56 }
                                            Text { text: mediatekController.chipName || "-"; color: tx0; font.pixelSize: 11; Layout.fillWidth: true; elide: Text.ElideRight }
                                            Text { text: "HW Code:"; color: tx2; font.pixelSize: 11; Layout.preferredWidth: 56 }
                                            Text { text: mediatekController.hwCode || "-"; color: tx0; font.pixelSize: 11; Layout.fillWidth: true; elide: Text.ElideRight }
                                        }
                                    }
                                }
                                GridLayout { Layout.fillWidth: true; columns: 2; columnSpacing: 6; rowSpacing: 6
                                    Btn { Layout.fillWidth: true; label: curLang===0?"读取分区表":"Read GPT"; enabled: mediatekController.daReady; onClicked: mediatekController.readPartitionTable() }
                                    Btn { Layout.fillWidth: true; label: curLang===0?"读取Flash":"Read Flash"; enabled: mediatekController.isDeviceReady&&mediatekController.hasCheckedPartitions; onClicked: mediatekController.readFlash() }
                                    Btn { Layout.fillWidth: true; label: curLang===0?"写入Flash":"Write Flash"; enabled: mediatekController.isDeviceReady&&mediatekController.hasCheckedPartitions; onClicked: mediatekController.writeFlash() }
                                    Btn { Layout.fillWidth: true; label: curLang===0?"擦除分区":"Erase"; enabled: mediatekController.isDeviceReady&&mediatekController.hasCheckedPartitions; onClicked: mediatekController.erasePartitions() }
                                    Btn { Layout.fillWidth: true; label: curLang===0?"格式化":"Format"; enabled: mediatekController.isDeviceReady; onClicked: mediatekController.formatAll() }
                                    Btn { Layout.fillWidth: true; label: "IMEI"; enabled: mediatekController.isDeviceReady; onClicked: mediatekController.readImei() }
                                    Btn { Layout.fillWidth: true; label: "NVRAM"; enabled: mediatekController.isDeviceReady; onClicked: mediatekController.readNvram() }
                                    Btn { Layout.fillWidth: true; label: curLang===0?"解锁":"Unlock BL"; enabled: mediatekController.isDeviceReady; onClicked: mediatekController.unlockBootloader() }
                                    Btn { Layout.fillWidth: true; label: curLang===0?"重启":"Reboot"; enabled: mediatekController.isDeviceReady; onClicked: mediatekController.reboot() }
                                }
                                Item { Layout.fillHeight: true }
                                Btn { Layout.fillWidth: true; height: 40; primary: true; radius: 8; label: curLang===0?"刷写固件":"Flash"
                                    enabled: mediatekController.hasCheckedPartitions&&!mediatekController.isBusy; onClicked: mediatekController.writeFlash() }
                            }
                            Rectangle { anchors.left: mtkLeft.right; anchors.leftMargin: 12; anchors.right: parent.right; anchors.top: parent.top; anchors.bottom: parent.bottom
                                radius: 8; color: bg2; border.color: bdr; clip: true
                                ColumnLayout { anchors.fill: parent; spacing: 0
                                    Rectangle { Layout.fillWidth: true; height: 34; color: bg1
                                        RowLayout { anchors.fill: parent; anchors.leftMargin: 8; anchors.rightMargin: 12; spacing: 0
                                            Rectangle { width: 22; height: 22; radius: 4; color: _mSM.containsMouse?bg4:bg3; border.color: bdr
                                                Text { anchors.centerIn: parent; text: mediatekController.firmwareEntryCount>0?"\u2611":"\u2610"; color: acc; font.pixelSize: 13 }
                                                MouseArea { id: _mSM; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: mediatekController.selectAll(mediatekController.firmwareEntryCount===0) } }
                                            Item{width:6} Text{text:t("partName");color:tx2;font.pixelSize:11;font.weight:Font.Medium;Layout.preferredWidth:120}
                                            Text{text:curLang===0?"起始":"Start";color:tx2;font.pixelSize:11;font.weight:Font.Medium;Layout.preferredWidth:90}
                                            Text{text:t("partSize");color:tx2;font.pixelSize:11;font.weight:Font.Medium;Layout.fillWidth:true}
                                        } }
                                    Rectangle{Layout.fillWidth:true;height:1;color:bdr}
                                    Item { Layout.fillWidth: true; Layout.fillHeight: true
                                        Column { anchors.centerIn: parent; spacing: 8; visible: mediatekController.partitions.length===0
                                            Rectangle { width: 48; height: 48; radius: 12; color: bg3; anchors.horizontalCenter: parent.horizontalCenter; Text { anchors.centerIn: parent; text: "M"; color: tx2; font.pixelSize: 20; font.weight: Font.Medium } }
                                            Text { text: curLang===0?"选择 DA/Scatter 加载":"Select DA/Scatter to load"; color: tx2; font.pixelSize: 12; anchors.horizontalCenter: parent.horizontalCenter } }
                                        ListView { anchors.fill: parent; clip: true; visible: mediatekController.partitions.length>0; model: mediatekController.partitions; ScrollBar.vertical: ScrollBar{policy:ScrollBar.AsNeeded}
                                            delegate: Rectangle { required property int index; required property var modelData; width: ListView.view?ListView.view.width:100; height: 28
                                                color: _mrm.containsMouse?bg3:(index%2===0?"transparent":Qt.rgba(0,0,0,0.08))
                                                RowLayout { anchors.fill: parent; anchors.leftMargin: 8; anchors.rightMargin: 12; spacing: 0
                                                    Rectangle { width: 18; height: 18; radius: 4; color: modelData.checked?Qt.rgba(acc.r,acc.g,acc.b,0.15):bg3; border.color: modelData.checked?acc:bdr; border.width: 1.5
                                                        Text{anchors.centerIn:parent;text:"\u2713";color:acc;font.pixelSize:10;font.bold:true;visible:modelData.checked}
                                                        MouseArea{anchors.fill:parent;cursorShape:Qt.PointingHandCursor;onClicked:mediatekController.togglePartition(index)} }
                                                    Item{width:6} Text{text:modelData.name||"";color:modelData.checked?tx0:tx2;font.pixelSize:12;Layout.preferredWidth:120;elide:Text.ElideRight}
                                                    Text{text:modelData.start||"";color:tx1;font.pixelSize:11;font.family:"Consolas";Layout.preferredWidth:90}
                                                    Text{text:modelData.size||"";color:tx1;font.pixelSize:11;Layout.fillWidth:true}
                                                } MouseArea{id:_mrm;anchors.fill:parent;hoverEnabled:true;acceptedButtons:Qt.NoButton} } }
                                    }
                                }
                            }
                        }
                    }}
                    Connections { target: mediatekController; function onLogMessage(msg) { lm.append({msg:msg}) } }
                }

                // ══ PAGE 2: Spreadtrum ════════════════════════════════
                Item {
                    Loader { id: spdPacDlg; active: false; sourceComponent: Component { FileDialog { nameFilters: ["PAC (*.pac)", "All (*)"]
                        onAccepted: { spreadtrumController.loadPacFile(selectedFile.toString().replace("file:///","")); spdPacDlg.active=false }
                        onRejected: spdPacDlg.active=false; Component.onCompleted: open() } }}
                    Loader { id: spdFdl1Dlg; active: false; sourceComponent: Component { FileDialog { nameFilters: ["FDL1 (*.bin *.img *.fdl)", "All (*)"]
                        onAccepted: { spreadtrumController.loadFdl1File(selectedFile.toString().replace("file:///","")); spdFdl1Dlg.active=false }
                        onRejected: spdFdl1Dlg.active=false; Component.onCompleted: open() } }}
                    Loader { id: spdFdl2Dlg; active: false; sourceComponent: Component { FileDialog { nameFilters: ["FDL2 (*.bin *.img *.fdl)", "All (*)"]
                        onAccepted: { spreadtrumController.loadFdl2File(selectedFile.toString().replace("file:///","")); spdFdl2Dlg.active=false }
                        onRejected: spdFdl2Dlg.active=false; Component.onCompleted: open() } }}
                    Loader { id: spdFwDlg; active: false; sourceComponent: Component { FolderDialog {
                        onAccepted: { spreadtrumController.loadFirmwareDir(selectedFolder.toString().replace("file:///","")); spdFwDlg.active=false }
                        onRejected: spdFwDlg.active=false; Component.onCompleted: open() } }}

                    Rectangle { anchors.fill: parent; color: bg0
                    ColumnLayout { anchors.fill: parent; anchors.margins: 14; spacing: 10
                        RowLayout { spacing: 8
                            StatusDot { state: spreadtrumController.isDeviceReady ? 2 : spreadtrumController.isWatching ? 1 : 0 }
                            Text { text: spreadtrumController.statusHint; color: spreadtrumController.isDeviceReady?green:spreadtrumController.isWatching?amber:tx1; font.pixelSize: 12 }
                            Btn { width: 56; label: t("disconnect"); danger: true; visible: spreadtrumController.isDeviceReady; onClicked: spreadtrumController.disconnect() }
                            Btn { width: 48; label: curLang===0?"停止":"Stop"; danger: true; visible: spreadtrumController.isBusy; onClicked: spreadtrumController.stopOperation() }
                            Item { Layout.fillWidth: true }
                            FilePick { label: curLang===0?"选择PAC":"PAC"; ready: spreadtrumController.pacReady; onClicked: spdPacDlg.active=true }
                            FilePick { label: "FDL1"; ready: spreadtrumController.fdl1Ready; onClicked: spdFdl1Dlg.active=true }
                            FilePick { label: "FDL2"; ready: spreadtrumController.fdl2Ready; onClicked: spdFdl2Dlg.active=true }
                            FilePick { label: curLang===0?"固件":"FW Dir"; onClicked: spdFwDlg.active=true }
                        }

                        // ── FDL address rows ──
                        RowLayout { spacing: 6
                            Text { text: "FDL1:"; color: tx2; font.pixelSize: 11; Layout.preferredWidth: 36 }
                            Rectangle { Layout.fillWidth: true; height: 26; radius: 4; color: bg3; border.color: bdr
                                TextInput { id: spdAddr1Input; anchors.fill: parent; anchors.margins: 4; color: tx0; font.pixelSize: 11; font.family: "Consolas"
                                    text: spreadtrumController.fdl1Address || "0x65000800"
                                    onTextChanged: spreadtrumController.fdl1Address = text
                                }
                            }
                            Text { text: "FDL2:"; color: tx2; font.pixelSize: 11; Layout.preferredWidth: 36 }
                            Rectangle { Layout.fillWidth: true; height: 26; radius: 4; color: bg3; border.color: bdr
                                TextInput { id: spdAddr2Input; anchors.fill: parent; anchors.margins: 4; color: tx0; font.pixelSize: 11; font.family: "Consolas"
                                    text: spreadtrumController.fdl2Address || "0x9EFFFE00"
                                    onTextChanged: spreadtrumController.fdl2Address = text
                                }
                            }
                        }

                        Item { Layout.fillWidth: true; Layout.fillHeight: true
                            ColumnLayout { id: spdLeft; anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom; width: 250; spacing: 8
                                Rectangle { Layout.fillWidth: true; implicitHeight: _spdInfoCol.implicitHeight + 24; radius: 8; color: bg2; border.color: bdr
                                    ColumnLayout { id: _spdInfoCol; anchors.fill: parent; anchors.margins: 12; spacing: 6
                                        RowLayout { Text { text: "Spreadtrum / Unisoc"; color: tx0; font.pixelSize: 13; font.weight: Font.DemiBold } Item { Layout.fillWidth: true }
                                            Rectangle { width: 7; height: 7; radius: 4; color: spreadtrumController.isDeviceReady?green:tx2 } }
                                        Rectangle { Layout.fillWidth: true; height: 1; color: bdr }
                                        GridLayout { columns: 2; columnSpacing: 10; rowSpacing: 4
                                            Text { text: curLang===0?"芯片:":"Chip:"; color: tx2; font.pixelSize: 11; Layout.preferredWidth: 56 }
                                            Text { text: spreadtrumController.chipName || "-"; color: tx0; font.pixelSize: 11; Layout.fillWidth: true; elide: Text.ElideRight }
                                            Text { text: "Flash:"; color: tx2; font.pixelSize: 11; Layout.preferredWidth: 56 }
                                            Text { text: spreadtrumController.flashType || "-"; color: tx0; font.pixelSize: 11; Layout.fillWidth: true; elide: Text.ElideRight }
                                        }
                                    } }
                                GridLayout { Layout.fillWidth: true; columns: 2; columnSpacing: 6; rowSpacing: 6
                                    Btn { Layout.fillWidth: true; label: curLang===0?"读取分区表":"Read GPT"; enabled: spreadtrumController.fdlReady||spreadtrumController.pacReady; onClicked: spreadtrumController.readPartitionTable() }
                                    Btn { Layout.fillWidth: true; label: curLang===0?"读取":"Read"; enabled: spreadtrumController.isDeviceReady&&spreadtrumController.hasCheckedPartitions; onClicked: spreadtrumController.readFlash() }
                                    Btn { Layout.fillWidth: true; label: curLang===0?"写入":"Write"; enabled: spreadtrumController.isDeviceReady&&spreadtrumController.hasCheckedPartitions; onClicked: spreadtrumController.writeFlash() }
                                    Btn { Layout.fillWidth: true; label: curLang===0?"擦除":"Erase"; enabled: spreadtrumController.isDeviceReady&&spreadtrumController.hasCheckedPartitions; onClicked: spreadtrumController.eraseFlash() }
                                    Btn { Layout.fillWidth: true; label: "IMEI"; enabled: spreadtrumController.isDeviceReady; onClicked: spreadtrumController.readImei() }
                                    Btn { Layout.fillWidth: true; label: "NV Read"; enabled: spreadtrumController.isDeviceReady; onClicked: spreadtrumController.readNv() }
                                    Btn { Layout.fillWidth: true; label: curLang===0?"解锁":"Unlock"; enabled: spreadtrumController.isDeviceReady; onClicked: spreadtrumController.unlockBootloader() }
                                    Btn { Layout.fillWidth: true; label: curLang===0?"重启":"Reboot"; enabled: spreadtrumController.isDeviceReady; onClicked: spreadtrumController.reboot() }
                                }
                                Item { Layout.fillHeight: true }
                                Btn { Layout.fillWidth: true; height: 40; primary: true; radius: 8; label: curLang===0?"刷写固件":"Flash"
                                    enabled: spreadtrumController.hasCheckedPartitions&&!spreadtrumController.isBusy; onClicked: spreadtrumController.flashPac() }
                            }
                            Rectangle { anchors.left: spdLeft.right; anchors.leftMargin: 12; anchors.right: parent.right; anchors.top: parent.top; anchors.bottom: parent.bottom
                                radius: 8; color: bg2; border.color: bdr; clip: true
                                ColumnLayout { anchors.fill: parent; spacing: 0
                                    Rectangle { Layout.fillWidth: true; height: 34; color: bg1
                                        RowLayout { anchors.fill: parent; anchors.leftMargin: 8; anchors.rightMargin: 12; spacing: 0
                                            Rectangle { width: 22; height: 22; radius: 4; color: _sSM.containsMouse?bg4:bg3; border.color: bdr
                                                Text { anchors.centerIn: parent; text: spreadtrumController.firmwareEntryCount>0?"\u2611":"\u2610"; color: acc; font.pixelSize: 13 }
                                                MouseArea { id: _sSM; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: spreadtrumController.selectAll(spreadtrumController.firmwareEntryCount===0) } }
                                            Item{width:6} Text{text:t("partName");color:tx2;font.pixelSize:11;font.weight:Font.Medium;Layout.preferredWidth:120}
                                            Text{text:t("partSize");color:tx2;font.pixelSize:11;font.weight:Font.Medium;Layout.preferredWidth:80}
                                            Text{text:curLang===0?"文件":"File";color:tx2;font.pixelSize:11;font.weight:Font.Medium;Layout.fillWidth:true}
                                        } }
                                    Rectangle{Layout.fillWidth:true;height:1;color:bdr}
                                    Item { Layout.fillWidth: true; Layout.fillHeight: true
                                        Column { anchors.centerIn: parent; spacing: 8; visible: spreadtrumController.partitions.length===0
                                            Rectangle { width: 48; height: 48; radius: 12; color: bg3; anchors.horizontalCenter: parent.horizontalCenter; Text { anchors.centerIn: parent; text: "S"; color: tx2; font.pixelSize: 20; font.weight: Font.Medium } }
                                            Text { text: curLang===0?"选择 PAC 固件":"Select PAC firmware"; color: tx2; font.pixelSize: 12; anchors.horizontalCenter: parent.horizontalCenter } }
                                        ListView { anchors.fill: parent; clip: true; visible: spreadtrumController.partitions.length>0; model: spreadtrumController.partitions; ScrollBar.vertical: ScrollBar{policy:ScrollBar.AsNeeded}
                                            delegate: Rectangle { required property int index; required property var modelData; width: ListView.view?ListView.view.width:100; height: 28
                                                color: _srm.containsMouse?bg3:(index%2===0?"transparent":Qt.rgba(0,0,0,0.08))
                                                RowLayout { anchors.fill: parent; anchors.leftMargin: 8; anchors.rightMargin: 12; spacing: 0
                                                    Rectangle { width: 18; height: 18; radius: 4; color: modelData.checked?Qt.rgba(acc.r,acc.g,acc.b,0.15):bg3; border.color: modelData.checked?acc:bdr; border.width: 1.5
                                                        Text{anchors.centerIn:parent;text:"\u2713";color:acc;font.pixelSize:10;font.bold:true;visible:modelData.checked}
                                                        MouseArea{anchors.fill:parent;cursorShape:Qt.PointingHandCursor;onClicked:spreadtrumController.togglePartition(index)} }
                                                    Item{width:6} Text{text:modelData.name||"";color:modelData.checked?tx0:tx2;font.pixelSize:12;Layout.preferredWidth:120;elide:Text.ElideRight}
                                                    Text{text:modelData.size||"";color:tx1;font.pixelSize:11;Layout.preferredWidth:80}
                                                    Text{text:modelData.pacFile||"";color:tx1;font.pixelSize:11;Layout.fillWidth:true;elide:Text.ElideMiddle}
                                                } MouseArea{id:_srm;anchors.fill:parent;hoverEnabled:true;acceptedButtons:Qt.NoButton} } }
                                    }
                                }
                            }
                        }
                    }}
                    Connections { target: spreadtrumController; function onLogMessage(msg) { lm.append({msg:msg}) } }
                }

                // ══ PAGE 3: Fastboot ══════════════════════════════════
                Item {
                    Loader { id: fbImgDlg; active: false; sourceComponent: Component { FileDialog { fileMode: FileDialog.OpenFiles; nameFilters: ["Images (*.img *.bin *.mbn)", "All (*)"]
                        onAccepted: { var p=[]; for(var i=0;i<selectedFiles.length;i++) p.push(selectedFiles[i].toString().replace("file:///","")); fastbootController.loadImages(p); fbImgDlg.active=false }
                        onRejected: fbImgDlg.active=false; Component.onCompleted: open() } }}
                    Loader { id: fbFwDlg; active: false; sourceComponent: Component { FolderDialog {
                        onAccepted: { fastbootController.loadFirmwareDir(selectedFolder.toString().replace("file:///","")); fbFwDlg.active=false }
                        onRejected: fbFwDlg.active=false; Component.onCompleted: open() } }}
                    Loader { id: fbPayDlg; active: false; sourceComponent: Component { FileDialog { nameFilters: ["Payload (payload.bin)", "All (*)"]
                        onAccepted: { fastbootController.loadPayload(selectedFile.toString().replace("file:///","")); fbPayDlg.active=false }
                        onRejected: fbPayDlg.active=false; Component.onCompleted: open() } }}

                    Rectangle { anchors.fill: parent; color: bg0
                    ColumnLayout { anchors.fill: parent; anchors.margins: 14; spacing: 10
                        RowLayout { spacing: 8
                            StatusDot { state: fastbootController.connected ? 2 : fastbootController.isWatching ? 1 : 0 }
                            Text { text: fastbootController.statusHint; color: fastbootController.connected?green:fastbootController.isWatching?amber:tx1; font.pixelSize: 12 }
                            Btn { width: 56; label: t("disconnect"); danger: true; visible: fastbootController.connected; onClicked: fastbootController.disconnect() }
                            Btn { width: 48; label: curLang===0?"停止":"Stop"; danger: true; visible: fastbootController.isBusy; onClicked: fastbootController.stopOperation() }
                            Item { Layout.fillWidth: true }
                            FilePick { label: curLang===0?"镜像":"Images"; onClicked: fbImgDlg.active=true }
                            FilePick { label: "Payload"; ready: fastbootController.payloadLoaded; onClicked: fbPayDlg.active=true }
                            FilePick { label: curLang===0?"固件":"FW Dir"; onClicked: fbFwDlg.active=true }
                        }
                        Item { Layout.fillWidth: true; Layout.fillHeight: true
                            ColumnLayout { id: fbLeft; anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom; width: 250; spacing: 8
                                Rectangle { Layout.fillWidth: true; implicitHeight: _fbInfoCol.implicitHeight + 24; radius: 8; color: bg2; border.color: bdr
                                    ColumnLayout { id: _fbInfoCol; anchors.fill: parent; anchors.margins: 12; spacing: 6
                                        RowLayout { Text { text: "Fastboot"; color: tx0; font.pixelSize: 13; font.weight: Font.DemiBold } Item { Layout.fillWidth: true }
                                            Rectangle { width: 7; height: 7; radius: 4; color: fastbootController.connected?green:tx2 } }
                                        Rectangle { Layout.fillWidth: true; height: 1; color: bdr }
                                        GridLayout { columns: 2; columnSpacing: 10; rowSpacing: 4
                                            Text { text: curLang===0?"产品:":"Product:"; color: tx2; font.pixelSize: 11; Layout.preferredWidth: 56 }
                                            Text { text: fastbootController.product || "-"; color: tx0; font.pixelSize: 11; Layout.fillWidth: true; elide: Text.ElideRight }
                                            Text { text: curLang===0?"序列号:":"Serial:"; color: tx2; font.pixelSize: 11; Layout.preferredWidth: 56 }
                                            Text { text: fastbootController.serialNo || "-"; color: tx0; font.pixelSize: 11; Layout.fillWidth: true; elide: Text.ElideRight }
                                            Text { text: curLang===0?"槽位:":"Slot:"; color: tx2; font.pixelSize: 11; Layout.preferredWidth: 56 }
                                            Text { text: fastbootController.currentSlot || "-"; color: tx0; font.pixelSize: 11; Layout.fillWidth: true }
                                            Text { text: curLang===0?"解锁:":"Unlock:"; color: tx2; font.pixelSize: 11; Layout.preferredWidth: 56 }
                                            Text { text: fastbootController.isUnlocked ? "Yes" : "No"; color: fastbootController.isUnlocked ? green : amber; font.pixelSize: 11; Layout.fillWidth: true }
                                        }
                                    } }
                                GridLayout { Layout.fillWidth: true; columns: 2; columnSpacing: 6; rowSpacing: 6
                                    Btn { Layout.fillWidth: true; label: curLang===0?"读取分区表":"Read GPT"; enabled: fastbootController.connected; onClicked: fastbootController.readPartitionTable() }
                                    Btn { Layout.fillWidth: true; label: curLang===0?"擦除数据":"Erase Data"; enabled: fastbootController.connected; onClicked: fastbootController.eraseUserdata() }
                                    Btn { Layout.fillWidth: true; label: curLang===0?"解锁BL":"Unlock BL"; enabled: fastbootController.connected; onClicked: fastbootController.unlockBootloader() }
                                    Btn { Layout.fillWidth: true; label: curLang===0?"锁定BL":"Lock BL"; enabled: fastbootController.connected; onClicked: fastbootController.lockBootloader() }
                                    Btn { Layout.fillWidth: true; label: curLang===0?"重启":"Reboot"; enabled: fastbootController.connected; onClicked: fastbootController.reboot() }
                                    Btn { Layout.fillWidth: true; label: "Bootloader"; enabled: fastbootController.connected; onClicked: fastbootController.rebootBootloader() }
                                    Btn { Layout.fillWidth: true; label: "Recovery"; enabled: fastbootController.connected; onClicked: fastbootController.rebootRecovery() }
                                    Btn { Layout.fillWidth: true; label: "Fastbootd"; enabled: fastbootController.connected; onClicked: fastbootController.rebootFastbootd() }
                                    Btn { Layout.fillWidth: true; label: "Slot A"; enabled: fastbootController.connected; onClicked: fastbootController.setActiveSlot("a") }
                                    Btn { Layout.fillWidth: true; label: "Slot B"; enabled: fastbootController.connected; onClicked: fastbootController.setActiveSlot("b") }
                                    Btn { Layout.fillWidth: true; label: "OEM Info"; enabled: fastbootController.connected; onClicked: fastbootController.oemCommand("device-info") }
                                }

                                // ── Custom Command ──
                                Rectangle { Layout.fillWidth: true; height: 28; radius: 4; color: bg3; border.color: bdr
                                    RowLayout { anchors.fill: parent; anchors.margins: 4; spacing: 4
                                        TextInput { id: fbCmdInput; Layout.fillWidth: true; color: tx0; font.pixelSize: 11; font.family: "Consolas"
                                            onAccepted: { fastbootController.customCommand(text); text="" }
                                            Text { anchors.fill: parent; text: curLang===0?"自定义命令 (如 oem unlock)":"Custom cmd (e.g. oem unlock)"; color: tx2; font: parent.font; visible: !parent.text && !parent.activeFocus }
                                        }
                                        Btn { width: 40; label: curLang===0?"执行":"Run"; enabled: fastbootController.connected&&fbCmdInput.text.length>0
                                            onClicked: { fastbootController.customCommand(fbCmdInput.text); fbCmdInput.text="" } }
                                    }
                                }

                                // ── Cloud URL ──
                                Rectangle { Layout.fillWidth: true; height: 28; radius: 4; color: bg3; border.color: bdr
                                    RowLayout { anchors.fill: parent; anchors.margins: 4; spacing: 4
                                        TextInput { id: fbUrlInput; Layout.fillWidth: true; color: tx0; font.pixelSize: 11; font.family: "Consolas"
                                            onAccepted: { fastbootController.parseCloudUrl(text) }
                                            Text { anchors.fill: parent; text: curLang===0?"云端固件URL":"Cloud firmware URL"; color: tx2; font: parent.font; visible: !parent.text && !parent.activeFocus }
                                        }
                                        Btn { width: 40; label: curLang===0?"解析":"Parse"; enabled: fbUrlInput.text.length>0
                                            onClicked: { fastbootController.parseCloudUrl(fbUrlInput.text) } }
                                    }
                                }

                                Item { Layout.fillHeight: true }
                                Btn { Layout.fillWidth: true; height: 40; primary: true; radius: 8; label: curLang===0?"刷写全部":"Flash All"
                                    enabled: fastbootController.connected&&fastbootController.hasCheckedPartitions&&!fastbootController.isBusy; onClicked: fastbootController.flashAll() }
                            }
                            Rectangle { anchors.left: fbLeft.right; anchors.leftMargin: 12; anchors.right: parent.right; anchors.top: parent.top; anchors.bottom: parent.bottom
                                radius: 8; color: bg2; border.color: bdr; clip: true
                                ColumnLayout { anchors.fill: parent; spacing: 0
                                    Rectangle { Layout.fillWidth: true; height: 34; color: bg1
                                        RowLayout { anchors.fill: parent; anchors.leftMargin: 8; anchors.rightMargin: 12; spacing: 0
                                            Rectangle { width: 22; height: 22; radius: 4; color: _fSM.containsMouse?bg4:bg3; border.color: bdr
                                                Text { anchors.centerIn: parent; text: fastbootController.hasCheckedPartitions?"\u2611":"\u2610"; color: acc; font.pixelSize: 13 }
                                                MouseArea { id: _fSM; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: fastbootController.selectAll(!fastbootController.hasCheckedPartitions) } }
                                            Item{width:6} Text{text:curLang===0?"分区":"Partition";color:tx2;font.pixelSize:11;font.weight:Font.Medium;Layout.preferredWidth:120}
                                            Text{text:t("partSize");color:tx2;font.pixelSize:11;font.weight:Font.Medium;Layout.preferredWidth:80}
                                            Text{text:curLang===0?"文件":"File";color:tx2;font.pixelSize:11;font.weight:Font.Medium;Layout.fillWidth:true}
                                        } }
                                    Rectangle{Layout.fillWidth:true;height:1;color:bdr}
                                    Item { Layout.fillWidth: true; Layout.fillHeight: true
                                        Column { anchors.centerIn: parent; spacing: 8; visible: fastbootController.partitions.length===0
                                            Rectangle { width: 48; height: 48; radius: 12; color: bg3; anchors.horizontalCenter: parent.horizontalCenter; Text { anchors.centerIn: parent; text: "F"; color: tx2; font.pixelSize: 20; font.weight: Font.Medium } }
                                            Text { text: curLang===0?"选择镜像或 Payload":"Select images or Payload"; color: tx2; font.pixelSize: 12; anchors.horizontalCenter: parent.horizontalCenter } }
                                        ListView { anchors.fill: parent; clip: true; visible: fastbootController.partitions.length>0; model: fastbootController.partitions; ScrollBar.vertical: ScrollBar{policy:ScrollBar.AsNeeded}
                                            delegate: Rectangle { required property int index; required property var modelData; width: ListView.view?ListView.view.width:100; height: 28
                                                color: _frm.containsMouse?bg3:(index%2===0?"transparent":Qt.rgba(0,0,0,0.08))
                                                RowLayout { anchors.fill: parent; anchors.leftMargin: 8; anchors.rightMargin: 12; spacing: 0
                                                    Rectangle { width: 18; height: 18; radius: 4; color: modelData.checked?Qt.rgba(acc.r,acc.g,acc.b,0.15):bg3; border.color: modelData.checked?acc:bdr; border.width: 1.5
                                                        Text{anchors.centerIn:parent;text:"\u2713";color:acc;font.pixelSize:10;font.bold:true;visible:modelData.checked}
                                                        MouseArea{anchors.fill:parent;cursorShape:Qt.PointingHandCursor;onClicked:fastbootController.togglePartition(index)} }
                                                    Item{width:6} Text{text:modelData.name||"";color:modelData.checked?tx0:tx2;font.pixelSize:12;Layout.preferredWidth:120;elide:Text.ElideRight}
                                                    Text{text:modelData.sizeStr||"";color:tx1;font.pixelSize:11;Layout.preferredWidth:80}
                                                    Text{text:(modelData.filePath||"")+"";color:tx1;font.pixelSize:11;Layout.fillWidth:true;elide:Text.ElideMiddle}
                                                } MouseArea{id:_frm;anchors.fill:parent;hoverEnabled:true;acceptedButtons:Qt.NoButton} } }
                                    }
                                }
                            }
                        }
                    }}
                    Connections { target: fastbootController; function onLogMessage(msg) { lm.append({msg:msg}) } }
                }

                // ══ PAGE 4: Auto Root ═════════════════════════════════
                Rectangle { color: bg0
                    Column { anchors.centerIn: parent; spacing: 14
                        Rectangle { width: 64; height: 64; radius: 16; color: bg2; border.color: bdr; anchors.horizontalCenter: parent.horizontalCenter
                            Text { anchors.centerIn: parent; text: "R"; color: acc; font.pixelSize: 28; font.weight: Font.Medium } }
                        Text { text: t("rootSoon"); color: tx0; font.pixelSize: 16; font.weight: Font.DemiBold; anchors.horizontalCenter: parent.horizontalCenter }
                        Text { text: t("rootDesc"); color: tx2; font.pixelSize: 12; anchors.horizontalCenter: parent.horizontalCenter }
                    }
                }

                // ══ PAGE 5: Settings ══════════════════════════════════
                Rectangle { color: bg0
                    Flickable { anchors.fill: parent; anchors.margins: 14; contentHeight: _stCol.implicitHeight; clip: true
                        ColumnLayout { id: _stCol; width: parent.width; spacing: 16
                            Text { text: t("settingsTitle"); color: tx0; font.pixelSize: 18; font.weight: Font.DemiBold }
                            Rectangle { Layout.fillWidth: true; implicitHeight: _liCol.implicitHeight+28; radius: 8; color: bg2; border.color: bdr
                                ColumnLayout { id: _liCol; anchors.fill: parent; anchors.margins: 14; spacing: 10
                                    Text { text: t("language"); color: tx0; font.pixelSize: 13; font.weight: Font.Medium }
                                    Row { spacing: 8
                                        Repeater { model: [{l:"中文",i:0},{l:"English",i:1}]
                                            Rectangle { width: 80; height: 32; radius: 6
                                                color: curLang===modelData.i ? acc : (_lm3.containsMouse ? bg4 : bg3); border.color: curLang===modelData.i ? acc : bdr
                                                Text { anchors.centerIn: parent; text: modelData.l; color: curLang===modelData.i ? "#fff" : tx1; font.pixelSize: 12 }
                                                MouseArea { id: _lm3; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: curLang=modelData.i } } }
                                    }
                                }
                            }
                            Rectangle { Layout.fillWidth: true; implicitHeight: _abCol.implicitHeight+28; radius: 8; color: bg2; border.color: bdr
                                ColumnLayout { id: _abCol; anchors.fill: parent; anchors.margins: 14; spacing: 5
                                    Text { text: t("about"); color: tx0; font.pixelSize: 13; font.weight: Font.Medium }
                                    Text { text: t("aboutLine1"); color: tx1; font.pixelSize: 12 }
                                    Text { text: t("aboutLine2"); color: tx2; font.pixelSize: 12 }
                                    Text { text: t("aboutLine3"); color: tx2; font.pixelSize: 12 }
                                }
                            }
                        }
                    }
                }
            }

            // ═══ DUAL PROGRESS BAR ══════════════════════════════════════
            Rectangle {
                id: progBar
                Layout.fillWidth: true; Layout.preferredHeight: 64; color: bg2

                function getPartProg() {
                    var tp = activeCtrl.progress
                    if (tp <= 0) return 0
                    var n = Math.max(1, 1)
                    var perPart = 1.0 / n
                    var idx = Math.floor(tp / perPart)
                    var pp = (tp - idx * perPart) / perPart
                    return Math.min(pp, 1.0)
                }

                ColumnLayout {
                    anchors.fill: parent; anchors.leftMargin: 14; anchors.rightMargin: 14; anchors.topMargin: 6; anchors.bottomMargin: 6; spacing: 3

                    RowLayout { Layout.fillWidth: true; spacing: 8
                        Text { text: activeCtrl.progressText||(curLang===0?"就绪":"Ready"); color: activeCtrl.isBusy?tx0:tx2; font.pixelSize: 12; elide: Text.ElideRight; Layout.fillWidth: true }
                        Rectangle { visible: (activeCtrl.speedText||"")!==""; width: _spTx.implicitWidth+12; height: 18; radius: 4; color: Qt.rgba(acc.r,acc.g,acc.b,0.1); border.color: Qt.rgba(acc.r,acc.g,acc.b,0.3)
                            Text { id: _spTx; anchors.centerIn: parent; text: activeCtrl.speedText||""; color: acc; font.pixelSize: 10 } }
                        Text { visible: (activeCtrl.elapsedText||"")!==""; text: activeCtrl.elapsedText||""; color: tx1; font.pixelSize: 10 }
                        Text { visible: (activeCtrl.etaText||"")!==""&&(activeCtrl.etaText||"")!=="00:00"; text: "ETA "+(activeCtrl.etaText||""); color: amber; font.pixelSize: 10 }
                        Text { text: activeCtrl.isBusy?Math.round(activeCtrl.progress*100)+"%":"0%"; color: activeCtrl.isBusy?acc:tx2; font.pixelSize: 12; font.weight: Font.DemiBold }
                    }

                    // Bar 1: Total progress
                    RowLayout { Layout.fillWidth: true; spacing: 8
                        Text { text: curLang===0?"总体":"Total"; color: tx2; font.pixelSize: 10; Layout.preferredWidth: 30 }
                        Rectangle {
                            id: totalTrack; Layout.fillWidth: true; height: 6; radius: 3; color: bg3
                            Rectangle { height: parent.height; radius: 3; width: totalTrack.width * activeCtrl.progress; color: acc
                                Behavior on width { NumberAnimation { duration: 120; easing.type: Easing.OutQuad } } }
                        }
                    }

                    // Bar 2: Per-partition progress
                    RowLayout { Layout.fillWidth: true; spacing: 8
                        Text { text: curLang===0?"分区":"Part"; color: tx2; font.pixelSize: 10; Layout.preferredWidth: 30 }
                        Rectangle {
                            id: partTrack; Layout.fillWidth: true; height: 6; radius: 3; color: bg3
                            Rectangle { height: parent.height; radius: 3; width: partTrack.width * progBar.getPartProg(); color: "#48b0d6"
                                Behavior on width { NumberAnimation { duration: 80; easing.type: Easing.OutQuad } } }
                        }
                    }
                }
            }

            // ═══ LOG PANEL ════════════════════════════════════════════
            Rectangle { Layout.fillWidth: true; height: 1; color: bdr }
            Rectangle {
                Layout.fillWidth: true; Layout.preferredHeight: 140; color: bg1
                ColumnLayout { anchors.fill: parent; spacing: 0
                    Rectangle { Layout.fillWidth: true; height: 28; color: bg2
                        RowLayout { anchors.fill: parent; anchors.leftMargin: 14; anchors.rightMargin: 14
                            Text { text: t("log"); color: tx1; font.pixelSize: 12; font.weight: Font.Medium }
                            Item { Layout.fillWidth: true }
                            Rectangle { width: 50; height: 20; radius: 4; color: _clm.containsMouse ? bg4 : "transparent"; border.color: bdr
                                Text { anchors.centerIn: parent; text: t("clear"); color: tx2; font.pixelSize: 10 }
                                MouseArea { id: _clm; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: lm.clear() }
                            }
                        }
                    }
                    ListView { Layout.fillWidth: true; Layout.fillHeight: true; clip: true; model: ListModel { id: lm }
                        delegate: Text {
                            width: parent ? parent.width : 100; padding: 2; leftPadding: 14
                            text: msg; font.pixelSize: 11; font.family: "Consolas"; wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                            color: {
                                if (msg.indexOf("[OKAY]") >= 0 || msg.indexOf("[SUCCESS]") >= 0 || msg.indexOf("[DONE]") >= 0) return "#59b876"
                                if (msg.indexOf("[ERROR]") >= 0 || msg.indexOf("[FATAL]") >= 0) return "#d95757"
                                if (msg.indexOf("[FAIL]") >= 0 || msg.indexOf("[WARN]") >= 0 || msg.indexOf("[WARNING]") >= 0) return "#e0a145"
                                if (msg.indexOf("[INFO]") >= 0) return "#5b8def"
                                if (msg.indexOf("[DEBUG]") >= 0) return "#636b7e"
                                if (msg.indexOf("[SEND]") >= 0 || msg.indexOf("[TX]") >= 0) return "#8b6ec0"
                                if (msg.indexOf("[RECV]") >= 0 || msg.indexOf("[RX]") >= 0) return "#48b0d6"
                                if (msg.indexOf("[FLASH]") >= 0 || msg.indexOf("[WRITE]") >= 0) return "#e07845"
                                if (msg.indexOf("[READ]") >= 0 || msg.indexOf("[DUMP]") >= 0) return "#45c0b0"
                                if (msg.indexOf("[ERASE]") >= 0) return "#c0456e"
                                if (msg.indexOf("[AUTH]") >= 0 || msg.indexOf("[SLA]") >= 0) return "#8b6ec0"
                                if (msg.indexOf("[SAHARA]") >= 0 || msg.indexOf("[FIREHOSE]") >= 0) return "#6e9ed6"
                                if (msg.indexOf("[BROM]") >= 0 || msg.indexOf("[DA]") >= 0) return "#6ec078"
                                if (msg.indexOf("[FDL]") >= 0 || msg.indexOf("[BSL]") >= 0) return "#c0a86e"
                                if (msg.indexOf("[FASTBOOT]") >= 0) return "#6eb0c0"
                                return "#9ba3b5"
                            }
                        }
                        onCountChanged: Qt.callLater(function(){ positionViewAtEnd() })
                    }
                }
                Component.onCompleted: { var ts=new Date().toLocaleTimeString(); lm.append({msg:"["+ts+"] SakuraEDL v3.0 started"}); lm.append({msg:"["+ts+"] Qt/C++ Edition ready"}) }
            }
        }
    }

    // ═══ FOOTER ═══════════════════════════════════════════════════════
    footer: Rectangle { height: 24; color: bg1
        RowLayout { anchors.fill: parent; anchors.leftMargin: 14; anchors.rightMargin: 14
            Text { text: t("disconnected"); color: tx2; font.pixelSize: 10 }
            Item { Layout.fillWidth: true }
            Text { text: "SakuraEDL v3.0"; color: tx2; font.pixelSize: 10 }
        }
    }
}
