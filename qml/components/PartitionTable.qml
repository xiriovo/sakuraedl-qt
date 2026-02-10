import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import ".."

Rectangle {
    id: partTable
    color: Theme.bgCard
    radius: Theme.radiusMedium
    border.color: Theme.border
    border.width: 1

    property var partitions: []  // array of { name, start, size, lun }
    property int selectedIndex: -1
    signal partitionSelected(int index, string name)
    signal partitionDoubleClicked(int index, string name)

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 1
        spacing: 0

        // Header
        Rectangle {
            Layout.fillWidth: true
            height: 32
            color: Theme.bgSecondary
            radius: Theme.radiusMedium

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.spacingMedium
                anchors.rightMargin: Theme.spacingMedium
                spacing: 0

                Text {
                    text: appController.translate("partition.name")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeSmall
                    font.bold: true
                    Layout.preferredWidth: 180
                }
                Text {
                    text: appController.translate("partition.start")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeSmall
                    font.bold: true
                    Layout.preferredWidth: 120
                }
                Text {
                    text: appController.translate("partition.size")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeSmall
                    font.bold: true
                    Layout.preferredWidth: 120
                }
                Text {
                    text: appController.translate("partition.lun")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeSmall
                    font.bold: true
                    Layout.fillWidth: true
                }
            }
        }

        // Rows
        ListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: partTable.partitions

            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            delegate: Rectangle {
                width: ListView.view.width
                height: 30
                color: partTable.selectedIndex === index ? Theme.primary + "25" :
                       rowHover.containsMouse ? Theme.bgHover : "transparent"

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: Theme.spacingMedium
                    anchors.rightMargin: Theme.spacingMedium
                    spacing: 0

                    Text {
                        text: modelData.name || ""
                        color: Theme.textPrimary
                        font.pixelSize: Theme.fontSizeSmall
                        Layout.preferredWidth: 180
                        elide: Text.ElideRight
                    }
                    Text {
                        text: modelData.start !== undefined ? "0x" + modelData.start.toString(16).toUpperCase() : ""
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSizeSmall
                        font.family: "Consolas"
                        Layout.preferredWidth: 120
                    }
                    Text {
                        text: modelData.size || ""
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSizeSmall
                        Layout.preferredWidth: 120
                    }
                    Text {
                        text: modelData.lun !== undefined ? modelData.lun.toString() : ""
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSizeSmall
                        Layout.fillWidth: true
                    }
                }

                MouseArea {
                    id: rowHover
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: {
                        partTable.selectedIndex = index;
                        partTable.partitionSelected(index, modelData.name);
                    }
                    onDoubleClicked: {
                        partTable.partitionDoubleClicked(index, modelData.name);
                    }
                }
            }
        }
    }
}
