import QtQuick
import QtQuick.Controls
import "Theme.js" as Theme

// KAN-216: asks before an export replaces a file that already exists, split
// out of Main.qml. Yes emits confirmed(), which the export dialog answers by
// starting the export with replacing allowed; No does nothing.
Dialog {
    font.family: Theme.sans
    id: root
    objectName: "exportOverwriteDialog"
    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    width: 440
    title: qsTr("Replace existing file?")
    standardButtons: Dialog.Yes | Dialog.No
    signal confirmed()
    contentItem: FeLabel {
        width: 380
        text: qsTr("The selected export target already exists. Replace it only after the new video has encoded and passed validation?")
        wrapMode: Text.WordWrap
        color: Theme.onSurface
    }
    onAccepted: root.confirmed()
}
