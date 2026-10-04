import QtQuick
import QtQuick.Controls
import "Theme.js" as Theme

ComboBox {
    id: control
    property real popupMinimumWidth: 0
    property bool wrapPopupText: false
    implicitHeight: 36
    leftPadding: 11
    rightPadding: 30
    hoverEnabled: true

    contentItem: Text {
        text: control.displayText
        color: control.enabled ? Theme.onSurface : Theme.outline
        font.family: Theme.sans
        font.features: Theme.numbers
        font.pixelSize: Theme.body
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    indicator: Text {
        x: control.width - width - 11
        anchors.verticalCenter: parent.verticalCenter
        text: "⌄"
        color: control.enabled ? Theme.onSurfaceVariant : Theme.outline
        font.pixelSize: 15
    }
    background: Rectangle {
        radius: Theme.radius
        color: !control.enabled ? Theme.surfaceContainerLow : control.hovered || control.pressed ? Theme.surfaceContainerHighest : Theme.surfaceContainerHigh
        border.width: control.activeFocus ? 2 : 0
        border.color: Theme.primary
    }
    delegate: ItemDelegate {
        required property int index
        required property var modelData
        highlighted: control.highlightedIndex === index
        width: control.popup.availableWidth
        height: control.wrapPopupText ? Math.max(34, contentItem.implicitHeight + topPadding + bottomPadding) : 34
        contentItem: Text {
            text: parent.modelData
            color: parent.highlighted ? Theme.onPrimary : Theme.onSurface
            font.family: Theme.sans
            font.features: Theme.numbers
            font.pixelSize: Theme.body
            verticalAlignment: Text.AlignVCenter
            wrapMode: control.wrapPopupText ? Text.Wrap : Text.NoWrap
            elide: control.wrapPopupText ? Text.ElideNone : Text.ElideMiddle
        }
        background: Rectangle {
            radius: Theme.radius
            color: parent.highlighted ? Theme.primary : parent.hovered ? Theme.surfaceContainerHighest : Theme.surfaceContainerHigh
        }
    }
    popup: Popup {
        y: control.height + 4
        width: Math.min(Math.max(control.width, control.popupMinimumWidth),
            control.Window.window ? Math.max(0, control.Window.window.width - 16) : Infinity)
        margins: 8
        implicitHeight: Math.min(contentItem.implicitHeight + 8, 280)
        padding: 4
        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: control.popup.visible ? control.delegateModel : null
            currentIndex: control.highlightedIndex
            ScrollIndicator.vertical: ScrollIndicator {}
        }
        background: Rectangle {
            radius: Theme.radius
            color: Theme.surfaceContainerHigh
        }
    }
}
