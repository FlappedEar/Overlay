import QtQuick

// KAN-193: Tech throttle and brake. Two slanted horizontal bars.
Item {
    id: root
    property var frame: parent.frame
    anchors.fill: parent

    TechPanel {
        id: panel
        anchors.fill: parent
        frame: root.frame
    }
    Column {
        anchors.fill: parent
        anchors.margins: panel.innerPadding
        anchors.topMargin: panel.innerPadding * 1.2
        spacing: (height - 2 * rowHeight) / 1
        readonly property real rowHeight: height * 0.42
        Repeater {
            model: [
                { label: "acceleratorLabel", source: "acceleratorSource", fallback: "throttle", color: "acceleratorColor",
                  min: "acceleratorMin", max: "acceleratorMax", name: "Throttle", tint: "#4fd17a" },
                { label: "brakeLabel", source: "brakeSource", fallback: "brake", color: "brakeColor",
                  min: "brakeMin", max: "brakeMax", name: "Brake", tint: "#ff4d3d" }
            ]
            Item {
                id: row
                required property var modelData
                width: parent.width
                height: parent.rowHeight
                readonly property var raw: root.frame.raw(modelData.source, modelData.fallback)
                readonly property bool hasValue: raw !== undefined && raw !== null && Number.isFinite(Number(raw))
                readonly property real low: Number(root.frame.widgetSettings[modelData.min] ?? 0)
                readonly property real high: Number(root.frame.widgetSettings[modelData.max] ?? 100)
                readonly property real fraction: hasValue
                    ? Math.max(0, Math.min(1, (Number(raw) - low) / Math.max(0.001, high - low))) : 0
                TechLabel {
                    anchors.left: parent.left
                    anchors.top: parent.top
                    dim: true
                    text: (root.frame.widgetSettings[row.modelData.label] || row.modelData.name).toUpperCase()
                    font.pixelSize: row.height * 0.3
                    font.letterSpacing: row.height * 0.03
                }
                TechLabel {
                    visible: root.frame.widgetSettings.showValues ?? true
                    anchors.right: parent.right
                    anchors.top: parent.top
                    text: row.hasValue ? Number(row.raw).toFixed(Number(root.frame.widgetSettings.decimals ?? 0)) + "%" : "—"
                    font.weight: Font.Bold
                    font.pixelSize: row.height * 0.34
                }
                Item {
                    id: bar
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.leftMargin: height * 0.33
                    height: row.height * 0.4
                    // Slanted like the panel corners: x shifts left down the bar.
                    transform: Matrix4x4 {
                        matrix: Qt.matrix4x4(1, -0.33, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1)
                    }
                    Rectangle {
                        anchors.fill: parent
                        color: "#ffffff"
                        opacity: 0.1
                    }
                    Rectangle {
                        width: parent.width * row.fraction
                        height: parent.height
                        color: root.frame.widgetSettings[row.modelData.color] || row.modelData.tint
                    }
                }
            }
        }
    }
}
