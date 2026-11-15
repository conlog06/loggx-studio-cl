import QtQuick
import QtQuick.Templates as T
import LoggX 1.0

T.TextField {
    id: control
    implicitWidth: implicitBackgroundWidth + leftInset + rightInset || Math.max(contentWidth, placeholder.implicitWidth) + leftPadding + rightPadding
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             contentHeight + topPadding + bottomPadding)
    padding: 6
    leftPadding: 10
    rightPadding: 10
    font.pixelSize: Theme.fontSize
    color: Theme.text
    selectionColor: Theme.accent
    selectedTextColor: "white"
    placeholderTextColor: Theme.faint
    verticalAlignment: TextInput.AlignVCenter
    hoverEnabled: true
    Text {
        id: placeholder
        x: control.leftPadding
        y: control.topPadding
        width: control.width - (control.leftPadding + control.rightPadding)
        height: control.height - (control.topPadding + control.bottomPadding)
        text: control.placeholderText
        font: control.font
        color: control.placeholderTextColor
        verticalAlignment: control.verticalAlignment
        visible: !control.length && !control.preeditText
        elide: Text.ElideRight
    }
    background: Rectangle {
        implicitWidth: 160
        implicitHeight: Theme.controlHeight
        radius: Theme.radius
        color: control.activeFocus ? Theme.panel : Theme.raised
        border.width: control.activeFocus ? 2 : 1
        border.color: control.activeFocus ? Theme.accent : control.hovered ? Theme.borderStrong : Theme.border
    }
}
