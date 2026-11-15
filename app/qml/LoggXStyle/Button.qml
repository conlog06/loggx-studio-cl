import QtQuick
import QtQuick.Templates as T
import LoggX 1.0

// Knopf: optional mit Icon (icon.name = Name eines LoggX-Icons).
T.Button {
    id: control
    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)
    padding: 6
    leftPadding: text === "" ? 7 : 12
    rightPadding: text === "" ? 7 : 12
    spacing: 7
    font.pixelSize: Theme.fontSize
    hoverEnabled: true

    readonly property color fg: !enabled ? Theme.faint
                                : highlighted ? "white"
                                : checked ? Theme.accentHover : Theme.text

    contentItem: Row {
        spacing: control.spacing
        Icon {
            anchors.verticalCenter: parent.verticalCenter
            visible: control.icon.name !== ""
            name: control.icon.name
            color: control.fg
            width: 16
            height: 16
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            visible: control.text !== ""
            text: control.text
            font: control.font
            color: control.fg
            elide: Text.ElideRight
        }
    }

    background: Rectangle {
        implicitWidth: 30
        implicitHeight: Theme.controlHeight
        radius: Theme.radius
        color: control.highlighted
               ? (control.down ? Qt.darker(Theme.accent, 1.15) : control.hovered ? Theme.accentHover : Theme.accent)
               : control.checked ? Qt.rgba(0.36, 0.55, 1, 0.18)
               : control.down ? Theme.pressed : control.hovered ? Theme.hover : (control.flat ? "transparent" : Theme.raised)
        border.width: control.highlighted || control.flat ? 0 : 1
        border.color: control.checked ? Theme.accent : Theme.border
        Behavior on color { ColorAnimation { duration: 90 } }
    }
}
