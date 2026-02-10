import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../components"
import ".."

Item {
    id: mtkPage

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
                id: mtkPortCombo
                Layout.preferredWidth: 200
                model: mediatekController.detectPorts()
                editable: true
                background: Rectangle { color: Theme.bgInput; border.color: Theme.border; radius: Theme.radiusSmall }
            }

            Rectangle {
                width: 100; height: 32; radius: Theme.radiusSmall
                color: mtkConnHover.containsMouse ? Theme.primaryDark : Theme.primary
                Text {
                    anchors.centerIn: parent
                    text: mediatekController.deviceState === 0 ?
                              appController.translate("action.connect") :
                              appController.translate("action.disconnect")
                    color: "white"; font.pixelSize: Theme.fontSizeNormal
                }
                MouseArea {
                    id: mtkConnHover; anchors.fill: parent; hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        if (mediatekController.deviceState === 0)
                            mediatekController.connectDevice(mtkPortCombo.currentText);
                        else
                            mediatekController.disconnect();
                    }
                }
            }

            Item { Layout.fillWidth: true }

            Text { text: "Protocol:"; color: Theme.textSecondary; font.pixelSize: Theme.fontSizeNormal }
            ComboBox {
                Layout.preferredWidth: 120
                model: ["Auto", "XML V6", "XFlash"]
                currentIndex: mediatekController.protocolType
                onCurrentIndexChanged: mediatekController.protocolType = currentIndex
                background: Rectangle { color: Theme.bgInput; border.color: Theme.border; radius: Theme.radiusSmall }
            }
        }

        // DA file + Scatter file
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingLarge

            FileSelector {
                Layout.fillWidth: true
                label: "DA File:"
                filter: "DA Files (*.bin *.da);;All Files (*)"
                onFileSelected: function(path) { mediatekController.loadDaFile(path); }
            }

            FileSelector {
                Layout.fillWidth: true
                label: "Scatter:"
                filter: "Scatter Files (*.txt *.xml);;All Files (*)"
                onFileSelected: function(path) { mediatekController.loadScatterFile(path); }
            }
        }

        // Content
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.spacingMedium

            // Left panel
            ColumnLayout {
                Layout.preferredWidth: 320
                Layout.fillHeight: true
                spacing: Theme.spacingMedium

                DeviceInfoCard {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 200
                    title: "MediaTek"
                    connected: mediatekController.deviceState >= 6
                    infoModel: mediatekController.deviceInfo
                }

                GridLayout {
                    Layout.fillWidth: true
                    columns: 2
                    columnSpacing: Theme.spacingSmall
                    rowSpacing: Theme.spacingSmall

                    Repeater {
                        model: [
                            { text: "Read Partitions", action: function() { mediatekController.readPartitions(); } },
                            { text: "Format All", action: function() { mediatekController.formatAll(); } },
                            { text: "Reboot", action: function() { mediatekController.reboot(); } },
                            { text: "Read Flash", action: function() {} }
                        ]

                        delegate: Rectangle {
                            Layout.fillWidth: true; height: 36; radius: Theme.radiusSmall
                            color: mtkBtnHover.containsMouse ? Theme.bgHover : Theme.bgCard
                            border.color: Theme.border; border.width: 1
                            Text { anchors.centerIn: parent; text: modelData.text; color: Theme.textPrimary; font.pixelSize: Theme.fontSizeSmall }
                            MouseArea {
                                id: mtkBtnHover; anchors.fill: parent; hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: modelData.action()
                            }
                        }
                    }
                }

                Item { Layout.fillHeight: true }
            }

            PartitionTable {
                Layout.fillWidth: true
                Layout.fillHeight: true
                partitions: mediatekController.partitions
            }
        }

        ProgressPanel {
            Layout.fillWidth: true
            progress: mediatekController.progress
            text: mediatekController.progressText
        }
    }
}
