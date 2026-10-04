import QtQuick
import QtQuick.Controls
import "Theme.js" as Theme

Button {
    id: control
    property bool accent: false
    property bool danger: false
    property bool compact: false

    implicitHeight: compact ? 32 : 38
    implicitWidth: Math.max(72, contentItem.implicitWidth + 28)
    padding: 0
    hoverEnabled: true

    contentItem: Text {
        text: control.text
        color: !control.enabled ? Theme.outline : control.accent ? Theme.onPrimary : control.danger ? Theme.error : Theme.onSurface
        font.family: Theme.sans
        font.features: Theme.numbers
        font.pixelSize: Theme.body
        font.weight: Font.DemiBold
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    background: Rectangle {
        radius: Theme.radius
        color: {
            if (!control.enabled)
                return Theme.surfaceContainerLow;
            if (control.accent)
                return control.down ? Theme.primaryPressed : control.hovered ? Theme.primaryHover : Theme.primary;
            if (control.danger)
                return control.down || control.hovered ? Theme.errorContainer : Theme.surfaceContainerHigh;
            return control.down || control.hovered ? Theme.surfaceContainerHighest : Theme.surfaceContainerHigh;
        }
        border.width: control.activeFocus ? 2 : 0
        border.color: control.accent ? Theme.onSurface : Theme.primary
    }
}
