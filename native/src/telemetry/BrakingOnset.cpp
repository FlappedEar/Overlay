#include "telemetry/BrakingOnset.h"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace FlappedEar {
namespace {

bool validThreshold(const BrakingThreshold &threshold)
{
    return std::isfinite(threshold.on) && std::isfinite(threshold.off) && threshold.off > 0.0
        && threshold.on > threshold.off && !threshold.unit.trimmed().isEmpty();
}

} // namespace

BrakingOnsetDetection detectBrakingOnsets(const TelemetrySession &session, const double startTime,
    const double endTime, const BrakingOnsetOptions &options, const QVector<ProgressSegment> *lapTrace)
{
    BrakingOnsetDetection result;
    if (!std::isfinite(startTime) || !std::isfinite(endTime) || endTime <= startTime
        || !validThreshold(options.measuredBrake) || !validThreshold(options.inferredDeceleration)
        || !std::isfinite(options.minimumDurationSeconds) || options.minimumDurationSeconds <= 0.0)
        return result;
    result.valid = true;
    result.minimumDurationSeconds = options.minimumDurationSeconds;

    const QString brakeName = session.aliases.value("brake");
    const QString decelerationName = session.aliases.value("longitudinalAcceleration");
    const bool hasBrake = !brakeName.isEmpty() && session.channels.contains(brakeName);
    const bool hasDeceleration = !decelerationName.isEmpty() && session.channels.contains(decelerationName);
    double sign = 1.0;
    if (hasBrake) {
        result.method = brakingMethodMeasured;
        result.provenance = brakingProvenanceMeasured;
        result.channel = brakeName;
        result.threshold = options.measuredBrake;
    } else if (hasDeceleration) {
        result.method = brakingMethodInferred;
        result.provenance = brakingProvenanceInferred;
        result.channel = decelerationName;
        result.threshold = options.inferredDeceleration;
        sign = -1.0; // negative longitudinal G is braking
        if (!options.allowInferred) {
            result.unresolvedReason = brakingInferenceDisabled;
            return result;
        }
    } else {
        result.unresolvedReason = brakingNoChannel;
        return result;
    }

    const auto &channel = *session.channels.constFind(result.channel);
    result.channelUnit = channel.unit;
    const bool unitDeclared = !channel.unit.trimmed().isEmpty();
    if (unitDeclared && channel.unit.trimmed().compare(result.threshold.unit.trimmed(), Qt::CaseInsensitive) != 0) {
        result.unresolvedReason = brakingUnitMismatch;
        return result;
    }
    if (channel.timestamps.size() != channel.values.size()) {
        result.unresolvedReason = brakingNoSamples;
        return result;
    }

    const double gapThreshold = telemetryGapThreshold(channel);
    const auto &times = channel.timestamps;
    const auto first = std::lower_bound(times.cbegin(), times.cend(), startTime);
    const auto last = std::upper_bound(times.cbegin(), times.cend(), endTime);
    const auto begin = std::distance(times.cbegin(), first);
    const auto end = std::distance(times.cbegin(), last);

    struct Episode {
        double onset = 0.0;
        double tolerance = 0.0;
        double peak = 0.0;
        double lastTime = 0.0;
        QStringList reasons;
    };
    std::optional<Episode> episode;
    bool haveSample = false;
    int finiteSamples = 0;
    bool previousContiguous = false;
    bool followsGap = false;
    double previousTime = 0.0;
    double previousMagnitude = 0.0;

    const auto close = [&](const double endTimeValue, const char *truncation) {
        auto &current = *episode;
        const double duration = endTimeValue - current.onset;
        if (duration < options.minimumDurationSeconds && !truncation) {
            ++result.rejectedSpikes;
        } else {
            BrakingOnsetCandidate candidate;
            candidate.telemetryTime = current.onset;
            candidate.toleranceSeconds = current.tolerance;
            candidate.durationSeconds = duration;
            candidate.peakValue = sign * current.peak;
            candidate.uncertaintyReasons = current.reasons;
            if (truncation) candidate.uncertaintyReasons.append(truncation);
            if (!unitDeclared) candidate.uncertaintyReasons.append(brakingUnitUndeclared);
            if (lapTrace) candidate.progressMeters = progressAtTime(*lapTrace, candidate.telemetryTime);
            result.candidates.append(candidate);
        }
        episode.reset();
    };

    for (auto index = begin; index < end; ++index) {
        const double time = times[index];
        const double raw = channel.values[index];
        const bool gapBefore = haveSample && gapThreshold > 0.0 && time - previousTime > gapThreshold;
        if (!std::isfinite(raw) || gapBefore) {
            if (!followsGap) ++result.gaps;
            if (episode) close(episode->lastTime, brakingInterruptedByGap);
            followsGap = true;
            previousContiguous = false;
            if (!std::isfinite(raw)) { haveSample = false; continue; }
        }
        ++finiteSamples;
        const double magnitude = sign * raw;
        if (!episode && magnitude >= result.threshold.on) {
            Episode started;
            if (previousContiguous && previousMagnitude < result.threshold.on) {
                const double fraction = (result.threshold.on - previousMagnitude) / (magnitude - previousMagnitude);
                started.onset = previousTime + (time - previousTime) * fraction;
                started.tolerance = time - previousTime;
            } else {
                started.onset = time;
                started.tolerance = telemetryBaseInterval(channel);
                started.reasons.append(followsGap ? brakingFollowsGap : brakingAlreadyActive);
            }
            started.peak = magnitude;
            started.lastTime = time;
            episode = started;
        } else if (episode && magnitude < result.threshold.off) {
            close(time, nullptr);
        } else if (episode) {
            episode->peak = std::max(episode->peak, magnitude);
            episode->lastTime = time;
        }
        haveSample = true;
        previousContiguous = true;
        followsGap = false;
        previousTime = time;
        previousMagnitude = magnitude;
    }
    if (episode) close(episode->lastTime, brakingTruncatedAtWindowEnd);
    if (finiteSamples == 0) result.unresolvedReason = brakingNoSamples;
    return result;
}

} // namespace FlappedEar
