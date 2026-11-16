import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LoggX 1.0
import LoggXStyle

// Ein DJ-Deck: Titel, Tempo, Übersicht, Cue/Play, Jog, Sync, Hot Cues,
// Loops und Stems.
Rectangle {
    id: deckRoot
    property int deck: 0
    property var info: deck === 0 ? dj.deckA : dj.deckB
    property color accent: deck === 0 ? "#3d8bff" : "#ff8a3d"
    readonly property bool loaded: info.loaded === true
    readonly property var hotColors: ["#ff3b5c", "#ff8c1a", "#ffd41a", "#3cd96b", "#2fc6ff", "#5b6cff", "#b05bff", "#ff5bd6"]
    color: Theme.panel
    radius: 10
    border.color: info.master ? accent : Theme.border

    signal askBpm(string path, int deck, real current)

    // Rechtsklick ins Deck
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.RightButton
        onClicked: if (deckRoot.loaded) deckMenu.popup()
    }
    Menu {
        id: deckMenu
        MenuItem { text: qsTr("Tempo eingeben …"); onTriggered: deckRoot.askBpm(info.path, deckRoot.deck, info.bpm || 0) }
        MenuItem { text: qsTr("Tempo verdoppeln (×2)"); onTriggered: dj.multiplyBpm(deckRoot.deck, 2) }
        MenuItem { text: qsTr("Tempo halbieren (/2)"); onTriggered: dj.multiplyBpm(deckRoot.deck, 0.5) }
        MenuItem { text: qsTr("Raster: Schlag 1 hier"); onTriggered: dj.setGridHere(deckRoot.deck) }
        MenuItem { text: qsTr("Neu analysieren"); onTriggered: dj.reanalyze(info.path) }
        MenuSeparator {}
        MenuItem { text: qsTr("Im Musik-Editor öffnen"); icon.name: "piano"; onTriggered: dj.openInMusicEditor(info.path) }
        MenuItem { text: qsTr("Stems mit KI vorbereiten"); icon.name: "sparkles"; enabled: dj.aiAvailable && !info.hasStems; onTriggered: dj.prepareStems(info.path) }
        MenuSeparator {}
        MenuItem { text: qsTr("Auswerfen"); enabled: !info.playing; onTriggered: dj.eject(deckRoot.deck) }
    }

    // Track aus der Bibliothek hierher ziehen
    DropArea {
        anchors.fill: parent
        keys: ["loggx/djtrack"]
        onDropped: function (drop) { dj.loadTrack(deckRoot.deck, drop.source.trackPath) }
        Rectangle { anchors.fill: parent; radius: 10; color: Theme.alpha(deckRoot.accent, 0.15); visible: parent.containsDrag }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 8

        // ------------------------------------------------ Kopf
        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            Rectangle {
                width: 30; height: 30; radius: 8
                color: deckRoot.accent
                Label { anchors.centerIn: parent; text: deckRoot.deck + 1; font.bold: true; font.pixelSize: 16; color: "white" }
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0
                Label {
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    font.pixelSize: 15
                    font.bold: true
                    text: info.loading ? qsTr("Lädt …") : deckRoot.loaded ? info.title : qsTr("Leer – Track hierher ziehen")
                    color: deckRoot.loaded ? Theme.text : Theme.faint
                }
                Label {
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    text: deckRoot.loaded ? (info.artist || qsTr("Unbekannter Interpret")) : ""
                    color: Theme.dim
                }
            }
            Rectangle {
                visible: deckRoot.loaded && info.key !== ""
                width: keyText.implicitWidth + 14; height: 24; radius: 6
                color: Theme.panel2
                border.color: Theme.border
                Label {
                    id: keyText
                    anchors.centerIn: parent
                    // Nach Key Shift / Tempo die klingende Tonart (wie „Current Key“)
                    text: (info.keyNow && info.keyNow !== info.key) ? info.key + " → " + info.keyNow : (info.key || "")
                    font.bold: true
                    color: info.keyShift ? Theme.warn : Theme.ok
                }
                ToolTip.text: info.keyName || ""
                ToolTip.visible: keyMouse.containsMouse
                MouseArea { id: keyMouse; anchors.fill: parent; hoverEnabled: true }
            }
            ColumnLayout {
                spacing: 0
                Label {
                    Layout.alignment: Qt.AlignRight
                    text: deckRoot.loaded && info.bpmNow > 0 ? info.bpmNow.toFixed(2) : "--.--"
                    font.pixelSize: 22
                    font.bold: true
                    font.family: "monospace"
                }
                Label {
                    Layout.alignment: Qt.AlignRight
                    text: (info.tempo >= 0 ? "+" : "") + (info.tempo || 0).toFixed(1) + " %  ·  ±" + (info.range || 8)
                    color: Math.abs(info.tempo || 0) < 0.05 ? Theme.dim : deckRoot.accent
                    font.pixelSize: 11
                }
            }
        }

        // ------------------------------------------ Übersicht
        DjWave {
            Layout.fillWidth: true
            Layout.preferredHeight: 44
            controller: dj
            deck: deckRoot.deck
            overview: true
            accent: deckRoot.accent
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 4
            Label { text: info.elapsedText || "00:00.0"; font.family: "monospace"; color: Theme.dim }
            Item { Layout.fillWidth: true }
            // Takt-Anzeige: vier Felder wie im CDJ-Display
            Row {
                visible: deckRoot.loaded && info.bar !== undefined
                spacing: 2
                Repeater {
                    model: 4
                    Rectangle {
                        required property int index
                        width: 12; height: 8; radius: 2
                        color: (info.beatInBar || 0) === index + 1 ? (index === 0 ? Theme.danger : deckRoot.accent) : Theme.panel2
                    }
                }
            }
            Label {
                visible: deckRoot.loaded && info.bar !== undefined
                text: (info.barsToCue !== undefined && info.barsToCue >= 0 ? qsTr("%1 Takte bis Cue").arg(info.barsToCue.toFixed(1)) : qsTr("Takt %1").arg(info.bar || 0))
                color: Theme.dim
                font.family: "monospace"
                font.pixelSize: 11
            }
            Item { Layout.fillWidth: true }
            Label { text: info.remainingText || "-00:00.0"; font.family: "monospace"; color: Theme.text }
        }
        // Tempo/Beatgrid von Hand: TAP, ×2, ÷2, Raster verschieben
        RowLayout {
            Layout.fillWidth: true
            spacing: 3
            visible: deckRoot.loaded
            Button {
                text: "TAP"
                implicitWidth: 46
                implicitHeight: 24
                font.pixelSize: 11
                onClicked: dj.tap(deckRoot.deck)
                ToolTip.text: qsTr("Im Takt tippen (mind. 4×) – setzt das Tempo")
                ToolTip.visible: hovered
                ToolTip.delay: 500
            }
            Button { text: "×2"; implicitWidth: 38; implicitHeight: 24; font.pixelSize: 11; onClicked: dj.multiplyBpm(deckRoot.deck, 2) }
            Button { text: "/2"; implicitWidth: 38; implicitHeight: 24; font.pixelSize: 11; onClicked: dj.multiplyBpm(deckRoot.deck, 0.5) }
            Button {
                text: (info.bpm || 0).toFixed(2)
                flat: true
                implicitHeight: 24
                font.pixelSize: 11
                font.family: "monospace"
                onClicked: deckRoot.askBpm(info.path, deckRoot.deck, info.bpm || 0)
                ToolTip.text: qsTr("Original-Tempo des Tracks – klicken zum Ändern")
                ToolTip.visible: hovered
                ToolTip.delay: 500
            }
            Item { Layout.fillWidth: true }
            Label { text: qsTr("Tonart"); color: Theme.faint; font.pixelSize: 11 }
            Button { text: "−"; implicitWidth: 26; implicitHeight: 24; onClicked: dj.setKeyShift(deckRoot.deck, (info.keyShift || 0) - 1) }
            Label {
                text: (info.keyShift > 0 ? "+" : "") + (info.keyShift || 0)
                Layout.preferredWidth: 22
                horizontalAlignment: Text.AlignHCenter
                font.family: "monospace"
                font.pixelSize: 11
                color: info.keyShift ? Theme.warn : Theme.dim
                MouseArea { anchors.fill: parent; onDoubleClicked: dj.setKeyShift(deckRoot.deck, 0) }
            }
            Button { text: "+"; implicitWidth: 26; implicitHeight: 24; onClicked: dj.setKeyShift(deckRoot.deck, (info.keyShift || 0) + 1) }
            Button {
                text: qsTr("KEY SYNC")
                implicitHeight: 24
                font.pixelSize: 10
                onClicked: dj.keySync(deckRoot.deck)
                ToolTip.text: qsTr("Tonart an das andere Deck anpassen (Tempo bleibt)")
                ToolTip.visible: hovered
                ToolTip.delay: 500
            }
            Item { Layout.preferredWidth: 4 }
            IconButton { iconName: "chevron-left"; tip: qsTr("Raster 10 ms früher"); implicitWidth: 26; implicitHeight: 24; onClicked: dj.shiftGrid(deckRoot.deck, -10) }
            IconButton { iconName: "chevron-right"; tip: qsTr("Raster 10 ms später"); implicitWidth: 26; implicitHeight: 24; onClicked: dj.shiftGrid(deckRoot.deck, 10) }
            Button {
                text: "1"
                implicitWidth: 30
                implicitHeight: 24
                font.pixelSize: 11
                onClicked: dj.setGridHere(deckRoot.deck)
                ToolTip.text: qsTr("Schlag 1 des Rasters an die aktuelle Stelle legen")
                ToolTip.visible: hovered
                ToolTip.delay: 500
            }
        }

        // ------------------------------------- Bedienung
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 12

            ColumnLayout {
                spacing: 10
                Layout.alignment: Qt.AlignBottom
                // CUE
                Rectangle {
                    width: 54; height: 54; radius: 27
                    color: cueMouse.pressed ? Theme.alpha("#ff9a2f", 0.35) : "transparent"
                    border.color: "#ff9a2f"
                    border.width: 2
                    opacity: deckRoot.loaded ? 1 : 0.4
                    Label { anchors.centerIn: parent; text: "CUE"; font.bold: true; color: "#ff9a2f" }
                    MouseArea { id: cueMouse; anchors.fill: parent; onClicked: dj.cue(deckRoot.deck) }
                }
                // PLAY / PAUSE
                Rectangle {
                    width: 54; height: 54; radius: 27
                    color: info.playing ? Theme.alpha(Theme.ok, 0.25) : playMouse.pressed ? Theme.alpha(Theme.ok, 0.35) : "transparent"
                    border.color: Theme.ok
                    border.width: 2
                    opacity: deckRoot.loaded ? 1 : 0.4
                    Icon { anchors.centerIn: parent; width: 26; height: 26; name: info.playing ? "pause" : "play"; color: Theme.ok }
                    MouseArea { id: playMouse; anchors.fill: parent; onClicked: dj.togglePlay(deckRoot.deck) }
                }
            }

            // Jog-Rad: dreht sich mit der Musik, Ziehen schiebt vor/zurück
            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumWidth: 120
                Rectangle {
                    id: jog
                    readonly property real size: Math.min(parent.width, parent.height)
                    width: size; height: size; radius: size / 2
                    anchors.centerIn: parent
                    color: "#121419"
                    border.color: info.playing ? deckRoot.accent : Theme.border
                    border.width: 3
                    Rectangle {
                        width: jog.size * 0.7; height: width; radius: width / 2
                        anchors.centerIn: parent
                        color: "#1a1d24"
                        border.color: "#2a2e38"
                    }
                    // Markierung: eine Umdrehung je 1,8 s (33⅓ U/min)
                    Item {
                        anchors.fill: parent
                        rotation: ((info.position || 0) / 1.8 * 360) % 360
                        Rectangle {
                            width: 4; height: jog.size * 0.14; radius: 2
                            color: deckRoot.accent
                            anchors.horizontalCenter: parent.horizontalCenter
                            y: 6
                        }
                    }
                    Column {
                        anchors.centerIn: parent
                        Label {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: info.elapsedText || "00:00.0"
                            font.family: "monospace"
                            font.pixelSize: Math.max(12, jog.size * 0.1)
                        }
                        Label {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: info.looping ? qsTr("LOOP %1").arg(info.loopBeats) : info.keyLock ? "MT" : ""
                            color: info.looping ? Theme.ok : Theme.dim
                            font.pixelSize: 11
                        }
                    }
                    MouseArea {
                        anchors.fill: parent
                        property real lastAngle
                        function angle(m) { return Math.atan2(m.y - height / 2, m.x - width / 2) }
                        // Wie am Jog-Rad oben: festhalten = Scratchen
                        onPressed: function (m) { lastAngle = angle(m); dj.setScratch(deckRoot.deck, true) }
                        onReleased: dj.setScratch(deckRoot.deck, false)
                        onCanceled: dj.setScratch(deckRoot.deck, false)
                        onPositionChanged: function (m) {
                            let a = angle(m), d = a - lastAngle
                            if (d > Math.PI) d -= 2 * Math.PI
                            if (d < -Math.PI) d += 2 * Math.PI
                            lastAngle = a
                            dj.jogScratch(deckRoot.deck, d / (2 * Math.PI) * 1.8)
                        }
                    }
                }
            }

            ColumnLayout {
                spacing: 6
                RowLayout {
                    spacing: 4
                    Button {
                        text: "SYNC"
                        checkable: true
                        checked: info.sync === true
                        highlighted: checked
                        implicitWidth: 64
                        onClicked: dj.setSyncLock(deckRoot.deck, checked)
                        ToolTip.text: qsTr("Tempo und Takt an das andere Deck angleichen und mitführen")
                        ToolTip.visible: hovered
                        ToolTip.delay: 500
                    }
                    Rectangle {
                        width: 64; height: 30; radius: Theme.radius
                        color: info.master ? Theme.alpha(deckRoot.accent, 0.3) : "transparent"
                        border.color: info.master ? deckRoot.accent : Theme.border
                        Label { anchors.centerIn: parent; text: "MASTER"; font.pixelSize: 11; font.bold: true; color: info.master ? Theme.text : Theme.faint }
                    }
                }
                RowLayout {
                    spacing: 4
                    Button {
                        text: "MT"
                        checkable: true
                        checked: info.keyLock === true
                        highlighted: checked
                        implicitWidth: 64
                        onClicked: dj.setKeyLock(deckRoot.deck, checked)
                        ToolTip.text: qsTr("Master Tempo: Tonhöhe bleibt beim Tempo-Ändern gleich")
                        ToolTip.visible: hovered
                        ToolTip.delay: 500
                    }
                    ComboBox {
                        implicitWidth: 64
                        readonly property var ranges: [6, 8, 10, 16, 25, 50]
                        model: ranges.map(function (r) { return "±" + r })
                        currentIndex: Math.max(0, ranges.indexOf(info.range || 8))
                        onActivated: function (i) { dj.setTempoRange(deckRoot.deck, ranges[i]) }
                    }
                }
                // Tempo-Fader wie am CDJ/DDJ: oben = langsamer, unten = schneller
                RowLayout {
                    Layout.fillHeight: true
                    spacing: 4
                    Slider {
                        id: tempoSlider
                        Layout.fillHeight: true
                        Layout.preferredHeight: 90
                        orientation: Qt.Vertical
                        from: info.range || 8
                        to: -(info.range || 8)
                        value: info.tempo || 0
                        onMoved: dj.setTempo(deckRoot.deck, value)
                    }
                    ColumnLayout {
                        Button { text: "+"; implicitWidth: 30; onClicked: dj.setTempo(deckRoot.deck, (info.tempo || 0) + 0.1) }
                        Button { text: "0"; implicitWidth: 30; onClicked: dj.setTempo(deckRoot.deck, 0) }
                        Button { text: "−"; implicitWidth: 30; onClicked: dj.setTempo(deckRoot.deck, (info.tempo || 0) - 0.1) }
                    }
                }
            }
        }

        // ------------------------------------------ Hot Cues
        GridLayout {
            Layout.fillWidth: true
            columns: 8
            columnSpacing: 4
            Repeater {
                model: 8
                Rectangle {
                    required property int index
                    readonly property bool set: deckRoot.loaded && info.hot && info.hot[index] >= 0
                    Layout.fillWidth: true
                    implicitHeight: 30
                    radius: 5
                    color: set ? Theme.alpha(deckRoot.hotColors[index], hotMouse.pressed ? 0.7 : 0.45) : hotMouse.containsMouse ? Theme.hover : Theme.panel2
                    border.color: set ? deckRoot.hotColors[index] : Theme.border
                    Label { anchors.centerIn: parent; text: String.fromCharCode(65 + index); font.bold: true; color: parent.set ? "white" : Theme.dim }
                    MouseArea {
                        id: hotMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        onClicked: function (m) {
                            if (m.button === Qt.RightButton || (m.modifiers & Qt.ShiftModifier)) dj.clearHotCue(deckRoot.deck, index)
                            else dj.hotCue(deckRoot.deck, index)
                        }
                    }
                    ToolTip.text: set ? qsTr("Klick: springen · Rechtsklick: löschen") : qsTr("Klick: Hot Cue setzen")
                    ToolTip.visible: hotMouse.containsMouse
                    ToolTip.delay: 700
                }
            }
        }

        // ------------------------------------- Loops, Sprünge
        RowLayout {
            Layout.fillWidth: true
            spacing: 4
            Icon { width: 16; height: 16; name: "loop"; color: info.looping ? Theme.ok : Theme.dim }
            Repeater {
                model: [{ t: "½", b: 0.5 }, { t: "1", b: 1 }, { t: "2", b: 2 }, { t: "4", b: 4 }, { t: "8", b: 8 }, { t: "16", b: 16 }]
                Button {
                    required property var modelData
                    Layout.fillWidth: true
                    implicitWidth: 34
                    text: modelData.t
                    checkable: true
                    checked: info.looping === true && info.loopBeats === modelData.b
                    highlighted: checked
                    onClicked: dj.setLoop(deckRoot.deck, modelData.b)
                }
            }
            Divider { Layout.leftMargin: 4; Layout.rightMargin: 4 }
            IconButton { iconName: "chevron-left"; tip: qsTr("4 Schläge zurück"); onClicked: dj.beatJump(deckRoot.deck, -4) }
            IconButton { iconName: "chevron-right"; tip: qsTr("4 Schläge vor"); onClicked: dj.beatJump(deckRoot.deck, 4) }
        }

        // ---------------------------------------- Stems
        RowLayout {
            Layout.fillWidth: true
            spacing: 4
            Repeater {
                model: [{ t: "VOCAL", c: "#3cd96b", i: 0 }, { t: "INST", c: "#ff3b5c", i: 3 }, { t: "BASS", c: "#ff5bd6", i: 2 }, { t: "DRUMS", c: "#2fc6ff", i: 1 }]
                Rectangle {
                    required property var modelData
                    readonly property bool on: deckRoot.loaded && info.stems ? info.stems[modelData.i] : true
                    Layout.fillWidth: true
                    implicitHeight: 28
                    radius: 5
                    enabled: info.hasStems === true
                    opacity: enabled ? 1 : 0.35
                    color: on ? Theme.alpha(modelData.c, 0.18) : "transparent"
                    border.color: on ? modelData.c : Theme.border
                    Label { anchors.centerIn: parent; text: parent.modelData.t; font.bold: true; font.pixelSize: 11; color: parent.on ? parent.modelData.c : Theme.faint }
                    MouseArea { anchors.fill: parent; onClicked: dj.setStem(deckRoot.deck, parent.modelData.i, !parent.on) }
                }
            }
            Button {
                visible: deckRoot.loaded && !info.hasStems
                text: qsTr("KI-Stems")
                icon.name: "sparkles"
                enabled: dj.aiAvailable && !dj.working
                onClicked: dj.prepareStems(info.path)
                ToolTip.text: dj.aiAvailable ? qsTr("Gesang, Schlagzeug, Bass und Instrumente mit KI trennen (einmalig ca. 90 MB Modelle)") : qsTr("Diese Version hat keine KI eingebaut")
                ToolTip.visible: hovered
                ToolTip.delay: 400
            }
        }
    }
}
