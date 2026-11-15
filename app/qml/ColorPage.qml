import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import LoggX 1.0
import LoggXStyle

// Arbeitsbereich "Farbe": Vorschau, Scopes und Farbkorrektur des Clips.
Item {
    id: root
    readonly property var clip: app.clip
    readonly property bool active: clip.id !== undefined && clip.isVideo

    onVisibleChanged: if (visible && !active) app.selectClipAtPlayhead(true)

    FileDialog {
        id: lutDialog
        title: qsTr("LUT laden")
        nameFilters: [qsTr("3D-LUT (*.cube)"), qsTr("Alle Dateien (*)")]
        onAccepted: app.setLut(selectedFile)
    }

    SplitView {
        anchors.fill: parent
        orientation: Qt.Horizontal
        handle: Rectangle { implicitWidth: 5; color: SplitHandle.hovered || SplitHandle.pressed ? Theme.accent : Theme.bg }

        // Vorschau
        Rectangle {
            SplitView.fillWidth: true
            color: "#0a0b0e"
            ColumnLayout {
                anchors.fill: parent
                spacing: 0
                Viewer {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.margins: 10
                    controller: app
                    FileDropArea { anchors.fill: parent }
                }
                PreviewBar {
                    Layout.fillWidth: true
                    clipNavigation: true
                    rightText: root.active ? root.clip.name : qsTr("Kein Videoclip ausgewählt")
                }
                Slider {
                    Layout.fillWidth: true
                    Layout.leftMargin: 12
                    Layout.rightMargin: 12
                    from: 0
                    to: Math.max(0.01, app.duration)
                    value: app.position
                    onMoved: app.seek(value)
                }
            }
        }

        // Scopes und Regler
        Rectangle {
            SplitView.preferredWidth: 380
            SplitView.minimumWidth: 320
            color: Theme.panel

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 8

                RowLayout {
                    Layout.fillWidth: true
                    Label { text: qsTr("Scopes"); font.bold: true; Layout.fillWidth: true }
                    ComboBox {
                        id: scopeMode
                        model: [qsTr("Waveform"), qsTr("RGB-Parade"), qsTr("Histogramm")]
                    }
                }
                Scope {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 190
                    controller: app
                    mode: scopeMode.currentIndex
                }

                Label {
                    Layout.fillWidth: true
                    Layout.topMargin: 20
                    visible: !root.active
                    wrapMode: Text.WordWrap
                    horizontalAlignment: Text.AlignHCenter
                    color: Theme.dim
                    text: qsTr("Abspielkopf auf einen Videoclip setzen oder mit „Clip ▶“ einen auswählen.")
                }

                ScrollView {
                    id: scroll
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: root.active
                    contentWidth: availableWidth
                    clip: true
                    ColumnLayout {
                        width: scroll.availableWidth
                        spacing: 4
                        Section {
                            Layout.fillWidth: true
                            title: qsTr("Grundkorrektur")
                            ValueSlider {
                                Layout.fillWidth: true
                                label: qsTr("Belichtung"); prop: "exposure"; from: -3; to: 3; step: 0.05
                                value: root.active ? root.clip.exposure : 0; resetValue: 0; suffix: " EV"
                            }
                            ValueSlider {
                                Layout.fillWidth: true
                                label: qsTr("Kontrast"); prop: "contrast"; from: 0; to: 2
                                value: root.active ? root.clip.contrast : 1; resetValue: 1; displayFactor: 100; decimals: 0; suffix: " %"
                            }
                            ValueSlider {
                                Layout.fillWidth: true
                                label: qsTr("Sättigung"); prop: "saturation"; from: 0; to: 2
                                value: root.active ? root.clip.saturation : 1; resetValue: 1; displayFactor: 100; decimals: 0; suffix: " %"
                            }
                            ValueSlider {
                                Layout.fillWidth: true
                                label: qsTr("Temperatur"); prop: "temperature"; from: -1; to: 1
                                value: root.active ? root.clip.temperature : 0; resetValue: 0; displayFactor: 100; decimals: 0
                            }
                            ValueSlider {
                                Layout.fillWidth: true
                                label: qsTr("Tönung"); prop: "tint"; from: -1; to: 1
                                value: root.active ? root.clip.tint : 0; resetValue: 0; displayFactor: 100; decimals: 0
                            }
                        }
                        Section {
                            Layout.fillWidth: true
                            title: qsTr("Schatten · Mitten · Lichter")
                            ValueSlider {
                                Layout.fillWidth: true
                                label: qsTr("Schatten"); prop: "lift"; from: -0.5; to: 0.5; step: 0.005
                                value: root.active ? root.clip.lift : 0; resetValue: 0; displayFactor: 100; decimals: 0
                            }
                            ValueSlider {
                                Layout.fillWidth: true
                                label: qsTr("Mitten"); prop: "gamma"; from: 0.3; to: 3; step: 0.01
                                value: root.active ? root.clip.gamma : 1; resetValue: 1
                            }
                            ValueSlider {
                                Layout.fillWidth: true
                                label: qsTr("Lichter"); prop: "colorGain"; from: 0; to: 2
                                value: root.active ? root.clip.colorGain : 1; resetValue: 1; displayFactor: 100; decimals: 0; suffix: " %"
                            }
                        }
                        Section {
                            Layout.fillWidth: true
                            title: qsTr("LUT (Look)")
                            RowLayout {
                                Layout.fillWidth: true
                                Label {
                                    Layout.fillWidth: true
                                    elide: Text.ElideMiddle
                                    text: root.active && root.clip.lutName !== "" ? root.clip.lutName : qsTr("Keine LUT")
                                    color: root.active && root.clip.lutName !== "" ? Theme.text : Theme.dim
                                }
                                Button { text: qsTr("Laden …"); onClicked: lutDialog.open() }
                                Button {
                                    text: qsTr("Entfernen")
                                    visible: root.active && root.clip.lutName !== ""
                                    onClicked: app.setLut("")
                                }
                            }
                            ValueSlider {
                                Layout.fillWidth: true
                                visible: root.active && root.clip.lutName !== ""
                                label: qsTr("Stärke"); prop: "lutMix"
                                value: root.active ? root.clip.lutMix : 1; resetValue: 1; displayFactor: 100; decimals: 0; suffix: " %"
                            }
                        }
                        Button {
                            Layout.topMargin: 8
                            text: qsTr("Farbkorrektur zurücksetzen")
                            enabled: root.active && !root.clip.colorNeutral
                            onClicked: app.resetColor()
                        }
                        Label {
                            Layout.fillWidth: true
                            Layout.topMargin: 6
                            wrapMode: Text.WordWrap
                            color: Theme.faint
                            font.pixelSize: 11
                            text: qsTr("Doppelklick auf die Beschriftung eines Reglers setzt ihn zurück. Masken für lokale Korrekturen: im Arbeitsbereich „Schnitt“ eine zweite Kopie des Clips darüberlegen und maskieren.")
                        }
                    }
                }
            }
        }
    }
}
