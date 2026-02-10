import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../components"
import ".."

Item {
    id: qcPage

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacingLarge
        spacing: Theme.spacingMedium

        // Top bar: port selection + connect
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingMedium

            Text {
                text: "COM Port:"
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeNormal
            }

            ComboBox {
                id: portCombo
                Layout.preferredWidth: 200
                model: qualcommController.detectPorts()
                editable: true

                background: Rectangle {
                    color: Theme.bgInput
                    border.color: Theme.border
                    radius: Theme.radiusSmall
                }
            }

            // Refresh ports
            Rectangle {
                width: 32; height: 32; radius: Theme.radiusSmall
                color: refreshHover.containsMouse ? Theme.bgHover : Theme.bgCard
                border.color: Theme.border

                Text { anchors.centerIn: parent; text: "\u21BB"; color: Theme.textSecondary; font.pixelSize: 16 }
                MouseArea {
                    id: refreshHover; anchors.fill: parent; hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: portCombo.model = qualcommController.detectPorts()
                }
            }

            // Connect button
            Rectangle {
                width: 100; height: 32; radius: Theme.radiusSmall
                color: connectHover.containsMouse ? Theme.primaryDark : Theme.primary

                Text {
                    anchors.centerIn: parent
                    text: qualcommController.connectionState === 0 ?
                              appController.translate("action.connect") :
                              appController.translate("action.disconnect")
                    color: "white"
                    font.pixelSize: Theme.fontSizeNormal
                }

                MouseArea {
                    id: connectHover; anchors.fill: parent; hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        if (qualcommController.connectionState === 0)
                            qualcommController.connectDevice(portCombo.currentText);
                        else
                            qualcommController.disconnect();
                    }
                }
            }

            Item { Layout.fillWidth: true }

            // Loader file
            FileSelector {
                Layout.preferredWidth: 350
                label: "Loader:"
                filter: "Loader Files (*.mbn *.elf *.bin);;All Files (*)"
                onFileSelected: function(path) { qualcommController.loadLoader(path); }
            }
        }

        // Main content: device info + partition table side by side
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.spacingMedium

            // Left: Device info + actions
            ColumnLayout {
                Layout.preferredWidth: 320
                Layout.fillHeight: true
                spacing: Theme.spacingMedium

                DeviceInfoCard {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 200
                    title: "Qualcomm EDL"
                    connected: qualcommController.connectionState >= 4
                    infoModel: qualcommController.deviceInfo
                }

                // Action buttons grid
                GridLayout {
                    Layout.fillWidth: true
                    columns: 2
                    columnSpacing: Theme.spacingSmall
                    rowSpacing: Theme.spacingSmall

                    Repeater {
                        model: [
                            { text: "Read GPT", action: function() { qualcommController.readPartitions(); } },
                            { text: "Auto Loader", action: function() { qualcommController.autoMatchLoader(); } },
                            { text: "Read IMEI", action: function() { qualcommController.readImei(); } },
                            { text: "Reboot", action: function() { qualcommController.reboot(); } },
                            { text: "Power Off", action: function() { qualcommController.powerOff(); } },
                            { text: "Fix GPT", action: function() {} }
                        ]

                        delegate: Rectangle {
                            Layout.fillWidth: true
                            height: 36
                            radius: Theme.radiusSmall
                            color: btnHover.containsMouse ? Theme.bgHover : Theme.bgCard
                            border.color: Theme.border
                            border.width: 1
                            opacity: qualcommController.isBusy ? 0.5 : 1.0

                            Text {
                                anchors.centerIn: parent
                                text: modelData.text
                                color: Theme.textPrimary
                                font.pixelSize: Theme.fontSizeSmall
                            }

                            MouseArea {
                                id: btnHover; anchors.fill: parent; hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: if (!qualcommController.isBusy) modelData.action()
                            }
                        }
                    }
                }

                // Firmware package
                FileSelector {
                    Layout.fillWidth: true
                    label: "Firmware:"
                    filter: "Firmware Package (*.zip *.7z *.tar);;All Files (*)"
                    onFileSelected: function(path) { qualcommController.loadFirmwarePackage(path); }
                }

                Rectangle {
                    Layout.fillWidth: true
                    height: 40
                    radius: Theme.radiusSmall
                    color: flashHover.containsMouse ? Theme.primaryDark : Theme.primary
                    opacity: qualcommController.isBusy ? 0.5 : 1.0

                    Text {
                        anchors.centerIn: parent
                        text: appController.translate("action.flash") + " Firmware"
                        color: "white"
                        font.pixelSize: Theme.fontSizeMedium
                        font.bold: true
                    }

                    MouseArea {
                        id: flashHover; anchors.fill: parent; hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: if (!qualcommController.isBusy) qualcommController.flashFirmwarePackage()
                    }
                }

                Item { Layout.fillHeight: true }
            }

            // Right: Partition table
            PartitionTable {
                Layout.fillWidth: true
                Layout.fillHeight: true
                partitions: qualcommController.partitions
            }
        }

        // Progress bar
        ProgressPanel {
            Layout.fillWidth: true
            progress: qualcommController.progress
            text: qualcommController.progressText
        }
    }
}
