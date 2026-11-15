import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LoggX 1.0
import LoggXStyle

// Arbeitsbereich "Schnitt": Medienliste, Vorschau, Inspector, Timeline.
Item {
    id: editRoot
    signal importRequested()
    signal openPage(int page)

    TimelineMenu {
        id: editMenu
        onOpenPage: function (p) { editRoot.openPage(p) }
        onImportRequested: editRoot.importRequested()
    }

    SplitView {
        anchors.fill: parent
        orientation: Qt.Vertical
        handle: Rectangle { implicitHeight: 5; color: SplitHandle.hovered || SplitHandle.pressed ? Theme.accent : Theme.bg }

        // ------------------------------------------------ obere Hälfte
        SplitView {
            SplitView.fillHeight: true
            SplitView.minimumHeight: 260
            orientation: Qt.Horizontal
            handle: Rectangle { implicitWidth: 5; color: SplitHandle.hovered || SplitHandle.pressed ? Theme.accent : Theme.bg }

            // Medienliste
            Rectangle {
                SplitView.preferredWidth: 270
                SplitView.minimumWidth: 200
                color: Theme.panel

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 8
                    RowLayout {
                        Layout.fillWidth: true
                        Label { text: qsTr("Medien"); font.bold: true; Layout.fillWidth: true }
                        Button { text: qsTr("Importieren"); icon.name: "import"; flat: true; onClicked: editRoot.importRequested() }
                    }
                    ListView {
                        id: mediaList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: 4
                        model: app.assets
                        ScrollBar.vertical: ScrollBar {}

                        delegate: Item {
                            id: row
                            required property var modelData
                            width: mediaList.width
                            height: 46

                            Rectangle {
                                anchors.fill: parent
                                radius: 5
                                color: mouse.containsMouse ? Theme.hover : Theme.panel2
                                Rectangle {
                                    width: 3; height: parent.height - 12; radius: 2
                                    x: 6; anchors.verticalCenter: parent.verticalCenter
                                    color: row.modelData.hasVideo ? Theme.video : Theme.audio
                                }
                                Column {
                                    anchors.verticalCenter: parent.verticalCenter
                                    x: 16
                                    width: parent.width - 24
                                    spacing: 2
                                    Label { width: parent.width; text: row.modelData.name; elide: Text.ElideMiddle }
                                    Label {
                                        width: parent.width
                                        elide: Text.ElideRight
                                        font.pixelSize: 11
                                        color: row.modelData.missing ? Theme.danger : Theme.dim
                                        text: row.modelData.missing ? qsTr("Datei nicht gefunden") : row.modelData.info
                                    }
                                }
                            }
                            MouseArea {
                                id: mouse
                                anchors.fill: parent
                                hoverEnabled: true
                                drag.target: ghost
                                onDoubleClicked: app.addAssetToTimeline(row.modelData.id)
                                onReleased: {
                                    if (drag.active)
                                        ghost.Drag.drop()
                                    ghost.x = 0
                                    ghost.y = 0
                                }
                            }
                            // Beim Ziehen hängt das Etikett an der Maus und liegt über allem.
                            Rectangle {
                                id: ghost
                                property string assetId: row.modelData.id
                                width: 170; height: 28; radius: 5
                                visible: mouse.drag.active
                                color: row.modelData.hasVideo ? Theme.video : Theme.audio
                                opacity: 0.85
                                Drag.active: mouse.drag.active
                                Drag.keys: ["loggx/asset"]
                                Drag.hotSpot.x: 8
                                Drag.hotSpot.y: 14
                                Label {
                                    anchors.fill: parent
                                    anchors.margins: 6
                                    elide: Text.ElideMiddle
                                    color: "white"
                                    text: row.modelData.name
                                }
                                states: State {
                                    when: mouse.drag.active
                                    ParentChange { target: ghost; parent: editRoot }
                                }
                            }
                        }

                        Label {
                            anchors.centerIn: parent
                            visible: mediaList.count === 0
                            width: parent.width - 20
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.WordWrap
                            color: Theme.dim
                            text: qsTr("Videos, Musik oder Bilder importieren oder hierher ziehen.\n\nDoppelklick oder Ziehen legt eine Datei in die Timeline.")
                        }
                    }
                }
                FileDropArea { anchors.fill: parent; importOnly: true }
            }

            // Vorschau
            Rectangle {
                SplitView.fillWidth: true
                SplitView.minimumWidth: 360
                color: "#0a0b0e"
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0
                    Viewer {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.margins: 10
                        controller: app
                        FileDropArea { anchors.fill: parent }
                    }
                    PreviewBar { Layout.fillWidth: true }
                }
            }

            Inspector {
                SplitView.preferredWidth: 330
                SplitView.minimumWidth: 270
                onOpenPage: function (page) { editRoot.openPage(page) }
            }
        }

        // ------------------------------------------------------ Timeline
        Rectangle {
            SplitView.preferredHeight: 330
            SplitView.minimumHeight: 160
            color: Theme.bg

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 44
                    color: Theme.panel
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 10
                        spacing: 6
                        IconButton { iconName: "scissors"; tip: qsTr("Am Abspielkopf teilen (S)"); onClicked: app.splitAtPlayhead() }
                        IconButton { iconName: "trash"; tip: qsTr("Clip löschen (Entf)"); enabled: app.selectedClipId !== ""; onClicked: app.deleteSelected(false) }
                        Button { text: qsTr("Lücke schließen"); flat: true; icon.name: "trash"; enabled: app.selectedClipId !== ""; onClicked: app.deleteSelected(true); ToolTip.text: qsTr("Löschen und nachfolgende Clips aufrücken (Umschalt+Entf)"); ToolTip.visible: hovered; ToolTip.delay: 600 }
                        Divider { Layout.leftMargin: 4; Layout.rightMargin: 4 }
                        IconButton { iconName: "text"; tip: qsTr("Titel am Abspielkopf einfügen (T)"); onClicked: app.addTitle() }
                        IconButton { iconName: "piano"; tip: qsTr("Leeren MIDI-Clip (4 Takte) auf der Instrumentspur anlegen"); onClicked: app.addMidiClip() }
                        IconButton {
                            iconName: "layers"
                            tip: qsTr("Spur hinzufügen")
                            onClicked: trackMenu.open()
                            Menu {
                                id: trackMenu
                                y: parent.height
                                MenuItem { text: qsTr("Videospur"); onTriggered: app.addTrack(true) }
                                MenuItem { text: qsTr("Audiospur"); onTriggered: app.addTrack(false) }
                                MenuItem { text: qsTr("Instrumentspur"); onTriggered: app.addInstrumentTrack() }
                            }
                        }
                        Divider { Layout.leftMargin: 4; Layout.rightMargin: 4 }
                        // Beat-Werkzeuge
                        IconButton {
                            iconName: "magnet"
                            checkable: true
                            checked: app.beatGrid
                            onToggled: app.beatGrid = checked
                            tip: qsTr("Takt-Raster: Lineal in Takten, Clips rasten auf Schläge ein")
                        }
                        IconButton {
                            iconName: "sparkles"
                            enabled: app.clip.id !== undefined && app.clip.hasAudio === true && !app.clip.isMidi
                            onClicked: app.detectTempo()
                            tip: qsTr("Tempo aus dem ausgewählten Musik-Clip erkennen")
                        }
                        Button {
                            text: qsTr("Schnitt auf Beat")
                            icon.name: "beat-cut"
                            flat: true
                            enabled: app.selectedClipId !== ""
                            onClicked: beatMenu.open()
                            ToolTip.text: qsTr("Ausgewählten Clip im Takt der Musik zerschneiden (%1 BPM)").arg(Math.round(app.bpm))
                            ToolTip.visible: hovered
                            ToolTip.delay: 600
                            Menu {
                                id: beatMenu
                                y: parent.height
                                MenuItem { text: qsTr("Auf jeden Schlag"); onTriggered: app.cutOnBeats(1) }
                                MenuItem { text: qsTr("Jeden 2. Schlag"); onTriggered: app.cutOnBeats(2) }
                                MenuItem { text: qsTr("Auf jeden Takt"); onTriggered: app.cutOnBeats(4) }
                                MenuItem { text: qsTr("Alle 2 Takte"); onTriggered: app.cutOnBeats(8) }
                            }
                        }
                        Item { Layout.fillWidth: true }
                        Icon { width: 16; height: 16; name: "minus"; color: Theme.dim }
                        Slider {
                            Layout.preferredWidth: 160
                            // Logarithmisch, damit feines und grobes Zoomen gleich gut geht.
                            from: Math.log(2); to: Math.log(2000)
                            value: Math.log(timeline.pixelsPerSecond)
                            onMoved: timeline.pixelsPerSecond = Math.exp(value)
                        }
                        Icon { width: 16; height: 16; name: "plus"; color: Theme.dim }
                        IconButton { iconName: "fit"; tip: qsTr("Ganze Timeline zeigen"); onClicked: timeline.zoomToFit() }
                    }
                }

                Timeline {
                    id: timeline
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    controller: app
                    clip: true
                    onContextMenuRequested: function (x, y, index, header, clipId, seconds) { editMenu.show(x, y, index, header, clipId, seconds) }

                    ScrollBar {
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        anchors.topMargin: 26
                        orientation: Qt.Vertical
                        policy: ScrollBar.AlwaysOn
                        visible: timeline.contentHeight > timeline.height
                        size: timeline.height / timeline.contentHeight
                        position: timeline.scrollY / timeline.contentHeight
                        onPositionChanged: if (pressed) timeline.scrollY = position * timeline.contentHeight
                    }

                    FileDropArea { anchors.fill: parent; timeline: timeline }
                }
                ScrollBar {
                    Layout.fillWidth: true
                    orientation: Qt.Horizontal
                    policy: ScrollBar.AlwaysOn
                    size: timeline.viewportWidth / timeline.contentWidth
                    position: timeline.scrollX / timeline.contentWidth
                    onPositionChanged: if (pressed) timeline.scrollX = position * timeline.contentWidth
                }
            }
        }
    }
}
