import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

// Arbeitsbereich "Export": Zielgröße wählen, rendern, Fortschritt.
Item {
    id: root

    // Höhe vorgeben, Breite folgt dem Seitenverhältnis des Projekts.
    function sizeFor(height) {
        if (height <= 0)
            return { w: app.projectWidth, h: app.projectHeight }
        const w = Math.round(app.projectWidth * height / app.projectHeight / 2) * 2
        return { w: w, h: height }
    }
    readonly property var target: sizeFor(presetBox.model[presetBox.currentIndex].h)

    FileDialog {
        id: exportDialog
        title: qsTr("Video exportieren")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "mp4"
        nameFilters: [qsTr("MP4-Video (*.mp4)"), qsTr("QuickTime (*.mov)"), qsTr("Matroska (*.mkv)")]
        onAccepted: app.exportVideo(selectedFile, root.target.w, root.target.h)
    }

    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(parent.width - 80, 520)
        spacing: 12

        Label { text: qsTr("Export"); font.pixelSize: 26; font.bold: true }
        Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            color: "#8a91a0"
            text: qsTr("H.264-Video mit AAC-Ton. Wenn vorhanden, wird der Hardware-Encoder des Rechners benutzt.")
        }

        Label { text: qsTr("Auflösung"); Layout.topMargin: 8 }
        ComboBox {
            id: presetBox
            Layout.fillWidth: true
            textRole: "text"
            model: [
                { text: qsTr("Wie Projekt"), h: 0 },
                { text: qsTr("1080p"), h: 1080 },
                { text: qsTr("720p"), h: 720 },
                { text: qsTr("480p (Entwurf)"), h: 480 }
            ]
        }
        Label {
            color: "#8a91a0"
            text: qsTr("%1×%2 · Länge %3").arg(root.target.w).arg(root.target.h).arg(app.timecode(app.duration))
        }

        ProgressBar {
            Layout.fillWidth: true
            Layout.topMargin: 8
            visible: app.exporting
            value: app.exportProgress
        }
        RowLayout {
            Layout.topMargin: 8
            spacing: 8
            Button {
                text: qsTr("Exportieren …")
                highlighted: true
                enabled: !app.exporting && app.duration > 0
                onClicked: exportDialog.open()
            }
            Button {
                text: qsTr("Abbrechen")
                visible: app.exporting
                onClicked: app.cancelExport()
            }
            Label {
                visible: app.exporting
                text: Math.round(app.exportProgress * 100) + " %"
            }
        }
        Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: app.duration > 0 ? app.status : qsTr("Das Projekt enthält noch keine Clips.")
            color: "#b3b9c6"
        }
        Label {
            Layout.fillWidth: true
            Layout.topMargin: 12
            wrapMode: Text.WordWrap
            color: "#5d6370"
            font.pixelSize: 11
            text: qsTr("Geplant: ProRes, H.265, AV1, 10-bit, 8K und Presets für YouTube, TikTok und Instagram.")
        }
    }
}
