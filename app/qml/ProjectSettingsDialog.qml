import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LoggXStyle

// Projekteinstellungen: Format, Bildrate, Tempo.
Dialog {
    id: dialog
    title: qsTr("Projekteinstellungen")
    modal: true
    anchors.centerIn: parent
    width: Math.min(parent.width - 80, 520)
    ProjectFormatForm { width: parent.width }
    footer: Item {
        implicitHeight: 56
        Button {
            anchors.right: parent.right
            anchors.rightMargin: 16
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("Fertig")
            highlighted: true
            onClicked: dialog.close()
        }
    }
}
