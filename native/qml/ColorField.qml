import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import "Theme.js" as Theme

RowLayout {
    id: root
    property string colorValue: "#ffffff"
    signal edited(string value)
    spacing: 6

    Rectangle {
        width: 32
        height: 32
        radius: Theme.radius
        color: root.colorValue
        border.color: Theme.outline
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: colorDialog.open()
        }
    }
    FeTextField {
        Layout.fillWidth: true
        text: root.colorValue
        onEditingFinished: root.edited(text)
    }
    ColorDialog {
        id: colorDialog
        selectedColor: root.colorValue
        onAccepted: root.edited(selectedColor.toString())
    }
}
