import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import ".."

Rectangle {
    id: progressPanel
    height: visible ? 50 : 0
    color: Theme.bgCard
    radius: Theme.radiusMedium
    visible: progress > 0

    property double progress: 0.0
    property string text: ""

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacingSmall
        spacing: Theme.spacingTiny

        Text {
            text: progressPanel.text
            color: Theme.textSecondary
            font.pixelSize: Theme.fontSizeSmall
        }

        Rectangle {
            Layout.fillWidth: true
            height: 6
            radius: 3
            color: Theme.bgInput

            Rectangle {
                width: parent.width * progressPanel.progress
                height: parent.height
                radius: 3
                color: Theme.primary

                Behavior on width {
                    NumberAnimation { duration: Theme.animNormal }
                }
            }
        }
    }
}
