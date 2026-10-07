import QtQuick
import QtQuick.Controls
import "Theme.js" as Theme

// KAN-216: Help > Keyboard Shortcuts, split out of Main.qml.
Dialog {
    font.family: Theme.sans
    id: root
    objectName: "shortcutHelpDialog"
    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    width: 450
    title: qsTr("Keyboard Shortcuts")
    standardButtons: Dialog.Close
    contentItem: FeLabel {
        width: 390
        text: qsTr("Space  Play / pause\n← / →  Seek 5 seconds\nShift+← / →  Seek 30 seconds\nHome / End  Beginning / end\nCtrl/Cmd+E  Export\nCtrl/Cmd+Shift+V  Open video · Ctrl/Cmd+Shift+T  Open telemetry\nCtrl/Cmd+G  Group · Ctrl/Cmd+Shift+G  Ungroup\nCtrl/Cmd+Shift+N  New widget (widget editor)\nDelete / Backspace  Delete selected layer\nCtrl+Cmd+F (macOS)  Full screen · Escape  Exit or dismiss")
        color: Theme.onSurface
        wrapMode: Text.WordWrap
        font.pixelSize: Theme.body
    }
}
