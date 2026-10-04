import QtQuick
import QtQuick.Controls
import "Theme.js" as Theme

Slider {
    id: control
    implicitHeight: 28
    hoverEnabled: true
    background: Rectangle {
        x: control.leftPadding
        y: control.topPadding + control.availableHeight / 2 - height / 2
        width: control.availableWidth
        height: 4
        radius: 2
        color: Theme.outlineVariant
        Rectangle {
            width: control.visualPosition * parent.width
            height: parent.height
            radius: 2
            color: Theme.onSurface
        }
    }
    handle: Rectangle {
        x: control.leftPadding + control.visualPosition * (control.availableWidth - width)
        y: control.topPadding + control.availableHeight / 2 - height / 2
        width: 16
        height: 16
        radius: 8
        color: control.pressed ? Theme.primary : Theme.onSurface
        border.width: control.activeFocus || control.hovered ? 2 : 0
        border.color: Theme.primary
    }
}
