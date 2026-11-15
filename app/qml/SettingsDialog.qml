// Allgemeine Einstellungen: Audio, DJ, Controller, KI, Bibliothek, Über
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import LoggX 1.0
import LoggXStyle

Dialog {
    id: dialog
    title: qsTr("Einstellungen")
    modal: true
    anchors.centerIn: parent
    width: Math.min(parent.width - 60, 760)
    height: Math.min(parent.height - 60, 620)
    property alias tab: tabs.currentIndex
    padding: 0

    component Row2: RowLayout {
        property string label
        property string hint
        Layout.fillWidth: true
        spacing: 12
        ColumnLayout {
            Layout.preferredWidth: 230
            Layout.alignment: Qt.AlignTop
            spacing: 2
            Label { text: parent.parent.label; color: Theme.text }
            Label {
                visible: parent.parent.hint !== ""
                Layout.preferredWidth: 230
                text: parent.parent.hint
                color: Theme.faint
                font.pixelSize: 11
                wrapMode: Text.WordWrap
            }
        }
    }
    component Heading: Label {
        Layout.topMargin: 6
        font.pixelSize: 12
        font.weight: Font.DemiBold
        color: Theme.dim
    }

    contentItem: RowLayout {
        spacing: 0
        // Kategorien links
        Rectangle {
            Layout.preferredWidth: 170
            Layout.fillHeight: true
            color: Theme.panel
            radius: 0
            Column {
                id: tabs
                property int currentIndex: 0
                anchors.fill: parent
                anchors.margins: 8
                spacing: 2
                Repeater {
                    model: [
                        { text: qsTr("Audio"), icon: "wave" },
                        { text: qsTr("DJ"), icon: "disc" },
                        { text: qsTr("Controller"), icon: "sliders" },
                        { text: qsTr("Bibliothek"), icon: "folder" },
                        { text: qsTr("KI"), icon: "sparkles" },
                        { text: qsTr("Über"), icon: "home" }
                    ]
                    ItemDelegate {
                        required property var modelData
                        required property int index
                        width: parent.width
                        height: 34
                        highlighted: tabs.currentIndex === index
                        onClicked: tabs.currentIndex = index
                        contentItem: Row {
                            spacing: 9
                            Icon { width: 16; height: 16; anchors.verticalCenter: parent.verticalCenter; name: modelData.icon; color: highlighted ? Theme.accent : Theme.dim }
                            Label { anchors.verticalCenter: parent.verticalCenter; text: modelData.text; color: highlighted ? Theme.text : Theme.dim }
                        }
                    }
                }
            }
        }
        Rectangle { Layout.fillHeight: true; Layout.preferredWidth: 1; color: Theme.border }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 18
            currentIndex: tabs.currentIndex

            // ------------------------------------------------------ Audio
            ScrollView {
                clip: true
                contentWidth: availableWidth
                ColumnLayout {
                    width: parent.width
                    spacing: 12
                    Heading { text: qsTr("DJ-Ausgabe") }
                    Row2 {
                        label: qsTr("Ausgang (Master)")
                        hint: qsTr("Bei DJ-Controllern mit Soundkarte (z. B. DDJ-FLX4) den Controller wählen")
                        ComboBox {
                            Layout.fillWidth: true
                            model: [qsTr("Standard des Systems")].concat(dj.outputDevices)
                            currentIndex: dj.outputDevice === "" ? 0 : Math.max(0, dj.outputDevices.indexOf(dj.outputDevice) + 1)
                            onActivated: function (i) { dj.outputDevice = i === 0 ? "" : dj.outputDevices[i - 1] }
                        }
                    }
                    Row2 {
                        label: qsTr("Kopfhörer (Vorhören)")
                        hint: qsTr("Kanäle 3/4: Kopfhörerbuchse des Controllers. Eigenes Gerät: z. B. Laptop-Kopfhörer")
                        ComboBox {
                            Layout.fillWidth: true
                            model: [qsTr("Aus"), qsTr("Kanäle 3/4 des Ausgangs"), qsTr("Eigenes Gerät")]
                            currentIndex: dj.cueMode
                            onActivated: function (i) { dj.cueMode = i }
                        }
                    }
                    Row2 {
                        visible: dj.cueMode === 2
                        label: qsTr("Kopfhörer-Gerät")
                        ComboBox {
                            Layout.fillWidth: true
                            model: dj.outputDevices
                            currentIndex: Math.max(0, dj.outputDevices.indexOf(dj.headphoneDevice))
                            onActivated: function (i) { dj.headphoneDevice = dj.outputDevices[i] }
                        }
                    }
                    Row2 {
                        label: qsTr("Puffer")
                        hint: qsTr("Kleiner = direkter (Scratchen), größer = sicherer gegen Aussetzer")
                        ComboBox {
                            Layout.fillWidth: true
                            readonly property var values: [10, 20, 30, 50, 80, 120]
                            model: values.map(function (v) { return v + " ms" })
                            currentIndex: Math.max(0, values.indexOf(dj.bufferMs))
                            onActivated: function (i) { dj.bufferMs = values[i] }
                        }
                    }
                    Label {
                        Layout.fillWidth: true
                        text: dj.audioInfo === "" ? qsTr("Audio startet, sobald der DJ-Bereich geöffnet wird.") : dj.audioInfo
                        color: Theme.faint
                        wrapMode: Text.WordWrap
                    }
                    Button { text: qsTr("Audio neu starten"); onClicked: dj.restartAudio() }

                    Heading { text: qsTr("Aufnahme (Musik)") }
                    Row2 {
                        label: qsTr("Mikrofon")
                        ComboBox {
                            Layout.fillWidth: true
                            model: app.audioInputs
                            currentIndex: app.audioInput
                            onActivated: function (i) { app.audioInput = i }
                        }
                    }
                }
            }

            // ------------------------------------------------------ DJ
            ScrollView {
                clip: true
                contentWidth: availableWidth
                ColumnLayout {
                    width: parent.width
                    spacing: 12
                    Heading { text: qsTr("Tempo-Erkennung") }
                    Row2 {
                        label: qsTr("BPM-Bereich")
                        hint: qsTr("Erkanntes Tempo wird in diesen Bereich gelegt (halb/doppelt). Wie in rekordbox unter „Analyse“.")
                        ComboBox {
                            id: rangeBox
                            Layout.fillWidth: true
                            readonly property var ranges: [[70, 180], [78, 180], [88, 175], [60, 150], [98, 195], [130, 260]]
                            model: ranges.map(function (r) { return r[0] + " – " + r[1] + " BPM" })
                            currentIndex: {
                                for (let i = 0; i < ranges.length; ++i)
                                    if (Math.abs(ranges[i][0] - dj.bpmMin) < 0.5 && Math.abs(ranges[i][1] - dj.bpmMax) < 0.5) return i
                                return -1
                            }
                            onActivated: function (i) { dj.bpmMin = ranges[i][0]; dj.bpmMax = ranges[i][1] }
                        }
                    }
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: Theme.faint
                        text: qsTr("Stimmt das Tempo eines Tracks nicht: im Deck „×2“ / „/2“, „TAP“ oder Rechtsklick → „Tempo eingeben …“. Handarbeit wird nie überschrieben.")
                    }
                    Heading { text: qsTr("Decks") }
                    Row2 {
                        label: qsTr("Tempo-Bereich (Standard)")
                        ComboBox {
                            Layout.fillWidth: true
                            readonly property var values: [6, 8, 10, 16, 25, 50]
                            model: values.map(function (v) { return "±" + v + " %" })
                            currentIndex: Math.max(0, values.indexOf(dj.defaultTempoRange))
                            onActivated: function (i) { dj.defaultTempoRange = values[i] }
                        }
                    }
                    Row2 {
                        label: qsTr("Key Lock beim Laden")
                        hint: qsTr("Tonhöhe bleibt beim Tempo-Ändern gleich (Master Tempo)")
                        Switch { checked: dj.autoKeyLock; onToggled: dj.autoKeyLock = checked }
                    }
                    Row2 {
                        label: qsTr("Quantize")
                        hint: qsTr("Hot Cues, Loops und Sprünge rasten auf Schläge ein")
                        Switch { checked: dj.quantize; onToggled: dj.quantize = checked }
                    }
                }
            }

            // ------------------------------------------------- Controller
            ScrollView {
                clip: true
                contentWidth: availableWidth
                ColumnLayout {
                    width: parent.width
                    spacing: 12
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 64
                        radius: Theme.radius
                        color: djmidi.connected ? Theme.alpha(Theme.ok, 0.12) : Theme.panel
                        border.color: djmidi.connected ? Theme.alpha(Theme.ok, 0.5) : Theme.border
                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 12
                            spacing: 12
                            Icon { width: 28; height: 28; name: "sliders"; color: djmidi.connected ? Theme.ok : Theme.faint }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2
                                Label {
                                    text: !djmidi.available ? qsTr("Diese Version ist ohne MIDI gebaut")
                                        : djmidi.connected ? djmidi.device : qsTr("Kein Controller verbunden")
                                    font.weight: Font.DemiBold
                                }
                                Label {
                                    text: djmidi.connected ? qsTr("Zuordnung: %1").arg(djmidi.preset)
                                        : qsTr("Pioneer DDJ-FLX4 wird beim Einstecken automatisch erkannt. Andere Controller: unten verbinden und per MIDI-Learn zuordnen.")
                                    color: Theme.faint
                                    font.pixelSize: 11
                                    wrapMode: Text.WordWrap
                                    Layout.fillWidth: true
                                }
                            }
                            Button { visible: djmidi.connected; text: qsTr("Trennen"); onClicked: djmidi.disconnectPort() }
                        }
                    }
                    Row2 {
                        label: qsTr("Controller verwenden")
                        Switch { checked: djmidi.enabled; onToggled: djmidi.enabled = checked }
                    }
                    Row2 {
                        label: qsTr("MIDI-Geräte")
                        ColumnLayout {
                            Layout.fillWidth: true
                            Repeater {
                                model: djmidi.ports
                                RowLayout {
                                    required property string modelData
                                    Layout.fillWidth: true
                                    Label { Layout.fillWidth: true; text: modelData; elide: Text.ElideRight }
                                    Button {
                                        text: djmidi.device === modelData ? qsTr("Verbunden") : qsTr("Verbinden")
                                        enabled: djmidi.device !== modelData
                                        onClicked: djmidi.connectPort(modelData)
                                    }
                                }
                            }
                            Label { visible: djmidi.ports.length === 0; text: qsTr("Keine MIDI-Geräte gefunden"); color: Theme.faint }
                            Button { text: qsTr("Neu suchen"); onClicked: djmidi.refresh() }
                        }
                    }
                    Row2 {
                        label: qsTr("Tempo-Fader umkehren")
                        hint: qsTr("Standard wie bei Pioneer: nach unten = schneller")
                        Switch { checked: djmidi.invertTempo; onToggled: djmidi.invertTempo = checked }
                    }
                    Heading { text: qsTr("MIDI-Learn (eigene Zuordnung)") }
                    RowLayout {
                        Layout.fillWidth: true
                        ComboBox {
                            id: actionBox
                            Layout.fillWidth: true
                            model: djmidi.actions
                            textRole: "name"
                        }
                        ComboBox {
                            id: deckBox
                            Layout.preferredWidth: 110
                            model: [qsTr("Deck 1"), qsTr("Deck 2")]
                        }
                        Button {
                            text: djmidi.learning !== "" ? qsTr("Warte auf Regler …") : qsTr("Lernen")
                            highlighted: djmidi.learning !== ""
                            onClicked: djmidi.learning !== "" ? djmidi.cancelLearn() : djmidi.learn(djmidi.actions[actionBox.currentIndex].id, deckBox.currentIndex)
                        }
                    }
                    Label {
                        Layout.fillWidth: true
                        color: Theme.faint
                        font.pixelSize: 11
                        wrapMode: Text.WordWrap
                        text: qsTr("Funktion wählen, „Lernen“ drücken, dann Taste/Regler am Controller bewegen. Letzte Nachricht: %1").arg(djmidi.lastMessage === "" ? "–" : djmidi.lastMessage)
                    }
                    Repeater {
                        model: djmidi.mappings
                        RowLayout {
                            required property var modelData
                            required property int index
                            Layout.fillWidth: true
                            Label { text: modelData.midi; font.family: "monospace"; color: Theme.dim; Layout.preferredWidth: 70 }
                            Label { Layout.fillWidth: true; text: modelData.label + " · " + qsTr("Deck %1").arg(modelData.deck + 1) }
                            IconButton { iconName: "trash"; tip: qsTr("Zuordnung löschen"); onClicked: djmidi.removeMapping(index) }
                        }
                    }
                }
            }

            // ------------------------------------------------- Bibliothek
            ScrollView {
                clip: true
                contentWidth: availableWidth
                ColumnLayout {
                    width: parent.width
                    spacing: 12
                    Heading { text: qsTr("Übernehmen") }
                    Row2 {
                        label: qsTr("rekordbox")
                        hint: qsTr("In rekordbox: Datei → Exportieren → „Collection im XML-Format exportieren“. Übernimmt Tracks, BPM/Beatgrid, Tonart, Hot Cues, Bewertung und Playlists.")
                        Button { text: qsTr("rekordbox-XML wählen …"); onClicked: xmlDialog.open() }
                    }
                    Row2 {
                        label: qsTr("Apple Music / iTunes")
                        hint: qsTr("In Apple Music: Einstellungen → Erweitert → „iTunes-Library-XML mit anderen Apps teilen“. Datei: Musik/Music/Library.xml")
                        Button { text: qsTr("Library.xml wählen …"); onClicked: xmlDialog.open() }
                    }
                    Heading { text: qsTr("Streaming") }
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: Theme.dim
                        text: qsTr("Spotify, Apple Music, SoundCloud und Beatport erlauben das Abspielen ihrer Streams in DJ-Programmen nur mit eigenem Partnervertrag (so wie bei rekordbox oder Serato). Bis dahin: Tracks als Datei kaufen/laden (z. B. Beatport, Bandcamp) und hier importieren. Rechtsklick auf einen Track → „Online suchen“ öffnet ihn bei Beatport, SoundCloud, Spotify oder Apple Music.")
                    }
                    Heading { text: qsTr("Verlauf") }
                    Button { text: qsTr("Verlauf leeren"); onClicked: dj.clearHistory() }
                }
            }

            // ------------------------------------------------------ KI
            ColumnLayout {
                spacing: 12
                Heading { text: qsTr("Stimme / Stems") }
                Row2 {
                    label: qsTr("Standard-Methode")
                    hint: qsTr("Für „Gesang entfernen“ im Schnitt und in der Musik")
                    ComboBox {
                        Layout.fillWidth: true
                        enabled: app.aiAvailable
                        model: [qsTr("KI – beste Qualität"), qsTr("KI – schnell"), qsTr("Ohne KI (schnell, einfacher)")]
                        currentIndex: app.separationMethod
                        onActivated: function (i) { app.separationMethod = i }
                    }
                }
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: Theme.faint
                    text: app.aiAvailable ? qsTr("KI-Modelle (MDX-Net) werden beim ersten Gebrauch geladen und lokal gespeichert – danach läuft alles offline auf diesem Rechner.")
                                          : qsTr("Diese Version wurde ohne KI gebaut.")
                }
                Item { Layout.fillHeight: true }
            }

            // ------------------------------------------------------ Über
            ColumnLayout {
                spacing: 12
                RowLayout {
                    spacing: 14
                    LogoMark { size: 64 }
                    ColumnLayout {
                        spacing: 2
                        Label { text: "LXStudio"; font.pixelSize: 22; font.weight: Font.Bold }
                        Label { text: qsTr("Version %1 · %2").arg(Qt.application.version).arg(app.editionName); color: Theme.dim }
                    }
                }
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: Theme.dim
                    text: qsTr("Video schneiden, Musik machen und auflegen – in einer App. Bibliotheken: FFmpeg, Qt, Rubber Band, RtMidi, ONNX Runtime, pocketfft. MIDI-Nummern des DDJ-FLX4 aus der Zuordnung des freien DJ-Programms Mixxx.")
                }
                Item { Layout.fillHeight: true }
            }
        }
    }

    FileDialog {
        id: xmlDialog
        title: qsTr("Bibliothek übernehmen")
        nameFilters: [qsTr("Bibliothek (*.xml)")]
        onAccepted: dj.importLibraryXml(selectedFile)
    }

    footer: Item {
        implicitHeight: 56
        Rectangle { width: parent.width; height: 1; color: Theme.border }
        Label {
            anchors.left: parent.left
            anchors.leftMargin: 18
            anchors.verticalCenter: parent.verticalCenter
            text: dj.status
            color: Theme.faint
            elide: Text.ElideRight
            width: parent.width - 160
        }
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
