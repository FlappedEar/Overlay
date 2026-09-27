#pragma once

#include "app/VideoLink.h"
#include "app/AnalysisController.h"
#include "app/DocumentController.h"
#include "app/DocumentHost.h"
#include "telemetry/TelemetrySessionCache.h"

#include "telemetry/LapTiming.h"
#include "telemetry/TelemetrySession.h"
#include "telemetry/TelemetryRenderContext.h"
#include "telemetry/TrackGeometry.h"
#include "telemetry/CornerPhases.h"
#include "telemetry/TrackProgress.h"
#include "telemetry/TrackSegmentReview.h"
#include "telemetry/TrackSegmentEditing.h"
#include "telemetry/SectorTiming.h"
#include "telemetry/TheoreticalBest.h"
#include "telemetry/TimeLoss.h"
#include "telemetry/Consistency.h"
#include "telemetry/DrivingVariability.h"
#include "telemetry/CornerSpeeds.h"
#include "telemetry/BrakingMetrics.h"
#include "telemetry/ExitMetrics.h"
#include "telemetry/TelemetryImportPlan.h"
#include "telemetry/OutingLaps.h"
#include "telemetry/OutingLapLoader.h"
#include "telemetry/OutingLapDerivation.h"
#include "telemetry/OutingTheoreticalBest.h"
#include "telemetry/TrackInference.h"
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

class TelemetryTests;

namespace FlappedEar {

class AppController final : public QObject, private VideoLink, private DocumentHost {
    Q_OBJECT
    Q_PROPERTY(QUrl videoSource READ videoSource NOTIFY videoSourceChanged)
    Q_PROPERTY(QString videoName READ videoName NOTIFY videoSourceChanged)
    Q_PROPERTY(QString telemetryName READ telemetryName NOTIFY telemetryChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
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
    Q_PROPERTY(QString exportDiagnosticLog READ exportDiagnosticLog NOTIFY exportChanged)
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
    Q_PROPERTY(QStringList analysisChannels READ analysisChannels WRITE setAnalysisChannels NOTIFY analysisChanged)
    Q_PROPERTY(bool analysisVisible READ analysisVisible WRITE setAnalysisVisible NOTIFY analysisChanged)
    Q_PROPERTY(int analysisWindowX READ analysisWindowX CONSTANT)
    Q_PROPERTY(int analysisWindowY READ analysisWindowY CONSTANT)
    Q_PROPERTY(int analysisWindowWidth READ analysisWindowWidth CONSTANT)
    Q_PROPERTY(int analysisWindowHeight READ analysisWindowHeight CONSTANT)
    Q_PROPERTY(int analysisSidebarWidth READ analysisSidebarWidth CONSTANT)
    Q_PROPERTY(int analysisVideoHeight READ analysisVideoHeight CONSTANT)
    Q_PROPERTY(int windowX READ windowX CONSTANT)
    Q_PROPERTY(int windowY READ windowY CONSTANT)
    Q_PROPERTY(int windowWidth READ windowWidth CONSTANT)
    Q_PROPERTY(int windowHeight READ windowHeight CONSTANT)
    Q_PROPERTY(QUrl projectPath READ projectPath NOTIFY documentStateChanged)
    Q_PROPERTY(QString eventName READ eventName NOTIFY documentStateChanged)
    Q_PROPERTY(QVariantList eventRuns READ eventRuns NOTIFY documentStateChanged)
    Q_PROPERTY(QString activeRunId READ activeRunId NOTIFY documentStateChanged)
    Q_PROPERTY(QString batchImportState READ batchImportState NOTIFY batchImportChanged)
    Q_PROPERTY(QString batchImportError READ batchImportError NOTIFY batchImportChanged)
    Q_PROPERTY(QStringList analysisImportMessages READ analysisImportMessages NOTIFY batchImportChanged)
    Q_PROPERTY(QVariantList comparisonSlots READ comparisonSlots NOTIFY comparisonSlotsChanged)
    Q_PROPERTY(QVariantList comparisonLaps READ comparisonLaps NOTIFY comparisonSlotsChanged)
    Q_PROPERTY(bool comparisonPairReady READ comparisonPairReady NOTIFY comparisonSlotsChanged)
    Q_PROPERTY(QStringList comparisonAvailableChannels READ comparisonAvailableChannels NOTIFY comparisonSlotsChanged)
    // A genuine property (not a Q_INVOKABLE read via a comma-operator forced
    // dependency): that hack is a known-fragile QML pattern that a layout
    // change elsewhere in ComparisonDetailPanel.qml tripped into a spurious
    // "Binding loop detected" warning (KAN-40 investigation). A real NOTIFY
    // gives QML's normal dependency tracking something to attach to.
    Q_PROPERTY(double comparisonProgressAxisLength READ comparisonProgressAxisLength NOTIFY comparisonSlotsChanged)
    Q_PROPERTY(bool comparisonViewOpen READ comparisonViewOpen WRITE setComparisonViewOpen NOTIFY comparisonViewOpenChanged)
    Q_PROPERTY(QVariantMap selectedOutingLap READ selectedOutingLap NOTIFY outingLapDetailChanged)
    Q_PROPERTY(QString outingLapDetailState READ outingLapDetailState NOTIFY outingLapDetailChanged)
    Q_PROPERTY(QString outingLapDetailError READ outingLapDetailError NOTIFY outingLapDetailChanged)
    Q_PROPERTY(QStringList outingLapChannels READ outingLapChannels WRITE setOutingLapChannels NOTIFY outingLapDetailChanged)
    Q_PROPERTY(QStringList outingLapAvailableChannels READ outingLapAvailableChannels NOTIFY outingLapDetailChanged)
    Q_PROPERTY(QVariantList outingLapTrack READ outingLapTrack NOTIFY outingLapDetailChanged)
    // KAN-48: automatic segment proposals for the open lap and their review.
    Q_PROPERTY(QString segmentReviewState READ segmentReviewState NOTIFY segmentReviewChanged)
    Q_PROPERTY(QString segmentReviewMessage READ segmentReviewMessage NOTIFY segmentReviewChanged)
    Q_PROPERTY(double segmentReviewAxisLength READ segmentReviewAxisLength NOTIFY segmentReviewChanged)
    Q_PROPERTY(QVariantList segmentReviewItems READ segmentReviewItems NOTIFY segmentReviewChanged)
    Q_PROPERTY(QVariantMap segmentReviewApproved READ segmentReviewApproved NOTIFY segmentReviewChanged)
    Q_PROPERTY(QVariantList segmentReviewMapLayers READ segmentReviewMapLayers NOTIFY segmentReviewChanged)
    Q_PROPERTY(QVariantMap outingLapTrackPoint READ outingLapTrackPoint NOTIFY outingLapCursorChanged)
    Q_PROPERTY(double outingLapCursor READ outingLapCursor WRITE setOutingLapCursor NOTIFY outingLapCursorChanged)
    // KAN-39: video linkage for the open lap, gated to the lap's own run
    // being the currently active/loaded one -- a lap from a different run
    // is treated as having no video for this increment (disclosed gap),
    // rather than silently switching the active run and its loaded sources.
    Q_PROPERTY(bool outingLapVideoAvailable READ outingLapVideoAvailable NOTIFY outingLapVideoChanged)
    Q_PROPERTY(qint64 outingLapVideoPositionMilliseconds READ outingLapVideoPositionMilliseconds NOTIFY outingLapVideoChanged)
    Q_PROPERTY(QVariantMap outingRanking READ outingRanking NOTIFY outingLapsChanged)
    Q_PROPERTY(QVariantMap outingProgression READ outingProgression NOTIFY outingLapsChanged)
    // KAN-56: the fastest valid time per approved sector across the current
    // comparison group's whole compatible population, not just one lap.
    // Loading state is separate from outingRanking/outingProgression's
    // (those are cheap JSON aggregation; this decodes every eligible lap's
    // recording) and must be explicitly requested.
    Q_PROPERTY(QVariantMap outingTheoreticalBest READ outingTheoreticalBest NOTIFY outingTheoreticalBestChanged)
    // KAN-60: the day's largest observed losses of each eligible lap against
    // the group's actual best, from the same calculation.
    Q_PROPERTY(QVariantMap outingTimeLossRanking READ outingTimeLossRanking NOTIFY outingTheoreticalBestChanged)
    // KAN-62: lap-time consistency of the comparison group's eligible laps
    // (median and interquartile range), for the day and for each run.
    Q_PROPERTY(QVariantMap outingLapConsistency READ outingLapConsistency NOTIFY outingLapsChanged)
    // KAN-67: recorded temperature channels summarized per run and per
    // recorded section (mean, extrema, coverage); requested explicitly.
    Q_PROPERTY(QVariantMap outingChannelSummaries READ outingChannelSummaries NOTIFY outingChannelSummariesChanged)
    // KAN-71: the computed day report (see telemetry/DayReport.h): every
    // result with algorithm, range, data status and evidence references.
    Q_PROPERTY(QVariantMap outingDayReport READ outingDayReport NOTIFY outingDayReportChanged)
    // KAN-64: each approved segment's typical time and spread per session, in
    // chronological order, with the laps behind every figure.
    Q_PROPERTY(QVariantMap outingSectorProgression READ outingSectorProgression NOTIFY outingTheoreticalBestChanged)
    // KAN-117: by default only each run's best lap is ranked; warm-up and
    // traffic laps otherwise dominate the list.
    Q_PROPERTY(bool outingTimeLossAllLaps READ outingTimeLossAllLaps WRITE setOutingTimeLossAllLaps NOTIFY outingTheoreticalBestChanged)
    // KAN-57: a segment the comparison view should show in the Corner
    // Analyzer once the requested pair is loaded; cleared when shown or when
    // the comparison view closes.
    Q_PROPERTY(QString comparisonFocusSegmentId READ comparisonFocusSegmentId NOTIFY comparisonFocusSegmentIdChanged)
    Q_PROPERTY(QVariantList outingCompatibilityGroups READ outingCompatibilityGroups NOTIFY outingLapsChanged)
    Q_PROPERTY(QString outingComparisonGroupId READ outingComparisonGroupId NOTIFY outingLapsChanged)
    Q_PROPERTY(QString outingComparisonSelectionState READ outingComparisonSelectionState NOTIFY outingLapsChanged)
    Q_PROPERTY(QVariantList outingLaps READ outingLaps NOTIFY outingLapsChanged)
    Q_PROPERTY(QVariantMap outingAnalysisStatus READ outingAnalysisStatus NOTIFY outingLapsChanged)
    Q_PROPERTY(QStringList outingLapMessages READ outingLapMessages NOTIFY outingLapsChanged)
    Q_PROPERTY(bool outingLapsLoading READ outingLapsLoading NOTIFY outingLapsChanged)
    Q_PROPERTY(QVariantList batchImportRows READ batchImportRows NOTIFY batchImportChanged)
    Q_PROPERTY(int batchImportProcessed READ batchImportProcessed NOTIFY batchImportChanged)
    Q_PROPERTY(int batchImportTotal READ batchImportTotal NOTIFY batchImportChanged)
    Q_PROPERTY(bool dirty READ dirty NOTIFY documentStateChanged)
    Q_PROPERTY(quint64 lastSavedRevision READ lastSavedRevision NOTIFY documentStateChanged)
    Q_PROPERTY(QString pendingDestructiveAction READ pendingDestructiveAction NOTIFY destructiveActionChanged)
    Q_PROPERTY(QString videoLoadState READ videoLoadState NOTIFY sourceLoadStateChanged)
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
    Q_PROPERTY(QVariantMap runRecordingReview READ runRecordingReview NOTIFY runRecordingsChanged)

public:
    explicit AppController(QObject *parent = nullptr, QString recoveryPath = {},
                           ProjectRecoveryStore::Operations recoveryOperations = {});
    ~AppController() override;

    [[nodiscard]] QUrl videoSource() const;
    [[nodiscard]] QString videoName() const;
    [[nodiscard]] QString telemetryName() const;
    [[nodiscard]] QString statusText() const;
    [[nodiscard]] QStringList channelNames() const;
    [[nodiscard]] qsizetype sampleCount() const;
    [[nodiscard]] double telemetryDuration() const;
    [[nodiscard]] double playbackTime() const;
    [[nodiscard]] double syncOffset() const;
    [[nodiscard]] double timeScale() const;
    [[nodiscard]] bool syncing() const;
    [[nodiscard]] bool exporting() const;
    // KAN-124: the document is busy with an operation that must not be
    // interleaved with analysis edits (today: an export). Analysis and import
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
    [[nodiscard]] QStringList analysisChannels() const;
    [[nodiscard]] bool analysisVisible() const;
    [[nodiscard]] int analysisWindowX() const;
    [[nodiscard]] int analysisWindowY() const;
    [[nodiscard]] int analysisWindowWidth() const;
    [[nodiscard]] int analysisWindowHeight() const;
    [[nodiscard]] int analysisSidebarWidth() const;
    [[nodiscard]] int analysisVideoHeight() const;
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
    Q_INVOKABLE void loadVbo(const QUrl &url);
    Q_INVOKABLE bool selectEventRun(const QString &runId) { return m_document.selectEventRun(runId); }
    Q_INVOKABLE QVariantMap runMetadata(const QString &runId) const;
    Q_INVOKABLE bool updateRunMetadata(const QString &runId, const QString &expectedToken,
        const QString &name, const QString &notes, const QString &conditions, const QString &setupChanges);
    Q_INVOKABLE QVariantMap runTrackConfiguration(const QString &runId) const;
    Q_INVOKABLE bool confirmRunTrackConfiguration(const QString &runId, const QString &expectedDerivationKey,
        const QString &layoutId, const QString &direction, bool applyToMatching = false);
    Q_INVOKABLE bool selectOutingComparisonGroup(const QString &groupId);
    [[nodiscard]] QVariantMap outingRanking() const;
    [[nodiscard]] QVariantMap outingProgression() const;
    [[nodiscard]] QVariantMap outingTheoreticalBest() const;
    [[nodiscard]] QVariantMap outingTimeLossRanking() const;
    [[nodiscard]] QVariantMap outingLapConsistency() const;
    [[nodiscard]] QVariantMap outingChannelSummaries() const;
    Q_INVOKABLE void requestOutingChannelSummaries();
    Q_INVOKABLE QVariantMap outingTemperatureAssociations() const;
    [[nodiscard]] QVariantMap outingDayReport() const;
    // Starts the background results the report needs (theoretical best with
    // losses and section progression, channel summaries).
    Q_INVOKABLE void requestOutingDayReport();
    // KAN-73: opens a focus area's evidence pair in the comparison, at its segment.
    Q_INVOKABLE bool openFocusArea(const QVariantMap &evidence);
    [[nodiscard]] QVariantMap outingSectorProgression() const;
    [[nodiscard]] bool outingTimeLossAllLaps() const { return m_analysis.outingTimeLossAllLaps(); }
    void setOutingTimeLossAllLaps(bool allLaps);
    Q_INVOKABLE void requestOutingTheoreticalBest();
    Q_INVOKABLE bool openTheoreticalBestSector(const QString &segmentId);
    // KAN-61: loss evidence. Opens the ranked loss's lap (A) against the
    // ranking's reference (B), focused on the loss window.
    Q_INVOKABLE bool openTimeLoss(const QVariantMap &loss);
    // Opens one comparison lap in the lap view with its cursor where it
    // reaches `progressMeters`, so the run's video (if any) follows.
    Q_INVOKABLE bool openComparisonLapAtProgress(int slot, double progressMeters);
    [[nodiscard]] QString comparisonFocusSegmentId() const { return m_analysis.comparisonFocusSegmentId(); }
    Q_INVOKABLE void clearComparisonFocusSegment();
    [[nodiscard]] QVariantList outingCompatibilityGroups() const;
    [[nodiscard]] QString outingComparisonGroupId() const;
    [[nodiscard]] QString outingComparisonSelectionState() const;
    Q_INVOKABLE bool setRunTrackConfiguration(
        const QString &runId, const QString &layoutId, const QString &direction);
    [[nodiscard]] QVariantList comparisonSlots() const;
    [[nodiscard]] QVariantList comparisonLaps() const;
    [[nodiscard]] bool comparisonPairReady() const;
    Q_INVOKABLE bool selectComparisonLap(int slot, const QVariantMap &reference);
    Q_INVOKABLE void clearComparisonLap(int slot);
    Q_INVOKABLE bool swapComparisonLaps();
    Q_INVOKABLE bool useBestComparisonLap(bool wholeDay);
    Q_INVOKABLE bool inspectComparisonLap(int slot);
    [[nodiscard]] bool comparisonViewOpen() const { return m_analysis.comparisonViewOpen(); }
    void setComparisonViewOpen(bool open);
    Q_INVOKABLE QVariantMap comparisonLapSeries(
        int slot, const QString &channel, double startTime, double endTime, int maximumPoints) const;
    Q_INVOKABLE QVariantList comparisonLapTrack(int slot) const;
    // Overlay comparison: both slots' GPS traces sharing one normalization
    // (so they draw to scale on one map), and channel/delta series
    // parameterized by the shared cross-lap track-progress axis (KAN-31/32/33)
    // so a corner lines up at the same position for both laps even when they
    // take different racing lines -- not just "meters since each lap's own
    // start" (see the now-removed LapDistance-based methods this replaced).
    Q_INVOKABLE QVariantList comparisonOverlayTrack(int slot) const;
    Q_INVOKABLE QVariantMap comparisonPositionAtProgress(int slot, double progressMeters) const;
    Q_INVOKABLE QVariantMap comparisonChannelSeriesByProgress(
        int slot, const QString &channel, double startProgress, double endProgress, int maximumPoints) const;
    [[nodiscard]] double comparisonProgressAxisLength() const;
    // Cumulative time gap between the two laps at the same shared progress
    // (A minus B; positive means A took longer to reach that point, i.e. A is
    // behind there) -- the classic lap-delta trace, not a per-sample
    // channel-value difference.
    Q_INVOKABLE QVariantMap comparisonDeltaSeriesByProgress(double startProgress, double endProgress, int maximumPoints) const;
    [[nodiscard]] QStringList comparisonAvailableChannels() const;
    // Persisted comparison-view range (shared-progress meters) and visible
    // channel selection (KAN-41). Read once by QML when a pair's axis/channels
    // first become valid after a document (re)opens; written on every change.
    // Not part of ComparisonSlot: this is pair-level view state, not per-slot
    // load state, and survives independently of which laps are selected.
    Q_INVOKABLE QVariantMap comparisonPersistedRangeMeters() const;
    Q_INVOKABLE QStringList comparisonPersistedChannels() const;
    Q_INVOKABLE void persistComparisonRange(double startMeters, double endMeters);
    Q_INVOKABLE void persistComparisonChannels(const QStringList &channels);
    // KAN-55 (Corner Analyzer): approved segments common to both compared
    // laps (same id, same approved revision -- never a guessed correspondence
    // between two independently-approved sets), and one segment's combined
    // A/B/delta metrics (sector time; entry/apex/minimum/exit speeds, braking
    // and exit effects when the segment is a corner). Reuses the shared
    // comparison progress axis (ensureComparisonProgressAxis), never a second
    // alignment.
    Q_INVOKABLE QVariantList comparisonApprovedSegments() const;
    Q_INVOKABLE QVariantMap comparisonSegmentMetrics(const QString &segmentId) const;
    Q_INVOKABLE QString comparisonSegmentationNote() const;
    Q_INVOKABLE QStringList comparisonPreferredChannels() const;
    // "1:49.898" for anything a minute or longer, "28.662 s" below that; "—"
    // when not finite. One formatter for every lap and segment time.
    Q_INVOKABLE static QString formatElapsedTime(double seconds);
    // KAN-59: one loss window per approved segment for the current pair.
    Q_INVOKABLE QVariantMap comparisonTimeLossObservations() const;
    // KAN-66: both laps' G-G samples over [start, end] metres of the shared
    // axis: at most `maximumPoints` drawn per lap, peaks and counts from all.
    Q_INVOKABLE QVariantMap comparisonGgScatter(double startMeters, double endMeters, int maximumPoints) const;
    // KAN-69: both laps' heart rate over [start, end] metres of the shared axis;
    // start after end crosses start/finish (KAN-70).
    Q_INVOKABLE QVariantMap comparisonHeartRate(double startMeters, double endMeters) const;
    Q_INVOKABLE QVariantMap comparisonTrailBraking(double startMeters, double endMeters) const;
    Q_INVOKABLE QVariantList comparisonMapLayerOptions() const;
    Q_INVOKABLE QVariantMap comparisonMapLayer(const QString &layerId, int slot) const;
    Q_INVOKABLE bool selectOutingLap(int index);
    // Snapshot resolution: opening detail revalidates source content off-thread.
    Q_INVOKABLE QVariantMap resolveOutingLapReference(const QVariantMap &reference) const;
    Q_INVOKABLE bool selectOutingLapReference(const QVariantMap &reference);
    // KAN-70: opens a lap with one of its recorded channels shown first (for
    // example heart rate from a summary). The saved chart preference is kept.
    Q_INVOKABLE bool openOutingLapChannel(const QVariantMap &reference, const QString &channel);
    Q_INVOKABLE bool setOutingLapExcluded(const QVariantMap &reference, bool excluded, const QString &reason = {});
    Q_INVOKABLE void closeOutingLap();
    Q_INVOKABLE void requestSegmentReview();
    Q_INVOKABLE QString approveSegmentProposal(int index);
    Q_INVOKABLE int approveCertainSegmentProposals();
    Q_INVOKABLE bool setSegmentProposalRejected(int index, bool rejected);
    Q_INVOKABLE QString editSegmentProposal(int index, const QString &name, const QString &type,
        double startMeters, double endMeters);
    Q_INVOKABLE bool revokeApprovedSegment(const QString &id);
    Q_INVOKABLE bool discardOtherConfigurationSegments();
    // KAN-49: editing approved segments. Each returns an empty string on
    // success, otherwise the reason the edit was refused.
    Q_INVOKABLE QString editApprovedSegment(const QString &id, const QString &name, const QString &type,
        double startMeters, double endMeters, bool keepAdjacentJoined);
    Q_INVOKABLE QString splitApprovedSegment(const QString &id, double atMeters);
    Q_INVOKABLE QString mergeApprovedSegments(const QString &firstId, const QString &secondId);
    Q_INVOKABLE QString undoSegmentEdit();
    Q_INVOKABLE QString redoSegmentEdit();
    // Track progress at a normalized point of the lap map, or {"error": reason}.
    Q_INVOKABLE QVariantMap segmentReviewProgressAt(double x, double y) const;
    // KAN-51: sector times of the reviewed lap from the approved segmentation.
    Q_INVOKABLE QVariantMap outingLapSectorTimes() const;
    // KAN-52: entry/apex/minimum/exit speeds of the reviewed lap per approved corner.
    Q_INVOKABLE QVariantList outingLapCornerSpeeds() const;
    // KAN-53: braking point, time, distance and deceleration per approved corner.
    Q_INVOKABLE QVariantList outingLapBrakingMetrics() const;
    // KAN-54: throttle pickup and the following-straight interval per approved corner.
    Q_INVOKABLE QVariantList outingLapExitMetrics() const;
    Q_INVOKABLE QVariantMap outingLapCoasting() const;
    Q_INVOKABLE QVariantMap outingLapSeries(const QString &channel, int maximumPoints) const;
    Q_INVOKABLE QVariantMap outingLapSeries(
        const QString &channel, double startTime, double endTime, int maximumPoints) const;
    Q_INVOKABLE QString outingLapValueText(const QString &channel) const;
    [[nodiscard]] QVariantMap selectedOutingLap() const { return m_analysis.selectedOutingLap(); }
    [[nodiscard]] QString outingLapDetailState() const { return m_analysis.outingLapDetailState(); }
    [[nodiscard]] QString outingLapDetailError() const { return m_analysis.outingLapDetailError(); }
    [[nodiscard]] QStringList outingLapAvailableChannels() const;
    void setOutingLapChannels(const QStringList &channels);
    [[nodiscard]] QStringList outingLapChannels() const { return m_analysis.outingLapChannels(); }
    [[nodiscard]] QVariantList outingLapTrack() const { return m_analysis.outingLapTrack(); }
    [[nodiscard]] QVariantMap outingLapTrackPoint() const;
    [[nodiscard]] double outingLapCursor() const { return m_analysis.outingLapCursor(); }
    [[nodiscard]] QString segmentReviewState() const { return m_analysis.segmentReviewState(); }
    [[nodiscard]] QString segmentReviewMessage() const { return m_analysis.segmentReviewMessage(); }
    [[nodiscard]] double segmentReviewAxisLength() const;
    [[nodiscard]] QVariantList segmentReviewItems() const;
    [[nodiscard]] QVariantMap segmentReviewApproved() const;
    [[nodiscard]] QVariantList segmentReviewMapLayers() const;
    void setOutingLapCursor(double seconds);
    // Reuses the central SyncTransform (videoToTelemetryTime/telemetryToVideoTime,
    // TelemetrySession.h) already relied on for the main preview's playback<->
    // telemetry mapping -- never a second, ad hoc conversion.
    [[nodiscard]] bool outingLapVideoAvailable() const;
    [[nodiscard]] qint64 outingLapVideoPositionMilliseconds() const;
    Q_INVOKABLE bool followOutingLapVideoPosition(qint64 videoPositionMilliseconds);
    [[nodiscard]] QVariantList outingLaps() const;
    [[nodiscard]] QVariantMap outingAnalysisStatus() const;
    Q_INVOKABLE bool retryOutingAnalysis();
    [[nodiscard]] QStringList outingLapMessages() const { return m_analysis.outingLapMessages(); }
    [[nodiscard]] bool outingLapsLoading() const { return m_analysis.outingLapsLoading(); }
    [[nodiscard]] QString batchImportState() const { return m_document.batchImportState(); }
    [[nodiscard]] QString batchImportError() const { return m_document.batchImportError(); }
    [[nodiscard]] QStringList analysisImportMessages() const { return m_document.analysisImportMessages(); }
    [[nodiscard]] QVariantList batchImportRows() const { return m_document.batchImportRows(); }
    [[nodiscard]] int batchImportProcessed() const { return m_document.batchImportProcessed(); }
    [[nodiscard]] int batchImportTotal() const { return m_document.batchImportTotal(); }
    Q_INVOKABLE bool beginBatchImport(const QList<QUrl> &urls) { return m_document.beginBatchImport(urls); }
    Q_INVOKABLE bool importAnalysisRuns(const QString &name, const QList<QUrl> &urls) { return m_document.importAnalysisRuns(name, urls); }
    Q_INVOKABLE bool importAnalysisFolder(const QString &name, const QUrl &folder, bool includeSubfolders) { return m_document.importAnalysisFolder(name, folder, includeSubfolders); }
    Q_INVOKABLE bool importAnalysisSources(const QString &name, const QList<QUrl> &urls, bool includeSubfolders) { return m_document.importAnalysisSources(name, urls, includeSubfolders); }
    Q_INVOKABLE QVariantList runRecordings(const QString &runId) const { return m_document.runRecordings(runId); }
    Q_INVOKABLE bool attachRunRecording(const QString &runId, const QUrl &url) { return m_document.attachRunRecording(runId, url); }
    Q_INVOKABLE bool confirmRunRecording() { return m_document.confirmRunRecording(); }
    Q_INVOKABLE void cancelRunRecording() { m_document.cancelRunRecording(); }
    Q_INVOKABLE bool setRunPrimarySource(const QString &runId, const QString &sourceId) { return m_document.setRunPrimarySource(runId, sourceId); }
    Q_INVOKABLE bool checkRunRecordingAlignment(const QString &runId, const QString &sourceId) { return m_document.checkRunRecordingAlignment(runId, sourceId); }
    [[nodiscard]] QVariantMap runRecordingReview() const { return m_document.runRecordingReview(); }
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
    Q_INVOKABLE void toggleAnalysisChannel(const QString &channelName);
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
    Q_INVOKABLE void saveAnalysisWindowState(
        int x, int y, int width, int height, int sidebarWidth, int videoHeight);
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
    void setAnalysisChannels(const QStringList &channels);
    void setAnalysisVisible(bool visible);

signals:
    void batchImportChanged();
    void batchImportCommitted();
    void outingLapsChanged();
    void outingLapDetailChanged();
    void outingLapVideoChanged();
    void outingTheoreticalBestChanged();
    void outingChannelSummariesChanged();
    void outingDayReportChanged();
    void comparisonFocusSegmentIdChanged();
    void comparisonSlotsChanged();
    void comparisonViewOpenChanged();
    void outingLapCursorChanged();
    void segmentReviewChanged();
    void videoSourceChanged();
    void telemetryChanged();
    void lapNavigationChanged();
    void statusTextChanged();
    void playbackTimeChanged();
    void syncChanged();
    void syncingChanged();
    void exportChanged();
    void syncCandidateChanged();
    void liveValuesChanged();
    void analysisChanged();
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
    void runRecordingsChanged();

private:
    friend class ::TelemetryTests; // Controlled asynchronous completion in regression tests.
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

    struct VideoProbeResult {
        bool success = false;
        bool cancelled = false;
        QString path;
        MediaInfo mediaInfo;
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
    };
    [[nodiscard]] quint64 beginSourceReplacement(bool replacingVideo);
    [[nodiscard]] quint64 beginSourceGeneration(bool preserveOuting = false) override;
    void cancelSourceJobs(bool cancelOutingDetail = true);
    void startVideoProbe(const QString &path, quint64 generation, bool markDocumentDirty,
                         QJsonObject expectedFingerprint = {}, bool relink = false);
    void startVboLoad(const QString &path, quint64 generation, bool markDocumentDirty,
                      QJsonObject expectedFingerprint = {}, bool relink = false);
    void commitVideoProbe(const VideoProbeResult &result, bool markDocumentDirty);
    void commitVboLoad(const VboLoadResult &result, bool markDocumentDirty);
    [[nodiscard]] static QString normalizedSourcePath(const QString &path) { return DocumentController::normalizedSourcePath(path); }
    [[nodiscard]] static QVariantList trackPointsFor(const TrackGeometry &geometry);
    void reconcileAnalysisChannels();
    void clearActiveTemplate();
    void handleExportOutput();
    void finishExport(int exitCode, QProcess::ExitStatus exitStatus);
    void appendExportDiagnostic(const QString &entry);
    void appendExportLifecycle(const QString &event);
    void finishPersistentExportLog(const QString &result, const QString &error = {});
    [[nodiscard]] static QString syncCandidateLevelName(double confidence);

    QSettings m_settings;
    // KAN-124: the day's analysis; reads and edits this document through
    // AnalysisDocument and reaches the loaded video through VideoLink.
    [[nodiscard]] std::optional<qint64> videoPositionForTelemetry(const QString &runId, double telemetrySeconds) const override;
    [[nodiscard]] std::optional<double> telemetryForVideoPosition(const QString &runId, qint64 videoMilliseconds) const override;
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
    void showStatus(const QString &status) override { setStatus(status); }
    void revealAnalysis() override { setAnalysisVisible(true); }
    void initializeDocument();
    void initializeAnalysis();
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
    std::unique_ptr<PersistentExportLog> m_persistentExportLog;
    bool m_exportProgressVisible = false;
    bool m_quitAfterExport = false;
    QVariantMap m_syncCandidate;
    QStringList m_analysisChannels;
    bool m_analysisVisible = false;
    // Declared last, analysis after the document it reads: destroyed first.
    DocumentController m_document;
    AnalysisController m_analysis{m_document};
};

} // namespace FlappedEar
