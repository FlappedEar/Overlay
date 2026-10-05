import QtQuick
import ".."

// KAN-193: Tech G radar. Rings every Ring step, an amber dot and a fading
// trail of the last Trail seconds, read from the recording at fixed offsets
// so preview and export draw the same trail.
Item {
    id: root
    property var frame: parent.frame
    anchors.fill: parent
    readonly property var settings: frame.widgetSettings
    readonly property real maxG: Number(settings.maxG ?? 1.5) > 0 ? Number(settings.maxG ?? 1.5) : 1.5
    readonly property real ringStep: Number(settings.ringStepG ?? 0.5) > 0 ? Number(settings.ringStepG ?? 0.5) : 0.5
    readonly property real trailSeconds: Number(settings.trailSeconds ?? 1)
    readonly property int trailPoints: 14

    GForceData {
        id: gForce
        frame: root.frame
    }

    function sampleAgo(seconds) {
        const context = frame.renderContext;
        const lat = context.telemetryValueAgo(settings.lateralSource || "lateralAcceleration", seconds);
        const lon = context.telemetryValueAgo(settings.longitudinalSource || "longitudinalAcceleration", seconds);
        if (lat === undefined || lat === null || lon === undefined || lon === null
                || !Number.isFinite(Number(lat)) || !Number.isFinite(Number(lon)))
            return null;
        return [Number(lat) * ((settings.invertLateral ?? false) ? -1 : 1),
                Number(lon) * ((settings.invertLongitudinal ?? false) ? -1 : 1)];
    }

    Canvas {
        id: radar
        anchors.centerIn: parent
        width: Math.min(root.width, root.height)
        height: width
        property real time: root.frame.renderContext.time
        onTimeChanged: requestPaint()
        onWidthChanged: requestPaint()
        onPaint: {
            const ctx = getContext("2d");
            ctx.reset();
            const m = width / 2;
            const field = m * 0.92;
            const scale = field / root.maxG;
            ctx.globalAlpha = Number(root.settings.backgroundOpacity ?? 0.93);
            ctx.fillStyle = root.settings.radarBackgroundColor && root.settings.radarBackgroundColor !== "#16232d"
                ? root.settings.radarBackgroundColor : "#08090b";
            ctx.beginPath();
            ctx.arc(m, m, m, 0, Math.PI * 2);
            ctx.fill();
            ctx.globalAlpha = 1;
            ctx.strokeStyle = "rgba(255,255,255,0.16)";
            ctx.lineWidth = Math.max(1, width * 0.006);
            for (let g = root.ringStep; g <= root.maxG + 1e-6; g += root.ringStep) {
                ctx.beginPath();
                ctx.arc(m, m, g * scale, 0, Math.PI * 2);
                ctx.stroke();
            }
            if (root.settings.showCrosshair ?? true) {
                ctx.beginPath();
                ctx.moveTo(m, m - field);
                ctx.lineTo(m, m + field);
                ctx.moveTo(m - field, m);
                ctx.lineTo(m + field, m);
                ctx.stroke();
            }
            if ((root.settings.showRingLabels ?? true) && root.maxG >= 1) {
                ctx.fillStyle = "rgba(255,255,255,0.5)";
                ctx.font = "600 " + Math.max(7, width * 0.055) + "px 'Chakra Petch'";
                ctx.fillText("1.0g", m + scale * 0.72, m - scale * 0.72);
            }
            const place = (lat, lon) => {
                const g = Math.sqrt(lat * lat + lon * lon);
                const k = g > root.maxG ? root.maxG / g : 1;
                // As the Classic radar: braking up, acceleration down.
                return [m + lat * k * scale, m + lon * k * scale];
            };
            const dot = root.settings.dotColor && root.settings.dotColor !== "#f5a623" ? root.settings.dotColor : "#fcb203";
            if (root.trailSeconds > 0) {
                for (let i = root.trailPoints; i >= 1; --i) {
                    const sample = root.sampleAgo(root.trailSeconds * i / root.trailPoints);
                    if (!sample)
                        continue;
                    const fade = 1 - i / (root.trailPoints + 1);
                    const [x, y] = place(sample[0], sample[1]);
                    ctx.globalAlpha = fade * 0.5;
                    ctx.fillStyle = dot;
                    ctx.beginPath();
                    ctx.arc(x, y, width * (0.01 + 0.02 * fade), 0, Math.PI * 2);
                    ctx.fill();
                }
                ctx.globalAlpha = 1;
            }
            if (gForce.hasValue) {
                const [x, y] = place(gForce.lateral, gForce.longitudinal);
                ctx.fillStyle = dot;
                ctx.strokeStyle = "#1a1200";
                ctx.lineWidth = Math.max(1, width * 0.01);
                ctx.beginPath();
                ctx.arc(x, y, width * 0.04, 0, Math.PI * 2);
                ctx.fill();
                ctx.stroke();
            }
        }
    }
    Connections {
        target: frame.widgetModel
        ignoreUnknownSignals: true
        function onRevisionChanged() {
            radar.requestPaint();
        }
    }
}
