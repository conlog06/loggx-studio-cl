import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Regler für eine Eigenschaft des ausgewählten Clips. Ziehen ergibt einen
// einzigen Undo-Schritt; Doppelklick auf die Beschriftung setzt zurück.
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
    spacing: 8

    Label {
        text: root.label
        Layout.preferredWidth: 78
        color: "#b3b9c6"
        MouseArea {
            anchors.fill: parent
            onDoubleClicked: app.setClipProperty(root.prop, root.resetValue)
        }
    }
    Slider {
        id: slider
        Layout.fillWidth: true
        from: root.from
        to: root.to
        stepSize: root.step
        value: root.value
        onMoved: app.setClipProperty(root.prop, value)
        onPressedChanged: pressed ? app.beginInteractive() : app.endInteractive()
    }
    Label {
        text: (root.value * root.displayFactor).toFixed(root.decimals) + root.suffix
        Layout.preferredWidth: 58
        horizontalAlignment: Text.AlignRight
        font.family: "monospace"
        font.pixelSize: 11
    }
}
