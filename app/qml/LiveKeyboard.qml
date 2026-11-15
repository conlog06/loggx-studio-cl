import QtQuick
import QtQuick.Controls
import LoggXStyle

// Klaviatur zum Mitspielen (Maus) – zeigt gehaltene Töne an.
Item {
    id: root
    property int octaves: 3
    readonly property int base: app.keyboardOctave * 12
    readonly property var held: app.heldNotes
    readonly property real whiteWidth: width / (octaves * 7 + 1)
    property string labelsFor: ""   // "computer": Tasten der Computertastatur zeigen

    readonly property var whiteSteps: [0, 2, 4, 5, 7, 9, 11]
    readonly property var blackSteps: [1, 3, -1, 6, 8, 10, -1]
    readonly property var computerKeys: ({ 0: "A", 1: "W", 2: "S", 3: "E", 4: "D", 5: "F", 6: "T", 7: "G", 8: "Z", 9: "H", 10: "U", 11: "J", 12: "K", 13: "O", 14: "L" })

    function isHeld(p) { return held.indexOf(p) >= 0 }
    function noteName(p) { return ["C", "C♯", "D", "D♯", "E", "F", "F♯", "G", "G♯", "A", "A♯", "H"][p % 12] + (Math.floor(p / 12) - 1) }

    // weiße Tasten
    Repeater {
        model: root.octaves * 7 + 1
        Rectangle {
            required property int index
            readonly property int pitch: root.base + Math.floor(index / 7) * 12 + root.whiteSteps[index % 7]
            x: index * root.whiteWidth
            width: root.whiteWidth - 1
            height: root.height
            radius: 3
            color: root.isHeld(pitch) ? Theme.music : whiteMouse.pressed ? "#d8dce4" : "#eef0f4"
            Text {
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 5
                anchors.horizontalCenter: parent.horizontalCenter
                font.pixelSize: 10
                color: "#6a707d"
                text: {
                    const k = root.computerKeys[parent.pitch - root.base]
                    if (root.labelsFor === "computer" && k !== undefined) return k
                    return parent.index % 7 === 0 ? root.noteName(parent.pitch) : ""
                }
            }
            MouseArea {
                id: whiteMouse
                anchors.fill: parent
                onPressed: app.liveNoteOn(parent.pitch, 0.8)
                onReleased: app.liveNoteOff(parent.pitch)
                onCanceled: app.liveNoteOff(parent.pitch)
            }
        }
    }
    // schwarze Tasten
    Repeater {
        model: root.octaves * 7
        Rectangle {
            required property int index
            readonly property int step: root.blackSteps[index % 7]
            readonly property int pitch: root.base + Math.floor(index / 7) * 12 + step
            visible: step >= 0
            x: (index + 1) * root.whiteWidth - width / 2
            width: root.whiteWidth * 0.6
            height: root.height * 0.6
            radius: 3
            color: root.isHeld(pitch) ? Theme.music : blackMouse.pressed ? "#3a3f4c" : "#1c1f26"
            Text {
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 4
                anchors.horizontalCenter: parent.horizontalCenter
                font.pixelSize: 9
                color: "#9aa2b1"
                visible: root.labelsFor === "computer"
                text: root.computerKeys[parent.pitch - root.base] || ""
            }
            MouseArea {
                id: blackMouse
                anchors.fill: parent
                onPressed: app.liveNoteOn(parent.pitch, 0.8)
                onReleased: app.liveNoteOff(parent.pitch)
                onCanceled: app.liveNoteOff(parent.pitch)
            }
        }
    }
}
