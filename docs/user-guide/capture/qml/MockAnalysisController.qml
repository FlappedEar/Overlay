import QtQuick

// Stand-in for the Analysis parts of AppController, used only to capture
// user-guide screenshots. render_analysis.py fills it from a real recording;
// `data` provides chart series, map points and values computed from it.
QtObject {
    id: controller
    required property var data

    // Window and import state
    property bool analysisVisible: true
    property int analysisWindowWidth: 1440
    property int analysisWindowHeight: 900
    property int analysisWindowX: 0
    property int analysisWindowY: 0
    property int analysisSidebarWidth: 420
    property int analysisVideoHeight: 0
    property var analysisImportMessages: []
    property var importAnalysisSources: []
    property string batchImportState: "idle"
    property string batchImportError: ""
    property int batchImportProcessed: 0
    property int batchImportTotal: 0
    property bool exporting: false
    property bool projectLoading: false
    property bool recoveryPending: false
    property string pendingDestructiveAction: ""
    property bool comparisonViewOpen: false

    // Event
    property string eventName: ""
    property var eventRuns: []
    property string activeRunId: ""
    property int sampleCount: 0
    property real telemetryDuration: 0
    property real playbackTime: 0
    property real syncOffset: 0
    property real timeScale: 1
    property var channelNames: []
    property var analysisChannels: []
    property var trackPoints: []
    property var currentTrackPoint: ({})

    // Day results
    property var outingLaps: []
    property bool outingLapsLoading: false
    property var outingAnalysisStatus: ({})
    property var outingRanking: ({})
    property var outingCompatibilityGroups: []
    property string outingComparisonGroupId: ""
    property string outingComparisonSelectionState: "automatic"

    // Lap detail
    property var selectedOutingLap: ({})
    property string outingLapDetailState: "idle"
    property string outingLapDetailError: ""
    property real outingLapCursor: 0
    property var outingLapChannels: []
    property var outingLapAvailableChannels: []
    property var outingLapTrack: []
    readonly property var outingLapTrackPoint: data.trackPointAt(outingLapCursor)
    property bool outingLapVideoAvailable: false
    property int outingLapVideoPositionMilliseconds: 0
    property var segmentReviewMapLayers: []
    property var comparisonSlots: []
    property bool comparisonPairReady: false
    property real comparisonProgressAxisLength: 0
    property var comparisonAvailableChannels: []
    property var preferredComparisonChannels: []
    property string comparisonFocusSegmentId: ""
    property int comparisonVideoRevision: 0
    function comparisonPreferredChannels() { return preferredComparisonChannels; }
    function comparisonPersistedChannels() { return []; }
    function comparisonPersistedRangeMeters() { return ({}); }
    function persistComparisonChannels() {}
    function persistComparisonRange() {}
    function comparisonVideo(slot) { return ({ "state": "no-video" }); }
    function comparisonChannelSeriesByProgress(slot, channel, start, end, points) { return data.comparisonSeries(slot, channel, start, end, points); }
    function comparisonDeltaSeriesByProgress(start, end, points) { return data.comparisonDelta(start, end, points); }
    function comparisonOverlayTrack(slot) { return data.overlayTrack(slot); }
    function comparisonPositionAtProgress(slot, meters) { return data.positionAtProgress(slot, meters); }
    function comparisonMapLayerOptions() { return data.mapLayerOptions(); }
    function comparisonMapLayer(layerId, slot) { return data.mapLayer(layerId, slot); }

    function outingLapSeries(channel, start, end, points) { return data.series(channel, start, end, points); }
    function outingLapValueText(channel) { return data.valueText(channel, outingLapCursor); }
    function valueText(channel, decimals) { return "—"; }
    function telemetrySeries() { return ({}); }
    function comparisonLapSeries() { return ({}); }
    function comparisonLapTrack() { return []; }
    function runTrackConfiguration() { return ({}); }
    function formatElapsedTime(seconds) {
        const value = Number(seconds);
        const minutes = Math.floor(value / 60);
        return minutes + ":" + (value - minutes * 60).toFixed(3).padStart(6, "0");
    }

    // Actions are ignored while capturing screenshots.
    function saveAnalysisWindowState() {}
    function selectOutingLapReference() { return false; }
    function selectOutingComparisonGroup() { return false; }
    function followOutingLapVideoPosition() { return false; }
    function reportPlaybackError() {}
}
