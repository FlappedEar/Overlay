// KAN-71/73: the day report assembled from computed results (KAN-124: moved
// from AppController into telemetry core so both apps build the same report).

#include "telemetry/OutingDayReport.h"

#include "telemetry/ChannelSummary.h"
#include "telemetry/OutingLaps.h"
#include "telemetry/TheoreticalBest.h"
#include "telemetry/TimeLoss.h"

namespace FlappedEar {

namespace {
DayResultStatus statusFromState(const QString &state)
{
    if (state == "ready") return DayResultStatus::Available;
    if (state == "idle") return DayResultStatus::NotComputed;
    if (state == "loading") return DayResultStatus::Computing;
    return DayResultStatus::Unavailable;
}

QJsonObject lapEvidence(const QVariant &reference, const QString &label)
{
    return {{"kind", "lap"}, {"reference", QJsonObject::fromVariantMap(reference.toMap())}, {"label", label}};
}

QString reasonFor(const QVariantMap &result, const QString &fallback)
{
    const auto message = result.value("message").toString();
    return message.isEmpty() ? fallback : message;
}
}

QJsonObject buildOutingDayReport(const OutingDayReportSources &sources)
{
    DayReportInput input;
    input.eventId = sources.eventId;
    input.groupId = sources.groupId;
    const auto &ranking = sources.ranking;
    input.groupLabel = ranking.value("groupLabel").toString();
    input.decisionsKey = sources.decisionsKey;
    const QJsonObject dayRange{{"scope", "day"}, {"groupId", sources.groupId}};

    // Best lap of the day (KAN-23), from the eligibility-filtered ranking.
    {
        DayReportResult result;
        result.id = "bestLap";
        result.algorithm = QString::fromLatin1(lapRankingAlgorithm);
        result.revision = QString::fromLatin1(lapReferenceAlgorithm);
        result.decisionsKey = input.decisionsKey; // computed synchronously from the current decisions
        result.range = dayRange;
        result.range.insert("lapCount", ranking.value("lapCount").toInt());
        result.range.insert("eligibleLapCount", ranking.value("eligibleLapCount").toInt());
        const auto state = ranking.value("state").toString();
        const auto best = ranking.value("bestOfDay").toMap();
        if (state == "loading") {
            result.status = DayResultStatus::Computing;
        } else if (state == "available" && !best.isEmpty()) {
            const auto label = QStringLiteral("%1 · LAP %2").arg(best.value("runName").toString()).arg(best.value("lapNumber").toInt());
            result.status = DayResultStatus::Available;
            result.value = {{"seconds", best.value("durationSeconds").toDouble()}, {"label", label},
                {"runId", best.value("runId").toString()}, {"lapNumber", best.value("lapNumber").toInt()}};
            result.evidence.append(lapEvidence(best.value("reference"), label));
        } else {
            result.status = DayResultStatus::Unavailable;
            result.reason = state == "selection-required" ? QStringLiteral("Choose a compatibility group.")
                                                          : QStringLiteral("No eligible lap in this group.");
        }
        input.results.append(result);
    }

    // Per-session best and spread (KAN-25), from the same ranking.
    {
        const auto &progression = sources.progression;
        DayReportResult result;
        result.id = "progression";
        result.algorithm = QString::fromLatin1(lapRankingAlgorithm);
        result.decisionsKey = input.decisionsKey;
        result.range = dayRange;
        const auto runs = progression.value("runs").toList();
        result.range.insert("runCount", runs.size());
        if (progression.value("state") == "loading") {
            result.status = DayResultStatus::Computing;
        } else if (runs.isEmpty()) {
            result.status = DayResultStatus::Unavailable;
            result.reason = QStringLiteral("No session in this group.");
        } else {
            QJsonArray rows;
            for (const auto &value : runs) {
                const auto run = value.toMap();
                QJsonObject row{{"runId", run.value("runId").toString()}, {"runName", run.value("runName").toString()},
                    {"lapCount", run.value("lapCount").toInt()}, {"eligibleLapCount", run.value("eligibleLapCount").toInt()}};
                const auto best = run.value("bestLap").toMap();
                if (!best.isEmpty()) {
                    row.insert("bestSeconds", best.value("durationSeconds").toDouble());
                    row.insert("evidenceIndex", result.evidence.size());
                    result.evidence.append(lapEvidence(best.value("reference"),
                        QStringLiteral("%1 · LAP %2").arg(run.value("runName").toString()).arg(best.value("lapNumber").toInt())));
                }
                const auto distribution = run.value("distribution").toMap();
                if (!distribution.isEmpty()) row.insert("distribution", QJsonObject::fromVariantMap(distribution));
                const auto delta = run.value("bestDeltaPreviousListedSeconds");
                if (!delta.isNull() && delta.isValid()) row.insert("bestDeltaPreviousSeconds", delta.toDouble());
                rows.append(row);
            }
            result.status = DayResultStatus::Available;
            result.value = {{"runs", rows}};
        }
        input.results.append(result);
    }

    // Lap-time consistency (KAN-62): day and per session, over the eligible laps.
    {
        const auto &consistency = sources.consistency;
        DayReportResult result;
        result.id = "consistency";
        result.algorithm = consistency.value("algorithm").toString();
        result.decisionsKey = input.decisionsKey;
        result.range = dayRange;
        result.range.insert("minimumSamples", consistency.value("minimumSamples").toInt());
        if (!consistency.contains("day")) {
            result.status = sources.lapsLoading ? DayResultStatus::Computing : DayResultStatus::Unavailable;
            if (!sources.lapsLoading) result.reason = QStringLiteral("No eligible laps to summarize.");
        } else {
            result.status = DayResultStatus::Available;
            result.value = {{"day", QJsonObject::fromVariantMap(consistency.value("day").toMap())},
                {"runs", QJsonArray::fromVariantList(consistency.value("runs").toList())}};
            result.evidence = sources.eligibleLaps;
        }
        input.results.append(result);
    }

    // Theoretical best (KAN-56): each sector with the lap it came from.
    const auto &theoretical = sources.theoretical;
    const auto theoreticalStatus = statusFromState(theoretical.value("state").toString());
    {
        DayReportResult result;
        result.id = "theoreticalBest";
        result.algorithm = QString::fromLatin1(theoreticalBestAlgorithm);
        result.decisionsKey = sources.theoreticalKey;
        result.range = dayRange;
        result.status = theoreticalStatus;
        if (theoreticalStatus == DayResultStatus::Available) {
            QJsonArray sectors;
            for (const auto &value : theoretical.value("sectors").toList()) {
                const auto sector = value.toMap();
                QJsonObject row{{"segmentId", sector.value("segmentId").toString()}, {"name", sector.value("name").toString()},
                    {"type", sector.value("type").toString()}};
                if (sector.contains("seconds")) {
                    row.insert("seconds", sector.value("seconds").toDouble());
                    row.insert("sourceLapLabel", sector.value("sourceLapLabel").toString());
                    result.evidence.append(QJsonObject{{"kind", "segment"}, {"segmentId", sector.value("segmentId").toString()},
                        {"reference", QJsonObject::fromVariantMap(sector.value("sourceLapReference").toMap())},
                        {"label", sector.value("sourceLapLabel").toString()}});
                } else {
                    row.insert("unavailableReason", sector.value("unavailableReason").toString());
                }
                if (sector.contains("lossSeconds")) row.insert("lossSeconds", sector.value("lossSeconds").toDouble());
                sectors.append(row);
            }
            result.value = {{"sectors", sectors}};
            result.range.insert("sectorCount", sectors.size());
            if (theoretical.contains("totalSeconds")) result.value.insert("totalSeconds", theoretical.value("totalSeconds").toDouble());
            if (theoretical.contains("differenceSeconds"))
                result.value.insert("differenceSeconds", theoretical.value("differenceSeconds").toDouble());
            const auto actual = theoretical.value("actualBest").toMap();
            if (!actual.isEmpty()) {
                result.value.insert("actualBest", QJsonObject{{"label", actual.value("label").toString()},
                    {"seconds", actual.value("lapSeconds").toDouble()}});
                result.evidence.append(lapEvidence(actual.value("reference"), actual.value("label").toString()));
            }
        } else {
            result.reason = reasonFor(theoretical, theoreticalStatus == DayResultStatus::NotComputed
                ? QStringLiteral("Not calculated yet.") : QString());
        }
        input.results.append(result);
    }

    // Largest time losses (KAN-60) against the group's best lap.
    {
        const auto &losses = sources.timeLosses;
        DayReportResult result;
        result.id = "timeLosses";
        result.algorithm = QString::fromLatin1(timeLossAlgorithm);
        result.revision = losses.value("revision").toString();
        result.decisionsKey = sources.theoreticalKey;
        result.range = dayRange;
        result.status = statusFromState(losses.value("state").toString());
        if (result.status == DayResultStatus::Available) {
            result.range.insert("scope", losses.value("scope").toString() == "allLaps" ? "allLaps" : "runBests");
            result.range.insert("groupId", sources.groupId);
            result.range.insert("comparedLapCount", losses.value("comparedLapCount").toInt());
            result.range.insert("observationCount", losses.value("observationCount").toInt());
            const auto against = QJsonObject::fromVariantMap(losses.value("referenceLap").toMap());
            QJsonArray rows;
            const auto all = losses.value("losses").toList();
            for (qsizetype i = 0; i < std::min<qsizetype>(10, all.size()); ++i) {
                const auto loss = all[i].toMap();
                QJsonObject row{{"name", loss.value("name").toString()}, {"role", loss.value("role").toString()},
                    {"segmentId", loss.value("segmentId").toString()}, {"lossSeconds", loss.value("lossSeconds").toDouble()},
                    {"lapLabel", loss.value("lapLabel").toString()}};
                if (loss.contains("cornerName")) row.insert("cornerName", loss.value("cornerName").toString());
                rows.append(row);
                result.evidence.append(QJsonObject{{"kind", "segment"}, {"segmentId", loss.value("segmentId").toString()},
                    {"reference", QJsonObject::fromVariantMap(loss.value("lapReference").toMap())}, {"against", against},
                    {"startMeters", loss.value("startMeters").toDouble()}, {"endMeters", loss.value("endMeters").toDouble()},
                    {"label", loss.value("lapLabel").toString()}});
            }
            result.value = {{"referenceLabel", losses.value("referenceLabel").toString()}, {"losses", rows}};
        } else {
            result.reason = reasonFor(losses, result.status == DayResultStatus::NotComputed
                ? QStringLiteral("Not calculated yet.") : QString());
        }
        input.results.append(result);
    }

    // Areas to inspect next (KAN-73), selected from the computed losses of
    // each session's fastest lap, the best lap's sector gaps and the corner
    // variability. Observations and hypotheses are kept apart.
    {
        DayReportResult result;
        result.id = "focusAreas";
        result.algorithm = QString::fromLatin1(focusAreasAlgorithm);
        result.decisionsKey = sources.theoreticalKey;
        result.range = dayRange;
        result.status = theoreticalStatus;
        if (theoreticalStatus == DayResultStatus::Available && !sources.focus) {
            result.status = DayResultStatus::Unavailable;
            result.reason = QStringLiteral("The group's best lap could not be timed against the approved segments.");
        } else if (theoreticalStatus == DayResultStatus::Available) {
            const auto &focus = *sources.focus;
            QJsonArray areas;
            for (const auto &area : selectFocusAreas(focus)) {
                areas.append(QJsonObject{{"kind", area.kind}, {"segmentId", area.segmentId}, {"name", area.name},
                    {"observation", area.observation}, {"hypothesis", area.hypothesis}, {"metric", area.metric},
                    {"value", area.value}, {"unit", area.unit}, {"sampleCount", area.sampleCount},
                    {"evidenceIndex", result.evidence.size()}});
                result.evidence.append(QJsonObject{{"kind", "segment"}, {"segmentId", area.segmentId},
                    {"reference", area.lap}, {"label", sources.lapLabel(area.lap)},
                    {"against", area.against}, {"againstLabel", sources.lapLabel(area.against)}});
            }
            result.range.insert("comparedLapCount", focus.comparedLapCount);
            if (areas.isEmpty()) {
                result.status = DayResultStatus::Unavailable;
                result.reason = QStringLiteral("No loss, sector gap or spread is large enough to single out.");
            } else {
                result.value = {{"areas", areas}, {"referenceLabel", focus.referenceLabel}};
            }
        } else {
            result.reason = reasonFor(theoretical, theoreticalStatus == DayResultStatus::NotComputed
                ? QStringLiteral("Not calculated yet.") : QString());
        }
        input.results.append(result);
    }

    // Sections by session (KAN-64): typical time and spread per segment.
    {
        const auto &sections = sources.sectionProgression;
        DayReportResult result;
        result.id = "sectionProgression";
        result.algorithm = sections.value("algorithm").toString();
        result.decisionsKey = sources.theoreticalKey;
        result.range = dayRange;
        result.status = statusFromState(sections.value("state").toString());
        if (result.status == DayResultStatus::Available) {
            QJsonArray rows;
            for (const auto &value : sections.value("segments").toList()) {
                const auto segment = value.toMap();
                QJsonArray cells;
                for (const auto &cellValue : segment.value("cells").toList()) {
                    const auto cell = cellValue.toMap();
                    cells.append(QJsonObject{{"runId", cell.value("runId").toString()},
                        {"summary", QJsonObject::fromVariantMap(cell.value("summary").toMap())}});
                    result.evidence.append(QJsonObject{{"kind", "segment"}, {"segmentId", segment.value("segmentId").toString()},
                        {"runId", cell.value("runId").toString()}, {"lapCount", cell.value("laps").toList().size()}});
                }
                QJsonObject row{{"segmentId", segment.value("segmentId").toString()}, {"name", segment.value("name").toString()},
                    {"type", segment.value("type").toString()}, {"cells", cells}};
                if (segment.contains("fastestTypical")) row.insert("fastestTypicalSeconds", segment.value("fastestTypical").toDouble());
                rows.append(row);
            }
            QJsonArray sessions;
            for (const auto &value : sections.value("sessions").toList()) {
                const auto session = value.toMap();
                sessions.append(QJsonObject{{"runId", session.value("runId").toString()}, {"runName", session.value("runName").toString()}});
            }
            result.value = {{"sessions", sessions}, {"segments", rows}};
        } else {
            result.reason = reasonFor(sections, result.status == DayResultStatus::NotComputed
                ? QStringLiteral("Not calculated yet.") : QString());
        }
        input.results.append(result);
    }

    // Recorded temperatures (KAN-67/68) and heart rate (KAN-69/70), per
    // session. They do not depend on the analysis decisions; they are
    // invalidated with the run set.
    const auto &summaries = sources.channelSummaries;
    const auto summariesStatus = statusFromState(summaries.value("state").toString());
    for (const auto *id : {"temperatures", "heartRate"}) {
        const bool heart = QLatin1String(id) == QLatin1String("heartRate");
        DayReportResult result;
        result.id = QString::fromLatin1(id);
        result.algorithm = QString::fromLatin1(channelSummaryAlgorithm);
        result.range = {{"scope", "day"}, {"runCount", summaries.value("runs").toList().size()}};
        result.status = summariesStatus;
        if (summariesStatus == DayResultStatus::Available) {
            QJsonArray runs;
            for (const auto &value : summaries.value("runs").toList()) {
                const auto run = value.toMap();
                QJsonObject row{{"runId", run.value("runId").toString()}, {"runName", run.value("runName").toString()}};
                QJsonArray channels;
                const auto entries = heart ? (run.contains("heartRate") ? QVariantList{run.value("heartRate")} : QVariantList{})
                                           : run.value("channels").toList();
                for (const auto &entryValue : entries) {
                    const auto entry = entryValue.toMap();
                    QJsonObject channel{{"channel", entry.value("channel").toString()}, {"unit", entry.value("unit").toString()},
                        {"run", QJsonObject::fromVariantMap(entry.value("run").toMap())}};
                    if (!heart) {
                        double largestDrop = 0.0;
                        const auto cooling = entry.value("cooling").toList();
                        for (const auto &interval : cooling) largestDrop = std::max(largestDrop, interval.toMap().value("drop").toDouble());
                        channel.insert("coolingCount", cooling.size());
                        if (!cooling.isEmpty()) channel.insert("largestCoolingDrop", largestDrop);
                    }
                    channels.append(channel);
                    result.evidence.append(QJsonObject{{"kind", "channel"}, {"runId", run.value("runId").toString()},
                        {"channel", entry.value("channel").toString()}});
                }
                if (heart) {
                    if (!channels.isEmpty()) row.insert("heartRate", channels.first());
                } else {
                    row.insert("channels", channels);
                }
                if (run.contains("unavailableReason")) row.insert("unavailableReason", run.value("unavailableReason").toString());
                runs.append(row);
            }
            if (result.evidence.isEmpty()) {
                result.status = DayResultStatus::Unavailable;
                result.reason = heart ? QStringLiteral("No heart rate recorded.") : QStringLiteral("No temperature recorded.");
            } else {
                result.value = {{"runs", runs}};
            }
        } else {
            result.reason = reasonFor(summaries, summariesStatus == DayResultStatus::NotComputed
                ? QStringLiteral("Not calculated yet.") : QString());
        }
        input.results.append(result);
    }

    return buildDayReport(input);
}

} // namespace FlappedEar
