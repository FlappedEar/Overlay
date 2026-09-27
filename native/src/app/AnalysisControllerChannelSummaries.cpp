// KAN-67: summaries of recorded channels (temperatures) per run and per
// recorded section. Independent of segmentation and of the comparison group:
// vehicle health applies to every run. One background worker decodes each
// run's recording once.

#include "app/AnalysisController.h"
#include "telemetry/ChannelSummary.h"
#include "telemetry/OutingChannelSummaries.h"
#include "telemetry/OutingLaps.h"
#include "telemetry/TemperatureAssociation.h"

#include <QtConcurrent/QtConcurrentRun>

using namespace FlappedEar;

void AnalysisController::initializeOutingChannelSummaries()
{
    connect(&m_channelSummariesWatcher, &QFutureWatcher<ChannelSummariesResult>::finished, this, [this] {
        auto result = m_channelSummariesWatcher.future().takeResult();
        if (result.request != m_channelSummariesRequest) return;
        m_channelSummariesState = result.error.isEmpty() ? QStringLiteral("ready") : QStringLiteral("error");
        m_channelSummariesMessage = result.error;
        m_channelSummariesRuns = std::move(result.runs);
        emit outingChannelSummariesChanged();
    });
    connect(this, &AnalysisController::outingLapsChanged, this, [this] {
        if (m_channelSummariesState == "idle") return;
        // Summaries depend only on the recordings. Re-deriving laps (e.g.
        // after Save As moves the project) keeps them; decide once loaded.
        if (outingLapsLoading()) return;
        if (!m_channelSummariesKey.isEmpty() && channelSummariesInputKey() == m_channelSummariesKey) return;
        ++m_channelSummariesRequest;
        if (m_channelSummariesCancellation) m_channelSummariesCancellation->store(true);
        m_channelSummariesState = QStringLiteral("idle");
        m_channelSummariesMessage.clear();
        m_channelSummariesRuns.clear();
        emit outingChannelSummariesChanged();
    });
}

QByteArray AnalysisController::channelSummariesInputKey() const
{
    // Each run's recording identity, independent of where the project is saved.
    QByteArray key;
    for (const auto &value : outingLapSources()) {
        const auto runId = value.toObject().value("runId").toString();
        key += runId.toUtf8() + '\0' + outingRunKey(runId) + '\n';
    }
    return key;
}

QVariantMap AnalysisController::outingChannelSummaries() const
{
    return {{"state", m_channelSummariesState}, {"message", m_channelSummariesMessage},
        {"algorithm", QString::fromLatin1(channelSummaryAlgorithm)}, {"runs", m_channelSummariesRuns}};
}

void AnalysisController::requestOutingChannelSummaries()
{
    if (m_channelSummariesState == "loading" || outingLapsLoading()) return;
    ++m_channelSummariesRequest;
    if (m_channelSummariesCancellation) m_channelSummariesCancellation->store(true);
    QHash<QString, QJsonObject> sourcesByRunId;
    for (const auto &value : outingLapSources())
        sourcesByRunId.insert(value.toObject().value("runId").toString(), value.toObject());
    if (m_outingRawLapRows.isEmpty() || sourcesByRunId.isEmpty()) {
        m_channelSummariesState = QStringLiteral("unavailable");
        m_channelSummariesMessage = QStringLiteral("Import the day's recordings first.");
        m_channelSummariesRuns.clear();
        emit outingChannelSummariesChanged();
        return;
    }
    m_channelSummariesCancellation = std::make_shared<std::atomic_bool>(false);
    m_channelSummariesKey = channelSummariesInputKey();
    m_channelSummariesState = QStringLiteral("loading");
    m_channelSummariesMessage.clear();
    emit outingChannelSummariesChanged();
    m_channelSummariesWatcher.setFuture(QtConcurrent::run(
        [rows = m_outingRawLapRows, sourcesByRunId, projectPath = m_document.documentProjectPath(),
            request = m_channelSummariesRequest, cancellation = m_channelSummariesCancellation] {
            return computeOutingChannelSummaries(rows, sourcesByRunId, projectPath, request, cancellation);
        }));
}

AnalysisController::ChannelSummariesResult AnalysisController::computeOutingChannelSummaries(QVector<OutingLapRow> rows,
    const QHash<QString, QJsonObject> sourcesByRunId, const QString projectPath, const quint64 request,
    const std::shared_ptr<std::atomic_bool> &cancellation)
{
    auto summary = summarizeOutingChannels(rows, sourcesByRunId, projectPath, request, cancellation);
    return {request, std::move(summary.error), std::move(summary.runs)};
}

QVariantMap AnalysisController::comparisonHeartRate(const double startMeters, const double endMeters) const
{
    if (!comparisonPairReady()) return {{"valid", false}};
    ensureComparisonProgressAxis();
    if (!m_comparisonProgressAxis.valid) return {{"valid", false}};
    const double length = m_comparisonProgressAxis.lengthMeters;
    const double from = std::clamp(startMeters, 0.0, length), to = std::clamp(endMeters, 0.0, length);
    QVariantList laps;
    for (int slot = 0; slot < 2; ++slot) {
        const auto &comparisonSlot = m_comparisonSlots[slot];
        const double lapStart = comparisonSlot.row.value("startTime").toDouble();
        const double lapEnd = comparisonSlot.row.value("endTime").toDouble();
        const auto timeAt = [&](const double meters) -> std::optional<double> {
            if (meters <= 1e-6) return lapStart;
            if (meters >= length - 1e-6) return lapEnd;
            return timeAtProgress(m_comparisonProgressTraceCache[slot], meters);
        };
        // A range with start after end crosses start/finish: the lap's end
        // and its beginning, summarized as two parts and combined.
        const QVector<std::pair<double, double>> ranges = from <= to
            ? QVector<std::pair<double, double>>{{from, to}}
            : QVector<std::pair<double, double>>{{from, length}, {0.0, to}};
        QVector<ChannelSummary> parts;
        for (const auto &[rangeStart, rangeEnd] : ranges) {
            const auto t0 = timeAt(rangeStart), t1 = timeAt(rangeEnd);
            if (!t0 || !t1 || *t1 <= *t0) { parts.clear(); break; }
            parts.append(summarizeChannel(*comparisonSlot.session, QStringLiteral("heartRate"), *t0, *t1, heartRateSummaryPolicy()));
        }
        if (parts.isEmpty()) {
            laps.append(QVariantMap{{"valid", false}, {"unavailableReason", QStringLiteral("incompleteCoverage")}});
            continue;
        }
        auto map = channelSummaryMap(combineChannelSummaries(parts));
        map.insert("channel", comparisonSlot.session->aliases.value("heartRate"));
        laps.append(map);
    }
    return {{"valid", true}, {"algorithm", QString::fromLatin1(channelSummaryAlgorithm)}, {"laps", laps},
        {"startMeters", from}, {"endMeters", to}, {"crossesStartFinish", from > to}};
}

namespace {

QVariantMap correlationMap(const RankCorrelation &correlation)
{
    QVariantMap map{{"count", correlation.count}, {"available", correlation.coefficient.has_value()}};
    if (correlation.coefficient) {
        map.insert("coefficient", *correlation.coefficient);
        map.insert("strength", associationStrength(*correlation.coefficient));
    } else {
        map.insert("unavailableReason", correlation.unavailableReason);
    }
    return map;
}

} // namespace

QVariantMap AnalysisController::outingTemperatureAssociations() const
{
    QVariantMap result{{"algorithm", QString::fromLatin1(temperatureAssociationAlgorithm)},
        {"minimumLaps", minimumAssociationSamples}, {"minimumCoverage", minimumAssociationCoverage},
        {"orderConfoundLevel", associationOrderConfoundLevel}, {"channels", QVariantList{}}};
    if (m_channelSummariesState != QLatin1String("ready") || m_outingComparisonGroupId.isEmpty() || outingLapsLoading())
        return result;
    QVector<const OutingLapRow *> eligible;
    try {
        eligible = eligibleOutingLaps(m_outingRawLapRows, m_outingComparisonGroupId, m_outingRunConfigurations,
            m_document.analysisProject().value("event").toObject().value("lapExclusions").toArray(), m_outingStaleRunIds);
    } catch (const std::exception &) {
        return result;
    }
    QSet<QByteArray> eligibleKeys;
    for (const auto *row : eligible) eligibleKeys.insert(lapReferenceKey(row->reference));
    result.insert("eligibleLaps", eligible.size());

    // Channel order: first seen across runs.
    QStringList names;
    QHash<QString, QString> units;
    for (const auto &runValue : m_channelSummariesRuns)
        for (const auto &channelValue : runValue.toMap().value("channels").toList()) {
            const auto channel = channelValue.toMap();
            const auto name = channel.value("channel").toString();
            if (!names.contains(name)) { names.append(name); units.insert(name, channel.value("unit").toString()); }
        }
    QVariantList channels;
    for (const auto &name : names) {
        QVector<AssociationObservation> lapTimes, accelerations;
        QVariantList observations;
        qsizetype lowCoverage = 0, notRecorded = 0;
        for (const auto &runValue : m_channelSummariesRuns) {
            const auto run = runValue.toMap();
            const auto laps = run.value("laps").toList();
            QVariantMap channel;
            for (const auto &channelValue : run.value("channels").toList())
                if (channelValue.toMap().value("channel") == name) channel = channelValue.toMap();
            const auto sections = channel.value("sections").toList();
            for (qsizetype index = 0; index < laps.size(); ++index) {
                const auto lap = laps[index].toMap();
                if (!eligibleKeys.contains(lapReferenceKey(QJsonObject::fromVariantMap(lap.value("reference").toMap()))))
                    continue;
                const auto section = sections.value(index).toMap();
                if (section.isEmpty() || !section.value("valid").toBool()) { ++notRecorded; continue; }
                if (section.value("coverage").toDouble() < minimumAssociationCoverage) { ++lowCoverage; continue; }
                const double temperature = section.value("mean").toDouble();
                const double start = lap.value("startTime").toDouble(), end = lap.value("endTime").toDouble();
                const double lapTime = end - start;
                // Order through the day: runs are in recording order and laps in
                // run order (each run's clock only orders its own laps).
                const auto order = static_cast<double>(observations.size());
                lapTimes.append({temperature, lapTime, order});
                QVariantMap observation{{"runName", run.value("runName")}, {"lapNumber", lap.value("lapNumber")},
                    {"temperature", temperature}, {"lapTime", lapTime}, {"coverage", section.value("coverage")},
                    {"reference", lap.value("reference")}};
                if (lap.contains("strongAccelerationG")) {
                    const double acceleration = lap.value("strongAccelerationG").toDouble();
                    accelerations.append({temperature, acceleration, order});
                    observation.insert("strongAccelerationG", acceleration);
                }
                observations.append(observation);
            }
        }
        const auto withLapTime = associateTemperature(lapTimes);
        const auto withAcceleration = associateTemperature(accelerations);
        channels.append(QVariantMap{{"channel", name}, {"unit", units.value(name)},
            {"lapTime", correlationMap(withLapTime.withValue)},
            {"acceleration", correlationMap(withAcceleration.withValue)},
            {"order", correlationMap(withLapTime.withOrder)},
            {"confoundedByOrder", withLapTime.confoundedByOrder},
            {"lowCoverageLaps", lowCoverage}, {"notRecordedLaps", notRecorded},
            {"observations", observations}});
    }
    result.insert("channels", channels);
    return result;
}
