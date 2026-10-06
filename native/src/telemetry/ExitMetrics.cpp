#include "telemetry/ExitMetrics.h"

#include "telemetry/SectorTiming.h"

#include <QJsonObject>

#include <algorithm>
#include <cmath>
#include <iterator>

namespace FlappedEar {
namespace {

constexpr double boundaryEpsilon = 1e-6;

bool validThreshold(const ExitThreshold &threshold)
{
    return std::isfinite(threshold.on) && std::isfinite(threshold.off) && threshold.off >= 0.0
        && threshold.on > threshold.off && !threshold.unit.trimmed().isEmpty();
}

struct Rise {
    std::optional<double> time;
    QStringList limitations;
    QString reason; // why there is no rise
};

// The first rise to `on` after the channel was at or below `off`, sustained at or
// above `off` for `minimumDuration`. Raw samples only; gaps are never bridged.
Rise firstSustainedRise(const TelemetryChannel &channel, const double on, const double off,
    const double minimumDuration, const double startTime, const double endTime)
{
    Rise rise;
    const auto &times = channel.timestamps();
    const auto &values = channel.values();
    if (times.size() != values.size()) {
        rise.reason = exitIncompleteCoverage;
        return rise;
    }
    const double gapThreshold = telemetryGapThreshold(channel);
    const auto begin = std::distance(times.cbegin(), std::lower_bound(times.cbegin(), times.cend(), startTime));
    const auto end = std::distance(times.cbegin(), std::upper_bound(times.cbegin(), times.cend(), endTime));
    bool lifted = false;
    bool rising = false;
    bool followsGap = false;
    bool previousContiguous = false;
    bool haveSample = false;
    double previousTime = 0.0;
    double previousValue = 0.0;
    double riseStart = 0.0;
    QStringList riseLimitations;
    int finite = 0;
    for (auto index = begin; index < end; ++index) {
        const double time = times[index];
        const double value = values[index];
        const bool gapBefore = haveSample && gapThreshold > 0.0 && telemetryIsGap(channel, previousTime, time);
        if (!std::isfinite(value) || gapBefore) {
            if (rising && previousTime - riseStart >= minimumDuration) {
                rise.time = riseStart;
                rise.limitations = riseLimitations;
                return rise;
            }
            rising = false;
            followsGap = true;
            previousContiguous = false;
            if (!std::isfinite(value)) { haveSample = false; continue; }
        }
        ++finite;
        if (!rising) {
            if (!lifted) {
                if (value <= off) lifted = true;
            } else if (value >= on) {
                rising = true;
                riseLimitations.clear();
                if (previousContiguous && previousValue < on)
                    riseStart = previousTime + (time - previousTime) * (on - previousValue) / (value - previousValue);
                else
                    riseStart = time;
                if (followsGap) riseLimitations.append(exitFollowsGap);
            }
        } else if (value < off) {
            if (time - riseStart >= minimumDuration) {
                rise.time = riseStart;
                rise.limitations = riseLimitations;
                return rise;
            }
            rising = false; // a brief spike, not a pickup
        }
        haveSample = true;
        previousContiguous = true;
        followsGap = false;
        previousTime = time;
        previousValue = value;
    }
    if (rising) {
        rise.time = riseStart;
        rise.limitations = riseLimitations;
        if (previousTime - riseStart < minimumDuration) rise.limitations.append(exitTruncated);
        return rise;
    }
    rise.reason = finite == 0 ? exitIncompleteCoverage : lifted ? exitNoPickup : exitNoLift;
    return rise;
}

std::optional<double> speedAtProgress(const QVector<ProgressSegment> &lap, const TelemetrySession &session,
    const QString &channel, const double progress)
{
    const auto time = timeAtProgress(lap, progress);
    return time ? session.valueAt(channel, *time) : std::nullopt;
}

} // namespace

ExitMetrics computeExitMetrics(const double axisLengthMeters, const ApprovedSegmentation &approved,
    const QString &segmentId, const QVector<ProgressSegment> &lapTrace, const TelemetrySession &session,
    const std::optional<double> lapEndTime, const ExitMetricsOptions &options)
{
    ExitMetrics result;
    if (!approved.valid || !std::isfinite(axisLengthMeters) || axisLengthMeters <= 0.0 || !validThreshold(options.throttle)
        || !validThreshold(options.acceleration) || !std::isfinite(options.minimumDurationSeconds)
        || options.minimumDurationSeconds <= 0.0 || !std::isfinite(options.followMeters) || options.followMeters <= 0.0)
        return result;
    QJsonObject segment;
    for (const auto &value : approved.segments)
        if (value.toObject().value("id").toString() == segmentId) segment = value.toObject();
    if (segment.isEmpty()) return result;
    const double length = axisLengthMeters;
    const double start = segment.value("startProgressMeters").toDouble();
    const double end = segment.value("endProgressMeters").toDouble();
    result.segmentId = segmentId;
    result.stamp = segmentationResultStamp(approved, exitMetricsAlgorithm);
    result.valid = true;
    if (end < start) {
        result.pickup.unavailableReason = exitCrossesGate;
        result.downstreamUnavailableReason = exitCrossesGate;
        return result;
    }

    // Pickup inside the segment.
    auto &pickup = result.pickup;
    const QString throttleName = session.aliases.value("throttle");
    const QString accelerationName = session.aliases.value("longitudinalAcceleration");
    const bool hasThrottle = !throttleName.isEmpty() && session.channels.contains(throttleName);
    const bool hasAcceleration = !accelerationName.isEmpty() && session.channels.contains(accelerationName);
    if (hasThrottle || hasAcceleration) {
        pickup.method = hasThrottle ? pickupMethodMeasured : pickupMethodInferred;
        pickup.provenance = hasThrottle ? QStringLiteral("measured") : QStringLiteral("inferred");
        pickup.channel = hasThrottle ? throttleName : accelerationName;
        pickup.threshold = hasThrottle ? options.throttle : options.acceleration;
        const auto &channel = *session.channels.constFind(pickup.channel);
        pickup.unit = channel.unit;
        const bool declared = !channel.unit.trimmed().isEmpty();
        const auto fromTime = timeAtProgress(lapTrace, start);
        // A segment ending at the gate ends at the lap's timed end; the projection never reaches it exactly.
        const auto toTime = end >= length - boundaryEpsilon && lapEndTime ? lapEndTime : timeAtProgress(lapTrace, end);
        if (declared && channel.unit.trimmed().compare(pickup.threshold.unit.trimmed(), Qt::CaseInsensitive) != 0) {
            pickup.unavailableReason = exitUnitMismatch;
        } else if (!fromTime || !toTime || !(*toTime > *fromTime)) {
            pickup.unavailableReason = exitIncompleteCoverage;
        } else {
            const auto rise = firstSustainedRise(channel, pickup.threshold.on, pickup.threshold.off,
                options.minimumDurationSeconds, *fromTime, *toTime);
            if (!rise.time) {
                pickup.unavailableReason = rise.reason;
            } else {
                pickup.telemetryTime = rise.time;
                pickup.progressMeters = progressAtTime(lapTrace, *rise.time);
                pickup.limitations = rise.limitations;
                if (!declared) pickup.limitations.append(exitUnitUndeclared);
                if (!pickup.progressMeters) pickup.unavailableReason = exitIncompleteCoverage;
            }
        }
    } else {
        pickup.unavailableReason = exitNoChannel;
    }

    // Downstream interval from the end boundary.
    result.intervalStartMeters = end;
    result.intervalSource = intervalFixedDistance;
    result.intervalEndMeters = end + options.followMeters;
    for (const auto &value : approved.segments) {
        const auto next = value.toObject();
        if (next.value("type").toString() == trackSegmentTypeName(TrackSegmentType::Straight)
            && std::abs(next.value("startProgressMeters").toDouble() - end) <= boundaryEpsilon
            && next.value("endProgressMeters").toDouble() > end) {
            result.intervalSource = intervalFollowingStraight;
            result.intervalEndMeters = next.value("endProgressMeters").toDouble();
        }
    }
    const QString speedName = session.aliases.value("speed");
    const auto speedChannel = session.channels.constFind(speedName);
    if (!speedName.isEmpty() && speedChannel != session.channels.cend()) {
        result.speedChannel = speedName;
        result.speedUnit = speedChannel->unit;
        result.exitSpeed = speedAtProgress(lapTrace, session, speedName, end);
    }
    if (result.intervalEndMeters > length + boundaryEpsilon) {
        result.downstreamUnavailableReason = exitCrossesGate; // the straight continues into the next lap
        return result;
    }
    const double intervalLength = result.intervalEndMeters - result.intervalStartMeters;
    const auto startTime = timeAtProgress(lapTrace, result.intervalStartMeters);
    // An interval ending at the gate ends at the lap's timed end; the projection never reaches it exactly.
    const auto endTime = result.intervalEndMeters >= length - boundaryEpsilon
        ? lapEndTime : timeAtProgress(lapTrace, result.intervalEndMeters);
    if (projectedCoverageMeters(lapTrace, result.intervalStartMeters, result.intervalEndMeters, length)
            < intervalLength - boundaryEpsilon || !startTime || !endTime) {
        result.downstreamUnavailableReason = exitIncompleteCoverage;
    } else {
        result.elapsedSeconds = *endTime - *startTime;
        if (!result.speedChannel.isEmpty())
            result.intervalEndSpeed = session.valueAt(speedName, *endTime);
    }
    if (result.speedChannel.isEmpty() && result.downstreamUnavailableReason.isEmpty())
        result.downstreamUnavailableReason = exitSpeedChannelMissing;
    return result;
}

ExitComparison compareExitMetrics(const ExitMetrics &a, const ExitMetrics &b)
{
    ExitComparison comparison;
    if (!a.valid || !b.valid || a.segmentId != b.segmentId || a.stamp.revision != b.stamp.revision
        || a.stamp.trackConfigurationReference != b.stamp.trackConfigurationReference
        || a.intervalSource != b.intervalSource || a.intervalEndMeters != b.intervalEndMeters)
        return comparison;
    comparison.valid = true;
    const auto delta = [](const std::optional<double> &x, const std::optional<double> &y) -> std::optional<double> {
        return x && y ? std::optional<double>(*x - *y) : std::nullopt;
    };
    if (a.speedChannel == b.speedChannel && a.speedUnit == b.speedUnit) {
        comparison.exitSpeedDelta = delta(a.exitSpeed, b.exitSpeed);
        comparison.intervalEndSpeedDelta = delta(a.intervalEndSpeed, b.intervalEndSpeed);
    }
    comparison.elapsedSecondsDelta = delta(a.elapsedSeconds, b.elapsedSeconds);
    if (a.pickup.method != b.pickup.method || a.pickup.channel != b.pickup.channel) {
        comparison.pickupUnavailableReason = exitMixedProvenance;
    } else if (!a.pickup.progressMeters || !b.pickup.progressMeters) {
        comparison.pickupUnavailableReason = !a.pickup.unavailableReason.isEmpty() ? a.pickup.unavailableReason
                                                                                 : b.pickup.unavailableReason;
    } else {
        comparison.pickupDeltaMeters = *a.pickup.progressMeters - *b.pickup.progressMeters;
    }
    return comparison;
}

} // namespace FlappedEar
