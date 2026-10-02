import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: pedals
    property var frame: parent.frame
    anchors.fill: parent
    // One pedal row's share of the widget, from the widget's own fixed size:
    // sizes taken from the rows' layout would feed back into it (KAN-140).
    readonly property real rowHeight: Math.max(0, (height - 2 * panel.innerPadding - pedalColumn.spacing) / 2)
    TelemetryPanel {
        id: panel
        anchors.fill: parent
        frame: parent.frame
    }
    ColumnLayout {
        id: pedalColumn
        anchors.fill: parent
        anchors.margins: panel.innerPadding
        spacing: Math.max(5 * frame.sceneScale, height * 0.045)
        Repeater {
            model: [
            {
                "labelKey": "acceleratorLabel",
                "sourceKey": "acceleratorSource",
                "fallback": "throttle",
                "colorKey": "acceleratorColor",
                "minKey": "acceleratorMin",
                "maxKey": "acceleratorMax"
            },
            {
                "labelKey": "brakeLabel",
                "sourceKey": "brakeSource",
                "fallback": "brake",
                "colorKey": "brakeColor",
                "minKey": "brakeMin",
                "maxKey": "brakeMax"
            }
        ]
            ColumnLayout {
                required property var modelData
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 3 * frame.sceneScale
                property var pedalRaw: frame.raw(modelData.sourceKey, modelData.fallback)
                property bool hasValue: pedalRaw !== undefined && pedalRaw !== null && Number.isFinite(Number(pedalRaw))
                property real pedalValue: hasValue ? Number(pedalRaw) : 0
                RowLayout {
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.max(panel.panelLabelSize * 1.12, pedals.rowHeight * 0.38)
                    Label {
                        Layout.fillWidth: true
                        text: frame.widgetSettings[modelData.labelKey] || (modelData.fallback === "throttle" ? "Throttle" : "Brake")
                        color: frame.secondary
                        font.family: frame.family
                        font.weight: Font.DemiBold
                        font.pixelSize: panel.panelLabelSize
                    }
                    Label {
                        visible: frame.widgetSettings.showValues ?? true
                        text: parent.parent.hasValue ? parent.parent.pedalValue.toFixed(Number(frame.widgetSettings.decimals ?? 0)) + "%" : "—"
                        color: frame.primary
                        font.family: frame.family
                        font.weight: Font.DemiBold
                        font.pixelSize: panel.panelLabelSize
                    }
                }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.max(11 * frame.sceneScale, pedals.rowHeight * 0.28)
                    radius: Number(frame.widgetSettings.barRadius ?? 5) * frame.sceneScale
                    color: frame.neutralTrack
                    Rectangle {
                        property real low: Number(frame.widgetSettings[modelData.minKey] ?? 0)
                        property real high: Number(frame.widgetSettings[modelData.maxKey] ?? 100)
                        width: parent.parent.hasValue ? parent.width * Math.max(0, Math.min(1, (parent.parent.pedalValue - low) / Math.max(0.001, high - low))) : 0
                        height: parent.height
                        radius: parent.radius
                        color: frame.widgetSettings[modelData.colorKey]
                            || (modelData.fallback === "throttle" ? frame.throttle : frame.brake)
                    }
                }
            }
        }
    }
}
