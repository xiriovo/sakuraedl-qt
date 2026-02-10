import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import ".."

Rectangle {
    id: sideNav
    color: Theme.bgSecondary

    property alias model: navRepeater.model
    property int currentIndex: 0
    signal navigated(int index)

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Logo header
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 64
            color: "transparent"

            RowLayout {
                anchors.centerIn: parent
                spacing: Theme.spacingSmall

                Text {
                    text: "\u{1F338}"
                    font.pixelSize: 24
                }
                Text {
                    text: "SakuraEDL"
                    color: Theme.primary
                    font.pixelSize: Theme.fontSizeLarge
                    font.bold: true
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            Layout.leftMargin: Theme.spacingMedium
            Layout.rightMargin: Theme.spacingMedium
            color: Theme.border
        }

        Item { Layout.preferredHeight: Theme.spacingSmall }

        // Navigation items
        Repeater {
            id: navRepeater

            delegate: Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 44
                Layout.leftMargin: Theme.spacingSmall
                Layout.rightMargin: Theme.spacingSmall
                radius: Theme.radiusMedium
                color: sideNav.currentIndex === index ? Theme.primary + "30" :
                       hoverArea.containsMouse ? Theme.bgHover : "transparent"

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: Theme.spacingMedium
                    anchors.rightMargin: Theme.spacingMedium
                    spacing: Theme.spacingMedium

                    Text {
                        text: model.icon
                        font.pixelSize: 18
                        color: sideNav.currentIndex === index ? Theme.primary : Theme.textSecondary
                    }

                    Text {
                        text: appController.translate(model.labelKey)
                        color: sideNav.currentIndex === index ? Theme.primary : Theme.textPrimary
                        font.pixelSize: Theme.fontSizeNormal
                        font.bold: sideNav.currentIndex === index
                        Layout.fillWidth: true
                    }
                }

                // Active indicator
                Rectangle {
                    width: 3
                    height: parent.height * 0.6
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    radius: 2
                    color: Theme.primary
                    visible: sideNav.currentIndex === index
                }

                MouseArea {
                    id: hoverArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: sideNav.navigated(index)
                }

                Behavior on color { ColorAnimation { duration: Theme.animFast } }
            }
        }

        Item { Layout.fillHeight: true }

        // Version info at bottom
        Text {
            Layout.alignment: Qt.AlignHCenter
            Layout.bottomMargin: Theme.spacingMedium
            text: "v3.0.0"
            color: Theme.textMuted
            font.pixelSize: Theme.fontSizeSmall
        }
    }
}
