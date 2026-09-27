pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtMultimedia

// KAN-39: optional video evidence for the open lap. Video is only ever
// available when the lap's own run is the currently active/loaded one --
// opening a lap from a different run shows "no video for this run" rather
// than silently switching the active run just to follow a lap selection
// (a heavier, more disruptive action with its own reload/generation
// semantics elsewhere). Metrics (chart, map, section-time slider) work
// identically either way; video is additive, never required.
Rectangle {
    id: root
    color: "#090e14"
    // Mirrors AnalysisWindow.qml's own videoSource/playbackPosition/
    // playbackRunning/seekRequested contract for its legacy video pane --
    // the same shape, passed down explicitly since this is a separate
    // component, not sharing that file's local ids.
    property url videoSource
    property real chapterStartMilliseconds: 0 // KAN-105: see AnalysisWindow
    property real playbackPosition: 0
    property bool playbackRunning: false
    signal seekRequested(real milliseconds)
    readonly property var lap: appController.selectedOutingLap
    readonly property bool ready: appController.outingLapDetailState === "ready"
    // KAN-48: swaps the charts for the segment-proposal review of this lap.
    property bool reviewingSegments: false
    // KAN-92: the right pane shows charts, segment review or coasting.
    property bool showingCoasting: false
    onReviewingSegmentsChanged: if (reviewingSegments) showingCoasting = false
    onShowingCoastingChanged: if (showingCoasting) reviewingSegments = false
    onLapChanged: if (!root.lap.reference) root.reviewingSegments = false
    function duration(seconds) {
        return Math.floor(seconds / 60) + ":" + (seconds % 60).toFixed(3).padStart(6, "0");
    }
    // KAN-78: Escape closes this view, except while typing in a text field
    // or area here (a lap-exclusion reason, a segment name), which keeps
    // the view and the edit.
    function textEditorHasFocus() {
        let item = Window.activeFocusItem;
        while (item) {
            if (item instanceof TextInput || item instanceof TextEdit)
                return true;
            item = item.parent;
        }
        return false;
    }
    Shortcut { sequence: "Escape"; enabled: root.visible && !root.textEditorHasFocus(); onActivated: appController.closeOutingLap() }

    // A second, muted, position-mirrored player -- the same pattern
    // AnalysisWindow.qml already uses for its own (legacy-mode) video pane,
    // since one MediaPlayer can only render into one VideoOutput sink at a
    // time and the two modes (legacy vs. Event/outing) are mutually
    // exclusive by construction (eventRuns.length is 0 or >0 for a given
    // document's lifetime), so there's never real contention over playback.
    // Loaded only while this panel is actually visible (a lap is open) --
    // the startup-smoke test asserts the analysis decoder lifecycle stays
    // lazy (flappedear_startup_smoke's "media players closed/open/released"
    // check), so a MediaPlayer must not exist just because the analysis
    // window itself is open with no lap selected.
    Loader {
        id: lapVideoPlayerLoader
        active: root.visible
        sourceComponent: MediaPlayer {
            source: root.videoSource
            videoOutput: lapVideoOutput
            audioOutput: AudioOutput {
                muted: true
            }
            onMediaStatusChanged: {
                if (mediaStatus === MediaPlayer.LoadedMedia) {
                    position = root.playbackPosition - root.chapterStartMilliseconds;
                    if (root.playbackRunning) play();
                }
            }
            onErrorOccurred: function(error, errorString) {
                pause();
                appController.reportPlaybackError(errorString);
            }
        }
    }
    onPlaybackPositionChanged: {
        if (lapVideoPlayerLoader.item
                && Math.abs(lapVideoPlayerLoader.item.position - (root.playbackPosition - root.chapterStartMilliseconds)) > 180)
            lapVideoPlayerLoader.item.position = root.playbackPosition - root.chapterStartMilliseconds;
        // Playback drives the analysis cursor while playing; scrubbing the
        // cursor drives video seeking otherwise (below) -- kept mutually
        // exclusive on root.playbackRunning so the two directions cannot
        // fight/oscillate against each other on the same tick.
        if (root.playbackRunning) appController.followOutingLapVideoPosition(Math.round(root.playbackPosition));
    }
    onPlaybackRunningChanged: {
        if (!lapVideoPlayerLoader.item) return;
        if (root.playbackRunning) lapVideoPlayerLoader.item.play();
        else lapVideoPlayerLoader.item.pause();
    }
    // appController.outingLapVideoPositionMilliseconds is a real Q_PROPERTY
    // (NOTIFY outingLapVideoChanged) -- reading it here is a plain property
    // read, not a function call re-entering reactive state, so this cannot
    // reproduce the KAN-40 binding-loop class of bug.
    readonly property int lapVideoTargetPosition: appController.outingLapVideoPositionMilliseconds
    onLapVideoTargetPositionChanged: {
        if (!root.playbackRunning && appController.outingLapVideoAvailable)
            root.seekRequested(root.lapVideoTargetPosition);
    }
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 14
        spacing: 12
        RowLayout {
            Layout.fillWidth: true
            FeButton {
                objectName: "backToOutingLaps"
                text: qsTr("← All laps")
                onClicked: appController.closeOutingLap()
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 3
                Label {
                    text: (root.lap.type || "") + (root.lap.type === "LAP" ? " " + root.lap.lapNumber : "")
                        + " · " + root.duration(Number(root.lap.durationSeconds || 0))
                    color: "#55e6a5"
                    font.pixelSize: 20
                    font.weight: Font.DemiBold
                }
                Label {
                    text: root.lap.runName || ""
                    Layout.fillWidth: true
                    elide: Text.ElideMiddle
                    color: "#91a0b2"
                    font.pixelSize: 11
                }
            }
            FeButton {
                objectName: "toggleCoasting"
                visible: root.ready && root.lap.type === "LAP"
                text: root.showingCoasting ? qsTr("Show charts") : qsTr("Coasting")
                onClicked: root.showingCoasting = !root.showingCoasting
            }
            FeButton {
                objectName: "toggleSegmentReview"
                visible: root.ready && root.lap.type === "LAP"
                text: root.reviewingSegments ? qsTr("Show charts") : qsTr("Review segments")
                onClicked: root.reviewingSegments = !root.reviewingSegments
            }
        }
        RowLayout {
            visible: root.ready && root.lap.type === "LAP"
            Layout.fillWidth: true
            Label {
                text: root.lap.excluded ? qsTr("Excluded from comparisons") : qsTr("Lap eligibility")
                color: root.lap.excluded ? "#ffb84d" : "#91a0b2"
            }
            FeTextField {
                id: exclusionReason
                objectName: "lapExclusionReason"
                Layout.fillWidth: true
                maximumLength: 256
                placeholderText: qsTr("Reason, e.g. traffic or cooldown")
                text: root.lap.exclusionReason || ""
                readOnly: Boolean(root.lap.excluded)
                Accessible.name: qsTr("Lap exclusion reason")
            }
            FeButton {
                objectName: "toggleLapExclusion"
                text: root.lap.excluded ? qsTr("Restore lap") : qsTr("Exclude lap")
                enabled: Boolean(root.lap.excluded) || exclusionReason.text.trim().length > 0
                onClicked: {
                    if (!appController.setOutingLapExcluded(root.lap.reference, !root.lap.excluded, exclusionReason.text))
                        exclusionError.text = qsTr("Could not change this lap. Reopen the lap and try again.");
                    else exclusionError.text = "";
                }
            }
        }
        Label {
            id: exclusionError
            Layout.fillWidth: true
            visible: text.length > 0
            color: "#ffb84d"
            wrapMode: Text.WordWrap
        }
        BusyIndicator {
            Layout.alignment: Qt.AlignHCenter
            running: appController.outingLapDetailState === "loading"
            visible: running
        }
        Label {
            Layout.fillWidth: true
            visible: appController.outingLapDetailState === "error"
            text: appController.outingLapDetailError
            color: "#ffb84d"
            wrapMode: Text.WordWrap
        }
        RowLayout {
            visible: root.ready
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 10
            ColumnLayout {
                Layout.preferredWidth: Math.max(190, root.width * 0.29)
                Layout.maximumWidth: Layout.preferredWidth
                Layout.fillHeight: true
                spacing: 10
                Rectangle {
                    objectName: "outingLapVideoPane"
                    Layout.fillWidth: true
                    Layout.preferredHeight: visible ? Math.max(110, parent.height * 0.32) : 0
                    Layout.maximumHeight: visible ? -1 : 0
                    color: "#020304"
                    border.color: "#1c2631"
                    visible: appController.outingLapVideoAvailable
                    VideoOutput {
                        id: lapVideoOutput
                        anchors.fill: parent
                        fillMode: VideoOutput.PreserveAspectFit
                    }
                    Label {
                        anchors.centerIn: parent
                        width: Math.max(0, parent.width - 24)
                        visible: lapVideoPlayerLoader.item && lapVideoPlayerLoader.item.error !== MediaPlayer.NoError
                        text: (lapVideoPlayerLoader.item && lapVideoPlayerLoader.item.errorString) || qsTr("The video could not be decoded.")
                        color: "#ff8f99"
                        font.pixelSize: 11
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                    }
                }
                Label {
                    objectName: "outingLapNoVideoLabel"
                    Layout.fillWidth: true
                    // Qt Quick Layouts otherwise defaults a wrapped Label's
                    // minimum width to its unwrapped implicitWidth, which
                    // forced this whole column (and everything to its right)
                    // wider than intended.
                    Layout.minimumWidth: 0
                    visible: !appController.outingLapVideoAvailable
                    text: !root.videoSource.toString() ? qsTr("No video loaded")
                        : root.lap.runId !== undefined && appController.activeRunId !== root.lap.runId
                            ? qsTr("Video not shown — this lap is from a different run")
                            : qsTr("No video for this section")
                    color: "#657386"
                    font.pixelSize: 10
                    wrapMode: Text.WordWrap
                }
                TrackMapPanel {
                    lapDetail: true
                    segmentReview: root.reviewingSegments
                    coastingLayers: coastingLoader.item ? coastingLoader.item.mapLayers : []
                    selectedSegmentIndex: segmentReviewLoader.item ? segmentReviewLoader.item.selectedIndex : -1
                    pickingProgress: segmentReviewLoader.item ? segmentReviewLoader.item.pickTarget !== "" : false
                    onProgressPicked: (x, y) => {
                        if (segmentReviewLoader.item) segmentReviewLoader.item.acceptMapPick(x, y);
                    }
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                }
            }
            AnalysisPanel {
                objectName: "outingLapCharts"
                visible: !root.reviewingSegments && !root.showingCoasting
                lapDetail: true
                mediaDuration: 0
                Layout.fillWidth: true
                Layout.fillHeight: true
            }
            // Created on demand: proposals are computed only when a review is opened.
            Loader {
                id: segmentReviewLoader
                active: root.reviewingSegments && root.ready
                visible: active
                Layout.fillWidth: true
                Layout.fillHeight: true
                sourceComponent: SegmentReviewPanel { objectName: "segmentReviewPanel" }
            }
            Loader {
                id: coastingLoader
                active: root.showingCoasting && root.ready
                visible: active
                Layout.fillWidth: true
                Layout.fillHeight: true
                sourceComponent: CoastingPanel {}
            }
        }
        RowLayout {
            visible: root.ready
            Layout.fillWidth: true
            Label {
                text: qsTr("Section time")
                color: "#91a0b2"
            }
            Slider {
                objectName: "outingLapCursorSlider"
                Layout.fillWidth: true
                from: Number(root.lap.startTime || 0)
                to: Number(root.lap.endTime || 1)
                value: appController.outingLapCursor
                onMoved: appController.outingLapCursor = value
            }
            Label {
                text: root.duration(Math.max(0, appController.outingLapCursor - Number(root.lap.startTime || 0)))
                color: "#f2f6fb"
                font.family: "Menlo"
                Layout.preferredWidth: 94
                horizontalAlignment: Text.AlignRight
            }
        }
        Item { visible: !root.ready; Layout.fillHeight: true }
    }
}
