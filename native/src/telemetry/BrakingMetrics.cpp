#include "telemetry/BrakingMetrics.h"
#include "telemetry/TrackSegments.h"

#include <QJsonObject>

#include <algorithm>
#include <cmath>
#include <iterator>

namespace FlappedEar {
namespace {

constexpr double gateEpsilon = 1e-6;

} // namespace

BrakingMetrics computeBrakingMetrics(const double axisLengthMeters, const ApprovedSegmentation &approved,
    const QString &segmentId, const QVector<ProgressSegment> &lapTrace, const TelemetrySession &session,
    const std::optional<double> lapStartTime, const std::optional<double> lapEndTime, const BrakingMetricsOptions &options)
{
    BrakingMetrics result;
    if (!approved.valid || !std::isfinite(axisLengthMeters) || axisLengthMeters <= 0.0
        || !std::isfinite(options.approachMeters) || options.approachMeters < 0.0)
        return result;
    QJsonObject segment;
    for (const auto &value : approved.segments)
        if (value.toObject().value("id").toString() == segmentId) segment = value.toObject();
    if (segment.isEmpty()) return result;
    result.segmentId = segmentId;
    result.stamp = segmentationResultStamp(approved, brakingMetricsAlgorithm);
    result.valid = true;

    const double start = segment.value("startProgressMeters").toDouble();
    const double end = segment.value("endProgressMeters").toDouble();
    result.entryMeters = start;
    if (end < start) {
        result.unavailableReason = brakingSegmentCrossesGate;
        return result;
    }
    // One gate-to-gate lap cannot see an approach that began before the gate.
    result.intervalStartMeters = std::max(0.0, start - options.approachMeters);
    result.intervalEndMeters = end;
    if (start - options.approachMeters < 0.0) result.limitations.append(brakingApproachClipped);
    // KAN-63: the approach never reaches back into the previous approved
    // corner; braking there belongs to that corner, not to this one.
    bool clippedByCorner = false;
    for (const auto &value : approved.segments) {
        const auto other = value.toObject();
        if (other.value("id").toString() == segmentId
            || other.value("type").toString() != trackSegmentTypeName(TrackSegmentType::Corner))
            continue;
        const double otherEnd = other.value("endProgressMeters").toDouble();
        if (otherEnd <= start + 1e-6 && otherEnd > result.intervalStartMeters) {
            result.intervalStartMeters = otherEnd;
            clippedByCorner = true;
        }
    }
    if (clippedByCorner) result.limitations.append(brakingApproachClippedAtCorner);

    // Without a brake or deceleration channel no coverage makes a braking point possible.
    const auto hasChannel = [&session](const char *alias) {
        const QString name = session.aliases.value(QString::fromLatin1(alias));
        return !name.isEmpty() && session.channels.contains(name);
    };
    if (!hasChannel("brake") && !hasChannel("longitudinalAcceleration")) {
        result.unavailableReason = brakingNoChannel;
        return result;
    }

    const auto fromTime = result.intervalStartMeters <= gateEpsilon && lapStartTime
        ? lapStartTime : timeAtProgress(lapTrace, result.intervalStartMeters);
    const auto toTime = result.intervalEndMeters >= axisLengthMeters - gateEpsilon && lapEndTime
        ? lapEndTime : timeAtProgress(lapTrace, result.intervalEndMeters);
    if (!fromTime || !toTime || !(*toTime > *fromTime)) {
        result.unavailableReason = brakingIncompleteCoverage;
        return result;
    }
    const auto detection = detectBrakingOnsets(session, *fromTime, *toTime, options.onset, &lapTrace);
    result.method = detection.method;
    result.provenance = detection.provenance;
    result.channel = detection.channel;
    result.thresholdUnit = detection.threshold.unit;
    result.onThreshold = detection.threshold.on;
    if (!detection.valid || !detection.unresolvedReason.isEmpty()) {
        result.unavailableReason = detection.valid ? detection.unresolvedReason : brakingIncompleteCoverage;
        return result;
    }
    const auto candidate = std::find_if(detection.candidates.cbegin(), detection.candidates.cend(),
        [](const BrakingOnsetCandidate &onset) { return onset.progressMeters.has_value(); });
    if (candidate == detection.candidates.cend()) {
        result.unavailableReason = brakingNoneDetected;
        return result;
    }
    result.brakingPointMeters = candidate->progressMeters;
    result.brakingPointTime = candidate->telemetryTime;
    result.distanceBeforeEntryMeters = start - *candidate->progressMeters;
    result.brakingSeconds = candidate->durationSeconds;
    result.limitations.append(candidate->uncertaintyReasons);
    const double episodeEnd = candidate->telemetryTime + candidate->durationSeconds;
    const auto endProgress = progressAtTime(lapTrace, episodeEnd);
    // Only a continuous projection between onset and episode end yields a distance.
    const bool continuous = std::any_of(lapTrace.cbegin(), lapTrace.cend(), [&](const ProgressSegment &piece) {
        return !piece.samples.isEmpty() && piece.samples.first().telemetryTime <= candidate->telemetryTime
            && piece.samples.last().telemetryTime >= episodeEnd;
    });
    if (endProgress && continuous && *endProgress >= *candidate->progressMeters)
        result.brakingDistanceMeters = *endProgress - *candidate->progressMeters;

    const QString decelerationName = session.aliases.value("longitudinalAcceleration");
    const auto decelerationChannel = session.channels.constFind(decelerationName);
    if (decelerationName.isEmpty() || decelerationChannel == session.channels.cend()) {
        result.decelerationUnavailableReason = brakingDecelerationChannelMissing;
        return result;
    }
    result.decelerationChannel = decelerationName;
    result.decelerationUnit = decelerationChannel->unit;
    const auto &times = decelerationChannel->timestamps();
    const auto &values = decelerationChannel->values();
    if (times.size() != values.size()) {
        result.decelerationUnavailableReason = brakingIncompleteCoverage;
        return result;
    }
    const double gapThreshold = telemetryGapThreshold(*decelerationChannel);
    const auto first = std::lower_bound(times.cbegin(), times.cend(), candidate->telemetryTime);
    const auto last = std::upper_bound(times.cbegin(), times.cend(), episodeEnd);
    double peak = 0.0;
    double sum = 0.0;
    int samples = 0;
    std::optional<double> previousTime;
    bool gap = false;
    for (auto it = first; it != last; ++it) {
        const auto index = std::distance(times.cbegin(), it);
        const double value = values[index];
        if (!std::isfinite(value)
            || (previousTime && gapThreshold > 0.0 && telemetryIsGap(*decelerationChannel, *previousTime, *it))) {
            gap = true;
            break;
        }
        previousTime = *it;
        const double magnitude = -value; // negative longitudinal G is braking
        peak = samples == 0 ? magnitude : std::max(peak, magnitude);
        sum += magnitude;
        ++samples;
    }
    // The episode must be covered from onset to end, not just somewhere inside.
    const bool reachesEnds = samples > 0 && first != times.cend()
        && !telemetryIsGap(*decelerationChannel, candidate->telemetryTime, *first)
        && previousTime && !telemetryIsGap(*decelerationChannel, *previousTime, episodeEnd);
    if (gap || samples < 2 || !reachesEnds) {
        result.decelerationUnavailableReason = brakingIncompleteCoverage;
        return result;
    }
    result.peakDeceleration = peak;
    result.meanDeceleration = sum / samples;
    return result;
}

BrakingComparison compareBrakingMetrics(const BrakingMetrics &a, const BrakingMetrics &b)
{
    BrakingComparison comparison;
    if (!a.valid || !b.valid || a.segmentId != b.segmentId || a.stamp.revision != b.stamp.revision
        || a.stamp.trackConfigurationReference != b.stamp.trackConfigurationReference) {
        comparison.unavailableReason = QStringLiteral("differentSegmentOrRevision");
        return comparison;
    }
    comparison.valid = true;
    if (!a.brakingPointMeters || !b.brakingPointMeters) {
        comparison.unavailableReason = !a.unavailableReason.isEmpty() ? a.unavailableReason : b.unavailableReason;
        return comparison;
    }
    if (a.method != b.method || a.provenance != b.provenance || a.channel != b.channel) {
        comparison.unavailableReason = brakingMixedProvenance;
        return comparison;
    }
    const auto delta = [](const std::optional<double> &x, const std::optional<double> &y) -> std::optional<double> {
        return x && y ? std::optional<double>(*x - *y) : std::nullopt;
    };
    comparison.brakingPointDeltaMeters = delta(a.brakingPointMeters, b.brakingPointMeters);
    comparison.brakingSecondsDelta = delta(a.brakingSeconds, b.brakingSeconds);
    comparison.brakingDistanceDeltaMeters = delta(a.brakingDistanceMeters, b.brakingDistanceMeters);
    if (a.decelerationChannel == b.decelerationChannel && a.decelerationUnit == b.decelerationUnit) {
        comparison.peakDecelerationDelta = delta(a.peakDeceleration, b.peakDeceleration);
        comparison.meanDecelerationDelta = delta(a.meanDeceleration, b.meanDeceleration);
    }
    return comparison;
}

} // namespace FlappedEar
