import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import LoggXStyle

// Eigenschaften des ausgewählten Clips.
Rectangle {
    id: root
    color: Theme.panel
    readonly property var clip: app.clip
    readonly property bool hasClip: clip.id !== undefined
    readonly property bool isVideo: hasClip && clip.isVideo
    signal openPage(int page)

    ColorDialog {
        id: titleColorDialog
        title: qsTr("Textfarbe")
        selectedColor: root.hasClip ? root.clip.titleColor : "white"
        onAccepted: app.setTitleColor(selectedColor)
    }
    ColorDialog {
        id: keyColorDialog
        title: qsTr("Schlüsselfarbe")
        selectedColor: root.hasClip ? root.clip.keyColor : "lime"
        onAccepted: app.setKeyColor(selectedColor)
    }

    Label {
        anchors.centerIn: parent
        visible: !root.hasClip
        width: parent.width - 40
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        color: Theme.dim
        text: qsTr("Clip in der Timeline anklicken, um Position, Größe, Lautstärke, Blenden, Effekte und Maske einzustellen.\n\n◇ setzt Keyframes für Animationen.")
    }

    ScrollView {
        id: scroll
        anchors.fill: parent
        visible: root.hasClip
        contentWidth: availableWidth
        clip: true

        ColumnLayout {
            width: scroll.availableWidth - 24
            x: 12
            spacing: 4

            Item { Layout.preferredHeight: 6 }
            Label {
                Layout.fillWidth: true
                text: root.hasClip ? root.clip.name : ""
                font.bold: true
                elide: Text.ElideMiddle
            }
            Label {
                color: Theme.dim
                text: root.hasClip
                      ? qsTr("Start %1 · Länge %2").arg(app.timecode(root.clip.start)).arg(app.timecode(root.clip.duration))
                      : ""
            }
            RowLayout {
                visible: root.hasClip && root.clip.hasKeys
                spacing: 4
                Label { text: qsTr("Keyframes:"); color: Theme.warn }
                Button { text: "◀"; onClicked: app.jumpToKeyframe(-1); implicitWidth: 36 }
                Button { text: "▶"; onClicked: app.jumpToKeyframe(1); implicitWidth: 36 }
            }

            // ---------------------------------------------------- Titel
            Section {
                Layout.fillWidth: true
                title: qsTr("Titel")
                visible: root.hasClip && root.clip.isTitle
                TextArea {
                    id: titleText
                    Layout.fillWidth: true
                    Layout.preferredHeight: 64
                    wrapMode: TextEdit.Wrap
                    text: root.hasClip && root.clip.isTitle ? root.clip.text : ""
                    placeholderText: qsTr("Text (Enter für neue Zeile)")
                    onActiveFocusChanged: if (!activeFocus) app.setTitleText(text)
                    Keys.onEscapePressed: focus = false
                }
                Button {
                    text: qsTr("Text übernehmen")
                    onClicked: app.setTitleText(titleText.text)
                }
                ValueSlider {
                    Layout.fillWidth: true
                    label: qsTr("Schriftgröße"); prop: "titleSize"; from: 0.02; to: 0.3
                    value: root.hasClip ? root.clip.titleSize : 0.09
                    resetValue: 0.09; displayFactor: 100; decimals: 0; suffix: " %"
                }
                ValueSlider {
                    Layout.fillWidth: true
                    label: qsTr("Höhe"); prop: "titlePosY"
                    value: root.hasClip ? root.clip.titlePosY : 0.5
                    resetValue: 0.5; displayFactor: 100; decimals: 0; suffix: " %"
                }
                ValueSlider {
                    Layout.fillWidth: true
                    label: qsTr("Balken"); prop: "titleBox"
                    value: root.hasClip ? root.clip.titleBox : 0
                    resetValue: 0; displayFactor: 100; decimals: 0; suffix: " %"
                }
                RowLayout {
                    spacing: 6
                    ComboBox {
                        model: [qsTr("Links"), qsTr("Mitte"), qsTr("Rechts")]
                        currentIndex: root.hasClip ? root.clip.titleAlign : 1
                        onActivated: function (i) { app.setClipProperty("titleAlign", i) }
                    }
                    Button {
                        text: qsTr("Farbe")
                        onClicked: titleColorDialog.open()
                        contentItem: RowLayout {
                            Rectangle { width: 14; height: 14; radius: 3; color: root.hasClip ? root.clip.titleColor : "white"; border.color: "#555" }
                            Label { text: qsTr("Farbe") }
                        }
                    }
                }
                RowLayout {
                    CheckBox {
                        text: qsTr("Fett")
                        checked: root.hasClip && root.clip.titleBold
                        onToggled: app.setClipProperty("titleBold", checked ? 1 : 0)
                    }
                    CheckBox {
                        text: qsTr("Schatten")
                        checked: root.hasClip && root.clip.titleShadow
                        onToggled: app.setClipProperty("titleShadow", checked ? 1 : 0)
                    }
                }
            }

            // -------------------------------------------------- MIDI
            Section {
                Layout.fillWidth: true
                title: qsTr("Instrument")
                visible: root.hasClip && root.clip.isMidi
                ComboBox {
                    Layout.fillWidth: true
                    model: app.instrumentNames
                    currentIndex: root.hasClip && root.clip.isMidi ? root.clip.instrument : 0
                    onActivated: function (i) { app.setTrackProperty(root.clip.trackIndex, "instrument", i) }
                }
                Button {
                    text: qsTr("Noten bearbeiten …")
                    onClicked: root.openPage(3)
                }
            }

            // -------------------------------------------- Transformieren
            Section {
                Layout.fillWidth: true
                title: qsTr("Transformieren")
                visible: root.isVideo
                ValueSlider {
                    Layout.fillWidth: true
                    label: qsTr("Position X"); prop: "x"; from: -1; to: 1; keyable: true
                    value: root.hasClip ? root.clip.x : 0
                    resetValue: 0; displayFactor: 100; decimals: 0; suffix: " %"
                }
                ValueSlider {
                    Layout.fillWidth: true
                    label: qsTr("Position Y"); prop: "y"; from: -1; to: 1; keyable: true
                    value: root.hasClip ? root.clip.y : 0
                    resetValue: 0; displayFactor: 100; decimals: 0; suffix: " %"
                }
                ValueSlider {
                    Layout.fillWidth: true
                    label: qsTr("Größe"); prop: "scale"; from: 0.05; to: 3; keyable: true
                    value: root.hasClip ? root.clip.scale : 1
                    resetValue: 1; displayFactor: 100; decimals: 0; suffix: " %"
                }
                ValueSlider {
                    Layout.fillWidth: true
                    label: qsTr("Drehung"); prop: "rotation"; from: -180; to: 180; step: 0.5; keyable: true
                    value: root.hasClip ? root.clip.rotation : 0
                    resetValue: 0; decimals: 1; suffix: "°"
                }
                ValueSlider {
                    Layout.fillWidth: true
                    label: qsTr("Deckkraft"); prop: "opacity"; keyable: true
                    value: root.hasClip ? root.clip.opacity : 1
                    resetValue: 1; displayFactor: 100; decimals: 0; suffix: " %"
                }
            }

            // ------------------------------------------------- Übergang
            Section {
                Layout.fillWidth: true
                title: qsTr("Übergang vom vorherigen Clip")
                visible: root.hasClip && root.clip.hasPrevious === true
                ComboBox {
                    Layout.fillWidth: true
                    model: app.transitionNames
                    currentIndex: root.hasClip && root.clip.transition !== undefined ? root.clip.transition : 0
                    onActivated: function (i) { app.setClipProperty("transition", i) }
                }
                ValueSlider {
                    Layout.fillWidth: true
                    visible: root.hasClip && root.clip.transition > 0
                    label: qsTr("Dauer"); prop: "transitionDuration"; from: 0.1; to: 3; step: 0.05
                    value: root.hasClip && root.clip.transitionDuration !== undefined ? root.clip.transitionDuration : 0.5
                    resetValue: 0.5; decimals: 2; suffix: " s"
                }
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: Theme.faint
                    font.pixelSize: Theme.smallFont
                    text: qsTr("Bild und Ton der beiden Clips werden am Schnitt ineinander übergeblendet.")
                }
            }

            // -------------------------------------------------- Blenden
            Section {
                Layout.fillWidth: true
                title: qsTr("Ein- und Ausblenden")
                visible: root.hasClip
                ValueSlider {
                    Layout.fillWidth: true
                    label: qsTr("Einblenden"); prop: "fadeIn"
                    to: root.hasClip ? Math.min(5, root.clip.duration / 2) : 1; step: 0.05
                    value: root.hasClip ? root.clip.fadeIn : 0
                    resetValue: 0; decimals: 2; suffix: " s"
                }
                ValueSlider {
                    Layout.fillWidth: true
                    label: qsTr("Ausblenden"); prop: "fadeOut"
                    to: root.hasClip ? Math.min(5, root.clip.duration / 2) : 1; step: 0.05
                    value: root.hasClip ? root.clip.fadeOut : 0
                    resetValue: 0; decimals: 2; suffix: " s"
                }
            }

            // ----------------------------------------------------- Ton
            Section {
                Layout.fillWidth: true
                title: qsTr("Ton")
                visible: root.hasClip && root.clip.hasAudio
                ValueSlider {
                    Layout.fillWidth: true
                    label: qsTr("Lautstärke"); prop: "gainDb"; from: -60; to: 12; step: 0.5; keyable: true
                    value: root.hasClip ? root.clip.gainDb : 0
                    resetValue: 0; decimals: 1; suffix: " dB"
                }
                ValueSlider {
                    Layout.fillWidth: true
                    enabled: app.stretchAvailable || (root.hasClip && root.clip.isMidi)
                    label: qsTr("Tonhöhe"); prop: "pitch"; from: -12; to: 12; step: 1
                    value: root.hasClip ? root.clip.pitch : 0
                    resetValue: 0; decimals: 0; suffix: " HT"
                }
                Label {
                    Layout.fillWidth: true
                    visible: root.hasClip && root.clip.isMedia && !app.stretchAvailable
                    wrapMode: Text.WordWrap
                    color: "#d9a441"
                    font.pixelSize: 11
                    text: qsTr("Diese Version wurde ohne Rubber Band gebaut: Tonhöhe und Tempo wirken nicht auf den Ton.")
                }
            }

            Section {
                Layout.fillWidth: true
                title: qsTr("Tempo")
                visible: root.hasClip && root.clip.isMedia
                ValueSlider {
                    Layout.fillWidth: true
                    label: qsTr("Tempo"); prop: "speed"; from: 0.25; to: 4; step: 0.05
                    value: root.hasClip ? root.clip.speed : 1
                    resetValue: 1; suffix: "×"
                }
            }

            // ------------------------------------------------- Effekte
            Section {
                Layout.fillWidth: true
                title: qsTr("Effekte")
                visible: root.isVideo
                ValueSlider {
                    Layout.fillWidth: true
                    label: qsTr("Weichzeichnen"); prop: "blur"; from: 0; to: 0.05; step: 0.0005; keyable: true
                    value: root.hasClip ? root.clip.blur : 0
                    resetValue: 0; displayFactor: 2000; decimals: 0; suffix: " %"
                }
                ValueSlider {
                    Layout.fillWidth: true
                    label: qsTr("Vignette"); prop: "vignette"
                    value: root.hasClip ? root.clip.vignette : 0
                    resetValue: 0; displayFactor: 100; decimals: 0; suffix: " %"
                }
                CheckBox {
                    text: qsTr("Greenscreen (Chroma Key)")
                    checked: root.hasClip && root.clip.chromaKey
                    onToggled: app.setClipProperty("chromaKey", checked ? 1 : 0)
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    visible: root.hasClip && root.clip.chromaKey
                    spacing: 8
                    RowLayout {
                        spacing: 6
                        Label { text: qsTr("Farbe"); Layout.preferredWidth: 84; color: Theme.dim }
                        Rectangle { width: 18; height: 18; radius: 3; color: root.hasClip ? root.clip.keyColor : "lime"; border.color: "#555" }
                        Button { text: qsTr("Grün"); onClicked: app.setKeyColor(Qt.rgba(0, 1, 0, 1)) }
                        Button { text: qsTr("Blau"); onClicked: app.setKeyColor(Qt.rgba(0, 0.3, 1, 1)) }
                        Button { text: "…"; onClicked: keyColorDialog.open(); implicitWidth: 32 }
                    }
                    ValueSlider {
                        Layout.fillWidth: true
                        label: qsTr("Toleranz"); prop: "keyTolerance"
                        value: root.hasClip ? root.clip.keyTolerance : 0.3
                        resetValue: 0.3; displayFactor: 100; decimals: 0; suffix: " %"
                    }
                    ValueSlider {
                        Layout.fillWidth: true
                        label: qsTr("Weiche Kante"); prop: "keySoftness"
                        value: root.hasClip ? root.clip.keySoftness : 0.1
                        resetValue: 0.1; displayFactor: 100; decimals: 0; suffix: " %"
                    }
                }
                Button {
                    text: root.hasClip && !root.clip.colorNeutral ? qsTr("Farbkorrektur bearbeiten …") : qsTr("Farbkorrektur …")
                    onClicked: root.openPage(2)
                }
            }

            // --------------------------------------------------- Maske
            Section {
                Layout.fillWidth: true
                title: qsTr("Maske")
                visible: root.isVideo
                open: root.hasClip && root.clip.maskShape > 0
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    Label { text: qsTr("Form"); Layout.preferredWidth: 84; color: Theme.dim }
                    ComboBox {
                        Layout.fillWidth: true
                        model: [qsTr("Keine"), qsTr("Rechteck"), qsTr("Ellipse")]
                        currentIndex: root.hasClip ? root.clip.maskShape : 0
                        onActivated: function (index) { app.setClipProperty("maskShape", index) }
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    visible: root.hasClip && root.clip.maskShape > 0
                    spacing: 8
                    ValueSlider {
                        Layout.fillWidth: true
                        label: qsTr("Position X"); prop: "maskX"
                        value: root.hasClip ? root.clip.maskX : 0.5
                        resetValue: 0.5; displayFactor: 100; decimals: 0; suffix: " %"
                    }
                    ValueSlider {
                        Layout.fillWidth: true
                        label: qsTr("Position Y"); prop: "maskY"
                        value: root.hasClip ? root.clip.maskY : 0.5
                        resetValue: 0.5; displayFactor: 100; decimals: 0; suffix: " %"
                    }
                    ValueSlider {
                        Layout.fillWidth: true
                        label: qsTr("Breite"); prop: "maskW"; from: 0.02; to: 1.5
                        value: root.hasClip ? root.clip.maskW : 0.5
                        resetValue: 0.5; displayFactor: 100; decimals: 0; suffix: " %"
                    }
                    ValueSlider {
                        Layout.fillWidth: true
                        label: qsTr("Höhe"); prop: "maskH"; from: 0.02; to: 1.5
                        value: root.hasClip ? root.clip.maskH : 0.5
                        resetValue: 0.5; displayFactor: 100; decimals: 0; suffix: " %"
                    }
                    ValueSlider {
                        Layout.fillWidth: true
                        label: qsTr("Weiche Kante"); prop: "maskFeather"; from: 0; to: 0.5
                        value: root.hasClip ? root.clip.maskFeather : 0
                        resetValue: 0; displayFactor: 100; decimals: 0; suffix: " %"
                    }
                    CheckBox {
                        text: qsTr("Maske umkehren")
                        checked: root.hasClip && root.clip.maskInvert
                        onToggled: app.setClipProperty("maskInvert", checked ? 1 : 0)
                    }
                }
            }
            Item { Layout.preferredHeight: 14 }
        }
    }
}
