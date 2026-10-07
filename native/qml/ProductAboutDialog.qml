import QtQuick
import QtQuick.Controls
import "Theme.js" as Theme

// KAN-216: Help > About, split out of Main.qml. Main.qml passes the window
// width so the dialog fits a narrow window.
Dialog {
    font.family: Theme.sans
    id: root
    objectName: "productAboutDialog"
    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    property real hostWidth: 0
    width: Math.min(440, root.hostWidth - 40)
    title: qsTr("About %1").arg(Application.displayName)
    standardButtons: Dialog.Close
    contentItem: FeLabel {
        text: qsTr("Version %1").arg(Application.version) + "\n" + qsTr("Video telemetry overlays for track days.")
        wrapMode: Text.WordWrap
    }
}
