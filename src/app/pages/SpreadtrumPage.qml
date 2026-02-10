import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../components"
import ".."

Item {
    id: spdPage

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacingLarge
        spacing: Theme.spacingMedium

        // Top bar
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingMedium

            Text { text: "COM Port:"; color: Theme.textSecondary; font.pixelSize: Theme.fontSizeNormal }
            ComboBox {
                id: spdPortCombo; Layout.preferredWidth: 200
                model: spreadtrumController.detectPorts(); editable: true
                background: Rectangle { color: Theme.bgInput; border.color: Theme.border; radius: Theme.radiusSmall }
            }

            Rectangle {
                width: 100; height: 32; radius: Theme.radiusSmall
                color: spdConnHover.containsMouse ? Theme.primaryDark : Theme.primary
                Text {
                    anchors.centerIn: parent
                    text: spreadtrumController.deviceState === 0 ?
                              appController.translate("action.connect") :
                              appController.translate("action.disconnect")
                    color: "white"; font.pixelSize: Theme.fontSizeNormal
                }
                MouseArea {
                    id: spdConnHover; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        if (spreadtrumController.deviceState === 0)
                            spreadtrumController.connectDevice(spdPortCombo.currentText);
                        else
                            spreadtrumController.disconnect();
                    }
                }
            }

            Item { Layout.fillWidth: true }
        }

        // PAC file
        FileSelector {
            Layout.fillWidth: true
            label: "PAC File:"
            filter: "PAC Files (*.pac);;All Files (*)"
            onFileSelected: function(path) { spreadtrumController.loadPacFile(path); }
        }

        // Content
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.spacingMedium

            ColumnLayout {
                Layout.preferredWidth: 320
                Layout.fillHeight: true
                spacing: Theme.spacingMedium

                DeviceInfoCard {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 200
                    title: "Spreadtrum/Unisoc"
                    connected: spreadtrumController.deviceState >= 4
                    infoModel: spreadtrumController.deviceInfo
                }

                GridLayout {
                    Layout.fillWidth: true; columns: 2
                    columnSpacing: Theme.spacingSmall; rowSpacing: Theme.spacingSmall

                    Repeater {
                        model: [
                            { text: "Flash PAC", action: function() { spreadtrumController.flashPac(); } },
                            { text: "Read Parts", action: function() { spreadtrumController.readPartitions(); } },
                            { text: "Read IMEI", action: function() { spreadtrumController.readImei(); } },
                            { text: "Reboot", action: function() { spreadtrumController.reboot(); } }
                        ]
                        delegate: Rectangle {
                            Layout.fillWidth: true; height: 36; radius: Theme.radiusSmall
                            color: spdBtnHover.containsMouse ? Theme.bgHover : Theme.bgCard
                            border.color: Theme.border; border.width: 1
                            Text { anchors.centerIn: parent; text: modelData.text; color: Theme.textPrimary; font.pixelSize: Theme.fontSizeSmall }
                            MouseArea { id: spdBtnHover; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: modelData.action() }
                        }
                    }
                }

                // Flash button
                Rectangle {
                    Layout.fillWidth: true; height: 40; radius: Theme.radiusSmall
                    color: spdFlashHover.containsMouse ? Theme.primaryDark : Theme.primary
                    Text { anchors.centerIn: parent; text: appController.translate("action.flash"); color: "white"; font.pixelSize: Theme.fontSizeMedium; font.bold: true }
                    MouseArea { id: spdFlashHover; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: spreadtrumController.flashPac() }
                }

                Item { Layout.fillHeight: true }
            }

            PartitionTable {
                Layout.fillWidth: true
                Layout.fillHeight: true
                partitions: spreadtrumController.partitions
            }
        }

        ProgressPanel {
            Layout.fillWidth: true
            progress: spreadtrumController.progress
            text: spreadtrumController.progressText
        }
    }
}
