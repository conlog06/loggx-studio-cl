import QtQuick
import QtQuick.Templates as T
import LoggX 1.0

T.ToolTip {
    id: control
    x: parent ? (parent.width - implicitWidth) / 2 : 0
    y: -implicitHeight - 6
    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset, contentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset, contentHeight + topPadding + bottomPadding)
    margins: 6
    padding: 6
    leftPadding: 9
    rightPadding: 9
    font.pixelSize: Theme.smallFont + 1
    closePolicy: T.Popup.CloseOnEscape | T.Popup.CloseOnPressOutsideParent | T.Popup.CloseOnReleaseOutsideParent
    contentItem: Text {
        text: control.text
        font: control.font
        wrapMode: Text.Wrap
        color: Theme.text
    }
    background: Rectangle {
        radius: 6
        color: "#2b313c"
        border.color: Theme.borderStrong
    }
    enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 100 } }
    exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 80 } }
}
