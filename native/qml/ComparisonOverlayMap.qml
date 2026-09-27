pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// One track map showing both A/B lap traces to the same scale (a shared
// bounding-box normalization, not each lap filling the frame on its own).
// hoverDistanceMeters (-1 when idle; a shared track-progress value, not each
// lap's own distance-into-lap) drives a position marker per lap at the
// corresponding point on the shared progress axis (KAN-37/38).
Rectangle {
    id: root
    property real hoverDistanceMeters: -1
    // KAN-117: the shared zoom window, highlighted on lap B's trace so the
    // selected segment is visible on the map. Empty for the whole lap.
    property real rangeStartMeters: 0
    property real rangeEndMeters: 0
    property real totalMeters: 0
    readonly property bool rangeShown: root.totalMeters > 0 && root.rangeEndMeters > root.rangeStartMeters
        && root.rangeEndMeters - root.rangeStartMeters < root.totalMeters - 1
    readonly property var trackA: appController.comparisonSlots.length
        ? (appController.comparisonOverlayTrack(0) || []) : []
    readonly property var trackB: appController.comparisonSlots.length
        ? (appController.comparisonOverlayTrack(1) || []) : []
    // KAN-97: colour one lap's line by a recorded value (or the A−B delta).
    // Off by default; the layer is static geometry, repainted only when the
    // layer, the lap or the pair changes, never on hover.
    readonly property var layerOptions: appController.comparisonSlots.length
        ? (appController.comparisonSlots, appController.comparisonMapLayerOptions()) : []
    property string layerId: ""
    property int layerSlot: 1
    readonly property var mapLayer: root.layerId.length > 0 && appController.comparisonSlots.length
        ? (appController.comparisonSlots, appController.comparisonMapLayer(root.layerId, root.layerSlot)) : ({})
    readonly property bool layerShown: root.mapLayer.valid === true
    readonly property bool diverging: root.mapLayer.scale === "diverging"
    // Diverging scales are symmetric around zero, so zero is always neutral.
    readonly property real layerLow: root.diverging
        ? -Math.max(Math.abs(root.mapLayer.minimum || 0), Math.abs(root.mapLayer.maximum || 0), 1e-9) : (root.mapLayer.minimum || 0)
    readonly property real layerHigh: root.diverging ? -root.layerLow : Math.max(root.mapLayer.maximum || 0, root.layerLow + 1e-9)
    // Sequential: one blue hue, dim to bright. Diverging: blue, grey at zero, amber.
    readonly property var sequentialStops: ["#28527a", "#e3f4ff"]
    readonly property var divergingStops: ["#4f9dff", "#8b95a1", "#ffab40"]
    function mix(from, to, t) {
        const a = Qt.color(from), b = Qt.color(to);
        return Qt.rgba(a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, 1);
    }
    function layerColor(value) {
        const t = Math.max(0, Math.min(1, (value - root.layerLow) / (root.layerHigh - root.layerLow)));
        if (!root.diverging) return root.mix(root.sequentialStops[0], root.sequentialStops[1], t);
        return t < 0.5 ? root.mix(root.divergingStops[0], root.divergingStops[1], t * 2)
                       : root.mix(root.divergingStops[1], root.divergingStops[2], (t - 0.5) * 2);
    }
    function valueText(value) {
        const magnitude = Math.max(Math.abs(root.layerLow), Math.abs(root.layerHigh));
        const digits = magnitude >= 100 ? 0 : magnitude >= 10 ? 1 : 2;
        return (root.diverging && value > 0 ? "+" : "") + Number(value).toFixed(digits);
    }
    readonly property string unavailableText: {
        if (root.layerId.length === 0 || root.layerShown) return "";
        const option = root.layerOptions.find(candidate => candidate.id === root.layerId);
        if (option && !option.available) return qsTr("Not recorded on either lap.");
        if (root.mapLayer.reason === "channelMissing") return qsTr("Not recorded on lap %1.").arg(root.layerSlot === 0 ? "A" : "B");
        if (root.mapLayer.reason === "noSamples") return qsTr("No usable samples on lap %1.").arg(root.layerSlot === 0 ? "A" : "B");
        return qsTr("No shared track position for this pair.");
    }
    color: "#070b10"
    border.color: "#1c2631"
    radius: 8

    Label {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: 10
        text: qsTr("TRACK POSITION")
        color: "#687789"
        font.pixelSize: 9
        font.letterSpacing: 1
    }

    RowLayout {
        id: layerControls
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.topMargin: 26
        anchors.leftMargin: 8
        anchors.rightMargin: 8
        spacing: 4
        FeComboBox {
            id: layerPicker
            objectName: "comparisonMapLayerPicker"
            Layout.fillWidth: true
            Layout.minimumWidth: 60
            implicitHeight: 26
            popupMinimumWidth: 180
            model: [qsTr("Line: A / B")].concat(root.layerOptions.map(option =>
                option.available ? option.label : qsTr("%1 · not recorded").arg(option.label)))
            currentIndex: root.layerId.length > 0 ? 1 + root.layerOptions.findIndex(option => option.id === root.layerId) : 0
            onActivated: index => root.layerId = index > 0 ? root.layerOptions[index - 1].id : ""
        }
        FeButton {
            objectName: "comparisonMapLayerSlotA"
            visible: root.layerId.length > 0
            compact: true
            accent: root.layerSlot === 0
            text: "A"
            Layout.preferredWidth: 30
            onClicked: root.layerSlot = 0
        }
        FeButton {
            objectName: "comparisonMapLayerSlotB"
            visible: root.layerId.length > 0
            compact: true
            accent: root.layerSlot === 1
            text: "B"
            Layout.preferredWidth: 30
            onClicked: root.layerSlot = 1
        }
    }

    Item {
        id: mapArea
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: layerControls.bottom
        anchors.topMargin: 6
        width: Math.max(0, Math.min(parent.width - 32, parent.height - layerControls.height - 26 - 6 - legend.height - 18))
        height: width

        Canvas {
            id: trackCanvas
            anchors.fill: parent
            property var segmentsA: root.trackA
            property var segmentsB: root.trackB
            property bool dimmed: root.layerShown
            onSegmentsAChanged: requestPaint()
            onSegmentsBChanged: requestPaint()
            onDimmedChanged: requestPaint()
            onAvailableChanged: if (available) requestPaint()
            onVisibleChanged: if (visible) requestPaint()
            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()
            function drawTrace(context, segments, color) {
                context.strokeStyle = color;
                context.lineWidth = 2.5;
                context.lineCap = "round";
                context.lineJoin = "round";
                for (const points of segments) {
                    if (points.length < 2) continue;
                    context.beginPath();
                    context.moveTo(Number(points[0].x) * width, Number(points[0].y) * height);
                    for (let index = 1; index < points.length; ++index)
                        context.lineTo(Number(points[index].x) * width, Number(points[index].y) * height);
                    context.stroke();
                }
            }
            onPaint: {
                const context = getContext("2d");
                context.reset();
                context.globalAlpha = dimmed ? 0.3 : 1;
                drawTrace(context, segmentsA, "#55e6a5");
                drawTrace(context, segmentsB, "#d95926");
            }
        }
        Canvas {
            id: rangeCanvas
            objectName: "comparisonMapRange"
            anchors.fill: parent
            visible: root.rangeShown
            // Repainted only when the zoom window changes, never on hover.
            property var points: {
                if (!root.rangeShown) return [];
                const span = root.rangeEndMeters - root.rangeStartMeters;
                const step = Math.max(2, span / 150);
                const result = [];
                for (let meters = root.rangeStartMeters; meters <= root.rangeEndMeters + 1e-6; meters += step) {
                    const point = appController.comparisonPositionAtProgress(1, Math.min(meters, root.rangeEndMeters));
                    result.push(point.x !== undefined ? point : null);
                }
                return result;
            }
            onPointsChanged: requestPaint()
            onAvailableChanged: if (available) requestPaint()
            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()
            onPaint: {
                const context = getContext("2d");
                context.reset();
                context.strokeStyle = "rgba(255, 255, 255, 0.85)";
                context.lineWidth = 7;
                context.lineCap = "round";
                context.lineJoin = "round";
                let drawing = false;
                context.beginPath();
                for (const point of points) {
                    if (!point) { drawing = false; continue; }
                    const x = Number(point.x) * width, y = Number(point.y) * height;
                    if (drawing) context.lineTo(x, y); else context.moveTo(x, y);
                    drawing = true;
                }
                context.stroke();
            }
        }
        Canvas {
            id: layerCanvas
            objectName: "comparisonMapLayer"
            anchors.fill: parent
            visible: root.layerShown
            property var polylines: root.layerShown ? root.mapLayer.polylines : []
            onPolylinesChanged: requestPaint()
            onAvailableChanged: if (available) requestPaint()
            onVisibleChanged: if (visible) requestPaint()
            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()
            onPaint: {
                const context = getContext("2d");
                context.reset();
                context.lineWidth = 4;
                context.lineCap = "round";
                for (const polyline of polylines) {
                    const points = polyline.points, values = polyline.values;
                    for (let index = 1; index < points.length; ++index) {
                        context.strokeStyle = root.layerColor((Number(values[index - 1]) + Number(values[index])) / 2);
                        context.beginPath();
                        context.moveTo(Number(points[index - 1].x) * width, Number(points[index - 1].y) * height);
                        context.lineTo(Number(points[index].x) * width, Number(points[index].y) * height);
                        context.stroke();
                    }
                }
            }
        }
        Label {
            anchors.centerIn: parent
            visible: root.trackA.length === 0 && root.trackB.length === 0
            text: qsTr("No GPS data in this section")
            color: "#657386"
        }
        Repeater {
            model: 2
            delegate: Rectangle {
                id: marker
                required property int index
                readonly property var point: root.hoverDistanceMeters >= 0
                    ? appController.comparisonPositionAtProgress(index, root.hoverDistanceMeters) : ({})
                visible: point.x !== undefined
                width: 16
                height: 16
                radius: 8
                color: index === 0 ? "#55e6a5" : "#d95926"
                border.color: "#0c150f"
                border.width: 2
                x: Number(point.x || 0) * mapArea.width - width / 2
                y: Number(point.y || 0) * mapArea.height - height / 2
            }
        }
    }

    // Legend: what the colour means, its range and unit, and where it comes from.
    ColumnLayout {
        id: legend
        objectName: "comparisonMapLegend"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 8
        spacing: 2
        visible: root.layerId.length > 0
        Label {
            objectName: "comparisonMapLayerUnavailable"
            Layout.fillWidth: true
            visible: root.unavailableText.length > 0
            text: root.unavailableText
            color: "#8d9aaa"
            font.pixelSize: 10
            wrapMode: Text.WordWrap
        }
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 6
            visible: root.layerShown
            radius: 3
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0; color: root.diverging ? root.divergingStops[0] : root.sequentialStops[0] }
                GradientStop { position: 0.5; color: root.diverging ? root.divergingStops[1] : root.mix(root.sequentialStops[0], root.sequentialStops[1], 0.5) }
                GradientStop { position: 1; color: root.diverging ? root.divergingStops[2] : root.sequentialStops[1] }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            visible: root.layerShown
            Label {
                objectName: "comparisonMapLegendLow"
                text: root.valueText(root.layerLow) + (root.diverging && root.mapLayer.negativeLabel ? " " + root.mapLayer.negativeLabel : "")
                color: "#b4c0cd"; font.pixelSize: 10
                elide: Text.ElideRight; Layout.fillWidth: true
            }
            Label {
                objectName: "comparisonMapLegendHigh"
                text: root.valueText(root.layerHigh) + (root.diverging && root.mapLayer.positiveLabel ? " " + root.mapLayer.positiveLabel : "")
                color: "#b4c0cd"; font.pixelSize: 10
                horizontalAlignment: Text.AlignRight
                elide: Text.ElideLeft; Layout.fillWidth: true
            }
        }
        Label {
            objectName: "comparisonMapLegendSource"
            Layout.fillWidth: true
            visible: root.layerShown
            // The unit only when the recording declares one; never guessed.
            text: qsTr("%1 · lap %2%3%4").arg(root.mapLayer.label || "").arg(root.layerSlot === 0 ? "A" : "B")
                .arg(root.mapLayer.unit ? " · " + root.mapLayer.unit : "")
                .arg(root.mapLayer.provenance === "calculated" ? qsTr(" · calculated") : "")
            color: "#657386"
            font.pixelSize: 10
            elide: Text.ElideRight
        }
    }
}
