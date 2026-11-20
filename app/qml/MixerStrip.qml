import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LoggXStyle

// Kanalzug im Mixer: Pegel, Fader, Panorama, Stumm/Solo und Effekte.
Rectangle {
    id: root
    property var track: ({})          // aus app.tracks; leer = Master
    property bool master: false
    readonly property string trackId: master ? "master" : (track.id || "")
    readonly property real level: app.levels[trackId] !== undefined ? app.levels[trackId] : 0
    implicitWidth: 132
    color: master ? "#232631" : "#1e2128"
    radius: 6

    function set(name, value) { app.setTrackProperty(track.index, name, value) }

    // Plugin auswählen
    Popup {
        id: pluginPicker
        modal: true
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: 460
        height: 480
        onOpened: { search.text = ""; search.forceActiveFocus() }
        readonly property var shown: app.availablePlugins.filter(function (p) {
            const q = search.text.toLowerCase()
            return q === "" || p.name.toLowerCase().indexOf(q) >= 0 || p.vendor.toLowerCase().indexOf(q) >= 0
        })
        contentItem: ColumnLayout {
            spacing: 8
            Label { text: qsTr("Plugin für „%1“").arg(root.track.name || ""); font.bold: true }
            TextField { id: search; Layout.fillWidth: true; placeholderText: qsTr("Suchen (Name oder Hersteller) …") }
            ListView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: pluginPicker.shown
                ScrollBar.vertical: ScrollBar {}
                delegate: ItemDelegate {
                    required property var modelData
                    width: ListView.view.width
                    contentItem: RowLayout {
                        Label { text: modelData.name; Layout.fillWidth: true; elide: Text.ElideRight }
                        Label { text: modelData.vendor; color: Theme.dim; font.pixelSize: 11 }
                        Label { text: modelData.format.toUpperCase(); color: Theme.faint; font.pixelSize: 10; Layout.preferredWidth: 34 }
                    }
                    onClicked: { app.addPlugin(root.track.index, modelData); pluginPicker.close() }
                }
                Label {
                    anchors.centerIn: parent
                    visible: pluginPicker.shown.length === 0
                    width: parent.width - 40
                    wrapMode: Text.WordWrap
                    horizontalAlignment: Text.AlignHCenter
                    color: Theme.dim
                    text: app.availablePlugins.length === 0 ? qsTr("Noch keine Plugins gefunden. „Plugins suchen“ durchsucht die üblichen Ordner (CLAP, VST3%1).").arg(Qt.platform.os === "osx" ? qsTr(", Audio Units") : "")
                                                            : qsTr("Kein Treffer")
                }
            }
            RowLayout {
                Button { text: qsTr("Plugins suchen"); icon.name: "sparkles"; enabled: !app.working; onClicked: app.scanPluginsNow() }
                Item { Layout.fillWidth: true }
                Button { text: qsTr("Schließen"); onClicked: pluginPicker.close() }
            }
        }
    }

    // Parameter eines Plugins (für Plugins ohne eigene Oberfläche)
    Dialog {
        id: paramDialog
        property var plugin: ({})
        property var params: []
        function show(p) { plugin = p; params = app.pluginParams(p.uid); open() }
        title: plugin.name || ""
        modal: true
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: 440
        height: Math.min(560, 120 + params.length * 44)
        standardButtons: Dialog.Close
        contentItem: ListView {
            clip: true
            model: paramDialog.params
            ScrollBar.vertical: ScrollBar {}
            delegate: ColumnLayout {
                required property var modelData
                width: ListView.view.width
                spacing: 0
                RowLayout {
                    Label { text: modelData.name; Layout.fillWidth: true; elide: Text.ElideRight }
                    Label { text: slider.value.toFixed(modelData.stepped ? 0 : 2) + (modelData.unit ? " " + modelData.unit : ""); color: Theme.dim; font.pixelSize: 11 }
                }
                Slider {
                    id: slider
                    Layout.fillWidth: true
                    from: modelData.min
                    to: modelData.max
                    value: modelData.value
                    stepSize: modelData.stepped ? (modelData.max - modelData.min <= 1 ? 1 / Math.max(1, modelData.max) : 1) : 0
                    onMoved: app.setPluginParam(root.track.index, paramDialog.plugin.uid, modelData.id, value)
                    onPressedChanged: pressed ? app.beginInteractive() : app.endInteractive()
                }
            }
            Label { anchors.centerIn: parent; visible: paramDialog.params.length === 0; text: qsTr("Keine einstellbaren Parameter"); color: Theme.dim }
        }
    }

    // Kleiner Drehregler-Ersatz: kompakter Schieberegler mit Beschriftung.
    component Knob: ColumnLayout {
        id: knob
        property string text
        property real from: -12
        property real to: 12
        property real value: 0
        property real resetValue: 0
        property string name
        property string unit: ""
        property real factor: 1
        spacing: 0
        Layout.fillWidth: true
        RowLayout {
            Layout.fillWidth: true
            Label {
                text: knob.text
                font.pixelSize: 10
                color: Theme.dim
                Layout.fillWidth: true
                MouseArea { anchors.fill: parent; onDoubleClicked: root.set(knob.name, knob.resetValue) }
            }
            Label { text: (knob.value * knob.factor).toFixed(0) + knob.unit; font.pixelSize: 10; color: Theme.dim }
        }
        Slider {
            Layout.fillWidth: true
            Layout.preferredHeight: 18
            from: knob.from
            to: knob.to
            value: knob.value
            onMoved: root.set(knob.name, value)
            onPressedChanged: pressed ? app.beginInteractive() : app.endInteractive()
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 4

        Label {
            Layout.fillWidth: true
            text: root.master ? qsTr("Master") : root.track.name
            font.bold: true
            elide: Text.ElideRight
            horizontalAlignment: Text.AlignHCenter
        }
        Label {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            font.pixelSize: 10
            color: root.master ? Theme.accent : root.track.kind === "instrument" ? "#d9822b" : root.track.kind === "audio" ? Theme.audio : Theme.video
            text: root.master ? qsTr("Summe + Limiter")
                  : root.track.kind === "instrument" ? app.instrumentNames[root.track.instrument]
                  : root.track.kind === "audio" ? qsTr("Audio") : qsTr("Ton der Videospur")
        }

        // Effekte (nicht beim Master)
        ColumnLayout {
            visible: !root.master
            Layout.fillWidth: true
            spacing: 2
            Knob { text: qsTr("Bässe"); name: "eqLow"; value: root.track.eqLow || 0; unit: " dB" }
            Knob { text: qsTr("Mitten"); name: "eqMid"; value: root.track.eqMid || 0; unit: " dB" }
            Knob { text: qsTr("Höhen"); name: "eqHigh"; value: root.track.eqHigh || 0; unit: " dB" }
            Knob { text: qsTr("Kompressor"); name: "compressor"; from: 0; to: 1; value: root.track.compressor || 0; factor: 100; unit: " %" }
            Knob { text: qsTr("Echo"); name: "delay"; from: 0; to: 1; value: root.track.delay || 0; factor: 100; unit: " %" }
            Knob { text: qsTr("Hall"); name: "reverb"; from: 0; to: 1; value: root.track.reverb || 0; factor: 100; unit: " %" }
            Knob { text: qsTr("Panorama"); name: "pan"; from: -1; to: 1; value: root.track.pan || 0; factor: 100 }
        }
        // Audio-Plugins (CLAP, VST3, AU)
        RowLayout {
            visible: !root.master
            Layout.fillWidth: true
            Layout.topMargin: 4
            Label { text: qsTr("PLUGINS"); font.pixelSize: 9; font.bold: true; color: Theme.faint; Layout.fillWidth: true }
            IconButton {
                iconName: "plus"
                implicitWidth: 20
                implicitHeight: 20
                tip: qsTr("Plugin einsetzen (CLAP, VST3, AU)")
                onClicked: pluginPicker.open()
            }
        }
        Repeater {
            model: root.master ? [] : (root.track.plugins || [])
            Rectangle {
                id: slot
                required property int index
                required property var modelData
                readonly property string err: app.pluginError(modelData.uid)
                Layout.fillWidth: true
                implicitHeight: 22
                radius: 4
                color: slotMouse.containsMouse ? Theme.hover : Theme.panel2
                border.color: err !== "" ? Theme.danger : modelData.bypass ? Theme.border : Theme.alpha(Theme.accent, 0.6)
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 4
                    anchors.rightMargin: 2
                    spacing: 2
                    Rectangle {
                        width: 8; height: 8; radius: 4
                        color: slot.err !== "" ? Theme.danger : slot.modelData.bypass ? Theme.faint : Theme.ok
                        MouseArea {
                            anchors.fill: parent
                            anchors.margins: -4
                            onClicked: app.setPluginBypass(root.track.index, slot.modelData.uid, !slot.modelData.bypass)
                        }
                    }
                    Label {
                        Layout.fillWidth: true
                        text: slot.modelData.name
                        elide: Text.ElideRight
                        font.pixelSize: 10
                        color: slot.modelData.bypass ? Theme.faint : Theme.text
                    }
                }
                MouseArea {
                    id: slotMouse
                    anchors.fill: parent
                    anchors.leftMargin: 14
                    hoverEnabled: true
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    onClicked: function (m) {
                        if (m.button === Qt.RightButton) slotMenu.popup()
                        else if (slot.err !== "") return
                        else if (!app.openPluginEditor(slot.modelData.uid)) paramDialog.show(slot.modelData)
                    }
                }
                Menu {
                    id: slotMenu
                    MenuItem { text: qsTr("Oberfläche öffnen"); enabled: app.pluginHasEditor(slot.modelData.uid); onTriggered: app.openPluginEditor(slot.modelData.uid) }
                    MenuItem { text: qsTr("Parameter …"); onTriggered: paramDialog.show(slot.modelData) }
                    MenuItem { text: slot.modelData.bypass ? qsTr("Einschalten") : qsTr("Umgehen (Bypass)"); onTriggered: app.setPluginBypass(root.track.index, slot.modelData.uid, !slot.modelData.bypass) }
                    MenuSeparator {}
                    MenuItem { text: qsTr("Nach oben"); enabled: slot.index > 0; onTriggered: app.movePlugin(root.track.index, slot.modelData.uid, -1) }
                    MenuItem { text: qsTr("Nach unten"); enabled: slot.index < root.track.plugins.length - 1; onTriggered: app.movePlugin(root.track.index, slot.modelData.uid, 1) }
                    MenuSeparator {}
                    MenuItem { text: qsTr("Entfernen"); onTriggered: app.removePlugin(root.track.index, slot.modelData.uid) }
                }
                ToolTip.text: (slot.err !== "" ? slot.err : (slot.modelData.vendor + " · " + slot.modelData.format.toUpperCase())) + qsTr("\nKlick: öffnen · Punkt: an/aus · Rechtsklick: Menü")
                ToolTip.visible: slotMouse.containsMouse
                ToolTip.delay: 700
            }
        }
        Item { visible: root.master; Layout.fillHeight: true }

        // Pegel + Fader
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 120
            spacing: 6
            Rectangle {
                Layout.fillHeight: true
                Layout.preferredWidth: 10
                color: "#0a0b0e"
                radius: 2
                Rectangle {
                    anchors.bottom: parent.bottom
                    width: parent.width
                    height: parent.height * Math.min(1, root.level)
                    radius: 2
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: root.level > 0.98 ? Theme.danger : "#e8c547" }
                        GradientStop { position: 0.35; color: "#4fc36b" }
                        GradientStop { position: 1.0; color: Theme.audio }
                    }
                }
            }
            Slider {
                id: fader
                Layout.fillHeight: true
                Layout.fillWidth: true
                orientation: Qt.Vertical
                from: -60
                to: 6
                value: root.master ? app.masterGainDb : (root.track.gainDb !== undefined ? root.track.gainDb : 0)
                onMoved: root.master ? app.masterGainDb = value : root.set("gainDb", value)
                onPressedChanged: pressed ? app.beginInteractive() : app.endInteractive()
            }
        }
        Label {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            font.family: "monospace"
            font.pixelSize: 11
            text: fader.value <= -59.9 ? "−∞ dB" : fader.value.toFixed(1) + " dB"
            MouseArea {
                anchors.fill: parent
                onDoubleClicked: root.master ? app.masterGainDb = 0 : root.set("gainDb", 0)
            }
        }
        RowLayout {
            Layout.fillWidth: true
            visible: !root.master
            spacing: 4
            Button {
                Layout.fillWidth: true
                text: "M"
                checkable: true
                checked: root.track.muted === true
                onClicked: root.set("muted", checked ? 1 : 0)
                palette.button: checked ? "#a8323e" : Theme.hover
                ToolTip.text: qsTr("Stumm"); ToolTip.visible: hovered; ToolTip.delay: 500
            }
            Button {
                Layout.fillWidth: true
                text: "S"
                checkable: true
                checked: root.track.solo === true
                onClicked: root.set("solo", checked ? 1 : 0)
                palette.button: checked ? "#b08a1e" : Theme.hover
                ToolTip.text: qsTr("Solo"); ToolTip.visible: hovered; ToolTip.delay: 500
            }
        }
        Label {
            Layout.fillWidth: true
            visible: root.master
            horizontalAlignment: Text.AlignHCenter
            font.pixelSize: 10
            color: (app.levels.reduction || 0) < -0.5 ? "#e8c547" : Theme.faint
            text: qsTr("Limiter %1 dB").arg((app.levels.reduction || 0).toFixed(1))
        }
    }
}
