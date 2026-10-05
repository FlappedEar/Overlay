import QtQuick

// KAN-193: Tech speed tile.
Item {
    id: root
    property var frame: parent.frame
    anchors.fill: parent
    readonly property bool mph: frame.widgetSettings.unit === "mph"

    TechPanel {
        id: panel
        anchors.fill: parent
        frame: root.frame
    }
    TechLabel {
        x: panel.innerPadding
        y: panel.innerPadding
        dim: true
        text: (frame.widgetSettings.label || "Speed").toUpperCase()
        font.pixelSize: Math.max(8, root.height * 0.13)
        font.letterSpacing: root.height * 0.012
    }
    TechLabel {
        anchors.left: parent.left
        anchors.leftMargin: panel.innerPadding
        anchors.bottom: parent.bottom
        anchors.bottomMargin: panel.innerPadding * 0.6
        text: frame.numberText(frame.raw("source", "speed"), root.mph ? 0.621371 : 1)
        font.weight: Font.Bold
        font.pixelSize: frame.configuredFontSize() > 0 ? frame.configuredFontSize() * frame.sceneScale : root.height * 0.5
    }
    TechLabel {
        visible: frame.widgetSettings.showUnit ?? true
        anchors.right: parent.right
        anchors.rightMargin: panel.innerPadding
        anchors.bottom: parent.bottom
        anchors.bottomMargin: panel.innerPadding
        dim: true
        text: (frame.widgetSettings.unit || "km/h").toUpperCase()
        font.pixelSize: Math.max(8, root.height * 0.12)
    }
}
