import QtQuick

// KAN-193: Tech heart rate. Optional five-zone strip from the max heart rate
// (Max value): zones start at 50, 60, 70, 80 and 90 % of it.
Item {
    id: root
    property var frame: parent.frame
    anchors.fill: parent
    readonly property var raw: frame.adjusted(frame.raw("source", "heartRate"))
    readonly property bool hasValue: raw !== undefined && raw !== null && Number.isFinite(Number(raw))
    readonly property real maxRate: Math.max(1, Number(frame.widgetSettings.maxValue ?? 220))
    readonly property int zone: !hasValue ? 0 : Math.max(0, Math.min(5, Math.floor((Number(raw) / maxRate - 0.4) * 10)))
    readonly property color red: frame.widgetSettings.accentColor || "#ff4d3d"

    TechPanel {
        id: panel
        anchors.fill: parent
        frame: root.frame
    }
    Column {
        anchors.centerIn: parent
        spacing: root.height * 0.04
        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: root.height * 0.05
            Canvas {
                visible: frame.widgetSettings.showIcon ?? true
                width: root.height * 0.14
                height: width
                anchors.verticalCenter: parent.verticalCenter
                onPaint: {
                    const ctx = getContext("2d");
                    ctx.reset();
                    ctx.scale(width / 24, height / 24);
                    ctx.fillStyle = root.red;
                    ctx.beginPath();
                    ctx.moveTo(12, 21);
                    ctx.bezierCurveTo(12, 21, 4.5, 16.4, 2.4, 11.7);
                    ctx.bezierCurveTo(0.9, 8.2, 3.1, 4.5, 6.7, 4.5);
                    ctx.bezierCurveTo(8.9, 4.5, 10.3, 5.7, 12, 7.6);
                    ctx.bezierCurveTo(13.7, 5.7, 15.1, 4.5, 17.3, 4.5);
                    ctx.bezierCurveTo(20.9, 4.5, 23.1, 8.2, 21.6, 11.7);
                    ctx.bezierCurveTo(19.5, 16.4, 12, 21, 12, 21);
                    ctx.fill();
                }
            }
            TechLabel {
                anchors.verticalCenter: parent.verticalCenter
                dim: true
                text: (frame.widgetSettings.unit || "bpm").toUpperCase()
                font.pixelSize: root.height * 0.1
                font.letterSpacing: root.height * 0.012
            }
        }
        TechLabel {
            anchors.horizontalCenter: parent.horizontalCenter
            text: frame.numberText(frame.raw("source", "heartRate"), 1)
            font.weight: Font.Bold
            font.pixelSize: frame.configuredFontSize() > 0 ? frame.configuredFontSize() * frame.sceneScale : root.height * 0.36
        }
        Row {
            visible: frame.widgetSettings.showZones ?? false
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: root.height * 0.02
            Repeater {
                model: 5
                Rectangle {
                    required property int index
                    width: root.width * 0.13
                    height: Math.max(2, root.height * 0.035)
                    color: index < root.zone ? root.red : "#ffffff"
                    opacity: index < root.zone ? 1 : 0.18
                }
            }
        }
    }
}
