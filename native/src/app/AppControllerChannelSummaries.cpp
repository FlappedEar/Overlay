// KAN-67: summaries of recorded channels (temperatures) per run and per
// recorded section. Independent of segmentation and of the comparison group:
// vehicle health applies to every run. One background worker decodes each
// run's recording once.

#include "app/AppController.h"
#include "telemetry/ChannelSummary.h"
#include "telemetry/OutingLaps.h"

#include <QtConcurrent/QtConcurrentRun>

using namespace FlappedEar;

namespace {
QVariantMap summaryMap(const ChannelSummary &summary)
{
    QVariantMap map{{"valid", summary.valid}, {"sampleCount", summary.sampleCount},
        {"excludedArtifacts", summary.excludedArtifacts}, {"coverage", summary.coverage},
        {"coveredSeconds", summary.coveredSeconds}, {"startTime", summary.startTime}, {"endTime", summary.endTime}};
    if (!summary.valid) { map.insert("unavailableReason", summary.unavailableReason); return map; }
    map.insert("minimum", *summary.minimum); map.insert("maximum", *summary.maximum); map.insert("mean", *summary.mean);
    map.insert("minimumTime", *summary.minimumTime); map.insert("maximumTime", *summary.maximumTime);
    return map;
}

// KAN-68: a bounded trend of one run's channel for the day view. Each bin is
// summarized on its own, so a bin inside a recording gap is null (never
// interpolated) and the drawn curve breaks there.
QVariantList trendTrace(const TelemetrySession &session, const QString &name, const double start, const double end,
    const ChannelSummaryPolicy &policy, const int bins = 120)
{
    QVariantList trace;
    if (end <= start) return trace;
    const double width = (end - start) / bins;
    for (int bin = 0; bin < bins; ++bin) {
        const double from = start + bin * width;
        const auto summary = summarizeChannel(session, name, from, from + width, policy);
        trace.append(summary.valid ? QVariant(QVariantList{from + width / 2, *summary.mean}) : QVariant());
    }
    return trace;
}

QVariantList coolingList(const TelemetrySession &session, const QString &name, const QVector<OutingLapRow> &runRows,
    const ChannelSummaryPolicy &policy)
{
    QVariantList list;
    for (const auto &interval : findCoolingIntervals(session, name, policy)) {
        QVariantMap map{{"startTime", interval.startTime}, {"endTime", interval.endTime},
            {"startValue", interval.startValue}, {"endValue", interval.endValue},
            {"drop", interval.drop()}, {"seconds", interval.seconds()}};
        // The recorded section the cooling started in (a cool-down lap, the pit lane).
        for (const auto &row : runRows)
            if (interval.startTime >= row.start && interval.startTime < row.end) {
                map.insert("type", lapSectionName(row.type));
                map.insert("lapNumber", row.lapNumber);
                break;
            }
        list.append(map);
    }
    return list;
}
}

void AppController::initializeOutingChannelSummaries()
{
    connect(&m_channelSummariesWatcher, &QFutureWatcher<ChannelSummariesResult>::finished, this, [this] {
        auto result = m_channelSummariesWatcher.future().takeResult();
        if (result.request != m_channelSummariesRequest) return;
        m_channelSummariesState = result.error.isEmpty() ? QStringLiteral("ready") : QStringLiteral("error");
        m_channelSummariesMessage = result.error;
        m_channelSummariesRuns = std::move(result.runs);
        emit outingChannelSummariesChanged();
    });
    connect(this, &AppController::outingLapsChanged, this, [this] {
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

QByteArray AppController::channelSummariesInputKey() const
{
    // Each run's recording identity, independent of where the project is saved.
    QByteArray key;
    for (const auto &value : outingLapSources()) {
        const auto runId = value.toObject().value("runId").toString();
        key += runId.toUtf8() + '\0' + outingRunKey(runId) + '\n';
    }
    return key;
}

QVariantMap AppController::outingChannelSummaries() const
{
    return {{"state", m_channelSummariesState}, {"message", m_channelSummariesMessage},
        {"algorithm", QString::fromLatin1(channelSummaryAlgorithm)}, {"runs", m_channelSummariesRuns}};
}

void AppController::requestOutingChannelSummaries()
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
        [rows = m_outingRawLapRows, sourcesByRunId, projectPath = m_documentState.projectPath(),
            request = m_channelSummariesRequest, cancellation = m_channelSummariesCancellation] {
            return computeOutingChannelSummaries(rows, sourcesByRunId, projectPath, request, cancellation);
        }));
}

AppController::ChannelSummariesResult AppController::computeOutingChannelSummaries(QVector<OutingLapRow> rows,
    const QHash<QString, QJsonObject> sourcesByRunId, const QString projectPath, const quint64 request,
    const std::shared_ptr<std::atomic_bool> &cancellation)
{
    ChannelSummariesResult result;
    result.request = request;
    const auto cancelled = [cancellation] { return cancellation->load(); };
    try {
        QStringList runOrder;
        QHash<QString, QVector<OutingLapRow>> byRun;
        for (const auto &row : rows) {
            if (!byRun.contains(row.runId)) runOrder.append(row.runId);
            byRun[row.runId].append(row);
        }
        auto cache = std::make_shared<TelemetrySessionCache>();
        for (const auto &runId : runOrder) {
            throwIfCancelled(cancelled);
            const auto &runRows = byRun[runId];
            QVariantMap run{{"runId", runId}, {"runName", runRows.first().runName}};
            // Any section of the run loads (and verifies) the whole recording.
            const auto &first = runRows.first();
            const auto detail = FlappedEar::loadOutingLapDetail(sourcesByRunId.value(runId), projectPath,
                QVariantMap{{"startTime", first.start}, {"endTime", first.end}, {"lapNumber", first.lapNumber},
                    {"reference", first.reference.toVariantMap()}},
                request, cancellation, cache);
            if (!detail.session) {
                run.insert("unavailableReason", detail.error.isEmpty() ? QStringLiteral("Recording unavailable.") : detail.error);
                result.runs.append(run);
                continue;
            }
            const auto &session = *detail.session;
            QVariantList channels;
            for (const auto &name : recordedTemperatureChannels(session)) {
                const auto policy = temperatureSummaryPolicy();
                QVariantList sections;
                for (const auto &row : runRows) {
                    auto section = summaryMap(summarizeChannel(session, name, row.start, row.end, policy));
                    section.insert("type", lapSectionName(row.type));
                    section.insert("lapNumber", row.lapNumber);
                    section.insert("reference", row.reference.toVariantMap());
                    sections.append(section);
                }
                const auto &times = session.channels.value(name).timestamps;
                const double start = times.isEmpty() ? 0.0 : times.first();
                const double end = times.isEmpty() ? 0.0 : times.last();
                channels.append(QVariantMap{{"channel", name}, {"unit", session.channels.value(name).unit},
                    {"run", summaryMap(summarizeChannel(session, name, start, session.duration + start, policy))},
                    {"sections", sections}, {"trace", trendTrace(session, name, start, end, policy)},
                    {"cooling", coolingList(session, name, runRows, policy)}});
            }
            run.insert("channels", channels);
            // KAN-69: heart rate from the recording's own heart-rate channel
            // (the imported VBO/RCZ; never a separate source).
            const auto heartRate = session.aliases.value("heartRate");
            if (!heartRate.isEmpty() && session.channels.contains(heartRate)) {
                const auto policy = heartRateSummaryPolicy();
                QVariantList sections;
                for (const auto &row : runRows) {
                    auto section = summaryMap(summarizeChannel(session, heartRate, row.start, row.end, policy));
                    section.insert("type", lapSectionName(row.type));
                    section.insert("lapNumber", row.lapNumber);
                    section.insert("reference", row.reference.toVariantMap());
                    sections.append(section);
                }
                const auto &times = session.channels.value(heartRate).timestamps;
                const double start = times.isEmpty() ? 0.0 : times.first();
                const double end = times.isEmpty() ? 0.0 : times.last();
                run.insert("heartRate", QVariantMap{{"channel", heartRate}, {"unit", session.channels.value(heartRate).unit},
                    {"run", summaryMap(summarizeChannel(session, heartRate, start, session.duration + start, policy))},
                    {"sections", sections}, {"trace", trendTrace(session, heartRate, start, end, policy)}});
            }
            result.runs.append(run);
        }
    } catch (const OperationCancelled &) {
        result.error = QStringLiteral("Channel summaries were cancelled.");
    } catch (const std::exception &error) {
        result.error = QString::fromUtf8(error.what());
    }
    return result;
}

QVariantMap AppController::comparisonHeartRate(const double startMeters, const double endMeters) const
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
        auto map = summaryMap(combineChannelSummaries(parts));
        map.insert("channel", comparisonSlot.session->aliases.value("heartRate"));
        laps.append(map);
    }
    return {{"valid", true}, {"algorithm", QString::fromLatin1(channelSummaryAlgorithm)}, {"laps", laps},
        {"startMeters", from}, {"endMeters", to}, {"crossesStartFinish", from > to}};
}
