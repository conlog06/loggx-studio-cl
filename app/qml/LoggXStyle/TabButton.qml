import QtQuick
import QtQuick.Templates as T
import LoggX 1.0

T.TabButton {
    id: control
    implicitWidth: Math.max(implicitBackgroundWidth, implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight, implicitContentHeight + topPadding + bottomPadding)
    padding: 6
    leftPadding: 14
    rightPadding: 14
    spacing: 7
    font.pixelSize: Theme.fontSize
    font.weight: checked ? Font.DemiBold : Font.Normal
    hoverEnabled: true
    contentItem: Item {
        implicitWidth: row.implicitWidth
        implicitHeight: row.implicitHeight
        Row {
            id: row
            anchors.centerIn: parent
            spacing: control.spacing
            Icon {
                anchors.verticalCenter: parent.verticalCenter
                visible: control.icon.name !== ""
                name: control.icon.name
                color: control.checked ? Theme.accentHover : control.hovered ? Theme.text : Theme.dim
                width: 16
                height: 16
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: control.text
                font: control.font
                color: control.checked ? Theme.text : control.hovered ? Theme.text : Theme.dim
            }
        }
    }
    background: Rectangle {
        implicitHeight: 28
        radius: Theme.radius - 2
        color: "transparent"
    }
}
