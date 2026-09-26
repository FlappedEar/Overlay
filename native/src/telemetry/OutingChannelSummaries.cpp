// KAN-67/68/69: recorded channel summaries of every run and recorded
// section (KAN-124: moved from AppController into telemetry core so both apps
// share the worker).

#include "telemetry/OutingChannelSummaries.h"

#include "telemetry/OutingLapLoader.h"
#include "telemetry/SourceOperation.h"

namespace FlappedEar {

QVariantMap channelSummaryMap(const ChannelSummary &summary)
{
    QVariantMap map{{"valid", summary.valid}, {"sampleCount", summary.sampleCount},
        {"excludedArtifacts", summary.excludedArtifacts}, {"coverage", summary.coverage},
        {"coveredSeconds", summary.coveredSeconds}, {"startTime", summary.startTime}, {"endTime", summary.endTime}};
    if (!summary.valid) { map.insert("unavailableReason", summary.unavailableReason); return map; }
    map.insert("minimum", *summary.minimum); map.insert("maximum", *summary.maximum); map.insert("mean", *summary.mean);
    map.insert("minimumTime", *summary.minimumTime); map.insert("maximumTime", *summary.maximumTime);
    return map;
}

namespace {

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
} // namespace

OutingChannelSummariesResult summarizeOutingChannels(const QVector<OutingLapRow> &rows,
    const QHash<QString, QJsonObject> &sourcesByRunId, const QString &projectPath, const quint64 request,
    const std::shared_ptr<std::atomic_bool> &cancellation)
{
    OutingChannelSummariesResult result;
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
            const auto detail = loadOutingLapDetail(sourcesByRunId.value(runId), projectPath,
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
                    auto section = channelSummaryMap(summarizeChannel(session, name, row.start, row.end, policy));
                    section.insert("type", lapSectionName(row.type));
                    section.insert("lapNumber", row.lapNumber);
                    section.insert("reference", row.reference.toVariantMap());
                    sections.append(section);
                }
                const auto &times = session.channels.value(name).timestamps;
                const double start = times.isEmpty() ? 0.0 : times.first();
                const double end = times.isEmpty() ? 0.0 : times.last();
                channels.append(QVariantMap{{"channel", name}, {"unit", session.channels.value(name).unit},
                    {"run", channelSummaryMap(summarizeChannel(session, name, start, session.duration + start, policy))},
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
                    auto section = channelSummaryMap(summarizeChannel(session, heartRate, row.start, row.end, policy));
                    section.insert("type", lapSectionName(row.type));
                    section.insert("lapNumber", row.lapNumber);
                    section.insert("reference", row.reference.toVariantMap());
                    sections.append(section);
                }
                const auto &times = session.channels.value(heartRate).timestamps;
                const double start = times.isEmpty() ? 0.0 : times.first();
                const double end = times.isEmpty() ? 0.0 : times.last();
                run.insert("heartRate", QVariantMap{{"channel", heartRate}, {"unit", session.channels.value(heartRate).unit},
                    {"run", channelSummaryMap(summarizeChannel(session, heartRate, start, session.duration + start, policy))},
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

} // namespace FlappedEar
