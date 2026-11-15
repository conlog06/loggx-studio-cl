import QtQuick
import QtQuick.Controls
import LoggXStyle

// Drehregler: senkrecht ziehen ändert den Wert, Doppelklick setzt zurück.
Item {
    id: knob
    property real value: 0
    property real from: -1
    property real to: 1
    property real resetValue: 0
    property string label
    property color tint: Theme.accent
    property bool bipolar: true   // Bogen von der Mitte aus
    signal moved(real value)
    implicitWidth: 40
    implicitHeight: 48

    readonly property real norm: (value - from) / (to - from)

    Canvas {
        id: canvas
        width: 34
        height: 34
        anchors.horizontalCenter: parent.horizontalCenter
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            const cx = width / 2, cy = height / 2, r = 13
            const a0 = Math.PI * 0.75, a1 = Math.PI * 2.25
            ctx.lineWidth = 3
            ctx.strokeStyle = "#2a303a"
            ctx.beginPath(); ctx.arc(cx, cy, r, a0, a1); ctx.stroke()
            const av = a0 + (a1 - a0) * knob.norm
            const am = knob.bipolar ? (a0 + a1) / 2 : a0
            ctx.strokeStyle = knob.tint
            ctx.beginPath(); ctx.arc(cx, cy, r, Math.min(am, av), Math.max(am, av)); ctx.stroke()
            ctx.fillStyle = "#1c1f26"
            ctx.beginPath(); ctx.arc(cx, cy, r - 4, 0, Math.PI * 2); ctx.fill()
            ctx.strokeStyle = "#e8ebf1"
            ctx.lineWidth = 2
            ctx.beginPath()
            ctx.moveTo(cx + Math.cos(av) * 4, cy + Math.sin(av) * 4)
            ctx.lineTo(cx + Math.cos(av) * (r - 5), cy + Math.sin(av) * (r - 5))
            ctx.stroke()
        }
        Connections { target: knob; function onValueChanged() { canvas.requestPaint() } function onTintChanged() { canvas.requestPaint() } }
    }
    Label {
        anchors.top: canvas.bottom
        anchors.topMargin: 1
        anchors.horizontalCenter: parent.horizontalCenter
        text: knob.label
        font.pixelSize: 9
        color: Theme.dim
    }
    MouseArea {
        anchors.fill: parent
        property real startY
        property real startValue
        cursorShape: Qt.SizeVerCursor
        onPressed: function (m) { startY = m.y; startValue = knob.value }
        onPositionChanged: function (m) {
            if (!pressed) return
            const v = Math.max(knob.from, Math.min(knob.to, startValue + (startY - m.y) / 120 * (knob.to - knob.from)))
            knob.value = v
            knob.moved(v)
        }
        onDoubleClicked: { knob.value = knob.resetValue; knob.moved(knob.resetValue) }
        onWheel: function (w) {
            const v = Math.max(knob.from, Math.min(knob.to, knob.value + (w.angleDelta.y > 0 ? 1 : -1) * (knob.to - knob.from) / 40))
            knob.value = v
            knob.moved(v)
        }
    }
}
