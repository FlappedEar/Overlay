import QtQuick

// KAN-193: Tech lap tile. Lap number in amber, the running time, and the best
// completed lap so far. Hotlap mode behaves as in the Classic tile.
Item {
    id: root
    property var frame: parent.frame
    anchors.fill: parent

    property var timing: {
        frame.renderContext.time;
        return frame.renderContext.lapTiming;
    }
    readonly property bool hotlap: frame.widgetSettings.hotlapMode ?? false
    readonly property var hotlapTiming: {
        frame.renderContext.time;
        return root.hotlap ? frame.renderContext.fixedLapTiming(Number(frame.widgetSettings.hotlapLap ?? 0)) : ({});
    }
    readonly property bool hotlapFinished: hotlap && hotlapTiming.state === "finished"
    readonly property int decimals: Number(frame.widgetSettings.timingDecimals ?? 2)
    readonly property real elapsed: Number(hotlap ? hotlapTiming.elapsedSeconds : timing.currentElapsedSeconds)
    readonly property bool waiting: !hotlap && timing.state === "waiting"
    readonly property var lapNumber: hotlap ? hotlapTiming.lapNumber : timing.currentLapNumber
    readonly property real best: Number(timing.bestLapSeconds)
    // Optional live delta to the best completed lap (KAN-255): green ahead, orange behind.
    readonly property real delta: Number(timing.liveDeltaSeconds)
    readonly property bool showDelta: (frame.widgetSettings.showDelta ?? false) && !hotlap && Number.isFinite(delta)

    function formatTime(seconds) {
        const value = Number(seconds);
        if (!Number.isFinite(value) || value < 0)
            return "—:—." + "—".repeat(Math.max(1, decimals));
        return frame.renderContext.formatLapTime(value, decimals);
    }

    TechPanel {
        id: panel
        anchors.fill: parent
        frame: root.frame
    }
    TechLabel {
        x: panel.innerPadding
        y: panel.innerPadding * 1.2
        dim: true
        text: (frame.widgetSettings.label && frame.widgetSettings.label !== "Current" ? frame.widgetSettings.label : "Lap").toUpperCase()
        font.pixelSize: root.height * 0.1
        font.letterSpacing: root.height * 0.012
    }
    TechLabel {
        anchors.right: parent.right
        anchors.rightMargin: panel.innerPadding * 1.4
        y: panel.innerPadding
        color: "#fcb203"
        text: Number.isFinite(Number(root.lapNumber)) ? String(root.lapNumber) : "—"
        font.weight: Font.Bold
        font.pixelSize: root.height * 0.15
    }
    TechLabel {
        x: panel.innerPadding
        anchors.verticalCenter: parent.verticalCenter
        anchors.verticalCenterOffset: root.height * 0.03
        width: parent.width - 2 * panel.innerPadding
        text: root.waiting ? qsTr("READY") : root.formatTime(root.elapsed)
        color: root.hotlapFinished ? (frame.widgetSettings.accentColor || "#4fd17a") : "#f4f4f4"
        font.weight: Font.Bold
        font.pixelSize: Math.min(root.height * 0.3, width / 4.6)
    }
    TechLabel {
        visible: (frame.widgetSettings.showBest ?? true) && !root.hotlap
        x: panel.innerPadding
        anchors.bottom: parent.bottom
        anchors.bottomMargin: panel.innerPadding * 0.9
        dim: true
        textFormat: Text.StyledText
        text: "BEST <font color=\"#f4f4f4\">" + (Number.isFinite(root.best) ? root.formatTime(root.best) : "—") + "</font>"
        font.pixelSize: root.height * 0.1
    }
    TechLabel {
        objectName: "lapDelta"
        visible: root.showDelta
        anchors.right: parent.right
        anchors.rightMargin: panel.innerPadding * 1.4
        anchors.bottom: parent.bottom
        anchors.bottomMargin: panel.innerPadding * 0.9
        text: frame.renderContext.formatLapDelta(root.delta, root.decimals)
        color: root.delta <= 0 ? "#4fd17a" : "#ff9f1a"
        font.weight: Font.Bold
        font.pixelSize: root.height * 0.13
    }
}
