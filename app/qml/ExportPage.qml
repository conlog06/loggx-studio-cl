import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import LoggX 1.0
import LoggXStyle

// Arbeitsbereich "Export": Video, nur Audio, Stems (Spuren einzeln), MIDI.
Rectangle {
    id: root
    color: Theme.bg

    // Höhe vorgeben, Breite folgt dem Seitenverhältnis des Projekts.
    function sizeFor(height) {
        if (height <= 0)
            return { w: app.projectWidth, h: app.projectHeight }
        if (app.projectHeight > app.projectWidth) {
            const h = Math.round(app.projectHeight * height / app.projectWidth / 2) * 2
            return { w: height, h: h }
        }
        const w = Math.round(app.projectWidth * height / app.projectHeight / 2) * 2
        return { w: w, h: height }
    }
    readonly property var sizes: [
        { text: qsTr("Wie Projekt"), h: 0 },
        { text: "8K (4320p)", h: 4320 },
        { text: "4K UHD (2160p)", h: 2160 },
        { text: "1440p", h: 1440 },
        { text: "1080p", h: 1080 },
        { text: "720p", h: 720 },
        { text: qsTr("480p (Entwurf)"), h: 480 }
    ]
    readonly property var target: sizeFor(sizes[sizeBox.currentIndex].h)
    readonly property bool needsPro: Math.min(target.w, target.h) > 1080 || codecBox.currentIndex === 1

    readonly property var presets: [
        { text: qsTr("Eigene Einstellungen"), size: -1 },
        { text: qsTr("YouTube 4K"), size: 2, codec: 0, quality: 2 },
        { text: qsTr("YouTube 1080p"), size: 4, codec: 0, quality: 2 },
        { text: qsTr("Instagram / TikTok (1080p)"), size: 4, codec: 0, quality: 1 },
        { text: qsTr("Archiv (H.265, sehr hoch)"), size: 0, codec: 1, quality: 3 },
        { text: qsTr("Entwurf (480p, klein)"), size: 6, codec: 0, quality: 0 }
    ]
    readonly property var audioFormats: [
        { text: qsTr("WAV (unkomprimiert)"), ext: "wav", lossless: true },
        { text: qsTr("FLAC (verlustfrei)"), ext: "flac", lossless: true },
        { text: "MP3 (320 kbit/s)", ext: "mp3", lossless: false },
        { text: "AAC / M4A (256 kbit/s)", ext: "m4a", lossless: false },
        { text: "Ogg Vorbis", ext: "ogg", lossless: false },
        { text: "Opus (192 kbit/s)", ext: "opus", lossless: false }
    ]
    readonly property var audioFormat: audioFormats[formatBox.currentIndex]
    readonly property int bits: bitsBox.currentIndex === 1 ? 24 : 16

    // Musikprojekte haben kein Bild: dort beginnt die Auswahl beim Audio.
    readonly property var kinds: app.musicMode
        ? [{ k: 1, text: qsTr("Audio"), icon: "wave" }, { k: 2, text: qsTr("Stems"), icon: "layers" }, { k: 3, text: "MIDI", icon: "piano" }]
        : [{ k: 0, text: qsTr("Video"), icon: "film" }, { k: 1, text: qsTr("Audio"), icon: "wave" }, { k: 2, text: qsTr("Stems"), icon: "layers" }, { k: 3, text: "MIDI", icon: "piano" }]
    readonly property int kind: kinds[Math.min(kindBar.currentIndex, kinds.length - 1)].k

    FileDialog {
        id: videoDialog
        title: qsTr("Video exportieren")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "mp4"
        nameFilters: [qsTr("MP4-Video (*.mp4)"), qsTr("QuickTime (*.mov)"), qsTr("Matroska (*.mkv)")]
        onAccepted: app.exportVideo(selectedFile, root.target.w, root.target.h, codecBox.currentIndex, qualityBox.currentIndex)
    }
    FileDialog {
        id: audioDialog
        title: qsTr("Audio exportieren")
        fileMode: FileDialog.SaveFile
        defaultSuffix: root.audioFormat.ext
        nameFilters: [root.audioFormat.text + " (*." + root.audioFormat.ext + ")"]
        onAccepted: app.exportAudio(selectedFile, root.bits, tailBox.checked)
    }
    FolderDialog {
        id: stemsDialog
        title: qsTr("Ordner für die Stems wählen")
        onAccepted: app.exportStems(selectedFolder, stemFormatBox.currentIndex === 1 ? "flac" : "wav", root.bits)
    }
    FileDialog {
        id: midiDialog
        title: qsTr("Als MIDI-Datei exportieren")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "mid"
        nameFilters: [qsTr("MIDI-Datei (*.mid)")]
        onAccepted: app.exportMidiFile(selectedFile)
    }

    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth
        contentHeight: card.implicitHeight + 80
        clip: true

        Rectangle {
            id: card
            width: Math.min(root.width - 80, 600)
            x: (root.width - width) / 2
            y: 40
            implicitHeight: col.implicitHeight + 48
            radius: 14
            color: Theme.panel
            border.color: Theme.border

            ColumnLayout {
                id: col
                anchors.fill: parent
                anchors.margins: 24
                spacing: 12

                RowLayout {
                    spacing: 12
                    Rectangle {
                        width: 42; height: 42; radius: 11
                        gradient: Gradient {
                            orientation: Gradient.Horizontal
                            GradientStop { position: 0; color: Theme.accent }
                            GradientStop { position: 1; color: Theme.accent2 }
                        }
                        Icon { anchors.centerIn: parent; width: 22; height: 22; name: "export"; color: "white" }
                    }
                    ColumnLayout {
                        spacing: 1
                        Label { text: qsTr("Export"); font.pixelSize: 22; font.bold: true }
                        Label { text: qsTr("„%1“ · Länge %2").arg(app.projectName).arg(app.timecode(app.duration)); color: Theme.dim }
                    }
                }

                TabBar {
                    id: kindBar
                    Layout.fillWidth: true
                    Repeater {
                        model: root.kinds
                        TabButton {
                            required property var modelData
                            text: modelData.text
                            icon.name: modelData.icon
                        }
                    }
                }

                // ------------------------------------------------ Video
                GridLayout {
                    visible: root.kind === 0
                    Layout.fillWidth: true
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
                        model: root.sizes
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
                    Item { width: 1 }
                    Label {
                        color: Theme.dim
                        text: qsTr("%1×%2 · %3 fps · Ton AAC").arg(root.target.w).arg(root.target.h).arg(app.projectFps)
                    }
                }

                // ------------------------------------------------ Audio
                GridLayout {
                    visible: root.kind === 1
                    Layout.fillWidth: true
                    columns: 2
                    columnSpacing: 14
                    rowSpacing: 8
                    Label { text: qsTr("Format") }
                    ComboBox {
                        id: formatBox
                        Layout.fillWidth: true
                        textRole: "text"
                        model: root.audioFormats
                    }
                    Label { text: qsTr("Bittiefe"); visible: root.audioFormat.ext === "wav" || root.audioFormat.ext === "flac" }
                    ComboBox {
                        id: bitsBox
                        visible: root.audioFormat.ext === "wav" || root.audioFormat.ext === "flac"
                        Layout.fillWidth: true
                        model: [qsTr("16 Bit (CD)"), qsTr("24 Bit (Studio)")]
                    }
                    Item { width: 1 }
                    CheckBox {
                        id: tailBox
                        checked: true
                        text: qsTr("Ausklang mitnehmen (Hall und Release nach dem Ende)")
                    }
                    Item { width: 1 }
                    Label { color: Theme.dim; text: qsTr("Stereo · 48 kHz · Master-Limiter aktiv") }
                }

                // ------------------------------------------------ Stems
                ColumnLayout {
                    visible: root.kind === 2
                    Layout.fillWidth: true
                    spacing: 8
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: Theme.dim
                        text: qsTr("Jede Spur mit Ton als eigene Datei (mit ihren Effekten, ohne Master) – zum Weitermischen in einer anderen Software.")
                    }
                    RowLayout {
                        spacing: 8
                        Label { text: qsTr("Format") }
                        ComboBox { id: stemFormatBox; Layout.preferredWidth: 140; model: ["WAV", "FLAC"] }
                        ComboBox { Layout.preferredWidth: 160; model: bitsBox.model; currentIndex: bitsBox.currentIndex; onActivated: function (i) { bitsBox.currentIndex = i } }
                    }
                    Label {
                        color: Theme.dim
                        text: qsTr("%1 Spuren").arg(app.tracks.filter(function (t) { return t.hasSound }).length)
                    }
                }

                // ------------------------------------------------- MIDI
                Label {
                    visible: root.kind === 3
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: Theme.dim
                    text: qsTr("Alle Instrumentspuren als Standard-MIDI-Datei (Typ 1) mit Tempo – zum Öffnen in jeder Musiksoftware.")
                }

                // ---------------------------------------- Pro-Hinweis
                Rectangle {
                    visible: root.kind === 0 && root.needsPro && !app.isPro
                    Layout.fillWidth: true
                    implicitHeight: proRow.implicitHeight + 16
                    radius: 9
                    color: Theme.alpha(Theme.warn, 0.12)
                    border.color: Theme.alpha(Theme.warn, 0.5)
                    RowLayout {
                        id: proRow
                        anchors.fill: parent
                        anchors.margins: 8
                        spacing: 8
                        Icon { width: 16; height: 16; name: "key"; color: Theme.warn }
                        Label {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            color: Theme.warn
                            text: qsTr("Über 1080p und H.265 sind Teil von LXStudio Pro. Lizenz oben rechts eingeben.")
                        }
                    }
                }

                ProgressBar {
                    Layout.fillWidth: true
                    visible: app.exporting
                    value: app.exportProgress
                }
                RowLayout {
                    spacing: 8
                    Button {
                        text: [qsTr("Video exportieren …"), qsTr("Audio exportieren …"), qsTr("Stems exportieren …"), qsTr("MIDI exportieren …")][root.kind]
                        icon.name: "export"
                        highlighted: true
                        enabled: !app.exporting && app.duration > 0 && !(root.kind === 0 && root.needsPro && !app.isPro)
                        onClicked: [videoDialog, audioDialog, stemsDialog, midiDialog][root.kind].open()
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
                    color: Theme.dim
                }
            }
        }
    }
}
