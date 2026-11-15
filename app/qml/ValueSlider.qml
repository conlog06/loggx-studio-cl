import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LoggXStyle

// Regler für eine Eigenschaft des ausgewählten Clips. Ziehen ergibt einen
// einzigen Undo-Schritt; Doppelklick auf die Beschriftung setzt zurück.
// Mit keyable: true erscheint ein Keyframe-Knopf (◆).
RowLayout {
    id: root
    property string label
    property string prop
    property real from: 0
    property real to: 1
    property real step: 0.01
    property real value: 0
    property real resetValue: 0
    property int decimals: 2
    property string suffix: ""
    property real displayFactor: 1
    property bool keyable: false
    // Eigener Setter (z. B. für Spureigenschaften); sonst Clip-Eigenschaft.
    property var setter: null
    readonly property bool keyed: keyable && app.clip.keyed !== undefined && app.clip.keyed[prop] === true
    readonly property bool keyHere: keyable && app.clip.keyHere !== undefined && app.clip.keyHere[prop] === true
    spacing: 6

    function apply(v) {
        if (setter)
            setter(v)
        else
            app.setClipProperty(root.prop, v)
    }

    Label {
        text: root.label
        Layout.preferredWidth: 84
        color: root.keyed ? Theme.warn : Theme.dim
        elide: Text.ElideRight
        MouseArea {
            anchors.fill: parent
            onDoubleClicked: root.apply(root.resetValue)
        }
    }
    Slider {
        id: slider
        Layout.fillWidth: true
        from: root.from
        to: root.to
        stepSize: root.step
        value: root.value
        onMoved: root.apply(value)
        onPressedChanged: pressed ? app.beginInteractive() : app.endInteractive()
    }
    Label {
        text: (root.value * root.displayFactor).toFixed(root.decimals) + root.suffix
        Layout.preferredWidth: 54
        horizontalAlignment: Text.AlignRight
        font.family: "monospace"
        font.pixelSize: 11
    }
    ToolButton {
        visible: root.keyable
        Layout.preferredWidth: 22
        Layout.preferredHeight: 22
        padding: 0
        text: root.keyHere ? "◆" : "◇"
        font.pixelSize: 13
        tint: root.keyed ? Theme.warn : "#6a707d"
        onClicked: app.toggleKeyframe(root.prop)
        ToolTip.text: root.keyHere ? qsTr("Keyframe hier entfernen") : qsTr("Keyframe am Abspielkopf setzen")
        ToolTip.visible: hovered
        ToolTip.delay: 500
    }
    Item {
        visible: !root.keyable
        Layout.preferredWidth: 0
    }
}
