#include "app/AnalysisController.h"
#include "telemetry/ChannelSeries.h"

#include <algorithm>
#include <limits>

namespace FlappedEar {

AnalysisController::AnalysisController(AnalysisDocument &document, QObject *parent)
    : QObject(parent)
    , m_document(document)
{
    initializeOutingLaps();
    initializeOutingLapDetail();
    initializeSegmentReview();
    initializeComparisonLaps();
    initializeOutingTheoreticalBest();
    initializeOutingChannelSummaries();
    initializeOutingDayReport();
    initializeAutomaticSegments();
}

AnalysisController::~AnalysisController() = default;

bool AnalysisController::documentEditable() const
{
    return m_document.isEventDocument() && !m_document.projectLoading() && !m_document.documentBusy()
        && !m_document.recoveryPending() && !m_document.batchImportPending()
        && !m_document.destructiveActionPending()
        && m_document.documentRevision() != std::numeric_limits<quint64>::max();
}

void AnalysisController::resetForNewSources()
{
    ++m_outingDocumentGeneration;
    m_outingRunCache.clear();
    m_outingInferredGroups = {};
    m_outingRunGenerations.clear();
    closeOutingLap();
    resetComparisonSlot(0);
    resetComparisonSlot(1);
    setComparisonViewOpen(false);
    // KAN-151: day results belong to the day they were asked for. A new
    // request number rejects any result still on its way.
    ++m_theoreticalBestRequest;
    if (m_theoreticalBestCancellation) m_theoreticalBestCancellation->store(true);
    m_theoreticalBestKey.clear();
    if (m_theoreticalBestState != "idle") {
        m_theoreticalBestState = QStringLiteral("idle");
        m_theoreticalBestMessage.clear();
        m_theoreticalBest.best = {};
        m_theoreticalBest.actualBest.reset();
        m_theoreticalBest.population.clear();
        emit outingTheoreticalBestChanged();
    }
    ++m_channelSummariesRequest;
    if (m_channelSummariesCancellation) m_channelSummariesCancellation->store(true);
    m_channelSummariesKey.clear();
    if (m_channelSummariesState != "idle") {
        m_channelSummariesState = QStringLiteral("idle");
        m_channelSummariesMessage.clear();
        m_channelSummariesRuns.clear();
        emit outingChannelSummariesChanged();
    }
}

void AnalysisController::activeRunSourceReplaced(const QString &runId)
{
    ++m_outingRunGenerations[runId];
    invalidateOutingLapDetail();
    invalidateComparisonLaps();
}

void AnalysisController::cancelWork(const bool cancelDetail)
{
    if (cancelDetail && m_comparisonCancellation) m_comparisonCancellation->store(true);
    if (cancelDetail && m_outingLapDetailCancellation) m_outingLapDetailCancellation->store(true);
    if (cancelDetail && m_segmentReviewCancellation) m_segmentReviewCancellation->store(true);
    if (m_outingLapCancellation) m_outingLapCancellation->store(true);
    if (m_automaticSegmentsCancellation) m_automaticSegmentsCancellation->store(true);
    // Day results are kept while the same day's sources change (a video
    // opening); a new day stops them (KAN-151).
    if (cancelDetail && m_theoreticalBestCancellation) m_theoreticalBestCancellation->store(true);
    if (cancelDetail && m_channelSummariesCancellation) m_channelSummariesCancellation->store(true);
}

bool AnalysisController::workRunning() const
{
    return m_outingLapWatcher.isRunning() || m_outingLapDetailWatcher.isRunning()
        || m_comparisonWatcher.isRunning() || m_segmentReviewWatcher.isRunning()
        || m_automaticSegmentsWatcher.isRunning() || m_theoreticalBestWatcher.isRunning()
        || m_channelSummariesWatcher.isRunning();
}

QVariantMap AnalysisController::sessionSeries(const TelemetrySession &session, const QString &channelName,
    double telemetryStart, double telemetryEnd, int maximumPoints)
{
    return channelSeries(session, channelName, telemetryStart, telemetryEnd, maximumPoints);
}

} // namespace FlappedEar
