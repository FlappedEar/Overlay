#include "app/AppController.h"
#include "project/EventProjectCodec.h"
#include "telemetry/TelemetrySession.h"

// KAN-124: AppController's side of the day's analysis. It owns the document
// AnalysisController reads (AnalysisDocument) and the video it links to
// (VideoLink), and forwards its unchanged QML API to AnalysisController.

namespace FlappedEar {

void AppController::initializeAnalysis()
{
    // Analysis handlers keep their place among this controller's own
    // document and source handlers.
    connect(this, &AppController::documentStateChanged, &m_analysis, &AnalysisController::documentStateChanged);
    connect(this, &AppController::sourceLoadStateChanged, &m_analysis, &AnalysisController::sourceLoadStateChanged);
    connect(&m_analysis, &AnalysisController::lapExclusionsRefreshing, this, &AppController::applyActiveLapExclusions);
    connect(&m_analysis, &AnalysisController::outingLapsChanged, this, &AppController::outingLapsChanged);
    connect(&m_analysis, &AnalysisController::outingLapDetailChanged, this, &AppController::outingLapDetailChanged);
    connect(&m_analysis, &AnalysisController::outingLapVideoChanged, this, &AppController::outingLapVideoChanged);
    connect(&m_analysis, &AnalysisController::outingTheoreticalBestChanged, this, &AppController::outingTheoreticalBestChanged);
    connect(&m_analysis, &AnalysisController::outingChannelSummariesChanged, this, &AppController::outingChannelSummariesChanged);
    connect(&m_analysis, &AnalysisController::outingDayReportChanged, this, &AppController::outingDayReportChanged);
    connect(&m_analysis, &AnalysisController::comparisonFocusSegmentIdChanged, this, &AppController::comparisonFocusSegmentIdChanged);
    connect(&m_analysis, &AnalysisController::comparisonSlotsChanged, this, &AppController::comparisonSlotsChanged);
    connect(&m_analysis, &AnalysisController::comparisonViewOpenChanged, this, &AppController::comparisonViewOpenChanged);
    connect(&m_analysis, &AnalysisController::outingLapCursorChanged, this, &AppController::outingLapCursorChanged);
    connect(&m_analysis, &AnalysisController::segmentReviewChanged, this, &AppController::segmentReviewChanged);
    m_analysis.setVideoLink(this);
}

QJsonObject AppController::activeLapBinding() const
{
    for (auto value : m_analysis.outingLapSources()) {
        auto source = value.toObject();
        if (source.value("runId").toString() != activeRunId()) continue;
        source.insert("sourceRevision", QString::fromLatin1(m_loadedSourceRevision));
        source.remove("reference"); source.remove("name"); source.remove("trackConfiguration");
        return source;
    }
    return {};
}

void AppController::applyActiveLapExclusions()
{
    const auto exclusions = currentProjectObject().value("event").toObject().value("lapExclusions").toArray();
    applyLapExclusions(m_lapSession, activeLapBinding(), exclusions);
    m_previewRenderContext.setLapSession(m_lapSession);
    emit lapNavigationChanged();
    emit liveValuesChanged();
}

// AppController's VideoLink (the overlay side). The lap's run must be
// the active, loaded one; a lap from another run has no video rather than
// silently switching runs. Out-of-range footage is unavailable, never clamped.
std::optional<qint64> AppController::videoPositionForTelemetry(const QString &runId, const double telemetrySeconds) const
{
    if (m_videoSource.isEmpty() || runId != activeRunId()) return std::nullopt;
    const auto videoTime = FlappedEar::telemetryToVideoTime(telemetrySeconds, m_sync);
    if (!videoTime || *videoTime < 0.0) return std::nullopt;
    const auto milliseconds = qRound64(*videoTime * 1000.0);
    if (milliseconds > previewEndPositionMilliseconds()) return std::nullopt;
    return clampPreviewPositionMilliseconds(milliseconds);
}

std::optional<double> AppController::telemetryForVideoPosition(const QString &runId, const qint64 videoMilliseconds) const
{
    if (runId != activeRunId()) return std::nullopt;
    return FlappedEar::videoToTelemetryTime(videoMilliseconds / 1000.0, m_sync);
}



QVariantMap AppController::runMetadata(const QString &runId) const
{
    return m_analysis.runMetadata(runId);
}

bool AppController::updateRunMetadata(const QString &runId, const QString &expectedToken, const QString &name, const QString &notes, const QString &conditions, const QString &setupChanges)
{
    return m_analysis.updateRunMetadata(runId, expectedToken, name, notes, conditions, setupChanges);
}

QVariantMap AppController::runTrackConfiguration(const QString &runId) const
{
    return m_analysis.runTrackConfiguration(runId);
}

bool AppController::confirmRunTrackConfiguration(const QString &runId, const QString &expectedDerivationKey, const QString &layoutId, const QString &direction, bool applyToMatching)
{
    return m_analysis.confirmRunTrackConfiguration(runId, expectedDerivationKey, layoutId, direction, applyToMatching);
}

bool AppController::selectOutingComparisonGroup(const QString &groupId)
{
    return m_analysis.selectOutingComparisonGroup(groupId);
}

QVariantMap AppController::outingRanking() const
{
    return m_analysis.outingRanking();
}

QVariantMap AppController::outingProgression() const
{
    return m_analysis.outingProgression();
}

QVariantMap AppController::outingTheoreticalBest() const
{
    return m_analysis.outingTheoreticalBest();
}

QVariantMap AppController::outingTimeLossRanking() const
{
    return m_analysis.outingTimeLossRanking();
}

QVariantMap AppController::outingLapConsistency() const
{
    return m_analysis.outingLapConsistency();
}

QVariantMap AppController::outingChannelSummaries() const
{
    return m_analysis.outingChannelSummaries();
}

void AppController::requestOutingChannelSummaries()
{
    m_analysis.requestOutingChannelSummaries();
}

QVariantMap AppController::outingDayReport() const
{
    return m_analysis.outingDayReport();
}

void AppController::requestOutingDayReport()
{
    m_analysis.requestOutingDayReport();
}

bool AppController::openFocusArea(const QVariantMap &evidence)
{
    return m_analysis.openFocusArea(evidence);
}

QVariantMap AppController::outingSectorProgression() const
{
    return m_analysis.outingSectorProgression();
}

void AppController::setOutingTimeLossAllLaps(bool allLaps)
{
    m_analysis.setOutingTimeLossAllLaps(allLaps);
}

void AppController::requestOutingTheoreticalBest()
{
    m_analysis.requestOutingTheoreticalBest();
}

bool AppController::openTheoreticalBestSector(const QString &segmentId)
{
    return m_analysis.openTheoreticalBestSector(segmentId);
}

bool AppController::openTimeLoss(const QVariantMap &loss)
{
    return m_analysis.openTimeLoss(loss);
}

bool AppController::openComparisonLapAtProgress(int slot, double progressMeters)
{
    return m_analysis.openComparisonLapAtProgress(slot, progressMeters);
}

void AppController::clearComparisonFocusSegment()
{
    m_analysis.clearComparisonFocusSegment();
}

QVariantList AppController::outingCompatibilityGroups() const
{
    return m_analysis.outingCompatibilityGroups();
}

QString AppController::outingComparisonGroupId() const
{
    return m_analysis.outingComparisonGroupId();
}

QString AppController::outingComparisonSelectionState() const
{
    return m_analysis.outingComparisonSelectionState();
}

bool AppController::setRunTrackConfiguration( const QString &runId, const QString &layoutId, const QString &direction)
{
    return m_analysis.setRunTrackConfiguration(runId, layoutId, direction);
}

QVariantList AppController::comparisonSlots() const
{
    return m_analysis.comparisonSlots();
}

QVariantList AppController::comparisonLaps() const
{
    return m_analysis.comparisonLaps();
}

bool AppController::comparisonPairReady() const
{
    return m_analysis.comparisonPairReady();
}

bool AppController::selectComparisonLap(int slot, const QVariantMap &reference)
{
    return m_analysis.selectComparisonLap(slot, reference);
}

void AppController::clearComparisonLap(int slot)
{
    m_analysis.clearComparisonLap(slot);
}

bool AppController::swapComparisonLaps()
{
    return m_analysis.swapComparisonLaps();
}

bool AppController::useBestComparisonLap(bool wholeDay)
{
    return m_analysis.useBestComparisonLap(wholeDay);
}

bool AppController::inspectComparisonLap(int slot)
{
    return m_analysis.inspectComparisonLap(slot);
}

void AppController::setComparisonViewOpen(bool open)
{
    m_analysis.setComparisonViewOpen(open);
}

QVariantMap AppController::comparisonLapSeries( int slot, const QString &channel, double startTime, double endTime, int maximumPoints) const
{
    return m_analysis.comparisonLapSeries(slot, channel, startTime, endTime, maximumPoints);
}

QVariantList AppController::comparisonLapTrack(int slot) const
{
    return m_analysis.comparisonLapTrack(slot);
}

QVariantList AppController::comparisonOverlayTrack(int slot) const
{
    return m_analysis.comparisonOverlayTrack(slot);
}

QVariantMap AppController::comparisonPositionAtProgress(int slot, double progressMeters) const
{
    return m_analysis.comparisonPositionAtProgress(slot, progressMeters);
}

QVariantMap AppController::comparisonChannelSeriesByProgress( int slot, const QString &channel, double startProgress, double endProgress, int maximumPoints) const
{
    return m_analysis.comparisonChannelSeriesByProgress(slot, channel, startProgress, endProgress, maximumPoints);
}

double AppController::comparisonProgressAxisLength() const
{
    return m_analysis.comparisonProgressAxisLength();
}

QVariantMap AppController::comparisonDeltaSeriesByProgress(double startProgress, double endProgress, int maximumPoints) const
{
    return m_analysis.comparisonDeltaSeriesByProgress(startProgress, endProgress, maximumPoints);
}

QStringList AppController::comparisonAvailableChannels() const
{
    return m_analysis.comparisonAvailableChannels();
}

QVariantMap AppController::comparisonPersistedRangeMeters() const
{
    return m_analysis.comparisonPersistedRangeMeters();
}

QStringList AppController::comparisonPersistedChannels() const
{
    return m_analysis.comparisonPersistedChannels();
}

void AppController::persistComparisonRange(double startMeters, double endMeters)
{
    m_analysis.persistComparisonRange(startMeters, endMeters);
}

void AppController::persistComparisonChannels(const QStringList &channels)
{
    m_analysis.persistComparisonChannels(channels);
}

QVariantList AppController::comparisonApprovedSegments() const
{
    return m_analysis.comparisonApprovedSegments();
}

QVariantMap AppController::comparisonSegmentMetrics(const QString &segmentId) const
{
    return m_analysis.comparisonSegmentMetrics(segmentId);
}

QString AppController::comparisonSegmentationNote() const
{
    return m_analysis.comparisonSegmentationNote();
}

QStringList AppController::comparisonPreferredChannels() const
{
    return m_analysis.comparisonPreferredChannels();
}

QString AppController::formatElapsedTime(double seconds)
{
    return AnalysisController::formatElapsedTime(seconds);
}

QVariantMap AppController::comparisonTimeLossObservations() const
{
    return m_analysis.comparisonTimeLossObservations();
}

QVariantMap AppController::comparisonGgScatter(double startMeters, double endMeters, int maximumPoints) const
{
    return m_analysis.comparisonGgScatter(startMeters, endMeters, maximumPoints);
}

QVariantMap AppController::comparisonHeartRate(double startMeters, double endMeters) const
{
    return m_analysis.comparisonHeartRate(startMeters, endMeters);
}

bool AppController::selectOutingLap(int index)
{
    return m_analysis.selectOutingLap(index);
}

QVariantMap AppController::resolveOutingLapReference(const QVariantMap &reference) const
{
    return m_analysis.resolveOutingLapReference(reference);
}

bool AppController::selectOutingLapReference(const QVariantMap &reference)
{
    return m_analysis.selectOutingLapReference(reference);
}

bool AppController::openOutingLapChannel(const QVariantMap &reference, const QString &channel)
{
    return m_analysis.openOutingLapChannel(reference, channel);
}

bool AppController::setOutingLapExcluded(const QVariantMap &reference, bool excluded, const QString &reason)
{
    return m_analysis.setOutingLapExcluded(reference, excluded, reason);
}

void AppController::closeOutingLap()
{
    m_analysis.closeOutingLap();
}

void AppController::requestSegmentReview()
{
    m_analysis.requestSegmentReview();
}

QString AppController::approveSegmentProposal(int index)
{
    return m_analysis.approveSegmentProposal(index);
}

int AppController::approveCertainSegmentProposals()
{
    return m_analysis.approveCertainSegmentProposals();
}

bool AppController::setSegmentProposalRejected(int index, bool rejected)
{
    return m_analysis.setSegmentProposalRejected(index, rejected);
}

QString AppController::editSegmentProposal(int index, const QString &name, const QString &type, double startMeters, double endMeters)
{
    return m_analysis.editSegmentProposal(index, name, type, startMeters, endMeters);
}

bool AppController::revokeApprovedSegment(const QString &id)
{
    return m_analysis.revokeApprovedSegment(id);
}

bool AppController::discardOtherConfigurationSegments()
{
    return m_analysis.discardOtherConfigurationSegments();
}

QString AppController::editApprovedSegment(const QString &id, const QString &name, const QString &type, double startMeters, double endMeters, bool keepAdjacentJoined)
{
    return m_analysis.editApprovedSegment(id, name, type, startMeters, endMeters, keepAdjacentJoined);
}

QString AppController::splitApprovedSegment(const QString &id, double atMeters)
{
    return m_analysis.splitApprovedSegment(id, atMeters);
}

QString AppController::mergeApprovedSegments(const QString &firstId, const QString &secondId)
{
    return m_analysis.mergeApprovedSegments(firstId, secondId);
}

QString AppController::undoSegmentEdit()
{
    return m_analysis.undoSegmentEdit();
}

QString AppController::redoSegmentEdit()
{
    return m_analysis.redoSegmentEdit();
}

QVariantMap AppController::segmentReviewProgressAt(double x, double y) const
{
    return m_analysis.segmentReviewProgressAt(x, y);
}

QVariantMap AppController::outingLapSectorTimes() const
{
    return m_analysis.outingLapSectorTimes();
}

QVariantList AppController::outingLapCornerSpeeds() const
{
    return m_analysis.outingLapCornerSpeeds();
}

QVariantList AppController::outingLapBrakingMetrics() const
{
    return m_analysis.outingLapBrakingMetrics();
}

QVariantList AppController::outingLapExitMetrics() const
{
    return m_analysis.outingLapExitMetrics();
}

QVariantMap AppController::outingLapSeries(const QString &channel, int maximumPoints) const
{
    return m_analysis.outingLapSeries(channel, maximumPoints);
}

QVariantMap AppController::outingLapSeries( const QString &channel, double startTime, double endTime, int maximumPoints) const
{
    return m_analysis.outingLapSeries(channel, startTime, endTime, maximumPoints);
}

QString AppController::outingLapValueText(const QString &channel) const
{
    return m_analysis.outingLapValueText(channel);
}

QStringList AppController::outingLapAvailableChannels() const
{
    return m_analysis.outingLapAvailableChannels();
}

void AppController::setOutingLapChannels(const QStringList &channels)
{
    m_analysis.setOutingLapChannels(channels);
}

QVariantMap AppController::outingLapTrackPoint() const
{
    return m_analysis.outingLapTrackPoint();
}

double AppController::segmentReviewAxisLength() const
{
    return m_analysis.segmentReviewAxisLength();
}

QVariantList AppController::segmentReviewItems() const
{
    return m_analysis.segmentReviewItems();
}

QVariantMap AppController::segmentReviewApproved() const
{
    return m_analysis.segmentReviewApproved();
}

QVariantList AppController::segmentReviewMapLayers() const
{
    return m_analysis.segmentReviewMapLayers();
}

void AppController::setOutingLapCursor(double seconds)
{
    m_analysis.setOutingLapCursor(seconds);
}

bool AppController::outingLapVideoAvailable() const
{
    return m_analysis.outingLapVideoAvailable();
}

qint64 AppController::outingLapVideoPositionMilliseconds() const
{
    return m_analysis.outingLapVideoPositionMilliseconds();
}

bool AppController::followOutingLapVideoPosition(qint64 videoPositionMilliseconds)
{
    return m_analysis.followOutingLapVideoPosition(videoPositionMilliseconds);
}

QVariantList AppController::outingLaps() const
{
    return m_analysis.outingLaps();
}

QVariantMap AppController::outingAnalysisStatus() const
{
    return m_analysis.outingAnalysisStatus();
}

bool AppController::retryOutingAnalysis()
{
    return m_analysis.retryOutingAnalysis();
}

QVariantMap AppController::outingLapCoasting() const
{
    return m_analysis.outingLapCoasting();
}

} // namespace FlappedEar
