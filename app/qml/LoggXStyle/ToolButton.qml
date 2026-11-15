import QtQuick
import QtQuick.Templates as T
import LoggX 1.0

T.ToolButton {
    id: control
    implicitWidth: Math.max(implicitBackgroundWidth, implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight, implicitContentHeight + topPadding + bottomPadding)
    padding: 5
    spacing: 6
    font.pixelSize: Theme.fontSize
    hoverEnabled: true
    // Eigene Vordergrundfarbe möglich (z. B. für Keyframe-Knöpfe).
    property color tint: Theme.text
    readonly property color fg: !enabled ? Theme.faint : checked ? Theme.accentHover : tint

    contentItem: Row {
        spacing: control.spacing
        Icon {
            anchors.verticalCenter: parent.verticalCenter
            visible: control.icon.name !== ""
            name: control.icon.name
            color: control.fg
            width: 16
            height: 16
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            visible: control.text !== ""
            text: control.text
            font: control.font
            color: control.fg
        }
    }
    background: Rectangle {
        implicitWidth: 26
        implicitHeight: 26
        radius: Theme.radius - 1
        color: control.down ? Theme.pressed : control.hovered || control.checked ? Theme.hover : "transparent"
    }
}
