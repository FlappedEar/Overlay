// KAN-56: theoretical best sector times across a whole compatible population
// of laps (not just the two comparison slots or the single open lap).
// Segments are approved strictly per run (KAN-48-50); there is no cross-run
// merge mechanism, so one run's approved segmentation stands in as canonical
// for the whole population, the same way KAN-55's comparison view requires an
// exact revision match rather than guessing a correspondence between two
// independently-approved sets.

#include "app/AnalysisController.h"
#include "telemetry/OutingTheoreticalBestResults.h"
#include "telemetry/OutingLaps.h"
#include "telemetry/SectorTiming.h"
#include "telemetry/TheoreticalBest.h"
#include "telemetry/TrackProgress.h"
#include "telemetry/BrakingMetrics.h"
#include "telemetry/CornerSpeeds.h"
#include "telemetry/ExitMetrics.h"
#include "telemetry/TelemetryGeometry.h"

#include <QCryptographicHash>
#include <QJsonDocument>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>

using namespace FlappedEar;


void AnalysisController::initializeOutingTheoreticalBest()
{
    connect(&m_theoreticalBestWatcher, &QFutureWatcher<TheoreticalBestResult>::finished, this, [this] {
        auto result = m_theoreticalBestWatcher.future().takeResult();
        if (result.request != m_theoreticalBestRequest) return; // stale: outing laps changed or a new request started
        m_theoreticalBest.best = {};
        m_theoreticalBest.actualBest.reset();
        m_theoreticalBest.population.clear();
        if (!result.error.isEmpty()) {
            m_theoreticalBestState = QStringLiteral("error");
            m_theoreticalBestMessage = result.error;
        } else if (!result.best.valid) {
            m_theoreticalBestState = QStringLiteral("unavailable");
            m_theoreticalBestMessage = theoreticalBestReasonText(result.best.unavailableReason);
        } else {
            m_theoreticalBestState = QStringLiteral("ready");
            m_theoreticalBestMessage = result.best.totalSeconds
                ? QString() : theoreticalBestReasonText(result.best.unavailableReason);
            m_theoreticalBest.best = std::move(result.best);
            m_theoreticalBest.actualBest = std::move(result.actualBest);
            m_theoreticalBest.canonicalRunId = result.canonicalRunId;
            m_theoreticalBest.population = std::move(result.population);
            m_theoreticalBest.approved = std::move(result.approved);
            m_theoreticalBest.axisLengthMeters = result.axisLengthMeters;
            m_theoreticalBest.axis = std::move(result.axis);
            m_theoreticalBest.cornerObservations = std::move(result.cornerObservations);
        }
        emit outingTheoreticalBestChanged();
    });
    // The eligible population, its approved segmentation or the document
    // itself can change under an in-flight or displayed result; never show a
    // theoretical best computed from laps that no longer apply.
    const auto invalidate = [this] {
        if (m_theoreticalBestState == "idle") return;
        // While laps are re-derived (e.g. after Save As moves the project)
        // the population is transient; decide once they are loaded. A stale
        // worker result is still rejected by its request number.
        if (outingLapsLoading()) return;
        if (!m_theoreticalBestKey.isEmpty() && theoreticalBestInputKey() == m_theoreticalBestKey) return;
        ++m_theoreticalBestRequest;
        if (m_theoreticalBestCancellation) m_theoreticalBestCancellation->store(true);
        m_theoreticalBestState = QStringLiteral("idle");
        m_theoreticalBestMessage.clear();
        m_theoreticalBest.best = {};
        m_theoreticalBest.actualBest.reset();
        m_theoreticalBest.population.clear();
        emit outingTheoreticalBestChanged();
    };
    connect(this, &AnalysisController::outingLapsChanged, this, invalidate);
    connect(this, &AnalysisController::documentStateChanged, this, invalidate);
}

QByteArray AnalysisController::theoreticalBestInputKey() const
{
    const auto event = m_document.analysisProject().value("event").toObject();
    QJsonArray runs;
    for (const auto &value : event.value("runs").toArray()) {
        const auto run = value.toObject();
        runs.append(QJsonObject{{"id", run.value("id")}, {"trackSegments", run.value("trackSegments")},
            {"trackConfiguration", run.value("trackConfiguration")}});
    }
    QJsonArray population;
    try {
        for (const auto *row : eligibleOutingLaps(m_outingRawLapRows, m_outingComparisonGroupId,
                 m_outingRunConfigurations, event.value("lapExclusions").toArray(), m_outingStaleRunIds))
            population.append(row->reference);
    } catch (const std::exception &) {
        population = {};
    }
    const QJsonObject key{{"group", m_outingComparisonGroupId}, {"runs", runs}, {"population", population},
        {"exclusions", event.value("lapExclusions")}, {"best", QJsonObject::fromVariantMap(
            m_outingRanking.value("bestOfDay").toMap().value("reference").toMap())}};
    return QCryptographicHash::hash(QJsonDocument(key).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256);
}


QVariantMap AnalysisController::outingLapConsistency() const
{
    QVariantMap result{{"algorithm", QString::fromLatin1(consistencyAlgorithm)},
        {"minimumSamples", minimumConsistencySamples}};
    if (m_outingComparisonGroupId.isEmpty() || outingLapsLoading()) return result;
    QVector<const OutingLapRow *> eligible;
    try {
        eligible = eligibleOutingLaps(m_outingRawLapRows, m_outingComparisonGroupId, m_outingRunConfigurations,
            m_document.analysisProject().value("event").toObject().value("lapExclusions").toArray(), m_outingStaleRunIds);
    } catch (const std::exception &) {
        return result;
    }
    QVector<double> day;
    QStringList runOrder;
    QHash<QString, QVector<double>> byRun;
    QHash<QString, QString> runNames;
    for (const auto *row : eligible) {
        day.append(row->end - row->start);
        if (!byRun.contains(row->runId)) runOrder.append(row->runId);
        byRun[row->runId].append(row->end - row->start);
        runNames.insert(row->runId, row->runName);
    }
    result.insert("day", consistencySummaryMap(summarizeConsistency(day)));
    QVariantList runs;
    for (const auto &runId : runOrder)
        runs.append(QVariantMap{{"runId", runId}, {"runName", runNames.value(runId)},
            {"laps", consistencySummaryMap(summarizeConsistency(byRun.value(runId)))}});
    result.insert("runs", runs);
    return result;
}


QVariantMap AnalysisController::outingSectorProgression() const
{
    return publishSectorProgression(m_theoreticalBest, m_theoreticalBestState, m_theoreticalBestMessage, m_outingProgression,
        [this](const QJsonObject &reference) { return outingLapLabel(reference); });
}

QVariantMap AnalysisController::outingTheoreticalBest() const
{
    return publishTheoreticalBest(m_theoreticalBest, m_theoreticalBestState, m_theoreticalBestMessage,
        [this](const QJsonObject &reference) { return outingLapLabel(reference); });
}

QVariantMap AnalysisController::outingTimeLossRanking() const
{
    return publishTimeLossRanking(m_theoreticalBest, m_theoreticalBestState, m_theoreticalBestMessage, m_timeLossAllLaps,
        [this](const QJsonObject &reference) { return outingLapLabel(reference); });
}

TimeLossRanking AnalysisController::computeTimeLossRanking(const bool allLaps, const qsizetype maximumResults) const
{
    return rankOutingTimeLosses(m_theoreticalBest, allLaps, maximumResults);
}

QString AnalysisController::outingLapLabel(const QJsonObject &reference) const
{
    const auto resolved = resolveOutingLapReference(reference.toVariantMap());
    if (resolved.value("state") != "resolved") return {};
    const auto row = m_outingLapRows[resolved.value("index").toInt()].toMap();
    return QStringLiteral("%1 · LAP %2").arg(row.value("runName").toString()).arg(row.value("lapNumber").toInt());
}




void AnalysisController::setOutingTimeLossAllLaps(const bool allLaps)
{
    if (m_timeLossAllLaps == allLaps) return;
    m_timeLossAllLaps = allLaps;
    emit outingTheoreticalBestChanged();
}

bool AnalysisController::openTheoreticalBestSector(const QString &segmentId)
{
    if (m_theoreticalBestState != "ready" || !m_theoreticalBest.actualBest) return false;
    const auto sector = std::find_if(m_theoreticalBest.best.sectors.cbegin(), m_theoreticalBest.best.sectors.cend(),
        [&segmentId](const TheoreticalBestSector &candidate) { return candidate.segmentId == segmentId; });
    if (sector == m_theoreticalBest.best.sectors.cend() || !sector->seconds) return false;
    // Donor lap as A against the group's actual best as B. When the donor is
    // the actual best, comparing it with itself shows nothing: B is then the
    // next-fastest lap through this sector (KAN-117).
    auto against = m_theoreticalBest.actualBest->lapReference;
    if (against == sector->sourceLapReference) {
        std::optional<double> nextBest;
        QJsonObject nextLap;
        for (const auto &lap : m_theoreticalBest.population) {
            if (lap.times.lapReference == sector->sourceLapReference) continue;
            for (const auto &candidate : lap.times.sectors) {
                if (candidate.segmentId != segmentId || !candidate.seconds) continue;
                if (!nextBest || *candidate.seconds < *nextBest) { nextBest = candidate.seconds; nextLap = lap.times.lapReference; }
            }
        }
        if (nextLap.isEmpty()) return false;
        against = nextLap;
    }
    return openComparisonEvidence(sector->sourceLapReference.toVariantMap(), against.toVariantMap(), segmentId);
}

bool AnalysisController::openTimeLoss(const QVariantMap &loss)
{
    if (m_theoreticalBestState != "ready" || !m_theoreticalBest.actualBest) return false;
    const auto segmentId = loss.value("segmentId").toString();
    const auto known = std::any_of(m_theoreticalBest.best.sectors.cbegin(), m_theoreticalBest.best.sectors.cend(),
        [&segmentId](const TheoreticalBestSector &sector) { return sector.segmentId == segmentId; });
    if (!known) return false;
    return openComparisonEvidence(loss.value("lapReference").toMap(),
        m_theoreticalBest.actualBest->lapReference.toVariantMap(), segmentId);
}

bool AnalysisController::openComparisonEvidence(const QVariantMap &lapA, const QVariantMap &lapB, const QString &segmentId)
{
    closeOutingLap();
    if (!selectComparisonLap(0, lapA) || !selectComparisonLap(1, lapB)) return false;
    // Measured against the same canonical segments as the result that led here.
    m_comparisonSegmentationRunId = m_theoreticalBest.canonicalRunId;
    if (m_comparisonFocusSegmentId != segmentId) {
        m_comparisonFocusSegmentId = segmentId;
        emit comparisonFocusSegmentIdChanged();
    }
    setComparisonViewOpen(true);
    return true;
}

void AnalysisController::clearComparisonFocusSegment()
{
    if (m_comparisonFocusSegmentId.isEmpty()) return;
    m_comparisonFocusSegmentId.clear();
    emit comparisonFocusSegmentIdChanged();
}

void AnalysisController::requestOutingTheoreticalBest()
{
    if (m_theoreticalBestState == "loading") return;
    ++m_theoreticalBestRequest;
    if (m_theoreticalBestCancellation) m_theoreticalBestCancellation->store(true);
    m_theoreticalBest.best = {};
    if (m_outingComparisonGroupId.isEmpty()) {
        m_theoreticalBestState = QStringLiteral("unavailable");
        m_theoreticalBestMessage = QStringLiteral("Confirm a compatible track configuration before calculating a theoretical best.");
        emit outingTheoreticalBestChanged();
        return;
    }
    QVector<OutingLapRow> population;
    try {
        const auto eligible = eligibleOutingLaps(m_outingRawLapRows, m_outingComparisonGroupId,
            m_outingRunConfigurations, m_document.analysisProject().value("event").toObject().value("lapExclusions").toArray(),
            m_outingStaleRunIds);
        population.reserve(eligible.size());
        for (const auto *row : eligible) population.append(*row);
    } catch (const std::exception &error) {
        m_theoreticalBestState = QStringLiteral("error");
        m_theoreticalBestMessage = QString::fromUtf8(error.what());
        emit outingTheoreticalBestChanged();
        return;
    }
    if (population.isEmpty()) {
        m_theoreticalBestState = QStringLiteral("unavailable");
        m_theoreticalBestMessage = QStringLiteral("No eligible laps in this group to calculate a theoretical best from.");
        emit outingTheoreticalBestChanged();
        return;
    }
    // Canonical run: the first eligible run (by id, for determinism) whose own
    // approved segmentation is non-empty for this configuration.
    QStringList runOrder;
    for (const auto &row : population) if (!runOrder.contains(row.runId)) runOrder.append(row.runId);
    std::sort(runOrder.begin(), runOrder.end());
    const auto runs = m_document.analysisProject().value("event").toObject().value("runs").toArray();
    QString canonicalRunId;
    ApprovedSegmentation approved;
    for (const auto &runId : runOrder) {
        QJsonObject run;
        for (const auto &value : runs) if (value.toObject().value("id").toString() == runId) run = value.toObject();
        auto candidate = approvedSegmentation(run.value("trackSegments"), m_outingComparisonGroupId);
        if (candidate.valid && !candidate.revision.isEmpty() && !candidate.segments.isEmpty()) {
            canonicalRunId = runId;
            approved = candidate;
            break;
        }
    }
    if (canonicalRunId.isEmpty()) {
        m_theoreticalBestState = QStringLiteral("unavailable");
        m_theoreticalBestMessage = QStringLiteral(
            "No run in this group has an approved segment review yet. Approve segments for at least one run first.");
        emit outingTheoreticalBestChanged();
        return;
    }
    QHash<QString, QJsonObject> sourcesByRunId;
    for (const auto &value : outingLapSources())
        sourcesByRunId.insert(value.toObject().value("runId").toString(), value.toObject());

    m_theoreticalBestKey = theoreticalBestInputKey();
    m_theoreticalBestCancellation = std::make_shared<std::atomic_bool>(false);
    m_theoreticalBestState = QStringLiteral("loading");
    m_theoreticalBestMessage.clear();
    emit outingTheoreticalBestChanged();
    const auto actualBestReference = QJsonObject::fromVariantMap(
        m_outingRanking.value("bestOfDay").toMap().value("reference").toMap());
    m_theoreticalBestWatcher.setFuture(QtConcurrent::run(
        [population, sourcesByRunId, projectPath = m_document.documentProjectPath(), approved, canonicalRunId,
            actualBestReference, request = m_theoreticalBestRequest, cancellation = m_theoreticalBestCancellation] {
            return computeOutingTheoreticalBest(population, sourcesByRunId, projectPath, approved, canonicalRunId,
                actualBestReference, request, cancellation);
        }));
}

AnalysisController::TheoreticalBestResult AnalysisController::computeOutingTheoreticalBest(QVector<OutingLapRow> population,
    const QHash<QString, QJsonObject> sourcesByRunId, const QString projectPath, const ApprovedSegmentation approved,
    const QString canonicalRunId, const QJsonObject actualBestReference, const quint64 request,
    const std::shared_ptr<std::atomic_bool> &cancellation)
{
    TheoreticalBestResult result;
    static_cast<OutingTheoreticalBest &>(result) = calculateOutingTheoreticalBest(std::move(population), sourcesByRunId,
        projectPath, approved, canonicalRunId, actualBestReference, request, cancellation);
    result.request = request;
    return result;
}
