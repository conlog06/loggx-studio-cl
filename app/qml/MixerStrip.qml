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
