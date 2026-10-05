import QtQuick

// KAN-193: Tech RPM gauge. A thin 270° arc with a red zone from the warning
// value, the scale in thousands inside it, and speed in the middle.
Item {
    id: root
    property var frame: parent.frame
    anchors.fill: parent

    readonly property var settings: frame.widgetSettings
    readonly property var rpmRaw: frame.adjusted(frame.raw("source", "rpm"))
    readonly property bool hasRpm: rpmRaw !== undefined && rpmRaw !== null && Number.isFinite(Number(rpmRaw))
    readonly property real rpm: hasRpm ? Number(rpmRaw) : 0
    readonly property var speedRaw: {
        frame.renderContext.time;
        return frame.renderContext.telemetryValue(settings.speedSource || "speed");
    }
    readonly property bool hasSpeed: speedRaw !== undefined && speedRaw !== null && Number.isFinite(Number(speedRaw))
    readonly property bool mph: settings.speedUnit === "mph"
    readonly property bool showSpeed: settings.showSpeed ?? true
    readonly property real minimum: Number(settings.minValue ?? 0)
    readonly property real maximum: Math.max(minimum + 1, Number(settings.maxValue ?? 9000))
    readonly property real warning: Number(settings.warningValue ?? 7500)
    readonly property real side: Math.min(width, height)

    Canvas {
        id: gauge
        anchors.centerIn: parent
        width: root.side
        height: root.side
        property real value: root.rpm
        property bool hasValue: root.hasRpm
        onValueChanged: requestPaint()
        onHasValueChanged: requestPaint()
        onWidthChanged: requestPaint()
        onPaint: {
            const ctx = getContext("2d");
            ctx.reset();
            const c = width / 2;
            const r = width * 0.43;
            const line = Math.max(2, width * 0.02);
            const start = Math.PI * 0.75;
            const sweep = Math.PI * 1.5;
            const span = root.maximum - root.minimum;
            const ratio = v => Math.max(0, Math.min(1, (v - root.minimum) / span));
            const redFrom = ratio(root.warning);
            const red = root.settings.warningColor || "#ff4d3d";

            ctx.lineCap = "round";
            ctx.lineWidth = line;
            ctx.strokeStyle = "rgba(255,255,255,0.16)";
            ctx.beginPath();
            ctx.arc(c, c, r, start, start + sweep);
            ctx.stroke();
            ctx.lineCap = "butt";
            ctx.strokeStyle = red;
            ctx.globalAlpha = 0.5;
            ctx.beginPath();
            ctx.arc(c, c, r, start + sweep * redFrom, start + sweep);
            ctx.stroke();
            ctx.globalAlpha = 1;

            const steps = Math.max(2, Math.min(16, Math.round(span / 1000)));
            ctx.textAlign = "center";
            ctx.textBaseline = "middle";
            ctx.font = "600 " + Math.max(8, width * 0.052) + "px 'Chakra Petch'";
            for (let i = 0; i <= steps; ++i) {
                const t = i / steps;
                const a = start + sweep * t;
                const high = t >= redFrom;
                ctx.strokeStyle = high ? red : "rgba(255,255,255,0.6)";
                ctx.lineWidth = Math.max(1, width * 0.009);
                ctx.beginPath();
                ctx.moveTo(c + Math.cos(a) * r * 0.84, c + Math.sin(a) * r * 0.84);
                ctx.lineTo(c + Math.cos(a) * r * 0.9, c + Math.sin(a) * r * 0.9);
                ctx.stroke();
                ctx.fillStyle = "rgba(255,255,255,0.7)";
                ctx.fillText(String(Math.round((root.minimum + span * t) / 1000)),
                             c + Math.cos(a) * r * 0.72, c + Math.sin(a) * r * 0.72);
            }

            if (hasValue) {
                const p = ratio(value);
                ctx.lineCap = "round";
                ctx.lineWidth = line;
                ctx.strokeStyle = value >= root.warning ? red : "#f4f4f4";
                ctx.beginPath();
                ctx.arc(c, c, r, start, start + sweep * Math.max(0.001, p));
                ctx.stroke();
            }
        }
    }

    Column {
        anchors.centerIn: parent
        anchors.verticalCenterOffset: root.side * 0.04
        spacing: root.side * 0.012
        TechLabel {
            anchors.horizontalCenter: parent.horizontalCenter
            visible: root.showSpeed
            text: root.hasSpeed ? Math.round(Number(root.speedRaw) * (root.mph ? 0.621371 : 1)).toString() : "—"
            font.weight: Font.Bold
            font.pixelSize: root.side * 0.25
        }
        TechLabel {
            anchors.horizontalCenter: parent.horizontalCenter
            visible: root.showSpeed
            dim: true
            text: root.mph ? "MPH" : "KM/H"
            font.pixelSize: root.side * 0.055
            font.letterSpacing: root.side * 0.008
        }
        TechLabel {
            anchors.horizontalCenter: parent.horizontalCenter
            dim: root.showSpeed
            text: (root.hasRpm ? Math.round(root.rpm).toString() : "—") + " " + (root.settings.label || "RPM")
            font.pixelSize: root.showSpeed ? root.side * 0.065 : root.side * 0.12
        }
    }

    Connections {
        target: frame.widgetModel
        ignoreUnknownSignals: true
        function onRevisionChanged() {
            gauge.requestPaint();
        }
    }
}
