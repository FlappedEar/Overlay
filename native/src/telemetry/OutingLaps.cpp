#include "telemetry/OutingLaps.h"

#include <QRegularExpression>
#include <QJsonDocument>
#include <QCryptographicHash>
#include <QSet>
#include <QMap>

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace FlappedEar {

QString lapSectionName(const LapSectionType type)
{
    switch (type) {
    case LapSectionType::Out: return "OUT";
    case LapSectionType::Lap: return "LAP";
    case LapSectionType::In: return "IN";
    case LapSectionType::Unknown: return "UNKNOWN";
    }
    return {};
}

bool validLapReference(const QJsonObject &reference)
{
    if (reference.size() != 10 || !reference.value("version").isDouble()
        || reference.value("version").toDouble() != 1.0) return false;
    for (const auto *key : {"eventId", "runId", "sourceId", "algorithm"}) {
        const auto value = reference.value(key);
        if (!value.isString() || value.toString().trimmed().isEmpty()
            || value.toString().size() > 128 || value.toString().contains(QChar::Null)) return false;
    }
    static const QRegularExpression digest("^[0-9a-f]{64}$");
    for (const auto *key : {"sourceRevision", "derivationKey"}) {
        const auto value = reference.value(key);
        if (!value.isString() || !digest.match(value.toString()).hasMatch()) return false;
    }
    const auto type = reference.value("type");
    if (!type.isString() || !QStringList{"OUT", "LAP", "IN", "UNKNOWN"}.contains(type.toString())) return false;
    const auto start = reference.value("startTime"), end = reference.value("endTime");
    return start.isDouble() && end.isDouble() && std::isfinite(start.toDouble())
        && std::isfinite(end.toDouble()) && start.toDouble() >= 0 && end.toDouble() > start.toDouble();
}

QJsonObject makeLapReference(const OutingLapRow &row, const QString &eventId,
    const QString &sourceId, const QByteArray &sourceRevision, const QByteArray &derivationKey)
{
    const QJsonObject reference{{"version", 1}, {"algorithm", lapReferenceAlgorithm},
        {"eventId", eventId}, {"runId", row.runId}, {"sourceId", sourceId},
        {"sourceRevision", QString::fromLatin1(sourceRevision)},
        {"derivationKey", QString::fromLatin1(derivationKey)}, {"type", lapSectionName(row.type)},
        {"startTime", row.start}, {"endTime", row.end}};
    return validLapReference(reference) ? reference : QJsonObject{};
}

namespace {
bool knownLayout(const QJsonObject &config)
{
    const auto value = config.value("layoutId");
    return value.isString() && !value.toString().trimmed().isEmpty()
        && value.toString().size() <= 128 && !value.toString().contains(QChar::Null);
}
bool knownDirection(const QJsonObject &config)
{
    return config.value("direction") == "clockwise" || config.value("direction") == "counterclockwise";
}
bool knownGates(const QJsonObject &config)
{
    static const QRegularExpression pattern("^gates-v1:[0-9a-f]{64}$");
    return pattern.match(config.value("gateRevision").toString()).hasMatch();
}
}

QStringList lapCompatibilityReasons(const QJsonObject &config, const QJsonObject &reference,
    const LapReferenceIssue issue, const bool userExcluded)
{
    QStringList reasons;
    if (!knownLayout(config) || (!reference.isEmpty() && !knownLayout(reference))) reasons.append("layout-unresolved");
    if (!knownDirection(config) || (!reference.isEmpty() && !knownDirection(reference))) reasons.append("direction-unresolved");
    if (!knownGates(config) || (!reference.isEmpty() && !knownGates(reference))) reasons.append("timing-gate-unresolved");
    if (!reference.isEmpty()) {
        if (knownLayout(config) && knownLayout(reference) && config.value("layoutId") != reference.value("layoutId"))
            reasons.append("changed-layout");
        if (knownDirection(config) && knownDirection(reference) && config.value("direction") != reference.value("direction"))
            reasons.append("opposite-direction");
        if (knownGates(config) && knownGates(reference) && config.value("gateRevision") != reference.value("gateRevision"))
            reasons.append("changed-timing-gate");
    }
    if (issue == LapReferenceIssue::GpsGap) reasons.append("incomplete-gps");
    if (issue == LapReferenceIssue::InvalidGps) reasons.append("invalid-gps");
    if (userExcluded) reasons.append("user-exclusion");
    return reasons;
}

QString lapCompatibilityGroupId(const QJsonObject &configuration)
{
    if (!lapCompatibilityReasons(configuration).isEmpty()) return {};
    const QJsonObject basis{{"version", 1}, {"layoutId", configuration.value("layoutId")},
        {"direction", configuration.value("direction")}, {"gateRevision", configuration.value("gateRevision")}};
    return "compatibility-v1:" + QString::fromLatin1(QCryptographicHash::hash(
        QJsonDocument(basis).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex());
}

QString lapCompatibilityReasonText(const QString &reason)
{
    if (reason == "layout-unresolved") return "Layout needs confirmation";
    if (reason == "direction-unresolved") return "Direction needs confirmation";
    if (reason == "timing-gate-unresolved") return "Timing gates unresolved";
    if (reason == "changed-layout") return "Different layout";
    if (reason == "opposite-direction") return "Opposite direction";
    if (reason == "changed-timing-gate") return "Different timing gates";
    if (reason == "incomplete-gps") return "Incomplete GPS";
    if (reason == "invalid-gps") return "Invalid GPS";
    if (reason == "user-exclusion") return "User exclusion";
    if (reason == "not-timed-lap") return "Not a complete timed lap";
    if (reason == "stale-source") return "Source changed; reload recording";
    if (reason == "ineligible-lap") return "Lap is not eligible";
    if (reason == "invalid-reference") return "Lap identity is invalid";
    if (reason == "different-recorded-route") return "Lap leaves the route the other laps took (off track, a detour or the pit lane)";
    return reason;
}

namespace {
// The compatibility/eligibility reasons one lap row has within its group
// (empty when eligible). Shared by rankOutingLaps and eligibleOutingLaps so a
// second population consumer can never drift out of sync with ranking's own
// definition of "eligible".
QStringList outingLapEligibilityReasons(const OutingLapRow &row, const QJsonObject &configuration,
    const QString &userExclusionReason, const bool staleSource)
{
    auto reasons = lapCompatibilityReasons(configuration, {}, row.referenceIssue, !userExclusionReason.isEmpty());
    if (!row.layoutIssue.isEmpty()) reasons.append(row.layoutIssue);
    if (staleSource) reasons.append("stale-source");
    if (!row.referenceEligible && reasons.isEmpty()) reasons.append("ineligible-lap");
    if (!validLapReference(row.reference) || row.reference.value("algorithm") != lapReferenceAlgorithm
        || row.reference.value("type") != "LAP" || row.reference.value("runId") != row.runId
        || row.reference.value("startTime").toDouble() != row.start || row.reference.value("endTime").toDouble() != row.end)
        reasons.append("invalid-reference");
    return reasons;
}
}

QVector<const OutingLapRow *> eligibleOutingLaps(const QVector<OutingLapRow> &rows, const QString &groupId,
    const QHash<QString, QJsonObject> &configurations, const QJsonArray &exclusions,
    const QSet<QString> &staleRunIds)
{
    QVector<const OutingLapRow *> eligible;
    if (groupId.isEmpty()) return eligible;
    if (rows.size() > maximumOutingLapRows || exclusions.size() > maximumOutingLapRows)
        throw ResourceLimitError("Too many laps or exclusions to rank this outing.");
    const auto reasonsByReference = lapExclusionReasons(exclusions);
    for (const auto &row : rows) {
        if (row.type != LapSectionType::Lap) continue;
        if (lapCompatibilityGroupId(configurations.value(row.runId)) != groupId) continue;
        const auto reason = reasonsByReference.value(lapReferenceKey(row.reference));
        if (outingLapEligibilityReasons(row, configurations.value(row.runId), reason, staleRunIds.contains(row.runId)).isEmpty())
            eligible.append(&row);
    }
    return eligible;
}

QJsonObject rankOutingLaps(const QVector<OutingLapRow> &rows, const QString &groupId,
    const QHash<QString, QJsonObject> &configurations, const QJsonArray &exclusions,
    const QSet<QString> &staleRunIds)
{
    QJsonObject result{{"groupId", groupId}, {"state", "selection-required"},
        {"bestOfDay", QJsonValue::Null}, {"runs", QJsonArray{}}, {"excludedLaps", QJsonArray{}},
        {"lapCount", 0}, {"eligibleLapCount", 0}, {"tieCount", 0}};
    if (groupId.isEmpty()) return result;
    if (rows.size() > maximumOutingLapRows || exclusions.size() > maximumOutingLapRows)
        throw ResourceLimitError("Too many laps or exclusions to rank this outing.");
    // Source order, display names and lap numbering never break a timing tie.
    const auto less = [](const OutingLapRow *a, const OutingLapRow *b) {
        const auto durationA = a->end - a->start, durationB = b->end - b->start;
        if (durationA != durationB) return durationA < durationB;
        if (a->timestampMilliseconds != b->timestampMilliseconds) {
            if (!a->timestampMilliseconds) return false;
            if (!b->timestampMilliseconds) return true;
            return *a->timestampMilliseconds < *b->timestampMilliseconds;
        }
        if (a->runId != b->runId) return a->runId < b->runId;
        if (a->start != b->start) return a->start < b->start;
        if (a->end != b->end) return a->end < b->end;
        return lapReferenceKey(a->reference) < lapReferenceKey(b->reference);
    };
    const auto record = [&groupId](const OutingLapRow &row) {
        return QJsonObject{{"runId", row.runId}, {"runName", row.runName}, {"lapNumber", row.lapNumber},
            {"reference", row.reference}, {"groupId", groupId},
            {"durationSeconds", std::isfinite(row.end - row.start) ? QJsonValue(row.end - row.start) : QJsonValue(QJsonValue::Null)}};
    };
    struct Run { const OutingLapRow *first = nullptr; QVector<const OutingLapRow *> eligible; int count = 0; };
    QMap<QString, Run> runs;
    QVector<const OutingLapRow *> eligible;
    QJsonArray rejected;
    const auto reasonsByReference = lapExclusionReasons(exclusions);
    for (const auto &row : rows) {
        if (lapCompatibilityGroupId(configurations.value(row.runId)) != groupId) continue;
        auto &run = runs[row.runId];
        if (!run.first) run.first = &row;
        if (row.type != LapSectionType::Lap) continue;
        ++run.count;
        const auto reason = reasonsByReference.value(lapReferenceKey(row.reference));
        auto reasons = outingLapEligibilityReasons(row, configurations.value(row.runId), reason, staleRunIds.contains(row.runId));
        if (reasons.isEmpty()) { eligible.append(&row); run.eligible.append(&row); }
        else {
            auto item = record(row); QStringList labels;
            for (const auto &code : reasons) labels.append(lapCompatibilityReasonText(code));
            item.insert("reasons", QJsonArray::fromStringList(reasons));
            item.insert("reasonLabels", QJsonArray::fromStringList(labels)); item.insert("userReason", reason);
            rejected.append(item);
        }
    }
    std::sort(eligible.begin(), eligible.end(), less);
    QVector<QString> runOrder;
    for (auto it = runs.begin(); it != runs.end(); ++it) {
        std::sort(it->eligible.begin(), it->eligible.end(), less);
        runOrder.append(it.key());
    }
    std::sort(runOrder.begin(), runOrder.end(), [&](const QString &a, const QString &b) {
        const auto &left = runs[a].eligible, &right = runs[b].eligible;
        if (left.isEmpty() != right.isEmpty()) return !left.isEmpty();
        return left.isEmpty() ? a < b : less(left.first(), right.first());
    });
    const auto ties = [](const QVector<const OutingLapRow *> &laps) {
        if (laps.isEmpty()) return 0;
        const double best = laps.first()->end - laps.first()->start;
        return static_cast<int>(std::count_if(laps.cbegin(), laps.cend(), [best](const auto *lap) { return lap->end - lap->start == best; }));
    };
    QJsonArray rankedRuns;
    int lapCount = 0;
    for (const auto &id : runOrder) {
        const auto &run = runs[id]; lapCount += run.count;
        QJsonValue distribution(QJsonValue::Null);
        if (!run.eligible.isEmpty()) {
            const auto quantile = [&run](const double fraction) {
                const double position = fraction * static_cast<double>(run.eligible.size() - 1);
                const auto lower = static_cast<qsizetype>(std::floor(position));
                const auto upper = static_cast<qsizetype>(std::ceil(position));
                const double a = run.eligible[lower]->end - run.eligible[lower]->start;
                const double b = run.eligible[upper]->end - run.eligible[upper]->start;
                return a + (b - a) * (position - static_cast<double>(lower));
            };
            distribution = QJsonObject{{"minimum", quantile(0)}, {"q1", quantile(.25)},
                {"median", quantile(.5)}, {"q3", quantile(.75)}, {"maximum", quantile(1)}};
        }
        rankedRuns.append(QJsonObject{{"distribution", distribution}, {"runId", id}, {"runName", run.first->runName}, {"groupId", groupId},
            {"state", run.eligible.isEmpty() ? "no-eligible-laps" : "available"}, {"lapCount", run.count},
            {"eligibleLapCount", run.eligible.size()}, {"tieCount", ties(run.eligible)},
            {"bestLap", run.eligible.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(record(*run.eligible.first()))}});
    }
    result.insert("state", eligible.isEmpty() ? "no-eligible-laps" : "available");
    result.insert("runs", rankedRuns); result.insert("excludedLaps", rejected);
    result.insert("lapCount", lapCount); result.insert("eligibleLapCount", eligible.size());
    result.insert("tieCount", ties(eligible));
    if (!eligible.isEmpty()) result.insert("bestOfDay", record(*eligible.first()));
    return result;
}

QJsonObject summarizeOutingProgression(const QVector<OutingLapRow> &rows,
    const QJsonObject &ranking, const QJsonArray &runMetadata)
{
    if (rows.size() > maximumOutingLapRows || runMetadata.size() > 64)
        throw ResourceLimitError("Too many runs or lap sections for progression.");
    QJsonObject result{{"state", ranking.value("state")}, {"groupId", ranking.value("groupId")},
        {"groupLabel", ranking.value("groupLabel")}, {"runs", QJsonArray{}},
        {"eligibleLapCount", ranking.value("eligibleLapCount")}, {"lapCount", ranking.value("lapCount")},
        {"minimumSeconds", QJsonValue::Null}, {"maximumSeconds", QJsonValue::Null}};
    const auto group = ranking.value("groupId").toString();
    if (group.isEmpty() || (ranking.value("state") != "available" && ranking.value("state") != "no-eligible-laps")) return result;
    QHash<QString, QJsonObject> ranked;
    for (const auto &value : ranking.value("runs").toArray())
        ranked.insert(value.toObject().value("runId").toString(), value.toObject());
    QHash<QString, QJsonArray> rejected;
    for (const auto &value : ranking.value("excludedLaps").toArray())
        rejected[value.toObject().value("runId").toString()].append(value);
    QHash<QString, qint64> clocks;
    for (const auto &row : rows) {
        if (row.timestampMilliseconds && (!clocks.contains(row.runId) || *row.timestampMilliseconds < clocks.value(row.runId)))
            clocks.insert(row.runId, *row.timestampMilliseconds);
    }
    struct Run { QJsonObject value; std::optional<qint64> clock; qsizetype order; };
    QVector<Run> runs;
    for (qsizetype i = 0; i < runMetadata.size(); ++i) {
        const auto metadata = runMetadata[i].toObject();
        if (metadata.value("groupId").toString() != group) continue;
        const auto id = metadata.value("id").toString();
        auto item = ranked.value(id, QJsonObject{{"state", "no-recorded-laps"}, {"lapCount", 0},
            {"eligibleLapCount", 0}, {"bestLap", QJsonValue::Null}, {"distribution", QJsonValue::Null}});
        item.insert("runId", id); item.insert("runName", metadata.value("name")); item.insert("groupId", group);
        for (const auto *field : {"notes", "conditions", "setupChanges"})
            item.insert(field, metadata.value(field).isUndefined() ? QJsonValue(QJsonValue::Null) : metadata.value(field));
        item.insert("excludedLaps", rejected.value(id));
        const auto clock = clocks.contains(id) ? std::optional<qint64>(clocks.value(id)) : std::nullopt;
        item.insert("chronologyKnown", clock.has_value());
        // Preserve the integer clock as text; QML never reconstructs dates from filenames.
        item.insert("firstSectionUtcMilliseconds", clock ? QJsonValue(QString::number(*clock)) : QJsonValue(QJsonValue::Null));
        runs.append(Run{item, clock, i});
        const auto distribution = item.value("distribution").toObject();
        if (!distribution.isEmpty()) {
            const double low = distribution.value("minimum").toDouble(), high = distribution.value("maximum").toDouble();
            if (result.value("minimumSeconds").isNull() || low < result.value("minimumSeconds").toDouble()) result.insert("minimumSeconds", low);
            if (result.value("maximumSeconds").isNull() || high > result.value("maximumSeconds").toDouble()) result.insert("maximumSeconds", high);
        }
    }
    std::sort(runs.begin(), runs.end(), [](const Run &a, const Run &b) {
        if (a.clock.has_value() != b.clock.has_value()) return a.clock.has_value();
        if (a.clock && a.clock != b.clock) return *a.clock < *b.clock;
        return a.order < b.order;
    });
    QJsonArray published;
    QJsonObject previous;
    for (auto &run : runs) {
        const auto best = run.value.value("bestLap").toObject();
        const auto previousBest = previous.value("bestLap").toObject();
        run.value.insert("bestDeltaPreviousListedSeconds", !best.isEmpty() && !previousBest.isEmpty()
            ? QJsonValue(best.value("durationSeconds").toDouble() - previousBest.value("durationSeconds").toDouble())
            : QJsonValue(QJsonValue::Null));
        run.value.insert("previousListedRunName", previous.value("runName"));
        published.append(run.value);
        previous = run.value; // A run without an eligible lap breaks the comparison; never fill gaps.
    }
    result.insert("runs", published);
    return result;
}

QByteArray lapReferenceKey(const QJsonObject &reference)
{
    return QJsonDocument(reference).toJson(QJsonDocument::Compact);
}

bool validLapExclusions(const QJsonValue &value, const QString &eventId)
{
    if (value.isUndefined()) return true;
    if (!value.isArray() || value.toArray().size() > maximumOutingLapRows) return false;
    QSet<QByteArray> seen;
    for (const auto &item : value.toArray()) {
        const auto entry = item.toObject();
        const auto reference = entry.value("reference").toObject();
        const auto reason = entry.value("reason");
        const auto key = lapReferenceKey(reference);
        if (entry.size() != 2 || !validLapReference(reference) || reference.value("type") != "LAP"
            || reference.value("eventId") != eventId || seen.contains(key) || !reason.isString()
            || reason.toString().trimmed().isEmpty() || reason.toString().size() > 256
            || reason.toString().contains(QChar::Null)) return false;
        seen.insert(key);
    }
    return true;
}

LapExclusionReasons lapExclusionReasons(const QJsonArray &exclusions)
{
    LapExclusionReasons reasons;
    for (const auto &item : exclusions) {
        const auto entry = item.toObject();
        reasons.insert(lapReferenceKey(entry.value("reference").toObject()), entry.value("reason").toString());
    }
    return reasons;
}

void applyLapExclusions(LapSession &laps, const QJsonObject &binding, const QJsonArray &exclusions)
{
    const auto reasons = lapExclusionReasons(exclusions);
    for (auto &lap : laps.timedLaps) {
        OutingLapRow row;
        row.runId = binding.value("runId").toString(); row.type = LapSectionType::Lap;
        row.start = lap.startTelemetryTime; row.end = lap.endTelemetryTime;
        const auto reference = makeLapReference(row, binding.value("eventId").toString(),
            binding.value("sourceId").toString(), binding.value("sourceRevision").toString().toLatin1(),
            binding.value("derivationKey").toString().toLatin1());
        lap.userExclusionReason = reasons.value(lapReferenceKey(reference));
    }
    recomputeLapRanking(laps);
}

std::optional<qint64> recordingTimestamp(const TelemetrySession &session)
{
    bool ok = false;
    const auto value = session.metadata.value("firstTimestampMilliseconds").toLongLong(&ok);
    return ok ? std::optional<qint64>(value) : std::nullopt;
}

QVector<OutingLapRow> outingLapRows(const TelemetrySession &session, const LapSession &laps,
    const QString &runId, const QString &runName, const qsizetype sourceOrder,
    const CancellationCheck &cancelled)
{
    throwIfCancelled(cancelled);
    QVector<OutingLapRow> rows;
    if (!std::isfinite(session.duration) || session.duration <= 0) return rows;
    if (laps.timedLaps.size() > maximumOutingLapRows - 2)
        throw ResourceLimitError("Too many lap sections in this recording.");
    const auto origin = recordingTimestamp(session);
    const auto append = [&](LapSectionType type, int number, double start, double end, const TimedLap *lap = nullptr) {
        throwIfCancelled(cancelled);
        if (!std::isfinite(start) || !std::isfinite(end) || start < 0 || end > session.duration || end <= start)
            return;
        OutingLapRow row{runId, runName, type, number, start, end, {}, sourceOrder, false, LapReferenceIssue::None, false, {}};
        if (lap) {
            row.referenceEligible = lap->referenceEligible();
            row.referenceIssue = lap->referenceIssue;
            row.bestOfRun = lap->referenceEligible() && laps.fastestLapIndex && *laps.fastestLapIndex >= 0
                && *laps.fastestLapIndex < laps.timedLaps.size()
                && laps.timedLaps[*laps.fastestLapIndex].number == lap->number;
        }
        const double milliseconds = start * 1000.0;
        if (origin && milliseconds < static_cast<double>(std::numeric_limits<qint64>::max())) {
            const auto delta = static_cast<qint64>(std::llround(milliseconds));
            if (*origin <= std::numeric_limits<qint64>::max() - delta)
                row.timestampMilliseconds = *origin + delta;
        }
        rows.append(row);
    };
    if (laps.acceptedPasses.isEmpty()) {
        append(LapSectionType::Unknown, 0, 0, session.duration);
        return rows;
    }
    append(LapSectionType::Out, 0, 0, laps.acceptedPasses.first().telemetryTime);
    for (const auto &lap : laps.timedLaps)
        append(LapSectionType::Lap, lap.number, lap.startTelemetryTime, lap.endTelemetryTime, &lap);
    append(LapSectionType::In, 0, laps.acceptedPasses.last().telemetryTime, session.duration);
    return rows;
}

void sortOutingLaps(QVector<OutingLapRow> &rows)
{
    if (rows.size() > maximumOutingLapRows) throw ResourceLimitError("Too many lap sections in this outing.");
    // Unknown clocks remain explicit and stable, after the time-ordered records.
    std::stable_sort(rows.begin(), rows.end(), [](const auto &a, const auto &b) {
        if (a.timestampMilliseconds.has_value() != b.timestampMilliseconds.has_value())
            return a.timestampMilliseconds.has_value();
        if (a.timestampMilliseconds && a.timestampMilliseconds != b.timestampMilliseconds)
            return *a.timestampMilliseconds < *b.timestampMilliseconds;
        if (a.sourceOrder != b.sourceOrder) return a.sourceOrder < b.sourceOrder;
        return a.start < b.start;
    });
}

} // namespace FlappedEar
