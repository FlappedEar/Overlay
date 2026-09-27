pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// KAN-57/KAN-120: where the best lap can improve. The group's best lap
// against the sector theoretical best, with the track map coloured by the
// time the best lap leaves in each approved segment. Selecting a segment in
// the list (or double-clicking it on the map) opens its fastest lap against
// the best lap in the Corner Analyzer.
Dialog {
    id: root
    objectName: "theoreticalBestDialog"
    title: qsTr("Where your best lap can improve")
    modal: true
    anchors.centerIn: parent
    width: Math.min(1100, parent.width - 40)
    height: Math.min(760, parent.height - 30)
    standardButtons: Dialog.Close
    readonly property var theoretical: appController.outingTheoreticalBest
    readonly property var actualBest: root.theoretical.actualBest || null
    readonly property var gains: root.theoretical.gains || []
    readonly property var mapData: root.theoretical.map || ({})
    readonly property real maximumGain: Math.max(0.001, ...root.gains.map(gain => Number(gain.lossSeconds || 0)))
    property string selectedSegmentId: ""
    readonly property var lapConsistency: appController.outingLapConsistency.day || ({})
    // KAN-62: "typical" is the median, "spread" the interquartile range.
    function consistencyText(consistency, unitLabel) {
        if (!consistency || consistency.count === undefined) return "";
        if (!consistency.available)
            return qsTr("%1 %2 · too few for consistency (minimum %3)").arg(consistency.count).arg(unitLabel)
                .arg(appController.outingLapConsistency.minimumSamples || 3);
        return qsTr("typical %1 · spread %2 s · %3 %4").arg(appController.formatElapsedTime(Number(consistency.median)))
            .arg(Number(consistency.interquartileRange).toFixed(3)).arg(consistency.count).arg(unitLabel);
    }

    function calculateIfNeeded() {
        if (root.visible && ["idle", "error"].indexOf(root.theoretical.state) >= 0) appController.requestOutingTheoreticalBest();
    }
    onOpened: root.calculateIfNeeded()
    // Exclusions, a new best lap or edited segments invalidate the result;
    // recalculate while the dialog is showing.
    onTheoreticalChanged: if (root.theoretical.state === "idle") Qt.callLater(root.calculateIfNeeded)

    function actualSeconds() {
        if (!root.actualBest) return undefined;
        return root.actualBest.coversWholeLap ? root.actualBest.lapSeconds : root.actualBest.sectorSumSeconds;
    }
    // One hue, dim to bright: no loss is neutral, the largest loss is brightest.
    function gainColor(gain) {
        if (!gain || gain.lossSeconds === undefined) return "#26303b";
        const t = Math.max(0, Math.min(1, Number(gain.lossSeconds) / root.maximumGain));
        return Qt.rgba(0.30 + 0.70 * t, 0.36 + 0.24 * t, 0.42 - 0.22 * t, 1);
    }
    readonly property var selectedGain: root.gains.find(gain => gain.segmentId === root.selectedSegmentId) || null
    // KAN-63: one line per metric; empty when the metric has no samples.
    function spreadLine(label, summary, unit, provenance) {
        if (!summary || !summary.count) return "";
        const tail = qsTr("%1 laps").arg(summary.count) + (provenance ? " · " + provenance : "");
        if (!summary.available) return qsTr("%1: too few laps (%2)").arg(label).arg(tail);
        return qsTr("%1: spread %2%3 · %4").arg(label).arg(Number(summary.interquartileRange).toFixed(1)).arg(unit).arg(tail);
    }
    function speedLine(label, summary) {
        if (!summary || !summary.count) return "";
        if (!summary.available) return qsTr("%1: too few laps (%2)").arg(label).arg(summary.count);
        return qsTr("%1: typical %2 · spread %3 · %4 laps").arg(label).arg(Number(summary.median).toFixed(1))
            .arg(Number(summary.interquartileRange).toFixed(1)).arg(summary.count);
    }
    function variabilityLines(gain) {
        if (!gain || !gain.variability) return [];
        const v = gain.variability;
        const lines = [
            root.spreadLine(qsTr("Braking point"), v.brakingPointMeasured, " m", qsTr("measured")),
            root.spreadLine(qsTr("Braking point"), v.brakingPointInferred, " m", qsTr("inferred")),
            root.speedLine(qsTr("Apex speed"), v.apexSpeed),
            root.speedLine(qsTr("Minimum speed"), v.minimumSpeed),
            root.speedLine(qsTr("Exit speed"), v.exitSpeed),
            root.spreadLine(qsTr("Throttle pickup"), v.pickupMeasured, " m", qsTr("measured")),
            root.spreadLine(qsTr("Throttle pickup"), v.pickupInferred, " m", qsTr("inferred"))];
        if (v.lineOffset && v.lineOffset.available) {
            const accuracy = v.typicalGpsAccuracyMeters !== undefined
                ? qsTr("GPS accuracy about %1 m").arg(Number(v.typicalGpsAccuracyMeters).toFixed(v.typicalGpsAccuracyMeters < 1 ? 2 : 1)) : qsTr("GPS accuracy not recorded");
            lines.push(qsTr("Line: spread %1 m · %2%3").arg(Number(v.lineOffset.interquartileRange).toFixed(1)).arg(accuracy)
                .arg(v.lineSpreadResolvable ? "" : qsTr(" · not distinguishable from GPS error")));
        }
        return lines.filter(line => line.length > 0);
    }
    function openSegment(segmentId) {
        if (appController.openTheoreticalBestSector(segmentId)) root.close();
    }

    contentItem: Item {
        ColumnLayout {
            anchors.fill: parent
            spacing: 10
            Label {
                Layout.fillWidth: true
                text: appController.outingRanking.groupLabel || qsTr("Choose a compatibility group in Day results")
                wrapMode: Text.WordWrap
                textFormat: Text.PlainText
                color: "#91a0b2"
            }
            RowLayout {
                Layout.fillWidth: true
                visible: root.theoretical.state === "loading"
                BusyIndicator { running: parent.visible; Layout.preferredWidth: 24; Layout.preferredHeight: 24 }
                Label { text: qsTr("Timing every eligible lap on one shared track axis…"); color: "#91a0b2" }
            }
            Label {
                objectName: "theoreticalBestMessage"
                Layout.fillWidth: true
                visible: text.length > 0
                text: root.theoretical.message || ""
                wrapMode: Text.WordWrap
                textFormat: Text.PlainText
                color: "#d6a457"
            }
            FeButton {
                objectName: "retryTheoreticalBest"
                visible: ["unavailable", "error"].indexOf(root.theoretical.state) >= 0
                text: qsTr("Calculate again")
                onClicked: appController.requestOutingTheoreticalBest()
            }

            // Headline: your best lap -> theoretical best -> time available.
            // KAN-78: wraps onto a second line in a narrow window instead of
            // widening the dialog's content past its edge.
            Flow {
                objectName: "theoreticalBestSummary"
                Layout.fillWidth: true
                visible: root.theoretical.state === "ready"
                spacing: 28
                ColumnLayout {
                    spacing: 0
                    Label { text: qsTr("YOUR BEST LAP"); color: "#8d9aaa"; font.pixelSize: 10; font.letterSpacing: 1 }
                    Label {
                        objectName: "theoreticalBestActual"
                        text: appController.formatElapsedTime(Number(root.actualSeconds()))
                            + (root.actualBest ? " · " + root.actualBest.label : "")
                        textFormat: Text.PlainText
                        color: "#f2f6fb"; font.pixelSize: 22; font.weight: Font.DemiBold
                    }
                }
                Label { text: "→"; color: "#657386"; font.pixelSize: 22 }
                ColumnLayout {
                    spacing: 0
                    Label { text: qsTr("THEORETICAL BEST"); color: "#8d9aaa"; font.pixelSize: 10; font.letterSpacing: 1 }
                    Label {
                        objectName: "theoreticalBestTotal"
                        text: appController.formatElapsedTime(Number(root.theoretical.totalSeconds))
                        color: "#55e6a5"; font.pixelSize: 22; font.weight: Font.DemiBold
                    }
                }
                ColumnLayout {
                    spacing: 0
                    Label { text: qsTr("LAPS TODAY"); color: "#8d9aaa"; font.pixelSize: 10; font.letterSpacing: 1 }
                    Label {
                        objectName: "theoreticalBestLapConsistency"
                        text: root.consistencyText(root.lapConsistency, qsTr("laps"))
                        color: "#dce4ee"; font.pixelSize: 14
                    }
                }
                ColumnLayout {
                    spacing: 0
                    Label { text: qsTr("AVAILABLE"); color: "#8d9aaa"; font.pixelSize: 10; font.letterSpacing: 1 }
                    Label {
                        objectName: "theoreticalBestDifference"
                        text: root.theoretical.differenceSeconds === undefined ? "—"
                            : Number(root.theoretical.differenceSeconds).toFixed(3) + " s"
                        color: "#ff9a4d"; font.pixelSize: 22; font.weight: Font.DemiBold
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                visible: root.theoretical.state === "ready"
                spacing: 12
                // Track map, each segment coloured by the time left there.
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumWidth: 260
                    color: "#070b10"
                    border.color: "#1c2631"
                    radius: 8
                    Item {
                        id: mapArea
                        anchors.centerIn: parent
                        anchors.verticalCenterOffset: -14
                        width: Math.max(0, Math.min(parent.width - 40, parent.height - 70))
                        height: width
                        Canvas {
                            id: mapCanvas
                            objectName: "theoreticalBestMap"
                            anchors.fill: parent
                            property var segments: root.mapData.segments || []
                            property string selected: root.selectedSegmentId
                            onSegmentsChanged: requestPaint()
                            onSelectedChanged: requestPaint()
                            onAvailableChanged: if (available) requestPaint()
                            onWidthChanged: requestPaint()
                            onHeightChanged: requestPaint()
                            function drawPart(context, points) {
                                if (points.length < 2) return;
                                context.beginPath();
                                context.moveTo(points[0].x * width, points[0].y * height);
                                for (let index = 1; index < points.length; ++index)
                                    context.lineTo(points[index].x * width, points[index].y * height);
                                context.stroke();
                            }
                            onPaint: {
                                const context = getContext("2d");
                                context.reset();
                                context.lineCap = "round";
                                context.lineJoin = "round";
                                for (const segment of segments) {
                                    context.lineWidth = 7;
                                    context.strokeStyle = root.gainColor(segment);
                                    for (const part of segment.parts) drawPart(context, part);
                                }
                                for (const segment of segments) {
                                    if (segment.segmentId !== selected) continue;
                                    context.lineWidth = 13;
                                    context.strokeStyle = "#ffffff";
                                    for (const part of segment.parts) drawPart(context, part);
                                    context.lineWidth = 8;
                                    context.strokeStyle = root.gainColor(segment);
                                    for (const part of segment.parts) drawPart(context, part);
                                }
                            }
                        }
                        MouseArea {
                            anchors.fill: parent
                            // The segment nearest the pointer, within 16 px.
                            function segmentAt(x, y) {
                                let best = "", bestDistance = 16 * 16;
                                for (const segment of mapCanvas.segments) {
                                    for (const part of segment.parts) {
                                        for (const point of part) {
                                            const dx = point.x * width - x, dy = point.y * height - y;
                                            if (dx * dx + dy * dy < bestDistance) { bestDistance = dx * dx + dy * dy; best = segment.segmentId; }
                                        }
                                    }
                                }
                                return best;
                            }
                            onClicked: mouse => {
                                const id = segmentAt(mouse.x, mouse.y);
                                if (id.length > 0) root.selectedSegmentId = id;
                            }
                            onDoubleClicked: mouse => {
                                const id = segmentAt(mouse.x, mouse.y);
                                if (id.length > 0) root.openSegment(id);
                            }
                        }
                    }
                    // KAN-63: how repeatable the selected corner is.
                    Rectangle {
                        objectName: "theoreticalBestVariability"
                        anchors.left: parent.left
                        anchors.top: parent.top
                        anchors.margins: 10
                        width: Math.min(parent.width - 20, variabilityColumn.implicitWidth + 20)
                        height: variabilityColumn.implicitHeight + 16
                        visible: root.variabilityLines(root.selectedGain).length > 0
                        color: "#dd0b1119"
                        border.color: "#253244"
                        radius: 6
                        ColumnLayout {
                            id: variabilityColumn
                            anchors.left: parent.left
                            anchors.top: parent.top
                            anchors.margins: 8
                            spacing: 2
                            Label {
                                text: root.selectedGain ? qsTr("%1 · lap to lap").arg(root.selectedGain.name) : ""
                                textFormat: Text.PlainText
                                color: "#f2f6fb"; font.pixelSize: 12; font.weight: Font.DemiBold
                            }
                            Repeater {
                                model: root.variabilityLines(root.selectedGain)
                                Label {
                                    required property string modelData
                                    text: modelData
                                    textFormat: Text.PlainText
                                    color: "#b5c1d0"; font.pixelSize: 11
                                }
                            }
                        }
                    }
                    // Legend: one sequential scale.
                    RowLayout {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        anchors.margins: 12
                        spacing: 8
                        Label { text: "0 s"; color: "#91a0b2"; font.pixelSize: 10 }
                        Rectangle {
                            Layout.preferredWidth: 140
                            Layout.preferredHeight: 8
                            radius: 4
                            gradient: Gradient {
                                orientation: Gradient.Horizontal
                                GradientStop { position: 0.0; color: root.gainColor({lossSeconds: 0}) }
                                GradientStop { position: 1.0; color: root.gainColor({lossSeconds: root.maximumGain}) }
                            }
                        }
                        Label { text: "+" + root.maximumGain.toFixed(3) + " s"; color: "#91a0b2"; font.pixelSize: 10 }
                        Label {
                            Layout.fillWidth: true
                            text: qsTr("time your best lap loses to the fastest recorded time · double-click a segment to compare")
                            color: "#657386"; font.pixelSize: 10; elide: Text.ElideRight
                        }
                    }
                }
                // Segments, largest gain first.
                ColumnLayout {
                    Layout.preferredWidth: 420
                    Layout.maximumWidth: 420
                    Layout.minimumWidth: 280
                    Layout.fillHeight: true
                    spacing: 4
                    Label { text: qsTr("WHERE THE TIME IS"); color: "#8d9aaa"; font.pixelSize: 10; font.letterSpacing: 1 }
                    ListView {
                        id: sectors
                        objectName: "theoreticalBestSectors"
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: 2
                        model: root.gains
                        currentIndex: root.gains.findIndex(gain => gain.segmentId === root.selectedSegmentId)
                        onCurrentIndexChanged: if (currentIndex >= 0) positionViewAtIndex(currentIndex, ListView.Contain)
                        ScrollBar.vertical: ScrollBar {}
                        delegate: ItemDelegate {
                            id: sectorRow
                            required property var modelData
                            required property int index
                            objectName: "theoreticalBestSector" + sectorRow.index
                            width: sectors.width - 12
                            height: 60
                            highlighted: sectorRow.modelData.segmentId === root.selectedSegmentId
                            enabled: sectorRow.modelData.seconds !== undefined
                            onClicked: root.openSegment(sectorRow.modelData.segmentId)
                            onHoveredChanged: if (sectorRow.hovered) root.selectedSegmentId = sectorRow.modelData.segmentId
                            ToolTip.visible: sectorRow.hovered && sectorRow.enabled
                            ToolTip.text: qsTr("Compare the fastest lap here with your best lap in the Corner Analyzer")
                            contentItem: RowLayout {
                                spacing: 10
                                Rectangle {
                                    Layout.preferredWidth: 6
                                    Layout.fillHeight: true
                                    radius: 3
                                    color: root.gainColor(sectorRow.modelData)
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 1
                                    Label {
                                        Layout.fillWidth: true
                                        text: sectorRow.modelData.name
                                        textFormat: Text.PlainText
                                        elide: Text.ElideRight
                                        color: "#f2f6fb"; font.pixelSize: 13
                                    }
                                    Label {
                                        Layout.fillWidth: true
                                        text: sectorRow.modelData.seconds === undefined
                                            ? (sectorRow.modelData.unavailableReason || "")
                                            : Number(sectorRow.modelData.lossSeconds || 0) > 0.0005
                                                ? qsTr("fastest: %1 · %2").arg(sectorRow.modelData.sourceLapLabel || qsTr("lap unavailable"))
                                                    .arg(appController.formatElapsedTime(Number(sectorRow.modelData.seconds)))
                                                : qsTr("your best lap is the fastest here")
                                        textFormat: Text.PlainText
                                        elide: Text.ElideRight
                                        color: "#91a0b2"; font.pixelSize: 11
                                    }
                                    Label {
                                        objectName: "theoreticalBestConsistency" + sectorRow.index
                                        Layout.fillWidth: true
                                        text: root.consistencyText(sectorRow.modelData.consistency, qsTr("laps"))
                                        textFormat: Text.PlainText
                                        elide: Text.ElideRight
                                        color: "#657386"; font.pixelSize: 11
                                    }
                                }
                                Label {
                                    objectName: "theoreticalBestLoss" + sectorRow.index
                                    text: sectorRow.modelData.lossSeconds === undefined ? "—"
                                        : "+" + Number(sectorRow.modelData.lossSeconds).toFixed(3) + " s"
                                    color: Number(sectorRow.modelData.lossSeconds || 0) > 0.0005 ? "#ff9a4d" : "#657386"
                                    font.pixelSize: 14; font.weight: Font.DemiBold
                                }
                            }
                        }
                    }
                }
            }
            Label {
                objectName: "theoreticalBestExplanation"
                Layout.fillWidth: true
                visible: root.theoretical.state === "ready"
                text: qsTr("Algorithm: %1. The fastest recorded time for each approved segment across the eligible laps of this group, every lap timed on one shared track axis. It combines fragments of different laps and does not show that the whole lap can be driven that fast. Typical is the median; spread is the interquartile range, the time between the 25th and 75th percentile (the middle half of the laps), so one slow lap does not dominate it (%2, at least %3 laps).")
                    .arg(root.theoretical.algorithm || "").arg(root.theoretical.consistencyAlgorithm || "")
                    .arg(appController.outingLapConsistency.minimumSamples || 3)
                    + (root.actualBest && !root.actualBest.coversWholeLap
                        ? " " + qsTr("The approved segments do not cover the whole lap, so your best lap above is the sum of its own times over the same segments, not its lap time.")
                        : "")
                wrapMode: Text.WordWrap
                textFormat: Text.PlainText
                font.pixelSize: 10
                color: "#657386"
            }
        }
    }
}
