import QtQuick
import QtQuick.Controls

// Kleiner Werkzeugknopf mit Symbol und Hinweis beim Darüberfahren.
ToolButton {
    property string iconName
    property string tip
    icon.name: iconName
    ToolTip.text: tip
    ToolTip.visible: hovered && tip !== ""
    ToolTip.delay: 500
}
