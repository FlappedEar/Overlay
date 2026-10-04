import QtQuick
import QtQuick.Controls
import "Theme.js" as Theme

TextField {
    id: control
    implicitHeight: 36
    color: control.readOnly ? Theme.onSurfaceVariant : Theme.onSurface
    placeholderTextColor: Theme.outline
    selectionColor: Theme.primary
    selectedTextColor: Theme.onPrimary
    leftPadding: 11
    rightPadding: 11
    font.family: Theme.sans
    font.features: Theme.numbers
    font.pixelSize: Theme.body
    background: Rectangle {
        radius: Theme.radius
        color: control.readOnly || !control.enabled ? Theme.surfaceContainerLow : Theme.surfaceContainerHigh
        border.width: control.activeFocus ? 2 : 0
        border.color: Theme.primary
    }
}
