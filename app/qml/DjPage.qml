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
            Layout.preferredHeight: 62
            controller: dj
            deck: 0
            accent: "#3d8bff"
        }
        DjWave {
            Layout.fillWidth: true
            Layout.preferredHeight: 62
            controller: dj
            deck: 1
            accent: "#ff8a3d"
        }

        // ----------------------------------- Decks und Mixer
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 430
            spacing: 8
            DjDeck { deck: 0; Layout.fillWidth: true; Layout.fillHeight: true }

            Rectangle {
                Layout.preferredWidth: 190
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
                        Label { text: qsTr("Master"); color: Theme.dim }
                        Slider { Layout.fillWidth: true; from: 0; to: 1.5; value: dj.masterVolume; onMoved: dj.masterVolume = value }
                    }
                }
            }

            DjDeck { deck: 1; Layout.fillWidth: true; Layout.fillHeight: true }
        }

        // ----------------------------------------- Bibliothek
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: Math.max(130, page.height * 0.2)
            color: Theme.panel
            radius: 10
            border.color: Theme.border
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 8
                spacing: 6
                RowLayout {
                    Layout.fillWidth: true
                    Label { text: qsTr("Bibliothek"); font.bold: true }
                    Label { text: qsTr("%1 Tracks").arg(dj.library.length); color: Theme.dim }
                    Button { text: qsTr("Dateien …"); icon.name: "plus"; flat: true; onClicked: addDialog.open() }
                    Button { text: qsTr("Ordner …"); icon.name: "folder"; flat: true; onClicked: folderDialog.open() }
                    Item { Layout.fillWidth: true }
                    TextField {
                        Layout.preferredWidth: 260
                        placeholderText: qsTr("Suchen (Titel, Interpret)")
                        text: dj.search
                        onTextEdited: dj.search = text
                    }
                }
                // Spaltenköpfe (klicken = sortieren)
                RowLayout {
                    id: header
                    Layout.fillWidth: true
                    spacing: 0
                    readonly property var widths: [0, 220, 80, 70, 70, 60, 90]
                    Repeater {
                        model: [{ t: qsTr("Titel"), c: 0 }, { t: qsTr("Interpret"), c: 1 }, { t: "BPM", c: 2 }, { t: qsTr("Tonart"), c: 3 }, { t: qsTr("Dauer"), c: 4 }, { t: "Stems", c: -1 }, { t: qsTr("Laden"), c: -1 }]
                        Rectangle {
                            required property var modelData
                            required property int index
                            Layout.fillWidth: index === 0
                            Layout.preferredWidth: header.widths[index]
                            height: 24
                            color: "transparent"
                            Label {
                                anchors.verticalCenter: parent.verticalCenter
                                x: 8
                                text: parent.modelData.t + (Math.abs(dj.sortColumn) === parent.modelData.c && parent.modelData.c >= 0 ? (dj.sortColumn < 0 ? " ▼" : " ▲") : "")
                                color: Theme.dim
                                font.pixelSize: 11
                                font.bold: true
                            }
                            MouseArea {
                                anchors.fill: parent
                                enabled: parent.modelData.c >= 0
                                onClicked: {
                                    const c = parent.modelData.c
                                    dj.sortColumn = (Math.abs(dj.sortColumn) % 10 === c && dj.sortColumn >= 0) ? -(c === 0 ? 10 : c) : c
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
                    ScrollBar.vertical: ScrollBar {}
                    delegate: Rectangle {
                        id: row
                        required property var modelData
                        required property int index
                        readonly property string trackPath: modelData.path
                        width: list.width
                        height: 28
                        color: rowMouse.containsMouse ? Theme.hover : index % 2 ? Theme.panel2 : "transparent"
                        opacity: modelData.exists ? 1 : 0.45
                        MouseArea {
                            id: rowMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            acceptedButtons: Qt.LeftButton | Qt.RightButton
                            drag.target: dragProxy
                            onReleased: { dragProxy.x = 0; dragProxy.y = 0 }
                            onDoubleClicked: {
                                // In das Deck, das gerade nicht spielt (zuerst 1)
                                const a = dj.deckA, b = dj.deckB
                                dj.loadTrack(!a.playing ? 0 : !b.playing ? 1 : 0, row.modelData.path)
                            }
                            onClicked: function (m) { if (m.button === Qt.RightButton) rowMenu.popup() }
                        }
                        Item { id: dragProxy; width: 1; height: 1; Drag.active: rowMouse.drag.active; Drag.keys: ["loggx/djtrack"]; Drag.source: row }
                        Menu {
                            id: rowMenu
                            MenuItem { text: qsTr("In Deck 1 laden"); onTriggered: dj.loadTrack(0, row.modelData.path) }
                            MenuItem { text: qsTr("In Deck 2 laden"); onTriggered: dj.loadTrack(1, row.modelData.path) }
                            MenuItem { text: qsTr("Stems mit KI vorbereiten"); enabled: dj.aiAvailable && !row.modelData.stems; onTriggered: dj.prepareStems(row.modelData.path) }
                            MenuItem { text: qsTr("Aus der Bibliothek entfernen"); onTriggered: dj.removeTrack(row.modelData.path) }
                        }
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
                            Label { Layout.preferredWidth: 220; leftPadding: 8; elide: Text.ElideRight; text: row.modelData.artist; color: Theme.dim }
                            Label { Layout.preferredWidth: 80; leftPadding: 8; text: row.modelData.bpm > 0 ? row.modelData.bpm.toFixed(1) : row.modelData.analyzed ? "–" : "…"; font.family: "monospace" }
                            Label { Layout.preferredWidth: 70; leftPadding: 8; text: row.modelData.key || (row.modelData.analyzed ? "–" : "…"); color: Theme.ok; font.bold: true }
                            Label { Layout.preferredWidth: 70; leftPadding: 8; text: row.modelData.durationText; color: Theme.dim; font.family: "monospace" }
                            Item {
                                Layout.preferredWidth: 60
                                implicitHeight: 20
                                Icon { anchors.centerIn: parent; width: 14; height: 14; name: "sparkles"; color: Theme.ok; visible: row.modelData.stems }
                            }
                            RowLayout {
                                Layout.preferredWidth: 90
                                spacing: 2
                                Button { text: "1"; implicitWidth: 34; implicitHeight: 22; onClicked: dj.loadTrack(0, row.modelData.path) }
                                Button { text: "2"; implicitWidth: 34; implicitHeight: 22; onClicked: dj.loadTrack(1, row.modelData.path) }
                            }
                        }
                    }
                    Label {
                        anchors.centerIn: parent
                        visible: list.count === 0
                        color: Theme.faint
                        horizontalAlignment: Text.AlignHCenter
                        text: qsTr("Noch keine Musik. Dateien oder Ordner hierher ziehen oder „Dateien …“ wählen.\nTempo (BPM) und Tonart werden automatisch erkannt.")
                    }
                }
            }
        }
    }
}
