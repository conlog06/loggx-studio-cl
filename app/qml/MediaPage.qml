import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LoggXStyle

// Arbeitsbereich "Medien": alle importierten Dateien und das Projektformat.
Item {
    id: root
    signal importRequested()

    RowLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 20

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 10
            RowLayout {
                Layout.fillWidth: true
                Label { text: qsTr("Medien"); font.pixelSize: 22; font.bold: true; Layout.fillWidth: true }
                Button { text: qsTr("Importieren …"); highlighted: true; onClicked: root.importRequested() }
            }
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: Theme.panel
                radius: 6

                ListView {
                    id: list
                    anchors.fill: parent
                    anchors.margins: 8
                    clip: true
                    spacing: 4
                    model: app.assets
                    ScrollBar.vertical: ScrollBar {}

                    delegate: Rectangle {
                        id: row
                        required property var modelData
                        width: list.width
                        height: 56
                        radius: 5
                        color: Theme.panel2
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 12
                            anchors.rightMargin: 8
                            spacing: 12
                            Rectangle {
                                width: 4; height: 34; radius: 2
                                color: row.modelData.hasVideo ? Theme.video : Theme.audio
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2
                                Label { Layout.fillWidth: true; text: row.modelData.name; elide: Text.ElideMiddle }
                                Label {
                                    Layout.fillWidth: true
                                    elide: Text.ElideMiddle
                                    font.pixelSize: 11
                                    color: row.modelData.missing ? Theme.danger : Theme.dim
                                    text: (row.modelData.missing ? qsTr("Datei nicht gefunden") : row.modelData.info)
                                          + " · " + row.modelData.path
                                }
                            }
                            Button { text: qsTr("In Timeline"); onClicked: app.addAssetToTimeline(row.modelData.id) }
                            Button { text: qsTr("Entfernen"); onClicked: app.removeAsset(row.modelData.id) }
                        }
                    }

                    Label {
                        anchors.centerIn: parent
                        visible: list.count === 0
                        color: Theme.dim
                        horizontalAlignment: Text.AlignHCenter
                        text: qsTr("Noch keine Medien.\nVideos, Musik oder Bilder importieren oder ins Fenster ziehen.")
                    }
                }
                DropArea {
                    anchors.fill: parent
                    keys: ["text/uri-list"]
                    onDropped: function (drop) {
                        if (drop.hasUrls)
                            app.importMedia(drop.urls)
                    }
                }
            }
        }

        // Projektformat
        Rectangle {
            Layout.preferredWidth: 300
            Layout.fillHeight: true
            color: Theme.panel
            radius: 6

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 16
                spacing: 10
                Label { text: qsTr("Projektformat"); font.bold: true }
                Label { text: app.formatInfo; color: Theme.dim }

                Label { text: qsTr("Auflösung"); Layout.topMargin: 8 }
                ComboBox {
                    id: sizeBox
                    Layout.fillWidth: true
                    textRole: "text"
                    model: [
                        { text: qsTr("Full HD 1920×1080 (16:9)"), w: 1920, h: 1080 },
                        { text: qsTr("HD 1280×720 (16:9)"), w: 1280, h: 720 },
                        { text: qsTr("4K UHD 3840×2160 (16:9)"), w: 3840, h: 2160 },
                        { text: qsTr("Hochkant 1080×1920 (9:16)"), w: 1080, h: 1920 },
                        { text: qsTr("Quadrat 1080×1080 (1:1)"), w: 1080, h: 1080 }
                    ]
                    Component.onCompleted: sync()
                    function sync() {
                        for (let i = 0; i < model.length; ++i)
                            if (model[i].w === app.projectWidth && model[i].h === app.projectHeight)
                                currentIndex = i
                    }
                    onActivated: function (index) {
                        app.setProjectFormat(model[index].w, model[index].h, fpsBox.model[fpsBox.currentIndex])
                    }
                }
                Label { text: qsTr("Bilder pro Sekunde") }
                ComboBox {
                    id: fpsBox
                    Layout.fillWidth: true
                    model: [24, 25, 30, 50, 60]
                    currentIndex: 2
                    onActivated: function (index) {
                        app.setProjectFormat(app.projectWidth, app.projectHeight, model[index])
                    }
                }
                Connections {
                    target: app
                    function onProjectChanged() { sizeBox.sync() }
                }
                Label {
                    Layout.fillWidth: true
                    Layout.topMargin: 8
                    wrapMode: Text.WordWrap
                    color: Theme.dim
                    font.pixelSize: 11
                    text: qsTr("Der Prototyp rechnet Bilder auf der CPU. Für flüssige Vorschau wird sie auf höchstens 960×540 verkleinert; der Export nutzt die volle Auflösung.")
                }
                Item { Layout.fillHeight: true }
            }
        }
    }
}
