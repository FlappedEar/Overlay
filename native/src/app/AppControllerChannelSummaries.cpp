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
        ++m_channelSummariesRequest;
        if (m_channelSummariesCancellation) m_channelSummariesCancellation->store(true);
        m_channelSummariesState = QStringLiteral("idle");
        m_channelSummariesMessage.clear();
        m_channelSummariesRuns.clear();
        emit outingChannelSummariesChanged();
    });
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
            const auto detail = readOutingLapDetail(sourcesByRunId.value(runId), projectPath,
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
                const double start = session.channels.value(name).timestamps.isEmpty() ? 0.0 : session.channels.value(name).timestamps.first();
                channels.append(QVariantMap{{"channel", name}, {"unit", session.channels.value(name).unit},
                    {"run", summaryMap(summarizeChannel(session, name, start, session.duration + start, policy))},
                    {"sections", sections}});
            }
            run.insert("channels", channels);
            result.runs.append(run);
        }
    } catch (const OperationCancelled &) {
        result.error = QStringLiteral("Channel summaries were cancelled.");
    } catch (const std::exception &error) {
        result.error = QString::fromUtf8(error.what());
    }
    return result;
}
