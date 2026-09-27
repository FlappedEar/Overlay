pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

Rectangle {
    id: root
    property bool lapDetail: false
    // Set to 0 or 1 to show one side of an A/B comparison pair instead of the
    // single-lap/playback session. No marker yet: a shared scrubbing cursor
    // across both traces lands with the shared-progress axis.
    property int comparisonSlot: -1
    // KAN-48: draw the open lap's segment proposals and approved segments.
    // Static geometry: repainted only when the review changes, never on playback.
    property bool segmentReview: false
    property int selectedSegmentIndex: -1
    // KAN-92: coasting episodes of the open lap, drawn over the track.
    property var coastingLayers: []
    // KAN-49: while a boundary is being placed, a tap reports its normalized map point.
    property bool pickingProgress: false
    signal progressPicked(real x, real y)
    readonly property var pathSegments: {
        if (root.comparisonSlot >= 0) {
            appController.comparisonSlots;
            return appController.comparisonLapTrack(root.comparisonSlot);
        }
        return root.lapDetail ? appController.outingLapTrack : [appController.trackPoints];
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

    Item {
        id: mapArea
        anchors.centerIn: parent
        anchors.verticalCenterOffset: 10
        width: Math.max(0, Math.min(parent.width - 48, parent.height - 64))
        height: width
        property var currentPoint: {
            if (root.comparisonSlot >= 0) return {};
            if (root.lapDetail) return appController.outingLapTrackPoint;
            appController.playbackTime;
            return appController.currentTrackPoint;
        }

        Canvas {
            id: trackCanvas
            anchors.fill: parent
            property var segments: root.pathSegments
            onSegmentsChanged: requestPaint()
            onAvailableChanged: if (available) requestPaint()
            onVisibleChanged: if (visible) requestPaint()
            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()
            onPaint: {
                const context = getContext("2d");
                context.reset();
                context.strokeStyle = "#536779";
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
            Connections {
                target: appController
                function onTelemetryChanged() {
                    trackCanvas.requestPaint();
                }
            }
        }
        Canvas {
            id: segmentCanvas
            objectName: "segmentReviewMapLayer"
            anchors.fill: parent
            visible: root.segmentReview
            property var layers: root.segmentReview ? appController.segmentReviewMapLayers : []
            property int selected: root.selectedSegmentIndex
            onLayersChanged: requestPaint()
            onSelectedChanged: requestPaint()
            onAvailableChanged: if (available) requestPaint()
            onVisibleChanged: if (visible) requestPaint()
            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()
            TapHandler {
                objectName: "segmentPickHandler"
                enabled: root.segmentReview && root.pickingProgress
                onTapped: (eventPoint, button) => {
                    if (segmentCanvas.width > 0 && segmentCanvas.height > 0)
                        root.progressPicked(eventPoint.position.x / segmentCanvas.width,
                                            eventPoint.position.y / segmentCanvas.height);
                }
            }
            function strokeLayer(context, layer) {
                for (const points of layer.polylines) {
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
                context.lineCap = "round";
                context.lineJoin = "round";
                // Approved first, then proposals, then uncertainty windows on top.
                for (const kind of ["approved", "proposal", "uncertain"]) {
                    for (const layer of layers) {
                        if (layer.kind !== kind) continue;
                        const selectedLayer = layer.index !== undefined && layer.index === selected;
                        if (kind === "approved") {
                            context.strokeStyle = "#55e6a5";
                            context.lineWidth = 4;
                        } else if (kind === "proposal") {
                            context.strokeStyle = layer.type === "corner" ? "#4da3ff" : "#8a9bb0";
                            context.lineWidth = selectedLayer ? 6 : 3;
                        } else {
                            context.strokeStyle = "#ffb84d";
                            context.lineWidth = selectedLayer ? 9 : 7;
                        }
                        strokeLayer(context, layer);
                    }
                }
            }
        }
        Canvas {
            id: coastingCanvas
            objectName: "coastingMapLayer"
            anchors.fill: parent
            visible: root.coastingLayers.length > 0
            property var layers: root.coastingLayers
            onLayersChanged: requestPaint()
            onAvailableChanged: if (available) requestPaint()
            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()
            onPaint: {
                const context = getContext("2d");
                context.reset();
                context.lineCap = "round";
                context.lineJoin = "round";
                context.strokeStyle = "#ff9a4d";
                context.lineWidth = 6;
                for (const layer of layers) {
                    for (const points of layer.polylines) {
                        if (points.length < 2) continue;
                        context.beginPath();
                        context.moveTo(Number(points[0].x) * width, Number(points[0].y) * height);
                        for (let index = 1; index < points.length; ++index)
                            context.lineTo(Number(points[index].x) * width, Number(points[index].y) * height);
                        context.stroke();
                    }
                }
            }
        }
        Label {
            objectName: "segmentPickHint"
            anchors.top: parent.top
            anchors.horizontalCenter: parent.horizontalCenter
            visible: root.segmentReview && root.pickingProgress
            text: qsTr("Click the track line to place the boundary")
            color: "#ffb84d"
            font.pixelSize: 11
        }
        Label {
            anchors.centerIn: parent
            visible: (root.lapDetail || root.comparisonSlot >= 0) && root.pathSegments.length === 0
            text: qsTr("No GPS data in this section")
            color: "#657386"
        }
        Rectangle {
            visible: mapArea.currentPoint.x !== undefined
            width: 10
            height: 10
            radius: 5
            color: "#55e6a5"
            border.color: "#d9fff0"
            x: Number(mapArea.currentPoint.x || 0) * mapArea.width - width / 2
            y: Number(mapArea.currentPoint.y || 0) * mapArea.height - height / 2
        }
    }
}
