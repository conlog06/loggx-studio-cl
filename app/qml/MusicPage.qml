import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import LoggX 1.0
import LoggXStyle

// Arbeitsbereich "Musik": Arrangement, Piano-Roll, Live-Spielen mit
// Aufnahme (Computertastatur, Maus oder MIDI-Keyboard) und Mixer.
FocusScope {
    id: root
    readonly property var clip: app.clip
    readonly property bool midi: clip.id !== undefined && clip.isMidi
    readonly property bool hasAudioClip: clip.id !== undefined && !clip.isMidi && !clip.isTitle && clip.hasAudio === true
    property bool keyboardActive: false

    // Computertastatur → Halbtöne über dem eingestellten C
    readonly property var keyMap: ({
        [Qt.Key_A]: 0, [Qt.Key_W]: 1, [Qt.Key_S]: 2, [Qt.Key_E]: 3, [Qt.Key_D]: 4, [Qt.Key_F]: 5,
        [Qt.Key_T]: 6, [Qt.Key_G]: 7, [Qt.Key_Z]: 8, [Qt.Key_Y]: 8, [Qt.Key_H]: 9, [Qt.Key_U]: 10,
        [Qt.Key_J]: 11, [Qt.Key_K]: 12, [Qt.Key_O]: 13, [Qt.Key_L]: 14
    })
    property var down: ({})
    signal importRequested()

    TimelineMenu {
        id: musicMenu
        onOpenPage: function (p) { if (p === 3) roll.forceActiveFocus() }
        onImportRequested: root.importRequested()
    }

    onKeyboardActiveChanged: {
        if (keyboardActive)
            root.forceActiveFocus()
        else
            app.allNotesOff()
    }
    Keys.onPressed: function (event) {
        if (!keyboardActive || event.modifiers & (Qt.ControlModifier | Qt.MetaModifier | Qt.AltModifier))
            return
        if (event.key === Qt.Key_Minus) { app.keyboardOctave = app.keyboardOctave - 1; event.accepted = true; return }
        if (event.key === Qt.Key_Plus) { app.keyboardOctave = app.keyboardOctave + 1; event.accepted = true; return }
        const off = keyMap[event.key]
        if (off === undefined) return
        event.accepted = true
        if (event.isAutoRepeat) return
        const pitch = app.keyboardOctave * 12 + off
        down[event.key] = pitch
        app.liveNoteOn(pitch, 0.8)
    }
    Keys.onReleased: function (event) {
        if (!keyboardActive || event.isAutoRepeat) return
        const pitch = down[event.key]
        if (pitch === undefined) return
        delete down[event.key]
        app.liveNoteOff(pitch)
        event.accepted = true
    }

    FileDialog {
        id: midiImportDialog
        title: qsTr("MIDI-Datei importieren")
        nameFilters: [qsTr("MIDI-Dateien (*.mid *.midi)")]
        onAccepted: app.importMidiFile(selectedFile)
    }
    FileDialog {
        id: midiExportDialog
        title: qsTr("Als MIDI-Datei exportieren")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "mid"
        nameFilters: [qsTr("MIDI-Datei (*.mid)")]
        onAccepted: app.exportMidiFile(selectedFile)
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ------------------------------------------------ Kopfzeile
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 50
            color: Theme.panel
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.border }
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                spacing: 6
                TabBar {
                    id: tabs
                    Layout.preferredWidth: 220
                    currentIndex: startTab
                    TabButton { text: qsTr("Noten"); icon.name: "piano" }
                    TabButton { text: qsTr("Mixer"); icon.name: "sliders" }
                }
                Divider { Layout.leftMargin: 6; Layout.rightMargin: 6 }
                IconButton { iconName: "skip-back"; tip: qsTr("Zum Anfang (Pos1)"); onClicked: app.seek(0) }
                Button {
                    icon.name: app.playing ? "pause" : "play"
                    text: app.playing ? qsTr("Pause") : qsTr("Abspielen")
                    highlighted: true
                    implicitWidth: 112
                    onClicked: app.recording ? app.stopRecording() : app.togglePlay()
                }
                IconButton {
                    iconName: "record"
                    tint: Theme.danger
                    enabled: !app.micRecording
                    checked: app.recording
                    tip: app.recording ? qsTr("Aufnahme stoppen (R)") : qsTr("Aufnahme: Noten auf die Instrumentspur spielen (R)")
                    onClicked: app.recording ? app.stopRecording() : app.startRecording()
                }
                IconButton {
                    iconName: "mic"
                    tint: app.micRecording ? Theme.danger : Theme.text
                    checked: app.micRecording
                    enabled: !app.recording
                    tip: app.micRecording ? qsTr("Mikrofon-Aufnahme stoppen") : qsTr("Mit dem Mikrofon aufnehmen (Gesang, Instrument) – Kopfhörer benutzen")
                    onClicked: app.micRecording ? app.stopMicRecording() : app.startMicRecording()
                }
                Rectangle {
                    Layout.preferredWidth: 150
                    Layout.preferredHeight: 32
                    Layout.leftMargin: 4
                    radius: Theme.radius
                    color: Theme.bg
                    border.color: app.recording || app.micRecording ? Theme.danger : Theme.border
                    Column {
                        anchors.centerIn: parent
                        Label {
                            anchors.horizontalCenter: parent.horizontalCenter
                            font.family: "monospace"
                            font.pixelSize: 14
                            text: {
                                const beat = 60 / app.bpm
                                const b = Math.max(0, Math.floor((app.position + 1e-6) / beat))
                                const bar = Math.max(1, app.beatsPerBar)
                                return (Math.floor(b / bar) + 1) + "." + (b % bar + 1) + "  ·  " + app.timecode(app.position).slice(3, 8)
                            }
                        }
                    }
                }
                Divider { Layout.leftMargin: 6; Layout.rightMargin: 6 }
                Icon { width: 16; height: 16; name: "metronome"; color: Theme.dim }
                SpinBox {
                    from: 20
                    to: 300
                    value: Math.round(app.bpm)
                    editable: true
                    // Musikprojekt: Noten und Audio ziehen mit; sonst nur das Raster.
                    onValueModified: {
                        if (app.musicMode) app.setSongTempo(value)
                        else app.bpm = value
                        value = Qt.binding(function () { return Math.round(app.bpm) })
                    }
                    ToolTip.text: app.musicMode ? qsTr("Song-Tempo: Noten und Audio werden mitgezogen (Tonhöhe bleibt)") : qsTr("Tempo des Takt-Rasters")
                    ToolTip.visible: hovered
                    ToolTip.delay: 600
                }
                Label { text: qsTr("BPM"); color: Theme.dim }
                IconButton {
                    iconName: "magnet"
                    checkable: true
                    checked: app.beatGrid
                    onToggled: app.beatGrid = checked
                    tip: qsTr("Takt-Raster: Lineal in Takten, Clips rasten auf Schläge ein")
                }
                IconButton {
                    iconName: "sparkles"
                    enabled: root.hasAudioClip
                    onClicked: app.detectTempo()
                    tip: qsTr("Tempo aus dem ausgewählten Audio-Clip erkennen")
                }
                Button {
                    text: qsTr("Transponieren")
                    icon.name: "key"
                    flat: true
                    onClicked: transposeMenu.open()
                    Menu {
                        id: transposeMenu
                        y: parent.height
                        MenuItem { text: qsTr("+1 Halbton"); onTriggered: app.transposeSong(1) }
                        MenuItem { text: qsTr("−1 Halbton"); onTriggered: app.transposeSong(-1) }
                        MenuItem { text: qsTr("+1 Ganzton"); onTriggered: app.transposeSong(2) }
                        MenuItem { text: qsTr("−1 Ganzton"); onTriggered: app.transposeSong(-2) }
                        MenuItem { text: qsTr("Oktave höher"); onTriggered: app.transposeSong(12) }
                        MenuItem { text: qsTr("Oktave tiefer"); onTriggered: app.transposeSong(-12) }
                    }
                }
                Item { Layout.fillWidth: true }
                Button { text: qsTr("Spur"); icon.name: "plus"; flat: true; onClicked: app.addInstrumentTrack() }
                Button { text: qsTr("MIDI-Clip"); icon.name: "plus"; onClicked: app.addMidiClip() }
                Button {
                    text: qsTr("MIDI")
                    icon.name: "file"
                    flat: true
                    onClicked: midiMenu.open()
                    Menu {
                        id: midiMenu
                        y: parent.height
                        MenuItem { text: qsTr("MIDI-Datei importieren …"); onTriggered: midiImportDialog.open() }
                        MenuItem { text: qsTr("Als MIDI-Datei exportieren …"); onTriggered: midiExportDialog.open() }
                    }
                }
            }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: tabs.currentIndex

            // ------------------------------------------------- Noten
            ColumnLayout {
                spacing: 0
                SplitView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    orientation: Qt.Vertical
                    handle: Rectangle { implicitHeight: 4; color: SplitHandle.hovered || SplitHandle.pressed ? Theme.accent : Theme.bg }

                    Timeline {
                        id: arrangement
                        SplitView.preferredHeight: app.musicMode ? 250 : 210
                        SplitView.minimumHeight: 110
                        controller: app
                        clip: true
                        Component.onCompleted: if (!app.musicMode) scrollY = contentHeight
                        onContextMenuRequested: function (x, y, index, header, clipId, seconds) { musicMenu.show(x, y, index, header, clipId, seconds) }
                        FileDropArea { anchors.fill: parent; timeline: arrangement }
                    }

                    RowLayout {
                        SplitView.fillHeight: true
                        spacing: 0

                        PianoRoll {
                            id: roll
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            controller: app
                            gridDivision: [1, 2, 4, 8][gridBox.currentIndex]
                            Keys.onDeletePressed: roll.deleteSelectedNote()
                        }

                        Rectangle {
                            Layout.preferredWidth: 260
                            Layout.fillHeight: true
                            color: Theme.panel
                            Rectangle { width: 1; height: parent.height; color: Theme.border }
                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 14
                                spacing: 8
                                RowLayout {
                                    Icon { width: 16; height: 16; name: "piano"; color: Theme.music }
                                    Label { text: qsTr("MIDI-Clip"); font.bold: true }
                                }
                                Label {
                                    Layout.fillWidth: true
                                    visible: !root.midi
                                    wrapMode: Text.WordWrap
                                    color: Theme.dim
                                    text: qsTr("Oben einen orangen MIDI-Clip anklicken, mit „MIDI-Clip“ einen anlegen – oder einfach auf ● drücken und spielen.")
                                }
                                ColumnLayout {
                                    visible: root.midi
                                    Layout.fillWidth: true
                                    spacing: 8
                                    Label { text: qsTr("Instrument"); color: Theme.dim }
                                    ComboBox {
                                        Layout.fillWidth: true
                                        model: app.instrumentNames
                                        currentIndex: root.midi ? root.clip.instrument : 0
                                        onActivated: function (i) { app.setTrackProperty(root.clip.trackIndex, "instrument", i) }
                                    }
                                    Label { text: qsTr("Raster"); color: Theme.dim }
                                    ComboBox {
                                        id: gridBox
                                        Layout.fillWidth: true
                                        model: [qsTr("Viertel"), qsTr("Achtel"), qsTr("Sechzehntel"), qsTr("Zweiunddreißigstel")]
                                        currentIndex: 2
                                    }
                                    Label {
                                        text: roll.selectedNote >= 0 ? qsTr("Anschlag der Note") : qsTr("Keine Note ausgewählt")
                                        color: Theme.dim
                                    }
                                    Slider {
                                        Layout.fillWidth: true
                                        enabled: roll.selectedNote >= 0
                                        from: 0.05
                                        to: 1
                                        value: roll.selectedVelocity
                                        onMoved: roll.setSelectedVelocity(value)
                                        onPressedChanged: pressed ? app.beginInteractive() : app.endInteractive()
                                    }
                                    ValueSlider {
                                        Layout.fillWidth: true
                                        label: qsTr("Transponieren"); prop: "pitch"; from: -12; to: 12; step: 1
                                        value: root.midi ? root.clip.pitch : 0
                                        resetValue: 0; decimals: 0; suffix: " HT"
                                    }
                                    Button { text: qsTr("Ansicht einpassen"); icon.name: "fit"; onClicked: roll.zoomToFit() }
                                }
                                Item { Layout.fillHeight: true }
                                Label {
                                    Layout.fillWidth: true
                                    wrapMode: Text.WordWrap
                                    color: Theme.faint
                                    font.pixelSize: Theme.smallFont
                                    text: qsTr("Klicken: Note setzen (ziehen = Länge)\nNote ziehen: verschieben\nRechter Rand: Länge ändern\nRechtsklick / Doppelklick: löschen\nMausrad: hoch/runter · Strg+Rad: Zoom")
                                }
                            }
                        }
                    }
                }

                // -------------------------------------- Live-Spielen
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 158
                    color: Theme.panel
                    Rectangle { width: parent.width; height: 1; color: Theme.border }
                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 14
                        ColumnLayout {
                            Layout.preferredWidth: 270
                            Layout.maximumWidth: 270
                            Layout.fillWidth: false
                            Layout.fillHeight: true
                            spacing: 6
                            RowLayout {
                                spacing: 6
                                Button {
                                    icon.name: "keyboard"
                                    text: root.keyboardActive ? qsTr("Tastatur spielt") : qsTr("Mit Tastatur spielen")
                                    checkable: true
                                    checked: root.keyboardActive
                                    highlighted: root.keyboardActive
                                    onToggled: root.keyboardActive = checked
                                    ToolTip.text: qsTr("A W S E D F T G Z H U J K = Töne, − / + = Oktave")
                                    ToolTip.visible: hovered
                                    ToolTip.delay: 400
                                }
                            }
                            RowLayout {
                                spacing: 4
                                Label { text: qsTr("Oktave"); color: Theme.dim }
                                IconButton { iconName: "minus"; onClicked: app.keyboardOctave = app.keyboardOctave - 1 }
                                Label { text: "C" + (app.keyboardOctave - 1); font.family: "monospace"; Layout.preferredWidth: 26; horizontalAlignment: Text.AlignHCenter }
                                IconButton { iconName: "plus"; onClicked: app.keyboardOctave = app.keyboardOctave + 1 }
                                CheckBox {
                                    text: qsTr("Quantisieren")
                                    checked: app.quantizeRecording
                                    onToggled: app.quantizeRecording = checked
                                    ToolTip.text: qsTr("Aufgenommene Noten auf Sechzehntel ausrichten")
                                    ToolTip.visible: hovered
                                    ToolTip.delay: 400
                                }
                            }
                            RowLayout {
                                spacing: 6
                                Icon { width: 16; height: 16; name: "piano"; color: app.midiInput >= 0 ? Theme.ok : Theme.dim }
                                ComboBox {
                                    Layout.fillWidth: true
                                    enabled: app.midiAvailable
                                    model: [app.midiAvailable ? qsTr("Kein MIDI-Keyboard") : qsTr("MIDI nicht eingebaut")].concat(app.midiInputs)
                                    currentIndex: app.midiInput + 1
                                    onActivated: function (i) { app.openMidiInput(i - 1) }
                                    onPressedChanged: if (pressed) app.refreshMidiInputs()
                                }
                            }
                            RowLayout {
                                spacing: 6
                                Icon { width: 16; height: 16; name: "mic"; color: app.micRecording ? Theme.danger : Theme.dim }
                                ComboBox {
                                    Layout.fillWidth: true
                                    enabled: !app.micRecording
                                    model: app.audioInputs.length > 0 ? app.audioInputs : [qsTr("Kein Mikrofon gefunden")]
                                    currentIndex: Math.min(app.audioInput, Math.max(0, app.audioInputs.length - 1))
                                    onActivated: function (i) { app.audioInput = i }
                                }
                                // Pegel
                                Rectangle {
                                    Layout.preferredWidth: 6
                                    Layout.preferredHeight: 26
                                    radius: 3
                                    color: Theme.bg
                                    Rectangle {
                                        anchors.bottom: parent.bottom
                                        width: parent.width
                                        radius: 3
                                        height: parent.height * Math.min(1, app.micLevel)
                                        color: app.micLevel > 0.9 ? Theme.danger : app.micLevel > 0.6 ? Theme.warn : Theme.ok
                                    }
                                }
                            }
                        }
                        LiveKeyboard {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            octaves: width > 900 ? 3 : 2
                            labelsFor: root.keyboardActive ? "computer" : ""
                        }
                    }
                }
            }

            // ------------------------------------------------- Mixer
            Rectangle {
                color: Theme.bg
                ScrollView {
                    anchors.fill: parent
                    anchors.margins: 12
                    contentHeight: availableHeight
                    ScrollBar.vertical.policy: ScrollBar.AlwaysOff
                    RowLayout {
                        height: parent.height
                        spacing: 8
                        Repeater {
                            model: app.tracks.filter(function (t) { return t.hasSound })
                            MixerStrip {
                                required property var modelData
                                Layout.fillHeight: true
                                track: modelData
                            }
                        }
                        Rectangle { Layout.fillHeight: true; width: 1; color: Theme.border; Layout.leftMargin: 4; Layout.rightMargin: 4 }
                        MixerStrip {
                            Layout.fillHeight: true
                            master: true
                        }
                    }
                }
            }
        }
    }
}
