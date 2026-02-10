import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import ".."

Rectangle {
    id: logPanel
    color: Theme.bgSecondary

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Header
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 32
            color: Theme.bgCard

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.spacingMedium
                anchors.rightMargin: Theme.spacingMedium

                Text {
                    text: appController.translate("log.title")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeSmall
                    font.bold: true
                }

                Item { Layout.fillWidth: true }

                // Clear button
                Rectangle {
                    width: 60
                    height: 22
                    radius: Theme.radiusSmall
                    color: clearArea.containsMouse ? Theme.bgHover : "transparent"
                    border.color: Theme.border
                    border.width: 1

                    Text {
                        anchors.centerIn: parent
                        text: appController.translate("action.cancel")
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSizeSmall
                    }

                    MouseArea {
                        id: clearArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            logListView.model.clear();
                            appController.clearLog();
                        }
                    }
                }
            }
        }

        // Log list
        ListView {
            id: logListView
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: ListModel { id: logModel }

            ScrollBar.vertical: ScrollBar {
                policy: ScrollBar.AsNeeded
            }

            delegate: Text {
                width: logListView.width
                text: model.message
                color: Theme.logColor(model.level)
                font.pixelSize: Theme.fontSizeSmall
                font.family: "Consolas"
                padding: 2
                leftPadding: Theme.spacingMedium
                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
            }

            onCountChanged: {
                Qt.callLater(function() {
                    logListView.positionViewAtEnd();
                });
            }
        }
    }

    // Connect to log signals
    Connections {
        target: appController
        function onNewLogMessage(message, level) {
            logModel.append({ "message": message, "level": level });
            // Limit log entries
            while (logModel.count > 2000)
                logModel.remove(0);
        }
    }
}
