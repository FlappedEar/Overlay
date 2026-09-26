// KAN-56/57/60/62/63/64/120: the theoretical-best family in its published
// form (KAN-124: moved from AppController into telemetry core so both apps
// present the same results).

#include "telemetry/OutingTheoreticalBestResults.h"

#include "telemetry/TelemetryGeometry.h"

#include <QHash>
#include <algorithm>

namespace FlappedEar {

QString theoreticalBestReasonText(const QString &reason)
{
    if (reason == theoreticalBestNoApprovedSegmentation)
        return QStringLiteral("No approved segments to measure sectors against.");
    if (reason == theoreticalBestIncompleteCoverage)
        return QStringLiteral("At least one sector has no fully covered time on any eligible lap, so no total is shown.");
    return reason;
}

QVariantMap consistencySummaryMap(const ConsistencySummary &summary)
{
    QVariantMap map{{"count", summary.count}, {"available", summary.available}};
    if (!summary.available) { map.insert("unavailableReason", summary.unavailableReason); return map; }
    map.insert("minimum", *summary.minimum); map.insert("q1", *summary.q1); map.insert("median", *summary.median);
    map.insert("q3", *summary.q3); map.insert("maximum", *summary.maximum);
    map.insert("interquartileRange", *summary.interquartileRange);
    return map;
}

TimeLossRanking rankOutingTimeLosses(const OutingTheoreticalBest &computed, const bool allLaps, const qsizetype maximumResults)
{
    // Requires a ready theoretical best with a timed actual best.
    const TimedLapSectors reference{*computed.actualBest,
        computed.actualBest->lapReference.value("startTime").toDouble()};
    QVector<TimedLapSectors> compared;
    if (allLaps) {
        compared = computed.population;
    } else {
        // Each session's fastest lap, so warm-up laps do not dominate.
        QHash<QString, qsizetype> fastestByRun;
        for (qsizetype i = 0; i < computed.population.size(); ++i) {
            const auto &lap = computed.population[i];
            const auto runId = lap.times.lapReference.value("runId").toString();
            const auto it = fastestByRun.constFind(runId);
            if (it == fastestByRun.cend() || lap.times.lapSeconds < computed.population[*it].times.lapSeconds)
                fastestByRun.insert(runId, i);
        }
        for (const auto index : fastestByRun) compared.append(computed.population[index]);
    }
    return rankTimeLosses(computed.approved, computed.axisLengthMeters, compared, reference, maximumResults);
}

QVariantMap publishTheoreticalBest(const OutingTheoreticalBest &computed, const QString &state, const QString &message,
    const LapLabel &lapLabel)
{
    QVariantMap result{{"state", state}, {"message", message}};
    if (state != "ready") return result;
    result.insert("algorithm", QString::fromLatin1(theoreticalBestAlgorithm));
    const auto *actual = computed.actualBest ? &*computed.actualBest : nullptr;
    QVariantList sectors;
    double actualSum = 0.0;
    bool actualComplete = actual != nullptr;
    for (const auto &sector : computed.best.sectors) {
        QVariantMap row{{"segmentId", sector.segmentId}, {"name", sector.name}, {"type", sector.type}};
        if (sector.seconds) {
            row.insert("seconds", *sector.seconds);
            row.insert("sourceLapLabel", lapLabel(sector.sourceLapReference));
            row.insert("sourceLapReference", sector.sourceLapReference.toVariantMap());
        } else {
            row.insert("unavailableReason", theoreticalBestReasonText(sector.unavailableReason));
        }
        std::optional<double> actualSeconds;
        if (actual) {
            for (const auto &candidate : actual->sectors)
                if (candidate.segmentId == sector.segmentId) actualSeconds = candidate.seconds;
        }
        if (actualSeconds) {
            row.insert("actualSeconds", *actualSeconds);
            actualSum += *actualSeconds;
            if (sector.seconds) row.insert("lossSeconds", *actualSeconds - *sector.seconds);
        } else {
            actualComplete = false;
        }
        sectors.append(row);
    }
    // KAN-62: how repeatable each segment is across the same population.
    const auto consistency = computeSectorConsistency(computed.approved, computed.population);
    for (auto &value : sectors) {
        auto row = value.toMap();
        for (const auto &sector : consistency)
            if (sector.segmentId == row.value("segmentId").toString()) row.insert("consistency", consistencySummaryMap(sector.summary));
        value = row;
    }
    // KAN-63: braking, apex, exit and line variability for each corner.
    for (auto &value : sectors) {
        auto row = value.toMap();
        const auto id = row.value("segmentId").toString();
        if (!computed.cornerObservations.contains(id)) continue;
        const auto variability = summarizeCornerVariability(id, row.value("name").toString(),
            computed.cornerObservations.value(id));
        QVariantMap map{{"brakingPointMeasured", consistencySummaryMap(variability.brakingPointMeasured)},
            {"brakingPointInferred", consistencySummaryMap(variability.brakingPointInferred)},
            {"apexSpeed", consistencySummaryMap(variability.apexSpeed)}, {"minimumSpeed", consistencySummaryMap(variability.minimumSpeed)},
            {"exitSpeed", consistencySummaryMap(variability.exitSpeed)}, {"pickupMeasured", consistencySummaryMap(variability.pickupMeasured)},
            {"pickupInferred", consistencySummaryMap(variability.pickupInferred)}, {"lineOffset", consistencySummaryMap(variability.lineOffset)},
            {"lineSpreadResolvable", variability.lineSpreadResolvable}};
        if (variability.typicalGpsAccuracyMeters) map.insert("typicalGpsAccuracyMeters", *variability.typicalGpsAccuracyMeters);
        row.insert("variability", map);
        value = row;
    }
    result.insert("variabilityAlgorithm", QString::fromLatin1(drivingVariabilityAlgorithm));
    result.insert("consistencyAlgorithm", QString::fromLatin1(consistencyAlgorithm));
    result.insert("sectors", sectors);
    if (computed.best.totalSeconds) result.insert("totalSeconds", *computed.best.totalSeconds);
    if (actual) {
        QVariantMap best{{"label", lapLabel(actual->lapReference)},
            {"reference", actual->lapReference.toVariantMap()}, {"lapSeconds", actual->lapSeconds},
            {"coversWholeLap", actual->completePartition}};
        // Over the same sectors, so a partition with gaps or a gate-crossing
        // segment still compares like with like.
        if (actualComplete) best.insert("sectorSumSeconds", actualSum);
        result.insert("actualBest", best);
        if (actualComplete && computed.best.totalSeconds)
            result.insert("differenceSeconds", actualSum - *computed.best.totalSeconds);
    }
    result.insert("revision", computed.best.stamp.revision);
    result.insert("trackConfigurationReference", computed.best.stamp.trackConfigurationReference);

    // KAN-120: where the best lap loses time, largest first ...
    auto gains = sectors;
    std::stable_sort(gains.begin(), gains.end(), [](const QVariant &left, const QVariant &right) {
        return left.toMap().value("lossSeconds", -1.0).toDouble() > right.toMap().value("lossSeconds", -1.0).toDouble();
    });
    result.insert("gains", gains);
    // ... and each segment's line on the track map (north up, fitted to a
    // unit square with the aspect ratio kept).
    const auto &axis = computed.axis;
    if (axis.valid && axis.points.size() == axis.cumulative.size() && !axis.points.isEmpty()) {
        double minX = axis.points.first().x(), maxX = minX, minY = axis.points.first().y(), maxY = minY;
        for (const auto &point : axis.points) {
            minX = std::min(minX, point.x()); maxX = std::max(maxX, point.x());
            minY = std::min(minY, point.y()); maxY = std::max(maxY, point.y());
        }
        const double span = std::max({maxX - minX, maxY - minY, 1.0});
        const double offsetX = (span - (maxX - minX)) / 2.0, offsetY = (span - (maxY - minY)) / 2.0;
        const auto normalized = [&](const QPointF &point) {
            return QVariantMap{{"x", (point.x() - minX + offsetX) / span}, {"y", (maxY - point.y() + offsetY) / span}};
        };
        const auto line = [&](const double from, const double to) {
            QVariantList points;
            for (qsizetype i = 0; i < axis.points.size(); ++i)
                if (axis.cumulative[i] >= from - 1e-6 && axis.cumulative[i] <= to + 1e-6) points.append(normalized(axis.points[i]));
            return points;
        };
        QVariantList mapSegments;
        for (const auto &value : sectors) {
            auto row = value.toMap();
            const auto id = row.value("segmentId").toString();
            double start = 0.0, end = 0.0;
            for (const auto &segmentValue : computed.approved.segments) {
                const auto segment = segmentValue.toObject();
                if (segment.value("id").toString() != id) continue;
                start = segment.value("startProgressMeters").toDouble();
                end = segment.value("endProgressMeters").toDouble();
            }
            QVariantList parts;
            if (end >= start) parts.append(QVariant(line(start, end)));
            else { parts.append(QVariant(line(start, axis.lengthMeters))); parts.append(QVariant(line(0.0, end))); }
            row.insert("parts", parts);
            mapSegments.append(row);
        }
        QVariantList outline;
        for (qsizetype i = 0; i < axis.points.size(); i += std::max<qsizetype>(1, axis.points.size() / 800))
            outline.append(normalized(axis.points[i]));
        result.insert("map", QVariantMap{{"segments", mapSegments}, {"outline", outline}});
    }
    return result;
}

QVariantMap publishTimeLossRanking(const OutingTheoreticalBest &computed, const QString &state, const QString &message,
    const bool allLaps, const LapLabel &lapLabel)
{
    QVariantMap result{{"state", state}, {"message", message}};
    if (state != "ready") return result;
    if (!computed.actualBest) {
        result.insert("state", QStringLiteral("unavailable"));
        result.insert("message", QStringLiteral("The group's best lap could not be timed against the approved segments."));
        return result;
    }
    const auto ranking = rankOutingTimeLosses(computed, allLaps, 50);
    if (!ranking.valid) {
        result.insert("state", QStringLiteral("unavailable"));
        result.insert("message", ranking.unavailableReason);
        return result;
    }
    QVariantList losses;
    for (const auto &loss : ranking.losses) {
        const auto &window = loss.window;
        QVariantMap row{{"lossSeconds", loss.lossSeconds}, {"segmentId", window.segmentId}, {"name", window.name},
            {"type", window.type}, {"role", window.role}, {"startMeters", window.startProgressMeters},
            {"endMeters", window.endProgressMeters}, {"lapReference", loss.lapReference.toVariantMap()},
            {"lapLabel", lapLabel(loss.lapReference)}, {"coverageLap", loss.coverageLap},
            {"coverageReference", loss.coverageReference}};
        if (!window.cornerSegmentId.isEmpty()) {
            row.insert("cornerSegmentId", window.cornerSegmentId);
            for (const auto &candidate : computed.best.sectors)
                if (candidate.segmentId == window.cornerSegmentId) row.insert("cornerName", candidate.name);
        }
        if (window.cumulativeAtStartSeconds) row.insert("cumulativeAtStartSeconds", *window.cumulativeAtStartSeconds);
        if (window.cumulativeAtEndSeconds) row.insert("cumulativeAtEndSeconds", *window.cumulativeAtEndSeconds);
        losses.append(row);
    }
    result.insert("losses", losses);
    result.insert("algorithm", QString::fromLatin1(timeLossAlgorithm));
    result.insert("referenceLabel", lapLabel(ranking.referenceLap));
    result.insert("referenceLap", ranking.referenceLap.toVariantMap());
    result.insert("scope", allLaps ? QStringLiteral("allLaps") : QStringLiteral("runBests"));
    result.insert("observationCount", ranking.observationCount);
    result.insert("comparedLapCount", ranking.comparedLapCount);
    result.insert("untimedWindowCount", ranking.untimedWindowCount);
    result.insert("revision", ranking.stamp.revision);
    return result;
}

QVariantMap publishSectorProgression(const OutingTheoreticalBest &computed, const QString &state, const QString &message,
    const QVariantMap &progression, const LapLabel &lapLabel)
{
    QVariantMap result{{"state", state}, {"message", message},
        {"algorithm", QString::fromLatin1(consistencyAlgorithm)}, {"minimumSamples", minimumConsistencySamples}};
    if (state != "ready") return result;
    QHash<QString, QVector<const TimedLapSectors *>> byRun;
    for (const auto &lap : computed.population) byRun[lap.times.lapReference.value("runId").toString()].append(&lap);
    // Sessions in the progression's chronological order, with their context.
    QVariantList sessions;
    QStringList order;
    for (const auto &value : progression.value("runs").toList()) {
        const auto run = value.toMap();
        const auto runId = run.value("runId").toString();
        if (!byRun.contains(runId)) continue;
        QVector<double> lapTimes;
        for (const auto *lap : byRun.value(runId)) lapTimes.append(lap->times.lapSeconds);
        order.append(runId);
        sessions.append(QVariantMap{{"runId", runId}, {"runName", run.value("runName")}, {"clock", run.value("clock")},
            {"notes", run.value("notes")}, {"conditions", run.value("conditions")}, {"setupChanges", run.value("setupChanges")},
            {"laps", consistencySummaryMap(summarizeConsistency(lapTimes))}});
    }
    QVector<QJsonObject> segments;
    for (const auto &value : computed.approved.segments) segments.append(value.toObject());
    std::stable_sort(segments.begin(), segments.end(), [](const QJsonObject &a, const QJsonObject &b) {
        return a.value("startProgressMeters").toDouble() < b.value("startProgressMeters").toDouble();
    });
    QVariantList rows;
    for (const auto &segment : segments) {
        const auto segmentId = segment.value("id").toString();
        QVariantList cells;
        std::optional<double> fastestTypical;
        for (const auto &runId : order) {
            QVector<double> times;
            QVector<std::pair<double, QJsonObject>> laps;
            for (const auto *lap : byRun.value(runId)) {
                if (lap->times.stamp.revision != computed.approved.revision) continue;
                for (const auto &sector : lap->times.sectors) {
                    if (sector.segmentId != segmentId || !sector.seconds) continue;
                    times.append(*sector.seconds);
                    laps.append({*sector.seconds, lap->times.lapReference});
                }
            }
            std::sort(laps.begin(), laps.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
            QVariantList lapRows;
            for (const auto &[seconds, reference] : laps)
                lapRows.append(QVariantMap{{"seconds", seconds}, {"reference", reference.toVariantMap()},
                    {"label", lapLabel(reference)}});
            const auto summary = summarizeConsistency(times);
            if (summary.available && (!fastestTypical || *summary.median < *fastestTypical)) fastestTypical = summary.median;
            cells.append(QVariantMap{{"runId", runId}, {"summary", consistencySummaryMap(summary)}, {"laps", lapRows}});
        }
        QVariantMap row{{"segmentId", segmentId}, {"name", segment.value("name").toString()},
            {"type", segment.value("type").toString()}, {"cells", cells}};
        if (fastestTypical) row.insert("fastestTypical", *fastestTypical);
        rows.append(row);
    }
    result.insert("sessions", sessions);
    result.insert("segments", rows);
    return result;
}

} // namespace FlappedEar
