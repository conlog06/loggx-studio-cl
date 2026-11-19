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

            // ------------------------------------------ Tonart & Tempo
            Section {
                Layout.fillWidth: true
                title: qsTr("Tonart & BPM")
                visible: root.hasClip && root.clip.isMedia && root.clip.hasAudio
                Button {
                    visible: !root.clip.analyzed
                    Layout.fillWidth: true
                    icon.name: "sparkles"
                    text: app.working ? app.workLabel : qsTr("Tempo und Tonart erkennen")
                    enabled: !app.working
                    onClicked: app.analyzeSelected()
                }
                Label {
                    visible: root.hasClip && root.clip.analyzed === true
                    Layout.fillWidth: true
                    color: Theme.dim
                    wrapMode: Text.WordWrap
                    text: root.clip.analyzed ? qsTr("Original: %1 · %2 BPM").arg(root.clip.origKeyName || "?").arg((root.clip.origBpm || 0).toFixed(1)) : ""
                }
                GridLayout {
                    visible: root.hasClip && root.clip.analyzed === true
                    Layout.fillWidth: true
                    columns: 2
                    columnSpacing: 10
                    rowSpacing: 6
                    Label { text: qsTr("Tonart") }
                    ComboBox {
                        Layout.fillWidth: true
                        enabled: root.clip.origKey >= 0
                        model: root.clip.analyzed ? app.keyNames(root.clip.origMinor) : []
                        currentIndex: root.clip.analyzed ? root.clip.keyNow : -1
                        displayText: root.clip.origKey >= 0 ? currentText : qsTr("nicht erkannt")
                        onActivated: function (i) { app.setClipKey(i) }
                    }
                    Label { text: qsTr("BPM") }
                    RowLayout {
                        spacing: 6
                        SpinBox {
                            Layout.fillWidth: true
                            enabled: (root.clip.origBpm || 0) > 0
                            from: 20; to: 300; editable: true
                            value: Math.round(root.clip.bpmNow || 0)
                            onValueModified: {
                                app.setClipBpm(value)
                                value = Qt.binding(function () { return Math.round(root.clip.bpmNow || 0) })
                            }
                        }
                        IconButton {
                            iconName: "metronome"
                            enabled: (root.clip.origBpm || 0) > 0
                            tip: qsTr("An das Projekt-Tempo anpassen (%1 BPM)").arg(Math.round(app.bpm))
                            onClicked: app.matchClipTempo()
                        }
                    }
                }
                RowLayout {
                    visible: root.hasClip && root.clip.analyzed === true
                    Button { text: qsTr("Original"); flat: true; onClicked: { app.setClipProperty("pitch", 0); app.setClipProperty("speed", 1) } }
                    Button { text: qsTr("Neu erkennen"); flat: true; enabled: !app.working; onClicked: app.analyzeSelected() }
                }
            }

            // --------------------------------------------------- Stimme
            Section {
                Layout.fillWidth: true
                title: qsTr("Stimme (Vocal Remover)")
                visible: root.hasClip && root.clip.isMedia && root.clip.hasAudio
                ComboBox {
                    Layout.fillWidth: true
                    model: app.aiAvailable ? [qsTr("KI – beste Qualität"), qsTr("KI – schnell"), qsTr("Ohne KI – sofort")]
                                           : [qsTr("Ohne KI – sofort")]
                    currentIndex: app.aiAvailable ? app.separationMethod : 0
                    onActivated: function (i) { if (app.aiAvailable) app.separationMethod = i }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    Button {
                        Layout.fillWidth: true
                        text: qsTr("Stimme entfernen")
                        icon.name: "mic"
                        enabled: !app.working
                        onClicked: app.separateSelected(false, -1)
                        ToolTip.text: qsTr("Karaoke: Instrumental hörbar, Gesang auf einer stummen Spur darunter")
                        ToolTip.visible: hovered
                        ToolTip.delay: 400
                    }
                    Button {
                        Layout.fillWidth: true
                        text: qsTr("Stimme isolieren")
                        icon.name: "wave"
                        enabled: !app.working
                        onClicked: app.separateSelected(true, -1)
                        ToolTip.text: qsTr("A cappella: nur Gesang hörbar, Instrumental stumm darunter")
                        ToolTip.visible: hovered
                        ToolTip.delay: 400
                    }
                }
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: Theme.faint
                    font.pixelSize: Theme.smallFont
                    text: app.aiAvailable && app.separationMethod < 2
                          ? qsTr("Ein neuronales Netz (MDX-Net) trennt Gesang und Instrumental – auch bei Mono. Beim ersten Mal wird das Modell geladen (%1 MB). Ergebnis: zwei WAV-Dateien auf neuen Spuren, das Original wird stumm.").arg(app.separationMethod === 0 ? 67 : 30)
                          : qsTr("Trennt ohne KI, was mittig im Stereobild liegt und tonal klingt. Mono-Aufnahmen gehen so nicht. Ergebnis: zwei WAV-Dateien auf neuen Spuren, das Original wird stumm.")
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
                ValueSlider {
                    Layout.fillWidth: true
                    label: qsTr("Schärfen"); prop: "sharpen"
                    value: root.hasClip ? root.clip.sharpen : 0
                    resetValue: 0; displayFactor: 100; decimals: 0; suffix: " %"
                }
                ValueSlider {
                    Layout.fillWidth: true
                    label: qsTr("Leuchten"); prop: "glow"
                    value: root.hasClip ? root.clip.glow : 0
                    resetValue: 0; displayFactor: 100; decimals: 0; suffix: " %"
                }
                ValueSlider {
                    Layout.fillWidth: true
                    label: qsTr("Filmkorn"); prop: "grain"
                    value: root.hasClip ? root.clip.grain : 0
                    resetValue: 0; displayFactor: 100; decimals: 0; suffix: " %"
                }
                ValueSlider {
                    Layout.fillWidth: true
                    label: qsTr("Sepia"); prop: "sepia"
                    value: root.hasClip ? root.clip.sepia : 0
                    resetValue: 0; displayFactor: 100; decimals: 0; suffix: " %"
                }
                ValueSlider {
                    Layout.fillWidth: true
                    label: qsTr("Farbversatz"); prop: "rgbSplit"
                    value: root.hasClip ? root.clip.rgbSplit : 0
                    resetValue: 0; displayFactor: 100; decimals: 0; suffix: " %"
                }
                ValueSlider {
                    Layout.fillWidth: true
                    label: qsTr("Verpixeln"); prop: "pixelate"
                    value: root.hasClip ? root.clip.pixelate : 0
                    resetValue: 0; displayFactor: 100; decimals: 0; suffix: " %"
                }
                Flow {
                    Layout.fillWidth: true
                    spacing: 4
                    CheckBox { text: qsTr("Spiegeln ↔"); checked: root.hasClip && root.clip.mirrorH; onToggled: app.setClipProperty("mirrorH", checked ? 1 : 0) }
                    CheckBox { text: qsTr("Spiegeln ↕"); checked: root.hasClip && root.clip.mirrorV; onToggled: app.setClipProperty("mirrorV", checked ? 1 : 0) }
                    CheckBox { text: qsTr("Negativ"); checked: root.hasClip && root.clip.invert; onToggled: app.setClipProperty("invert", checked ? 1 : 0) }
                }
                Button {
                    id: lookButton
                    text: qsTr("Look anwenden …")
                    icon.name: "sparkles"
                    onClicked: lookMenu.open()
                    // Fertige Kombinationen als ein Undo-Schritt
                    function look(values) {
                        app.beginInteractive()
                        for (const k in values) app.setClipProperty(k, values[k])
                        app.endInteractive()
                    }
                    Menu {
                        id: lookMenu
                        y: parent.height
                        MenuItem { text: qsTr("Retro-Film"); onTriggered: lookButton.look({ sepia: 0.55, grain: 0.35, vignette: 0.45, sharpen: 0, glow: 0, rgbSplit: 0, pixelate: 0, invert: 0 }) }
                        MenuItem { text: qsTr("Traum"); onTriggered: lookButton.look({ glow: 0.7, sepia: 0, grain: 0, vignette: 0.25, sharpen: 0, rgbSplit: 0, pixelate: 0, invert: 0 }) }
                        MenuItem { text: qsTr("Glitch"); onTriggered: lookButton.look({ rgbSplit: 0.6, grain: 0.25, sharpen: 0.3, sepia: 0, glow: 0, vignette: 0, pixelate: 0, invert: 0 }) }
                        MenuItem { text: qsTr("Knackig"); onTriggered: lookButton.look({ sharpen: 0.6, vignette: 0.2, saturation: 1.2, grain: 0, glow: 0, sepia: 0, rgbSplit: 0, pixelate: 0, invert: 0 }) }
                        MenuItem { text: qsTr("Schwarzweiß"); onTriggered: lookButton.look({ saturation: 0, grain: 0.2, vignette: 0.3, sepia: 0, glow: 0, rgbSplit: 0, pixelate: 0, invert: 0 }) }
                        MenuItem { text: qsTr("Alle Effekte aus"); onTriggered: lookButton.look({ sharpen: 0, glow: 0, grain: 0, sepia: 0, rgbSplit: 0, pixelate: 0, vignette: 0, invert: 0, mirrorH: 0, mirrorV: 0, saturation: 1 }) }
                    }
                }
                CheckBox {
                    text: qsTr("KI-Freistellung (Hintergrund entfernen)")
                    checked: root.hasClip && root.clip.aiCutout === true
                    onToggled: app.setAiCutout(checked)
                    ToolTip.text: qsTr("Erkennt Person/Objekt und macht den Hintergrund durchsichtig – ohne Greenscreen. Darunterliegende Videospur scheint durch.")
                    ToolTip.visible: hovered
                    ToolTip.delay: 500
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    visible: root.hasClip && root.clip.aiCutout === true
                    spacing: 8
                    RowLayout {
                        Layout.fillWidth: true
                        Label { text: qsTr("Modell"); Layout.preferredWidth: 84; color: Theme.dim }
                        ComboBox {
                            Layout.fillWidth: true
                            model: [qsTr("Schnell (alle Motive)"), qsTr("Personen – beste Qualität (176 MB)")]
                            currentIndex: app.cutoutQuality()
                            onActivated: app.setCutoutQuality(currentIndex)
                        }
                    }
                    ValueSlider {
                        Layout.fillWidth: true
                        label: qsTr("Kante"); prop: "cutoutEdge"
                        value: root.hasClip ? (root.clip.cutoutEdge || 0) : 0.4
                        resetValue: 0.4; displayFactor: 100; decimals: 0; suffix: " %"
                    }
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
                    RowLayout {
                        Layout.fillWidth: true
                        Button {
                            text: qsTr("Maske verfolgen (Tracking)")
                            icon.name: "sparkles"
                            enabled: !app.working
                            onClicked: app.trackSelectedMask()
                            ToolTip.text: qsTr("Folgt dem Inhalt unter der Maske ab dem Abspielkopf bis zum Clip-Ende und setzt Keyframes für die Position")
                            ToolTip.visible: hovered
                            ToolTip.delay: 500
                        }
                    }
                    ValueSlider {
                        Layout.fillWidth: true
                        keyable: true
                        label: qsTr("Position X"); prop: "maskX"
                        value: root.hasClip ? root.clip.maskX : 0.5
                        resetValue: 0.5; displayFactor: 100; decimals: 0; suffix: " %"
                    }
                    ValueSlider {
                        Layout.fillWidth: true
                        keyable: true
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
