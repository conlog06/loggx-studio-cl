import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LoggX 1.0
import LoggXStyle

// Lizenz eingeben und – für Admins – Werkzeuge und Systeminfos.
Dialog {
    id: dialog
    title: qsTr("Lizenz & Admin")
    modal: true
    anchors.centerIn: parent
    width: Math.min(parent.width - 80, 620)
    height: Math.min(parent.height - 60, 720)
    footer: Item {
        implicitHeight: 56
        Button {
            anchors.right: parent.right
            anchors.rightMargin: 16
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("Schließen")
            onClicked: dialog.close()
        }
    }
    readonly property var info: app.systemInfo

    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth
        clip: true

        ColumnLayout {
            width: parent.width
            spacing: 12

            // ------------------------------------------------- Edition
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 72
                radius: 12
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0; color: Theme.alpha(Theme.accent, 0.22) }
                    GradientStop { position: 1; color: Theme.alpha(Theme.accent2, 0.22) }
                }
                border.color: Theme.border
                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 14
                    spacing: 12
                    Icon { width: 28; height: 28; name: app.isAdmin ? "shield" : "key"; color: Theme.text }
                    ColumnLayout {
                        spacing: 2
                        Layout.fillWidth: true
                        Label { text: app.editionName; font.pixelSize: 18; font.bold: true }
                        Label {
                            color: Theme.dim
                            text: !app.licensingActive
                                  ? qsTr("Ohne Lizenzschlüssel gebaut – alle Funktionen frei")
                                  : app.licenseHolder !== "" ? qsTr("Lizenziert für %1").arg(app.licenseHolder)
                                                             : qsTr("Free: Export bis 1080p, H.264")
                        }
                    }
                }
            }

            // ----------------------------------------------- Vergleich
            GridLayout {
                Layout.fillWidth: true
                columns: 4
                columnSpacing: 10
                rowSpacing: 4
                Repeater {
                    model: [
                        "", "Free", "Pro", "Admin",
                        qsTr("Schnitt, Masken, Farbe, Musik"), "✓", "✓", "✓",
                        qsTr("Export 4K / 8K"), "–", "✓", "✓",
                        qsTr("H.265 / HEVC"), "–", "✓", "✓",
                        qsTr("Admin-Werkzeuge"), "–", "–", "✓"
                    ]
                    Label {
                        required property string modelData
                        required property int index
                        text: modelData
                        Layout.fillWidth: index % 4 === 0
                        horizontalAlignment: index % 4 === 0 ? Text.AlignLeft : Text.AlignHCenter
                        Layout.preferredWidth: index % 4 === 0 ? -1 : 60
                        font.bold: index < 4
                        color: modelData === "✓" ? Theme.ok : modelData === "–" ? Theme.faint : index < 4 ? Theme.dim : Theme.text
                    }
                }
            }

            // ------------------------------------------------- Schlüssel
            Label { visible: app.licensingActive; text: qsTr("Lizenzschlüssel"); font.bold: true; Layout.topMargin: 6 }
            TextArea {
                id: keyField
                visible: app.licensingActive
                Layout.fillWidth: true
                Layout.preferredHeight: 70
                wrapMode: TextEdit.WrapAnywhere
                placeholderText: "LGX1-…"
                font.family: "monospace"
            }
            RowLayout {
                visible: app.licensingActive
                spacing: 8
                Button {
                    text: qsTr("Aktivieren")
                    highlighted: true
                    icon.name: "check"
                    enabled: keyField.text.trim() !== ""
                    onClicked: {
                        if (app.activateLicense(keyField.text))
                            keyField.text = ""
                        else
                            errorLabel.text = app.licenseError()
                    }
                }
                Button {
                    text: qsTr("Lizenz entfernen")
                    visible: app.licenseHolder !== ""
                    onClicked: app.removeLicense()
                }
                Label { id: errorLabel; color: Theme.danger; Layout.fillWidth: true; wrapMode: Text.WordWrap }
            }

            // ---------------------------------------------------- Admin
            ColumnLayout {
                visible: app.isAdmin
                Layout.fillWidth: true
                spacing: 8
                Rectangle { Layout.fillWidth: true; Layout.topMargin: 6; height: 1; color: Theme.border }
                RowLayout {
                    Icon { width: 16; height: 16; name: "shield"; color: Theme.accent }
                    Label { text: qsTr("Admin-Werkzeuge"); font.bold: true }
                }
                Switch {
                    text: qsTr("Free-Edition simulieren (zum Testen der Grenzen)")
                    checked: app.simulateFree
                    onToggled: app.simulateFree = checked
                }
                Switch {
                    text: qsTr("Vorschau in voller Auflösung (1080p statt 540p)")
                    checked: app.previewFullRes
                    onToggled: app.previewFullRes = checked
                }
                Label {
                    color: Theme.dim
                    text: app.previewMs > 0 ? qsTr("Letztes Vorschaubild: %1 ms").arg(app.previewMs.toFixed(1)) : qsTr("Noch kein Vorschaubild berechnet")
                }

                Label { text: qsTr("System"); font.bold: true; Layout.topMargin: 6 }
                GridLayout {
                    Layout.fillWidth: true
                    columns: 2
                    columnSpacing: 14
                    rowSpacing: 4
                    Repeater {
                        model: [
                            qsTr("Version"), dialog.info.version,
                            qsTr("System"), dialog.info.system,
                            qsTr("CPU-Threads"), String(dialog.info.threads),
                            "Qt", dialog.info.qt,
                            "FFmpeg", dialog.info.ffmpeg,
                            qsTr("Encoder"), dialog.info.encoders,
                            qsTr("Pitch/Stretch"), dialog.info.rubberband ? qsTr("Rubber Band aktiv") : qsTr("einfach (ohne Rubber Band)"),
                            qsTr("MIDI-Keyboards"), dialog.info.rtmidi ? qsTr("verfügbar") : qsTr("nicht eingebaut"),
                            qsTr("Einstellungen"), dialog.info.settings,
                            qsTr("Autosave"), dialog.info.autosave
                        ]
                        Label {
                            required property string modelData
                            required property int index
                            text: modelData
                            color: index % 2 === 0 ? Theme.dim : Theme.text
                            Layout.fillWidth: index % 2 === 1
                            Layout.preferredWidth: index % 2 === 1 ? 1 : -1
                            wrapMode: Text.WrapAnywhere
                            font.pixelSize: Theme.smallFont + 1
                        }
                    }
                }
            }
        }
    }
}
