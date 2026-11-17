import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import LoggX 1.0
import LoggXStyle

ApplicationWindow {
    id: window
    width: 1440
    height: 900
    minimumWidth: 1100
    minimumHeight: 680
    visible: true
    color: Theme.bg
    title: window.djMode ? "LXStudio DJ" : (app.dirty ? "● " : "") + app.projectName + " – LXStudio"

    property int page: startPage === 6 ? 3 : startPage
    // DJ ist ein eigener Modus in derselben App (oben umschalten)
    property bool djMode: startPage === 6
    // Während auf der Computertastatur gespielt wird, sind Buchstaben-Kürzel aus.
    readonly property bool playingKeys: studioActive && page === 3 && musicPage.keyboardActive
    // Studio (Video/Musik) sichtbar – Kürzel des Studios nur dann
    readonly property bool studioActive: !app.showStart && !window.djMode

    // Arbeitsbereiche: Videoprojekte zeigen alle, Musikprojekte nur die passenden.
    readonly property var workspaces: app.musicMode
        ? [{ page: 3, text: qsTr("Musik"), icon: "piano" },
           { page: 0, text: qsTr("Samples"), icon: "wave" },
           { page: 4, text: qsTr("Export"), icon: "export" }]
        : [{ page: 0, text: qsTr("Medien"), icon: "folder" },
           { page: 1, text: qsTr("Schnitt"), icon: "scissors" },
           { page: 2, text: qsTr("Farbe"), icon: "palette" },
           { page: 3, text: qsTr("Musik"), icon: "piano" },
           { page: 4, text: qsTr("Export"), icon: "export" }]

    Connections {
        target: app
        function onProjectChanged() {
            // Nach dem Wechsel des Projekttyps einen passenden Bereich zeigen.
            if (app.musicMode && (window.page === 1 || window.page === 2))
                window.page = 3
        }
    }
    Component.onCompleted: {
        if (showLicense) licenseDialog.open()
        if (showSettings) { generalSettings.tab = settingsTab; generalSettings.open() }
    }
    // Aus dem DJ-Bereich: Track im Musik-Editor öffnen
    Connections {
        target: dj
        function onOpenInEditor(path, bpm) {
            window.guarded(function () {
                if (app.openAudioInMusicEditor(path, bpm)) {
                    window.djMode = false
                    window.page = 3
                }
            })
        }
    }

    // Projektdatei wurde ins Fenster gezogen
    function openDropped(path) { guarded(function () { app.openRecent(path) }) }
    // Aktion, die nach "Verwerfen" oder erfolgreichem Speichern ausgeführt wird.
    property var pendingAction: null
    property bool closeConfirmed: false

    function guarded(action) {
        if (!app.dirty) {
            action()
            return
        }
        pendingAction = action
        discardDialog.open()
    }

    function save(then) {
        if (app.filePath === "") {
            saveDialog.then = then === undefined ? null : then
            saveDialog.open()
        } else if (app.saveProject() && then) {
            then()
        }
    }

    onClosing: function (close) {
        if (app.dirty && !closeConfirmed) {
            close.accepted = false
            guarded(function () {
                closeConfirmed = true
                window.close()
            })
        }
    }

    // ---------------------------------------------------- Tastaturkürzel
    Shortcut { sequences: [StandardKey.New]; onActivated: guarded(function () { app.showStart = true }) }
    Shortcut { sequences: [StandardKey.Open]; onActivated: guarded(function () { openDialog.open() }) }
    Shortcut { sequences: [StandardKey.Save]; onActivated: save() }
    Shortcut { sequences: [StandardKey.Undo]; onActivated: app.undo() }
    Shortcut { sequences: [StandardKey.Redo, "Ctrl+Y"]; onActivated: app.redo() }
    Shortcut { sequence: "Ctrl+I"; onActivated: importDialog.open() }
    Shortcut { sequences: [StandardKey.Copy]; enabled: studioActive; onActivated: app.copySelected() }
    Shortcut { sequences: [StandardKey.Paste]; enabled: studioActive && app.canPaste; onActivated: app.pasteAtPlayhead() }
    Shortcut { sequences: [StandardKey.Cut]; enabled: studioActive; onActivated: app.cutSelected() }
    Shortcut { sequence: "Ctrl+D"; enabled: studioActive; onActivated: app.duplicateSelected() }
    Shortcut { sequence: "Ctrl+,"; onActivated: generalSettings.open() }
    Shortcut { sequence: "Ctrl+Shift+,"; enabled: !window.djMode; onActivated: settingsDialog.open() }
    Shortcut { sequence: "Ctrl+Tab"; onActivated: window.djMode = !window.djMode }
    Shortcut { sequence: "Space"; enabled: studioActive; onActivated: app.recording ? app.stopRecording() : app.togglePlay() }
    Shortcut { sequences: ["S", "Ctrl+B"]; enabled: studioActive && !window.playingKeys && !app.musicMode; onActivated: app.splitAtPlayhead() }
    Shortcut { sequence: "Ctrl+B"; enabled: studioActive && app.musicMode; onActivated: app.splitAtPlayhead() }
    Shortcut { sequence: "R"; enabled: studioActive && window.page === 3; onActivated: app.recording ? app.stopRecording() : app.startRecording() }
    Shortcut { sequence: "Ctrl+Home"; onActivated: app.showStart = true }
    Shortcut { sequences: ["Delete", "Backspace"]; enabled: studioActive; onActivated: app.deleteSelected(false) }
    Shortcut { sequences: ["Shift+Delete", "Shift+Backspace"]; enabled: studioActive; onActivated: app.deleteSelected(true) }
    Shortcut { sequence: "Left"; enabled: studioActive; onActivated: app.stepFrames(-1) }
    Shortcut { sequence: "Right"; enabled: studioActive; onActivated: app.stepFrames(1) }
    Shortcut { sequence: "Home"; enabled: studioActive; onActivated: app.seek(0) }
    Shortcut { sequence: "T"; enabled: studioActive && !window.playingKeys && !app.musicMode; onActivated: app.addTitle() }
    Shortcut { sequence: "Ctrl+M"; enabled: studioActive; onActivated: app.addMidiClip() }
    Shortcut { sequence: "End"; enabled: studioActive; onActivated: app.seek(app.duration) }

    // ------------------------------------------------------------ Dialoge
    FileDialog {
        id: openDialog
        title: qsTr("Projekt öffnen")
        nameFilters: [qsTr("LoggX-Projekt (*.loggx)")]
        onAccepted: app.openProject(selectedFile)
    }
    FileDialog {
        id: saveDialog
        property var then: null
        title: qsTr("Projekt speichern")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "loggx"
        nameFilters: [qsTr("LoggX-Projekt (*.loggx)")]
        onAccepted: {
            if (app.saveProjectAs(selectedFile) && then)
                then()
            then = null
        }
        onRejected: then = null
    }
    FileDialog {
        id: importDialog
        title: qsTr("Medien importieren")
        fileMode: FileDialog.OpenFiles
        nameFilters: [
            qsTr("Medien (*.mp4 *.mov *.mkv *.webm *.avi *.m4v *.mp3 *.wav *.flac *.aac *.m4a *.ogg *.opus *.png *.jpg *.jpeg *.webp)"),
            qsTr("Alle Dateien (*)")
        ]
        onAccepted: app.importMedia(selectedFiles)
    }
    Dialog {
        id: discardDialog
        title: qsTr("Ungespeicherte Änderungen")
        modal: true
        anchors.centerIn: parent
        width: 420
        Label {
            width: parent.width
            wrapMode: Text.WordWrap
            text: qsTr("„%1“ hat ungespeicherte Änderungen. Was soll damit passieren?").arg(app.projectName)
        }
        footer: DialogButtonBox {
            Button {
                text: qsTr("Speichern")
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            }
            Button {
                text: qsTr("Verwerfen")
                DialogButtonBox.buttonRole: DialogButtonBox.DestructiveRole
            }
            Button {
                text: qsTr("Abbrechen")
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            }
            onClicked: function (button) {
                if (button.DialogButtonBox.buttonRole === DialogButtonBox.DestructiveRole) {
                    const action = window.pendingAction
                    window.pendingAction = null
                    discardDialog.close()
                    if (action)
                        action()
                }
            }
        }
        onAccepted: {
            const action = window.pendingAction
            window.pendingAction = null
            window.save(action)
        }
        onRejected: window.pendingAction = null
    }

    Dialog {
        id: recoveryDialog
        title: qsTr("Projekt wiederherstellen?")
        modal: true
        anchors.centerIn: parent
        width: 460
        visible: app.recoveryAvailable
        closePolicy: Popup.NoAutoClose
        Label {
            width: parent.width
            wrapMode: Text.WordWrap
            text: app.recoveryInfo + "\n\n" + qsTr("LXStudio wurde nicht ordentlich beendet. Die automatisch gesicherte Fassung wiederherstellen?")
        }
        footer: DialogButtonBox {
            Button { text: qsTr("Wiederherstellen"); DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole }
            Button { text: qsTr("Verwerfen"); DialogButtonBox.buttonRole: DialogButtonBox.RejectRole }
        }
        onAccepted: app.recoverAutosave()
        onRejected: app.discardRecovery()
    }

    LicenseDialog { id: licenseDialog }
    ProjectSettingsDialog { id: settingsDialog }
    SettingsDialog { id: generalSettings }
    Dialog {
        id: updateDialog
        title: qsTr("Update auf LXStudio %1").arg(updater.latestVersion)
        modal: true
        anchors.centerIn: parent
        width: Math.min(parent.width - 80, 560)
        ColumnLayout {
            width: parent.width
            spacing: 10
            Label { text: qsTr("Installiert: %1").arg(updater.currentVersion); color: Theme.dim }
            ScrollView {
                Layout.fillWidth: true
                Layout.preferredHeight: 200
                clip: true
                Label {
                    width: parent.width
                    text: updater.notes === "" ? qsTr("Keine Beschreibung.") : updater.notes
                    textFormat: Text.MarkdownText
                    wrapMode: Text.WordWrap
                }
            }
            ProgressBar { Layout.fillWidth: true; visible: updater.downloading; value: updater.progress }
            Label { Layout.fillWidth: true; text: updater.status; color: Theme.faint; wrapMode: Text.WordWrap }
            Label {
                Layout.fillWidth: true
                visible: !updater.canInstall
                wrapMode: Text.WordWrap
                color: Theme.faint
                text: qsTr("Für dieses System gibt es kein automatisches Paket – die Download-Seite wird geöffnet.")
            }
        }
        footer: RowLayout {
            spacing: 8
            Item { Layout.fillWidth: true }
            Button { text: qsTr("Release-Seite"); flat: true; onClicked: Qt.openUrlExternally(updater.pageUrl) }
            Button { text: updater.downloading ? qsTr("Abbrechen") : qsTr("Später"); onClicked: updater.downloading ? updater.cancel() : updateDialog.close() }
            Button {
                text: qsTr("Jetzt installieren")
                highlighted: true
                enabled: !updater.downloading
                Layout.rightMargin: 12
                Layout.bottomMargin: 12
                // Vorher ungespeicherte Änderungen sichern
                onClicked: window.guarded(function () { updater.install() })
            }
        }
    }

    // ---------------------------------------------------------- Kopfzeile
    header: Rectangle {
        height: 50
        color: Theme.panel
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 12
            anchors.rightMargin: 12
            spacing: 4

            // Logo: zum Startbildschirm
            MouseArea {
                Layout.preferredWidth: logoRow.implicitWidth
                Layout.fillHeight: true
                cursorShape: Qt.PointingHandCursor
                onClicked: app.showStart = true
                Row {
                    id: logoRow
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 9
                    LogoMark { size: 28 }
                    Label {
                        anchors.verticalCenter: parent.verticalCenter
                        text: "LXStudio"
                        font.pixelSize: 15
                        font.bold: true
                    }
                }
            }
            // Modus: Studio (Video/Musik) oder DJ – getrennt, aber eine App
            Rectangle {
                Layout.leftMargin: 12
                Layout.preferredHeight: 30
                Layout.preferredWidth: modeSwitch.implicitWidth + 6
                radius: 15
                color: Theme.bg
                border.color: Theme.border
                Row {
                    id: modeSwitch
                    anchors.centerIn: parent
                    spacing: 2
                    Repeater {
                        model: [{ dj: false, text: qsTr("Studio"), icon: "film" }, { dj: true, text: qsTr("DJ"), icon: "disc" }]
                        Rectangle {
                            required property var modelData
                            readonly property bool active: window.djMode === modelData.dj
                            width: modeText.implicitWidth + 40
                            height: 24
                            radius: 12
                            color: active ? (modelData.dj ? Theme.alpha(Theme.warn, 0.22) : Theme.alpha(Theme.accent, 0.22)) : modeMouse.containsMouse ? Theme.hover : "transparent"
                            Row {
                                anchors.centerIn: parent
                                spacing: 5
                                Icon { width: 13; height: 13; anchors.verticalCenter: parent.verticalCenter; name: modelData.icon; color: active ? (modelData.dj ? Theme.warn : Theme.accent) : Theme.dim }
                                Label { id: modeText; anchors.verticalCenter: parent.verticalCenter; text: modelData.text; font.pixelSize: 12; font.weight: active ? Font.DemiBold : Font.Normal; color: active ? Theme.text : Theme.dim }
                            }
                            MouseArea {
                                id: modeMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    window.djMode = modelData.dj
                                    if (!modelData.dj && app.showStart && app.duration > 0) app.showStart = false
                                }
                            }
                            ToolTip.visible: modeMouse.containsMouse
                            ToolTip.delay: 600
                            ToolTip.text: modelData.dj ? qsTr("DJ: auflegen mit zwei Decks (Strg+Tab)") : qsTr("Studio: Video schneiden und Musik machen (Strg+Tab)")
                        }
                    }
                }
            }
            Divider { Layout.leftMargin: 10; Layout.rightMargin: 6; visible: !window.djMode }
            IconButton { visible: !window.djMode; iconName: "home"; tip: qsTr("Startbildschirm (Strg+Pos1)"); checked: app.showStart; onClicked: app.showStart = !app.showStart }
            IconButton { visible: !window.djMode; iconName: "folder"; tip: qsTr("Projekt öffnen (Strg+O)"); onClicked: guarded(function () { openDialog.open() }) }
            IconButton { visible: !window.djMode; iconName: "save"; tip: qsTr("Speichern (Strg+S)"); enabled: app.dirty || app.filePath === ""; onClicked: save() }
            Divider { Layout.leftMargin: 6; Layout.rightMargin: 6; visible: !window.djMode }
            IconButton { visible: !window.djMode; iconName: "undo"; tip: qsTr("Rückgängig (Strg+Z)"); enabled: app.canUndo; onClicked: app.undo() }
            IconButton { visible: !window.djMode; iconName: "redo"; tip: qsTr("Wiederholen (Strg+Y)"); enabled: app.canRedo; onClicked: app.redo() }

            Item { Layout.fillWidth: true }
            // Projekttyp + Name
            Rectangle {
                visible: window.studioActive
                Layout.preferredHeight: 22
                Layout.preferredWidth: modeLabel.implicitWidth + 30
                radius: 11
                color: Theme.alpha(app.musicMode ? Theme.music : Theme.video, 0.16)
                Row {
                    anchors.centerIn: parent
                    spacing: 5
                    Icon { width: 13; height: 13; anchors.verticalCenter: parent.verticalCenter; name: app.musicMode ? "music" : "film"; color: app.musicMode ? Theme.music : Theme.video }
                    Label { id: modeLabel; text: app.musicMode ? qsTr("Musik") : qsTr("Video"); font.pixelSize: 11; color: app.musicMode ? Theme.music : Theme.video }
                }
            }
            // DJ: Controller-Status in der Mitte
            Row {
                visible: window.djMode
                spacing: 8
                Rectangle { width: 8; height: 8; radius: 4; anchors.verticalCenter: parent.verticalCenter; color: djmidi.connected ? Theme.ok : Theme.faint }
                Label {
                    anchors.verticalCenter: parent.verticalCenter
                    text: djmidi.connected ? djmidi.device : qsTr("Kein Controller – Tastatur & Maus")
                    color: djmidi.connected ? Theme.text : Theme.dim
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: { generalSettings.tab = 3; generalSettings.open() } }
                }
            }
            TextField {
                id: nameField
                visible: window.studioActive
                Layout.preferredWidth: 250
                horizontalAlignment: Text.AlignHCenter
                text: app.projectName + (app.dirty ? " •" : "")
                selectByMouse: true
                background: Rectangle {
                    radius: Theme.radius
                    color: nameField.activeFocus ? Theme.raised : nameField.hovered ? Theme.panel2 : "transparent"
                    border.color: nameField.activeFocus ? Theme.accent : "transparent"
                }
                onActiveFocusChanged: if (activeFocus) text = app.projectName
                onEditingFinished: {
                    app.projectName = text
                    text = Qt.binding(function () { return app.projectName + (app.dirty ? " •" : "") })
                    focus = false
                }
            }
            Item { Layout.fillWidth: true }
            Button {
                visible: window.studioActive
                flat: true
                icon.name: "sliders"
                text: app.musicMode ? qsTr("%1 BPM · %2").arg(Math.round(app.bpm * 10) / 10).arg(app.beatsPerBar >= 6 ? app.beatsPerBar + "/8" : app.beatsPerBar + "/4") : app.formatInfo
                onClicked: settingsDialog.open()
                ToolTip.text: qsTr("Projekteinstellungen: Auflösung, Bildrate, Tempo (Strg+,)")
                ToolTip.visible: hovered
                ToolTip.delay: 500
            }
            // Update verfügbar (GitHub Releases)
            Button {
                visible: updater.available
                text: updater.downloading ? qsTr("Update %1 %").arg(Math.round(updater.progress * 100)) : qsTr("Update %1").arg(updater.latestVersion)
                icon.name: "import"
                highlighted: true
                onClicked: updateDialog.open()
            }
            IconButton { iconName: "settings"; tip: qsTr("Einstellungen (Strg+,)"); onClicked: generalSettings.open() }
            // Edition / Lizenz / Admin
            Button {
                flat: true
                icon.name: app.isAdmin && !app.simulateFree ? "shield" : "key"
                text: app.editionName
                onClicked: licenseDialog.open()
                ToolTip.text: qsTr("Lizenz und Admin-Bereich")
                ToolTip.visible: hovered
                ToolTip.delay: 500
            }
        }
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.border }
    }

    // Dateien von außen: überall dort, wo kein eigener Ablagebereich ist.
    FileDropArea {
        anchors.fill: parent
        enabled: !window.djMode
        hint: app.showStart ? qsTr("Ablegen: importieren und schneiden") : qsTr("Ablegen: am Abspielkopf einfügen")
        onFilesDropped: if (app.showStart && app.duration > 0) app.showStart = false
    }

    // -------------------------------------------------------- Arbeitsbereiche
    StackLayout {
        anchors.fill: parent
        currentIndex: window.djMode ? 6 : app.showStart ? 5 : window.page

        MediaPage { onImportRequested: importDialog.open() }
        EditPage {
            onImportRequested: importDialog.open()
            onOpenPage: function (p) { window.page = p }
        }
        ColorPage {}
        MusicPage { id: musicPage; onImportRequested: importDialog.open() }
        ExportPage {}
        StartPage {
            onOpenRequested: guarded(function () { openDialog.open() })
            onGuard: function (action) { window.guarded(action) }
            onOpenDj: window.djMode = true
        }
        DjPage { active: window.djMode }
    }

    // ------------------------------------------------------------ Fußzeile
    footer: Rectangle {
        height: 46
        color: Theme.panel
        Rectangle { width: parent.width; height: 1; color: Theme.border }
        TabBar {
            id: tabs
            anchors.centerIn: parent
            visible: window.studioActive
            width: window.workspaces.length * 118
            currentIndex: {
                for (let i = 0; i < window.workspaces.length; ++i)
                    if (window.workspaces[i].page === window.page) return i
                return -1
            }
            Repeater {
                model: window.workspaces
                TabButton {
                    required property var modelData
                    text: modelData.text
                    icon.name: modelData.icon
                    onClicked: window.page = modelData.page
                }
            }
        }
        Label {
            anchors.left: parent.left
            anchors.leftMargin: 14
            anchors.right: tabs.visible ? tabs.left : parent.horizontalCenter
            anchors.rightMargin: 14
            anchors.verticalCenter: parent.verticalCenter
            elide: Text.ElideRight
            text: window.djMode ? dj.status : app.status
            color: Theme.dim
        }
        Row {
            anchors.right: parent.right
            anchors.rightMargin: 14
            anchors.verticalCenter: parent.verticalCenter
            spacing: 10
            Row {
                visible: app.working
                spacing: 8
                anchors.verticalCenter: parent.verticalCenter
                Label { text: app.workLabel; color: Theme.text; anchors.verticalCenter: parent.verticalCenter }
                ProgressBar { width: 120; value: app.workProgress; anchors.verticalCenter: parent.verticalCenter }
                IconButton { iconName: "close"; tip: qsTr("Abbrechen"); onClicked: app.cancelWork() }
            }
            Row {
                visible: app.recording
                spacing: 5
                anchors.verticalCenter: parent.verticalCenter
                Rectangle {
                    width: 9; height: 9; radius: 4.5; color: Theme.danger
                    anchors.verticalCenter: parent.verticalCenter
                    SequentialAnimation on opacity { running: app.recording; loops: Animation.Infinite; NumberAnimation { to: 0.25; duration: 500 } NumberAnimation { to: 1; duration: 500 } }
                }
                Label { text: qsTr("Aufnahme"); color: Theme.danger }
            }
            Label {
                visible: app.isAdmin && !app.simulateFree && app.previewMs > 0
                text: qsTr("Vorschau %1 ms").arg(Math.round(app.previewMs))
                color: Theme.faint
            }
            Label { text: "v" + Qt.application.version; color: Theme.faint }
        }
    }
}
