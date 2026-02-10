import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick.Window 2.15
import "components"
import "pages"

ApplicationWindow {
    id: root
    width: 1320
    height: 820
    minimumWidth: 1000
    minimumHeight: 600
    visible: true
    title: appController.translate("window.title")
    color: Theme.bgPrimary

    // Navigation model
    ListModel {
        id: navModel
        ListElement { key: "qualcomm";   icon: "\u26A1"; labelKey: "nav.qualcomm" }
        ListElement { key: "mediatek";   icon: "\u2699"; labelKey: "nav.mediatek" }
        ListElement { key: "spreadtrum"; icon: "\u2706"; labelKey: "nav.spreadtrum" }
        ListElement { key: "fastboot";   icon: "\u25B6"; labelKey: "nav.fastboot" }
        ListElement { key: "autoroot";   icon: "\u2618"; labelKey: "nav.autoroot" }
        ListElement { key: "settings";   icon: "\u2692"; labelKey: "nav.settings" }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // --- Side Navigation ---
        SideNav {
            id: sideNav
            Layout.fillHeight: true
            Layout.preferredWidth: Theme.sideNavWidth
            model: navModel
            currentIndex: appController.currentPage
            onNavigated: function(index) {
                appController.currentPage = index;
            }
        }

        // --- Vertical separator ---
        Rectangle {
            Layout.fillHeight: true
            Layout.preferredWidth: 1
            color: Theme.border
        }

        // --- Main content area ---
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // Content pages
            StackLayout {
                id: pageStack
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: appController.currentPage

                QualcommPage {}
                MediatekPage {}
                SpreadtrumPage {}
                FastbootPage {}
                AutoRootPage {}
                SettingsPage {}
            }

            // --- Horizontal separator ---
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 1
                color: Theme.border
            }

            // --- Log panel (bottom) ---
            LogPanel {
                id: logPanel
                Layout.fillWidth: true
                Layout.preferredHeight: 200
                Layout.minimumHeight: 100
                Layout.maximumHeight: 400
            }
        }
    }

    // Status bar
    footer: Rectangle {
        height: 28
        color: Theme.bgSecondary

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Theme.spacingMedium
            anchors.rightMargin: Theme.spacingMedium

            Text {
                text: appController.statusText || appController.translate("status.disconnected")
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeSmall
            }
            Item { Layout.fillWidth: true }
            Text {
                text: "SakuraEDL v3.0.0"
                color: Theme.textMuted
                font.pixelSize: Theme.fontSizeSmall
            }
        }
    }
}
