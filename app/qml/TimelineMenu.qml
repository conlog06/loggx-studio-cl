// Rechtsklick-Menüs der Timeline (Schnitt und Musik): Clip, Spurkopf, leere Fläche
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LoggX 1.0
import LoggXStyle

Item {
    id: root
    signal openPage(int page)
    signal importRequested()

    property int trackIndex: -1
    property real seconds: 0
    readonly property var track: trackIndex >= 0 && trackIndex < app.tracks.length ? app.tracks[trackIndex] : null

    function show(x, y, index, header, clipId, secs) {
        trackIndex = index
        seconds = secs
        if (clipId !== "") clipMenu.popup()
        else if (header && index >= 0) trackMenu.popup()
        else emptyMenu.popup()
    }
    function atHere(action) {
        if (root.seconds >= 0) app.seek(root.seconds)
        action()
    }

    // ------------------------------------------------------------ Clip
    Menu {
        id: clipMenu
        readonly property var c: app.clip
        MenuItem { text: qsTr("Ausschneiden"); hint: "Strg+X"; icon.name: "scissors"; onTriggered: app.cutSelected() }
        MenuItem { text: qsTr("Kopieren"); hint: "Strg+C"; icon.name: "copy"; onTriggered: app.copySelected() }
        MenuItem { text: qsTr("Hier einfügen"); hint: "Strg+V"; enabled: app.canPaste; onTriggered: root.atHere(function () { app.pasteAtPlayhead() }) }
        MenuItem { text: qsTr("Duplizieren"); hint: "Strg+D"; onTriggered: app.duplicateSelected() }
        MenuItem { text: qsTr("Hier teilen"); hint: "S"; onTriggered: root.atHere(function () { app.splitAtPlayhead() }) }
        MenuSeparator {}
        MenuItem {
            text: qsTr("Im Noten-Editor öffnen")
            icon.name: "piano"
            visible: clipMenu.c.isMidi === true
            height: visible ? implicitHeight : 0
            onTriggered: root.openPage(3)
        }
        MenuItem {
            text: qsTr("Tempo und Tonart erkennen")
            visible: clipMenu.c.isMedia === true && clipMenu.c.hasAudio === true
            height: visible ? implicitHeight : 0
            onTriggered: app.analyzeSelected()
        }
        MenuItem {
            text: qsTr("An Projekt-Tempo anpassen (%1 BPM)").arg(Math.round(app.bpm * 100) / 100)
            visible: clipMenu.c.isMedia === true && clipMenu.c.hasAudio === true
            height: visible ? implicitHeight : 0
            onTriggered: app.matchClipTempo()
        }
        Menu {
            title: qsTr("Stimme (Vocal Remover)")
            enabled: clipMenu.c.isMedia === true && clipMenu.c.hasAudio === true
            MenuItem { text: qsTr("Gesang entfernen (Instrumental)"); icon.name: "mic"; onTriggered: app.separateSelected(false) }
            MenuItem { text: qsTr("Nur Gesang behalten (A cappella)"); onTriggered: app.separateSelected(true) }
        }
        MenuSeparator {}
        MenuItem { text: qsTr("Löschen"); hint: "Entf"; danger: true; icon.name: "trash"; onTriggered: app.deleteSelected(false) }
        MenuItem { text: qsTr("Löschen und Lücke schließen"); hint: "Umschalt+Entf"; danger: true; onTriggered: app.deleteSelected(true) }
    }

    // ------------------------------------------------------------ Spur
    Menu {
        id: trackMenu
        MenuItem { text: qsTr("Umbenennen …"); onTriggered: { renameField.text = root.track ? root.track.name : ""; renameDialog.open() } }
        MenuItem {
            text: root.track && root.track.muted ? qsTr("Stummschaltung aufheben") : qsTr("Stummschalten")
            hint: qsTr("Klick")
            onTriggered: app.toggleTrackMute(root.trackIndex)
        }
        MenuItem {
            text: root.track && root.track.solo ? qsTr("Solo aus") : qsTr("Solo")
            enabled: root.track !== null && root.track.kind !== "video"
            onTriggered: app.setTrackProperty(root.trackIndex, "solo", root.track.solo ? 0 : 1)
        }
        Menu {
            id: instrumentMenu
            title: qsTr("Instrument")
            enabled: root.track !== null && root.track.kind === "instrument"
            Repeater {
                model: app.instrumentNames
                MenuItem {
                    required property string modelData
                    required property int index
                    text: modelData
                    checkable: true
                    checked: root.track !== null && root.track.instrument === index
                    onTriggered: app.setTrackProperty(root.trackIndex, "instrument", index)
                }
            }
        }
        MenuSeparator {}
        MenuItem { text: qsTr("Nach oben"); enabled: root.trackIndex > 0; onTriggered: app.moveTrack(root.trackIndex, -1) }
        MenuItem { text: qsTr("Nach unten"); enabled: root.trackIndex < app.tracks.length - 1; onTriggered: app.moveTrack(root.trackIndex, 1) }
        MenuSeparator {}
        MenuItem { text: qsTr("Neue Videospur"); visible: !app.musicMode; height: visible ? implicitHeight : 0; onTriggered: app.addTrack(true) }
        MenuItem { text: qsTr("Neue Audiospur"); onTriggered: app.addTrack(false) }
        MenuItem { text: qsTr("Neue Instrumentspur"); onTriggered: app.addInstrumentTrack() }
        MenuSeparator {}
        MenuItem {
            text: qsTr("Spur löschen")
            danger: true
            icon.name: "trash"
            onTriggered: {
                if (root.track && !root.track.empty) deleteDialog.open()
                else app.deleteTrack(root.trackIndex)
            }
        }
    }

    // ------------------------------------------------------- Leere Fläche
    Menu {
        id: emptyMenu
        MenuItem { text: qsTr("Hier einfügen"); hint: "Strg+V"; enabled: app.canPaste; onTriggered: root.atHere(function () { app.pasteAtPlayhead() }) }
        MenuItem { text: qsTr("Abspielkopf hierher"); enabled: root.seconds >= 0; onTriggered: app.seek(root.seconds) }
        MenuSeparator {}
        MenuItem { text: qsTr("Medien importieren …"); hint: "Strg+I"; icon.name: "folder"; onTriggered: root.importRequested() }
        MenuItem { text: qsTr("Titel hinzufügen"); hint: "T"; visible: !app.musicMode; height: visible ? implicitHeight : 0; onTriggered: root.atHere(function () { app.addTitle() }) }
        MenuItem { text: qsTr("MIDI-Clip hinzufügen"); hint: "Strg+M"; onTriggered: root.atHere(function () { app.addMidiClip() }) }
        MenuSeparator {}
        MenuItem { text: qsTr("Neue Videospur"); visible: !app.musicMode; height: visible ? implicitHeight : 0; onTriggered: app.addTrack(true) }
        MenuItem { text: qsTr("Neue Audiospur"); onTriggered: app.addTrack(false) }
        MenuItem { text: qsTr("Neue Instrumentspur"); onTriggered: app.addInstrumentTrack() }
    }

    Dialog {
        id: renameDialog
        title: qsTr("Spur umbenennen")
        modal: true
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: 360
        standardButtons: Dialog.Ok | Dialog.Cancel
        TextField {
            id: renameField
            width: parent.width
            selectByMouse: true
            onAccepted: renameDialog.accept()
        }
        onOpened: { renameField.forceActiveFocus(); renameField.selectAll() }
        onAccepted: app.renameTrack(root.trackIndex, renameField.text)
    }
    Dialog {
        id: deleteDialog
        title: qsTr("Spur löschen?")
        modal: true
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: 400
        standardButtons: Dialog.Yes | Dialog.Cancel
        Label {
            width: parent.width
            wrapMode: Text.WordWrap
            text: qsTr("„%1“ enthält Clips. Spur mit allem Inhalt löschen? (Strg+Z macht es rückgängig)").arg(root.track ? root.track.name : "")
        }
        onAccepted: app.deleteTrack(root.trackIndex)
    }
}
