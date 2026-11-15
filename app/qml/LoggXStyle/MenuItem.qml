import QtQuick
import QtQuick.Templates as T
import LoggX 1.0

T.MenuItem {
    id: control
    // Kürzel rechts im Menü (nur Anzeige), z. B. „Strg+C“
    property string hint: ""
    property bool danger: false
    implicitWidth: Math.max(implicitBackgroundWidth, implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight, implicitContentHeight + topPadding + bottomPadding)
    padding: 7
    leftPadding: 10
    rightPadding: 10
    spacing: 8
    font.pixelSize: Theme.fontSize
    hoverEnabled: true
    contentItem: Item {
        implicitWidth: row.implicitWidth + (hintText.visible ? hintText.implicitWidth + 24 : 0) + (control.subMenu ? 16 : 0)
        implicitHeight: row.implicitHeight
        Row {
            id: row
            anchors.verticalCenter: parent.verticalCenter
            spacing: control.spacing
            Rectangle {
                visible: control.checkable
                width: 14; height: 14; radius: 3
                anchors.verticalCenter: parent.verticalCenter
                color: control.checked ? Theme.accent : "transparent"
                border.color: control.checked ? Theme.accent : Theme.borderStrong
                Text { anchors.centerIn: parent; text: "✓"; color: "white"; font.pixelSize: 10; visible: control.checked }
            }
            Icon {
                anchors.verticalCenter: parent.verticalCenter
                visible: control.icon.name !== ""
                name: control.icon.name
                color: control.danger ? Theme.danger : Theme.dim
                width: 16
                height: 16
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: control.text
                font: control.font
                color: !control.enabled ? Theme.faint : control.danger ? Theme.danger : Theme.text
            }
        }
        Text {
            id: hintText
            visible: control.hint !== ""
            anchors.right: parent.right
            anchors.rightMargin: control.subMenu ? 16 : 0
            anchors.verticalCenter: parent.verticalCenter
            text: control.hint
            font.pixelSize: Theme.fontSize - 1
            color: Theme.faint
        }
        Text {
            visible: control.subMenu !== null
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            text: "›"
            font.pixelSize: Theme.fontSize + 3
            color: Theme.dim
        }
    }
    background: Rectangle {
        implicitWidth: 210
        implicitHeight: 30
        radius: Theme.radius - 2
        color: control.highlighted || control.hovered ? Theme.hover : "transparent"
    }
}
