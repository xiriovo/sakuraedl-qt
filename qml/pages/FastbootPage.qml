import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick.Dialogs
import "../components"
import ".."

Item {
    id: fbPage

    // File dialog for single partition flash
    FileDialog {
        id: fbFlashFileDialog
        property string partitionName: ""
        title: qsTr("Select image for ") + partitionName
        nameFilters: ["Image files (*.img *.bin *.mbn)","All files (*)"]
        onAccepted: {
            if (partitionName.length > 0 && selectedFile.toString().length > 0) {
                var path = selectedFile.toString().replace("file:///", "")
                fastbootController.flashPartition(partitionName, path)
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacingLarge
        spacing: Theme.spacingMedium

        // Top bar
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingMedium

            Rectangle {
                width: 120; height: 32; radius: Theme.radiusSmall
                color: fbConnHover.containsMouse ? Theme.primaryDark : Theme.primary
                Text {
                    anchors.centerIn: parent
                    text: fastbootController.connected ?
                              appController.translate("action.disconnect") :
                              appController.translate("action.connect")
                    color: "white"; font.pixelSize: Theme.fontSizeNormal
                }
                MouseArea {
                    id: fbConnHover; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        if (fastbootController.connected) fastbootController.disconnect();
                        else fastbootController.connectDevice();
                    }
                }
            }

            Item { Layout.fillWidth: true }

            FileSelector {
                Layout.preferredWidth: 400
                label: "Payload:"
                filter: "Payload (payload.bin);;All Files (*)"
                onFileSelected: function(path) { fastbootController.loadPayload(path); }
            }
        }

        // Content
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.spacingMedium

            // Left: device info + actions
            ColumnLayout {
                Layout.preferredWidth: 320
                Layout.fillHeight: true
                spacing: Theme.spacingMedium

                DeviceInfoCard {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 200
                    title: "Fastboot"
                    connected: fastbootController.connected
                    infoModel: fastbootController.deviceInfo
                }

                // Flash single partition
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingSmall

                    Text { text: "Flash Partition:"; color: Theme.textSecondary; font.pixelSize: Theme.fontSizeNormal }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacingSmall

                        ComboBox {
                            id: fbPartCombo
                            Layout.fillWidth: true
                            model: ["boot", "recovery", "system", "vendor", "vbmeta", "dtbo", "super", "userdata"]
                            editable: true
                            background: Rectangle { color: Theme.bgInput; border.color: Theme.border; radius: Theme.radiusSmall }
                        }

                        Rectangle {
                            width: 70; height: 32; radius: Theme.radiusSmall
                            color: fbFlashOneHover.containsMouse ? Theme.primaryDark : Theme.primary
                            Text { anchors.centerIn: parent; text: "Flash"; color: "white"; font.pixelSize: Theme.fontSizeSmall }
                            MouseArea {
                                id: fbFlashOneHover; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    var partName = modelData.name || ""
                                    if (partName.length === 0) return
                                    fbFlashFileDialog.partitionName = partName
                                    fbFlashFileDialog.open()
                                }
                            }
                        }
                    }
                }

                // Action buttons
                GridLayout {
                    Layout.fillWidth: true; columns: 2
                    columnSpacing: Theme.spacingSmall; rowSpacing: Theme.spacingSmall

                    Repeater {
                        model: [
                            { text: "Reboot", action: function() { fastbootController.reboot(); } },
                            { text: "Bootloader", action: function() { fastbootController.rebootBootloader(); } },
                            { text: "Recovery", action: function() { fastbootController.rebootRecovery(); } },
                            { text: "Unlock BL", action: function() { fastbootController.unlockBootloader(); } },
                            { text: "Lock BL", action: function() { fastbootController.lockBootloader(); } },
                            { text: "Erase Data", action: function() { fastbootController.erasePartition("userdata"); } }
                        ]
                        delegate: Rectangle {
                            Layout.fillWidth: true; height: 36; radius: Theme.radiusSmall
                            color: fbBtnHover.containsMouse ? Theme.bgHover : Theme.bgCard
                            border.color: Theme.border; border.width: 1
                            Text { anchors.centerIn: parent; text: modelData.text; color: Theme.textPrimary; font.pixelSize: Theme.fontSizeSmall }
                            MouseArea { id: fbBtnHover; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: modelData.action() }
                        }
                    }
                }

                // Bat script
                FileSelector {
                    Layout.fillWidth: true
                    label: "Script:"
                    filter: "Scripts (*.bat *.sh *.txt);;All Files (*)"
                    onFileSelected: function(path) { fastbootController.loadBatScript(path); }
                }

                Rectangle {
                    Layout.fillWidth: true; height: 40; radius: Theme.radiusSmall
                    color: fbExecHover.containsMouse ? Theme.primaryDark : Theme.primary
                    Text { anchors.centerIn: parent; text: "Execute Script"; color: "white"; font.pixelSize: Theme.fontSizeMedium; font.bold: true }
                    MouseArea { id: fbExecHover; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: fastbootController.executeBatScript() }
                }

                Item { Layout.fillHeight: true }
            }

            // Right: partition list from getvar all
            PartitionTable {
                Layout.fillWidth: true
                Layout.fillHeight: true
                partitions: fastbootController.partitions
            }
        }

        ProgressPanel {
            Layout.fillWidth: true
            progress: fastbootController.progress
            text: fastbootController.progressText
        }
    }
}
