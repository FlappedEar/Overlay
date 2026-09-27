import QtQuick
import QtQuick.Controls

Item {
    id: root

    property var frame: parent.frame
    property var timing: {
        frame.renderContext.time;
        return frame.renderContext.lapTiming;
    }

    readonly property string tileType: String(frame.widgetType)
    readonly property bool isSpeed: tileType.indexOf("speed") === 0
    readonly property bool isBest: tileType.endsWith("Best")
    readonly property bool isCurrent: tileType.endsWith("Current")
    readonly property bool isDelta: tileType.endsWith("Delta")
    // Hotlap mode (current lap time only): one chosen lap, 0:00 before its
    // start-line crossing, running during it, and held at its final time after.
    readonly property bool hotlap: tileType === "lapCurrent" && (frame.widgetSettings.hotlapMode ?? false)
    readonly property var hotlapTiming: {
        frame.renderContext.time;
        return root.hotlap ? frame.renderContext.fixedLapTiming(Number(frame.widgetSettings.hotlapLap ?? 0)) : ({});
    }
    readonly property bool hotlapFinished: root.hotlap && root.hotlapTiming.state === "finished"
    readonly property real tileScale: frame.sceneScale * frame.widgetScale
    readonly property int decimals: isSpeed
        ? Number(isDelta ? frame.widgetSettings.speedDeltaDecimals ?? 1
                         : frame.widgetSettings.speedDecimals ?? 0)
        : Number(isDelta ? frame.widgetSettings.deltaDecimals ?? 2
                         : frame.widgetSettings.timingDecimals ?? 1)
    readonly property real metricValue: Number(isSpeed
        ? (isBest ? timing.referenceSpeedKmh
                  : isCurrent ? timing.currentSpeedKmh : timing.speedDeltaKmh)
        : (isBest ? timing.bestLapSeconds
                  : isCurrent ? (hotlap ? hotlapTiming.elapsedSeconds : timing.currentElapsedSeconds)
                              : timing.liveDeltaSeconds))
    readonly property bool hasValue: Number.isFinite(metricValue)
    readonly property real deltaRange: Math.max(1, Number(isSpeed
        ? frame.widgetSettings.speedDeltaRangeKmh ?? 30
        : frame.widgetSettings.deltaRangeSeconds ?? 10))
    readonly property bool deltaIsGood: hasValue && (isSpeed ? metricValue >= 0 : metricValue <= 0)
    readonly property color goodColor: frame.widgetSettings.accentColor || "#20d05a"
    readonly property color badColor: frame.widgetSettings.accentColor2 || "#ef4f5f"
    readonly property color deltaColor: !hasValue ? frame.secondary
        : deltaIsGood ? goodColor : badColor
    readonly property var lapNumber: isBest
        ? timing.bestLapNumber : hotlap ? hotlapTiming.lapNumber : timing.currentLapNumber

    function formatTime(seconds) {
        const value = Number(seconds);
        if (!Number.isFinite(value) || value < 0)
            return "—:—." + "—".repeat(Math.max(1, decimals));
        const minutes = Math.floor(value / 60);
        const remainder = value - minutes * 60;
        const width = decimals > 0 ? 3 + decimals : 2;
        return minutes + ":" + remainder.toFixed(decimals).padStart(width, "0");
    }

    function formatNumber(value) {
        if (!Number.isFinite(value))
            return "—";
        const zeroThreshold = 0.5 * Math.pow(10, -decimals);
        const rounded = Math.abs(value) < zeroThreshold ? 0 : value;
        return (isDelta && rounded > 0 ? "+" : "") + rounded.toFixed(decimals);
    }

    TelemetryPanel {
        anchors.fill: parent
        frame: root.frame
        borderWidth: Number(root.frame.widgetSettings.borderWidth ?? 1) * root.tileScale
        cornerRadius: Number(root.frame.widgetSettings.cornerRadius ?? 12) * root.tileScale
    }

    Label {
        visible: !root.isDelta
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.leftMargin: 12 * root.tileScale
        anchors.topMargin: 7 * root.tileScale
        text: root.frame.widgetSettings.label || (root.isBest ? qsTr("Best") : qsTr("Current"))
        color: root.frame.primary
        font.family: root.frame.family
        font.pixelSize: Math.min(24 * root.frame.labelScale * root.tileScale,
                                 root.height * 0.22)
        font.weight: Font.Medium
    }

    Label {
        visible: !root.isDelta && root.isSpeed && (root.frame.widgetSettings.showUnit ?? true)
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.rightMargin: 12 * root.tileScale
        anchors.topMargin: 11 * root.tileScale
        text: root.frame.widgetSettings.unit || "km/h"
        color: root.frame.secondary
        font.family: root.frame.family
        font.pixelSize: Math.min(15 * root.frame.labelScale * root.tileScale,
                                 root.height * 0.14)
        font.weight: Font.DemiBold
    }

    Label {
        visible: !root.isDelta
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.leftMargin: 12 * root.tileScale
        anchors.bottomMargin: 13 * root.tileScale
        text: Number.isFinite(root.lapNumber) ? root.lapNumber : "—"
        color: root.frame.primary
        font.family: root.frame.family
        font.pixelSize: Math.min(24 * root.frame.labelScale * root.tileScale,
                                 root.height * 0.22)
        font.weight: Font.DemiBold
    }

    Item {
        id: deltaGauge
        visible: root.isDelta
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.leftMargin: 10 * root.tileScale
        anchors.rightMargin: 10 * root.tileScale
        anchors.topMargin: 8 * root.tileScale
        height: parent.height * 0.34
        readonly property real centerX: width / 2
        readonly property real magnitude: root.hasValue
            ? Math.min(1, Math.abs(root.metricValue) / root.deltaRange) : 0

        Rectangle {
            z: 2
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            height: Math.max(1, 2 * root.tileScale)
            color: root.frame.primary
            opacity: 0.9
        }
        Repeater {
            model: 5
            Rectangle {
                z: 2
                required property int index
                x: index * (deltaGauge.width - width) / 4
                anchors.verticalCenter: deltaGauge.verticalCenter
                width: Math.max(1, 2 * root.tileScale)
                height: index === 2 ? deltaGauge.height * 0.72 : deltaGauge.height * 0.42
                color: root.frame.primary
                opacity: 0.9
            }
        }
        Rectangle {
            z: 1
            visible: root.hasValue
            x: root.deltaIsGood ? deltaGauge.centerX : deltaGauge.centerX - width
            anchors.verticalCenter: parent.verticalCenter
            width: deltaGauge.width * 0.5 * deltaGauge.magnitude
            height: parent.height * 0.74
            color: root.deltaColor
            opacity: 0.92
        }
    }

    Label {
        visible: root.isDelta && root.isSpeed && (root.frame.widgetSettings.showUnit ?? true)
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.leftMargin: 12 * root.tileScale
        anchors.bottomMargin: 13 * root.tileScale
        text: root.frame.widgetSettings.unit || "km/h"
        color: root.frame.secondary
        font.family: root.frame.family
        font.pixelSize: Math.min(15 * root.frame.labelScale * root.tileScale,
                                 root.height * 0.14)
        font.weight: Font.DemiBold
    }

    Label {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.rightMargin: 12 * root.tileScale
        anchors.bottomMargin: 7 * root.tileScale
        text: root.isCurrent && !root.isSpeed && !root.hotlap && root.timing.state === "waiting"
            ? qsTr("READY")
            : root.isSpeed || root.isDelta
                ? root.formatNumber(root.metricValue) : root.formatTime(root.metricValue)
        // A finished hotlap keeps its result in the accent colour.
        color: root.hotlapFinished ? root.goodColor
            : root.hasValue || (root.isCurrent && root.timing.state === "waiting")
            ? root.frame.primary : root.frame.secondary
        font.family: root.frame.family
        font.pixelSize: root.isCurrent && !root.isSpeed && !root.hotlap && root.timing.state === "waiting"
            ? Math.min(31 * root.frame.valueScale * root.tileScale, root.height * 0.32)
            : Math.min(50 * root.frame.valueScale * root.tileScale, root.height * 0.46)
        font.weight: Font.Medium
    }
}
