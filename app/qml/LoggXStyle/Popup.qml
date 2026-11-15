import QtQuick
import QtQuick.Templates as T
import LoggX 1.0

T.Popup {
    id: control
    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset, contentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset, contentHeight + topPadding + bottomPadding)
    padding: 12
    background: Rectangle {
        radius: Theme.radius + 2
        color: Theme.panel2
        border.color: Theme.borderStrong
    }
    T.Overlay.modal: Rectangle { color: "#99000000" }
}
