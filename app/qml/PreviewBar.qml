import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import LoggX 1.0
import LoggXStyle

// Leiste unter der Vorschau: Zeit, Wiedergabe, Qualität, Standbild.
Rectangle {
    id: root
    property bool clipNavigation: false     // ◀ Clip / Clip ▶ (Farbe)
    property string rightText: app.timecode(app.duration)
    implicitHeight: mcRow.visible ? 84 : 48
    color: Theme.bg
    // Multicam: aktuelle Gruppe (wird beim Abspielen neu gelesen)
    readonly property var mc: { app.position; return app.multicam }

    FileDialog {
        id: stillDialog
        title: qsTr("Standbild speichern")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "png"
        nameFilters: [qsTr("PNG-Bild (*.png)"), qsTr("JPEG-Bild (*.jpg)")]
        onAccepted: app.exportStill(selectedFile)
    }

    // Multicam-Kameras: Klick oder Taste 1–9 schneidet am Abspielkopf um
    RowLayout {
        id: mcRow
        visible: root.mc.angles !== undefined
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: 12
        anchors.rightMargin: 12
        anchors.topMargin: 4
        height: 32
        spacing: 6
        Label { text: (root.mc.name || "") + ":"; color: Theme.dim; font.pixelSize: 12 }
        Repeater {
            model: root.mc.angles || []
            Rectangle {
                required property int index
                required property var modelData
                readonly property bool active: root.mc.current === index
                Layout.preferredWidth: 120
                Layout.preferredHeight: 28
                radius: 5
                opacity: modelData.available ? 1 : 0.4
                color: active ? Theme.alpha("#ff3b5c", 0.25) : mcMouse.containsMouse ? Theme.hover : Theme.panel2
                border.color: active ? "#ff3b5c" : Theme.border
                Label {
                    anchors.centerIn: parent
                    text: (parent.index + 1) + "  " + parent.modelData.name
                    font.bold: parent.active
                    font.pixelSize: 12
                    color: parent.active ? "white" : Theme.dim
                }
                MouseArea {
                    id: mcMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    enabled: parent.modelData.available
                    onClicked: app.switchAngle(parent.index)
                }
                ToolTip.text: modelData.file + qsTr(" – Taste %1").arg(index + 1)
                ToolTip.visible: mcMouse.containsMouse
                ToolTip.delay: 500
            }
        }
        Item { Layout.fillWidth: true }
    }

    RowLayout {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 48
        anchors.leftMargin: 12
        anchors.rightMargin: 12
        spacing: 4
        Label {
            text: app.timecode(app.position)
            font.family: "monospace"
            font.pixelSize: 15
            Layout.preferredWidth: 118
        }
        ComboBox {
            id: quality
            Layout.preferredWidth: 128
            model: [qsTr("Auto"), qsTr("Voll"), qsTr("Halb"), qsTr("Viertel")]
            currentIndex: app.previewQuality
            displayText: currentText + "  " + app.previewSize.split("×")[1] + "p"
            onActivated: function (i) { app.previewQuality = i }
            ToolTip.text: qsTr("Vorschauqualität – Auto: so scharf wie das Fenster, beim Abspielen so groß, wie es flüssig bleibt. Der Export hat immer volle Qualität.")
            ToolTip.visible: hovered
            ToolTip.delay: 500
        }
        Item { Layout.fillWidth: true }
        IconButton { visible: root.clipNavigation; iconName: "skip-back"; tip: qsTr("Vorheriger Clip"); onClicked: app.selectNeighbourClip(-1) }
        IconButton { visible: !root.clipNavigation; iconName: "skip-back"; tip: qsTr("Zum Anfang (Pos1)"); onClicked: app.seek(0) }
        IconButton { iconName: "chevron-left"; tip: qsTr("Ein Bild zurück (←)"); onClicked: app.stepFrames(-1) }
        Button {
            icon.name: app.playing ? "pause" : "play"
            text: app.playing ? qsTr("Pause") : qsTr("Abspielen")
            highlighted: true
            implicitWidth: 112
            onClicked: app.togglePlay()
            ToolTip.text: qsTr("Leertaste"); ToolTip.visible: hovered; ToolTip.delay: 600
        }
        IconButton { iconName: "chevron-right"; tip: qsTr("Ein Bild vor (→)"); onClicked: app.stepFrames(1) }
        IconButton { visible: root.clipNavigation; iconName: "skip-forward"; tip: qsTr("Nächster Clip"); onClicked: app.selectNeighbourClip(1) }
        IconButton { visible: !root.clipNavigation; iconName: "skip-forward"; tip: qsTr("Zum Ende (Ende)"); onClicked: app.seek(app.duration) }
        Item { Layout.fillWidth: true }
        IconButton { iconName: "image"; tip: qsTr("Standbild in voller Auflösung speichern"); enabled: app.duration > 0; onClicked: stillDialog.open() }
        Label {
            text: root.rightText
            font.family: root.clipNavigation ? Qt.application.font.family : "monospace"
            font.pixelSize: root.clipNavigation ? 13 : 15
            color: Theme.dim
            elide: Text.ElideMiddle
            horizontalAlignment: Text.AlignRight
            Layout.preferredWidth: root.clipNavigation ? 200 : 118
        }
    }
}
