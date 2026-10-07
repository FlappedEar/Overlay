import QtQuick
import QtQuick.Controls
import "Theme.js" as Theme

// KAN-216: what the move from FlappedEar Telemetry (KAN-125) left in its old
// place, split out of Main.qml. Main.qml opens it at startup when
// appController.startupNotice is not empty.
Dialog {
    font.family: Theme.sans
    id: root
    objectName: "startupNoticeDialog"
    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    width: 560
    title: qsTr("Your data from FlappedEar Telemetry")
    standardButtons: Dialog.Ok
    contentItem: FeLabel {
        width: 500
        text: qsTr("FlappedEar Overlays is the new name of this app. Nothing was deleted or overwritten, but not everything could be moved:") + "\n\n" + appController.startupNotice
        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
        textFormat: Text.PlainText
        color: Theme.onSurface
    }
}
