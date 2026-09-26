// KAN-71: the computed day report. Assembles results the controller already
// computed (ranking, progression, consistency, theoretical best with its
// losses and section progression, channel summaries) into the portable
// telemetry/DayReport.h document with provenance. Nothing is recalculated
// here; a result that has not been computed is reported as such.

#include "app/AppController.h"
#include "telemetry/ChannelSummary.h"
#include "telemetry/DayReport.h"
#include "telemetry/FocusAreas.h"
#include "telemetry/OutingDayReport.h"

#include <QJsonDocument>

using namespace FlappedEar;


void AppController::initializeOutingDayReport()
{
    const auto invalidate = [this] {
        m_dayReportCache.reset();
        emit outingDayReportChanged();
    };
    connect(this, &AppController::outingLapsChanged, this, invalidate);
    connect(this, &AppController::outingTheoreticalBestChanged, this, invalidate);
    connect(this, &AppController::outingChannelSummariesChanged, this, invalidate);
    connect(this, &AppController::documentStateChanged, this, invalidate);
}

void AppController::requestOutingDayReport()
{
    requestOutingTheoreticalBest();
    requestOutingChannelSummaries();
}

bool AppController::openFocusArea(const QVariantMap &evidence)
{
    if (evidence.value("kind").toString() != "segment") return false;
    return openComparisonEvidence(evidence.value("reference").toMap(), evidence.value("against").toMap(),
        evidence.value("segmentId").toString());
}

QVariantMap AppController::outingDayReport() const
{
    if (!m_dayReportCache) m_dayReportCache = computeOutingDayReport();
    return *m_dayReportCache;
}

QVariantMap AppController::computeOutingDayReport() const
{
    OutingDayReportSources sources;
    sources.eventId = currentProjectObject().value("event").toObject().value("id").toString();
    sources.groupId = m_outingComparisonGroupId;
    sources.decisionsKey = theoreticalBestInputKey();
    sources.theoreticalKey = m_theoreticalBestKey;
    sources.lapsLoading = outingLapsLoading();
    sources.ranking = outingRanking();
    sources.progression = outingProgression();
    sources.consistency = outingLapConsistency();
    sources.theoretical = outingTheoreticalBest();
    sources.timeLosses = outingTimeLossRanking();
    sources.sectionProgression = outingSectorProgression();
    sources.channelSummaries = outingChannelSummaries();
    sources.lapLabel = [this](const QJsonObject &reference) { return outingLapLabel(reference); };
    try {
        for (const auto *row : eligibleOutingLaps(m_outingRawLapRows, m_outingComparisonGroupId, m_outingRunConfigurations,
                 currentProjectObject().value("event").toObject().value("lapExclusions").toArray(), m_outingStaleRunIds))
            sources.eligibleLaps.append(QJsonObject{{"kind", "lap"}, {"reference", row->reference},
                {"label", QStringLiteral("%1 · LAP %2").arg(row->runName).arg(row->lapNumber)}});
    } catch (const std::exception &) {
        sources.eligibleLaps = {};
    }
    // KAN-73: focus inputs need the actual best timed on the canonical axis.
    if (m_theoreticalBestState == "ready" && m_theoreticalBestActual) {
        FocusInputs focus;
        focus.referenceLap = m_theoreticalBestActual->lapReference;
        focus.referenceLabel = outingLapLabel(focus.referenceLap);
        for (const auto &value : sources.theoretical.value("sectors").toList()) {
            const auto sector = value.toMap();
            if (!sector.contains("lossSeconds") || !sector.contains("sourceLapReference")) continue;
            focus.gaps.append({sector.value("segmentId").toString(), sector.value("name").toString(),
                sector.value("lossSeconds").toDouble(), focus.referenceLap, focus.referenceLabel,
                QJsonObject::fromVariantMap(sector.value("sourceLapReference").toMap()), sector.value("sourceLapLabel").toString()});
        }
        const auto ranking = computeTimeLossRanking(false, maximumOutingLapRows);
        if (ranking.valid) {
            focus.comparedLapCount = ranking.comparedLapCount;
            for (const auto &loss : ranking.losses)
                focus.losses.append({loss.window.segmentId, loss.window.name, loss.lossSeconds, loss.lapReference});
        }
        QStringList cornerIds = m_theoreticalBestCornerObservations.keys();
        cornerIds.sort();
        for (const auto &segmentId : cornerIds) {
            QString name = segmentId;
            for (const auto &sector : m_theoreticalBestBest.sectors)
                if (sector.segmentId == segmentId) name = sector.name;
            focus.corners.append({segmentId, name, m_theoreticalBestCornerObservations.value(segmentId)});
        }
        sources.focus = std::move(focus);
    }
    try {
        return buildOutingDayReport(sources).toVariantMap();
    } catch (const std::exception &error) {
        return {{"schema", QString::fromLatin1(dayReportSchema)}, {"error", QString::fromUtf8(error.what())}};
    }
}
