import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick.Dialogs
import ".."

RowLayout {
    id: fileSelector
    spacing: Theme.spacingSmall

    property string label: ""
    property string filePath: ""
    property string filter: "All Files (*)"
    property string placeholder: "..."

    signal fileSelected(string path)

    Text {
        text: fileSelector.label
        color: Theme.textSecondary
        font.pixelSize: Theme.fontSizeNormal
        visible: fileSelector.label.length > 0
        Layout.preferredWidth: 80
    }

    Rectangle {
        Layout.fillWidth: true
        height: 32
        radius: Theme.radiusSmall
        color: Theme.bgInput
        border.color: Theme.border
        border.width: 1

        Text {
            anchors.fill: parent
            anchors.leftMargin: Theme.spacingSmall
            anchors.rightMargin: Theme.spacingSmall
            verticalAlignment: Text.AlignVCenter
            text: fileSelector.filePath || fileSelector.placeholder
            color: fileSelector.filePath ? Theme.textPrimary : Theme.textMuted
            font.pixelSize: Theme.fontSizeSmall
            elide: Text.ElideMiddle
        }
    }

    Rectangle {
        width: 70
        height: 32
        radius: Theme.radiusSmall
        color: browseHover.containsMouse ? Theme.primaryDark : Theme.primary

        Text {
            anchors.centerIn: parent
            text: appController.translate("action.browse")
            color: "white"
            font.pixelSize: Theme.fontSizeSmall
        }

        MouseArea {
            id: browseHover
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: fileDialog.open()
        }
    }

    FileDialog {
        id: fileDialog
        title: fileSelector.label
        nameFilters: [fileSelector.filter]
        onAccepted: {
            var path = selectedFile.toString().replace("file:///", "");
            fileSelector.filePath = path;
            fileSelector.fileSelected(path);
        }
    }
}
