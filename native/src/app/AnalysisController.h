#pragma once

#include "app/AnalysisDocument.h"
#include "app/VideoLink.h"
#include "telemetry/TelemetrySessionCache.h"

#include "telemetry/LapTiming.h"
#include "telemetry/TelemetrySession.h"
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
#include "telemetry/OutingLaps.h"
#include "telemetry/OutingLapLoader.h"
#include "telemetry/OutingLapDerivation.h"
#include "telemetry/OutingTheoreticalBest.h"
#include "telemetry/TrackInference.h"

#include <QFutureWatcher>
#include <QJsonObject>
#include <QObject>
#include <QSettings>
#include <QSet>
#include <QTimer>
#include <QVariant>
#include <array>
#include <atomic>
#include <functional>
#include <memory>
#include <optional>

class TelemetryTests;

namespace FlappedEar {

// KAN-124: the day's analysis -- outing laps, lap detail, segment review,
// comparison, theoretical best, time losses, channel summaries and the day
// report. It reads and edits the project only through AnalysisDocument and
// reaches video only through an optional VideoLink, so Flapped Ear Telemetry
// can run it without the overlay editor. AppController forwards its QML API
// here unchanged.
class AnalysisController final : public QObject {
    Q_OBJECT
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

public:
    explicit AnalysisController(AnalysisDocument &document, QObject *parent = nullptr);
    ~AnalysisController() override;

    // Null when there is no video support (Flapped Ear Telemetry).
    void setVideoLink(const VideoLink *link) { m_videoLink = link; }

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
    [[nodiscard]] QVariantMap outingDayReport() const;
    // Starts the background results the report needs (theoretical best with
    // losses and section progression, channel summaries).
    Q_INVOKABLE void requestOutingDayReport();
    // KAN-73: opens a focus area's evidence pair in the comparison, at its segment.
    Q_INVOKABLE bool openFocusArea(const QVariantMap &evidence);
    [[nodiscard]] QVariantMap outingSectorProgression() const;
    [[nodiscard]] bool outingTimeLossAllLaps() const { return m_timeLossAllLaps; }
    void setOutingTimeLossAllLaps(bool allLaps);
    Q_INVOKABLE void requestOutingTheoreticalBest();
    Q_INVOKABLE bool openTheoreticalBestSector(const QString &segmentId);
    // KAN-61: loss evidence. Opens the ranked loss's lap (A) against the
    // ranking's reference (B), focused on the loss window.
    Q_INVOKABLE bool openTimeLoss(const QVariantMap &loss);
    // Opens one comparison lap in the lap view with its cursor where it
    // reaches `progressMeters`, so the run's video (if any) follows.
    Q_INVOKABLE bool openComparisonLapAtProgress(int slot, double progressMeters);
    [[nodiscard]] QString comparisonFocusSegmentId() const { return m_comparisonFocusSegmentId; }
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
    [[nodiscard]] bool comparisonViewOpen() const { return m_comparisonViewOpen; }
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
    // KAN-93: braking while cornering for both laps over [start, end] metres of
    // the shared axis: overlap time and distance, the states' provenance and
    // strips (fractions of the range) for braking, cornering and the overlap.
    Q_INVOKABLE QVariantMap comparisonTrailBraking(double startMeters, double endMeters) const;
    // KAN-97: analytical map layers for the A/B pair. The options list every
    // layer (speed, delta, G, pedals, recorded temperatures) with whether
    // either lap has it; a layer colours one lap's line on the shared map.
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
    // KAN-92: coasting of the open lap (see telemetry/CoastingAnalysis):
    // totals, episodes and, once the lap's progress axis is ready, segment
    // rows and map layers.
    Q_INVOKABLE QVariantMap outingLapCoasting() const;
    Q_INVOKABLE QVariantMap outingLapSeries(const QString &channel, int maximumPoints) const;
    Q_INVOKABLE QVariantMap outingLapSeries(
        const QString &channel, double startTime, double endTime, int maximumPoints) const;
    Q_INVOKABLE QString outingLapValueText(const QString &channel) const;
    [[nodiscard]] QVariantMap selectedOutingLap() const { return m_selectedOutingLap; }
    [[nodiscard]] QString outingLapDetailState() const { return m_outingLapDetailState; }
    [[nodiscard]] QString outingLapDetailError() const { return m_outingLapDetailError; }
    [[nodiscard]] QStringList outingLapAvailableChannels() const;
    void setOutingLapChannels(const QStringList &channels);
    [[nodiscard]] QStringList outingLapChannels() const { return m_outingLapChannels; }
    [[nodiscard]] QVariantList outingLapTrack() const { return m_outingLapTrack; }
    [[nodiscard]] QVariantMap outingLapTrackPoint() const;
    [[nodiscard]] double outingLapCursor() const { return m_outingLapCursor; }
    [[nodiscard]] QString segmentReviewState() const { return m_segmentReviewState; }
    [[nodiscard]] QString segmentReviewMessage() const { return m_segmentReviewMessage; }
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
    [[nodiscard]] QStringList outingLapMessages() const { return m_outingLapMessages; }
    [[nodiscard]] bool outingLapsLoading() const {
        return m_document.projectLoading() || m_outingLapsLoading || m_outingLapRequestedKey != outingLapKey()
            || m_outingLapGeneration != sourceGeneration();
    }

    // A channel over [start, end] telemetry seconds as chart segments (x
    // normalised to the range); gaps stay separate segments.
    static QVariantMap sessionSeries(const TelemetrySession &session, const QString &channel,
        double start, double end, int maximumPoints);

    // Document-facing lifecycle, called by the owner of the document.
    // A new source set for the whole document: forget cached derivations and
    // close the lap, comparison and view that belonged to the old sources.
    void resetForNewSources();
    // The active run's recording is being replaced.
    void activeRunSourceReplaced(const QString &runId);
    // Cooperative cancellation of in-flight work (lap detail work optional).
    void cancelWork(bool cancelDetail);
    [[nodiscard]] bool workRunning() const;
    // Each run's primary telemetry source as analysis keys it.
    [[nodiscard]] QJsonArray outingLapSources() const;
    // The project with the track inference the lap derivation verified.
    [[nodiscard]] QJsonObject projectWithOutingInference(QJsonObject project) const;
    // Re-applies lap exclusions to the day's rows (after a document change).
    void refreshLapExclusionPolicy();

signals:
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
    // Lap exclusions are being re-applied; the document owner applies them
    // first to anything else that shows laps (the editor's lap navigation).
    void lapExclusionsRefreshing();
    // Inputs, forwarded from the document owner's signals of the same name.
    void documentStateChanged();
    void sourceLoadStateChanged();

private:
    friend class ::TelemetryTests; // Controlled asynchronous completion in regression tests.
    AnalysisDocument &m_document;
    QSettings m_settings;
    [[nodiscard]] bool documentEditable() const;
    [[nodiscard]] quint64 sourceGeneration() const { return m_document.sourceGeneration(); }
    using OutingLapDetailResult = FlappedEar::OutingLapDetail;
    struct ComparisonSlot {
        QVariantMap row;
        QJsonObject source;
        QByteArray key;
        quint64 request = 0;
        QString state = QStringLiteral("empty");
        QString error;
        std::shared_ptr<const TelemetrySession> session;
        TrackGeometry geometry;
        QVariantList track;
        FlappedEar::LapTrace referenceTrace;
        FlappedEar::TimingGate referenceGate;
        bool hasReferenceGate = false;
    };
    void initializeComparisonLaps();
    void loadComparisonLap();
    void invalidateComparisonLaps();
    void failComparisonLap(int slot, const QString &reason);
    void resetComparisonSlot(int slot);
    void persistComparisonSlot(int slot, const QJsonValue &reference);
    void restorePersistedComparisonSlots();
    // Lazily rebuilt only when either slot's request id changes; both slots'
    // overlay tracks are recomputed together since they share one normalization.
    void ensureComparisonSharedGeometry() const;
    mutable TrackGeometry m_comparisonSharedGeometry;
    mutable quint64 m_comparisonSharedGeometryRequestA = 0;
    mutable quint64 m_comparisonSharedGeometryRequestB = 0;
    mutable std::array<QVariantList, 2> m_comparisonOverlayTrackCache;
    // Same lazy-rebuild pattern: the shared progress axis is built once from
    // slot 0's reference trace/gate (both slots are already verified
    // compatible, i.e. the same physical gate), and both slots' telemetry are
    // projected onto it. Building the axis and projecting one lap's telemetry
    // are both cheap (resampling + a bounded per-lap scan); only the earlier,
    // whole-file gate/lap derivation that produced referenceTrace/referenceGate
    // was expensive enough to need the background worker.
    void ensureComparisonProgressAxis() const;
    mutable FlappedEar::ProgressAxis m_comparisonProgressAxis;
    mutable quint64 m_comparisonProgressAxisRequestA = 0;
    mutable quint64 m_comparisonProgressAxisRequestB = 0;
    mutable std::array<QVector<FlappedEar::ProgressSegment>, 2> m_comparisonProgressTraceCache;
    // KAN-55: the approved segments for one comparison slot's own run, same
    // lookup as AppController::currentApprovedSegmentation() but parameterized
    // by slot instead of the single open outing lap.
    FlappedEar::ApprovedSegmentation comparisonApprovedSegmentation(int slot) const;
    std::shared_ptr<TelemetrySessionCache> m_analysisSourceCache = std::make_shared<TelemetrySessionCache>();
    std::array<ComparisonSlot, 2> m_comparisonSlots;
    QFutureWatcher<OutingLapDetailResult> m_comparisonWatcher;
    QTimer m_comparisonTimer;
    std::shared_ptr<std::atomic_bool> m_comparisonCancellation;
    quint64 m_comparisonRequest = 0;
    int m_comparisonLoadingSlot = -1;
    bool m_comparisonPending = false;
    bool m_comparisonViewOpen = false;
    // KAN-56: theoretical best across the current comparison group's whole
    // eligible population, not the two comparison slots. Deliberately its own
    // background worker/cache rather than m_analysisSourceCache/m_comparisonSlots
    // -- it must decode every eligible lap's recording in turn, which the
    // 2-entry comparison cache is not sized for; a fresh single-request cache
    // is used instead (sized fine since laps are processed grouped by run).
    // The core result plus the request it answers (stale results are dropped).
    struct TheoreticalBestResult : FlappedEar::OutingTheoreticalBest {
        quint64 request = 0;
    };
    static TheoreticalBestResult computeOutingTheoreticalBest(QVector<FlappedEar::OutingLapRow> population,
        QHash<QString, QJsonObject> sourcesByRunId, QString projectPath, FlappedEar::ApprovedSegmentation approved,
        QString canonicalRunId, QJsonObject actualBestReference, quint64 request,
        const std::shared_ptr<std::atomic_bool> &cancellation);
    void initializeOutingTheoreticalBest();
    struct ChannelSummariesResult {
        quint64 request = 0;
        QString error;
        QVariantList runs;
    };
    static ChannelSummariesResult computeOutingChannelSummaries(QVector<FlappedEar::OutingLapRow> rows,
        QHash<QString, QJsonObject> sourcesByRunId, QString projectPath, quint64 request,
        const std::shared_ptr<std::atomic_bool> &cancellation);
    void initializeOutingChannelSummaries();
    void initializeOutingDayReport();
    [[nodiscard]] QVariantMap computeOutingDayReport() const;
    mutable std::optional<QVariantMap> m_dayReportCache;
    QFutureWatcher<ChannelSummariesResult> m_channelSummariesWatcher;
    std::shared_ptr<std::atomic_bool> m_channelSummariesCancellation;
    quint64 m_channelSummariesRequest = 0;
    QString m_channelSummariesState = QStringLiteral("idle");
    QString m_channelSummariesMessage;
    QVariantList m_channelSummariesRuns;
    QByteArray m_channelSummariesKey;
    // KAN-124: video as the analysis side sees it; null without video support.
    const VideoLink *m_videoLink = nullptr;
    [[nodiscard]] QByteArray channelSummariesInputKey() const;
    [[nodiscard]] QString outingLapLabel(const QJsonObject &reference) const;
    // What the theoretical best and loss ranking depend on; a document change
    // that leaves this unchanged (e.g. persisting the comparison pair) keeps
    // the result (KAN-117).
    [[nodiscard]] QByteArray theoreticalBestInputKey() const;
    QByteArray m_theoreticalBestKey;
    bool m_timeLossAllLaps = false;
    bool openComparisonEvidence(const QVariantMap &lapA, const QVariantMap &lapB, const QString &segmentId);
    QFutureWatcher<TheoreticalBestResult> m_theoreticalBestWatcher;
    std::shared_ptr<std::atomic_bool> m_theoreticalBestCancellation;
    quint64 m_theoreticalBestRequest = 0;
    QString m_theoreticalBestState = QStringLiteral("idle");
    QString m_theoreticalBestMessage;
    // The committed worker result (valid while the state is "ready").
    FlappedEar::OutingTheoreticalBest m_theoreticalBest;
    // Losses of each session's fastest lap (or every eligible lap) against the
    // actual best; requires a ready theoretical best with a timed actual best.
    [[nodiscard]] FlappedEar::TimeLossRanking computeTimeLossRanking(bool allLaps, qsizetype maximumResults) const;
    QString m_comparisonFocusSegmentId;
    // KAN-57: set when the comparison is opened from a theoretical-best
    // sector. The pair is then measured against the canonical run's approved
    // segments (the ones the theoretical best used), labelled as such, as long
    // as both laps are in that segmentation's group. Cleared on close.
    QString m_comparisonSegmentationRunId;
    [[nodiscard]] std::optional<FlappedEar::ApprovedSegmentation> comparisonSharedSegmentation() const;
    void initializeOutingLapDetail();
    void loadOutingLapDetail();
    QFutureWatcher<OutingLapDetailResult> m_outingLapDetailWatcher;
    QTimer m_outingLapDetailTimer;
    std::shared_ptr<std::atomic_bool> m_outingLapDetailCancellation;
    std::shared_ptr<const TelemetrySession> m_outingLapDetailSession;
    TrackGeometry m_outingLapDetailGeometry;
    QVariantMap m_selectedOutingLap;
    QJsonObject m_outingLapDetailSource;
    QByteArray m_outingLapDetailKey;
    quint64 m_outingLapDetailRequest = 0;
    bool m_outingLapDetailPending = false;
    QString m_outingLapDetailState = QStringLiteral("idle");
    QString m_outingLapDetailError;
    QStringList m_outingLapChannels;
    QString m_outingLapPendingChannel;
    QVariantList m_outingLapTrack;
    double m_outingLapCursor = 0;
    struct SegmentReviewResult {
        quint64 request = 0;
        FlappedEar::ProgressAxis axis;
        FlappedEar::TrackSegmentProposals proposals;
        QVector<FlappedEar::CornerGeometryPhases> phases; // one per proposal; invalid for straights
        QVector<FlappedEar::ProgressSegment> lapTrace;
        QString unavailable; // no proposals can be made, and why
        QString error;
    };
    static SegmentReviewResult computeSegmentReview(std::shared_ptr<const TelemetrySession> session,
        double startTime, double endTime, int lapNumber, quint64 request,
        const std::shared_ptr<std::atomic_bool> &cancellation);
    void initializeSegmentReview();
    void resetSegmentReview();
    [[nodiscard]] QString segmentReviewUnavailableReason() const;
    [[nodiscard]] QString segmentReviewConfiguration() const;
    [[nodiscard]] FlappedEar::ApprovedSegmentation currentApprovedSegmentation() const;
    [[nodiscard]] QVector<FlappedEar::SegmentReviewItem> currentSegmentReviewItems() const;
    bool replaceRunTrackSegments(const QString &runId, const QJsonArray &segments, bool recordHistory = true);
    [[nodiscard]] QJsonValue storedRunTrackSegments(const QString &runId) const;
    [[nodiscard]] QJsonValue storedRunValue(const QString &runId, const QString &key) const;
    bool replaceRunField(const QString &runId, const QString &key, const QJsonValue &value,
        const std::function<void()> &beforeNotify = {});
    QString applySegmentEdit(const std::optional<QJsonArray> &next, const QString &error);
    QString applySegmentHistoryStep(bool undo);
    [[nodiscard]] QVariantList mapPolylines(double startMeters, double endMeters) const;
    QFutureWatcher<SegmentReviewResult> m_segmentReviewWatcher;
    std::shared_ptr<std::atomic_bool> m_segmentReviewCancellation;
    quint64 m_segmentReviewRequest = 0;
    QString m_segmentReviewState = QStringLiteral("idle");
    QString m_segmentReviewMessage;
    FlappedEar::ProgressAxis m_segmentReviewAxis;
    QVector<FlappedEar::TrackSegmentProposal> m_segmentProposals;
    QVector<FlappedEar::CornerGeometryPhases> m_segmentProposalPhases;
    QVector<FlappedEar::ProgressSegment> m_segmentReviewLapTrace;
    QSet<int> m_editedSegmentProposals;
    QSet<int> m_rejectedSegmentProposals;
    mutable QVariantList m_segmentReviewLayerCache;
    mutable bool m_segmentReviewLayersDirty = true;
    FlappedEar::SegmentEditHistory m_segmentEditHistory;
    mutable QVector<FlappedEar::ProgressMapPoint> m_segmentReviewPickTrace;
    mutable bool m_segmentReviewPickTraceDirty = true;
    using OutingSourceMessage = FlappedEar::OutingSourceMessage;
    using OutingRunResult = FlappedEar::OutingRunDerivation;
    // The core derivation plus the request identity it answers.
    struct OutingLapResult : FlappedEar::OutingLapDerivation {
        QByteArray key;
        quint64 generation = 0;
    };
    void initializeOutingLaps();
    void refreshOutingLaps();
    void refreshOutingCompatibility();
    bool setRunTrackConfigurations(const QStringList &runIds, const QString &layoutId, const QString &direction);
    QVariantMap m_outingRanking;
    QVariantMap m_outingProgression;
    QVariantList m_outingCompatibilityGroups;
    QString m_outingComparisonGroupId;
    // The per-run track configuration used by rankOutingLaps/refreshOutingCompatibility,
    // captured so a second population consumer (theoretical best) can reuse the
    // exact same configurations without recomputing or risking drift.
    QHash<QString, QJsonObject> m_outingRunConfigurations;
    QVector<OutingLapRow> m_outingRawLapRows;
    QList<OutingSourceMessage> m_outingSourceMessages;
    [[nodiscard]] QByteArray outingLapKey() const;
    [[nodiscard]] QByteArray outingRunKey(const QString &runId) const;
    [[nodiscard]] QSet<QString> reusableOutingRuns() const;
    void invalidateOutingLapDetail();
    QHash<QString, OutingRunResult> m_outingRunCache;
    InferredTrackGroups m_outingInferredGroups;
    QHash<QString, quint64> m_outingRunGenerations;
    quint64 m_outingDocumentGeneration = 0;
    QFutureWatcher<OutingLapResult> m_outingLapWatcher;
    QTimer m_outingLapTimer;
    std::shared_ptr<std::atomic_bool> m_outingLapCancellation;
    QByteArray m_outingLapRequestedKey;
    quint64 m_outingLapGeneration = 0;
    QVariantList m_outingLapRows;
    QSet<QString> m_outingStaleRunIds;
    QStringList m_outingLapMessages;
    bool m_outingLapsLoading = false;
};

} // namespace FlappedEar
