import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: tachometer
    property var frame: parent.frame
    anchors.fill: parent
    // A CSS font string needs a multi-word family quoted; unquoted, Canvas drops
    // the spaces ("HelveticaNeue") and falls back to its default font (KAN-139).
    readonly property string canvasFamily: "'" + String(frame.family).replace(/['"\\]/g, "") + "'"
    property var rawValue: frame.adjusted(frame.raw("source", "rpm"))
    property bool hasValue: rawValue !== undefined && rawValue !== null && Number.isFinite(Number(rawValue))
    property real value: hasValue ? Number(rawValue) : Number(frame.widgetSettings.minValue ?? 0)
    Canvas {
        id: retroTachometerCanvas
        anchors.fill: parent
        property real value: parent.value
        property bool hasValue: parent.hasValue
        onValueChanged: requestPaint()
        onHasValueChanged: requestPaint()
        onPaint: {
            const ctx = getContext("2d");
            ctx.reset();
            const settings = frame.widgetSettings;
            const cx = width / 2;
            const cy = height * 0.52;
            const radius = Math.min(width, height) * 0.45;
            const minimum = Number(settings.minValue ?? 0);
            const maximum = Math.max(minimum + 1, Number(settings.maxValue ?? 8000));
            const steps = Math.max(4, Math.min(16, Math.round((maximum - minimum) / 1000)));
            const progress = Math.max(0, Math.min(1, (value - minimum) / (maximum - minimum)));
            const warningValue = Number(settings.warningValue ?? 7500);
            const redlineStart = Math.max(0, Math.min(1, (warningValue - minimum) / (maximum - minimum)));
            const startAngle = Math.PI * 0.76;
            const sweep = Math.PI * 1.33;
            const endAngle = startAngle + sweep;
            const dialColor = settings.dialColor || "#f2f5f7";
            const rimColor = settings.rimColor || "#a6b3bf";
            const warningColor = settings.warningColor || "#e14b4b";

            // The face deliberately fills the widget and carries every visual
            // element, so the readout and analog scale read as one instrument.
            ctx.globalAlpha = Math.max(0.84, Number(settings.panelOpacity ?? 0.86));
            ctx.fillStyle = settings.panelColor || "#111a22";
            ctx.beginPath();
            ctx.arc(cx, cy, radius, 0, Math.PI * 2);
            ctx.fill();
            ctx.globalAlpha = 1;
            ctx.strokeStyle = rimColor;
            ctx.lineWidth = Math.max(1.5 * frame.sceneScale, radius * 0.018);
            ctx.beginPath();
            ctx.arc(cx, cy, radius, 0, Math.PI * 2);
            ctx.stroke();
            ctx.globalAlpha = 0.78;
            ctx.lineWidth = Math.max(1, frame.sceneScale * 0.8);
            ctx.beginPath();
            ctx.arc(cx, cy, radius * 0.955, 0, Math.PI * 2);
            ctx.stroke();
            ctx.globalAlpha = 1;
            ctx.strokeStyle = dialColor;
            ctx.fillStyle = dialColor;
            ctx.textAlign = "center";
            ctx.textBaseline = "middle";

            // A continuous redline unifies the red tick hierarchy at the high
            // end of the dial without competing with the white scale.
            ctx.strokeStyle = warningColor;
            ctx.lineWidth = Math.max(3 * frame.sceneScale, radius * 0.040);
            ctx.beginPath();
            ctx.arc(cx, cy, radius * 0.875, startAngle + sweep * redlineStart, endAngle);
            ctx.stroke();

            const tickCount = steps * 4;
            for (let tick = 0; tick <= tickCount; ++tick) {
                const ratio = tick / tickCount;
                const angle = startAngle + sweep * ratio;
                const major = tick % 4 === 0;
                const highRpm = ratio >= redlineStart;
                ctx.strokeStyle = highRpm ? warningColor : dialColor;
                ctx.fillStyle = ctx.strokeStyle;
                ctx.lineWidth = Math.max(major ? 2 * frame.sceneScale : frame.sceneScale,
                                         radius * (major ? 0.023 : 0.010));
                ctx.beginPath();
                ctx.moveTo(cx + Math.cos(angle) * radius * 0.91, cy + Math.sin(angle) * radius * 0.91);
                ctx.lineTo(cx + Math.cos(angle) * radius * (major ? 0.78 : 0.84),
                           cy + Math.sin(angle) * radius * (major ? 0.78 : 0.84));
                ctx.stroke();
                if (major) {
                    ctx.font = "700 " + (frame.configuredFontSize() > 0
                        ? frame.configuredFontSize() * frame.sceneScale
                        : Math.max(12 * frame.sceneScale, radius * 0.16)) + "px " + tachometer.canvasFamily;
                    ctx.fillText(String(Math.round((minimum + (maximum - minimum) * ratio) / 1000)),
                                 cx + Math.cos(angle) * radius * 0.64,
                                 cy + Math.sin(angle) * radius * 0.64);
                }
            }

            if (hasValue) {
                const angle = startAngle + sweep * progress;
                ctx.strokeStyle = settings.needleColor || "#e32636";
                ctx.lineWidth = Math.max(3 * frame.sceneScale, radius * 0.030);
                ctx.lineCap = "round";
                ctx.beginPath();
                ctx.moveTo(cx - Math.cos(angle) * radius * 0.13, cy - Math.sin(angle) * radius * 0.13);
                ctx.lineTo(cx + Math.cos(angle) * radius * 0.79, cy + Math.sin(angle) * radius * 0.79);
                ctx.stroke();
                ctx.lineCap = "butt";
            }

            // The compact two-line caption follows the reference hierarchy:
            // informative, but secondary to the analog face and readout.
            ctx.fillStyle = settings.secondaryTextColor || "#b5c0ca";
            ctx.font = "700 " + Math.max(9 * frame.sceneScale, radius * 0.105) + "px " + tachometer.canvasFamily;
            ctx.fillText(settings.label || "RPM", cx, cy - radius * 0.27);
            ctx.font = "600 " + Math.max(8 * frame.sceneScale, radius * 0.085) + "px " + tachometer.canvasFamily;
            ctx.fillText(settings.scaleLabel || "x1000", cx, cy - radius * 0.14);

            ctx.fillStyle = "#0d151b";
            ctx.beginPath();
            ctx.arc(cx, cy, Math.max(8 * frame.sceneScale, radius * 0.115), 0, Math.PI * 2);
            ctx.fill();
            ctx.globalAlpha = 0.48;
            ctx.strokeStyle = rimColor;
            ctx.lineWidth = Math.max(1, frame.sceneScale * 0.75);
            ctx.beginPath();
            ctx.arc(cx, cy, Math.max(8 * frame.sceneScale, radius * 0.115), 0, Math.PI * 2);
            ctx.stroke();
            ctx.globalAlpha = 1;

            ctx.fillStyle = dialColor;
            ctx.font = "700 " + Math.max(18 * frame.sceneScale, radius * 0.26) + "px " + tachometer.canvasFamily;
            ctx.fillText(hasValue ? Math.round(value).toString() : "—", cx, cy + radius * 0.63);
        }
    }
    Connections {
        target: frame.widgetModel
        ignoreUnknownSignals: true
        function onRevisionChanged() {
            retroTachometerCanvas.requestPaint();
        }
    }
}
