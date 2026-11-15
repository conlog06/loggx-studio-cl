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
    implicitHeight: 48
    color: Theme.bg

    FileDialog {
        id: stillDialog
        title: qsTr("Standbild speichern")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "png"
        nameFilters: [qsTr("PNG-Bild (*.png)"), qsTr("JPEG-Bild (*.jpg)")]
        onAccepted: app.exportStill(selectedFile)
    }

    RowLayout {
        anchors.fill: parent
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
