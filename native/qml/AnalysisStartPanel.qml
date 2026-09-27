import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    property alias outingName: nameField.text
    property bool importEnabled: true
    property alias includeSubfolders: subfolders.checked
    signal chooseFiles()
    signal chooseFolder()

    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(480, parent.width - 48)
        spacing: 18
        Label {
            text: qsTr("Start an outing")
            color: "#f2f6fb"
            font.pixelSize: 28
            font.weight: Font.DemiBold
        }
        Label {
            Layout.fillWidth: true
            text: qsTr("Add your day's RCZ or VBO recordings to see laps, best-day results and progression. Compatible routes are grouped automatically. No video is required.")
            wrapMode: Text.WordWrap
            color: "#91a0b2"
            font.pixelSize: 14
        }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 8
            Label { text: qsTr("Outing name"); color: "#dce4ee"; font.pixelSize: 12 }
            TextField {
                id: nameField
                objectName: "analysisOutingName"
                Layout.fillWidth: true
                implicitHeight: 46
                maximumLength: 160
                enabled: root.importEnabled
                placeholderText: qsTr("e.g. Jastrząb · Saturday")
                color: "#f2f6fb"
                placeholderTextColor: "#657386"
                leftPadding: 14
                background: Rectangle {
                    radius: 8
                    color: "#0d141d"
                    border.color: nameField.activeFocus ? "#55e6a5" : "#2a3746"
                    border.width: nameField.activeFocus ? 2 : 1
                }
                onAccepted: if (importFiles.enabled) root.chooseFiles()
            }
        }
        FeButton {
            id: importFiles
            objectName: "analysisChooseFiles"
            Layout.fillWidth: true
            text: qsTr("Add RCZ / VBO files…")
            accent: true
            enabled: root.importEnabled && nameField.text.trim().length > 0
            onClicked: root.chooseFiles()
        }
        // KAN-87: a whole folder of recordings, with subfolders only when asked.
        RowLayout {
            Layout.fillWidth: true
            spacing: 12
            FeButton {
                objectName: "analysisChooseFolder"
                text: qsTr("Add a folder…")
                enabled: importFiles.enabled
                onClicked: root.chooseFolder()
            }
            FeCheckBox {
                id: subfolders
                objectName: "analysisIncludeSubfolders"
                text: qsTr("Include subfolders")
                enabled: root.importEnabled
            }
            Item { Layout.fillWidth: true }
        }
        Label {
            Layout.fillWidth: true
            text: qsTr("Select several files at once. Matching VBO/RCZ exports share one run. For RaceChrono’s calculated G, choose VBO.")
            wrapMode: Text.WordWrap
            color: "#657386"
            font.pixelSize: 11
        }
    }
}
