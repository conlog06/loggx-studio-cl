import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Eigenschaften des ausgewählten Clips.
Rectangle {
    id: root
    color: "#1b1e24"
    readonly property var clip: app.clip
    readonly property bool hasClip: clip.id !== undefined

    Label {
        anchors.centerIn: parent
        visible: !root.hasClip
        width: parent.width - 40
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        color: "#8a91a0"
        text: qsTr("Clip in der Timeline anklicken, um Lautstärke, Tonhöhe, Tempo, Deckkraft und Maske einzustellen.")
    }

    ScrollView {
        anchors.fill: parent
        visible: root.hasClip
        contentWidth: availableWidth
        clip: true

        ColumnLayout {
            width: root.width - 28
            x: 14
            spacing: 10

            Item { Layout.preferredHeight: 4 }
            Label {
                Layout.fillWidth: true
                text: root.hasClip ? root.clip.name : ""
                font.bold: true
                elide: Text.ElideMiddle
            }
            Label {
                color: "#8a91a0"
                text: root.hasClip
                      ? qsTr("Start %1 · Länge %2").arg(app.timecode(root.clip.start)).arg(app.timecode(root.clip.duration))
                      : ""
            }

            Label { text: qsTr("TEMPO"); color: "#5d6370"; font.pixelSize: 10; font.bold: true; Layout.topMargin: 8 }
            ValueSlider {
                Layout.fillWidth: true
                label: qsTr("Tempo")
                prop: "speed"
                from: 0.25; to: 4; step: 0.05
                value: root.hasClip ? root.clip.speed : 1
                resetValue: 1
                suffix: "×"
            }

            Label {
                visible: root.hasClip && root.clip.hasAudio
                text: qsTr("TON"); color: "#5d6370"; font.pixelSize: 10; font.bold: true; Layout.topMargin: 8
            }
            ValueSlider {
                Layout.fillWidth: true
                visible: root.hasClip && root.clip.hasAudio
                label: qsTr("Lautstärke")
                prop: "gainDb"
                from: -60; to: 12; step: 0.5
                value: root.hasClip ? root.clip.gainDb : 0
                resetValue: 0
                decimals: 1
                suffix: " dB"
            }
            ValueSlider {
                Layout.fillWidth: true
                visible: root.hasClip && root.clip.hasAudio
                enabled: app.stretchAvailable
                label: qsTr("Tonhöhe")
                prop: "pitch"
                from: -12; to: 12; step: 1
                value: root.hasClip ? root.clip.pitch : 0
                resetValue: 0
                decimals: 0
                suffix: " HT"
            }
            Label {
                Layout.fillWidth: true
                visible: root.hasClip && root.clip.hasAudio && !app.stretchAvailable
                wrapMode: Text.WordWrap
                color: "#d9a441"
                font.pixelSize: 11
                text: qsTr("Diese Version wurde ohne Rubber Band gebaut: Tonhöhe und Tempo wirken nicht auf den Ton.")
            }

            Label {
                visible: root.hasClip && root.clip.isVideo
                text: qsTr("BILD"); color: "#5d6370"; font.pixelSize: 10; font.bold: true; Layout.topMargin: 8
            }
            ValueSlider {
                Layout.fillWidth: true
                visible: root.hasClip && root.clip.isVideo
                label: qsTr("Deckkraft")
                prop: "opacity"
                value: root.hasClip ? root.clip.opacity : 1
                resetValue: 1
                displayFactor: 100
                decimals: 0
                suffix: " %"
            }

            Label {
                visible: root.hasClip && root.clip.isVideo
                text: qsTr("MASKE"); color: "#5d6370"; font.pixelSize: 10; font.bold: true; Layout.topMargin: 8
            }
            RowLayout {
                Layout.fillWidth: true
                visible: root.hasClip && root.clip.isVideo
                spacing: 8
                Label { text: qsTr("Form"); Layout.preferredWidth: 78; color: "#b3b9c6" }
                ComboBox {
                    Layout.fillWidth: true
                    model: [qsTr("Keine"), qsTr("Rechteck"), qsTr("Ellipse")]
                    currentIndex: root.hasClip ? root.clip.maskShape : 0
                    onActivated: function (index) { app.setClipProperty("maskShape", index) }
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                visible: root.hasClip && root.clip.isVideo && root.clip.maskShape > 0
                spacing: 10
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
            Item { Layout.preferredHeight: 12 }
        }
    }
}
