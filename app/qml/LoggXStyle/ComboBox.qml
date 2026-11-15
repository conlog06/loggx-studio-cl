import QtQuick
import QtQuick.Controls
import QtQuick.Templates as T
import LoggX 1.0

T.ComboBox {
    id: control
    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding,
                             implicitIndicatorHeight + topPadding + bottomPadding)
    leftPadding: 10
    rightPadding: 30
    font.pixelSize: Theme.fontSize
    hoverEnabled: true

    delegate: ItemDelegate {
        width: ListView.view.width
        text: control.textRole ? (Array.isArray(control.model) ? modelData[control.textRole] : model[control.textRole]) : modelData
        highlighted: control.highlightedIndex === index
        font.weight: control.currentIndex === index ? Font.DemiBold : Font.Normal
    }

    indicator: Icon {
        x: control.width - width - 9
        y: (control.height - height) / 2
        width: 14
        height: 14
        name: "chevron-down"
        color: Theme.dim
    }

    contentItem: Text {
        text: control.displayText
        font: control.font
        color: control.enabled ? Theme.text : Theme.faint
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    background: Rectangle {
        implicitWidth: 120
        implicitHeight: Theme.controlHeight
        radius: Theme.radius
        color: control.down ? Theme.pressed : control.hovered ? Theme.hover : Theme.raised
        border.width: 1
        border.color: control.activeFocus ? Theme.accent : Theme.border
    }

    popup: T.Popup {
        y: control.height + 4
        width: Math.max(control.width, 160)
        implicitHeight: Math.min(contentItem.implicitHeight + 8, 360)
        padding: 4
        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: control.delegateModel
            currentIndex: control.highlightedIndex
            ScrollIndicator.vertical: ScrollIndicator {}
        }
        background: Rectangle {
            color: Theme.panel2
            radius: Theme.radius
            border.color: Theme.borderStrong
        }
        enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 90 } }
    }
}
