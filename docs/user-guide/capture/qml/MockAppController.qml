import QtQuick

// Stand-in for AppController, used only to capture user-guide screenshots of
// the production editor QML. render_editor.py fills every telemetry-derived
// value from a real recording; the video is a still placeholder clip.
QtObject {
    id: controller
    required property var widgetModel
    required property var renderContext
    property url videoSource: ""

    // Window and document
    property int windowWidth: 1440
    property int windowHeight: 900
    property int windowX: 0
    property int windowY: 0
    property bool dirty: false
    property string projectPath: ""
    property bool projectLoading: false
    property bool recoveryPending: false
    property bool recoveryDegraded: false
    property string pendingDestructiveAction: ""
    property string sourceMismatchType: ""
    property string sourceMismatchCandidateName: ""
    property string statusText: ""
    property string fixedFontFamily: "DejaVu Sans Mono"
    property string batchImportState: "idle"
    property var eventRuns: []
    property string activeRunId: ""
    property string selectedTemplateId: "motorsport-broadcast-smoke"

    // Sources
    property string videoName: ""
    property string telemetryName: ""
    property string videoLoadState: "ready"
    property string vboLoadState: "ready"
    property bool videoChaptered: false
    property var videoChapters: []
    property var videoChapterList: []
    property int videoChapterIndex: 0
    property url videoChapterSource: videoSource
    property int videoChapterStartMilliseconds: 0

    // Playback and timing
    property real playbackTime: 0
    property real syncOffset: 0
    property real timeScale: 1.0
    property bool syncing: false
    property var syncCandidate: ({})
    property int previewEndPositionMilliseconds: 0
    property string previewEndTimecode: ""
    property int sampleCount: 0
    property real telemetryDuration: 0
    property var channelNames: []
    property var liveValues: ({})
    property real frameRate: 30
    property var lapRange: ({})

    // Laps
    property var lapSummaries: []
    property var outingRanking: ({})

    // Export (KAN-215: appController.exporter)
    property QtObject exporter: QtObject {
        property bool exporting: false
        property string state: "idle"
        property string error: ""
        property real progress: 0
        property bool progressVisible: false
        property var progressInfo: ({})
        property var metrics: ({})
        property string diagnosticLog: ""
        property real diagnosticDroppedCharacters: 0
        function cancel() {}
        function cancelAndQuit() {}
        function dismissProgress() {}
        function copyDiagnostics() {}
    }
    property var exportSourceInfo: ({})

    function formatElapsedTime(seconds) {
        const value = Number(seconds);
        if (!Number.isFinite(value)) return "—";
        const minutes = Math.floor(value / 60);
        const rest = value - minutes * 60;
        return minutes + ":" + (rest < 10 ? "0" : "") + rest.toFixed(3);
    }
    function previewTimecodeForPositionMilliseconds(ms) {
        const frames = Math.round(Number(ms) / 1000 * frameRate);
        const fps = Math.round(frameRate);
        const f = frames % fps, s = Math.floor(frames / fps);
        const pad = n => String(n).padStart(2, "0");
        return pad(Math.floor(s / 3600)) + ":" + pad(Math.floor(s / 60) % 60) + ":" + pad(s % 60) + ":" + pad(f);
    }
    function previewViewport(width, height) {
        const aspect = 16 / 9;
        let w = width, h = width / aspect;
        if (h > height) { h = height; w = height * aspect; }
        return { "x": Math.round((width - w) / 2), "y": Math.round((height - h) / 2), "width": Math.round(w), "height": Math.round(h) };
    }
    // The editor primes playback at this position when the video loads.
    property int initialPositionMilliseconds: 0
    function previewInitialPositionMilliseconds() { return initialPositionMilliseconds; }
    function clampPreviewPositionMilliseconds(ms) { return Math.max(0, Math.min(previewEndPositionMilliseconds, ms)); }
    function valueText(name, decimals) {
        const value = liveValues[name];
        return value === undefined || value === null ? "—" : Number(value).toFixed(decimals);
    }
    property int currentLapNumber: 0
    function lapNumberAtPlayback() { return currentLapNumber; }
    function templateIndexForId(id) {
        const templates = widgetModel.templates;
        for (let i = 0; i < templates.length; ++i) if (templates[i].id === id) return i;
        return 0;
    }
    property var formatOptions: ({})
    function exportFormatOptions() { return formatOptions; }
    function exportFullRangeTimecode(num, den, end) { return end ? previewEndTimecode : "00:00:00:00"; }
    property int bitDepth: 8
    // Same formula as ExportFormat::recommendedVideoBitrate / bitrateForQuality.
    function recommendedExportBitrate(w, h, num, den, quality) {
        const factor = quality === "smaller" ? 0.70 : quality === "high" ? 1.30 : 1.0;
        const base = 12500000 * Math.pow(w * h * (num / den) / (1920 * 1080 * 30), 0.66) * (bitDepth > 8 ? 1.15 : 1.0);
        return Math.round(Math.max(1000000, Math.round(base)) * factor);
    }
    // Same formulas as ExportFormat::estimatedBytes / formatEstimatedSize.
    function estimateExportSize(bitrate, audio, seconds) { return Math.round((bitrate + (audio ? 192000 : 0)) * seconds / 8 * 1.03); }
    function formatEstimatedExportSize(bytes) {
        if (bytes <= 0) return "~0 MiB";
        const mib = bytes / 1048576;
        if (mib < 1024) return "~" + mib.toFixed(mib < 100 ? 1 : 0) + " MiB";
        const gib = mib / 1024;
        return "~" + gib.toFixed(gib < 10 ? 2 : 1) + " GiB";
    }
    function lapExportRange(lap, num, den, handle) { return lapRange; }
    function exportRangeDurationSeconds(num, den, inTimecode, outTimecode) {
        const frames = code => {
            const parts = String(code).split(":").map(Number);
            return parts.length === 4 ? ((parts[0] * 60 + parts[1]) * 60 + parts[2]) * Math.round(num / den) + parts[3] : NaN;
        };
        const count = frames(outTimecode) - frames(inTimecode) + 1;
        return Number.isFinite(count) && count > 0 ? count * den / num : 0;
    }
    function formatTime(ms) { return formatElapsedTime(ms / 1000); }
    function videoFilesNeedReview() { return false; }

    // Actions are ignored while capturing screenshots.
    function saveWindowState() {}
    function markTemplateActive() {}
    function selectTemplate() {}
    function reportPlaybackError() {}
    function setVideoChapter() {}
    function cancelPendingDestructiveAction() {}
}
