import QtQuick
import QtQuick.Templates as T
import LoggX 1.0

T.Label {
    color: enabled ? Theme.text : Theme.faint
    linkColor: Theme.accentHover
    font.pixelSize: Theme.fontSize
}
