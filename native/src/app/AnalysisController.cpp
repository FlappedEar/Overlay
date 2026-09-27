#include "app/AnalysisController.h"

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
}

bool AnalysisController::workRunning() const
{
    return m_outingLapWatcher.isRunning() || m_outingLapDetailWatcher.isRunning()
        || m_comparisonWatcher.isRunning() || m_segmentReviewWatcher.isRunning()
        || m_automaticSegmentsWatcher.isRunning();
}

QVariantMap AnalysisController::sessionSeries(const TelemetrySession &session, const QString &channelName,
    double telemetryStart, double telemetryEnd, int maximumPoints)
{
    SampledSegmentsStatus status = SampledSegmentsStatus::Ok;
    const QVector<QVector<QPointF>> sampledSegments = session.sampledSegments(
        channelName, telemetryStart, telemetryEnd, qBound(2, maximumPoints, 2000), &status);
    if (sampledSegments.isEmpty()) {
        // A genuinely empty overlap (status Ok) stays the plain "no data" shape
        // callers already expect; only a real range/channel problem gets a reason
        // so QML can tell "no data" apart from a rendering/data-shape failure.
        if (status == SampledSegmentsStatus::Ok) return {};
        QString reason;
        switch (status) {
        case SampledSegmentsStatus::InvalidRange: reason = QStringLiteral("invalidRange"); break;
        case SampledSegmentsStatus::ChannelMissing: reason = QStringLiteral("channelMissing"); break;
        case SampledSegmentsStatus::ChannelMalformed: reason = QStringLiteral("channelMalformed"); break;
        case SampledSegmentsStatus::Ok: break;
        }
        return {{"reason", reason}};
    }
    double minimum = sampledSegments.front().front().y();
    double maximum = minimum;
    for (const QVector<QPointF> &segment : sampledSegments) {
        for (const QPointF &sample : segment) {
            minimum = std::min(minimum, sample.y());
            maximum = std::max(maximum, sample.y());
        }
    }
    const double telemetrySpan = telemetryEnd - telemetryStart;
    QVariantList segments;
    segments.reserve(sampledSegments.size());
    for (const QVector<QPointF> &sampledSegment : sampledSegments) {
        QVariantList points;
        points.reserve(sampledSegment.size());
        for (const QPointF &sample : sampledSegment) {
            const double normalizedTime = telemetrySpan == 0.0
                ? 0.0
                : (sample.x() - telemetryStart) / telemetrySpan;
            points.append(QVariantMap{{"x", normalizedTime}, {"y", sample.y()}});
        }
        // QVariantList has an overload that appends another list's elements.
        // Wrap the points list explicitly so QML receives segments -> points,
        // preserving telemetry gaps as separate polylines.
        segments.append(QVariant::fromValue(points));
    }
    const QString resolved = session.aliases.value(channelName, channelName);
    const auto channel = session.channels.constFind(resolved);
    return {
        {"segments", segments},
        // Presentation only: retain signed samples and units for inspection.
        {"brakingUp", resolved == session.aliases.value("longitudinalAcceleration")},
        {"minimum", minimum},
        {"maximum", maximum},
        {"unit", channel == session.channels.cend() ? QString() : channel->unit},
    };
}

} // namespace FlappedEar
