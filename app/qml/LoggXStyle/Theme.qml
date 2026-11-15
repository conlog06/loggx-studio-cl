pragma Singleton
import QtQuick

// Farben und Maße des LoggX-Designs an einer Stelle.
QtObject {
    readonly property color bg: "#0f1115"
    readonly property color panel: "#15181e"
    readonly property color panel2: "#1a1e25"
    readonly property color raised: "#222730"
    readonly property color hover: "#2a303b"
    readonly property color pressed: "#323a47"
    readonly property color border: "#2a303a"
    readonly property color borderStrong: "#3a4250"
    readonly property color text: "#e8ebf1"
    readonly property color dim: "#9aa2b1"
    readonly property color faint: "#5f6775"
    readonly property color accent: "#5b8cff"
    readonly property color accentHover: "#7aa2ff"
    readonly property color accent2: "#8b5cf6"
    readonly property color video: "#4f7df0"
    readonly property color audio: "#2fb383"
    readonly property color music: "#f0923b"
    readonly property color title: "#a06bf0"
    readonly property color danger: "#ff5a6a"
    readonly property color warn: "#f5c451"
    readonly property color ok: "#3ecf8e"
    readonly property int radius: 7
    readonly property int controlHeight: 30
    readonly property int fontSize: 13
    readonly property int smallFont: 11

    // Farbe mit Deckkraft (Qt.alpha gibt es erst ab Qt 6.5)
    function alpha(c, a) { return Qt.rgba(c.r, c.g, c.b, a) }
}
