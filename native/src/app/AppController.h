#pragma once

#include "app/VideoChapterReview.h"
#include "export/MediaTimeline.h"
#include "app/BestLapFinder.h"
#include "app/DocumentController.h"
#include "app/DocumentHost.h"
#include "telemetry/TelemetrySessionCache.h"

#include "telemetry/LapTiming.h"
#include "telemetry/TelemetrySession.h"
#include "telemetry/TelemetryRenderContext.h"
#include "telemetry/TrackGeometry.h"
#include <utility>
#include "export/MediaProbe.h"
#include "export/ExportDiagnostics.h"
#include "export/ExportOutputTransaction.h"
#include "export/ExportProcessSupervisor.h"
#include "export/PersistentExportLog.h"
#include "export/BoundedProcessOutput.h"
#include "telemetry/TelemetrySyncEngine.h"
#include "widgets/WidgetModel.h"
#include "project/ProjectWriter.h"
#include "project/ProjectDocumentState.h"
#include "project/ProjectRecoveryStore.h"
#include "project/ProjectSourceReference.h"

#include <QFutureWatcher>
#include <QProcess>
#include <QTemporaryFile>
#include <QObject>
#include <QJsonObject>
#include <QSettings>
#include <QTimer>
#include <QUrl>
#include <QVariant>
#include <QSet>
#include <functional>
#include <array>
#include <atomic>
#include <memory>
#include <optional>

class ExportTests;
class ProjectTests;
class SourceTests;

namespace FlappedEar {

class AppController final : public QObject, private DocumentHost {
    Q_OBJECT
    Q_PROPERTY(QUrl videoSource READ videoSource NOTIFY videoSourceChanged)
    Q_PROPERTY(QString videoName READ videoName NOTIFY videoSourceChanged)
    Q_PROPERTY(QString telemetryName READ telemetryName NOTIFY telemetryChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
    // KAN-125: what the storage migration could not bring across, shown once at startup.
    Q_PROPERTY(QString startupNotice READ startupNotice CONSTANT)
    Q_PROPERTY(QStringList channelNames READ channelNames NOTIFY telemetryChanged)
    Q_PROPERTY(qsizetype sampleCount READ sampleCount NOTIFY telemetryChanged)
    Q_PROPERTY(double telemetryDuration READ telemetryDuration NOTIFY telemetryChanged)
    Q_PROPERTY(double playbackTime READ playbackTime WRITE setPlaybackTime NOTIFY playbackTimeChanged)
    Q_PROPERTY(double syncOffset READ syncOffset WRITE setSyncOffset NOTIFY syncChanged)
    Q_PROPERTY(double timeScale READ timeScale WRITE setTimeScale NOTIFY syncChanged)
    Q_PROPERTY(bool syncing READ syncing NOTIFY syncingChanged)
    Q_PROPERTY(bool exporting READ exporting NOTIFY exportChanged)
    Q_PROPERTY(int exportProgress READ exportProgress NOTIFY exportChanged)
    Q_PROPERTY(QString exportState READ exportState NOTIFY exportChanged)
    Q_PROPERTY(QString exportError READ exportError NOTIFY exportChanged)
    Q_PROPERTY(QVariantMap exportSourceInfo READ exportSourceInfo NOTIFY exportChanged)
    Q_PROPERTY(QVariantMap exportMetrics READ exportMetrics NOTIFY exportChanged)
    Q_PROPERTY(QVariantMap exportProgressInfo READ exportProgressInfo NOTIFY exportChanged)
    Q_PROPERTY(bool exportProgressVisible READ exportProgressVisible NOTIFY exportChanged)
    Q_PROPERTY(QString exportDiagnosticLog READ exportDiagnosticLog NOTIFY exportDiagnosticLogChanged)
    Q_PROPERTY(qint64 exportDiagnosticDroppedCharacters READ exportDiagnosticDroppedCharacters NOTIFY exportDiagnosticLogChanged)
    Q_PROPERTY(QString fixedFontFamily READ fixedFontFamily CONSTANT)
    Q_PROPERTY(QVariantMap syncCandidate READ syncCandidate NOTIFY syncCandidateChanged)
    Q_PROPERTY(QVariant speed READ speed NOTIFY liveValuesChanged)
    Q_PROPERTY(QVariant rpm READ rpm NOTIFY liveValuesChanged)
    Q_PROPERTY(QVariant heartRate READ heartRate NOTIFY liveValuesChanged)
    Q_PROPERTY(TelemetryRenderContext *renderContext READ renderContext CONSTANT)
    Q_PROPERTY(WidgetModel *widgetModel READ widgetModel CONSTANT)
    Q_PROPERTY(QVariantList trackPoints READ trackPoints NOTIFY telemetryChanged)
    Q_PROPERTY(QVariantMap currentTrackPoint READ currentTrackPoint NOTIFY liveValuesChanged)
    Q_PROPERTY(QString lapTimingStatus READ lapTimingStatus NOTIFY telemetryChanged)
    Q_PROPERTY(QVariantList lapSummaries READ lapSummaries NOTIFY telemetryChanged)
    Q_PROPERTY(QVariantList lapNavigationSegments READ lapNavigationSegments NOTIFY lapNavigationChanged)
    Q_PROPERTY(int windowX READ windowX CONSTANT)
    Q_PROPERTY(int windowY READ windowY CONSTANT)
    Q_PROPERTY(int windowWidth READ windowWidth CONSTANT)
    Q_PROPERTY(int windowHeight READ windowHeight CONSTANT)
    Q_PROPERTY(QUrl projectPath READ projectPath NOTIFY documentStateChanged)
    Q_PROPERTY(QString eventName READ eventName NOTIFY documentStateChanged)
    Q_PROPERTY(QVariantList eventRuns READ eventRuns NOTIFY documentStateChanged)
    Q_PROPERTY(QString activeRunId READ activeRunId NOTIFY documentStateChanged)
    // KAN-185: the day's best lap from lap detection, for the export dialog,
    // without the Lap Analysis window. See BestLapFinder for its states.
    Q_PROPERTY(QVariantMap dayBestLap READ dayBestLap NOTIFY dayBestLapChanged)
    Q_PROPERTY(bool dirty READ dirty NOTIFY documentStateChanged)
    Q_PROPERTY(quint64 lastSavedRevision READ lastSavedRevision NOTIFY documentStateChanged)
    Q_PROPERTY(QString pendingDestructiveAction READ pendingDestructiveAction NOTIFY destructiveActionChanged)
    Q_PROPERTY(QString videoLoadState READ videoLoadState NOTIFY sourceLoadStateChanged)
    // KAN-105: a video of several chapters plays as one timeline. The player
    // shows the current chapter's file; positions in QML are timeline time.
    Q_PROPERTY(bool videoChaptered READ videoChaptered NOTIFY videoChaptersChanged)
    Q_PROPERTY(int videoChapterIndex READ videoChapterIndex NOTIFY videoChaptersChanged)
    Q_PROPERTY(QUrl videoChapterSource READ videoChapterSource NOTIFY videoChaptersChanged)
    Q_PROPERTY(qint64 videoChapterStartMilliseconds READ videoChapterStartMilliseconds NOTIFY videoChaptersChanged)
    Q_PROPERTY(QVariantList videoChapterList READ videoChapterList NOTIFY videoChaptersChanged)
    // KAN-104: GoPro chapter-group review before a video is loaded.
    Q_PROPERTY(FlappedEar::VideoChapterReview *videoChapters READ videoChapters CONSTANT)
    Q_PROPERTY(QString vboLoadState READ vboLoadState NOTIFY sourceLoadStateChanged)
    Q_PROPERTY(qint64 previewEndPositionMilliseconds READ previewEndPositionMilliseconds NOTIFY previewMetadataChanged)
    Q_PROPERTY(QString previewEndTimecode READ previewEndTimecode NOTIFY previewMetadataChanged)
    Q_PROPERTY(bool projectLoading READ projectLoading NOTIFY projectLoadChanged)
    Q_PROPERTY(QString projectLoadStage READ projectLoadStage NOTIFY projectLoadChanged)
    Q_PROPERTY(QString projectLoadError READ projectLoadError NOTIFY projectLoadChanged)
    Q_PROPERTY(bool recoveryPending READ recoveryPending NOTIFY recoveryChanged)
    Q_PROPERTY(bool recoveryDegraded READ recoveryDegraded NOTIFY recoveryChanged)
    Q_PROPERTY(QString recoveryError READ recoveryError NOTIFY recoveryChanged)
    Q_PROPERTY(QString sourceMismatchType READ sourceMismatchType NOTIFY sourceMismatchChanged)
    Q_PROPERTY(QString sourceMismatchCandidateName READ sourceMismatchCandidateName NOTIFY sourceMismatchChanged)
    Q_PROPERTY(QString selectedTemplateId READ selectedTemplateId NOTIFY templateUiStateChanged)
    Q_PROPERTY(QString activeTemplateId READ activeTemplateId NOTIFY templateUiStateChanged)

public:
    explicit AppController(QObject *parent = nullptr, QString recoveryPath = {},
                           ProjectRecoveryStore::Operations recoveryOperations = {});
    ~AppController() override;

    [[nodiscard]] QUrl videoSource() const;
    [[nodiscard]] QString videoName() const;
    [[nodiscard]] QString telemetryName() const;
    [[nodiscard]] QString statusText() const;
    [[nodiscard]] QString startupNotice() const { return m_startupNotice; }
    // Before QML loads; the property is constant afterwards.
    void setStartupNotice(QString notice) { m_startupNotice = std::move(notice); }
    [[nodiscard]] QStringList channelNames() const;
    [[nodiscard]] qsizetype sampleCount() const;
    [[nodiscard]] double telemetryDuration() const;
    [[nodiscard]] double playbackTime() const;
    [[nodiscard]] double syncOffset() const;
    [[nodiscard]] double timeScale() const;
    [[nodiscard]] bool syncing() const;
    [[nodiscard]] bool exporting() const;
    // KAN-124: the document is busy with an operation that must not be
    // interleaved with document edits (today: an export). The document's
    // guards use this, not the overlay's export state.
    [[nodiscard]] bool documentBusy() const override;
    [[nodiscard]] int exportProgress() const;
    [[nodiscard]] QString exportState() const;
    [[nodiscard]] QString exportError() const;
    [[nodiscard]] QVariantMap exportSourceInfo() const;
    [[nodiscard]] QVariantMap exportMetrics() const;
    [[nodiscard]] QVariantMap exportProgressInfo() const;
    [[nodiscard]] bool exportProgressVisible() const;
    [[nodiscard]] QString exportDiagnosticLog() const;
    [[nodiscard]] qint64 exportDiagnosticDroppedCharacters() const { return m_exportDiagnosticLog.droppedCharacters(); }
    [[nodiscard]] QString fixedFontFamily() const;
    [[nodiscard]] QVariantMap syncCandidate() const;
    [[nodiscard]] QVariant speed() const;
    [[nodiscard]] QVariant rpm() const;
    [[nodiscard]] QVariant heartRate() const;
    [[nodiscard]] TelemetryRenderContext *renderContext();
    [[nodiscard]] WidgetModel *widgetModel();
    [[nodiscard]] QVariantList trackPoints() const;
    [[nodiscard]] QVariantMap currentTrackPoint() const;
    [[nodiscard]] QString lapTimingStatus() const;
    [[nodiscard]] QVariantList lapSummaries() const;
    [[nodiscard]] QVariantList lapNavigationSegments() const;
    [[nodiscard]] int windowX() const;
    [[nodiscard]] int windowY() const;
    [[nodiscard]] int windowWidth() const;
    [[nodiscard]] int windowHeight() const;
    [[nodiscard]] QUrl projectPath() const { return m_document.projectPath(); }
    [[nodiscard]] QString eventName() const { return m_document.eventName(); }
    [[nodiscard]] QVariantList eventRuns() const { return m_document.eventRuns(); }
    [[nodiscard]] QString activeRunId() const { return m_document.activeRunId(); }
    [[nodiscard]] bool dirty() const { return m_document.dirty(); }
    [[nodiscard]] quint64 lastSavedRevision() const { return m_document.lastSavedRevision(); }
    [[nodiscard]] QString pendingDestructiveAction() const { return m_document.pendingDestructiveAction(); }
    [[nodiscard]] QString videoLoadState() const;
    [[nodiscard]] QString vboLoadState() const;
    [[nodiscard]] bool projectLoading() const { return m_document.projectLoading(); }
    [[nodiscard]] QString projectLoadStage() const { return m_document.projectLoadStage(); }
    [[nodiscard]] QString projectLoadError() const { return m_document.projectLoadError(); }
    [[nodiscard]] bool recoveryPending() const { return m_document.recoveryPending(); }
    [[nodiscard]] bool recoveryDegraded() const { return m_document.recoveryDegraded(); }
    [[nodiscard]] QString recoveryError() const { return m_document.recoveryError(); }
    [[nodiscard]] QString sourceMismatchType() const;
    [[nodiscard]] QString sourceMismatchCandidateName() const;
    [[nodiscard]] QString selectedTemplateId() const;
    [[nodiscard]] QString activeTemplateId() const;

    Q_INVOKABLE void loadVideo(const QUrl &url);
    // KAN-105: a reviewed chapter group, played as one timeline.
    void loadVideoChapters(const QList<QUrl> &files);
    [[nodiscard]] int videoChapterIndex() const { return m_videoChapterIndex; }
    [[nodiscard]] QUrl videoChapterSource() const;
    [[nodiscard]] qint64 videoChapterStartMilliseconds() const;
    [[nodiscard]] QVariantList videoChapterList() const;
    // {chapter, localMilliseconds, gap} for a timeline position; {} outside it.
    Q_INVOKABLE QVariantMap locateVideoTimeline(qint64 timelineMilliseconds) const;
    // Shows chapter `index` in the player (its file, or a gap).
    Q_INVOKABLE bool setVideoChapter(int index);
    [[nodiscard]] VideoChapterReview *videoChapters() { return &m_videoChapters; }
    // True when the chosen files need a chapter review rather than a direct load.
    Q_INVOKABLE bool videoFilesNeedReview(const QList<QUrl> &urls) const { return VideoChapterReview::needsReview(urls); }
    Q_INVOKABLE void loadVbo(const QUrl &url);
    Q_INVOKABLE bool selectEventRun(const QString &runId) { return m_document.selectEventRun(runId); }
    [[nodiscard]] QVariantMap dayBestLap() const { return m_bestLapFinder.result(); }
    Q_INVOKABLE void requestDayBestLap() { m_bestLapFinder.request(); }
    // "1:49.898" for anything a minute or longer, "28.662 s" below that; "—"
    // when not finite. One formatter for every lap and segment time.
    Q_INVOKABLE static QString formatElapsedTime(double seconds);
    Q_INVOKABLE bool beginBatchImport(const QList<QUrl> &urls) { return m_document.beginBatchImport(urls); }
    Q_INVOKABLE void cancelBatchImport() { m_document.cancelBatchImport(); }
    Q_INVOKABLE bool confirmBatchImport(const QString &name, bool append, const QVariantList &choices) { return m_document.confirmBatchImport(name, append, choices); }
    Q_INVOKABLE void relinkVideo(const QUrl &url);
    Q_INVOKABLE void relinkVbo(const QUrl &url);
    Q_INVOKABLE void resolveSourceMismatch(bool acceptReplacement);
    Q_INVOKABLE QString valueText(const QString &channelName, int decimals = 2) const;
    Q_INVOKABLE QVariant telemetryValue(const QString &channelName) const;
    Q_INVOKABLE QVariantMap telemetrySeries(
        const QString &channelName, double videoStart, double videoEnd, int maximumPoints) const;
    Q_INVOKABLE qint64 videoMillisecondsForTelemetryTime(double telemetryTime) const;
    // The timed lap under the playhead, or 0 outside every lap (hotlap widget option).
    Q_INVOKABLE int lapNumberAtPlayback() const;
    Q_INVOKABLE void requestNewProject() { m_document.requestNewProject(); }
    Q_INVOKABLE void requestOpenProject(const QUrl &url) { m_document.requestOpenProject(url); }
    Q_INVOKABLE void requestQuit() { m_document.requestQuit(); }
    Q_INVOKABLE void resolveDestructiveAction(const QString &decision) { m_document.resolveDestructiveAction(decision); }
    Q_INVOKABLE void cancelPendingDestructiveAction() { m_document.cancelPendingDestructiveAction(); }
    Q_INVOKABLE bool saveCurrentProject() { return m_document.saveCurrentProject(); }
    Q_INVOKABLE bool saveProject(const QUrl &url) { return m_document.saveProject(url); }
    Q_INVOKABLE void resolveStartupRecovery(const QString &decision) { m_document.resolveStartupRecovery(decision); }
    Q_INVOKABLE void autoSync();
    Q_INVOKABLE void applySyncCandidate();
    Q_INVOKABLE void ignoreSyncCandidate();
    Q_INVOKABLE bool startExport(
        const QUrl &output,
        int outputWidth, int outputHeight, qint64 frameRateNumerator, qint64 frameRateDenominator,
        qint64 videoBitrate,
        bool audioEnabled,
        bool customRange,
        const QString &rangeIn,
        const QString &rangeOut,
        bool overwriteAllowed = false);
    Q_INVOKABLE QVariantMap exportFormatOptions() const;
    Q_INVOKABLE QString exportFullRangeTimecode(
        qint64 frameRateNumerator, qint64 frameRateDenominator, bool outPoint) const;
    Q_INVOKABLE QVariantMap lapExportRange(
        int lapNumber, qint64 frameRateNumerator, qint64 frameRateDenominator,
        int handleSeconds) const;
    Q_INVOKABLE double exportRangeDurationSeconds(
        qint64 frameRateNumerator, qint64 frameRateDenominator,
        const QString &rangeIn, const QString &rangeOut) const;
    Q_INVOKABLE qint64 recommendedExportBitrate(int width, int height, qint64 numerator, qint64 denominator, const QString &quality) const;
    Q_INVOKABLE qint64 estimateExportSize(qint64 videoBitrate, bool audioEnabled, double seconds) const;
    Q_INVOKABLE QString formatEstimatedExportSize(qint64 bytes) const;
    Q_INVOKABLE QVariantMap previewViewport(int availableWidth, int availableHeight) const;
    Q_INVOKABLE qint64 previewEndPositionMilliseconds() const;
    Q_INVOKABLE qint64 previewInitialPositionMilliseconds() const;
    Q_INVOKABLE qint64 clampPreviewPositionMilliseconds(qint64 requestedMilliseconds) const;
    Q_INVOKABLE QString previewTimecodeForPositionMilliseconds(qint64 positionMilliseconds) const;
    Q_INVOKABLE QString previewEndTimecode() const;
    Q_INVOKABLE void reportPlaybackError(const QString &message);
    Q_INVOKABLE void cancelExport();
    Q_INVOKABLE void cancelExportAndQuit();
    Q_INVOKABLE void dismissExportProgress();
    Q_INVOKABLE void copyExportDiagnostics();
    Q_INVOKABLE void saveWindowState(int x, int y, int width, int height);
    Q_INVOKABLE int templateIndexForId(const QString &templateId) const;
    Q_INVOKABLE void selectTemplate(const QString &templateId);
    Q_INVOKABLE void reconcileTemplateSelection();
    Q_INVOKABLE bool applyTemplate(const QString &templateId);
    Q_INVOKABLE void markTemplateActive(const QString &templateId);
    Q_INVOKABLE bool saveActiveTemplate();

public slots:
    void setPlaybackTime(double seconds);
    void setSyncOffset(double seconds);
    void setTimeScale(double scale);

signals:
    void dayBestLapChanged();
    void videoSourceChanged();
    void videoChaptersChanged();
    void telemetryChanged();
    void lapNavigationChanged();
    void statusTextChanged();
    void playbackTimeChanged();
    void syncChanged();
    void syncingChanged();
    void exportChanged();
    void exportDiagnosticLogChanged();
    void syncCandidateChanged();
    void liveValuesChanged();
    void documentStateChanged();
    void destructiveActionChanged();
    void sourceLoadStateChanged();
    void previewMetadataChanged();
    void projectLoadChanged();
    void recoveryChanged();
    void sourceMismatchChanged();
    void templateUiStateChanged();
    void saveAsRequested();
    void quitApproved();

private:
    friend class ::ExportTests; // Controlled asynchronous completion in regression tests.
    friend class ::ProjectTests;
    friend class ::SourceTests;
    void invalidateSyncForTimingEdit();
    struct AutoSyncResult {
        bool success = false;
        bool cancelled = false;
        QString error;
        SyncCandidate candidate;
        qsizetype packetCount = 0;
        qsizetype gpsSampleCount = 0;
        QString gpsStream;
        quint64 generation = 0;
        quint64 syncRevision = 0;
        QString videoPath;
        QString vboPath;
    };

    // KAN-105: a chapter after the first, as asked for and as found.
    struct VideoChapterInput {
        ProjectSourceReference reference;  // empty for a newly chosen file
        QString path;                      // resolved; empty when missing
        double durationSeconds = 0.0;      // saved duration, kept for a gap
    };
    struct VideoChapterState {
        ProjectSourceReference reference;
        QString path;
        double durationSeconds = 0.0;
        bool available = false;
        QString problem;                   // why it is a gap
    };
    struct VideoProbeResult {
        bool success = false;
        bool cancelled = false;
        QString path;
        MediaInfo mediaInfo;
        QVector<VideoChapterState> chapters; // all chapters, the first included; empty for one video
        QString error;
        quint64 generation = 0;
        QJsonObject fingerprint;
        QJsonObject expectedFingerprint;
        bool relink = false;
    };

    struct VboLoadResult {
        bool success = false;
        bool cancelled = false;
        QString path;
        TelemetrySession session;
        TrackGeometry geometry;
        LapSession lapSession;
        QByteArray contentRevision;
        bool contentMismatch = false;
        QString error;
        quint64 generation = 0;
        QJsonObject fingerprint;
        QJsonObject expectedFingerprint;
        bool relink = false;
    };

    [[nodiscard]] QVariant semanticValue(const QString &alias) const;
    void setStatus(QString status);
    struct SourceLoadRequest {
        QString path;
        bool markDocumentDirty = false;
        QJsonObject expectedFingerprint;
        bool relink = false;
        QVector<VideoChapterInput> chapters;
    };
    [[nodiscard]] quint64 beginSourceReplacement(bool replacingVideo);
    [[nodiscard]] quint64 beginSourceGeneration(bool preserveOuting = false) override;
    void cancelSourceJobs();
    void startVideoProbe(const QString &path, quint64 generation, bool markDocumentDirty,
                         QJsonObject expectedFingerprint = {}, bool relink = false,
                         QVector<VideoChapterInput> chapters = {});
    // KAN-105: the video as chapters played as one timeline.
    [[nodiscard]] bool videoChaptered() const { return m_videoTimeline.chapterCount() > 1; }
    [[nodiscard]] QVector<VideoChapterInput> videoChapterInputs() const;
    [[nodiscard]] std::optional<qint64> timelineLastFrame() const;
    void startVboLoad(const QString &path, quint64 generation, bool markDocumentDirty,
                      QJsonObject expectedFingerprint = {}, bool relink = false);
    void commitVideoProbe(const VideoProbeResult &result, bool markDocumentDirty);
    void commitVboLoad(const VboLoadResult &result, bool markDocumentDirty);
    [[nodiscard]] static QString normalizedSourcePath(const QString &path) { return DocumentController::normalizedSourcePath(path); }
    [[nodiscard]] static QVariantList trackPointsFor(const TrackGeometry &geometry);
    void clearActiveTemplate();
    void handleExportOutput();
    void finishExport(int exitCode, QProcess::ExitStatus exitStatus);
    void appendExportDiagnostic(const QString &entry);
    void appendExportLifecycle(const QString &event);
    void finishPersistentExportLog(const QString &result, const QString &error = {});
    [[nodiscard]] static QString syncCandidateLevelName(double confidence);

    QSettings m_settings;
    // DocumentHost: the active run's editor state in the project document.
    [[nodiscard]] bool acceptsEditorProject(const QJsonObject &project, const QString &projectPath) override;
    [[nodiscard]] bool applyEditorScene(const ProjectLoadResult &result, QString *error) override;
    void applyEditorProject(const ProjectLoadResult &result) override;
    void announceEditorProject() override;
    void startEditorSources(const ProjectLoadResult &result) override;
    void clearEditor() override;
    [[nodiscard]] QJsonObject withEditorState(QJsonObject projection, const QString &documentPath,
        const QString &targetPath) const override;
    [[nodiscard]] QByteArray loadedTelemetryRevision() const override;
    [[nodiscard]] QJsonObject withVerifiedAnalysis(const QJsonObject &project) const override;
    void editorProjectSaved(const QJsonObject &project) override;
    // A notice from the scene just applied (KAN-192) rides on the next status.
    void showStatus(const QString &status) override
    {
        setStatus(m_sceneNotice.isEmpty() ? status : status + QLatin1Char(' ') + std::exchange(m_sceneNotice, {}));
    }
    // Overlays has no analysis view since KAN-166; a day import has nothing to reveal.
    void revealAnalysis() override {}
    void initializeDocument();
    // Shorthands for the document the editor state belongs to.
    [[nodiscard]] QJsonObject currentProjectObject() const { return m_document.currentProjectObject(); }
    void markPersistentChange() { m_document.markPersistentChange(); }
    // The editor's own lap navigation follows the saved lap exclusions.
    void applyActiveLapExclusions();
    [[nodiscard]] QJsonObject activeLapBinding() const;
    QByteArray m_loadedSourceRevision;
    QUrl m_videoSource;
    QString m_telemetryPath;
    ProjectSourceReference m_videoReference;
    ProjectSourceReference m_vboReference;
    QString m_statusText = QStringLiteral("Open a video and VBO to begin.");
    QString m_startupNotice;
    QString m_sceneNotice;
    std::unique_ptr<TelemetrySession> m_session;
    LapSession m_lapSession;
    WidgetModel m_widgetModel;
    TrackGeometry m_trackGeometry;
    TelemetryRenderContext m_previewRenderContext;
    QVariantList m_trackPoints;
    double m_playbackTime = 0.0;
    SyncTransform m_sync;
    QFutureWatcher<AutoSyncResult> m_syncWatcher;
    QFutureWatcher<VideoProbeResult> m_videoProbeWatcher;
    VideoChapterReview m_videoChapters;
    QVector<VideoChapterState> m_videoChapterStates; // empty for an ordinary video
    MediaTimeline m_videoTimeline;
    int m_videoChapterIndex = 0;
    QString m_videoChapterNotice;
    QFutureWatcher<VboLoadResult> m_vboLoadWatcher;
    quint64 m_syncRevision = 0;
    std::shared_ptr<std::atomic_bool> m_videoProbeCancellation;
    std::shared_ptr<std::atomic_bool> m_vboLoadCancellation;
    std::shared_ptr<std::atomic_bool> m_syncCancellation;
    bool m_videoLoadMarksDocumentDirty = true;
    bool m_vboLoadMarksDocumentDirty = true;
    QString m_videoLoadState = QStringLiteral("idle");
    QString m_vboLoadState = QStringLiteral("idle");
    SourceLoadRequest m_videoLoadRequest;
    SourceLoadRequest m_vboLoadRequest;
    QString m_pendingVideoPath;
    QString m_pendingVboPath;
    VideoProbeResult m_pendingMismatchVideo;
    VboLoadResult m_pendingMismatchVbo;
    QString m_sourceMismatchType;
    QString m_selectedTemplateId;
    QString m_activeTemplateId;
    std::unique_ptr<QProcess> m_exportProcess;
    std::unique_ptr<ExportProcessSupervisor> m_exportSupervisor;
    std::unique_ptr<QTemporaryFile> m_exportConfig;
    std::unique_ptr<ExportOutputTransaction> m_exportOutputTransaction;
    QByteArray m_exportStdout;
    BoundedProcessOutput m_exportStderr{BoundedProcessOutput::Mode::DiagnosticTail,
                                        ProcessOutputLimits::ffmpegDiagnosticTailBytes};
    QString m_exportCancelPath;
    QString m_exportSupervisionReadyPath;
    QString m_exportManifestPath;
    int m_exportProgress = 0;
    QString m_exportState = QStringLiteral("idle");
    QString m_exportError;
    MediaInfo m_exportSourceInfo;
    QVariantMap m_exportMetrics;
    QVariantMap m_exportProgressInfo;
    BoundedDiagnosticLog m_exportDiagnosticLog{1500};
    // Coalesces exportDiagnosticLogChanged: a busy export logs faster than the
    // Very verbose view should re-lay out its text.
    QTimer m_exportDiagnosticNotifier;
    std::unique_ptr<PersistentExportLog> m_persistentExportLog;
    bool m_exportProgressVisible = false;
    bool m_quitAfterExport = false;
    QVariantMap m_syncCandidate;
    DocumentController m_document;
    BestLapFinder m_bestLapFinder{m_document};
};

} // namespace FlappedEar
