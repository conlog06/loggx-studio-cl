import QtQuick
import QtQuick.Templates as T
import LoggX 1.0

// Segment-Schalter: alle Reiter in einer abgerundeten Leiste.
T.TabBar {
    id: control
    implicitWidth: Math.max(implicitBackgroundWidth, contentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight, contentHeight + topPadding + bottomPadding)
    padding: 3
    spacing: 3
    contentItem: ListView {
        model: control.contentModel
        currentIndex: control.currentIndex
        spacing: control.spacing
        orientation: ListView.Horizontal
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.AutoFlickIfNeeded
        snapMode: ListView.SnapToItem
        highlightMoveDuration: 140
        highlightResizeDuration: 140
        highlight: Rectangle {
            radius: Theme.radius - 2
            color: Theme.hover
            border.color: Theme.borderStrong
        }
    }
    background: Rectangle {
        radius: Theme.radius + 1
        color: Theme.panel2
        border.color: Theme.border
    }
}
