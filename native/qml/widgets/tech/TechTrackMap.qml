import QtQuick
import ".."

// KAN-271: Tech track map. The Tech panel with the label at the top left and the lap number at
// the top right, and the map (TrackMapCore.qml) under them.
Item {
    id: root
    property var frame: parent.frame
    anchors.fill: parent

    readonly property bool showLabel: frame.widgetSettings.showLabel ?? true
    readonly property bool windowMode: String(frame.widgetSettings.mapMode || "whole") === "window"
    readonly property var timing: {
        frame.renderContext.time;
        return frame.renderContext.lapTiming;
    }
    readonly property string lapText: timing.available && timing.currentLapNumber !== undefined
        ? "LAP " + timing.currentLapNumber : ""
    readonly property string labelText: (frame.widgetSettings.label || "TRACK").toUpperCase()
        + (windowMode ? " · " + Math.round(Number(frame.widgetSettings.windowMeters ?? 300)) + " M" : "")

    TechPanel {
        id: panel
        anchors.fill: parent
        frame: root.frame
    }
    TechLabel {
        id: title
        visible: root.showLabel
        x: panel.innerPadding
        y: panel.innerPadding * 1.2
        dim: true
        text: root.labelText
        font.pixelSize: root.height * 0.055
        font.letterSpacing: root.height * 0.006
    }
    TechLabel {
        visible: root.showLabel && root.lapText !== ""
        anchors.right: parent.right
        anchors.rightMargin: panel.innerPadding + panel.cut * 0.6
        y: panel.innerPadding * 1.2
        text: root.lapText
        font.weight: Font.Bold
        font.pixelSize: root.height * 0.065
    }
    TrackMapCore {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.top: root.showLabel ? title.bottom : parent.top
        anchors.margins: panel.innerPadding
        anchors.topMargin: root.showLabel ? panel.innerPadding * 0.4 : panel.innerPadding
        frame: root.frame
        tech: true
    }
}
