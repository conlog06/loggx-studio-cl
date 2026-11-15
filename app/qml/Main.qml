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
    title: (app.dirty ? "● " : "") + app.projectName + " – LoggX Studio"

    property int page: startPage
    // Während auf der Computertastatur gespielt wird, sind Buchstaben-Kürzel aus.
    readonly property bool playingKeys: !app.showStart && page === 3 && musicPage.keyboardActive

    // Arbeitsbereiche: Videoprojekte zeigen alle, Musikprojekte nur die passenden.
    readonly property var workspaces: app.musicMode
        ? [{ page: 3, text: qsTr("Musik"), icon: "piano" },
           { page: 0, text: qsTr("Samples"), icon: "wave" },
           { page: 4, text: qsTr("Export"), icon: "export" },
           { page: 6, text: qsTr("DJ"), icon: "disc" }]
        : [{ page: 0, text: qsTr("Medien"), icon: "folder" },
           { page: 1, text: qsTr("Schnitt"), icon: "scissors" },
           { page: 2, text: qsTr("Farbe"), icon: "palette" },
           { page: 3, text: qsTr("Musik"), icon: "piano" },
           { page: 4, text: qsTr("Export"), icon: "export" },
           { page: 6, text: qsTr("DJ"), icon: "disc" }]

    Connections {
        target: app
        function onProjectChanged() {
            // Nach dem Wechsel des Projekttyps einen passenden Bereich zeigen.
            if (app.musicMode && (window.page === 1 || window.page === 2))
                window.page = 3
        }
    }
    Component.onCompleted: if (showLicense) licenseDialog.open()

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
    Shortcut { sequences: [StandardKey.Copy]; enabled: !app.showStart; onActivated: app.copySelected() }
    Shortcut { sequences: [StandardKey.Paste]; enabled: !app.showStart && app.canPaste; onActivated: app.pasteAtPlayhead() }
    Shortcut { sequence: "Ctrl+D"; enabled: !app.showStart; onActivated: app.duplicateSelected() }
    Shortcut { sequence: "Ctrl+,"; onActivated: settingsDialog.open() }
    Shortcut { sequence: "Space"; enabled: !app.showStart && window.page !== 6; onActivated: app.recording ? app.stopRecording() : app.togglePlay() }
    Shortcut { sequences: ["S", "Ctrl+B"]; enabled: !window.playingKeys && !app.musicMode; onActivated: app.splitAtPlayhead() }
    Shortcut { sequence: "Ctrl+B"; enabled: app.musicMode; onActivated: app.splitAtPlayhead() }
    Shortcut { sequence: "R"; enabled: !app.showStart && window.page === 3; onActivated: app.recording ? app.stopRecording() : app.startRecording() }
    Shortcut { sequence: "Ctrl+Home"; onActivated: app.showStart = true }
    Shortcut { sequences: ["Delete", "Backspace"]; enabled: !app.showStart; onActivated: app.deleteSelected(false) }
    Shortcut { sequences: ["Shift+Delete", "Shift+Backspace"]; onActivated: app.deleteSelected(true) }
    Shortcut { sequence: "Left"; onActivated: app.stepFrames(-1) }
    Shortcut { sequence: "Right"; onActivated: app.stepFrames(1) }
    Shortcut { sequence: "Home"; onActivated: app.seek(0) }
    Shortcut { sequence: "T"; enabled: !window.playingKeys && !app.musicMode; onActivated: app.addTitle() }
    Shortcut { sequence: "Ctrl+M"; onActivated: app.addMidiClip() }
    Shortcut { sequence: "End"; onActivated: app.seek(app.duration) }

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
            text: app.recoveryInfo + "\n\n" + qsTr("LoggX Studio wurde nicht ordentlich beendet. Die automatisch gesicherte Fassung wiederherstellen?")
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
                    Rectangle {
                        width: 28; height: 28; radius: 8
                        gradient: Gradient {
                            orientation: Gradient.Horizontal
                            GradientStop { position: 0; color: Theme.accent }
                            GradientStop { position: 1; color: Theme.accent2 }
                        }
                        Text { anchors.centerIn: parent; text: "LX"; color: "white"; font.pixelSize: 12; font.bold: true }
                    }
                    Label {
                        anchors.verticalCenter: parent.verticalCenter
                        text: "LoggX Studio"
                        font.pixelSize: 15
                        font.bold: true
                    }
                }
            }
            Divider { Layout.leftMargin: 10; Layout.rightMargin: 6 }
            IconButton { iconName: "home"; tip: qsTr("Startbildschirm (Strg+Pos1)"); checked: app.showStart; onClicked: app.showStart = !app.showStart }
            IconButton { iconName: "folder"; tip: qsTr("Projekt öffnen (Strg+O)"); onClicked: guarded(function () { openDialog.open() }) }
            IconButton { iconName: "save"; tip: qsTr("Speichern (Strg+S)"); enabled: app.dirty || app.filePath === ""; onClicked: save() }
            Divider { Layout.leftMargin: 6; Layout.rightMargin: 6 }
            IconButton { iconName: "undo"; tip: qsTr("Rückgängig (Strg+Z)"); enabled: app.canUndo; onClicked: app.undo() }
            IconButton { iconName: "redo"; tip: qsTr("Wiederholen (Strg+Y)"); enabled: app.canRedo; onClicked: app.redo() }

            Item { Layout.fillWidth: true }
            // Projekttyp + Name
            Rectangle {
                visible: !app.showStart
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
            TextField {
                id: nameField
                visible: !app.showStart
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
                visible: !app.showStart
                flat: true
                icon.name: "settings"
                text: app.musicMode ? qsTr("%1 BPM · %2").arg(Math.round(app.bpm * 10) / 10).arg(app.beatsPerBar >= 6 ? app.beatsPerBar + "/8" : app.beatsPerBar + "/4") : app.formatInfo
                onClicked: settingsDialog.open()
                ToolTip.text: qsTr("Projekteinstellungen: Auflösung, Bildrate, Tempo (Strg+,)")
                ToolTip.visible: hovered
                ToolTip.delay: 500
            }
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
        hint: app.showStart ? qsTr("Ablegen: importieren und schneiden") : qsTr("Ablegen: am Abspielkopf einfügen")
        onFilesDropped: if (app.showStart && app.duration > 0) app.showStart = false
    }

    // -------------------------------------------------------- Arbeitsbereiche
    StackLayout {
        anchors.fill: parent
        currentIndex: app.showStart ? 5 : window.page

        MediaPage { onImportRequested: importDialog.open() }
        EditPage {
            onImportRequested: importDialog.open()
            onOpenPage: function (p) { window.page = p }
        }
        ColorPage {}
        MusicPage { id: musicPage }
        ExportPage {}
        StartPage {
            onOpenRequested: guarded(function () { openDialog.open() })
            onGuard: function (action) { window.guarded(action) }
            onOpenDj: { app.showStart = false; window.page = 6 }
        }
        DjPage { active: !app.showStart && window.page === 6 }
    }

    // ------------------------------------------------------------ Fußzeile
    footer: Rectangle {
        height: 46
        color: Theme.panel
        Rectangle { width: parent.width; height: 1; color: Theme.border }
        TabBar {
            id: tabs
            anchors.centerIn: parent
            visible: !app.showStart
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
            text: app.status
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
