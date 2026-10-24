import QtQuick

// KAN-270: Tech steering wheel. The Tech panel with the label at the top left and the
// angle at the top right, like the throttle and brake rows, and a thin wheel under them.
// The angle is in degrees, positive = wheel turned left; QML rotates clockwise, so the
// wheel turns by -angle.
Item {
    id: root
    property var frame: parent.frame
    anchors.fill: parent

    readonly property var angle: frame.adjusted(frame.raw("source", "steeringAngle"))
    readonly property bool hasAngle: angle !== undefined && angle !== null && Number.isFinite(Number(angle))
    readonly property real wheelRotation: hasAngle ? -Number(angle) : 0
    readonly property color wheelColor: frame.widgetSettings.wheelColor || "#f4f4f4"
    readonly property color markerColor: "#fcb203"
    readonly property bool showLabel: frame.widgetSettings.showLabel ?? true
    readonly property bool showValue: frame.widgetSettings.showValue ?? true
    readonly property string valueText: {
        if (!hasAngle)
            return "—";
        const whole = Math.round(Math.abs(Number(angle)));
        return whole === 0 ? "0°" : whole + "° " + (Number(angle) > 0 ? "L" : "R");
    }

    TechPanel {
        id: panel
        anchors.fill: parent
        frame: root.frame
    }
    Item {
        id: content
        anchors.fill: parent
        anchors.margins: panel.innerPadding
        anchors.topMargin: panel.innerPadding * 1.2
        readonly property real rowHeight: height * 0.42

        TechLabel {
            visible: root.showLabel
            anchors.left: parent.left
            anchors.top: parent.top
            dim: true
            text: (root.frame.widgetSettings.label || "STEER").toUpperCase()
            font.pixelSize: content.rowHeight * 0.3
            font.letterSpacing: content.rowHeight * 0.03
        }
        TechLabel {
            visible: root.showValue
            anchors.right: parent.right
            anchors.top: parent.top
            text: root.valueText
            font.weight: Font.Bold
            font.pixelSize: content.rowHeight * 0.34
        }
        Item {
            id: stage
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: parent.height - content.rowHeight * 0.5
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
                    border.width: Math.max(2, wheel.width * 0.07)
                    border.color: root.wheelColor
                    antialiasing: true
                }
                Rectangle { // cross spoke, left to right
                    anchors.centerIn: parent
                    width: wheel.width * 0.9
                    height: wheel.width * 0.07
                    color: root.wheelColor
                }
                Rectangle { // lower spoke
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: wheel.height / 2
                    width: wheel.width * 0.07
                    height: wheel.height * 0.4
                    color: root.wheelColor
                }
                Rectangle { // hub
                    anchors.centerIn: parent
                    width: wheel.width * 0.2
                    height: width
                    radius: width / 2
                    color: root.wheelColor
                }
                Rectangle { // top mark: where "straight ahead" sits
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: 0
                    width: wheel.width * 0.09
                    height: wheel.width * 0.16
                    color: root.markerColor
                }
            }
        }
    }
}
