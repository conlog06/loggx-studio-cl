import QtQuick
import QtQuick.Controls
import LoggXStyle

// Dateien aus dem Explorer/Finder hineinziehen. Mit timeline: an die Stelle
// und Spur unter der Maus legen; importOnly: nur in die Medienliste.
DropArea {
    id: area
    property var timeline: null
    property bool importOnly: false
    property string hint: importOnly ? qsTr("Ablegen zum Importieren") : qsTr("Ablegen: in die Timeline legen")
    property bool fileDrag: false
    signal filesDropped()

    onEntered: function (drag) {
        fileDrag = drag.hasUrls
        if (drag.hasUrls) drag.accept(Qt.CopyAction)
    }
    onExited: fileDrag = false
    onDropped: function (drop) {
        fileDrag = false
        // Clip aus der eigenen Medienliste
        if (!drop.hasUrls && area.timeline && drop.source && drop.source.assetId !== undefined) {
            const index = area.timeline.trackIndexAt(drop.y)
            if (index >= 0)
                app.addClipAt(drop.source.assetId, index, area.timeline.timeAt(drop.x))
            else
                app.addAssetToTimeline(drop.source.assetId)
            return
        }
        if (!drop.hasUrls) return
        let track = area.importOnly ? -2 : -1
        let seconds = -1
        if (area.timeline && !area.importOnly) {
            track = area.timeline.trackIndexAt(drop.y)
            seconds = area.timeline.timeAt(drop.x)
        }
        const project = app.dropFiles(drop.urls, track, seconds)
        drop.accept(Qt.CopyAction)
        area.filesDropped()
        if (project !== "" && ApplicationWindow.window && ApplicationWindow.window.openDropped)
            ApplicationWindow.window.openDropped(project)
    }

    Rectangle {
        anchors.fill: parent
        anchors.margins: 4
        visible: area.containsDrag && area.fileDrag
        color: Theme.alpha(Theme.accent, 0.10)
        border.color: Theme.accent
        border.width: 2
        radius: 10
        z: 100
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            y: Math.max(8, Math.min(parent.height - height - 8, area.drag.y - height - 12))
            width: hintLabel.implicitWidth + 28
            height: 30
            radius: 15
            color: Theme.accent
            Label { id: hintLabel; anchors.centerIn: parent; text: area.hint; color: "white"; font.bold: true }
        }
        // Einfügestelle in der Timeline
        Rectangle {
            visible: area.timeline !== null && !area.importOnly
            x: area.drag.x - 1
            width: 2
            height: parent.height
            color: "white"
        }
    }
}
