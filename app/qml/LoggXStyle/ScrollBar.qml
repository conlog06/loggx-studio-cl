import QtQuick
import QtQuick.Templates as T
import LoggX 1.0

T.ScrollBar {
    id: control
    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset, implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset, implicitContentHeight + topPadding + bottomPadding)
    padding: 2
    visible: control.policy !== T.ScrollBar.AlwaysOff
    minimumSize: orientation === Qt.Horizontal ? height / width : width / height
    hoverEnabled: true
    contentItem: Rectangle {
        implicitWidth: 8
        implicitHeight: 8
        radius: 4
        color: control.pressed ? Theme.dim : control.hovered ? Theme.faint : Theme.borderStrong
        opacity: control.policy === T.ScrollBar.AlwaysOn || control.active || control.size < 1.0 ? 1 : 0
    }
}
