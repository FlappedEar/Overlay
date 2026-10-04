import QtQuick
import QtQuick.Controls
import "Theme.js" as Theme

CheckBox {
    id: control
    implicitHeight: 28
    spacing: 7
    hoverEnabled: true
    indicator: Rectangle {
        x: control.leftPadding
        anchors.verticalCenter: parent.verticalCenter
        width: 17
        height: 17
        radius: Theme.radius
        color: control.checked ? Theme.primary : control.hovered ? Theme.surfaceContainerHighest : "transparent"
        border.width: control.checked ? 0 : control.activeFocus ? 2 : 1.5
        border.color: control.activeFocus ? Theme.primary : Theme.outline
        Text {
            anchors.centerIn: parent
            text: "✓"
            visible: control.checked
            color: Theme.onPrimary
            font.pixelSize: 11
            font.weight: Font.Bold
        }
    }
    contentItem: Text {
        leftPadding: control.indicator.width + control.spacing
        text: control.text
        color: control.enabled ? Theme.onSurface : Theme.outline
        font.family: Theme.sans
        font.features: Theme.numbers
        font.pixelSize: Theme.labelMedium
        verticalAlignment: Text.AlignVCenter
    }
}
