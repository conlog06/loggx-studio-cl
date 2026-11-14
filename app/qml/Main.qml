import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import LoggX 1.0

ApplicationWindow {
    id: window
    width: 1440
    height: 900
    minimumWidth: 1100
    minimumHeight: 680
    visible: true
    color: "#16181d"
    title: (app.dirty ? "● " : "") + app.projectName + " – LoggX Studio"

    // Dunkles Farbschema für alle Standard-Bedienelemente.
    palette.window: "#16181d"
    palette.windowText: "#d7dbe4"
    palette.base: "#101216"
    palette.alternateBase: "#1b1e24"
    palette.text: "#d7dbe4"
    palette.button: "#2a2e38"
    palette.buttonText: "#d7dbe4"
    palette.brightText: "#ffffff"
    palette.highlight: "#4f8cff"
    palette.highlightedText: "#ffffff"
    palette.toolTipBase: "#2a2e38"
    palette.toolTipText: "#d7dbe4"
    palette.placeholderText: "#8a91a0"
    palette.light: "#3a3f4c"
    palette.midlight: "#323743"
    palette.mid: "#2d313b"
    palette.dark: "#0c0d10"
    palette.shadow: "#000000"
    palette.disabled.buttonText: "#6a707d"
    palette.disabled.windowText: "#6a707d"
    palette.disabled.text: "#6a707d"

    property int page: startPage
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
    Shortcut { sequences: [StandardKey.New]; onActivated: guarded(function () { app.newProject() }) }
    Shortcut { sequences: [StandardKey.Open]; onActivated: guarded(function () { openDialog.open() }) }
    Shortcut { sequences: [StandardKey.Save]; onActivated: save() }
    Shortcut { sequences: [StandardKey.Undo]; onActivated: app.undo() }
    Shortcut { sequences: [StandardKey.Redo, "Ctrl+Y"]; onActivated: app.redo() }
    Shortcut { sequence: "Ctrl+I"; onActivated: importDialog.open() }
    Shortcut { sequence: "Space"; onActivated: app.togglePlay() }
    Shortcut { sequences: ["S", "Ctrl+B"]; onActivated: app.splitAtPlayhead() }
    Shortcut { sequences: ["Delete", "Backspace"]; onActivated: app.deleteSelected(false) }
    Shortcut { sequences: ["Shift+Delete", "Shift+Backspace"]; onActivated: app.deleteSelected(true) }
    Shortcut { sequence: "Left"; onActivated: app.stepFrames(-1) }
    Shortcut { sequence: "Right"; onActivated: app.stepFrames(1) }
    Shortcut { sequence: "Home"; onActivated: app.seek(0) }
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
            qsTr("Medien (*.mp4 *.mov *.mkv *.webm *.avi *.m4v *.mp3 *.wav *.flac *.aac *.m4a *.ogg *.png *.jpg *.jpeg *.webp)"),
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

    // ---------------------------------------------------------- Kopfzeile
    header: Rectangle {
        height: 46
        color: "#1f2229"
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 14
            anchors.rightMargin: 14
            spacing: 8

            Label {
                text: "LoggX Studio"
                font.pixelSize: 15
                font.bold: true
                color: "#4f8cff"
            }
            Rectangle { width: 1; height: 22; color: "#2d313b"; Layout.leftMargin: 6; Layout.rightMargin: 6 }
            Button { text: qsTr("Neu"); onClicked: guarded(function () { app.newProject() }) }
            Button { text: qsTr("Öffnen"); onClicked: guarded(function () { openDialog.open() }) }
            Button { text: qsTr("Speichern"); enabled: app.dirty || app.filePath === ""; onClicked: save() }
            Rectangle { width: 1; height: 22; color: "#2d313b"; Layout.leftMargin: 6; Layout.rightMargin: 6 }
            Button { text: qsTr("Rückgängig"); enabled: app.canUndo; onClicked: app.undo() }
            Button { text: qsTr("Wiederholen"); enabled: app.canRedo; onClicked: app.redo() }

            Item { Layout.fillWidth: true }
            TextField {
                id: nameField
                Layout.preferredWidth: 240
                horizontalAlignment: Text.AlignHCenter
                text: app.projectName
                selectByMouse: true
                onEditingFinished: {
                    app.projectName = text
                    text = Qt.binding(function () { return app.projectName })
                    focus = false
                }
            }
            Item { Layout.fillWidth: true }
            Label { text: app.formatInfo; color: "#8a91a0" }
        }
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: "#2d313b" }
    }

    // -------------------------------------------------------- Arbeitsbereiche
    StackLayout {
        anchors.fill: parent
        currentIndex: window.page

        MediaPage { onImportRequested: importDialog.open() }
        EditPage { onImportRequested: importDialog.open() }
        PlannedPage {
            title: qsTr("Farbe")
            phase: qsTr("Geplant für das MVP")
            items: [
                qsTr("Farbräder für Schatten, Mitten und Lichter"),
                qsTr("Kurven und HSL-Korrektur"),
                qsTr("LUT-Import (.cube)"),
                qsTr("Scopes: Waveform, Vectorscope, Histogramm"),
                qsTr("Masken für lokale Korrekturen"),
                qsTr("HDR (Rec.2020, PQ/HLG) ab Version 1.0")
            ]
        }
        PlannedPage {
            title: qsTr("Musik")
            phase: qsTr("Geplant für das MVP")
            note: qsTr("Schon jetzt im Arbeitsbereich „Schnitt“: Audio schneiden, Lautstärke, Tonhöhe (Pitch) und Tempo pro Clip.")
            items: [
                qsTr("Arrangement-Ansicht auf derselben Zeitachse wie das Video"),
                qsTr("Piano-Roll und MIDI-Keyboard-Eingabe"),
                qsTr("Instrumente: Sampler, Synthesizer, Drum-Machine"),
                qsTr("Mixer mit Fadern, Pan, Sends und Master-Limiter"),
                qsTr("Effekte: EQ, Kompressor, Reverb, Delay"),
                qsTr("Beat-Erkennung und Schnitt auf den Beat"),
                qsTr("VST3-, Audio-Unit- und CLAP-Plugins ab Version 1.0")
            ]
        }
        ExportPage {}
    }

    // ------------------------------------------------------------ Fußzeile
    footer: Rectangle {
        height: 44
        color: "#1f2229"
        Rectangle { width: parent.width; height: 1; color: "#2d313b" }
        TabBar {
            id: tabs
            anchors.centerIn: parent
            width: 560
            currentIndex: window.page
            onCurrentIndexChanged: window.page = currentIndex
            background: Item {}
            Repeater {
                model: [qsTr("Medien"), qsTr("Schnitt"), qsTr("Farbe"), qsTr("Musik"), qsTr("Export")]
                TabButton {
                    required property string modelData
                    text: modelData
                }
            }
        }
        Label {
            anchors.left: parent.left
            anchors.leftMargin: 14
            anchors.right: tabs.left
            anchors.rightMargin: 14
            anchors.verticalCenter: parent.verticalCenter
            elide: Text.ElideRight
            text: app.status
            color: "#8a91a0"
        }
        Label {
            anchors.right: parent.right
            anchors.rightMargin: 14
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("Prototyp %1").arg(Qt.application.version)
            color: "#5d6370"
        }
    }
}
