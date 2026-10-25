import QtQuick
import "TrackMapDefaults.js" as Defaults

// KAN-271: the drawing of the track map widget, shared by the Classic and the Tech renderers.
//
// Whole track: the recording's GPS outline fitted into the area. The outline is a static
// layer: it repaints only with the geometry, the size or a setting, never with playback time.
// The car dot, the lap trail and the start/finish tick repaint above it.
//
// Part of the track: a window of `windowMeters` around the car, the outline fading out towards
// its edge. The map moves under the car, so both layers repaint with the car's position.
//
// The map is drawn in metres. The track geometry is normalized by its longest side
// (renderContext.trackScaleMeters), so every point is multiplied by that scale first.
Item {
    id: root

    property var frame: parent.frame
    property bool tech: false

    readonly property var settings: frame.widgetSettings
    readonly property var rc: frame.renderContext
    readonly property bool windowMode: String(settings.mapMode || "whole") === "window"
    readonly property real windowMeters: Math.max(100, Math.min(2000, Number(settings.windowMeters ?? 300)))
    readonly property bool headingUp: windowMode && String(settings.orientation || "north") === "heading"
    readonly property bool showTrail: settings.showTrail ?? true
    readonly property bool showStartLine: settings.showStartLine ?? true
    // A widget setting wins over the style's colours.
    readonly property color trailColor: settings.trailColor || Defaults.trailColor(tech)
    readonly property color baseColor: settings.lineColor || Defaults.lineColor(tech, windowMode)
    readonly property real side: Math.max(1, Math.min(width, height))
    readonly property real lineWidth: Math.max(2, side * (windowMode ? 0.022 : 0.014))

    // The outline in metres: xs/ys with NaN where the line is open (a GPS gap or a jump), its
    // bounds, and a grid of segment indices for the window's lookups. Rebuilt with the geometry.
    readonly property var outline: {
        rc.trackRevision;
        return buildOutline(rc.trackPoints, rc.trackScaleMeters);
    }
    readonly property bool hasOutline: outline.count > 1

    // Time-dependent inputs, read once per frame and shared by both layers.
    readonly property var trail: {
        rc.time;
        return rc.trackTrail();
    }
    readonly property var startLine: {
        rc.trackRevision;
        return rc.trackStartLine();
    }
    readonly property var car: {
        const metres = rc.trackScaleMeters;
        for (let i = trail.length - 1; i >= 0; --i)
            if (trail[i])
                return { x: trail[i].x * metres, y: trail[i].y * metres };
        return null;
    }
    // Where the line of travel points, from the last 8 m of the trail; null while standing still,
    // when the map stays north-up.
    readonly property var heading: {
        const metres = rc.trackScaleMeters;
        let walked = 0;
        let last = null;
        for (let i = trail.length - 1; i >= 0; --i) {
            const p = trail[i];
            if (!p)
                break;
            if (last === null) {
                last = p;
                continue;
            }
            walked = Math.hypot((last.x - p.x) * metres, (last.y - p.y) * metres);
            if (walked >= 8)
                return { x: last.x - p.x, y: last.y - p.y };
        }
        return null;
    }

    // The view: the world point at the centre of the area, metres per pixel and rotation.
    readonly property bool windowActive: windowMode && car !== null
    readonly property real mapRotation: windowActive && headingUp && heading
        ? -Math.PI / 2 - Math.atan2(heading.y, heading.x) : 0
    readonly property real viewScale: {
        if (windowActive)
            return side / windowMeters;
        if (!hasOutline)
            return 1;
        const b = outline.bounds;
        return Math.min(width / Math.max(1, b.maxX - b.minX), height / Math.max(1, b.maxY - b.minY)) * 0.94;
    }
    readonly property real viewX: windowActive ? car.x : hasOutline ? (outline.bounds.minX + outline.bounds.maxX) / 2 : 0
    readonly property real viewY: windowActive ? car.y : hasOutline ? (outline.bounds.minY + outline.bounds.maxY) / 2 : 0

    function buildOutline(points, metres) {
        const xs = [];
        const ys = [];
        const bounds = { minX: Infinity, minY: Infinity, maxX: -Infinity, maxY: -Infinity };
        const cell = 120;
        const grid = {};
        let lastX = NaN;
        let lastY = NaN;
        let count = 0;
        const open = () => {
            if (count > 0 && !Number.isNaN(xs[count - 1])) {
                xs.push(NaN);
                ys.push(NaN);
                count++;
            }
        };
        for (let i = 0; i < points.length; ++i) {
            const x = points[i].x * metres;
            const y = points[i].y * metres;
            if (!Number.isFinite(x) || !Number.isFinite(y))
                continue;
            if (!Number.isNaN(lastX)) {
                const step = Math.hypot(x - lastX, y - lastY);
                if (step < 2)
                    continue;
                if (step > 80)
                    open();
            }
            xs.push(x);
            ys.push(y);
            if (x < bounds.minX) bounds.minX = x;
            if (x > bounds.maxX) bounds.maxX = x;
            if (y < bounds.minY) bounds.minY = y;
            if (y > bounds.maxY) bounds.maxY = y;
            if (count > 0 && !Number.isNaN(xs[count - 1])) {
                const key = Math.floor(xs[count - 1] / cell) + "," + Math.floor(ys[count - 1] / cell);
                (grid[key] || (grid[key] = [])).push(count - 1);
            }
            count++;
            lastX = x;
            lastY = y;
        }
        return { xs: xs, ys: ys, count: count, bounds: bounds, grid: grid, cell: cell };
    }

    // World point to pixel.
    function px(x, y) {
        const dx = x - viewX;
        const dy = y - viewY;
        const c = Math.cos(mapRotation);
        const s = Math.sin(mapRotation);
        return [width / 2 + (dx * c - dy * s) * viewScale, height / 2 + (dx * s + dy * c) * viewScale];
    }

    // The segments of the outline within `radius` metres of the car, by index.
    function nearSegments(radius) {
        const o = outline;
        const found = [];
        const first = Math.floor((car.x - radius) / o.cell) - 1;
        const last = Math.floor((car.x + radius) / o.cell) + 1;
        const top = Math.floor((car.y - radius) / o.cell) - 1;
        const bottom = Math.floor((car.y + radius) / o.cell) + 1;
        for (let gx = first; gx <= last; ++gx)
            for (let gy = top; gy <= bottom; ++gy) {
                const list = o.grid[gx + "," + gy];
                if (list)
                    for (let k = 0; k < list.length; ++k)
                        found.push(list[k]);
            }
        return found;
    }

    Canvas {
        id: outlineLayer
        objectName: "trackMapOutline"
        anchors.fill: parent
        // Whole track: only geometry, size and settings repaint it. Window: the car moves the map.
        property real stamp: root.windowActive ? root.viewX * 7 + root.viewY * 13 + root.mapRotation * 1000 : 0
        onStampChanged: requestPaint()
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        Connections {
            target: root
            function onOutlineChanged() { outlineLayer.requestPaint(); }
            function onWindowActiveChanged() { outlineLayer.requestPaint(); }
            function onWindowMetersChanged() { outlineLayer.requestPaint(); }
            function onBaseColorChanged() { outlineLayer.requestPaint(); }
            function onLineWidthChanged() { outlineLayer.requestPaint(); }
            function onViewScaleChanged() { outlineLayer.requestPaint(); }
        }
        onPaint: {
            const ctx = getContext("2d");
            ctx.reset();
            if (!root.hasOutline)
                return;
            const o = root.outline;
            ctx.lineCap = "round";
            ctx.lineJoin = "round";
            if (!root.windowActive) {
                const path = () => {
                    ctx.beginPath();
                    let drawing = false;
                    for (let i = 0; i < o.count; ++i) {
                        if (Number.isNaN(o.xs[i])) {
                            drawing = false;
                            continue;
                        }
                        const p = root.px(o.xs[i], o.ys[i]);
                        if (drawing)
                            ctx.lineTo(p[0], p[1]);
                        else
                            ctx.moveTo(p[0], p[1]);
                        drawing = true;
                    }
                };
                ctx.lineWidth = root.lineWidth + 3;
                ctx.strokeStyle = "rgba(0,0,0,0.5)";
                path();
                ctx.stroke();
                ctx.lineWidth = root.lineWidth;
                ctx.strokeStyle = root.baseColor;
                path();
                ctx.stroke();
                return;
            }
            // Window: the segments around the car in one stroke, then faded towards the edge by
            // a radial mask centred on the car (a mask, not per-segment alpha, so joins do not
            // show).
            const radius = root.windowMeters * 0.75;
            const near = root.nearSegments(radius);
            ctx.lineWidth = root.lineWidth;
            ctx.strokeStyle = root.baseColor;
            ctx.beginPath();
            for (let k = 0; k < near.length; ++k) {
                const i = near[k];
                if (Number.isNaN(o.xs[i + 1]))
                    continue;
                const a = root.px(o.xs[i], o.ys[i]);
                const c = root.px(o.xs[i + 1], o.ys[i + 1]);
                ctx.moveTo(a[0], a[1]);
                ctx.lineTo(c[0], c[1]);
            }
            ctx.stroke();
            const reach = radius * root.viewScale;
            const fade = ctx.createRadialGradient(width / 2, height / 2, reach * 0.35, width / 2, height / 2, reach);
            fade.addColorStop(0, "rgba(0,0,0,1)");
            fade.addColorStop(1, "rgba(0,0,0,0.08)");
            ctx.globalCompositeOperation = "destination-in";
            ctx.fillStyle = fade;
            ctx.fillRect(0, 0, width, height);
            ctx.globalCompositeOperation = "source-over";
        }
    }

    Canvas {
        id: liveLayer
        objectName: "trackMapLive"
        anchors.fill: parent
        property var trail: root.trail
        property var car: root.car
        onTrailChanged: requestPaint()
        onCarChanged: requestPaint()
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        Connections {
            target: root
            function onViewScaleChanged() { liveLayer.requestPaint(); }
            function onMapRotationChanged() { liveLayer.requestPaint(); }
            function onStartLineChanged() { liveLayer.requestPaint(); }
            function onTrailColorChanged() { liveLayer.requestPaint(); }
            function onShowTrailChanged() { liveLayer.requestPaint(); }
            function onShowStartLineChanged() { liveLayer.requestPaint(); }
        }
        onPaint: {
            const ctx = getContext("2d");
            ctx.reset();
            if (!root.hasOutline)
                return;
            const metres = root.rc.trackScaleMeters;
            ctx.lineCap = "round";
            ctx.lineJoin = "round";

            const line = root.startLine;
            if (root.showStartLine && line && line.ax !== undefined) {
                const a = root.px(line.ax * metres, line.ay * metres);
                const b = root.px(line.bx * metres, line.by * metres);
                const length = Math.hypot(b[0] - a[0], b[1] - a[1]);
                if (length > 0) {
                    const reach = root.side * 0.03;
                    const mx = (a[0] + b[0]) / 2;
                    const my = (a[1] + b[1]) / 2;
                    const ux = (b[0] - a[0]) / length * reach;
                    const uy = (b[1] - a[1]) / length * reach;
                    ctx.setLineDash([3, 3]);
                    ctx.lineWidth = Math.max(2, root.side * 0.01);
                    ctx.strokeStyle = "#f4f4f4";
                    ctx.beginPath();
                    ctx.moveTo(mx - ux, my - uy);
                    ctx.lineTo(mx + ux, my + uy);
                    ctx.stroke();
                    ctx.setLineDash([]);
                }
            }

            if (root.showTrail && trail.length > 1) {
                ctx.lineWidth = root.lineWidth;
                ctx.strokeStyle = root.trailColor;
                ctx.beginPath();
                let drawing = false;
                for (let i = 0; i < trail.length; ++i) {
                    if (!trail[i]) {
                        drawing = false;
                        continue;
                    }
                    const p = root.px(trail[i].x * metres, trail[i].y * metres);
                    if (drawing)
                        ctx.lineTo(p[0], p[1]);
                    else
                        ctx.moveTo(p[0], p[1]);
                    drawing = true;
                }
                ctx.stroke();
            }

            if (car) {
                const p = root.px(car.x, car.y);
                const dot = Math.max(4, root.side * 0.03);
                ctx.fillStyle = Qt.rgba(root.trailColor.r, root.trailColor.g, root.trailColor.b, 0.25);
                ctx.beginPath();
                ctx.arc(p[0], p[1], dot * 1.6, 0, Math.PI * 2);
                ctx.fill();
                ctx.fillStyle = root.trailColor;
                ctx.strokeStyle = "#0c0e11";
                ctx.lineWidth = Math.max(1.5, dot * 0.3);
                ctx.beginPath();
                ctx.arc(p[0], p[1], dot, 0, Math.PI * 2);
                ctx.fill();
                ctx.stroke();
            }
        }
    }
}
