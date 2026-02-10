import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import ".."

Rectangle {
    id: card
    color: Theme.bgCard
    radius: Theme.radiusMedium
    border.color: Theme.border
    border.width: 1

    property string title: ""
    property var infoModel: ({})  // { key: value } pairs
    property bool connected: false

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacingMedium
        spacing: Theme.spacingSmall

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingSmall

            Text {
                text: card.title
                color: Theme.textPrimary
                font.pixelSize: Theme.fontSizeMedium
                font.bold: true
            }

            Item { Layout.fillWidth: true }

            // Connection indicator
            Rectangle {
                width: 10
                height: 10
                radius: 5
                color: card.connected ? Theme.success : Theme.error

                SequentialAnimation on opacity {
                    running: card.connected
                    loops: Animation.Infinite
                    NumberAnimation { to: 0.5; duration: 1000 }
                    NumberAnimation { to: 1.0; duration: 1000 }
                }
            }

            Text {
                text: card.connected ? appController.translate("status.connected")
                                     : appController.translate("status.disconnected")
                color: card.connected ? Theme.success : Theme.textMuted
                font.pixelSize: Theme.fontSizeSmall
            }
        }

        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: Theme.border
        }

        // Info rows
        GridLayout {
            Layout.fillWidth: true
            columns: 2
            columnSpacing: Theme.spacingMedium
            rowSpacing: Theme.spacingTiny

            Repeater {
                model: Object.keys(card.infoModel)

                delegate: RowLayout {
                    Layout.columnSpan: 1
                    spacing: Theme.spacingSmall

                    Text {
                        text: modelData + ":"
                        color: Theme.textMuted
                        font.pixelSize: Theme.fontSizeSmall
                        Layout.preferredWidth: 100
                    }
                    Text {
                        text: card.infoModel[modelData] || "-"
                        color: Theme.textPrimary
                        font.pixelSize: Theme.fontSizeSmall
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                }
            }
        }

        Item { Layout.fillHeight: true }
    }
}
