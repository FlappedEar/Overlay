import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "Theme.js" as Theme

// KAN-216: the "unsaved changes" dialog, split out of Main.qml. Main.qml opens
// it when appController asks to confirm a new project, an open or a quit with
// unsaved changes; the choice goes back through resolveDestructiveAction, and
// closing it any other way cancels the pending action.
Dialog {
    font.family: Theme.sans
    id: root
    objectName: "dirtyProjectDialog"
    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape
    padding: 0
    width: 480
    property bool actionHandled: false

    function actionTitle() {
        if (appController.pendingDestructiveAction === "new") return qsTr("Save before creating a new project?")
        if (appController.pendingDestructiveAction === "open") return qsTr("Save before opening another project?")
        return qsTr("Save before quitting?")
    }

    function resolve(action) {
        actionHandled = true
        appController.resolveDestructiveAction(action)
    }

    background: Rectangle {
        radius: Theme.dialogRadius
        color: Theme.surfaceContainer
        border.width: 1
        border.color: Theme.outlineVariant
    }

    contentItem: ColumnLayout {
        spacing: 0

        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 86

            Rectangle {
                width: 34
                height: 34
                radius: Theme.radius
                color: Theme.primaryContainer
                border.width: 1
                border.color: "transparent"
                anchors.left: parent.left
                anchors.leftMargin: 28
                anchors.verticalCenter: parent.verticalCenter

                Text {
                    anchors.centerIn: parent
                    text: "●"
                    color: Theme.primary
                    font.pixelSize: Theme.titleLarge
                }
            }

            Column {
                anchors.left: parent.left
                anchors.leftMargin: 76
                anchors.right: parent.right
                anchors.rightMargin: 28
                anchors.verticalCenter: parent.verticalCenter
                spacing: 5

                Text {
                    text: root.actionTitle()
                    color: Theme.onSurface
                    font.family: Theme.sans
                    font.pixelSize: Theme.dialogTitle
                    font.weight: Font.DemiBold
                }
                Text {
                    text: qsTr("Your project has unsaved changes.")
                    color: Theme.onSurfaceVariant
                    font.family: Theme.sans
                    font.pixelSize: Theme.body
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.outlineVariant
        }

        Text {
            Layout.fillWidth: true
            Layout.leftMargin: 28
            Layout.rightMargin: 28
            Layout.topMargin: 22
            Layout.bottomMargin: 22
            text: qsTr("Save your changes before continuing? You can also discard them and continue without saving.")
            color: Theme.onSurface
            font.family: Theme.sans
            font.pixelSize: Theme.titleSmall
            lineHeight: 1.35
            wrapMode: Text.WordWrap
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.outlineVariant
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            Layout.topMargin: 16
            Layout.bottomMargin: 18
            spacing: 10

            FeButton {
                objectName: "dirtyProjectDiscard"
                text: qsTr("Discard changes")
                danger: true
                onClicked: root.resolve("discard")
            }
            Item { Layout.fillWidth: true }
            FeButton {
                objectName: "dirtyProjectCancel"
                text: qsTr("Cancel")
                onClicked: root.resolve("cancel")
            }
            FeButton {
                objectName: "dirtyProjectSave"
                text: qsTr("Save project")
                accent: true
                onClicked: root.resolve("save")
            }
        }
    }

    onOpened: {
        actionHandled = false
        forceActiveFocus()
    }
    onClosed: {
        if (!actionHandled)
            appController.cancelPendingDestructiveAction()
    }
}
