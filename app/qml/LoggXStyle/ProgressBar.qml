import QtQuick
import QtQuick.Templates as T
import LoggX 1.0

T.ProgressBar {
    id: control
    implicitWidth: 200
    implicitHeight: 8
    contentItem: Item {
        Rectangle {
            width: control.visualPosition * parent.width
            height: parent.height
            radius: 4
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0; color: Theme.accent }
                GradientStop { position: 1; color: Theme.accent2 }
            }
        }
    }
    background: Rectangle {
        radius: 4
        color: Theme.border
    }
}
