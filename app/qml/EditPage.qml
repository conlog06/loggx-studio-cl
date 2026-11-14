import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LoggX 1.0

// Arbeitsbereich "Schnitt": Medienliste, Vorschau, Inspector, Timeline.
Item {
    id: editRoot
    signal importRequested()
    signal openPage(int page)

    SplitView {
        anchors.fill: parent
        orientation: Qt.Vertical
        handle: Rectangle { implicitHeight: 5; color: SplitHandle.hovered || SplitHandle.pressed ? "#4f8cff" : "#101216" }

        // ------------------------------------------------ obere Hälfte
        SplitView {
            SplitView.fillHeight: true
            SplitView.minimumHeight: 260
            orientation: Qt.Horizontal
            handle: Rectangle { implicitWidth: 5; color: SplitHandle.hovered || SplitHandle.pressed ? "#4f8cff" : "#101216" }

            // Medienliste
            Rectangle {
                SplitView.preferredWidth: 270
                SplitView.minimumWidth: 200
                color: "#1b1e24"

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 8
                    RowLayout {
                        Layout.fillWidth: true
                        Label { text: qsTr("Medien"); font.bold: true; Layout.fillWidth: true }
                        Button { text: qsTr("Importieren"); onClicked: editRoot.importRequested() }
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
                                color: mouse.containsMouse ? "#2a2e38" : "#22252d"
                                Rectangle {
                                    width: 3; height: parent.height - 12; radius: 2
                                    x: 6; anchors.verticalCenter: parent.verticalCenter
                                    color: row.modelData.hasVideo ? "#3d6fd6" : "#2c9a6c"
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
                                        color: row.modelData.missing ? "#ff4d5e" : "#8a91a0"
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
                                color: row.modelData.hasVideo ? "#3d6fd6" : "#2c9a6c"
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
                            color: "#8a91a0"
                            text: qsTr("Videos, Musik oder Bilder importieren oder hierher ziehen.\n\nDoppelklick oder Ziehen legt eine Datei in die Timeline.")
                        }
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

            // Vorschau
            Rectangle {
                SplitView.fillWidth: true
                SplitView.minimumWidth: 360
                color: "#0c0d10"
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0
                    Viewer {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.margins: 10
                        controller: app
                    }
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 48
                        color: "#16181d"
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 12
                            anchors.rightMargin: 12
                            spacing: 6
                            Label {
                                text: app.timecode(app.position)
                                font.family: "monospace"
                                font.pixelSize: 15
                                Layout.preferredWidth: 120
                            }
                            Item { Layout.fillWidth: true }
                            Button { text: "⏮"; onClicked: app.seek(0); ToolTip.text: qsTr("Zum Anfang (Pos1)"); ToolTip.visible: hovered; ToolTip.delay: 600 }
                            Button { text: "◀"; onClicked: app.stepFrames(-1); ToolTip.text: qsTr("Ein Bild zurück (←)"); ToolTip.visible: hovered; ToolTip.delay: 600 }
                            Button {
                                text: app.playing ? qsTr("Pause") : qsTr("Abspielen")
                                highlighted: true
                                implicitWidth: 104
                                onClicked: app.togglePlay()
                                ToolTip.text: qsTr("Leertaste"); ToolTip.visible: hovered; ToolTip.delay: 600
                            }
                            Button { text: "▶"; onClicked: app.stepFrames(1); ToolTip.text: qsTr("Ein Bild vor (→)"); ToolTip.visible: hovered; ToolTip.delay: 600 }
                            Button { text: "⏭"; onClicked: app.seek(app.duration); ToolTip.text: qsTr("Zum Ende (Ende)"); ToolTip.visible: hovered; ToolTip.delay: 600 }
                            Item { Layout.fillWidth: true }
                            Label {
                                text: app.timecode(app.duration)
                                font.family: "monospace"
                                font.pixelSize: 15
                                color: "#8a91a0"
                                horizontalAlignment: Text.AlignRight
                                Layout.preferredWidth: 120
                            }
                        }
                    }
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
            color: "#16181d"

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 40
                    color: "#1f2229"
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 10
                        spacing: 6
                        Button { text: qsTr("Teilen"); onClicked: app.splitAtPlayhead(); ToolTip.text: qsTr("Am Abspielkopf teilen (S)"); ToolTip.visible: hovered; ToolTip.delay: 600 }
                        Button { text: qsTr("Löschen"); enabled: app.selectedClipId !== ""; onClicked: app.deleteSelected(false); ToolTip.text: qsTr("Clip löschen (Entf)"); ToolTip.visible: hovered; ToolTip.delay: 600 }
                        Button { text: qsTr("Löschen + Lücke schließen"); enabled: app.selectedClipId !== ""; onClicked: app.deleteSelected(true); ToolTip.text: qsTr("Umschalt+Entf"); ToolTip.visible: hovered; ToolTip.delay: 600 }
                        Rectangle { width: 1; height: 20; color: "#2d313b"; Layout.leftMargin: 4; Layout.rightMargin: 4 }
                        Button { text: qsTr("+ Titel"); onClicked: app.addTitle(); ToolTip.text: qsTr("Titel am Abspielkopf einfügen (T)"); ToolTip.visible: hovered; ToolTip.delay: 600 }
                        Button { text: qsTr("+ MIDI-Clip"); onClicked: app.addMidiClip(); ToolTip.text: qsTr("Leeren MIDI-Clip (4 Takte) auf der Instrumentspur anlegen"); ToolTip.visible: hovered; ToolTip.delay: 600 }
                        Rectangle { width: 1; height: 20; color: "#2d313b"; Layout.leftMargin: 4; Layout.rightMargin: 4 }
                        Button {
                            text: qsTr("+ Spur")
                            onClicked: trackMenu.open()
                            Menu {
                                id: trackMenu
                                y: parent.height
                                MenuItem { text: qsTr("Videospur"); onTriggered: app.addTrack(true) }
                                MenuItem { text: qsTr("Audiospur"); onTriggered: app.addTrack(false) }
                                MenuItem { text: qsTr("Instrumentspur"); onTriggered: app.addInstrumentTrack() }
                            }
                        }
                        Item { Layout.fillWidth: true }
                        Label { text: qsTr("Zoom"); color: "#8a91a0" }
                        Slider {
                            Layout.preferredWidth: 160
                            // Logarithmisch, damit feines und grobes Zoomen gleich gut geht.
                            from: Math.log(2); to: Math.log(2000)
                            value: Math.log(timeline.pixelsPerSecond)
                            onMoved: timeline.pixelsPerSecond = Math.exp(value)
                        }
                        Button { text: qsTr("Alles zeigen"); onClicked: timeline.zoomToFit() }
                    }
                }

                Timeline {
                    id: timeline
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    controller: app
                    clip: true

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

                    DropArea {
                        anchors.fill: parent
                        keys: ["loggx/asset"]
                        onDropped: function (drop) {
                            const index = timeline.trackIndexAt(drop.y)
                            if (index >= 0)
                                app.addClipAt(drop.source.assetId, index, timeline.timeAt(drop.x))
                            else
                                app.addAssetToTimeline(drop.source.assetId)
                        }
                    }
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
