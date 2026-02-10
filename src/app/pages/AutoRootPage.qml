import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../components"
import ".."

Item {
    id: rootPage

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacingLarge
        spacing: Theme.spacingLarge

        Text {
            text: "Auto Root"
            color: Theme.textPrimary
            font.pixelSize: Theme.fontSizeTitle
            font.bold: true
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Theme.bgCard
            radius: Theme.radiusMedium
            border.color: Theme.border

            ColumnLayout {
                anchors.centerIn: parent
                spacing: Theme.spacingLarge

                Text {
                    Layout.alignment: Qt.AlignHCenter
                    text: "\u2618"
                    font.pixelSize: 64
                    color: Theme.primary
                }

                Text {
                    Layout.alignment: Qt.AlignHCenter
                    text: "Auto Root Module"
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fontSizeLarge
                    font.bold: true
                }

                Text {
                    Layout.alignment: Qt.AlignHCenter
                    text: "Coming soon - Automatic rooting via boot.img patching"
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeNormal
                }

                Rectangle {
                    Layout.alignment: Qt.AlignHCenter
                    width: 200; height: 44; radius: Theme.radiusMedium
                    color: Theme.bgHover
                    border.color: Theme.border

                    Text {
                        anchors.centerIn: parent
                        text: "Under Development"
                        color: Theme.textMuted
                        font.pixelSize: Theme.fontSizeNormal
                    }
                }
            }
        }
    }
}
