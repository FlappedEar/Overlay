import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "Theme.js" as Theme

// KAN-216: the "Save as template" popup, split out of Main.qml. It saves the
// current scene through appController.templatePicker and selects the new
// template.
Popup {
    font.family: Theme.sans
    id: root
    objectName: "templateSavePopup"
    // Main.qml passes its size in; the popup centres itself in it.
    property real hostWidth: 0
    property real hostHeight: 0
    x: (root.hostWidth - width) / 2
    y: (root.hostHeight - height) / 2
    width: 420
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    onOpened: {
        templateName.text = "";
        templateDescription.text = "";
        templateName.forceActiveFocus();
    }
    background: Rectangle {
        radius: Theme.dialogRadius
        color: Theme.surfaceContainer
        border.color: Theme.outlineVariant
    }
    contentItem: ColumnLayout {
        spacing: 10
        FeLabel {
            text: qsTr("Save as new custom template")
            color: Theme.onSurface
            font.pixelSize: Theme.dialogTitle
            font.weight: Font.DemiBold
        }
        FeLabel {
            Layout.fillWidth: true
            text: qsTr("Create a named custom template from the complete current scene, including widget positions, bindings and styles.")
            color: Theme.onSurfaceVariant
            font.pixelSize: Theme.labelMedium
            wrapMode: Text.WordWrap
        }
        FeLabel {
            text: qsTr("Name")
            color: Theme.onSurfaceVariant
            font.pixelSize: Theme.labelMedium
        }
        FeTextField {
            id: templateName
            objectName: "templateNameField"
            Layout.fillWidth: true
            placeholderText: qsTr("My circuit layout")
        }
        FeLabel {
            text: qsTr("Description")
            color: Theme.onSurfaceVariant
            font.pixelSize: Theme.labelMedium
        }
                FeLabel {
                    Layout.fillWidth: true
                    visible: appController.widgetModel.lastError.length > 0
                    text: appController.widgetModel.lastError
                    color: Theme.error
                    font.pixelSize: Theme.labelMedium
                    wrapMode: Text.Wrap
                }
        FeTextField {
            id: templateDescription
            Layout.fillWidth: true
            placeholderText: qsTr("When and why to use this layout")
        }
        RowLayout {
            Layout.fillWidth: true
            Item {
                Layout.fillWidth: true
            }
            FeButton {
                text: qsTr("Cancel")
                onClicked: root.close()
            }
            FeButton {
                objectName: "templateSaveButton"
                accent: true
                text: qsTr("Save template")
                enabled: templateName.text.trim().length > 0 && appController.widgetModel.count > 0
                onClicked: {
                    const templateId = appController.widgetModel.saveCurrentAsTemplate(templateName.text, templateDescription.text);
                    if (templateId) {
                        appController.templatePicker.select(templateId);
                        appController.templatePicker.markActive(templateId);
                        root.close();
                    }
                }
            }
        }
    }
}
