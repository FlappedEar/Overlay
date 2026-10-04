import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "Theme.js" as Theme

Item {
    id: root
    property string text: ""
    Layout.fillWidth: true
    implicitHeight: 28

    RowLayout {
        anchors.fill: parent
        spacing: 8
        Label {
            text: root.text.toUpperCase()
            color: Theme.onSurfaceVariant
            font.family: Theme.sans
            font.pixelSize: Theme.labelSmall
            font.weight: Font.DemiBold
            font.letterSpacing: 1.2
        }
        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: Theme.outlineVariant
        }
    }
}
