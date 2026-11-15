import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LoggXStyle

// Aufklappbarer Abschnitt im Inspector.
ColumnLayout {
    id: root
    property string title
    property bool open: true
    default property alias content: body.data
    spacing: 6

    Rectangle {
        Layout.fillWidth: true
        Layout.topMargin: 6
        Layout.preferredHeight: 24
        color: header.containsMouse ? Theme.hover : "transparent"
        radius: 4
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 2
            Label { text: root.open ? "▾" : "▸"; color: Theme.dim; Layout.preferredWidth: 12 }
            Label { text: root.title; color: Theme.dim; font.pixelSize: 11; font.bold: true; font.capitalization: Font.AllUppercase }
            Item { Layout.fillWidth: true }
        }
        MouseArea {
            id: header
            anchors.fill: parent
            hoverEnabled: true
            onClicked: root.open = !root.open
        }
    }
    ColumnLayout {
        id: body
        Layout.fillWidth: true
        visible: root.open
        spacing: 8
    }
}
