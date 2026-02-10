pragma Singleton
import QtQuick 2.15

QtObject {
    // Sakura-inspired color palette
    readonly property color primary: "#E91E63"        // Sakura pink
    readonly property color primaryLight: "#F48FB1"
    readonly property color primaryDark: "#C2185B"
    readonly property color accent: "#FF4081"

    readonly property color bgPrimary: "#1a1a2e"      // Dark navy
    readonly property color bgSecondary: "#16213e"
    readonly property color bgCard: "#1f2b47"
    readonly property color bgInput: "#0f1629"
    readonly property color bgHover: "#2a3a5c"

    readonly property color textPrimary: "#e0e0e0"
    readonly property color textSecondary: "#a0a0b0"
    readonly property color textMuted: "#6b6b80"

    readonly property color success: "#4CAF50"
    readonly property color warning: "#FF9800"
    readonly property color error: "#f44336"
    readonly property color info: "#2196F3"

    readonly property color border: "#2a2a4a"
    readonly property color borderLight: "#3a3a5a"

    // Typography
    readonly property int fontSizeSmall: 11
    readonly property int fontSizeNormal: 13
    readonly property int fontSizeMedium: 15
    readonly property int fontSizeLarge: 18
    readonly property int fontSizeTitle: 22
    readonly property int fontSizeHeader: 28

    // Spacing
    readonly property int spacingTiny: 4
    readonly property int spacingSmall: 8
    readonly property int spacingMedium: 12
    readonly property int spacingLarge: 16
    readonly property int spacingXLarge: 24

    // Border radius
    readonly property int radiusSmall: 4
    readonly property int radiusMedium: 8
    readonly property int radiusLarge: 12

    // Sidebar
    readonly property int sideNavWidth: 220
    readonly property int sideNavCollapsedWidth: 60

    // Animation durations
    readonly property int animFast: 150
    readonly property int animNormal: 250
    readonly property int animSlow: 400

    // Log colors
    function logColor(level) {
        switch(level) {
            case 0: return textMuted;     // Debug
            case 1: return success;        // Info
            case 2: return warning;        // Warning
            case 3: return error;          // Error
            case 4: return "#ff0000";      // Fatal
            default: return textPrimary;
        }
    }
}
