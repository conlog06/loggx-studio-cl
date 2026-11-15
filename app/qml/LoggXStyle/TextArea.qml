import QtQuick
import QtQuick.Templates as T
import LoggX 1.0

T.TextArea {
    id: control
    implicitWidth: Math.max(contentWidth + leftPadding + rightPadding, implicitBackgroundWidth + leftInset + rightInset)
    implicitHeight: Math.max(contentHeight + topPadding + bottomPadding, implicitBackgroundHeight + topInset + bottomInset)
    padding: 8
    font.pixelSize: Theme.fontSize
    color: Theme.text
    selectionColor: Theme.accent
    selectedTextColor: "white"
    placeholderTextColor: Theme.faint
    Text {
        x: control.leftPadding
        y: control.topPadding
        text: control.placeholderText
        font: control.font
        color: control.placeholderTextColor
        visible: !control.length && !control.preeditText
    }
    background: Rectangle {
        implicitWidth: 160
        implicitHeight: 60
        radius: Theme.radius
        color: control.activeFocus ? Theme.panel : Theme.raised
        border.width: control.activeFocus ? 2 : 1
        border.color: control.activeFocus ? Theme.accent : Theme.border
    }
}
