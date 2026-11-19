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

    // ---- Performance-Pads
    readonly property var padModes: [qsTr("HOT CUE"), qsTr("PAD FX"), qsTr("BEAT JUMP"), qsTr("BEAT LOOP"), qsTr("KEY SHIFT"), qsTr("SAMPLER"), qsTr("STEMS")]
    property int padMode: dj.savedPadMode(deck) % 10
    property int fxPage: dj.savedPadMode(deck) >= 10 ? 1 : 0
    onPadModeChanged: dj.savePadMode(deck, padMode, fxPage)
    onFxPageChanged: dj.savePadMode(deck, padMode, fxPage)
    property real loopLen: 4
    readonly property var jumpPads: [-1, 1, -2, 2, -4, 4, -8, 8]
    readonly property var loopPads: [0.25, 0.5, 1, 2, 4, 8, 16, 32]
    readonly property var keyPads: [0, 1, 2, 3, 99, -1, -2, -3]
    readonly property var stemPads: [{ t: "VOCAL", c: "#3cd96b", i: 0 }, { t: "DRUMS", c: "#2fc6ff", i: 1 }, { t: "BASS", c: "#ff5bd6", i: 2 }, { t: "INST", c: "#ff3b5c", i: 3 }]
    Connections {
        target: dj
        function onPadModeHint(deck, mode) {
            if (deck !== deckRoot.deck) return
            deckRoot.padMode = mode % 10
            if (mode % 10 === 1) deckRoot.fxPage = mode >= 10 ? 1 : 0
        }
    }
    function beatText(b) { return b < 1 ? "1/" + Math.round(1 / b) : String(b) }
    function padInfo(i, inf, fx, smp) {
        const loaded = inf.loaded === true
        switch (padMode) {
        case 0: {
            const set = loaded && inf.hot && inf.hot[i] >= 0
            return { text: String.fromCharCode(65 + i), lit: set, color: hotColors[i], enabled: loaded,
                     tip: set ? qsTr("Klick: springen · Rechtsklick: löschen") : qsTr("Klick: Hot Cue setzen") }
        }
        case 1: {
            const f = fx[fxPage * 8 + i]
            if (!f) return { text: "", enabled: false, color: Theme.border }
            const colors = ["#2fc6ff", "#3cd96b", "#5b6cff", "#2fc6ff", "#1fb5a8", "#1fb5a8", "#24d0e8", "#24d0e8", "#1fb5a8", "#5b6cff", "#b05bff", "#5b6cff"]
            return { text: (f.release ? "Ⓡ " : "") + f.name, value: f.value, lit: f.active, color: colors[f.type] || "#2fc6ff", enabled: loaded,
                     tip: f.release ? qsTr("Halten: Effekt · Deck stoppt am Ende") : qsTr("Halten: Effekt, Loslassen: aus") }
        }
        case 2: return { text: (jumpPads[i] < 0 ? "◀ " : "▶ ") + Math.abs(jumpPads[i]), lit: false, color: "#3cd96b", enabled: loaded,
                         tip: qsTr("%1 Schläge springen").arg(jumpPads[i]) }
        case 3: return { text: qsTr("LOOP"), value: beatText(loopPads[i]), color: "#ff8c1a", enabled: loaded,
                         lit: inf.looping === true && Math.abs((inf.loopBeats || 0) - loopPads[i]) < 1e-6 }
        case 4: {
            const v = keyPads[i]
            return { text: v === 99 ? "KEY SYNC" : v === 0 ? qsTr("RESET") : (v > 0 ? "+" + v : String(v)), color: "#b05bff", enabled: loaded,
                     lit: v !== 99 && loaded && (inf.keyShift || 0) === v }
        }
        case 5: {
            const s = smp[i] || {}
            return { text: s.name || "", lit: s.playing === true, color: "#ffd41a", enabled: true,
                     tip: qsTr("Klick: abspielen · Rechtsklick: stoppen") }
        }
        default: {
            const st = stemPads[i % 4]
            const has = loaded && inf.hasStems === true
            const on = has && inf.stems ? inf.stems[st.i] : false
            return i < 4 ? { text: st.t, lit: on, color: st.c, enabled: has, tip: qsTr("Stem an/aus") }
                         : { text: st.t + " SOLO", lit: false, color: st.c, enabled: has, tip: qsTr("Halten: nur dieser Stem") }
        }
        }
    }
    function padPress(i, down, alt) {
        switch (padMode) {
        case 0:
            if (down) { if (alt) dj.clearHotCue(deck, i); else dj.hotCue(deck, i) }
            break
        case 1: dj.padFx(deck, fxPage * 8 + i, down); break
        case 2: if (down) dj.beatJump(deck, jumpPads[i]); break
        case 3: if (down) dj.setLoop(deck, loopPads[i]); break
        case 4:
            if (!down) break
            if (keyPads[i] === 99) dj.keySync(deck)
            else { if (keyPads[i] !== 0) dj.setKeyLock(deck, true); dj.setKeyShift(deck, keyPads[i]) }
            break
        case 5: if (down) { if (alt) dj.stopSample(i); else dj.triggerSample(i) } break
        default:
            if (i < 4) { if (down) dj.setStem(deck, stemPads[i].i, !(info.stems && info.stems[stemPads[i].i])) }
            else dj.stemSolo(deck, stemPads[i - 4].i, down)
        }
    }

    // Pad FX belegen (wie das Zahnrad bei rekordbox)
    Dialog {
        id: padFxEditor
        title: qsTr("Pad FX belegen")
        modal: true
        anchors.centerIn: Overlay.overlay
        standardButtons: Dialog.Close
        width: 560
        contentItem: ColumnLayout {
            spacing: 6
            Repeater {
                model: 2
                ColumnLayout {
                    id: pageCol
                    required property int index
                    Label { text: qsTr("Seite %1").arg(pageCol.index + 1); font.bold: true; color: Theme.dim }
                    GridLayout {
                        columns: 4
                        columnSpacing: 6
                        rowSpacing: 4
                        Repeater {
                            model: 8
                            ColumnLayout {
                                id: slotCol
                                required property int index
                                readonly property int slot: pageCol.index * 8 + index
                                readonly property var f: dj.padFxSlots[slot] || ({})
                                spacing: 2
                                ComboBox {
                                    implicitWidth: 126
                                    model: dj.fxNameList
                                    currentIndex: slotCol.f.type || 0
                                    onActivated: dj.setPadFxSlot(slotCol.slot, currentIndex, slotCol.f.beats, slotCol.f.wet)
                                }
                                ComboBox {
                                    implicitWidth: 126
                                    readonly property var beats: [0.125, 0.25, 0.5, 0.75, 1, 2, 4, 8]
                                    model: ["1/8", "1/4", "1/2", "3/4", "1", "2", "4", "8"]
                                    currentIndex: Math.max(0, beats.indexOf(slotCol.f.beats))
                                    onActivated: dj.setPadFxSlot(slotCol.slot, slotCol.f.type, beats[currentIndex], slotCol.f.wet)
                                }
                            }
                        }
                    }
                }
            }
            Button { text: qsTr("Standard wiederherstellen"); onClicked: dj.resetPadFx() }
        }
    }

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
        MenuItem { text: qsTr("Stems vorbereiten"); icon.name: "sparkles"; enabled: !info.hasStems && !dj.working; onTriggered: dj.prepareStems(info.path) }
        MenuItem { text: qsTr("Stems neu berechnen"); enabled: info.hasStems === true && !dj.working; onTriggered: dj.recomputeStems(info.path) }
        MenuSeparator {}
        MenuItem { text: qsTr("Auswerfen"); enabled: !info.playing; onTriggered: dj.eject(deckRoot.deck) }
    }

    // Track aus der Bibliothek hierher ziehen
    DropArea {
        anchors.fill: parent
        keys: ["loggx/djtrack", "text/uri-list"]
        onDropped: function (drop) {
            const path = drop.hasFormat("loggx/djtrack") ? drop.getDataAsString("loggx/djtrack") : ""
            if (path !== "") dj.loadTrack(deckRoot.deck, path)
            else if (drop.hasUrls) dj.loadUrl(deckRoot.deck, drop.urls[0])   // Datei aus Explorer/Finder
            drop.accept(Qt.CopyAction)
        }
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
                implicitWidth: 30; implicitHeight: 30; radius: 8
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
                implicitWidth: keyText.implicitWidth + 14; implicitHeight: 24; radius: 6
                Layout.alignment: Qt.AlignVCenter
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
        // Aufbau: Intro, Up (Aufbau), Chorus (Drop), Down (Breakdown), Outro
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 13
            Layout.topMargin: -6
            visible: deckRoot.loaded && (info.phrases || []).length > 0
            Repeater {
                model: info.phrases || []
                Rectangle {
                    required property var modelData
                    x: modelData.start * parent.width
                    width: Math.max(1, (modelData.end - modelData.start) * parent.width - 1)
                    height: parent.height
                    radius: 2
                    color: modelData.kind === "Chorus" ? "#2fbf5b" : modelData.kind === "Up" ? "#8a4dff" : modelData.kind === "Down" ? "#c98a2a"
                         : modelData.kind === "Intro" ? "#d64545" : "#3d7bd9"
                    opacity: 0.85
                    Label {
                        anchors.left: parent.left
                        anchors.leftMargin: 3
                        anchors.verticalCenter: parent.verticalCenter
                        text: modelData.label || modelData.kind.toUpperCase()
                        visible: parent.width > 34
                        font.pixelSize: 8
                        font.bold: true
                        color: "white"
                    }
                }
            }
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

        // ------------------------------------------ Performance-Pads
        // Modi wie bei rekordbox / am FLX4: Hot Cue, Pad FX (2 Seiten),
        // Beat Jump, Beat Loop, Key Shift, Sampler, Stems
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 4
            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                Button {
                    id: modeButton
                    flat: true
                    implicitHeight: 24
                    text: deckRoot.padModes[deckRoot.padMode] + "  ▾"
                    font.bold: true
                    font.pixelSize: 11
                    onClicked: modeMenu.popup(modeButton, 0, modeButton.height)
                    ToolTip.text: qsTr("Pad-Modus wählen (am FLX4: Pad-Mode-Tasten, mit Shift die zweite Belegung)")
                    ToolTip.visible: hovered
                    ToolTip.delay: 600
                }
                // Kompakte Loop-Box: Länge, halbieren/verdoppeln, an/aus
                Rectangle {
                    implicitWidth: 104
                    implicitHeight: 22
                    radius: 4
                    color: info.looping ? Theme.alpha(Theme.ok, 0.18) : Theme.panel2
                    border.color: info.looping ? Theme.ok : Theme.border
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 4
                        anchors.rightMargin: 2
                        spacing: 0
                        Icon { width: 13; height: 13; name: "loop"; color: info.looping ? Theme.ok : Theme.dim }
                        Label {
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignHCenter
                            text: deckRoot.beatText(info.looping ? info.loopBeats : deckRoot.loopLen)
                            font.bold: true
                            font.pixelSize: 11
                            MouseArea {
                                anchors.fill: parent
                                enabled: deckRoot.loaded
                                onClicked: info.looping ? dj.exitLoop(deckRoot.deck) : dj.setLoop(deckRoot.deck, deckRoot.loopLen)
                            }
                        }
                        Repeater {
                            model: ["‹", "›"]
                            Label {
                                required property int index
                                required property string modelData
                                text: modelData
                                font.pixelSize: 15
                                leftPadding: 4
                                rightPadding: 4
                                color: Theme.dim
                                MouseArea {
                                    anchors.fill: parent
                                    onClicked: {
                                        const cur = info.looping ? info.loopBeats : deckRoot.loopLen
                                        const next = index === 0 ? Math.max(0.125, cur / 2) : Math.min(64, cur * 2)
                                        deckRoot.loopLen = next
                                        if (info.looping) dj.setLoop(deckRoot.deck, next)
                                    }
                                }
                            }
                        }
                    }
                    ToolTip.text: qsTr("Loop: Zahl = an/aus · ‹ › halbieren/verdoppeln")
                    ToolTip.delay: 700
                }
                Menu {
                    id: modeMenu
                    Repeater {
                        model: deckRoot.padModes
                        MenuItem {
                            required property int index
                            required property string modelData
                            text: modelData
                            checkable: true
                            checked: deckRoot.padMode === index
                            onTriggered: deckRoot.padMode = index
                        }
                    }
                }
                Item { Layout.fillWidth: true }
                // Stems (wie MUTE/DRUMS/VOCAL/INST bei rekordbox)
                Repeater {
                    model: deckRoot.loaded && info.hasStems ? [{ t: "VOCAL", c: "#3cd96b", i: 0 }, { t: "DRUMS", c: "#2fc6ff", i: 1 }, { t: "BASS", c: "#ff5bd6", i: 2 }, { t: "INST", c: "#ff3b5c", i: 3 }] : []
                    Rectangle {
                        required property var modelData
                        readonly property bool on: info.stems ? info.stems[modelData.i] : true
                        implicitWidth: 50
                        implicitHeight: 20
                        radius: 4
                        color: on ? Theme.alpha(modelData.c, 0.18) : "transparent"
                        border.color: on ? modelData.c : Theme.border
                        Label { anchors.centerIn: parent; text: parent.modelData.t; font.bold: true; font.pixelSize: 9; color: parent.on ? parent.modelData.c : Theme.faint }
                        MouseArea { anchors.fill: parent; onClicked: dj.setStem(deckRoot.deck, parent.modelData.i, !parent.on) }
                    }
                }
                Button {
                    visible: deckRoot.loaded && !info.hasStems
                    implicitHeight: 22
                    font.pixelSize: 11
                    text: qsTr("Stems trennen")
                    icon.name: "sparkles"
                    enabled: !dj.working
                    onClicked: dj.prepareStems(info.path)
                    ToolTip.text: dj.stemEngine === 1 && dj.aiAvailable ? qsTr("Gesang, Schlagzeug, Bass und Instrumente mit KI trennen")
                                                                         : qsTr("Gesang, Schlagzeug, Bass und Instrumente trennen – eingebaute Engine, sofort, ohne Download")
                    ToolTip.visible: hovered
                    ToolTip.delay: 400
                }
                Repeater {
                    model: deckRoot.padMode === 1 ? 2 : 0
                    Rectangle {
                        required property int index
                        implicitWidth: 20
                        implicitHeight: 18
                        radius: 3
                        color: deckRoot.fxPage === index ? "#2a6bff" : Theme.panel2
                        border.color: Theme.border
                        Label { anchors.centerIn: parent; text: index + 1; font.pixelSize: 10; color: "white" }
                        MouseArea { anchors.fill: parent; onClicked: deckRoot.fxPage = index }
                    }
                }
                IconButton {
                    visible: deckRoot.padMode === 1
                    iconName: "settings"
                    tip: qsTr("Pad FX belegen")
                    onClicked: padFxEditor.open()
                }
            }
            GridLayout {
                Layout.fillWidth: true
                columns: 4
                columnSpacing: 4
                rowSpacing: 4
                Repeater {
                    model: 8
                    Rectangle {
                        id: pad
                        required property int index
                        readonly property var p: deckRoot.padInfo(index, info, dj.padFxSlots, dj.sampler)
                        Layout.fillWidth: true
                        implicitHeight: 26
                        radius: 5
                        opacity: p.enabled ? 1 : 0.35
                        color: p.lit ? Theme.alpha(p.color, padMouse.pressed ? 0.75 : 0.42) : padMouse.pressed ? Theme.alpha(p.color, 0.3)
                               : padMouse.containsMouse ? Theme.hover : Theme.panel2
                        border.color: p.lit ? p.color : Theme.border
                        Rectangle { width: 3; height: parent.height - 10; radius: 1; x: 4; anchors.verticalCenter: parent.verticalCenter; color: pad.p.color; visible: deckRoot.padMode !== 0 }
                        Label {
                            anchors.left: parent.left
                            anchors.leftMargin: deckRoot.padMode === 0 ? 0 : 11
                            anchors.right: valueLabel.left
                            anchors.verticalCenter: parent.verticalCenter
                            horizontalAlignment: deckRoot.padMode === 0 ? Text.AlignHCenter : Text.AlignLeft
                            text: pad.p.text
                            elide: Text.ElideRight
                            font.bold: true
                            font.pixelSize: deckRoot.padMode === 0 ? 13 : 10
                            color: pad.p.lit ? "white" : Theme.dim
                        }
                        Label {
                            id: valueLabel
                            anchors.right: parent.right
                            anchors.rightMargin: 6
                            anchors.verticalCenter: parent.verticalCenter
                            text: pad.p.value || ""
                            font.pixelSize: 10
                            color: Theme.faint
                        }
                        MouseArea {
                            id: padMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            enabled: pad.p.enabled
                            acceptedButtons: Qt.LeftButton | Qt.RightButton
                            onPressed: function (m) { deckRoot.padPress(pad.index, true, m.button === Qt.RightButton || (m.modifiers & Qt.ShiftModifier)) }
                            onReleased: deckRoot.padPress(pad.index, false, false)
                            onCanceled: deckRoot.padPress(pad.index, false, false)
                        }
                        ToolTip.text: pad.p.tip || ""
                        ToolTip.visible: padMouse.containsMouse && ToolTip.text !== ""
                        ToolTip.delay: 700
                    }
                }
            }
        }

    }
}
