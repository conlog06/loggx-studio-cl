import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Platzhalter für Arbeitsbereiche, die laut Roadmap später kommen.
Item {
    id: root
    property string title
    property string phase
    property string note
    property var items: []

    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(parent.width - 80, 560)
        spacing: 10

        Label { text: root.title; font.pixelSize: 26; font.bold: true }
        Label { text: root.phase; color: "#4f8cff"; font.bold: true }
        Label {
            Layout.fillWidth: true
            Layout.topMargin: 4
            visible: root.note !== ""
            text: root.note
            wrapMode: Text.WordWrap
        }
        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: "#2d313b"; Layout.topMargin: 6; Layout.bottomMargin: 6 }
        Repeater {
            model: root.items
            Label {
                required property string modelData
                Layout.fillWidth: true
                text: "•  " + modelData
                color: "#b3b9c6"
                wrapMode: Text.WordWrap
            }
        }
    }
}
