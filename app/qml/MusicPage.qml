import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LoggX 1.0

// Arbeitsbereich "Musik": Arrangement (gleiche Zeitachse wie das Video),
// Piano-Roll für MIDI-Clips und Mixer.
Item {
    id: root
    readonly property var clip: app.clip
    readonly property bool midi: clip.id !== undefined && clip.isMidi

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ------------------------------------------------ Kopfzeile
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 44
            color: "#1f2229"
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                spacing: 8
                TabBar {
                    id: tabs
                    Layout.preferredWidth: 260
                    currentIndex: startTab
                    background: Item {}
                    TabButton { text: qsTr("Noten") }
                    TabButton { text: qsTr("Mixer") }
                }
                Rectangle { width: 1; height: 22; color: "#2d313b" }
                Button { text: app.playing ? qsTr("Pause") : qsTr("Abspielen"); highlighted: true; implicitWidth: 100; onClicked: app.togglePlay() }
                Button { text: "⏮"; onClicked: app.seek(0) }
                Label { text: app.timecode(app.position); font.family: "monospace"; font.pixelSize: 14 }
                Rectangle { width: 1; height: 22; color: "#2d313b" }
                Label { text: qsTr("Tempo") }
                SpinBox {
                    from: 40
                    to: 240
                    value: Math.round(app.bpm)
                    editable: true
                    onValueModified: app.bpm = value
                }
                Label { text: qsTr("BPM"); color: "#8a91a0" }
                Item { Layout.fillWidth: true }
                Button { text: qsTr("+ Instrumentspur"); onClicked: app.addInstrumentTrack() }
                Button { text: qsTr("+ MIDI-Clip"); highlighted: true; onClicked: app.addMidiClip() }
            }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: tabs.currentIndex

            // ------------------------------------------------- Noten
            SplitView {
                orientation: Qt.Vertical
                handle: Rectangle { implicitHeight: 5; color: SplitHandle.hovered || SplitHandle.pressed ? "#4f8cff" : "#101216" }

                Timeline {
                    id: arrangement
                    SplitView.preferredHeight: 230
                    SplitView.minimumHeight: 120
                    controller: app
                    clip: true
                    Component.onCompleted: {
                        // Instrumentspuren liegen unten: dorthin scrollen.
                        scrollY = contentHeight
                    }
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
                        focus: true
                        Keys.onDeletePressed: roll.deleteSelectedNote()
                    }

                    Rectangle {
                        Layout.preferredWidth: 250
                        Layout.fillHeight: true
                        color: "#1b1e24"
                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 12
                            spacing: 8
                            Label { text: qsTr("MIDI-Clip"); font.bold: true }
                            Label {
                                Layout.fillWidth: true
                                visible: !root.midi
                                wrapMode: Text.WordWrap
                                color: "#8a91a0"
                                text: qsTr("Oben einen orangen MIDI-Clip anklicken oder mit „+ MIDI-Clip“ einen anlegen.")
                            }
                            ColumnLayout {
                                visible: root.midi
                                Layout.fillWidth: true
                                spacing: 8
                                Label { text: qsTr("Instrument"); color: "#8a91a0" }
                                ComboBox {
                                    Layout.fillWidth: true
                                    model: app.instrumentNames
                                    currentIndex: root.midi ? root.clip.instrument : 0
                                    onActivated: function (i) { app.setTrackProperty(root.clip.trackIndex, "instrument", i) }
                                }
                                Label { text: qsTr("Raster"); color: "#8a91a0" }
                                ComboBox {
                                    id: gridBox
                                    Layout.fillWidth: true
                                    model: [qsTr("Viertel"), qsTr("Achtel"), qsTr("Sechzehntel"), qsTr("Zweiunddreißigstel")]
                                    currentIndex: 2
                                }
                                Label {
                                    text: roll.selectedNote >= 0 ? qsTr("Anschlag der Note") : qsTr("Keine Note ausgewählt")
                                    color: "#8a91a0"
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
                                Button { text: qsTr("Ansicht einpassen"); onClicked: roll.zoomToFit() }
                            }
                            Item { Layout.fillHeight: true }
                            Label {
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                color: "#5d6370"
                                font.pixelSize: 11
                                text: qsTr("Klicken: Note setzen (ziehen = Länge)\nNote ziehen: verschieben\nRechter Rand: Länge ändern\nRechtsklick / Doppelklick: löschen\nMausrad: hoch/runter · Strg+Rad: Zoom")
                            }
                        }
                    }
                }
            }

            // ------------------------------------------------- Mixer
            Rectangle {
                color: "#16181d"
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
                        Rectangle { Layout.fillHeight: true; width: 1; color: "#2d313b"; Layout.leftMargin: 4; Layout.rightMargin: 4 }
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
