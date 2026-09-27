// KAN-136: segments without manual review. When a track layout of the day has
// no approved segments on any run, the proposals of the day's best lap are
// approved automatically (all of them, as a driver accepting the proposal
// would), so sector times, the theoretical best, losses and the Corner
// Analyzer work straight after import. They are ordinary approved segments:
// the lap view's segment review edits, splits, merges or revokes them.
// Off by default (the Overlays app turns it on); the review stays explicit
// wherever segments already exist.

#include "app/AnalysisController.h"
#include "telemetry/OutingLapLoader.h"
#include "telemetry/OutingLaps.h"
#include "telemetry/TrackSegments.h"

#include <QJsonDocument>
#include <QtConcurrent/QtConcurrentRun>

using namespace FlappedEar;

void AnalysisController::setAutomaticSegments(const bool enabled)
{
    m_automaticSegments = enabled;
    if (enabled) QMetaObject::invokeMethod(this, &AnalysisController::createAutomaticSegments, Qt::QueuedConnection);
}

void AnalysisController::initializeAutomaticSegments()
{
    connect(this, &AnalysisController::outingLapsChanged, this, [this] {
        if (m_automaticSegments) QMetaObject::invokeMethod(this, &AnalysisController::createAutomaticSegments, Qt::QueuedConnection);
    });
    connect(&m_automaticSegmentsWatcher, &QFutureWatcher<AutomaticSegmentsResult>::finished, this, [this] {
        const auto result = m_automaticSegmentsWatcher.future().takeResult();
        // Stale: another document, another best lap, or segments approved meanwhile.
        if (result.key != automaticSegmentsKey() || groupHasApprovedSegments(result.groupId) || !result.error.isEmpty()
            || result.review.proposals.proposals.isEmpty() || !result.review.unavailable.isEmpty()) {
            if (!result.error.isEmpty() || !result.review.unavailable.isEmpty())
                emit automaticSegmentsFinished(QString(), 0);
            return;
        }
        QJsonValue stored;
        for (const auto &value : m_document.analysisProject().value("event").toObject().value("runs").toArray())
            if (value.toObject().value("id").toString() == result.runId) stored = value.toObject().value("trackSegments");
        int approved = 0;
        for (const auto &proposal : result.review.proposals.proposals) {
            const auto segment = makeTrackSegment(proposal.type, proposal.name, proposal.start.progressMeters,
                proposal.end.progressMeters, result.groupId);
            const auto next = segment.isEmpty() ? std::nullopt
                : withApprovedSegment(stored, segment, result.review.axis.lengthMeters);
            if (!next) continue;
            stored = *next;
            ++approved;
        }
        if (approved == 0 || !replaceRunTrackSegments(result.runId, stored.toArray(), false)) return;
        emit automaticSegmentsFinished(result.lapLabel, approved);
    });
}

QByteArray AnalysisController::automaticSegmentsKey() const
{
    const auto best = m_outingRanking.value("bestOfDay").toMap();
    return m_document.documentIdentity().toUtf8() + '\n' + m_outingComparisonGroupId.toUtf8() + '\n'
        + QJsonDocument(QJsonObject::fromVariantMap(best.value("reference").toMap())).toJson(QJsonDocument::Compact);
}

bool AnalysisController::groupHasApprovedSegments(const QString &groupId) const
{
    for (const auto &value : m_document.analysisProject().value("event").toObject().value("runs").toArray()) {
        const auto approved = approvedSegmentation(value.toObject().value("trackSegments"), groupId);
        if (approved.valid && !approved.segments.isEmpty()) return true;
    }
    return false;
}

void AnalysisController::createAutomaticSegments()
{
    if (!m_automaticSegments || outingLapsLoading() || m_automaticSegmentsWatcher.isRunning()
        || m_outingComparisonGroupId.isEmpty() || !documentEditable())
        return;
    const auto group = m_outingComparisonGroupId;
    if (!group.startsWith(QLatin1String("compatibility-v1:")) || groupHasApprovedSegments(group)) return;
    const auto best = m_outingRanking.value("bestOfDay").toMap();
    if (best.isEmpty()) return;
    const auto key = automaticSegmentsKey();
    if (key == m_automaticSegmentsAttempted) return; // once per document, layout and best lap
    const auto resolved = resolveOutingLapReference(best.value("reference").toMap());
    if (resolved.value("state") != "resolved") return;
    const auto row = m_outingLapRows.value(resolved.value("index").toInt()).toMap();
    QJsonObject source;
    for (const auto &value : outingLapSources())
        if (value.toObject().value("runId").toString() == row.value("runId").toString()) source = value.toObject();
    if (source.isEmpty()) return;
    m_automaticSegmentsAttempted = key;
    const auto cancellation = std::make_shared<std::atomic_bool>(false);
    m_automaticSegmentsCancellation = cancellation;
    const auto projectPath = m_document.documentProjectPath();
    const auto cache = m_analysisSourceCache;
    const auto lapLabel = QStringLiteral("%1 · LAP %2").arg(row.value("runName").toString()).arg(row.value("lapNumber").toInt());
    m_automaticSegmentsWatcher.setFuture(QtConcurrent::run([source, projectPath, row, cache, cancellation, key, group, lapLabel] {
        AutomaticSegmentsResult result;
        result.key = key;
        result.groupId = group;
        result.runId = row.value("runId").toString();
        result.lapLabel = lapLabel;
        const auto detail = FlappedEar::loadOutingLapDetail(source, projectPath, row, 0, cancellation, cache);
        if (!detail.session) { result.error = detail.error; return result; }
        result.review = computeSegmentReview(detail.session, row.value("startTime").toDouble(), row.value("endTime").toDouble(),
            row.value("lapNumber").toInt(), 0, cancellation);
        result.error = result.review.error;
        return result;
    }));
}
