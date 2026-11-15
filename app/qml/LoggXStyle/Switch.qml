import QtQuick
import QtQuick.Templates as T
import LoggX 1.0

T.Switch {
    id: control
    implicitWidth: Math.max(implicitBackgroundWidth, implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight, implicitContentHeight + topPadding + bottomPadding,
                             implicitIndicatorHeight + topPadding + bottomPadding)
    padding: 4
    spacing: 10
    font.pixelSize: Theme.fontSize
    indicator: Rectangle {
        x: control.leftPadding
        y: (control.height - height) / 2
        implicitWidth: 34
        implicitHeight: 18
        radius: 9
        color: control.checked ? Theme.accent : Theme.border
        Rectangle {
            x: control.checked ? parent.width - width - 2 : 2
            y: 2
            width: 14
            height: 14
            radius: 7
            color: "white"
            Behavior on x { NumberAnimation { duration: 110 } }
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
