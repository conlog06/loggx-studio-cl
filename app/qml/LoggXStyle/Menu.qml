import QtQuick
import QtQuick.Templates as T
import LoggX 1.0

T.Menu {
    id: control
    implicitWidth: Math.max(implicitBackgroundWidth, implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight, implicitContentHeight + topPadding + bottomPadding)
    padding: 4
    delegate: MenuItem {}
    contentItem: ListView {
        implicitHeight: contentHeight
        model: control.contentModel
        interactive: Window.window ? contentHeight + control.topPadding + control.bottomPadding > Window.window.height : false
        clip: true
        currentIndex: control.currentIndex
    }
    background: Rectangle {
        implicitWidth: 200
        radius: Theme.radius
        color: Theme.panel2
        border.color: Theme.borderStrong
    }
}
