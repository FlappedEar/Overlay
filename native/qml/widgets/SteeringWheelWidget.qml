import QtQuick
import QtQuick.Controls

// KAN-269: the driver's steering wheel angle. The angle is in degrees, positive =
// wheel turned left (anticlockwise as the driver sees it). QML's rotation is
// clockwise for positive values, so the wheel turns by -angle.
Item {
    id: root
    property var frame: parent.frame
    anchors.fill: parent

    readonly property var angle: frame.adjusted(frame.raw("source", "steeringAngle"))
    readonly property bool hasAngle: angle !== undefined && angle !== null && Number.isFinite(Number(angle))
    readonly property real wheelRotation: hasAngle ? -Number(angle) : 0
    readonly property color wheelColor: frame.widgetSettings.wheelColor || frame.primary
    readonly property color markerColor: frame.widgetSettings.accentColor || "#e14b4b"
    readonly property bool showLabel: frame.widgetSettings.showLabel ?? true
    readonly property bool showValue: frame.widgetSettings.showValue ?? true

    // Whole degrees with the side: "90° L", "45° R", "0°".
    readonly property string valueText: {
        if (!hasAngle)
            return "—";
        const whole = Math.round(Math.abs(Number(angle)));
        return whole === 0 ? "0°" : whole + "° " + (Number(angle) > 0 ? "L" : "R");
    }

    TelemetryPanel {
        anchors.fill: parent
        frame: root.frame
        accentColor: root.markerColor
    }
    Column {
        anchors.fill: parent
        anchors.margins: root.frame.pad
        spacing: 2 * frame.sceneScale
        Label {
            visible: root.showLabel
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: frame.widgetSettings.label || "STEER"
            color: frame.secondary
            font.family: frame.family
            font.weight: Font.DemiBold
            font.pixelSize: Math.max(8, 13 * frame.labelScale * frame.sceneScale)
            font.letterSpacing: 0.7 * frame.sceneScale
        }
        Item {
            id: stage
            width: parent.width
            height: parent.height - (root.showLabel ? labelAllowance : 0) - (root.showValue ? valueAllowance : 0)
            readonly property real labelAllowance: 18 * frame.labelScale * frame.sceneScale
            readonly property real valueAllowance: 34 * frame.valueScale * frame.sceneScale
            readonly property real size: Math.max(0, Math.min(width, height))

            Item {
                id: wheel
                anchors.centerIn: parent
                width: stage.size
                height: stage.size
                rotation: root.wheelRotation
                antialiasing: true

                Rectangle { // rim
                    anchors.fill: parent
                    radius: width / 2
                    color: "transparent"
                    border.width: Math.max(2, wheel.width * 0.09)
                    border.color: root.wheelColor
                    antialiasing: true
                }
                Rectangle { // cross spoke, left to right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: wheel.width * 0.86
                    height: wheel.width * 0.09
                    radius: height / 2
                    color: root.wheelColor
                }
                Rectangle { // lower spoke
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: wheel.height / 2
                    width: wheel.width * 0.09
                    height: wheel.height * 0.40
                    radius: width / 2
                    color: root.wheelColor
                }
                Rectangle { // hub
                    anchors.centerIn: parent
                    width: wheel.width * 0.22
                    height: width
                    radius: width / 2
                    color: root.wheelColor
                }
                Rectangle { // top mark: where "straight ahead" sits
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: 0
                    width: wheel.width * 0.07
                    height: wheel.width * 0.14
                    radius: width / 2
                    color: root.markerColor
                }
            }
        }
        Label {
            visible: root.showValue
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: root.valueText
            color: frame.primary
            font.family: frame.family
            font.weight: frame.weight
            font.pixelSize: frame.configuredFontSize() > 0
                ? frame.configuredFontSize() * frame.sceneScale
                : Math.max(12, 26 * frame.valueScale * frame.sceneScale)
        }
    }
}
