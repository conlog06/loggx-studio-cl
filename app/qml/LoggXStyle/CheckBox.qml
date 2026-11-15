import QtQuick
import QtQuick.Templates as T
import LoggX 1.0

T.CheckBox {
    id: control
    implicitWidth: Math.max(implicitBackgroundWidth, implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight, implicitContentHeight + topPadding + bottomPadding,
                             implicitIndicatorHeight + topPadding + bottomPadding)
    padding: 4
    spacing: 8
    font.pixelSize: Theme.fontSize
    hoverEnabled: true
    indicator: Rectangle {
        x: control.leftPadding
        y: (control.height - height) / 2
        implicitWidth: 18
        implicitHeight: 18
        radius: 5
        color: control.checked ? Theme.accent : control.hovered ? Theme.hover : Theme.raised
        border.width: control.checked ? 0 : 1
        border.color: Theme.borderStrong
        Icon {
            anchors.centerIn: parent
            width: 14
            height: 14
            name: "check"
            color: "white"
            visible: control.checked
        }
    }
    contentItem: Text {
        leftPadding: control.indicator.width + control.spacing
        text: control.text
        font: control.font
        color: control.enabled ? Theme.text : Theme.faint
        verticalAlignment: Text.AlignVCenter
    }
}
