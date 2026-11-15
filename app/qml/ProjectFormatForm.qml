import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LoggXStyle

// Auflösung, Bildrate, Tempo und Takt des Projekts – überall gleich.
ColumnLayout {
    id: root
    spacing: 8
    property bool showMusic: true

    readonly property var sizes: [
        { text: "8K UHD · 7680×4320", w: 7680, h: 4320 },
        { text: "4K DCI · 4096×2160", w: 4096, h: 2160 },
        { text: "4K UHD · 3840×2160", w: 3840, h: 2160 },
        { text: "QHD · 2560×1440", w: 2560, h: 1440 },
        { text: "Full HD · 1920×1080", w: 1920, h: 1080 },
        { text: "HD · 1280×720", w: 1280, h: 720 },
        { text: qsTr("Hochkant 9:16 · 1080×1920"), w: 1080, h: 1920 },
        { text: qsTr("Hochkant 4K · 2160×3840"), w: 2160, h: 3840 },
        { text: qsTr("Hochformat 4:5 · 1080×1350"), w: 1080, h: 1350 },
        { text: qsTr("Quadrat · 1080×1080"), w: 1080, h: 1080 },
        { text: qsTr("Eigene Größe …"), w: 0, h: 0 }
    ]
    readonly property var rates: [
        { text: "23,976", num: 24000, den: 1001 },
        { text: "24", num: 24, den: 1 },
        { text: "25", num: 25, den: 1 },
        { text: "29,97", num: 30000, den: 1001 },
        { text: "30", num: 30, den: 1 },
        { text: "48", num: 48, den: 1 },
        { text: "50", num: 50, den: 1 },
        { text: "59,94", num: 60000, den: 1001 },
        { text: "60", num: 60, den: 1 },
        { text: "100", num: 100, den: 1 },
        { text: "120", num: 120, den: 1 }
    ]

    function sizeIndex() {
        for (let i = 0; i < sizes.length - 1; ++i)
            if (sizes[i].w === app.projectWidth && sizes[i].h === app.projectHeight) return i
        return sizes.length - 1
    }
    function rateIndex() {
        for (let i = 0; i < rates.length; ++i)
            if (rates[i].num * app.projectFpsDen === app.projectFpsNum * rates[i].den) return i
        return -1
    }
    function applyRate(i) { app.setProjectFormat(app.projectWidth, app.projectHeight, rates[i].num, rates[i].den) }

    GridLayout {
        Layout.fillWidth: true
        columns: 2
        columnSpacing: 12
        rowSpacing: 8

        Label { text: qsTr("Auflösung") }
        ComboBox {
            id: sizeBox
            Layout.fillWidth: true
            textRole: "text"
            model: root.sizes
            currentIndex: root.sizeIndex()
            onActivated: function (i) {
                const s = root.sizes[i]
                if (s.w > 0)
                    app.setProjectFormat(s.w, s.h, app.projectFpsNum, app.projectFpsDen)
                else
                    customRow.visible = true
            }
        }
        Item { width: 1; visible: customRow.visible }
        RowLayout {
            id: customRow
            visible: sizeBox.currentIndex === root.sizes.length - 1
            spacing: 6
            SpinBox { id: wBox; from: 16; to: 16384; stepSize: 2; editable: true; value: app.projectWidth; Layout.preferredWidth: 120 }
            Label { text: "×" }
            SpinBox { id: hBox; from: 16; to: 16384; stepSize: 2; editable: true; value: app.projectHeight; Layout.preferredWidth: 120 }
            Button { text: qsTr("OK"); onClicked: app.setProjectFormat(wBox.value, hBox.value, app.projectFpsNum, app.projectFpsDen) }
        }

        Label { text: qsTr("Bildrate") }
        RowLayout {
            spacing: 6
            ComboBox {
                id: rateBox
                Layout.fillWidth: true
                textRole: "text"
                model: root.rates
                currentIndex: root.rateIndex()
                displayText: currentIndex >= 0 ? currentText + " fps" : app.formatInfo.split(" · ")[1]
                onActivated: function (i) { root.applyRate(i) }
            }
        }

        Label { text: qsTr("Tempo"); visible: root.showMusic }
        RowLayout {
            visible: root.showMusic
            spacing: 6
            SpinBox {
                from: 20; to: 300; editable: true
                value: Math.round(app.bpm)
                onValueModified: app.bpm = value
            }
            Label { text: "BPM"; color: Theme.dim }
            ComboBox {
                Layout.preferredWidth: 90
                model: ["2/4", "3/4", "4/4", "5/4", "6/8", "7/8"]
                readonly property var beats: [2, 3, 4, 5, 6, 7]
                currentIndex: Math.max(0, beats.indexOf(app.beatsPerBar))
                onActivated: function (i) { app.beatsPerBar = beats[i] }
            }
        }
    }
    Label {
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        color: Theme.faint
        font.pixelSize: Theme.smallFont
        text: qsTr("Clips behalten ihre Zeitposition. Der Export nutzt diese Bildrate und – wenn dort nichts anderes gewählt ist – diese Auflösung.")
    }
}
