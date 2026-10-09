import QtQuick
import QtQuick.Controls
import "Theme.js" as Theme

// KAN-216: asks whether to cancel a running export when the editor is closed,
// split out of Main.qml. Yes cancels the export and lets the editor quit;
// No leaves the export running and the editor open.
Dialog {
    font.family: Theme.sans
    id: root
    objectName: "exportQuitDialog"
    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    width: 390
    title: qsTr("Export is still running")
    standardButtons: Dialog.Yes | Dialog.No
    contentItem: FeLabel {
        width: 330
        text: qsTr("Cancel export and quit?")
        wrapMode: Text.WordWrap
        color: Theme.onSurface
    }
    onAccepted: appController.exporter.cancelAndQuit()
}
