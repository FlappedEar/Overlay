import QtQuick
import QtQuick.Controls
import "Theme.js" as Theme

SpinBox {
    id: control
    implicitHeight: 36
    editable: true
    contentItem: TextInput {
        z: 2
        text: control.textFromValue(control.value, control.locale)
        color: control.enabled ? Theme.onSurface : Theme.outline
        selectionColor: Theme.primary
        selectedTextColor: Theme.onPrimary
        horizontalAlignment: Qt.AlignHCenter
        verticalAlignment: Qt.AlignVCenter
        font.family: Theme.sans
        font.features: Theme.numbers
        font.pixelSize: Theme.body
        readOnly: !control.editable
        validator: control.validator
        inputMethodHints: Qt.ImhFormattedNumbersOnly
    }
    up.indicator: Rectangle {
        x: parent.width - width
        height: parent.height
        width: 30
        radius: Theme.radius
        color: control.up.pressed ? Theme.surfaceContainerHighest : "transparent"
        Text {
            anchors.centerIn: parent
            text: "+"
            color: Theme.onSurfaceVariant
            font.pixelSize: 14
        }
    }
    down.indicator: Rectangle {
        x: 0
        height: parent.height
        width: 30
        radius: Theme.radius
        color: control.down.pressed ? Theme.surfaceContainerHighest : "transparent"
        Text {
            anchors.centerIn: parent
            text: "−"
            color: Theme.onSurfaceVariant
            font.pixelSize: 14
        }
    }
    background: Rectangle {
        radius: Theme.radius
        color: control.enabled ? Theme.surfaceContainerHigh : Theme.surfaceContainerLow
        border.color: Theme.primary
        border.width: control.activeFocus ? 2 : 0
    }
}
