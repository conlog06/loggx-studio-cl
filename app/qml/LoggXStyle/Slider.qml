import QtQuick
import QtQuick.Templates as T
import LoggX 1.0

T.Slider {
    id: control
    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitHandleWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitHandleHeight + topPadding + bottomPadding)
    padding: 4
    hoverEnabled: true

    handle: Rectangle {
        x: control.leftPadding + (control.horizontal ? control.visualPosition * (control.availableWidth - width) : (control.availableWidth - width) / 2)
        y: control.topPadding + (control.horizontal ? (control.availableHeight - height) / 2 : control.visualPosition * (control.availableHeight - height))
        implicitWidth: control.horizontal ? 14 : 22
        implicitHeight: control.horizontal ? 14 : 12
        radius: control.horizontal ? 7 : 4
        color: control.pressed ? "white" : control.hovered ? "#f2f4f8" : "#dfe3ea"
        border.width: 2
        border.color: control.enabled ? Theme.accent : Theme.faint
        scale: control.pressed ? 1.1 : 1
        Behavior on scale { NumberAnimation { duration: 80 } }
    }

    background: Item {
        x: control.leftPadding
        y: control.topPadding
        implicitWidth: control.horizontal ? 160 : 24
        implicitHeight: control.horizontal ? 18 : 140
        width: control.availableWidth
        height: control.availableHeight
        Rectangle {
            x: control.horizontal ? 0 : (parent.width - width) / 2
            y: control.horizontal ? (parent.height - height) / 2 : 0
            width: control.horizontal ? parent.width : 4
            height: control.horizontal ? 4 : parent.height
            radius: 2
            color: Theme.border
            Rectangle {
                x: 0
                y: control.horizontal ? 0 : control.visualPosition * parent.height
                width: control.horizontal ? control.position * parent.width : parent.width
                height: control.horizontal ? parent.height : (1 - control.visualPosition) * parent.height
                radius: 2
                color: control.enabled ? Theme.accent : Theme.faint
            }
        }
    }
}
