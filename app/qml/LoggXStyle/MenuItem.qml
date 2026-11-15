import QtQuick
import QtQuick.Templates as T
import LoggX 1.0

T.MenuItem {
    id: control
    implicitWidth: Math.max(implicitBackgroundWidth, implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight, implicitContentHeight + topPadding + bottomPadding)
    padding: 7
    leftPadding: 10
    spacing: 8
    font.pixelSize: Theme.fontSize
    hoverEnabled: true
    contentItem: Row {
        spacing: control.spacing
        Icon {
            anchors.verticalCenter: parent.verticalCenter
            visible: control.icon.name !== ""
            name: control.icon.name
            color: Theme.dim
            width: 16
            height: 16
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: control.text
            font: control.font
            color: control.enabled ? Theme.text : Theme.faint
        }
    }
    background: Rectangle {
        implicitWidth: 190
        implicitHeight: 30
        radius: Theme.radius - 2
        color: control.highlighted || control.hovered ? Theme.hover : "transparent"
    }
}
