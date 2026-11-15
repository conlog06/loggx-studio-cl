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
                FileDropArea { anchors.fill: parent; importOnly: true }
            }
        }

        // Projektformat
        Rectangle {
            Layout.preferredWidth: 380
            Layout.fillHeight: true
            color: Theme.panel
            radius: 6

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 16
                spacing: 10
                Label { text: qsTr("Projektformat"); font.bold: true }
                Label { text: app.formatInfo; color: Theme.dim }

                ProjectFormatForm { Layout.fillWidth: true; Layout.topMargin: 8; showMusic: true }
                Item { Layout.fillHeight: true }
            }
        }
    }
}
