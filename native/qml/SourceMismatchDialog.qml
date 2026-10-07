import QtQuick
import QtQuick.Controls
import "Theme.js" as Theme

// KAN-216: asks whether a source file that does not match the one stored with
// the project is an intentional replacement, split out of Main.qml. Main.qml
// opens it when appController reports a mismatch; the answer goes back through
// resolveSourceMismatch.
Dialog {
    font.family: Theme.sans
    id: root
    objectName: "sourceMismatchDialog"
    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    width: 470
    title: qsTr("Source does not match project")
    contentItem: FeLabel {
        width: 410
        text: qsTr("The selected file “%1” does not match the source originally stored with this project. Use it as an intentional replacement?")
            .arg(appController.sourceMismatchCandidateName)
        wrapMode: Text.WordWrap
        color: Theme.onSurface
    }
    footer: DialogButtonBox {
        standardButtons: DialogButtonBox.Yes | DialogButtonBox.Cancel
        onAccepted: {
            root.close()
            appController.resolveSourceMismatch(true)
        }
        onRejected: {
            root.close()
            appController.resolveSourceMismatch(false)
        }
    }
}
