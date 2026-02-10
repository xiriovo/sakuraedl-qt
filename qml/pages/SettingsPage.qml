import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../components"
import ".."

Item {
    id: settingsPage

    ScrollView {
        anchors.fill: parent
        anchors.margins: Theme.spacingLarge
        contentWidth: availableWidth

        ColumnLayout {
            width: parent.width
            spacing: Theme.spacingLarge

            Text {
                text: appController.translate("nav.settings")
                color: Theme.textPrimary
                font.pixelSize: Theme.fontSizeTitle
                font.bold: true
            }

            // Language setting
            Rectangle {
                Layout.fillWidth: true
                height: langCol.implicitHeight + 2 * Theme.spacingLarge
                color: Theme.bgCard
                radius: Theme.radiusMedium
                border.color: Theme.border

                ColumnLayout {
                    id: langCol
                    anchors.fill: parent
                    anchors.margins: Theme.spacingLarge
                    spacing: Theme.spacingMedium

                    Text {
                        text: appController.translate("settings.language")
                        color: Theme.textPrimary
                        font.pixelSize: Theme.fontSizeMedium
                        font.bold: true
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacingSmall

                        Repeater {
                            model: appController.availableLanguages()

                            delegate: Rectangle {
                                width: 90; height: 36; radius: Theme.radiusSmall
                                color: appController.currentLanguage() === index ?
                                           Theme.primary : (langBtnHover.containsMouse ? Theme.bgHover : Theme.bgInput)
                                border.color: appController.currentLanguage() === index ? Theme.primary : Theme.border

                                Text {
                                    anchors.centerIn: parent
                                    text: modelData
                                    color: appController.currentLanguage() === index ? "white" : Theme.textPrimary
                                    font.pixelSize: Theme.fontSizeSmall
                                }

                                MouseArea {
                                    id: langBtnHover; anchors.fill: parent; hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: appController.setLanguage(index)
                                }
                            }
                        }
                    }
                }
            }

            // Performance setting
            Rectangle {
                Layout.fillWidth: true
                height: perfCol.implicitHeight + 2 * Theme.spacingLarge
                color: Theme.bgCard
                radius: Theme.radiusMedium
                border.color: Theme.border

                ColumnLayout {
                    id: perfCol
                    anchors.fill: parent
                    anchors.margins: Theme.spacingLarge
                    spacing: Theme.spacingMedium

                    Text {
                        text: appController.translate("settings.performance")
                        color: Theme.textPrimary
                        font.pixelSize: Theme.fontSizeMedium
                        font.bold: true
                    }

                    RowLayout {
                        spacing: Theme.spacingMedium

                        Text {
                            text: "Low Performance Mode:"
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontSizeNormal
                        }

                        Switch {
                            checked: perfConfig.lowPerformanceMode
                            onCheckedChanged: perfConfig.lowPerformanceMode = checked
                        }
                    }

                    Text {
                        text: "CPU: " + perfConfig.cpuCores + " cores | RAM: auto-detected"
                        color: Theme.textMuted
                        font.pixelSize: Theme.fontSizeSmall
                    }
                }
            }

            // Log level
            Rectangle {
                Layout.fillWidth: true
                height: logCol.implicitHeight + 2 * Theme.spacingLarge
                color: Theme.bgCard
                radius: Theme.radiusMedium
                border.color: Theme.border

                ColumnLayout {
                    id: logCol
                    anchors.fill: parent
                    anchors.margins: Theme.spacingLarge
                    spacing: Theme.spacingMedium

                    Text {
                        text: appController.translate("settings.log_level")
                        color: Theme.textPrimary
                        font.pixelSize: Theme.fontSizeMedium
                        font.bold: true
                    }

                    ComboBox {
                        Layout.preferredWidth: 200
                        model: ["Debug", "Info", "Warning", "Error"]
                        currentIndex: 1
                        background: Rectangle { color: Theme.bgInput; border.color: Theme.border; radius: Theme.radiusSmall }
                    }
                }
            }

            // About
            Rectangle {
                Layout.fillWidth: true
                height: aboutCol.implicitHeight + 2 * Theme.spacingLarge
                color: Theme.bgCard
                radius: Theme.radiusMedium
                border.color: Theme.border

                ColumnLayout {
                    id: aboutCol
                    anchors.fill: parent
                    anchors.margins: Theme.spacingLarge
                    spacing: Theme.spacingSmall

                    Text {
                        text: "About"
                        color: Theme.textPrimary
                        font.pixelSize: Theme.fontSizeMedium
                        font.bold: true
                    }

                    Text { text: "SakuraEDL v3.0.0 (Qt/C++ Edition)"; color: Theme.textSecondary; font.pixelSize: Theme.fontSizeNormal }
                    Text { text: "Multi-platform Android device flashing tool"; color: Theme.textMuted; font.pixelSize: Theme.fontSizeSmall }
                    Text { text: "Built with Qt 6.10 + C++17"; color: Theme.textMuted; font.pixelSize: Theme.fontSizeSmall }
                }
            }

            Item { Layout.preferredHeight: Theme.spacingLarge }
        }
    }
}
