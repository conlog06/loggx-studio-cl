import QtQuick
import QtQuick.Templates as T
import QtQuick.Controls
import LoggX 1.0

T.Dialog {
    id: control
    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            contentWidth + leftPadding + rightPadding,
                            implicitHeaderWidth, implicitFooterWidth)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             contentHeight + topPadding + bottomPadding
                             + (implicitHeaderHeight > 0 ? implicitHeaderHeight + spacing : 0)
                             + (implicitFooterHeight > 0 ? implicitFooterHeight + spacing : 0))
    padding: 18
    topPadding: 6
    background: Rectangle {
        radius: 12
        color: Theme.panel2
        border.color: Theme.borderStrong
    }
    header: Text {
        text: control.title
        visible: control.title
        elide: Text.ElideRight
        font.pixelSize: 16
        font.weight: Font.DemiBold
        color: Theme.text
        padding: 18
        bottomPadding: 6
    }
    footer: DialogButtonBox {
        visible: count > 0
        alignment: Qt.AlignRight
        padding: 14
        spacing: 8
        background: Item {}
    }
    T.Overlay.modal: Rectangle { color: "#a0000000" }
    enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 120 } }
}
