import QtQuick
import QtQuick.Templates as T
import LoggX 1.0

T.ItemDelegate {
    id: control
    implicitWidth: Math.max(implicitBackgroundWidth, implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight, implicitContentHeight + topPadding + bottomPadding)
    padding: 8
    leftPadding: 10
    font.pixelSize: Theme.fontSize
    hoverEnabled: true
    contentItem: Text {
        text: control.text
        font: control.font
        color: Theme.text
        elide: Text.ElideRight
        verticalAlignment: Text.AlignVCenter
    }
    background: Rectangle {
        implicitHeight: 30
        radius: Theme.radius - 2
        color: control.highlighted || control.hovered ? Theme.hover : "transparent"
    }
}
