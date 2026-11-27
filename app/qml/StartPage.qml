import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import LoggX 1.0
import LoggXStyle

// Startbildschirm: neues Video- oder Musikprojekt, öffnen, zuletzt benutzt.
Rectangle {
    id: root
    signal openRequested()
    signal guard(var action)
    signal openDj()
    gradient: Gradient {
        GradientStop { position: 0; color: "#141824" }
        GradientStop { position: 0.55; color: Theme.bg }
    }

    readonly property var videoTemplates: [
        { text: qsTr("Full HD"), sub: "1920×1080 · 30", w: 1920, h: 1080, fps: 30 },
        { text: qsTr("4K UHD"), sub: "3840×2160 · 30", w: 3840, h: 2160, fps: 30 },
        { text: qsTr("Hochkant 9:16"), sub: "1080×1920 · 30", w: 1080, h: 1920, fps: 30 },
        { text: qsTr("Quadrat"), sub: "1080×1080 · 30", w: 1080, h: 1080, fps: 30 },
        { text: qsTr("Kino"), sub: "1920×1080 · 24", w: 1920, h: 1080, fps: 24 },
        { text: qsTr("Flüssig"), sub: "1920×1080 · 60", w: 1920, h: 1080, fps: 60 }
    ]
    readonly property var musicTemplates: [
        { text: qsTr("Beat"), sub: "120 BPM", bpm: 120 },
        { text: qsTr("Dance"), sub: "128 BPM", bpm: 128 },
        { text: qsTr("Hip-Hop"), sub: "90 BPM", bpm: 90 },
        { text: qsTr("Ballade"), sub: "72 BPM", bpm: 72 }
    ]

    // Bildrate für neue Videoprojekte (0 = wie Vorlage)
    property int rateChoice: 0
    readonly property var rates: [
        { text: qsTr("fps wie Vorlage"), fps: 0 },
        { text: "23,976 fps", fps: 23.976 }, { text: "24 fps", fps: 24 }, { text: "25 fps", fps: 25 },
        { text: "29,97 fps", fps: 29.97 }, { text: "30 fps", fps: 30 }, { text: "50 fps", fps: 50 },
        { text: "59,94 fps", fps: 59.94 }, { text: "60 fps", fps: 60 }, { text: "120 fps", fps: 120 }
    ]
    function newVideo(t) {
        const fps = rates[rateChoice].fps > 0 ? rates[rateChoice].fps : t.fps
        root.guard(function () { app.newFromTemplate(false, t.w, t.h, fps, 120) })
    }
    function newMusic(t) { root.guard(function () { app.newFromTemplate(true, 1920, 1080, 30, t.bpm) }) }

    // Karte für einen Projekttyp mit Vorlagen
    component ProjectCard: Rectangle {
        id: card
        property string title
        property string subtitle
        property string iconName
        property color tint
        property var templates: []
        property bool showRate: false
        signal create(var template)
        Layout.fillWidth: true
        implicitHeight: cardCol.implicitHeight + 36
        radius: 14
        color: Theme.panel
        border.color: cardHover.hovered ? Theme.alpha(card.tint, 0.6) : Theme.border
        HoverHandler { id: cardHover }

        ColumnLayout {
            id: cardCol
            anchors.fill: parent
            anchors.margins: 18
            spacing: 14
            RowLayout {
                spacing: 14
                Rectangle {
                    width: 46; height: 46; radius: 12
                    color: Theme.alpha(card.tint, 0.16)
                    Icon { anchors.centerIn: parent; width: 24; height: 24; name: card.iconName; color: card.tint }
                }
                ColumnLayout {
                    spacing: 2
                    Layout.fillWidth: true
                    Label { text: card.title; font.pixelSize: 18; font.bold: true }
                    Label { text: card.subtitle; color: Theme.dim; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                }
                ComboBox {
                    visible: card.showRate
                    Layout.preferredWidth: 150
                    textRole: "text"
                    model: root.rates
                    currentIndex: root.rateChoice
                    onActivated: function (i) { root.rateChoice = i }
                }
                Button {
                    text: qsTr("Erstellen")
                    highlighted: true
                    icon.name: "plus"
                    onClicked: card.create(card.templates[0])
                }
            }
            GridLayout {
                Layout.fillWidth: true
                columns: card.templates.length === 4 ? 4 : 3
                columnSpacing: 8
                rowSpacing: 8
                Repeater {
                    model: card.templates
                    Rectangle {
                        required property var modelData
                        Layout.fillWidth: true
                        implicitHeight: 52
                        radius: 9
                        color: chipMouse.pressed ? Theme.pressed : chipMouse.containsMouse ? Theme.hover : Theme.panel2
                        border.color: chipMouse.containsMouse ? Theme.alpha(card.tint, 0.7) : Theme.border
                        Column {
                            anchors.verticalCenter: parent.verticalCenter
                            x: 12
                            spacing: 2
                            Label { text: modelData.text; font.bold: true }
                            Label {
                                // Bei gewählter Bildrate die Vorlage mit dieser Rate anzeigen
                                text: card.showRate && root.rateChoice > 0 ? modelData.sub.replace(/· [0-9]+$/, "· " + root.rates[root.rateChoice].text.replace(" fps", "")) : modelData.sub
                                color: Theme.dim
                                font.pixelSize: Theme.smallFont
                            }
                        }
                        MouseArea {
                            id: chipMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: card.create(parent.modelData)
                        }
                    }
                }
            }
        }
    }

    ScrollView {
        id: startScroll
        anchors.fill: parent
        contentWidth: availableWidth
        contentHeight: content.implicitHeight + 80
        clip: true
        // Entwicklung/Bildschirmfotos: --scroll-end zeigt das Seitenende
        Timer {
            interval: 3000
            running: Qt.application.arguments.indexOf("--scroll-end") >= 0
            onTriggered: startScroll.ScrollBar.vertical.position = 1.0 - startScroll.ScrollBar.vertical.size
        }

        RowLayout {
            id: content
            width: Math.min(root.width - 80, 1240)
            x: (root.width - width) / 2
            y: 40
            spacing: 28

            // ------------------------------------------------ links: neu
            ColumnLayout {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                spacing: 18

                ColumnLayout {
                    spacing: 6
                    Label {
                        text: qsTr("Willkommen bei LXStudio")
                        font.pixelSize: 30
                        font.bold: true
                    }
                    Label {
                        text: qsTr("Video schneiden, Musik machen – oder beides in einem Projekt.")
                        color: Theme.dim
                        font.pixelSize: 15
                    }
                }

                ProjectCard {
                    title: qsTr("Videoprojekt")
                    subtitle: qsTr("Schnitt, Masken, Farbe, Titel, Übergänge und Musik – Export bis 8K.")
                    iconName: "film"
                    tint: Theme.video
                    templates: root.videoTemplates
                    showRate: true
                    onCreate: function (t) { root.newVideo(t) }
                }
                ProjectCard {
                    title: qsTr("Musikprojekt")
                    subtitle: qsTr("Nur Musik: Instrumente, Piano-Roll, Aufnahme per Tastatur oder MIDI-Keyboard, Export als WAV/MP3/FLAC.")
                    iconName: "piano"
                    tint: Theme.music
                    templates: root.musicTemplates
                    onCreate: function (t) { root.newMusic(t) }
                }

                // DJ-Modus
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: 76
                    radius: 14
                    color: djHover.hovered ? Theme.hover : Theme.panel
                    border.color: djHover.hovered ? Theme.alpha("#ff8a3d", 0.6) : Theme.border
                    HoverHandler { id: djHover }
                    TapHandler { onTapped: root.openDj() }
                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 16
                        spacing: 14
                        Rectangle {
                            width: 46; height: 46; radius: 12
                            color: Theme.alpha("#ff8a3d", 0.16)
                            Icon { anchors.centerIn: parent; width: 24; height: 24; name: "disc"; color: "#ff8a3d" }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            Label { text: qsTr("DJ"); font.pixelSize: 18; font.bold: true }
                            Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.dim; text: qsTr("Zwei Decks, Sync, Hot Cues, Loops, EQ, Effekte, KI-Stems, Pioneer DDJ-FLX4 – Mix live aufnehmen.") }
                        }
                        Button { text: qsTr("Öffnen"); icon.name: "chevron-right"; onClicked: root.openDj() }
                    }
                }

                RowLayout {
                    spacing: 8
                    Button { text: qsTr("Projekt öffnen …"); icon.name: "folder"; onClicked: root.openRequested() }
                    Button {
                        flat: true
                        icon.name: "chevron-right"
                        text: qsTr("Weiter mit „%1“").arg(app.projectName)
                        onClicked: app.showStart = false
                    }
                }

                // ---------------------------------------- eigene Vorlagen
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    RowLayout {
                        Layout.fillWidth: true
                        Icon { width: 16; height: 16; name: "layers"; color: Theme.dim }
                        Label { text: qsTr("Eigene Vorlagen"); font.bold: true; font.pixelSize: 16; Layout.fillWidth: true }
                        Button { text: qsTr("Paket importieren …"); flat: true; icon.name: "import"; onClicked: packDialog.open() }
                    }
                    Label {
                        visible: app.templates.length === 0
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: Theme.faint
                        text: qsTr("Speichere ein Projekt über das Menü „Als Vorlage speichern …“ (mit Medien, Effekten und Titeln) oder importiere ein .lxpack-Paket von anderen.")
                    }
                    Flow {
                        Layout.fillWidth: true
                        spacing: 10
                        Repeater {
                            model: app.templates
                            Rectangle {
                                id: tcard
                                required property var modelData
                                width: 180
                                height: 150
                                radius: 10
                                color: tMouse.containsMouse ? Theme.hover : Theme.panel
                                border.color: Theme.border
                                ColumnLayout {
                                    anchors.fill: parent
                                    anchors.margins: 8
                                    spacing: 4
                                    Rectangle {
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 92
                                        radius: 6
                                        color: "#0a0b0e"
                                        clip: true
                                        Image {
                                            anchors.fill: parent
                                            source: tcard.modelData.preview
                                            fillMode: Image.PreserveAspectFit
                                            asynchronous: true
                                        }
                                        Icon { anchors.centerIn: parent; width: 24; height: 24; visible: !tcard.modelData.preview; name: tcard.modelData.music ? "music" : "film"; color: Theme.faint }
                                    }
                                    Label { text: tcard.modelData.name; font.bold: true; elide: Text.ElideRight; Layout.fillWidth: true }
                                    Label { text: tcard.modelData.format + (tcard.modelData.author ? " · " + tcard.modelData.author : ""); color: Theme.dim; font.pixelSize: 11; elide: Text.ElideRight; Layout.fillWidth: true }
                                }
                                MouseArea {
                                    id: tMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                                    onClicked: function (m) {
                                        if (m.button === Qt.RightButton) tMenu.popup()
                                        else app.newFromUserTemplate(tcard.modelData.dir)
                                    }
                                }
                                Menu {
                                    id: tMenu
                                    MenuItem { text: qsTr("Neues Projekt daraus"); onTriggered: app.newFromUserTemplate(tcard.modelData.dir) }
                                    MenuItem { text: qsTr("Als Paket teilen (.lxpack) …"); onTriggered: { shareDialog.dir = tcard.modelData.dir; shareDialog.currentFile = ""; shareDialog.open() } }
                                    MenuItem { text: qsTr("Im Marktplatz einreichen …"); enabled: !tcard.modelData.marketId; onTriggered: app.submitTemplate(tcard.modelData.dir, true) }
                                    MenuSeparator {}
                                    MenuItem { text: qsTr("Löschen"); onTriggered: app.deleteTemplate(tcard.modelData.dir) }
                                }
                                ToolTip.text: tcard.modelData.description || qsTr("Klick: neues Projekt · Rechtsklick: teilen/löschen")
                                ToolTip.visible: tMouse.containsMouse
                                ToolTip.delay: 600
                            }
                        }
                    }
                }

                // ---------------------------------------- Marktplatz
                ColumnLayout {
                    id: market
                    Layout.fillWidth: true
                    spacing: 8
                    property string query: ""
                    Component.onCompleted: if (app.marketState === 0) app.refreshMarket()
                    RowLayout {
                        Layout.fillWidth: true
                        Icon { width: 16; height: 16; name: "import"; color: Theme.dim }
                        Label { text: qsTr("Marktplatz"); font.bold: true; font.pixelSize: 16 }
                        Label { text: qsTr("Vorlagen von der Community"); color: Theme.faint; Layout.fillWidth: true }
                        TextField {
                            Layout.preferredWidth: 180
                            placeholderText: qsTr("Suchen …")
                            onTextChanged: market.query = text.toLowerCase()
                        }
                        Button { flat: true; icon.name: "loop"; enabled: app.marketState !== 1; onClicked: app.refreshMarket(); ToolTip.visible: hovered; ToolTip.text: qsTr("Neu laden") }
                    }
                    Label {
                        visible: app.marketState !== 2 || app.marketItems.length === 0
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: Theme.faint
                        text: app.marketState === 1 ? qsTr("Lädt …")
                            : app.marketState === 3 ? qsTr("Marktplatz nicht erreichbar (%1). Internetverbindung prüfen und neu laden.").arg(app.marketError)
                            : qsTr("Noch keine Vorlagen im Marktplatz.")
                    }
                    Flow {
                        Layout.fillWidth: true
                        spacing: 10
                        Repeater {
                            model: app.marketItems.filter(function (it) {
                                if (!market.query) return true
                                return (it.name + " " + it.author + " " + it.description + " " + it.format).toLowerCase().indexOf(market.query) >= 0
                            })
                            Rectangle {
                                id: mcard
                                required property var modelData
                                width: 180
                                height: 196
                                radius: 10
                                color: mMouse.containsMouse ? Theme.hover : Theme.panel
                                border.color: Theme.border
                                ColumnLayout {
                                    anchors.fill: parent
                                    anchors.margins: 8
                                    spacing: 4
                                    Rectangle {
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 92
                                        radius: 6
                                        color: "#0a0b0e"
                                        clip: true
                                        Image {
                                            anchors.fill: parent
                                            source: mcard.modelData.previewUrl
                                            fillMode: Image.PreserveAspectFit
                                            asynchronous: true
                                        }
                                    }
                                    Label { text: mcard.modelData.name; font.bold: true; elide: Text.ElideRight; Layout.fillWidth: true }
                                    Label {
                                        text: mcard.modelData.format + (mcard.modelData.author ? " · " + mcard.modelData.author : "") + " · " + (mcard.modelData.bytes < 1048576 ? Math.max(1, Math.round(mcard.modelData.bytes / 1024)) + " KB" : Math.round(mcard.modelData.bytes / 1048576 * 10) / 10 + " MB")
                                        color: Theme.dim; font.pixelSize: 11; elide: Text.ElideRight; Layout.fillWidth: true
                                    }
                                    Button {
                                        Layout.fillWidth: true
                                        text: mcard.modelData.installed ? qsTr("Installiert ✓") : app.marketBusy === mcard.modelData.id ? qsTr("Lädt …") : qsTr("Installieren")
                                        enabled: !mcard.modelData.installed && app.marketBusy === ""
                                        highlighted: !mcard.modelData.installed
                                        onClicked: app.installMarketItem(mcard.modelData.id)
                                    }
                                }
                                MouseArea {
                                    id: mMouse
                                    anchors.fill: parent
                                    anchors.bottomMargin: 40
                                    hoverEnabled: true
                                    acceptedButtons: Qt.NoButton
                                }
                                ToolTip.text: mcard.modelData.description
                                ToolTip.visible: mMouse.containsMouse && mcard.modelData.description !== ""
                                ToolTip.delay: 500
                            }
                        }
                    }
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: Theme.faint
                        font.pixelSize: 11
                        text: qsTr("Eigene Vorlage teilen: Rechtsklick auf die Vorlage → „Im Marktplatz einreichen“. Nach Prüfung erscheint sie hier für alle.")
                    }
                }
            }

            // ------------------------------------------- rechts: zuletzt
            Rectangle {
                Layout.preferredWidth: 380
                Layout.alignment: Qt.AlignTop
                Layout.topMargin: 84
                implicitHeight: recentCol.implicitHeight + 32
                radius: 14
                color: Theme.panel
                border.color: Theme.border

                ColumnLayout {
                    id: recentCol
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 6
                    RowLayout {
                        Icon { width: 16; height: 16; name: "clock"; color: Theme.dim }
                        Label { text: qsTr("Zuletzt geöffnet"); font.bold: true; Layout.fillWidth: true }
                    }
                    Label {
                        visible: app.recentProjects.length === 0
                        Layout.fillWidth: true
                        Layout.topMargin: 8
                        Layout.bottomMargin: 8
                        wrapMode: Text.WordWrap
                        color: Theme.dim
                        text: qsTr("Noch keine Projekte. Gespeicherte Projekte erscheinen hier.")
                    }
                    Repeater {
                        model: app.recentProjects
                        Rectangle {
                            id: recentRow
                            required property var modelData
                            Layout.fillWidth: true
                            implicitHeight: 54
                            radius: 9
                            color: rowMouse.containsMouse ? Theme.hover : "transparent"
                            opacity: modelData.exists ? 1 : 0.55
                            MouseArea {
                                id: rowMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                enabled: recentRow.modelData.exists
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    const path = recentRow.modelData.path
                                    root.guard(function () { app.openRecent(path) })
                                }
                            }
                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 10
                                anchors.rightMargin: 6
                                spacing: 10
                                Rectangle {
                                    width: 32; height: 32; radius: 8
                                    color: Theme.panel2
                                    Icon { anchors.centerIn: parent; width: 16; height: 16; name: "file"; color: Theme.accent }
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 1
                                    Label { Layout.fillWidth: true; text: recentRow.modelData.name; elide: Text.ElideRight; font.bold: true }
                                    Label {
                                        Layout.fillWidth: true
                                        elide: Text.ElideMiddle
                                        font.pixelSize: Theme.smallFont
                                        color: recentRow.modelData.exists ? Theme.dim : Theme.danger
                                        text: recentRow.modelData.exists ? recentRow.modelData.date + " · " + recentRow.modelData.folder : qsTr("Datei nicht gefunden")
                                    }
                                }
                                IconButton {
                                    visible: rowMouse.containsMouse || hovered || !recentRow.modelData.exists
                                    iconName: "close"
                                    tip: qsTr("Aus der Liste entfernen")
                                    onClicked: app.forgetRecent(recentRow.modelData.path)
                                }
                            }
                        }
                    }

                    Rectangle { Layout.fillWidth: true; Layout.topMargin: 10; height: 1; color: Theme.border }
                    Label { Layout.topMargin: 6; text: qsTr("Tastenkürzel"); color: Theme.dim; font.pixelSize: Theme.smallFont; font.bold: true }
                    GridLayout {
                        columns: 2
                        columnSpacing: 14
                        rowSpacing: 3
                        Repeater {
                            model: [
                                "Leertaste", qsTr("Abspielen / Pause"),
                                "S", qsTr("Am Abspielkopf teilen"),
                                "Strg+C / V / D", qsTr("Clip kopieren / einfügen / duplizieren"),
                                "R", qsTr("Aufnahme (Musik)"),
                                "Strg+Z / Y", qsTr("Rückgängig / Wiederholen"),
                                "Strg+Pos1", qsTr("Startbildschirm")
                            ]
                            Label {
                                required property string modelData
                                required property int index
                                text: modelData
                                font.pixelSize: Theme.smallFont
                                color: index % 2 === 0 ? Theme.text : Theme.dim
                                font.family: index % 2 === 0 ? "monospace" : Qt.application.font.family
                            }
                        }
                    }
                }
            }
        }
    }

    FileDialog {
        id: packDialog
        title: qsTr("Vorlagenpaket importieren")
        nameFilters: [qsTr("LXStudio-Vorlage (*.lxpack)")]
        onAccepted: app.importTemplatePack(selectedFile)
    }
    FileDialog {
        id: shareDialog
        property string dir
        title: qsTr("Vorlage als Paket speichern")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "lxpack"
        nameFilters: [qsTr("LXStudio-Vorlage (*.lxpack)")]
        onAccepted: app.exportTemplate(dir, selectedFile)
    }
}
