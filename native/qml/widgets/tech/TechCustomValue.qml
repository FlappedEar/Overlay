import QtQuick

// KAN-193: Tech value row (oil, ATF, coolant...). Label, an optional range
// bar from Min to Max value with the normal range as a green band, and the
// value, which turns red at or above the warning value.
Item {
    id: root
    property var frame: parent.frame
    anchors.fill: parent
    readonly property var settings: frame.widgetSettings
    readonly property var value: frame.adjusted(frame.raw("source", ""))
    readonly property bool hasValue: value !== undefined && value !== null && Number.isFinite(Number(value))
    readonly property real low: Number(settings.minValue ?? 0)
    readonly property real high: Math.max(low + 0.001, Number(settings.maxValue ?? 100))
    readonly property real normalLow: Number(settings.normalLow ?? 0)
    readonly property real normalHigh: Number(settings.normalHigh ?? 0)
    readonly property real warning: Number(settings.warningValue ?? 0)
    readonly property bool hot: hasValue && warning > 0 && Number(value) >= warning
    readonly property bool showRange: settings.showRange ?? false
    function at(v) {
        return Math.max(0, Math.min(1, (v - low) / (high - low)));
    }

    TechPanel {
        id: panel
        anchors.fill: parent
        frame: root.frame
    }
    Rectangle {
        // Hairline between stacked rows.
        visible: (root.settings.showSeparator ?? true)
            && (panel.stackPosition === "middle" || panel.stackPosition === "bottom")
        x: panel.innerPadding
        width: parent.width - 2 * panel.innerPadding
        height: Math.max(1, frame.sceneScale)
        color: "#ffffff"
        opacity: 0.08
    }
    Row {
        anchors.fill: parent
        anchors.leftMargin: panel.innerPadding
        anchors.rightMargin: panel.innerPadding
        spacing: root.height * 0.3
        TechLabel {
            id: label
            width: Math.max(parent.width * 0.24, Math.min(implicitWidth, parent.width * 0.36))
            anchors.verticalCenter: parent.verticalCenter
            dim: !root.hot
            color: root.hot ? "#ff4d3d" : "#a3a6ad"
            text: String(root.settings.label || root.settings.source || "VALUE").toUpperCase()
            font.pixelSize: root.height * 0.28
            font.letterSpacing: root.height * 0.02
        }
        Item {
            width: parent.width - label.width - valueText.width - 2 * parent.spacing
            height: parent.height
            Item {
                visible: root.showRange
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width
                height: Math.max(2, root.height * 0.13)
                Rectangle {
                    anchors.fill: parent
                    color: "#ffffff"
                    opacity: 0.13
                }
                Rectangle {
                    visible: root.normalHigh > root.normalLow
                    x: parent.width * root.at(root.normalLow)
                    width: parent.width * (root.at(root.normalHigh) - root.at(root.normalLow))
                    height: parent.height
                    color: "#4fd17a"
                    opacity: 0.35
                }
                Rectangle {
                    visible: root.hasValue
                    width: Math.max(2, root.height * 0.08)
                    height: parent.height * 2.4
                    anchors.verticalCenter: parent.verticalCenter
                    x: parent.width * root.at(Number(root.value)) - width / 2
                    color: root.hot ? "#ff4d3d" : "#f4f4f4"
                }
            }
        }
        TechLabel {
            id: valueText
            anchors.verticalCenter: parent.verticalCenter
            width: Math.max(implicitWidth, root.height * 1.5)
            horizontalAlignment: Text.AlignRight
            color: root.hot ? "#ff4d3d" : (root.hasValue ? "#f4f4f4" : "#a3a6ad")
            textFormat: Text.StyledText
            text: (root.hasValue
                   ? (root.settings.prefix || "") + Number(root.value).toFixed(Number(root.settings.decimals ?? 1)) + (root.settings.suffix || "")
                   : (root.settings.fallbackText ?? "—"))
                + ((root.settings.unit && root.settings.showUnit !== false)
                   ? " <font color=\"#a3a6ad\" size=\"2\">" + root.settings.unit + "</font>" : "")
            font.weight: Font.Bold
            font.pixelSize: frame.configuredFontSize() > 0 ? frame.configuredFontSize() * frame.sceneScale : root.height * 0.36
        }
    }
}
