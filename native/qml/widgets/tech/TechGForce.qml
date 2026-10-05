import QtQuick
import ".."

// KAN-193: Tech G readout: combined G, then lateral and longitudinal.
Item {
    id: root
    property var frame: parent.frame
    anchors.fill: parent
    readonly property int decimals: Number(frame.widgetSettings.decimals ?? 2)

    GForceData {
        id: gForce
        frame: root.frame
    }
    TechPanel {
        id: panel
        anchors.fill: parent
        frame: root.frame
    }
    TechLabel {
        x: panel.innerPadding
        anchors.top: parent.top
        anchors.topMargin: root.height * 0.2
        dim: true
        text: (frame.widgetSettings.labelText || "G").toUpperCase()
        font.pixelSize: root.height * 0.16
        font.letterSpacing: root.height * 0.015
    }
    TechLabel {
        anchors.right: parent.right
        anchors.rightMargin: panel.innerPadding
        anchors.top: parent.top
        anchors.topMargin: root.height * 0.1
        text: gForce.hasValue ? gForce.combinedG.toFixed(root.decimals) : "—"
        font.weight: Font.Bold
        font.pixelSize: root.height * 0.36
    }
    Row {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: panel.innerPadding
        anchors.bottomMargin: root.height * 0.14
        spacing: root.height * 0.2
        TechLabel {
            dim: true
            textFormat: Text.StyledText
            text: "LAT <font color=\"#f4f4f4\">" + (gForce.hasValue ? Math.abs(gForce.lateral).toFixed(root.decimals) : "—") + "</font>"
            font.pixelSize: root.height * 0.15
        }
        TechLabel {
            dim: true
            textFormat: Text.StyledText
            text: "LON <font color=\"#f4f4f4\">" + (gForce.hasValue ? Math.abs(gForce.longitudinal).toFixed(root.decimals) : "—") + "</font>"
            font.pixelSize: root.height * 0.15
        }
    }
}
