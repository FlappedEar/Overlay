pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// F1-debrief-style A/B lap comparison: both laps overlaid on shared charts
// and one track map (shared scale), aligned on the shared cross-lap
// track-progress axis (KAN-31/32/33) so a corner lines up at the same
// position for both laps even on different racing lines -- not just "meters
// since each lap's own start". One shared zoom/pan window keeps every
// channel row and the map moving together. Scroll to zoom (centered under
// the cursor), scroll sideways to pan, or drag to select a section; hover
// shows both laps' values, their delta, and where each car was on track.
Rectangle {
    id: root
    color: "#090e14"
    readonly property var slots: appController.comparisonSlots
    // "Δ time" is a synthetic pseudo-channel (the cumulative time gap between
    // the laps, not a recorded telemetry channel) offered alongside the real
    // ones once both laps are loaded.
    readonly property var availableChannels: appController.comparisonAvailableChannels.length > 0
        ? ["Δ time"].concat(appController.comparisonAvailableChannels) : []
    property var visibleChannels: []
    readonly property real totalMeters: Math.max(1, appController.comparisonProgressAxisLength)
    property real zoomStart: 0
    property real zoomEnd: totalMeters
    property real hoverDistanceMeters: -1
    readonly property bool zoomed: zoomStart > 1e-3 || zoomEnd < totalMeters - 1e-3
    // KAN-55: toggles the channel-chart area for the Corner Analyzer
    // (ComparisonSegmentPanel) -- shares this same zoomStart/zoomEnd/
    // hoverDistanceMeters state, not a separate cursor.
    property bool showingCornerAnalyzer: false
    // KAN-66: the G-G column; only one side column at a time.
    property bool showingGg: false
    onShowingCornerAnalyzerChanged: if (root.showingCornerAnalyzer) root.showingGg = false
    onShowingGgChanged: if (root.showingGg) root.showingCornerAnalyzer = false
    // KAN-41: on a fresh pair (this document's persisted A/B just restored, or
    // freshly (re)opening the compare view), apply the persisted range/channel
    // selection instead of resetting to full range/defaults, once per such
    // opening. Re-armed whenever the view closes (including the forced close
    // on a new document load) so a later, different document's own persisted
    // state gets its turn; harmless no-op if the same pair stays loaded, since
    // the persisted value already equals the live one in that case.
    property bool pendingRangeRestore: true
    property bool pendingChannelsRestore: true
    onTotalMetersChanged: {
        if (root.pendingRangeRestore) {
            if (!appController.comparisonPairReady) return;
            const persisted = appController.comparisonPersistedRangeMeters();
            if (persisted.endMeters > persisted.startMeters && persisted.startMeters >= 0
                    && persisted.endMeters <= root.totalMeters + 1e-3) {
                root.zoomStart = persisted.startMeters;
                root.zoomEnd = persisted.endMeters;
            } else {
                root.zoomStart = 0;
                root.zoomEnd = root.totalMeters;
            }
            root.pendingRangeRestore = false;
        } else {
            root.zoomStart = 0;
            root.zoomEnd = root.totalMeters;
        }
    }
    // Deferred, not a direct call: persisting synchronously here would call
    // back into markPersistentChange -> documentStateChanged ->
    // invalidateComparisonLaps -> comparisonSlotsChanged while this property's
    // own binding (which depends on comparisonSlotsChanged) is still on the
    // call stack -- a genuine re-entrant evaluation, not just a cosmetic
    // warning. Qt.callLater also coalesces a zoom-drag's many change events
    // into one write instead of one per frame.
    onZoomStartChanged: if (!root.pendingRangeRestore) Qt.callLater(() => appController.persistComparisonRange(root.zoomStart, root.zoomEnd))
    onZoomEndChanged: if (!root.pendingRangeRestore) Qt.callLater(() => appController.persistComparisonRange(root.zoomStart, root.zoomEnd))
    Connections {
        target: appController
        function onComparisonFocusSegmentIdChanged() {
            if (appController.comparisonFocusSegmentId.length > 0) root.showingCornerAnalyzer = true;
        }
        function onComparisonViewOpenChanged() {
            if (!appController.comparisonViewOpen) {
                root.pendingRangeRestore = true;
                root.pendingChannelsRestore = true;
            }
        }
    }

    function defaultChannels(available) {
        if (available.length === 0) return [];
        // The recording's own names for speed/throttle/brake (for example
        // "velocity", "throttle_pos-obd"), after the Δ time pseudo-channel.
        const preferred = ["Δ time"].concat(appController.comparisonPreferredChannels());
        const picked = preferred.filter(channel => available.indexOf(channel) >= 0);
        return picked.length > 0 ? picked.slice(0, 4) : available.slice(0, Math.min(2, available.length));
    }
    onAvailableChannelsChanged: {
        if (root.pendingChannelsRestore) {
            if (root.availableChannels.length === 0) return;
            const persisted = appController.comparisonPersistedChannels().filter(
                channel => root.availableChannels.indexOf(channel) >= 0);
            root.visibleChannels = persisted.length > 0 ? persisted : root.defaultChannels(root.availableChannels);
            root.pendingChannelsRestore = false;
        } else {
            const stillValid = root.visibleChannels.filter(channel => root.availableChannels.indexOf(channel) >= 0);
            root.visibleChannels = stillValid.length > 0 ? stillValid : root.defaultChannels(root.availableChannels);
        }
    }
    onVisibleChannelsChanged: if (!root.pendingChannelsRestore) Qt.callLater(() => appController.persistComparisonChannels(root.visibleChannels))
    function toggleChannel(channel) {
        const channels = root.visibleChannels.slice();
        const index = channels.indexOf(channel);
        if (index >= 0) channels.splice(index, 1);
        else if (channel && channels.length < 4) channels.push(channel);
        root.visibleChannels = channels;
    }
    function replaceChannel(previous, next) {
        const channels = root.visibleChannels.slice();
        const index = channels.indexOf(previous);
        if (index >= 0 && (previous === next || channels.indexOf(next) < 0)) {
            channels[index] = next;
            root.visibleChannels = channels;
        }
    }

    function duration(seconds) {
        return Math.floor(seconds / 60) + ":" + (seconds % 60).toFixed(3).padStart(6, "0");
    }
    function lapLabel(index) {
        const lap = root.slots[index] ? root.slots[index].lap : ({});
        return (index === 0 ? qsTr("Lap A · ") : qsTr("Lap B · ")) + (lap.runName || "")
            + " · LAP " + (lap.lapNumber || "") + " · " + root.duration(Number(lap.durationSeconds || 0));
    }
    // KAN-40: compatibility group, exclusions, GPS coverage and the reason a
    // slot isn't usable, in plain always-visible text -- never behind a
    // mouse-only hover/tooltip, so nothing here needs a keyboard interaction
    // to reach. Per-channel unavailability ("Channel is not available in both
    // laps") is already shown by ComparisonOverlayChart; this covers the
    // slot-level context that view doesn't have. Computed in C++
    // (comparisonSlots' statusText/statusIsWarning fields), not here, so QML
    // only ever does a plain property-path read off comparisonSlots.
    function pairStatusText() {
        const a = root.slots[0], b = root.slots[1];
        if (a && b && a.state === "ready" && b.state === "ready"
                && a.lap.compatibilityGroupId !== b.lap.compatibilityGroupId)
            return qsTr("Lap A and Lap B are from different, incompatible track configurations.");
        return qsTr("Select two ready, compatible laps to compare.");
    }
    Shortcut { sequence: "Escape"; enabled: root.visible; onActivated: appController.comparisonViewOpen = false }
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 14
        spacing: 10
        RowLayout {
            Layout.fillWidth: true
            FeButton {
                objectName: "backFromComparison"
                text: qsTr("← All laps")
                onClicked: appController.comparisonViewOpen = false
            }
            Label {
                Layout.fillWidth: true
                text: qsTr("Compare laps · A / B")
                color: "#55e6a5"
                font.pixelSize: 20
                font.weight: Font.DemiBold
            }
        }
        ColumnLayout {
            objectName: "comparisonSlotStatusList"
            Layout.fillWidth: true
            spacing: 2
            visible: root.slots.length === 2 && (root.slots[0].state !== "empty" || root.slots[1].state !== "empty")
            Repeater {
                objectName: "comparisonSlotStatusRepeater"
                model: 2
                delegate: Label {
                    required property int index
                    objectName: "comparisonSlotStatus" + index
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    font.pixelSize: 11
                    // Plain fields off comparisonSlots (computed in C++,
                    // KAN-40) -- the same shape of read as lapLabel() above,
                    // not a new reactive function call back into root.slots.
                    text: root.slots[index] ? root.slots[index].statusText : ""
                    color: root.slots[index] && root.slots[index].statusIsWarning ? "#ffb84d" : "#657386"
                    Accessible.role: Accessible.StaticText
                    Accessible.name: text
                }
            }
        }
        Label {
            objectName: "comparisonPairStatus"
            Layout.fillWidth: true
            visible: !appController.comparisonPairReady
            text: root.pairStatusText()
            color: "#91a0b2"
            wrapMode: Text.WordWrap
        }
        ColumnLayout {
            visible: appController.comparisonPairReady
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 6
            RowLayout {
                Layout.fillWidth: true
                spacing: 20
                Label {
                    objectName: "comparisonLapLabelA"
                    text: root.lapLabel(0)
                    color: "#55e6a5"
                    font.pixelSize: 13
                    elide: Text.ElideMiddle
                }
                Label {
                    objectName: "comparisonLapLabelB"
                    text: root.lapLabel(1)
                    color: "#d95926"
                    font.pixelSize: 13
                    elide: Text.ElideMiddle
                }
                Item { Layout.fillWidth: true }
                FeButton {
                    objectName: "comparisonToggleCornerAnalyzer"
                    compact: true
                    text: root.showingCornerAnalyzer ? qsTr("Hide Corner Analyzer") : qsTr("Corner Analyzer")
                    onClicked: root.showingCornerAnalyzer = !root.showingCornerAnalyzer
                }
                FeButton {
                    objectName: "comparisonToggleGg"
                    compact: true
                    text: root.showingGg ? qsTr("Hide G-G") : qsTr("G-G")
                    onClicked: root.showingGg = !root.showingGg
                }
                FeButton {
                    objectName: "comparisonResetZoom"
                    visible: root.zoomed
                    compact: true
                    text: qsTr("Reset zoom (%1 m)").arg((root.zoomEnd - root.zoomStart).toFixed(0))
                    onClicked: { root.zoomStart = 0; root.zoomEnd = root.totalMeters; }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Label {
                    text: qsTr("CHANNELS")
                    color: "#8d9aaa"
                    font.pixelSize: 9
                    font.weight: Font.DemiBold
                }
                Label {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignRight
                    text: qsTr("%1/4").arg(root.visibleChannels.length)
                    color: "#687789"
                    font.pixelSize: 9
                }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                FeComboBox {
                    id: channelPicker
                    objectName: "comparisonAddChannelPicker"
                    Layout.fillWidth: true
                    Layout.minimumWidth: 80
                    implicitHeight: 28
                    model: root.availableChannels.filter(channel => root.visibleChannels.indexOf(channel) < 0)
                    enabled: count > 0 && root.visibleChannels.length < 4
                }
                FeButton {
                    objectName: "comparisonAddChannel"
                    compact: true
                    text: qsTr("Add channel")
                    enabled: channelPicker.enabled
                    onClicked: root.toggleChannel(channelPicker.currentText)
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 8
                ComparisonOverlayMap {
                    id: overlayMap
                    Layout.preferredWidth: Math.max(160, root.width * (root.showingCornerAnalyzer || root.showingGg ? 0.2 : 0.26))
                    Layout.fillHeight: true
                    hoverDistanceMeters: root.hoverDistanceMeters
                    rangeStartMeters: root.zoomStart
                    rangeEndMeters: root.zoomEnd
                    totalMeters: root.totalMeters
                }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    radius: 8
                    color: "#070b10"
                    border.color: "#1c2631"
                    clip: true
                    SplitView {
                        id: chartSplit
                        anchors.fill: parent
                        anchors.margins: 6
                        orientation: Qt.Vertical
                        handle: Rectangle {
                            implicitHeight: 5
                            color: SplitHandle.pressed ? "#55e6a5" : SplitHandle.hovered ? "#334556" : "#14202a"
                        }
                        Repeater {
                            objectName: "comparisonChannelRows"
                            model: root.visibleChannels
                            delegate: Item {
                                id: rowItem
                                required property int index
                                required property var modelData
                                SplitView.fillWidth: true
                                SplitView.preferredHeight: Math.max(64, chartSplit.height / Math.max(1, root.visibleChannels.length))
                                SplitView.minimumHeight: 50
                                ColumnLayout {
                                    anchors.fill: parent
                                    spacing: 2
                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: 4
                                        FeComboBox {
                                            objectName: "comparisonReplaceChannel-" + rowItem.modelData
                                            Layout.preferredWidth: 150
                                            implicitHeight: 22
                                            model: root.availableChannels.filter(
                                                channel => channel === rowItem.modelData || root.visibleChannels.indexOf(channel) < 0)
                                            currentIndex: model.indexOf(rowItem.modelData)
                                            onActivated: index => root.replaceChannel(rowItem.modelData, model[index])
                                        }
                                        ToolButton {
                                            objectName: "comparisonRemoveChannel-" + rowItem.modelData
                                            implicitWidth: 20
                                            implicitHeight: 22
                                            text: "×"
                                            Accessible.name: qsTr("Remove %1").arg(rowItem.modelData)
                                            onClicked: root.toggleChannel(rowItem.modelData)
                                        }
                                        Item { Layout.fillWidth: true }
                                    }
                                    ComparisonOverlayChart {
                                        objectName: "comparisonOverlayChart-" + rowItem.modelData
                                        Layout.fillWidth: true
                                        Layout.fillHeight: true
                                        channel: rowItem.modelData
                                        zoomStart: root.zoomStart
                                        zoomEnd: root.zoomEnd
                                        totalMeters: root.totalMeters
                                        hoverDistanceMeters: root.hoverDistanceMeters
                                        onZoomRequested: (start, end) => { root.zoomStart = start; root.zoomEnd = end; }
                                        onHovered: meters => root.hoverDistanceMeters = meters
                                    }
                                }
                            }
                        }
                    }
                    Label {
                        anchors.centerIn: parent
                        visible: root.visibleChannels.length === 0
                        text: qsTr("Add a telemetry channel to compare.")
                        color: "#657386"
                        font.pixelSize: 11
                    }
                }
                // KAN-117: beside the map and charts, sharing their zoom.
                ComparisonSegmentPanel {
                    objectName: "comparisonSegmentPanel"
                    visible: root.showingCornerAnalyzer
                    Layout.preferredWidth: 420
                    Layout.minimumWidth: 380
                    Layout.maximumWidth: 420
                    Layout.fillHeight: true
                    onRangeRequested: (start, end) => { root.zoomStart = start; root.zoomEnd = end; }
                    onHovered: meters => root.hoverDistanceMeters = meters
                }
                ComparisonGgPanel {
                    objectName: "comparisonGgPanel"
                    visible: root.showingGg
                    Layout.preferredWidth: 380
                    Layout.minimumWidth: 320
                    Layout.maximumWidth: 380
                    Layout.fillHeight: true
                    rangeStartMeters: root.zoomStart
                    rangeEndMeters: root.zoomEnd
                    totalMeters: root.totalMeters
                }
            }
        }
    }
}
