import QtQuick
import QtQuick.Controls

// The Current lap time tile: the running lap time and lap number, READY
// before the first start-line crossing. Hotlap mode shows one chosen lap.
Item {
    id: root

    property var frame: parent.frame
    property var timing: {
        frame.renderContext.time;
        return frame.renderContext.lapTiming;
    }

    // Hotlap mode: one chosen lap, 0:00 before its start-line crossing,
    // running during it, and held at its final time after.
    readonly property bool hotlap: frame.widgetSettings.hotlapMode ?? false
    readonly property var hotlapTiming: {
        frame.renderContext.time;
        return root.hotlap ? frame.renderContext.fixedLapTiming(Number(frame.widgetSettings.hotlapLap ?? 0)) : ({});
    }
    readonly property bool hotlapFinished: root.hotlap && root.hotlapTiming.state === "finished"
    readonly property real tileScale: frame.sceneScale * frame.widgetScale
    readonly property int decimals: Number(frame.widgetSettings.timingDecimals ?? 2)
    readonly property real metricValue: Number(hotlap ? hotlapTiming.elapsedSeconds : timing.currentElapsedSeconds)
    readonly property bool hasValue: Number.isFinite(metricValue)
    readonly property bool waiting: !hotlap && timing.state === "waiting"
    readonly property color goodColor: frame.widgetSettings.accentColor || "#20d05a"
    readonly property var lapNumber: hotlap ? hotlapTiming.lapNumber : timing.currentLapNumber
    // Optional live delta to the best completed lap (KAN-255): green ahead, orange behind.
    readonly property real delta: Number(timing.liveDeltaSeconds)
    readonly property bool showDelta: (frame.widgetSettings.showDelta ?? false) && !hotlap && Number.isFinite(delta)

    function formatTime(seconds) {
        const value = Number(seconds);
        if (!Number.isFinite(value) || value < 0)
            return "—:—." + "—".repeat(Math.max(1, decimals));
        // Rounded before minutes are split, in C++ (KAN-149).
        return frame.renderContext.formatLapTime(value, decimals);
    }

    TelemetryPanel {
        anchors.fill: parent
        frame: root.frame
        borderWidth: Number(root.frame.widgetSettings.borderWidth ?? 1) * root.tileScale
        cornerRadius: Number(root.frame.widgetSettings.cornerRadius ?? 12) * root.tileScale
    }

    Label {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.leftMargin: 12 * root.tileScale
        anchors.topMargin: 7 * root.tileScale
        text: root.frame.widgetSettings.label || qsTr("Current")
        color: root.frame.primary
        font.family: root.frame.family
        font.pixelSize: Math.min(24 * root.frame.labelScale * root.tileScale,
                                 root.height * 0.22)
        font.weight: Font.Medium
    }

    Label {
        objectName: "lapDelta"
        visible: root.showDelta
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.rightMargin: 12 * root.tileScale
        anchors.topMargin: 7 * root.tileScale
        text: frame.renderContext.formatLapDelta(root.delta, root.decimals)
        color: root.delta <= 0 ? "#20d05a" : "#ff9f1a"
        font.family: root.frame.family
        font.pixelSize: Math.min(24 * root.frame.labelScale * root.tileScale, root.height * 0.22)
        font.weight: Font.DemiBold
    }

    Label {
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

    Label {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.rightMargin: 12 * root.tileScale
        anchors.bottomMargin: 7 * root.tileScale
        text: root.waiting ? qsTr("READY") : root.formatTime(root.metricValue)
        // A finished hotlap keeps its result in the accent colour.
        color: root.hotlapFinished ? root.goodColor
            : root.hasValue || root.waiting ? root.frame.primary : root.frame.secondary
        font.family: root.frame.family
        font.pixelSize: root.waiting
            ? Math.min(31 * root.frame.valueScale * root.tileScale, root.height * 0.32)
            : Math.min(50 * root.frame.valueScale * root.tileScale, root.height * 0.46)
        font.weight: Font.Medium
    }
}
