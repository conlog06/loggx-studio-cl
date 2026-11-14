import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

// Arbeitsbereich "Export": Preset oder eigene Einstellungen, rendern.
Item {
    id: root

    // Höhe vorgeben, Breite folgt dem Seitenverhältnis des Projekts.
    function sizeFor(height) {
        if (height <= 0)
            return { w: app.projectWidth, h: app.projectHeight }
        // Hochkant-Projekte: die kurze Seite ist die Breite.
        if (app.projectHeight > app.projectWidth) {
            const h = Math.round(app.projectHeight * height / app.projectWidth / 2) * 2
            return { w: height, h: h }
        }
        const w = Math.round(app.projectWidth * height / app.projectHeight / 2) * 2
        return { w: w, h: height }
    }
    readonly property var target: sizeFor(sizeBox.model[sizeBox.currentIndex].h)

    readonly property var presets: [
        { text: qsTr("Eigene Einstellungen"), size: -1 },
        { text: qsTr("YouTube 4K"), size: 1, codec: 0, quality: 2 },
        { text: qsTr("YouTube 1080p"), size: 3, codec: 0, quality: 2 },
        { text: qsTr("Instagram / TikTok (1080p)"), size: 3, codec: 0, quality: 1 },
        { text: qsTr("Archiv (H.265, sehr hoch)"), size: 0, codec: 1, quality: 3 },
        { text: qsTr("Entwurf (480p, klein)"), size: 5, codec: 0, quality: 0 }
    ]

    FileDialog {
        id: exportDialog
        title: qsTr("Video exportieren")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "mp4"
        nameFilters: [qsTr("MP4-Video (*.mp4)"), qsTr("QuickTime (*.mov)"), qsTr("Matroska (*.mkv)")]
        onAccepted: app.exportVideo(selectedFile, root.target.w, root.target.h, codecBox.currentIndex, qualityBox.currentIndex)
    }

    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(parent.width - 80, 540)
        spacing: 10

        Label { text: qsTr("Export"); font.pixelSize: 26; font.bold: true }
        Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            color: "#8a91a0"
            text: qsTr("Video mit fertig gemischtem Ton (AAC). Wenn vorhanden, wird der Hardware-Encoder des Rechners benutzt.")
        }

        GridLayout {
            Layout.fillWidth: true
            Layout.topMargin: 8
            columns: 2
            columnSpacing: 14
            rowSpacing: 8

            Label { text: qsTr("Vorlage") }
            ComboBox {
                id: presetBox
                Layout.fillWidth: true
                textRole: "text"
                model: root.presets
                onActivated: function (i) {
                    const p = root.presets[i]
                    if (p.size < 0) return
                    sizeBox.currentIndex = p.size
                    codecBox.currentIndex = p.codec
                    qualityBox.currentIndex = p.quality
                }
            }
            Label { text: qsTr("Auflösung") }
            ComboBox {
                id: sizeBox
                Layout.fillWidth: true
                textRole: "text"
                currentIndex: 0
                model: [
                    { text: qsTr("Wie Projekt"), h: 0 },
                    { text: qsTr("4K UHD (2160p)"), h: 2160 },
                    { text: qsTr("1440p"), h: 1440 },
                    { text: qsTr("1080p"), h: 1080 },
                    { text: qsTr("720p"), h: 720 },
                    { text: qsTr("480p (Entwurf)"), h: 480 }
                ]
                onActivated: presetBox.currentIndex = 0
            }
            Label { text: qsTr("Codec") }
            ComboBox {
                id: codecBox
                Layout.fillWidth: true
                model: [qsTr("H.264 (überall abspielbar)"), qsTr("H.265 / HEVC (kleinere Dateien)")]
                onActivated: presetBox.currentIndex = 0
            }
            Label { text: qsTr("Qualität") }
            ComboBox {
                id: qualityBox
                Layout.fillWidth: true
                currentIndex: 1
                model: [qsTr("Klein"), qsTr("Normal"), qsTr("Hoch"), qsTr("Sehr hoch")]
                onActivated: presetBox.currentIndex = 0
            }
        }
        Label {
            color: "#8a91a0"
            text: qsTr("%1×%2 · %3 fps · Länge %4").arg(root.target.w).arg(root.target.h).arg(app.projectFps).arg(app.timecode(app.duration))
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
            text: qsTr("Geplant für Version 1.0: 8K, ProRes, AV1 und 10-bit/HDR. 4K-Export läuft im MVP noch auf der CPU und kann dauern.")
        }
    }
}
