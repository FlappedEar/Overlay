// KAN-55: Corner Analyzer A/B evidence. Combines the four existing per-lap
// metric calculators (sector timing KAN-51, corner speeds KAN-52, braking
// KAN-53, exit effects KAN-54) for each comparison slot's own approved
// segmentation, using the shared comparison progress axis
// (ensureComparisonProgressAxis) rather than a second alignment. Segments are
// only ever shown when both slots' approved segmentation matches exactly
// (same revision, same track configuration) -- never a guessed
// correspondence between two independently-approved sets -- or, when opened
// from a theoretical-best sector (KAN-57), against the canonical run's
// segmentation that result used, labelled via comparisonSegmentationNote().

#include "app/AppController.h"
#include "telemetry/MetricProvenance.h"
#include "telemetry/GgPairs.h"

using namespace FlappedEar;

namespace {

QVariantMap provenanceValue(
    const std::optional<double> value, const MetricProvenance provenance, const QString &unavailableReason = {})
{
    QVariantMap map{{"provenance", metricProvenanceName(provenance)}};
    if (value) map.insert("value", *value);
    else if (!unavailableReason.isEmpty()) map.insert("unavailableReason", unavailableReason);
    return map;
}

QVariantMap deltaValue(const std::optional<double> value, const QString &unavailableReason = {})
{
    QVariantMap map;
    if (value) map.insert("value", *value);
    else if (!unavailableReason.isEmpty()) map.insert("unavailableReason", unavailableReason);
    return map;
}

MetricProvenance cornerSpeedPhaseProvenance(const CornerSpeeds &speeds, const CornerSpeedValue &phase)
{
    if (!phase.value) return MetricProvenance::Unavailable;
    return speeds.provenance == QLatin1String("measured") ? MetricProvenance::Measured : MetricProvenance::Unavailable;
}

MetricProvenance namedProvenance(const QString &provenance, const bool hasValue)
{
    if (!hasValue) return MetricProvenance::Unavailable;
    if (provenance == QLatin1String("measured")) return MetricProvenance::Measured;
    if (provenance == QLatin1String("inferred")) return MetricProvenance::Inferred;
    return MetricProvenance::Unavailable;
}

// KAN-117: measured speed at a segment's entry and exit and its extremes
// inside, for every segment type. Read only from the recorded speed channel;
// never derived from GPS positions.
struct SegmentSpeeds {
    std::optional<double> entry, exit, maximum, minimum;
    QString unavailableReason;
};

SegmentSpeeds segmentSpeeds(const TelemetrySession &session, const QVector<ProgressSegment> &trace,
    const double axisLengthMeters, const double lapStart, const double lapEnd,
    const double startMeters, const double endMeters)
{
    SegmentSpeeds speeds;
    if (!session.channels.contains(session.aliases.value("speed", "speed"))) {
        speeds.unavailableReason = QString(cornerPhaseSpeedChannelMissing);
        return speeds;
    }
    if (endMeters <= startMeters) {
        speeds.unavailableReason = QString(cornerPhaseCrossesGate);
        return speeds;
    }
    const auto timeAt = [&](const double meters) -> std::optional<double> {
        if (meters <= 1e-6) return lapStart;
        if (meters >= axisLengthMeters - 1e-6) return lapEnd;
        return timeAtProgress(trace, meters);
    };
    const auto start = timeAt(startMeters);
    const auto end = timeAt(endMeters);
    if (start) speeds.entry = session.valueAt("speed", *start);
    if (end) speeds.exit = session.valueAt("speed", *end);
    for (const auto &boundary : {speeds.entry, speeds.exit}) {
        if (!boundary) continue;
        if (!speeds.maximum || *boundary > *speeds.maximum) speeds.maximum = boundary;
        if (!speeds.minimum || *boundary < *speeds.minimum) speeds.minimum = boundary;
    }
    if (start && end && *end > *start) {
        for (const auto &segment : session.sampledSegments("speed", *start, *end, 4000)) {
            for (const auto &point : segment) {
                const double value = point.y();
                if (!speeds.maximum || value > *speeds.maximum) speeds.maximum = value;
                if (!speeds.minimum || value < *speeds.minimum) speeds.minimum = value;
            }
        }
    }
    if (!speeds.entry || !speeds.exit || !speeds.maximum) speeds.unavailableReason = QStringLiteral("incompleteCoverage");
    return speeds;
}

} // namespace

QStringList AppController::comparisonPreferredChannels() const
{
    // The recording's own names for the speed, throttle and brake aliases
    // (for example "velocity", "throttle_pos", "brake_pos").
    const auto available = comparisonAvailableChannels();
    if (available.isEmpty()) return {};
    const auto &aliases = m_comparisonSlots[0].session->aliases;
    QStringList preferred;
    for (const auto *alias : {"speed", "throttle", "brake"}) {
        const auto name = aliases.value(QString::fromLatin1(alias), QString::fromLatin1(alias));
        if (available.contains(name) && !preferred.contains(name)) preferred.append(name);
    }
    return preferred;
}

ApprovedSegmentation AppController::comparisonApprovedSegmentation(const int slot) const
{
    if (slot < 0 || slot > 1) return {};
    const auto &row = m_comparisonSlots[slot].row;
    const auto runId = row.value("runId").toString();
    const auto configuration = row.value("compatibilityGroupId").toString();
    for (const auto &value : currentProjectObject().value("event").toObject().value("runs").toArray()) {
        const auto run = value.toObject();
        if (run.value("id").toString() == runId)
            return approvedSegmentation(run.value("trackSegments"), configuration);
    }
    return {};
}

std::optional<ApprovedSegmentation> AppController::comparisonSharedSegmentation() const
{
    if (!comparisonPairReady()) return std::nullopt;
    const auto approvedA = comparisonApprovedSegmentation(0);
    const auto approvedB = comparisonApprovedSegmentation(1);
    if (approvedA.valid && approvedB.valid && !approvedA.revision.isEmpty()
        && approvedA.revision == approvedB.revision
        && approvedA.trackConfigurationReference == approvedB.trackConfigurationReference)
        return approvedA;
    // KAN-57: opened from a theoretical-best sector -- use the same canonical
    // segmentation the theoretical best measured every lap against, but only
    // when both laps belong to the group it was approved for.
    if (m_comparisonSegmentationRunId.isEmpty()) return std::nullopt;
    const auto group = m_comparisonSlots[0].row.value("compatibilityGroupId").toString();
    if (group.isEmpty() || m_comparisonSlots[1].row.value("compatibilityGroupId").toString() != group)
        return std::nullopt;
    for (const auto &value : currentProjectObject().value("event").toObject().value("runs").toArray()) {
        const auto run = value.toObject();
        if (run.value("id").toString() != m_comparisonSegmentationRunId) continue;
        auto canonical = approvedSegmentation(run.value("trackSegments"), group);
        if (canonical.valid && !canonical.revision.isEmpty() && !canonical.segments.isEmpty()) return canonical;
    }
    return std::nullopt;
}

QString AppController::comparisonSegmentationNote() const
{
    if (!comparisonPairReady() || m_comparisonSegmentationRunId.isEmpty()) return {};
    const auto approvedA = comparisonApprovedSegmentation(0);
    const auto approvedB = comparisonApprovedSegmentation(1);
    if (approvedA.valid && !approvedA.revision.isEmpty() && approvedA.revision == approvedB.revision) return {};
    if (!comparisonSharedSegmentation()) return {};
    QString runName = m_comparisonSegmentationRunId;
    for (const auto &value : currentProjectObject().value("event").toObject().value("runs").toArray())
        if (value.toObject().value("id").toString() == m_comparisonSegmentationRunId)
            runName = value.toObject().value("name").toString();
    return tr("Segments approved on %1, as used by the sector theoretical best. Boundaries are distances along "
              "that run's axis, so they can shift by a few metres on these laps.").arg(runName);
}

QVariantList AppController::comparisonApprovedSegments() const
{
    const auto shared = comparisonSharedSegmentation();
    if (!shared) return {};
    QVariantList rows;
    for (const auto &value : shared->segments) {
        const auto segment = value.toObject();
        rows.append(QVariantMap{{"id", segment.value("id").toString()}, {"name", segment.value("name").toString()},
            {"type", segment.value("type").toString()},
            {"startMeters", segment.value("startProgressMeters").toDouble()},
            {"endMeters", segment.value("endProgressMeters").toDouble()}});
    }
    return rows;
}

QVariantMap AppController::comparisonSegmentMetrics(const QString &segmentId) const
{
    if (!comparisonPairReady() || segmentId.isEmpty()) return {};
    ensureComparisonProgressAxis();
    if (!m_comparisonProgressAxis.valid) return {};
    const auto shared = comparisonSharedSegmentation();
    if (!shared) return {};
    const auto &approvedA = *shared;
    const auto &approvedB = *shared;
    QString segmentType;
    for (const auto &value : approvedA.segments) {
        const auto segment = value.toObject();
        if (segment.value("id").toString() == segmentId) { segmentType = segment.value("type").toString(); break; }
    }
    if (segmentType.isEmpty()) return {};

    const auto &slotA = m_comparisonSlots[0];
    const auto &slotB = m_comparisonSlots[1];
    if (!slotA.session || !slotB.session) return {};

    QVariantMap result{{"segmentId", segmentId}, {"type", segmentType}};

    // Sector time: every segment type, not just corners.
    {
        const auto timesA = computeLapSectorTimes(approvedA, m_comparisonProgressAxis.lengthMeters,
            m_comparisonProgressTraceCache[0], slotA.row.value("startTime").toDouble(),
            slotA.row.value("endTime").toDouble(), QJsonObject::fromVariantMap(slotA.row.value("reference").toMap()));
        const auto timesB = computeLapSectorTimes(approvedB, m_comparisonProgressAxis.lengthMeters,
            m_comparisonProgressTraceCache[1], slotB.row.value("startTime").toDouble(),
            slotB.row.value("endTime").toDouble(), QJsonObject::fromVariantMap(slotB.row.value("reference").toMap()));
        const SectorTime *sectorA = nullptr;
        const SectorTime *sectorB = nullptr;
        for (const auto &sector : timesA.sectors) if (sector.segmentId == segmentId) sectorA = &sector;
        for (const auto &sector : timesB.sectors) if (sector.segmentId == segmentId) sectorB = &sector;
        if (sectorA && sectorB) {
            const auto comparison = compareSectorTimes(timesA, timesB, segmentId);
            result.insert("sectorTime", QVariantMap{
                {"a", provenanceValue(sectorA->seconds,
                     sectorA->seconds ? MetricProvenance::Calculated : MetricProvenance::Unavailable, sectorA->unavailableReason)},
                {"b", provenanceValue(sectorB->seconds,
                     sectorB->seconds ? MetricProvenance::Calculated : MetricProvenance::Unavailable, sectorB->unavailableReason)},
                {"delta", deltaValue(comparison.secondsDelta, comparison.unavailableReason)}});
        }
    }

    // Entry/exit speed and the extremes inside, for every segment type (KAN-117).
    {
        double startMeters = 0.0, endMeters = 0.0;
        for (const auto &value : approvedA.segments) {
            const auto segment = value.toObject();
            if (segment.value("id").toString() != segmentId) continue;
            startMeters = segment.value("startProgressMeters").toDouble();
            endMeters = segment.value("endProgressMeters").toDouble();
        }
        const auto length = m_comparisonProgressAxis.lengthMeters;
        const auto speedsA = segmentSpeeds(*slotA.session, m_comparisonProgressTraceCache[0], length,
            slotA.row.value("startTime").toDouble(), slotA.row.value("endTime").toDouble(), startMeters, endMeters);
        const auto speedsB = segmentSpeeds(*slotB.session, m_comparisonProgressTraceCache[1], length,
            slotB.row.value("startTime").toDouble(), slotB.row.value("endTime").toDouble(), startMeters, endMeters);
        const auto entry = [&](const std::optional<double> a, const std::optional<double> b) {
            std::optional<double> delta;
            if (a && b) delta = *a - *b;
            return QVariantMap{
                {"a", provenanceValue(a, a ? MetricProvenance::Measured : MetricProvenance::Unavailable, speedsA.unavailableReason)},
                {"b", provenanceValue(b, b ? MetricProvenance::Measured : MetricProvenance::Unavailable, speedsB.unavailableReason)},
                {"delta", deltaValue(delta)}};
        };
        const auto speedChannel = slotA.session->aliases.value("speed", "speed");
        result.insert("speeds", QVariantMap{{"unit", slotA.session->channels.value(speedChannel).unit},
            {"entry", entry(speedsA.entry, speedsB.entry)},
            {"maximum", entry(speedsA.maximum, speedsB.maximum)}, {"minimum", entry(speedsA.minimum, speedsB.minimum)},
            {"exit", entry(speedsA.exit, speedsB.exit)}});
    }

    // Corner-only metrics (KAN-52/53/54's own scope, mirroring outingLap*Metrics).
    if (segmentType != trackSegmentTypeName(TrackSegmentType::Corner)) return result;
    const auto features = computeTrackFeatures(m_comparisonProgressAxis, segmentReviewSmoothingMeters);

    {
        const auto speedsA = computeCornerSpeeds(m_comparisonProgressAxis, features, approvedA, segmentId,
            m_comparisonProgressTraceCache[0], *slotA.session);
        const auto speedsB = computeCornerSpeeds(m_comparisonProgressAxis, features, approvedB, segmentId,
            m_comparisonProgressTraceCache[1], *slotB.session);
        if (speedsA.valid && speedsB.valid) {
            const auto comparison = compareCornerSpeeds(speedsA, speedsB);
            const auto phase = [&](const CornerSpeedValue &a, const CornerSpeedValue &b, const std::optional<double> &delta) {
                return QVariantMap{
                    {"a", provenanceValue(a.value, cornerSpeedPhaseProvenance(speedsA, a), a.unavailableReason)},
                    {"b", provenanceValue(b.value, cornerSpeedPhaseProvenance(speedsB, b), b.unavailableReason)},
                    {"delta", deltaValue(delta, comparison.unavailableReason)}};
            };
            result.insert("corner", QVariantMap{{"channel", speedsA.channel}, {"unit", speedsA.unit},
                {"entry", phase(speedsA.entry, speedsB.entry, comparison.entryDelta)},
                {"apex", phase(speedsA.apex, speedsB.apex, comparison.apexDelta)},
                {"minimum", phase(speedsA.minimum, speedsB.minimum, comparison.minimumDelta)},
                {"exit", phase(speedsA.exit, speedsB.exit, comparison.exitDelta)}});
        }
    }

    {
        const auto brakingA = computeBrakingMetrics(m_comparisonProgressAxis.lengthMeters, approvedA, segmentId,
            m_comparisonProgressTraceCache[0], *slotA.session, slotA.row.value("startTime").toDouble(),
            slotA.row.value("endTime").toDouble());
        const auto brakingB = computeBrakingMetrics(m_comparisonProgressAxis.lengthMeters, approvedB, segmentId,
            m_comparisonProgressTraceCache[1], *slotB.session, slotB.row.value("startTime").toDouble(),
            slotB.row.value("endTime").toDouble());
        if (brakingA.valid && brakingB.valid) {
            const auto comparison = compareBrakingMetrics(brakingA, brakingB);
            result.insert("braking", QVariantMap{
                {"point", QVariantMap{
                     {"a", provenanceValue(brakingA.brakingPointMeters,
                          namedProvenance(brakingA.provenance, brakingA.brakingPointMeters.has_value()), brakingA.unavailableReason)},
                     {"b", provenanceValue(brakingB.brakingPointMeters,
                          namedProvenance(brakingB.provenance, brakingB.brakingPointMeters.has_value()), brakingB.unavailableReason)},
                     {"delta", deltaValue(comparison.brakingPointDeltaMeters, comparison.unavailableReason)}}},
                {"seconds", QVariantMap{
                     {"a", provenanceValue(brakingA.brakingSeconds,
                          namedProvenance(brakingA.provenance, brakingA.brakingSeconds.has_value()), brakingA.unavailableReason)},
                     {"b", provenanceValue(brakingB.brakingSeconds,
                          namedProvenance(brakingB.provenance, brakingB.brakingSeconds.has_value()), brakingB.unavailableReason)},
                     {"delta", deltaValue(comparison.brakingSecondsDelta, comparison.unavailableReason)}}},
                {"peakDeceleration", QVariantMap{
                     {"a", provenanceValue(brakingA.peakDeceleration,
                          namedProvenance(brakingA.provenance, brakingA.peakDeceleration.has_value()), brakingA.decelerationUnavailableReason)},
                     {"b", provenanceValue(brakingB.peakDeceleration,
                          namedProvenance(brakingB.provenance, brakingB.peakDeceleration.has_value()), brakingB.decelerationUnavailableReason)},
                     {"delta", deltaValue(comparison.peakDecelerationDelta, comparison.unavailableReason)}}}});
        }
    }

    {
        const auto exitA = computeExitMetrics(m_comparisonProgressAxis.lengthMeters, approvedA, segmentId,
            m_comparisonProgressTraceCache[0], *slotA.session, slotA.row.value("endTime").toDouble());
        const auto exitB = computeExitMetrics(m_comparisonProgressAxis.lengthMeters, approvedB, segmentId,
            m_comparisonProgressTraceCache[1], *slotB.session, slotB.row.value("endTime").toDouble());
        if (exitA.valid && exitB.valid) {
            const auto comparison = compareExitMetrics(exitA, exitB);
            result.insert("exitEffects", QVariantMap{
                {"pickup", QVariantMap{
                     {"a", provenanceValue(exitA.pickup.progressMeters,
                          namedProvenance(exitA.pickup.provenance, exitA.pickup.progressMeters.has_value()), exitA.pickup.unavailableReason)},
                     {"b", provenanceValue(exitB.pickup.progressMeters,
                          namedProvenance(exitB.pickup.provenance, exitB.pickup.progressMeters.has_value()), exitB.pickup.unavailableReason)},
                     {"delta", deltaValue(comparison.pickupDeltaMeters, comparison.pickupUnavailableReason)}}},
                {"exitSpeed", QVariantMap{
                     {"a", provenanceValue(exitA.exitSpeed,
                          exitA.exitSpeed ? MetricProvenance::Measured : MetricProvenance::Unavailable, exitA.downstreamUnavailableReason)},
                     {"b", provenanceValue(exitB.exitSpeed,
                          exitB.exitSpeed ? MetricProvenance::Measured : MetricProvenance::Unavailable, exitB.downstreamUnavailableReason)},
                     {"delta", deltaValue(comparison.exitSpeedDelta)}}}});
        }
    }

    return result;
}

QVariantMap AppController::comparisonTimeLossObservations() const
{
    const auto shared = comparisonSharedSegmentation();
    if (!shared) return {{"valid", false}};
    ensureComparisonProgressAxis();
    if (!m_comparisonProgressAxis.valid) return {{"valid", false}};
    const auto &slotA = m_comparisonSlots[0];
    const auto &slotB = m_comparisonSlots[1];
    const auto startA = slotA.row.value("startTime").toDouble(), endA = slotA.row.value("endTime").toDouble();
    const auto startB = slotB.row.value("startTime").toDouble(), endB = slotB.row.value("endTime").toDouble();
    const auto timesA = computeLapSectorTimes(*shared, m_comparisonProgressAxis.lengthMeters,
        m_comparisonProgressTraceCache[0], startA, endA, QJsonObject::fromVariantMap(slotA.row.value("reference").toMap()));
    const auto timesB = computeLapSectorTimes(*shared, m_comparisonProgressAxis.lengthMeters,
        m_comparisonProgressTraceCache[1], startB, endB, QJsonObject::fromVariantMap(slotB.row.value("reference").toMap()));
    const auto observations = computeTimeLossObservations(
        *shared, m_comparisonProgressAxis.lengthMeters, timesA, startA, timesB, startB);
    if (!observations.valid) return {{"valid", false}, {"unavailableReason", observations.unavailableReason}};
    QVariantList windows;
    for (const auto &window : observations.windows) {
        QVariantMap row{{"segmentId", window.segmentId}, {"name", window.name}, {"type", window.type},
            {"role", window.role}, {"startMeters", window.startProgressMeters}, {"endMeters", window.endProgressMeters}};
        if (!window.cornerSegmentId.isEmpty()) row.insert("cornerSegmentId", window.cornerSegmentId);
        if (window.incrementSeconds) row.insert("incrementSeconds", *window.incrementSeconds);
        else row.insert("unavailableReason", window.unavailableReason);
        if (window.cumulativeAtStartSeconds) row.insert("cumulativeAtStartSeconds", *window.cumulativeAtStartSeconds);
        if (window.cumulativeAtEndSeconds) row.insert("cumulativeAtEndSeconds", *window.cumulativeAtEndSeconds);
        windows.append(row);
    }
    return {{"valid", true}, {"algorithm", QString::fromLatin1(timeLossAlgorithm)},
        {"revision", observations.stamp.revision}, {"windows", windows},
        {"allWindowsTimed", observations.allWindowsTimed},
        {"timedIncrementSumSeconds", observations.timedIncrementSumSeconds},
        {"lapDeltaSeconds", (endA - startA) - (endB - startB)}};
}

QVariantMap AppController::comparisonGgScatter(const double startMeters, const double endMeters, const int maximumPoints) const
{
    if (!comparisonPairReady()) return {{"valid", false}};
    ensureComparisonProgressAxis();
    if (!m_comparisonProgressAxis.valid) return {{"valid", false}};
    const double length = m_comparisonProgressAxis.lengthMeters;
    const double from = std::clamp(startMeters, 0.0, length), to = std::clamp(endMeters, 0.0, length);
    const auto peakMap = [](const std::optional<GgPeak> &peak) -> QVariant {
        if (!peak) return {};
        return QVariantMap{{"value", peak->value}, {"lateralG", peak->point.lateralG},
            {"longitudinalG", peak->point.longitudinalG}, {"time", peak->point.time}};
    };
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
        const auto t0 = timeAt(from), t1 = timeAt(to);
        if (!t0 || !t1 || *t1 <= *t0) {
            laps.append(QVariantMap{{"valid", false}, {"unavailableReason", QStringLiteral("incompleteCoverage")}});
            continue;
        }
        const auto pairs = buildGgPairs(*comparisonSlot.session, *t0, *t1);
        if (!pairs.valid) {
            laps.append(QVariantMap{{"valid", false}, {"unavailableReason", pairs.unavailableReason}});
            continue;
        }
        const auto peaks = computeGgPeaks(pairs.points);
        QVariantList points;
        for (const auto &point : decimateGgPoints(pairs.points, peaks, maximumPoints))
            points.append(QVariantMap{{"x", point.lateralG}, {"y", point.longitudinalG}});
        laps.append(QVariantMap{{"valid", true}, {"points", points}, {"sampleCount", peaks.sampleCount},
            {"candidateCount", pairs.candidateCount}, {"skippedForGap", pairs.skippedForGap},
            {"excludedOutliers", pairs.excludedOutliers}, {"sharedClock", pairs.sharedClock},
            {"unitsDeclared", pairs.unitsDeclared}, {"longitudinalChannel", pairs.longitudinalChannel},
            {"lateralChannel", pairs.lateralChannel},
            {"peaks", QVariantMap{{"lateral", peakMap(peaks.lateral)}, {"braking", peakMap(peaks.braking)},
                {"acceleration", peakMap(peaks.acceleration)}, {"combined", peakMap(peaks.combined)}}}});
    }
    return {{"valid", true}, {"algorithm", QString::fromLatin1(ggPairsAlgorithm)}, {"laps", laps},
        {"startMeters", from}, {"endMeters", to}};
}
