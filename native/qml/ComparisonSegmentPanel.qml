pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// KAN-55: Corner Analyzer -- approved-segment list with A/B/delta metrics
// (sector time; entry/apex/minimum/exit speed, braking point and throttle
// pickup for corners) for the current comparison pair. Only ever populated
// when both compared laps' approved segmentation matches exactly (same
// revision) -- comparisonApprovedSegments() returns nothing otherwise, and
// this panel simply stays empty rather than guessing a correspondence
// between two independently-approved segment sets. Selecting a metric row
// sets the shared progress cursor/range (rangeRequested/hovered), the same
// mechanism ComparisonOverlayChart/ComparisonOverlayMap already drive from
// hoverDistanceMeters/zoomStart/zoomEnd.
Rectangle {
    id: root
    color: "#070b10"
    border.color: "#1c2631"
    radius: 8
    signal rangeRequested(real startMeters, real endMeters)
    signal hovered(real meters)
    // KAN-70: show a recorded channel (heart rate) in the comparison charts.
    signal channelRequested(string channel)

    // appController.comparisonSlots is a real Q_PROPERTY (NOTIFY
    // comparisonSlotsChanged); forcing a read on it here is the same
    // established pattern comparisonChannelSeriesByProgress already uses in
    // ComparisonOverlayChart.qml so this Q_INVOKABLE re-runs whenever the
    // pair actually changes, not a new/different mechanism.
    readonly property var segments: (appController.comparisonSlots, appController.comparisonApprovedSegments())
    property string selectedSegmentId: ""
    readonly property var metrics: root.selectedSegmentId.length > 0
        ? (appController.comparisonSlots, appController.comparisonSegmentMetrics(root.selectedSegmentId)) : ({})
    // KAN-70: A/B heart rate over the selected segment, from each lap's own
    // recording. A segment across the start/finish line (end before start)
    // combines the end and the beginning of each lap.
    readonly property var selectedSegment: root.segments.find(segment => segment.id === root.selectedSegmentId) || null
    readonly property var heartRate: root.visible && root.selectedSegment && root.selectedSegment.endMeters !== root.selectedSegment.startMeters
        ? (appController.comparisonSlots, appController.comparisonHeartRate(root.selectedSegment.startMeters, root.selectedSegment.endMeters)) : ({})
    readonly property var heartRateLaps: root.heartRate.laps || []
    readonly property bool heartRateRecorded: root.heartRateLaps.some(lap => lap.valid)
    function heartRateText(lap) {
        return lap && lap.valid ? Number(lap.mean).toFixed(0) + " bpm" : "—";
    }
    function heartRateCoverage(lap) {
        if (!lap) return qsTr("unavailable");
        if (!lap.valid) return lap.unavailableReason === "channelMissing" ? qsTr("not recorded") : qsTr("no valid samples");
        return qsTr("%1 samples, %2% covered").arg(lap.sampleCount).arg(Math.round(lap.coverage * 100));
    }
    onSegmentsChanged: {
        if (root.segments.length === 0) { root.selectedSegmentId = ""; return; }
        if (!root.segments.some(segment => segment.id === root.selectedSegmentId))
            root.selectedSegmentId = root.segments[0].id;
        // Deferred: the same pair change also resets the shared zoom window
        // (ComparisonDetailPanel.onTotalMetersChanged); apply after it.
        Qt.callLater(root.applyRequestedSegment);
    }
    // KAN-57: a segment requested from elsewhere (the theoretical-best
    // dialog). Applied once the requested pair's segments are available, then
    // cleared so later list changes do not keep jumping back to it.
    readonly property string requestedSegmentId: appController.comparisonFocusSegmentId
    onRequestedSegmentIdChanged: root.applyRequestedSegment()
    onSelectedSegmentIdChanged: {
        const index = root.segments.findIndex(segment => segment.id === root.selectedSegmentId);
        if (index >= 0) Qt.callLater(() => segmentList.positionViewAtIndex(index, ListView.Contain));
    }
    function applyRequestedSegment() {
        if (root.requestedSegmentId.length === 0) return;
        const segment = root.segments.find(candidate => candidate.id === root.requestedSegmentId);
        if (!segment) return;
        root.selectedSegmentId = segment.id;
        if (segment.endMeters > segment.startMeters) root.selectMetric(segment.startMeters, segment.endMeters);
        Qt.callLater(() => appController.clearComparisonFocusSegment());
    }

    function selectMetric(startMeters, endMeters) {
        root.rangeRequested(startMeters, endMeters);
        root.hovered((startMeters + endMeters) / 2);
    }
    function segmentLength(segment) {
        const length = segment.endMeters - segment.startMeters;
        return length >= 0 ? length : length + appController.comparisonProgressAxisLength;
    }
    function formatValue(entry, digits, suffix) {
        if (!entry || entry.value === undefined) return "—";
        return Number(entry.value).toFixed(digits) + (suffix || "");
    }
    function formatTime(entry) {
        if (!entry || entry.value === undefined) return "—";
        return appController.formatElapsedTime(Number(entry.value));
    }
    function formatDelta(entry, digits, suffix) {
        if (!entry || entry.value === undefined) return "—";
        const value = Number(entry.value);
        return (value >= 0 ? "+" : "") + value.toFixed(digits) + (suffix || "");
    }

    // KAN-78: one vertical scroll surface when the panel is shorter than its
    // content (the 480 px analysis-window minimum); unchanged otherwise.
    Flickable {
        id: scroller
        objectName: "cornerAnalyzerScroll"
        anchors.fill: parent
        anchors.margins: 10
        contentWidth: width
        contentHeight: column.height
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { policy: scroller.contentHeight > scroller.height ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff }
    ColumnLayout {
        id: column
        width: scroller.width
        height: Math.max(implicitHeight, scroller.height)
        spacing: 8
        Label {
            text: qsTr("CORNER ANALYZER")
            color: "#8d9aaa"
            font.pixelSize: 9
            font.weight: Font.DemiBold
            font.letterSpacing: 1
        }
        Label {
            objectName: "cornerAnalyzerSegmentationNote"
            Layout.fillWidth: true
            visible: text.length > 0
            text: (appController.comparisonSlots, appController.comparisonSegmentationNote())
            color: "#d6a457"
            wrapMode: Text.WordWrap
            textFormat: Text.PlainText
            font.pixelSize: 11
        }
        Label {
            objectName: "cornerAnalyzerEmptyMessage"
            Layout.fillWidth: true
            visible: root.segments.length === 0
            text: qsTr("No matching approved segments for these two laps. Approve the same track segmentation on both to use the Corner Analyzer.")
            color: "#657386"
            wrapMode: Text.WordWrap
            font.pixelSize: 11
        }
        RowLayout {
            id: openLaps
            objectName: "cornerAnalyzerOpenLaps"
            visible: root.selectedSegmentId.length > 0
            Layout.fillWidth: true
            spacing: 6
            // KAN-61: the lap view shows the run's video when the lap belongs
            // to the loaded run; otherwise it opens without video.
            function openLap(slot) {
                const segment = root.segments.find(candidate => candidate.id === root.selectedSegmentId);
                if (segment) appController.openComparisonLapAtProgress(slot, segment.startMeters);
            }
            FeButton {
                objectName: "cornerAnalyzerOpenLapA"
                compact: true
                text: qsTr("Lap A here…")
                onClicked: openLaps.openLap(0)
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Open lap A at the start of this segment, with its video when available")
            }
            FeButton {
                objectName: "cornerAnalyzerOpenLapB"
                compact: true
                text: qsTr("Lap B here…")
                onClicked: openLaps.openLap(1)
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Open lap B at the start of this segment, with its video when available")
            }
            Item { Layout.fillWidth: true }
        }
        // KAN-117: a side column next to the map and charts. Segment list on
        // top, metrics for the selected segment below.
        ListView {
            id: segmentList
            objectName: "cornerAnalyzerSegmentList"
            visible: root.segments.length > 0
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(contentHeight, root.height * 0.42)
            Layout.minimumHeight: 80
            clip: true
            model: root.segments
            ScrollBar.vertical: ScrollBar {}
            delegate: ItemDelegate {
                id: segmentDelegate
                required property var modelData
                objectName: "cornerAnalyzerSegment-" + segmentDelegate.modelData.id
                width: segmentList.width - 12
                height: 26
                font.pixelSize: 12
                highlighted: segmentDelegate.modelData.id === root.selectedSegmentId
                text: segmentDelegate.modelData.name + " · " + segmentDelegate.modelData.type
                    + " · " + Math.round(root.segmentLength(segmentDelegate.modelData)) + " m"
                onClicked: {
                    root.selectedSegmentId = segmentDelegate.modelData.id;
                    root.selectMetric(segmentDelegate.modelData.startMeters, segmentDelegate.modelData.endMeters);
                }
            }
        }
        ColumnLayout {
            objectName: "cornerAnalyzerMetrics"
            visible: root.segments.length > 0
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 4
            RowLayout {
                Layout.fillWidth: true
                visible: Object.keys(root.metrics).length > 0
                Label { font.pixelSize: 12; text: ""; Layout.preferredWidth: 104 }
                Label { font.pixelSize: 12; text: qsTr("A"); color: "#55e6a5"; Layout.preferredWidth: 76; font.weight: Font.DemiBold }
                Label { font.pixelSize: 12; text: qsTr("B"); color: "#d95926"; Layout.preferredWidth: 76; font.weight: Font.DemiBold }
                Label { font.pixelSize: 12; text: qsTr("Δ (A−B)"); color: "#f3f6fa"; font.weight: Font.DemiBold }
            }
            RowLayout {
                objectName: "cornerAnalyzerSectorTimeRow"
                Layout.fillWidth: true
                visible: root.metrics.sectorTime !== undefined
                Label { font.pixelSize: 12; text: qsTr("Sector time"); color: "#91a0b2"; Layout.preferredWidth: 104 }
                Label { font.pixelSize: 12; objectName: "cornerAnalyzerSectorTimeA"; text: root.formatTime(root.metrics.sectorTime && root.metrics.sectorTime.a); color: "#55e6a5"; Layout.preferredWidth: 76 }
                Label { font.pixelSize: 12; objectName: "cornerAnalyzerSectorTimeB"; text: root.formatTime(root.metrics.sectorTime && root.metrics.sectorTime.b); color: "#d95926"; Layout.preferredWidth: 76 }
                Label { font.pixelSize: 12; objectName: "cornerAnalyzerSectorTimeDelta"; text: root.formatDelta(root.metrics.sectorTime && root.metrics.sectorTime.delta, 3, " s"); color: "#f3f6fa"; font.bold: true }
                Item { Layout.fillWidth: true }
                ToolButton {
                    objectName: "cornerAnalyzerSectorTimeSelect"
                    text: qsTr("⌖")
                    implicitWidth: 26
                    implicitHeight: 22
                    Accessible.name: qsTr("Zoom to this segment")
                    onClicked: {
                        const segment = root.segments.find(candidate => candidate.id === root.selectedSegmentId);
                        if (segment) root.selectMetric(segment.startMeters, segment.endMeters);
                    }
                }
            }
            // Straights and sectors: measured entry, maximum, minimum and exit speed.
            Repeater {
                objectName: "cornerAnalyzerSegmentSpeedRows"
                model: root.metrics.speeds !== undefined && root.metrics.corner === undefined
                    ? ["entry", "maximum", "minimum", "exit"] : []
                delegate: RowLayout {
                    id: segmentSpeedRow
                    required property string modelData
                    objectName: "cornerAnalyzerSegmentSpeed-" + segmentSpeedRow.modelData
                    Layout.fillWidth: true
                    readonly property var value: root.metrics.speeds ? root.metrics.speeds[segmentSpeedRow.modelData] : undefined
                    readonly property string unit: root.metrics.speeds ? " " + root.metrics.speeds.unit : ""
                    Label {
                        font.pixelSize: 12
                        text: ({entry: qsTr("Entry speed"), maximum: qsTr("Top speed"), minimum: qsTr("Lowest speed"),
                                exit: qsTr("Exit speed")})[segmentSpeedRow.modelData]
                        color: "#91a0b2"; Layout.preferredWidth: 104
                    }
                    Label { font.pixelSize: 12; text: root.formatValue(segmentSpeedRow.value && segmentSpeedRow.value.a, 1, segmentSpeedRow.unit); color: "#55e6a5"; Layout.preferredWidth: 76 }
                    Label { font.pixelSize: 12; text: root.formatValue(segmentSpeedRow.value && segmentSpeedRow.value.b, 1, segmentSpeedRow.unit); color: "#d95926"; Layout.preferredWidth: 76 }
                    Label { font.pixelSize: 12; text: root.formatDelta(segmentSpeedRow.value && segmentSpeedRow.value.delta, 1); color: "#f3f6fa"; font.bold: true }
                }
            }
            Repeater {
                objectName: "cornerAnalyzerCornerSpeedRows"
                model: root.metrics.corner !== undefined ? ["entry", "apex", "minimum", "exit"] : []
                delegate: RowLayout {
                    id: speedRow
                    required property string modelData
                    objectName: "cornerAnalyzerSpeedRow-" + speedRow.modelData
                    Layout.fillWidth: true
                    readonly property var phase: root.metrics.corner ? root.metrics.corner[speedRow.modelData] : undefined
                    readonly property string unit: root.metrics.corner ? " " + root.metrics.corner.unit : ""
                    Label {
                        font.pixelSize: 12
                        text: speedRow.modelData.charAt(0).toUpperCase() + speedRow.modelData.slice(1) + qsTr(" speed")
                        color: "#91a0b2"; Layout.preferredWidth: 104
                    }
                    Label { font.pixelSize: 12; text: root.formatValue(speedRow.phase && speedRow.phase.a, 1, speedRow.unit); color: "#55e6a5"; Layout.preferredWidth: 76 }
                    Label { font.pixelSize: 12; text: root.formatValue(speedRow.phase && speedRow.phase.b, 1, speedRow.unit); color: "#d95926"; Layout.preferredWidth: 76 }
                    Label { font.pixelSize: 12; text: root.formatDelta(speedRow.phase && speedRow.phase.delta, 1); color: "#f3f6fa"; font.bold: true }
                }
            }
            RowLayout {
                id: brakingRow
                objectName: "cornerAnalyzerBrakingRow"
                Layout.fillWidth: true
                visible: root.metrics.braking !== undefined
                readonly property var brakingPoint: root.metrics.braking ? root.metrics.braking.point : undefined
                Label { font.pixelSize: 12; text: qsTr("Braking point"); color: "#91a0b2"; Layout.preferredWidth: 104 }
                Label { font.pixelSize: 12; text: root.formatValue(brakingRow.brakingPoint && brakingRow.brakingPoint.a, 1, " m"); color: "#55e6a5"; Layout.preferredWidth: 76 }
                Label { font.pixelSize: 12; text: root.formatValue(brakingRow.brakingPoint && brakingRow.brakingPoint.b, 1, " m"); color: "#d95926"; Layout.preferredWidth: 76 }
                Label { font.pixelSize: 12; text: root.formatDelta(brakingRow.brakingPoint && brakingRow.brakingPoint.delta, 1, " m"); color: "#f3f6fa"; font.bold: true }
            }
            RowLayout {
                id: exitRow
                objectName: "cornerAnalyzerExitRow"
                Layout.fillWidth: true
                visible: root.metrics.exitEffects !== undefined
                readonly property var pickupMetric: root.metrics.exitEffects ? root.metrics.exitEffects.pickup : undefined
                Label { font.pixelSize: 12; text: qsTr("Throttle pickup"); color: "#91a0b2"; Layout.preferredWidth: 104 }
                Label { font.pixelSize: 12; text: root.formatValue(exitRow.pickupMetric && exitRow.pickupMetric.a, 1, " m"); color: "#55e6a5"; Layout.preferredWidth: 76 }
                Label { font.pixelSize: 12; text: root.formatValue(exitRow.pickupMetric && exitRow.pickupMetric.b, 1, " m"); color: "#d95926"; Layout.preferredWidth: 76 }
                Label { font.pixelSize: 12; text: root.formatDelta(exitRow.pickupMetric && exitRow.pickupMetric.delta, 1, " m"); color: "#f3f6fa"; font.bold: true }
            }
            RowLayout {
                objectName: "cornerAnalyzerHeartRateRow"
                Layout.fillWidth: true
                visible: root.heartRateRecorded
                Label { font.pixelSize: 12; text: qsTr("Heart rate"); color: "#91a0b2"; Layout.preferredWidth: 104 }
                Label { font.pixelSize: 12; objectName: "cornerAnalyzerHeartRateA"; text: root.heartRateText(root.heartRateLaps[0]); color: "#55e6a5"; Layout.preferredWidth: 76 }
                Label { font.pixelSize: 12; objectName: "cornerAnalyzerHeartRateB"; text: root.heartRateText(root.heartRateLaps[1]); color: "#d95926"; Layout.preferredWidth: 76 }
                Label {
                    font.pixelSize: 12
                    objectName: "cornerAnalyzerHeartRateDelta"
                    text: root.heartRateLaps.length === 2 && root.heartRateLaps[0].valid && root.heartRateLaps[1].valid
                        ? root.formatDelta({value: root.heartRateLaps[0].mean - root.heartRateLaps[1].mean}, 0, " bpm") : "—"
                    color: "#f3f6fa"; font.bold: true
                }
                Item { Layout.fillWidth: true }
                ToolButton {
                    objectName: "cornerAnalyzerHeartRateShow"
                    text: qsTr("♥")
                    implicitWidth: 26
                    implicitHeight: 22
                    Accessible.name: qsTr("Show the heart-rate channel in the charts")
                    ToolTip.visible: hovered
                    ToolTip.text: Accessible.name
                    onClicked: {
                        const lap = root.heartRateLaps.find(candidate => candidate.valid);
                        if (lap) root.channelRequested(lap.channel);
                    }
                }
            }
            Label {
                objectName: "cornerAnalyzerHeartRateNote"
                Layout.fillWidth: true
                visible: root.heartRateRecorded
                text: qsTr("Mean over this segment · A %1 · B %2. Observed values only.")
                    .arg(root.heartRateCoverage(root.heartRateLaps[0])).arg(root.heartRateCoverage(root.heartRateLaps[1]))
                wrapMode: Text.WordWrap
                color: "#657386"
                font.pixelSize: 10
            }
            Label {
                Layout.fillWidth: true
                visible: root.metrics.braking !== undefined || root.metrics.exitEffects !== undefined
                text: qsTr("Braking point and throttle pickup are positions along the lap (m). Δ positive: A later.")
                wrapMode: Text.WordWrap
                color: "#657386"
                font.pixelSize: 10
            }
            Item { Layout.fillHeight: true }
        }
    }
    }
}
