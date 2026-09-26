import QtQuick

// KAN-68/KAN-70: one recorded channel through the day. Every session is its
// own slot on a shared scale, so no line crosses a break between sessions; a
// null trace bin (no valid samples) leaves a hole. `entries` has one item per
// session: null when the session lacks the channel, otherwise
// {start, end, trace: [[time, value] | null], cooling: [{startTime, endTime}]}.
Canvas {
    id: chart
    property var entries: []
    property real low: 0
    property real high: 1
    property color lineColor: "#ff9b54"
    readonly property int slotGap: 10
    implicitHeight: 130
    onEntriesChanged: requestPaint()
    onLowChanged: requestPaint()
    onHighChanged: requestPaint()
    onWidthChanged: requestPaint()
    onAvailableChanged: if (available) requestPaint()
    onPaint: {
        const context = getContext("2d");
        context.reset();
        const count = entries.length;
        if (!count || high <= low) return;
        const top = 6, bottom = height - 20;
        const slot = (width - slotGap * (count - 1)) / count;
        const toY = value => bottom - (value - low) / (high - low) * (bottom - top);
        context.font = "10px sans-serif";
        context.textAlign = "center";
        for (let index = 0; index < count; ++index) {
            const left = index * (slot + slotGap);
            context.fillStyle = "#0b1119";
            context.fillRect(left, top, slot, bottom - top);
            context.fillStyle = "#91a0b2";
            context.fillText("S" + (index + 1), left + slot / 2, height - 5);
            const entry = entries[index];
            if (!entry) continue;
            const span = Math.max(1e-6, entry.end - entry.start);
            const toX = time => left + Math.max(0, Math.min(1, (time - entry.start) / span)) * slot;
            context.fillStyle = "rgba(88,160,255,0.22)";
            for (const interval of entry.cooling || [])
                context.fillRect(toX(interval.startTime), top, Math.max(2, toX(interval.endTime) - toX(interval.startTime)), bottom - top);
            context.strokeStyle = lineColor;
            context.lineWidth = 2;
            context.beginPath();
            let drawing = false;
            for (const point of entry.trace || []) {
                if (!point) { drawing = false; continue; } // a recording gap: no line
                const x = toX(point[0]), y = toY(point[1]);
                if (drawing) context.lineTo(x, y); else context.moveTo(x, y);
                drawing = true;
            }
            context.stroke();
        }
    }
}
