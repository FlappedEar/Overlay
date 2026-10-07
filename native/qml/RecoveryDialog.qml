import QtQuick
import QtQuick.Controls
import "Theme.js" as Theme

// KAN-216: the startup offer to recover unsaved changes, split out of Main.qml.
// Main.qml opens and closes it from appController.recoveryPending; the choice
// goes back through resolveStartupRecovery.
Dialog {
    font.family: Theme.sans
    id: root
    objectName: "recoveryDialog"
    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    closePolicy: Popup.NoAutoClose
    width: 460
    title: qsTr("Recover unsaved changes?")
    contentItem: FeLabel {
        width: 400
        text: qsTr("FlappedEar Overlays found changes that were not saved to the project file. Recover them as an unsaved document, or discard them and open the saved project?")
        wrapMode: Text.WordWrap
        color: Theme.onSurface
    }
    footer: DialogButtonBox {
        Button {
            objectName: "recoveryDiscard"
            text: qsTr("Discard")
            DialogButtonBox.buttonRole: DialogButtonBox.DestructiveRole
            onClicked: appController.resolveStartupRecovery("discard")
        }
        Button {
            objectName: "recoveryRecover"
            text: qsTr("Recover")
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            onClicked: appController.resolveStartupRecovery("recover")
        }
    }
}
