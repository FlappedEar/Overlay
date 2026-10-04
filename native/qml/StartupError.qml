import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "Theme.js" as Theme

Window {
    id: window
    property string message: ""
    title: Application.displayName
    width: 520
    height: Math.max(200, content.implicitHeight + 48)
    visible: true
    color: Theme.surface

    ColumnLayout {
        id: content
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 24
        spacing: 20
        FeLabel {
            Layout.fillWidth: true
            text: window.message
            wrapMode: Text.Wrap
        }
        FeButton {
            Layout.alignment: Qt.AlignRight
            text: qsTr("Close")
            onClicked: window.close()
        }
    }
}
