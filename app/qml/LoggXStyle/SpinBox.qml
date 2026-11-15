import QtQuick
import QtQuick.Templates as T
import LoggX 1.0

T.SpinBox {
    id: control
    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            contentItem.implicitWidth + up.implicitIndicatorWidth + down.implicitIndicatorWidth)
    implicitHeight: Theme.controlHeight
    leftPadding: down.indicator.width
    rightPadding: up.indicator.width
    font.pixelSize: Theme.fontSize
    editable: true
    validator: IntValidator {
        locale: control.locale.name
        bottom: Math.min(control.from, control.to)
        top: Math.max(control.from, control.to)
    }
    contentItem: TextInput {
        text: control.displayText
        font: control.font
        color: Theme.text
        selectionColor: Theme.accent
        selectedTextColor: "white"
        horizontalAlignment: Qt.AlignHCenter
        verticalAlignment: Qt.AlignVCenter
        readOnly: !control.editable
        validator: control.validator
        inputMethodHints: control.inputMethodHints
    }
    up.indicator: Rectangle {
        x: control.mirrored ? 0 : control.width - width
        height: control.height
        implicitWidth: 26
        radius: Theme.radius
        color: control.up.pressed ? Theme.pressed : control.up.hovered ? Theme.hover : "transparent"
        Icon { anchors.centerIn: parent; width: 14; height: 14; name: "plus"; color: Theme.dim }
    }
    down.indicator: Rectangle {
        x: control.mirrored ? control.width - width : 0
        height: control.height
        implicitWidth: 26
        radius: Theme.radius
        color: control.down.pressed ? Theme.pressed : control.down.hovered ? Theme.hover : "transparent"
        Icon { anchors.centerIn: parent; width: 14; height: 14; name: "minus"; color: Theme.dim }
    }
    background: Rectangle {
        implicitWidth: 110
        radius: Theme.radius
        color: Theme.raised
        border.color: control.activeFocus ? Theme.accent : Theme.border
    }
}
