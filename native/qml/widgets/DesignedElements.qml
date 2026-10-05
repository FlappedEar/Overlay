import QtQuick

// The elements of a designed widget (KAN-191), drawn into this item's box.
// Shared by the scene renderer (DesignedWidget.qml, preview and export) and
// the widget editor's canvas, so the editor shows exactly what exports.
Item {
    id: root

    property var elements: []
    property var renderContext: null
    property string fontFamily: "Helvetica Neue"
    property int fontWeight: 600
    // Item pixels per scene pixel, for corner radii given in scene pixels.
    property real pixelScale: 1
    // The lap timing snapshot at the render time; read once per frame.
    readonly property var timing: {
        if (!root.renderContext)
            return ({});
        root.renderContext.time;
        return root.renderContext.lapTiming;
    }

    function channelValue(element) {
        if (!root.renderContext || !element.source)
            return undefined;
        root.renderContext.time;
        const raw = root.renderContext.telemetryValue(element.source);
        if (raw === undefined || raw === null)
            return undefined;
        const value = Number(raw) * Number(element.multiplier ?? 1) + Number(element.valueOffset ?? 0);
        return Number.isFinite(value) ? value : undefined;
    }

    function signedText(value, decimals) {
        const zeroThreshold = 0.5 * Math.pow(10, -decimals);
        const rounded = Math.abs(value) < zeroThreshold ? 0 : value;
        return (rounded > 0 ? "+" : "") + rounded.toFixed(decimals);
    }

    function lapValue(element) {
        const field = element.lapField || "current";
        const timing = root.timing || ({});
        switch (field) {
        case "best": return timing.bestLapSeconds;
        case "last": return timing.lastLapSeconds;
        case "delta": return timing.liveDeltaSeconds;
        case "lastDelta": return timing.lastDeltaToBestSeconds;
        case "lapNumber": return timing.currentLapNumber;
        case "bestLapNumber": return timing.bestLapNumber;
        default: return timing.currentElapsedSeconds;
        }
    }

    function elementText(element) {
        const fallback = element.fallbackText ?? "—";
        const decimals = Number(element.decimals ?? 0);
        if (element.kind === "text")
            return element.text ?? "";
        if (element.kind === "value") {
            const value = root.channelValue(element);
            return value === undefined ? fallback
                : (element.prefix || "") + value.toFixed(decimals) + (element.suffix || "");
        }
        if (element.kind === "lap") {
            const value = Number(root.lapValue(element));
            if (!Number.isFinite(value))
                return fallback;
            const field = element.lapField || "current";
            let body;
            if (field === "lapNumber" || field === "bestLapNumber")
                body = String(Math.round(value));
            else if (field === "delta" || field === "lastDelta")
                body = root.signedText(value, decimals);
            else
                body = value < 0 ? fallback : root.renderContext.formatLapTime(value, decimals);
            return (element.prefix || "") + body + (element.suffix || "");
        }
        return "";
    }

    function textColor(element) {
        if (element.kind === "lap" && (element.colorBySign ?? true)
            && (element.lapField === "delta" || element.lapField === "lastDelta")) {
            const value = Number(root.lapValue(element));
            if (Number.isFinite(value) && Math.abs(value) >= 0.5 * Math.pow(10, -Number(element.decimals ?? 0)))
                return value < 0 ? element.gainColor || "#20d05a" : element.lossColor || "#ef4f5f";
        }
        return element.color || "#f2f5f7";
    }

    function barFraction(element) {
        const value = root.channelValue(element);
        if (value === undefined)
            return 0;
        const minimum = Number(element.minValue ?? 0);
        const maximum = Number(element.maxValue ?? 100);
        return maximum > minimum ? Math.max(0, Math.min(1, (value - minimum) / (maximum - minimum))) : 0;
    }

    Repeater {
        model: root.elements || []

        Item {
            id: elementItem
            required property var modelData
            readonly property var element: modelData
            x: Number(element.x ?? 0) * root.width
            y: Number(element.y ?? 0) * root.height
            width: Number(element.w ?? 0.5) * root.width
            height: Number(element.h ?? 0.3) * root.height
            visible: element.visible ?? true
            opacity: Number(element.opacity ?? 1)

            Rectangle {
                visible: elementItem.element.kind === "shape"
                anchors.fill: parent
                color: elementItem.element.fillColor || "#16232d"
                radius: Math.min(Math.min(width, height) / 2, Number(elementItem.element.radius ?? 0) * root.pixelScale)
            }

            Rectangle {
                id: barTrack
                visible: elementItem.element.kind === "bar"
                anchors.fill: parent
                color: elementItem.element.trackColor || "#2b3a46"
                radius: Math.min(Math.min(width, height) / 2, Number(elementItem.element.radius ?? 0) * root.pixelScale)
                readonly property bool vertical: elementItem.element.orientation === "vertical"
                readonly property real fraction: {
                    root.renderContext ? root.renderContext.time : 0;
                    return elementItem.element.kind === "bar" ? root.barFraction(elementItem.element) : 0;
                }
                Rectangle {
                    x: 0
                    y: barTrack.vertical ? parent.height * (1 - barTrack.fraction) : 0
                    width: barTrack.vertical ? parent.width : parent.width * barTrack.fraction
                    height: barTrack.vertical ? parent.height * barTrack.fraction : parent.height
                    radius: barTrack.radius
                    color: elementItem.element.fillColor || "#55d76a"
                }
            }

            Text {
                visible: elementItem.element.kind === "text" || elementItem.element.kind === "value"
                    || elementItem.element.kind === "lap"
                anchors.fill: parent
                text: {
                    root.renderContext ? root.renderContext.time : 0;
                    return visible ? root.elementText(elementItem.element) : "";
                }
                color: {
                    root.renderContext ? root.renderContext.time : 0;
                    return root.textColor(elementItem.element);
                }
                font.family: root.fontFamily
                font.weight: (elementItem.element.bold ?? true) ? Math.max(root.fontWeight, Font.DemiBold) : Font.Normal
                font.features: { "tnum": 1 }
                font.pixelSize: Math.max(1, height * Number(elementItem.element.fontScale ?? 0.75))
                fontSizeMode: Text.HorizontalFit
                minimumPixelSize: 4
                horizontalAlignment: elementItem.element.align === "left" ? Text.AlignLeft
                    : elementItem.element.align === "right" ? Text.AlignRight : Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideNone
            }
        }
    }
}
