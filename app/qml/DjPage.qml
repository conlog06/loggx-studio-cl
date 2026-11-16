import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import LoggX 1.0
import LoggXStyle

// Arbeitsbereich „DJ“: Wellenformen, zwei Decks, Mixer mit FX, Bibliothek.
Rectangle {
    id: page
    color: Theme.bg
    property bool active: false
    onActiveChanged: {
        dj.setActive(active)
        if (active) app.pause()   // Projekt-Wiedergabe nicht gleichzeitig
    }

    FileDialog {
        id: addDialog
        title: qsTr("Musik zur DJ-Bibliothek hinzufügen")
        fileMode: FileDialog.OpenFiles
        nameFilters: [qsTr("Musik (*.mp3 *.wav *.flac *.aiff *.aif *.m4a *.aac *.ogg *.opus *.mp4)"), qsTr("Alle Dateien (*)")]
        onAccepted: dj.importFiles(selectedFiles)
    }
    FolderDialog {
        id: folderDialog
        title: qsTr("Ordner mit Musik hinzufügen")
        onAccepted: dj.importFiles([selectedFolder])
    }
    Connections {
        target: dj
        function onMixRecorded(path) { mixDone.path = path; mixDone.open() }
    }
    Dialog {
        id: mixDone
        property string path
        anchors.centerIn: parent
        modal: true
        title: qsTr("Mix gespeichert")
        Label { text: qsTr("Den Mitschnitt ins aktuelle Projekt übernehmen?"); wrapMode: Text.WordWrap }
        footer: Item {
            implicitHeight: 56
            RowLayout {
                anchors.right: parent.right
                anchors.rightMargin: 16
                anchors.verticalCenter: parent.verticalCenter
                Button { text: qsTr("Nein"); flat: true; onClicked: mixDone.close() }
                Button {
                    text: qsTr("In die Medien")
                    highlighted: true
                    onClicked: { app.dropFiles(["file:///" + mixDone.path.replace(/^\/+/, "")], -2); mixDone.close() }
                }
            }
        }
    }

    FileDialog {
        id: xmlDialog
        title: qsTr("rekordbox-XML oder Apple-Music-/iTunes-Library.xml übernehmen")
        nameFilters: [qsTr("Bibliothek (*.xml)")]
        onAccepted: dj.importLibraryXml(selectedFile)
    }
    // Playlist anlegen/umbenennen
    Dialog {
        id: nameDialog
        property int index: -1
        property var addPaths: []
        title: index < 0 ? qsTr("Neue Playlist") : qsTr("Playlist umbenennen")
        modal: true
        anchors.centerIn: parent
        width: 360
        standardButtons: Dialog.Ok | Dialog.Cancel
        TextField { id: nameField; width: parent.width; onAccepted: nameDialog.accept() }
        onOpened: { nameField.text = index >= 0 && dj.playlists[index] ? dj.playlists[index].name : ""; nameField.forceActiveFocus() }
        onAccepted: {
            if (index >= 0) dj.renamePlaylist(index, nameField.text)
            else {
                const i = dj.createPlaylist(nameField.text)
                if (addPaths.length > 0) dj.addToPlaylist(i, addPaths)
            }
            addPaths = []
        }
        onRejected: addPaths = []
    }
    // Tempo von Hand
    Dialog {
        id: bpmDialog
        property string path
        property int deck: -1
        title: qsTr("Tempo eingeben")
        modal: true
        anchors.centerIn: parent
        width: 340
        standardButtons: Dialog.Ok | Dialog.Cancel
        ColumnLayout {
            width: parent.width
            TextField {
                id: bpmField
                Layout.fillWidth: true
                validator: DoubleValidator { bottom: 20; top: 400; decimals: 2 }
                inputMethodHints: Qt.ImhFormattedNumbersOnly
                onAccepted: bpmDialog.accept()
            }
            Label { text: qsTr("z. B. 138 oder 143,75 – das Raster bleibt am ersten Schlag"); color: Theme.faint; font.pixelSize: 11 }
        }
        onOpened: { bpmField.selectAll(); bpmField.forceActiveFocus() }
        onAccepted: {
            const v = parseFloat(bpmField.text.replace(",", "."))
            if (v > 0) { if (deck >= 0) dj.setDeckBpm(deck, v); else dj.setTrackBpm(path, v) }
        }
    }
    function askBpm(path, deck, current) {
        bpmDialog.path = path
        bpmDialog.deck = deck
        bpmField.text = current > 0 ? current.toFixed(2) : ""
        bpmDialog.open()
    }
    property bool samplerOpen: false
    Menu {
        id: sampleMenu
        property int slot: 0
        MenuItem { text: qsTr("Datei laden …"); icon.name: "folder"; onTriggered: sampleDialog.open() }
        MenuItem { text: qsTr("Eingebauten Klang"); onTriggered: dj.resetSample(sampleMenu.slot) }
        MenuItem { text: qsTr("Stoppen"); onTriggered: dj.stopSample(sampleMenu.slot) }
    }
    FileDialog {
        id: sampleDialog
        title: qsTr("Sample laden")
        nameFilters: [qsTr("Audio (*.wav *.mp3 *.flac *.aiff *.aif *.ogg *.m4a)")]
        onAccepted: dj.loadSample(sampleMenu.slot, selectedFile)
    }

    // Tastatur ohne Controller: Deck 1 links (A Cue, S Play, D Sync, Q/W Bend,
    // 1–4 Hot Cues), Deck 2 rechts (J Cue, K Play, L Sync, O/P Bend, 7–0)
    Shortcut { enabled: page.active; sequence: "S"; onActivated: dj.togglePlay(0) }
    Shortcut { enabled: page.active; sequence: "A"; onActivated: dj.cue(0) }
    Shortcut { enabled: page.active; sequence: "D"; onActivated: dj.sync(0) }
    Shortcut { enabled: page.active; sequence: "Q"; autoRepeat: true; onActivated: dj.jogBend(0, -0.02) }
    Shortcut { enabled: page.active; sequence: "W"; autoRepeat: true; onActivated: dj.jogBend(0, 0.02) }
    Shortcut { enabled: page.active; sequence: "K"; onActivated: dj.togglePlay(1) }
    Shortcut { enabled: page.active; sequence: "J"; onActivated: dj.cue(1) }
    Shortcut { enabled: page.active; sequence: "L"; onActivated: dj.sync(1) }
    Shortcut { enabled: page.active; sequence: "O"; autoRepeat: true; onActivated: dj.jogBend(1, -0.02) }
    Shortcut { enabled: page.active; sequence: "P"; autoRepeat: true; onActivated: dj.jogBend(1, 0.02) }
    Repeater {
        model: 4
        Item {
            required property int index
            Shortcut { enabled: page.active; sequence: String(index + 1); onActivated: dj.hotCue(0, index) }
            Shortcut { enabled: page.active; sequence: String((index + 7) % 10); onActivated: dj.hotCue(1, index) }
        }
    }
    Shortcut { enabled: page.active; sequences: ["Ctrl++", "Ctrl+="]; onActivated: dj.zoomWave(1) }
    Shortcut { enabled: page.active; sequence: "Ctrl+-"; onActivated: dj.zoomWave(-1) }

    Menu {
        id: playlistMenu
        property int index: -1
        MenuItem { text: qsTr("Umbenennen …"); onTriggered: { nameDialog.index = playlistMenu.index; nameDialog.open() } }
        MenuItem { text: qsTr("Löschen"); danger: true; icon.name: "trash"; onTriggered: dj.deletePlaylist(playlistMenu.index) }
    }
    // Rechtsklick auf einen Track
    Menu {
        id: trackMenu
        property string path
        property var info: ({})
        MenuItem { text: qsTr("In Deck 1 laden"); hint: "←"; onTriggered: dj.loadTrack(0, trackMenu.path) }
        MenuItem { text: qsTr("In Deck 2 laden"); hint: "→"; onTriggered: dj.loadTrack(1, trackMenu.path) }
        MenuItem { text: qsTr("Im Musik-Editor öffnen"); icon.name: "piano"; onTriggered: dj.openInMusicEditor(trackMenu.path) }
        MenuSeparator {}
        Menu {
            title: qsTr("Zu Playlist hinzufügen")
            Repeater {
                model: dj.playlists
                MenuItem {
                    required property var modelData
                    required property int index
                    text: modelData.name
                    onTriggered: dj.addToPlaylist(index, [trackMenu.path])
                }
            }
            MenuSeparator {}
            MenuItem { text: qsTr("Neue Playlist …"); icon.name: "plus"; onTriggered: { nameDialog.index = -1; nameDialog.addPaths = [trackMenu.path]; nameDialog.open() } }
        }
        MenuItem {
            text: qsTr("Aus dieser Playlist entfernen")
            visible: dj.view >= 0
            height: visible ? implicitHeight : 0
            onTriggered: dj.removeFromPlaylist(dj.view, trackMenu.path)
        }
        Menu {
            title: qsTr("Tempo (BPM)")
            MenuItem { text: qsTr("Tempo eingeben …"); onTriggered: page.askBpm(trackMenu.path, -1, trackMenu.info.bpm || 0) }
            MenuItem { text: qsTr("Doppelt (×2)"); enabled: trackMenu.info.bpm > 0; onTriggered: dj.setTrackBpm(trackMenu.path, trackMenu.info.bpm * 2) }
            MenuItem { text: qsTr("Halb (/2)"); enabled: trackMenu.info.bpm > 0; onTriggered: dj.setTrackBpm(trackMenu.path, trackMenu.info.bpm / 2) }
            MenuItem { text: qsTr("Auf ganze Zahl runden"); enabled: trackMenu.info.bpm > 0; onTriggered: dj.setTrackBpm(trackMenu.path, Math.round(trackMenu.info.bpm)) }
            MenuSeparator {}
            MenuItem { text: qsTr("Neu analysieren"); onTriggered: dj.reanalyze(trackMenu.path) }
        }
        Menu {
            title: qsTr("Bewertung")
            Repeater {
                model: 6
                MenuItem {
                    required property int index
                    text: index === 0 ? qsTr("Keine") : "★".repeat(index)
                    checkable: true
                    checked: trackMenu.info.rating === index
                    onTriggered: dj.setRating(trackMenu.path, index)
                }
            }
        }
        Menu {
            title: qsTr("Online suchen")
            MenuItem { text: "Beatport"; onTriggered: Qt.openUrlExternally(dj.searchUrl("beatport", trackMenu.path)) }
            MenuItem { text: "SoundCloud"; onTriggered: Qt.openUrlExternally(dj.searchUrl("soundcloud", trackMenu.path)) }
            MenuItem { text: "Spotify"; onTriggered: Qt.openUrlExternally(dj.searchUrl("spotify", trackMenu.path)) }
            MenuItem { text: "Apple Music"; onTriggered: Qt.openUrlExternally(dj.searchUrl("applemusic", trackMenu.path)) }
            MenuItem { text: "TIDAL"; onTriggered: Qt.openUrlExternally(dj.searchUrl("tidal", trackMenu.path)) }
            MenuItem { text: "YouTube"; onTriggered: Qt.openUrlExternally(dj.searchUrl("youtube", trackMenu.path)) }
        }
        MenuItem { text: qsTr("Stems mit KI vorbereiten"); icon.name: "sparkles"; enabled: dj.aiAvailable && !trackMenu.info.stems; onTriggered: dj.prepareStems(trackMenu.path) }
        MenuItem {
            text: qsTr("Im Ordner zeigen")
            icon.name: "folder"
            onTriggered: Qt.openUrlExternally("file:///" + trackMenu.path.substring(0, trackMenu.path.lastIndexOf("/")).replace(/^\/+/, ""))
        }
        MenuSeparator {}
        MenuItem { text: qsTr("Aus der Bibliothek entfernen"); danger: true; icon.name: "trash"; onTriggered: dj.removeTrack(trackMenu.path) }
    }

    // Dateien von außen → Bibliothek
    DropArea {
        anchors.fill: parent
        keys: ["text/uri-list"]
        onDropped: function (drop) { if (drop.hasUrls) dj.importFiles(drop.urls) }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 8

        // ---------------------------------------------- Kopfzeile
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Icon { width: 20; height: 20; name: "disc"; color: Theme.accent }
            Label { text: qsTr("DJ"); font.pixelSize: 18; font.bold: true }
            Divider { Layout.leftMargin: 6; Layout.rightMargin: 6 }
            Button {
                text: "Q"
                checkable: true
                checked: dj.quantize
                highlighted: checked
                implicitWidth: 40
                onClicked: dj.quantize = checked
                ToolTip.text: qsTr("Quantisieren: Cues und Loops rasten auf den Schlag ein")
                ToolTip.visible: hovered
                ToolTip.delay: 500
            }
            Button {
                text: qsTr("Sampler")
                icon.name: "layers"
                checkable: true
                checked: page.samplerOpen
                highlighted: checked
                onClicked: page.samplerOpen = checked
            }
            Button {
                icon.name: "record"
                text: dj.recording ? qsTr("REC %1:%2").arg(Math.floor(dj.recordSeconds / 60)).arg(("0" + Math.floor(dj.recordSeconds % 60)).slice(-2)) : qsTr("Mix aufnehmen")
                highlighted: dj.recording
                onClicked: dj.recording ? dj.stopRecording() : dj.startRecording()
            }
            Divider { Layout.leftMargin: 6; Layout.rightMargin: 6 }
            // Effekt-Einheit
            Label { text: "FX"; font.bold: true; color: "#b05bff" }
            ComboBox {
                implicitWidth: 110
                model: [qsTr("Echo"), qsTr("Hall"), qsTr("Bremse")]
                currentIndex: dj.fxType
                onActivated: function (i) { dj.setFx(i, dj.fxBeats, dj.fxWet, dj.fxTarget) }
            }
            ComboBox {
                implicitWidth: 72
                readonly property var beats: [0.25, 0.5, 0.75, 1, 2, 4]
                model: ["1/4", "1/2", "3/4", "1", "2", "4"]
                currentIndex: Math.max(0, beats.indexOf(dj.fxBeats))
                onActivated: function (i) { dj.setFx(dj.fxType, beats[i], dj.fxWet, dj.fxTarget) }
                ToolTip.text: qsTr("Länge in Schlägen")
                ToolTip.visible: hovered
                ToolTip.delay: 500
            }
            Repeater {
                model: ["1", "2", qsTr("Master")]
                Button {
                    required property string modelData
                    required property int index
                    text: modelData
                    checkable: true
                    checked: dj.fxTarget === index
                    highlighted: checked
                    onClicked: dj.setFx(dj.fxType, dj.fxBeats, dj.fxWet, index)
                }
            }
            Label { text: qsTr("Anteil"); color: Theme.dim }
            Slider { implicitWidth: 100; from: 0; to: 1; value: dj.fxWet; onMoved: dj.setFx(dj.fxType, dj.fxBeats, value, dj.fxTarget) }
            Button {
                text: qsTr("FX an")
                checkable: true
                checked: dj.fxOn
                highlighted: checked
                onClicked: dj.setFxOn(checked)
            }
            Item { Layout.fillWidth: true }
            Row {
                visible: dj.working
                spacing: 8
                Label { text: dj.workLabel; anchors.verticalCenter: parent.verticalCenter }
                ProgressBar { width: 140; value: dj.workProgress; anchors.verticalCenter: parent.verticalCenter }
                IconButton { iconName: "close"; tip: qsTr("Abbrechen"); onClicked: dj.cancelWork() }
            }
            Label { text: dj.status; color: Theme.dim; elide: Text.ElideRight; Layout.maximumWidth: 420 }
        }

        // --------------------------------- mitlaufende Wellenformen
        DjWave {
            Layout.fillWidth: true
            Layout.preferredHeight: 56
            controller: dj
            deck: 0
            zoom: dj.waveZoom
            accent: "#3d8bff"
        }
        DjWave {
            Layout.fillWidth: true
            Layout.preferredHeight: 56
            controller: dj
            deck: 1
            zoom: dj.waveZoom
            accent: "#ff8a3d"
        }

        // ------------------------------------------ Sampler
        Rectangle {
            visible: page.samplerOpen
            Layout.fillWidth: true
            Layout.preferredHeight: 54
            radius: 8
            color: Theme.panel
            border.color: Theme.border
            RowLayout {
                anchors.fill: parent
                anchors.margins: 6
                spacing: 5
                Label { text: qsTr("SAMPLER"); color: Theme.faint; font.pixelSize: 10; font.bold: true }
                Repeater {
                    model: dj.sampler
                    Rectangle {
                        required property var modelData
                        required property int index
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        radius: 6
                        color: modelData.playing ? Theme.alpha(Theme.warn, 0.45) : padMouse.containsMouse ? Theme.hover : Theme.panel2
                        border.color: modelData.custom ? Theme.accent : Theme.border
                        Label {
                            anchors.fill: parent
                            anchors.margins: 4
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                            text: (index + 1) + "  " + modelData.name
                            font.pixelSize: 11
                        }
                        MouseArea {
                            id: padMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            acceptedButtons: Qt.LeftButton | Qt.RightButton
                            onPressed: function (m) {
                                if (m.button === Qt.LeftButton) {
                                    if (modelData.playing) dj.stopSample(index)
                                    else dj.triggerSample(index)
                                } else {
                                    sampleMenu.slot = index
                                    sampleMenu.popup()
                                }
                            }
                        }
                        // Datei auf einen Platz ziehen
                        DropArea {
                            anchors.fill: parent
                            onDropped: function (drop) { if (drop.hasUrls) dj.loadSample(index, drop.urls[0]) }
                        }
                    }
                }
                Knob { label: qsTr("PEGEL"); from: 0; to: 1.5; value: dj.samplerVolume; resetValue: 0.8; bipolar: false; tint: Theme.warn; onMoved: function (v) { dj.samplerVolume = v } }
            }
        }

        // ----------------------------------- Decks und Mixer
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 450
            spacing: 8
            DjDeck { deck: 0; Layout.fillWidth: true; Layout.fillHeight: true; onAskBpm: function (path, deck, current) { page.askBpm(path, deck, current) } }

            Rectangle {
                Layout.preferredWidth: 200
                Layout.fillHeight: true
                color: Theme.panel
                radius: 10
                border.color: Theme.border
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 6
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        spacing: 4
                        Repeater {
                            model: 2
                            ColumnLayout {
                                id: strip
                                required property int index
                                readonly property var info: index === 0 ? dj.deckA : dj.deckB
                                readonly property color accent: index === 0 ? "#3d8bff" : "#ff8a3d"
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                spacing: 0
                                Knob { Layout.alignment: Qt.AlignHCenter; label: "TRIM"; from: 0; to: 2; value: 1; resetValue: 1; bipolar: false; tint: strip.accent; onMoved: function (v) { dj.setTrim(strip.index, v) } }
                                Knob { Layout.alignment: Qt.AlignHCenter; label: "HI"; value: strip.info.eq ? strip.info.eq[2] : 0; tint: strip.accent; onMoved: function (v) { dj.setEq(strip.index, 2, v) } }
                                Knob { Layout.alignment: Qt.AlignHCenter; label: "MID"; value: strip.info.eq ? strip.info.eq[1] : 0; tint: strip.accent; onMoved: function (v) { dj.setEq(strip.index, 1, v) } }
                                Knob { Layout.alignment: Qt.AlignHCenter; label: "LOW"; value: strip.info.eq ? strip.info.eq[0] : 0; tint: strip.accent; onMoved: function (v) { dj.setEq(strip.index, 0, v) } }
                                Knob { Layout.alignment: Qt.AlignHCenter; label: "FILTER"; value: strip.info.filter || 0; tint: "#b05bff"; onMoved: function (v) { dj.setFilter(strip.index, v) } }
                                RowLayout {
                                    Layout.alignment: Qt.AlignHCenter
                                    Layout.fillHeight: true
                                    spacing: 4
                                    // Pegel
                                    Rectangle {
                                        Layout.fillHeight: true
                                        Layout.preferredWidth: 6
                                        radius: 3
                                        color: Theme.bg
                                        Rectangle {
                                            anchors.bottom: parent.bottom
                                            width: parent.width
                                            radius: 3
                                            height: parent.height * Math.min(1, strip.info.level || 0)
                                            color: (strip.info.level || 0) > 0.9 ? Theme.danger : (strip.info.level || 0) > 0.6 ? Theme.warn : Theme.ok
                                        }
                                    }
                                    Slider {
                                        Layout.fillHeight: true
                                        orientation: Qt.Vertical
                                        from: 0
                                        to: 1
                                        value: strip.info.fader !== undefined ? strip.info.fader : 1
                                        onMoved: dj.setFader(strip.index, value)
                                    }
                                }
                                Button {
                                    Layout.alignment: Qt.AlignHCenter
                                    text: "CUE"
                                    checkable: true
                                    checked: strip.info.pfl === true
                                    highlighted: checked
                                    implicitWidth: 54
                                    implicitHeight: 24
                                    font.pixelSize: 11
                                    onClicked: dj.setPfl(strip.index, checked)
                                    ToolTip.text: qsTr("Im Kopfhörer vorhören (vor dem Fader)")
                                    ToolTip.visible: hovered
                                    ToolTip.delay: 500
                                }
                            }
                        }
                        // Master-Pegel
                        Rectangle {
                            Layout.fillHeight: true
                            Layout.preferredWidth: 8
                            radius: 4
                            color: Theme.bg
                            Rectangle {
                                anchors.bottom: parent.bottom
                                width: parent.width
                                radius: 4
                                height: parent.height * Math.min(1, dj.masterLevel)
                                color: dj.masterLevel > 0.88 ? Theme.danger : dj.masterLevel > 0.6 ? Theme.warn : Theme.ok
                            }
                        }
                    }
                    // Crossfader
                    RowLayout {
                        Layout.fillWidth: true
                        Label { text: "A"; color: "#3d8bff"; font.bold: true }
                        Slider {
                            Layout.fillWidth: true
                            from: -1
                            to: 1
                            value: dj.crossfader
                            onMoved: dj.crossfader = value
                        }
                        Label { text: "B"; color: "#ff8a3d"; font.bold: true }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignHCenter
                        spacing: 2
                        Knob { label: qsTr("KH MIX"); from: 0; to: 1; value: dj.headMix; resetValue: 0.5; bipolar: false; tint: Theme.warn; onMoved: function (v) { dj.headMix = v } }
                        Knob { label: qsTr("KH PEGEL"); from: 0; to: 1.5; value: dj.headVolume; resetValue: 1; bipolar: false; tint: Theme.warn; onMoved: function (v) { dj.headVolume = v } }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Label { text: qsTr("Master"); color: Theme.dim }
                        Slider { Layout.fillWidth: true; from: 0; to: 1.5; value: dj.masterVolume; onMoved: dj.masterVolume = value }
                    }
                }
            }

            DjDeck { deck: 1; Layout.fillWidth: true; Layout.fillHeight: true; onAskBpm: function (path, deck, current) { page.askBpm(path, deck, current) } }
        }

        // ----------------------------------------- Bibliothek
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: Math.max(150, page.height * 0.22)
            color: Theme.panel
            radius: 10
            border.color: Theme.border
            RowLayout {
                anchors.fill: parent
                anchors.margins: 8
                spacing: 8

                // Seitenleiste: Sammlung, Verlauf, Playlists
                ColumnLayout {
                    Layout.preferredWidth: 190
                    Layout.fillHeight: true
                    spacing: 2
                    component SideItem: Rectangle {
                        property string text
                        property string iconName
                        property int viewIndex
                        property int count: -1
                        property bool removable: false
                        Layout.fillWidth: true
                        height: 26
                        radius: 5
                        color: dj.view === viewIndex ? Theme.alpha(Theme.accent, 0.22) : sideMouse.containsMouse ? Theme.hover : "transparent"
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 6
                            anchors.rightMargin: 6
                            spacing: 6
                            Icon { width: 14; height: 14; name: iconName; color: dj.view === viewIndex ? Theme.accent : Theme.dim }
                            Label { Layout.fillWidth: true; text: parent.parent.text; elide: Text.ElideRight; font.pixelSize: 12 }
                            Label { visible: count >= 0; text: count; color: Theme.faint; font.pixelSize: 11 }
                        }
                        MouseArea {
                            id: sideMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            acceptedButtons: Qt.LeftButton | Qt.RightButton
                            onClicked: function (m) {
                                dj.view = viewIndex
                                if (m.button === Qt.RightButton && removable) { playlistMenu.index = viewIndex; playlistMenu.popup() }
                            }
                        }
                        // Tracks hierher ziehen = zur Playlist hinzufügen
                        DropArea {
                            anchors.fill: parent
                            keys: ["loggx/djtrack"]
                            enabled: viewIndex >= 0
                            onDropped: function (drop) { dj.addToPlaylist(viewIndex, [drop.source.trackPath]) }
                            Rectangle { anchors.fill: parent; radius: 5; color: "transparent"; border.color: Theme.accent; visible: parent.containsDrag }
                        }
                    }
                    SideItem { text: qsTr("Sammlung"); iconName: "folder"; viewIndex: -1 }
                    SideItem { text: qsTr("Verlauf"); iconName: "clock"; viewIndex: -2 }
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.topMargin: 6
                        Label { text: qsTr("PLAYLISTS"); color: Theme.faint; font.pixelSize: 10; font.bold: true; Layout.fillWidth: true; leftPadding: 6 }
                        IconButton { iconName: "plus"; tip: qsTr("Neue Playlist"); implicitWidth: 24; implicitHeight: 24; onClicked: { nameDialog.index = -1; nameDialog.open() } }
                    }
                    ListView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        model: dj.playlists
                        spacing: 2
                        delegate: SideItem {
                            required property var modelData
                            required property int index
                            width: ListView.view.width
                            text: modelData.name
                            iconName: "music"
                            viewIndex: index
                            count: modelData.count
                            removable: true
                        }
                    }
                    Button { Layout.fillWidth: true; text: qsTr("rekordbox / iTunes …"); icon.name: "import"; flat: true; onClicked: xmlDialog.open() }
                }
                Rectangle { Layout.fillHeight: true; Layout.preferredWidth: 1; color: Theme.border }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: 6
                    RowLayout {
                        Layout.fillWidth: true
                        Label { text: dj.view === -1 ? qsTr("Sammlung") : dj.view === -2 ? qsTr("Verlauf") : (dj.playlists[dj.view] ? dj.playlists[dj.view].name : ""); font.bold: true }
                        Label { text: qsTr("%1 Tracks").arg(dj.trackCount); color: Theme.dim }
                        Button { text: qsTr("Dateien …"); icon.name: "plus"; flat: true; onClicked: addDialog.open() }
                        Button { text: qsTr("Ordner …"); icon.name: "folder"; flat: true; onClicked: folderDialog.open() }
                        Button { visible: dj.view === -2 && dj.trackCount > 0; text: qsTr("Als Playlist sichern"); flat: true; onClicked: { const i = dj.createPlaylist(qsTr("Set %1").arg(new Date().toLocaleDateString(Qt.locale(), "dd.MM.yyyy"))); const paths = []; for (let r = 0; r < dj.trackCount; ++r) paths.unshift(dj.pathAt(r)); dj.addToPlaylist(i, paths) } }
                        Item { Layout.fillWidth: true }
                        TextField {
                            Layout.preferredWidth: 260
                            placeholderText: qsTr("Suchen (Titel, Interpret, Genre, Kommentar)")
                            text: dj.search
                            onTextEdited: dj.search = text
                        }
                    }
                    // Spaltenköpfe (klicken = sortieren)
                    RowLayout {
                        id: header
                        Layout.fillWidth: true
                        spacing: 0
                        readonly property var columns: [
                            { t: qsTr("Titel"), c: 0, w: 0 }, { t: qsTr("Interpret"), c: 1, w: 180 }, { t: qsTr("Genre"), c: 5, w: 100 },
                            { t: "BPM", c: 2, w: 74 }, { t: qsTr("Tonart"), c: 3, w: 60 }, { t: qsTr("Zeit"), c: 4, w: 56 },
                            { t: qsTr("Bewertung"), c: 6, w: 80 }, { t: qsTr("Neu"), c: 7, w: 70 }, { t: "", c: -1, w: 96 }]
                        Repeater {
                            model: header.columns
                            Rectangle {
                                required property var modelData
                                required property int index
                                Layout.fillWidth: modelData.w === 0
                                Layout.preferredWidth: modelData.w
                                height: 22
                                color: "transparent"
                                Label {
                                    anchors.verticalCenter: parent.verticalCenter
                                    x: 8
                                    text: parent.modelData.t + (Math.abs(dj.sortColumn) % 20 === parent.modelData.c && parent.modelData.c >= 0 && (dj.sortColumn !== 0 || dj.view === -1) ? (dj.sortColumn < 0 ? " ▼" : " ▲") : "")
                                    color: Theme.dim
                                    font.pixelSize: 11
                                    font.bold: true
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    enabled: parent.modelData.c >= 0
                                    onClicked: {
                                        const c = parent.modelData.c
                                        dj.sortColumn = (Math.abs(dj.sortColumn) % 20 === c && dj.sortColumn >= 0) ? -(c === 0 ? 20 : c) : c
                                    }
                                }
                            }
                        }
                    }
                    ListView {
                        id: list
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        model: dj.library
                        currentIndex: dj.cursor
                        highlightFollowsCurrentItem: true
                        highlightMoveDuration: 0
                        keyNavigationEnabled: false
                        ScrollBar.vertical: ScrollBar {}
                        delegate: Rectangle {
                            id: row
                            required property var modelData
                            required property int index
                            readonly property string trackPath: modelData.path
                            readonly property bool selected: index === dj.cursor
                            width: list.width
                            height: 27
                            color: selected ? Theme.alpha(Theme.accent, 0.20) : rowMouse.containsMouse ? Theme.hover : index % 2 ? Theme.panel2 : "transparent"
                            opacity: modelData.exists ? 1 : 0.45
                            MouseArea {
                                id: rowMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                acceptedButtons: Qt.LeftButton | Qt.RightButton
                                drag.target: dragProxy
                                onPressed: { dj.cursor = row.index; list.forceActiveFocus() }
                                onReleased: { dragProxy.x = 0; dragProxy.y = 0 }
                                onDoubleClicked: {
                                    // In das Deck, das gerade nicht spielt (zuerst 1)
                                    const a = dj.deckA, b = dj.deckB
                                    dj.loadTrack(!a.playing ? 0 : !b.playing ? 1 : 0, row.modelData.path)
                                }
                                onClicked: function (m) { if (m.button === Qt.RightButton) { trackMenu.path = row.modelData.path; trackMenu.info = row.modelData; trackMenu.popup() } }
                            }
                            Item { id: dragProxy; width: 1; height: 1; Drag.active: rowMouse.drag.active; Drag.keys: ["loggx/djtrack"]; Drag.source: row }
                            RowLayout {
                                anchors.fill: parent
                                spacing: 0
                                Label {
                                    Layout.fillWidth: true
                                    leftPadding: 8
                                    elide: Text.ElideRight
                                    text: row.modelData.title
                                    color: row.modelData.onDeck === 1 ? "#3d8bff" : row.modelData.onDeck === 2 ? "#ff8a3d" : Theme.text
                                    font.bold: row.modelData.onDeck > 0
                                }
                                Label { Layout.preferredWidth: 180; leftPadding: 8; elide: Text.ElideRight; text: row.modelData.artist; color: Theme.dim }
                                Label { Layout.preferredWidth: 100; leftPadding: 8; elide: Text.ElideRight; text: row.modelData.genre; color: Theme.dim }
                                Label {
                                    Layout.preferredWidth: 74
                                    leftPadding: 8
                                    text: row.modelData.bpm > 0 ? row.modelData.bpm.toFixed(2) : row.modelData.analyzed ? "–" : "…"
                                    font.family: "monospace"
                                    color: row.modelData.gridEdited ? Theme.warn : Theme.text
                                }
                                Label { Layout.preferredWidth: 60; leftPadding: 8; text: row.modelData.key || (row.modelData.analyzed ? "–" : "…"); color: Theme.ok; font.bold: true }
                                Label { Layout.preferredWidth: 56; leftPadding: 8; text: row.modelData.durationText; color: Theme.dim; font.family: "monospace" }
                                Row {
                                    Layout.preferredWidth: 80
                                    leftPadding: 8
                                    spacing: 1
                                    Repeater {
                                        model: 5
                                        Label {
                                            required property int index
                                            text: "★"
                                            font.pixelSize: 11
                                            color: index < row.modelData.rating ? Theme.warn : Theme.alpha(Theme.faint, 0.5)
                                            MouseArea { anchors.fill: parent; onClicked: dj.setRating(row.modelData.path, row.modelData.rating === index + 1 ? 0 : index + 1) }
                                        }
                                    }
                                }
                                Label { Layout.preferredWidth: 70; leftPadding: 8; text: row.modelData.addedText; color: Theme.faint; font.pixelSize: 11 }
                                RowLayout {
                                    Layout.preferredWidth: 96
                                    spacing: 2
                                    Icon { width: 13; height: 13; name: "sparkles"; color: Theme.ok; opacity: row.modelData.stems ? 1 : 0 }
                                    Button { text: "1"; implicitWidth: 34; implicitHeight: 21; onClicked: dj.loadTrack(0, row.modelData.path) }
                                    Button { text: "2"; implicitWidth: 34; implicitHeight: 21; onClicked: dj.loadTrack(1, row.modelData.path) }
                                }
                            }
                        }
                        Label {
                            anchors.centerIn: parent
                            visible: list.count === 0
                            color: Theme.faint
                            horizontalAlignment: Text.AlignHCenter
                            text: dj.view >= 0 ? qsTr("Playlist ist leer – Tracks aus der Sammlung auf die Playlist links ziehen\noder Rechtsklick → „Zu Playlist hinzufügen“.")
                                : dj.view === -2 ? qsTr("Noch nichts gespielt.")
                                : qsTr("Noch keine Musik. Dateien oder Ordner hierher ziehen oder „Dateien …“ wählen.\nTempo (BPM) und Tonart werden automatisch erkannt – oder rekordbox/iTunes übernehmen.")
                        }
                        Keys.onUpPressed: dj.moveCursor(-1)
                        Keys.onDownPressed: dj.moveCursor(1)
                        Keys.onReturnPressed: { const a = dj.deckA; dj.loadCursor(!a.playing ? 0 : 1) }
                        Keys.onLeftPressed: dj.loadCursor(0)
                        Keys.onRightPressed: dj.loadCursor(1)
                    }
                }
            }
        }
    }
}
