import QtQuick
import QtQuick.Controls
import "TrackMapDefaults.js" as Defaults

// KAN-271: the track map. The recording's GPS outline with the car and the lap so far, either
// the whole track or a window around the car. The drawing is TrackMapCore.qml; the Tech style
// has its own plate (tech/TechTrackMap.qml).
Item {
    id: root
    property var frame: parent.frame
    anchors.fill: parent

    readonly property bool showLabel: frame.widgetSettings.showLabel ?? true
    readonly property bool windowMode: String(frame.widgetSettings.mapMode || "whole") === "window"
    readonly property color trailColor: frame.widgetSettings.trailColor || Defaults.trailColor(false)

    TelemetryPanel {
        anchors.fill: parent
        frame: root.frame
        accentColor: root.trailColor
    }
    Label {
        id: title
        visible: root.showLabel
        x: root.frame.pad
        y: root.frame.pad * 0.8
        text: (frame.widgetSettings.label || "TRACK")
            + (root.windowMode ? " · " + Math.round(Number(frame.widgetSettings.windowMeters ?? 300)) + " m" : "")
        color: frame.secondary
        font.family: frame.family
        font.weight: Font.DemiBold
        font.pixelSize: Math.max(8, 13 * frame.labelScale * frame.sceneScale)
        font.letterSpacing: 0.7 * frame.sceneScale
    }
    TrackMapCore {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.top: root.showLabel ? title.bottom : parent.top
        anchors.margins: root.frame.pad
        frame: root.frame
        tech: false
    }
}
