#include "telemetry/DrivingStates.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <iterator>
#include <optional>

namespace FlappedEar {
namespace {

using Intervals = QVector<DrivingStateInterval>;

bool validThreshold(const BrakingThreshold &threshold)
{
    return std::isfinite(threshold.on) && std::isfinite(threshold.off) && threshold.off >= 0.0
        && threshold.on > threshold.off;
}

bool unitMatches(const QString &declared, const QStringList &accepted)
{
    const QString unit = declared.trimmed();
    if (unit.isEmpty()) return true; // undeclared: used, the channel is named in the result
    return std::any_of(accepted.cbegin(), accepted.cend(),
        [&unit](const QString &candidate) { return unit.compare(candidate, Qt::CaseInsensitive) == 0; });
}

// Episodes where `magnitude(value)` rises to `on` and stays above `off`, and
// the spans where the channel has contiguous samples. Nothing is bridged
// across a gap or a non-finite sample.
void classify(const TelemetryChannel &channel, const std::function<double(double)> &magnitude,
    const BrakingThreshold &threshold, const double minimumDuration, const double start, const double end,
    DrivingStateTrack &track)
{
    if (channel.timestamps().size() != channel.values().size()) return;
    const double gapThreshold = telemetryGapThreshold(channel);
    const auto &times = channel.timestamps();
    const auto begin = std::distance(times.cbegin(), std::lower_bound(times.cbegin(), times.cend(), start));
    const auto last = std::distance(times.cbegin(), std::upper_bound(times.cbegin(), times.cend(), end));
    std::optional<double> knownStart, activeStart;
    double previousTime = 0.0;
    bool havePrevious = false;
    const auto closeActive = [&](const double at) {
        if (!activeStart) return;
        if (at - *activeStart >= minimumDuration) track.active.append({*activeStart, at});
        else ++track.rejectedSpikes;
        activeStart.reset();
    };
    const auto closeKnown = [&](const double at) {
        closeActive(at);
        if (knownStart && at > *knownStart) track.known.append({*knownStart, at});
        knownStart.reset();
    };
    for (auto index = begin; index < last; ++index) {
        const double time = times[index];
        const double value = channel.values()[index];
        const bool gap = havePrevious && gapThreshold > 0.0 && telemetryIsGap(channel, previousTime, time);
        if (gap || !std::isfinite(value)) closeKnown(previousTime);
        if (!std::isfinite(value)) { havePrevious = false; continue; }
        if (!knownStart) knownStart = time;
        const double level = magnitude(value);
        if (!activeStart && level >= threshold.on) activeStart = time;
        else if (activeStart && level < threshold.off) closeActive(time);
        previousTime = time;
        havePrevious = true;
    }
    if (havePrevious) closeKnown(previousTime);
}

Intervals intersect(const Intervals &a, const Intervals &b)
{
    Intervals result;
    qsizetype i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        const double start = std::max(a[i].start, b[j].start), end = std::min(a[i].end, b[j].end);
        if (end > start) result.append({start, end});
        if (a[i].end < b[j].end) ++i; else ++j;
    }
    return result;
}

Intervals unite(Intervals intervals)
{
    std::sort(intervals.begin(), intervals.end(),
        [](const DrivingStateInterval &x, const DrivingStateInterval &y) { return x.start < y.start; });
    Intervals result;
    for (const auto &interval : intervals) {
        if (!result.isEmpty() && interval.start <= result.last().end) result.last().end = std::max(result.last().end, interval.end);
        else result.append(interval);
    }
    return result;
}

Intervals subtract(const Intervals &from, const Intervals &removed)
{
    Intervals result;
    for (auto piece : from) {
        for (const auto &cut : removed) {
            if (cut.end <= piece.start || cut.start >= piece.end) continue;
            if (cut.start > piece.start) result.append({piece.start, cut.start});
            piece.start = std::max(piece.start, cut.end);
            if (piece.start >= piece.end) break;
        }
        if (piece.end > piece.start) result.append(piece);
    }
    return result;
}

const TelemetryChannel *aliasChannel(const TelemetrySession &session, const char *alias, QString *name)
{
    *name = session.aliases.value(QString::fromLatin1(alias));
    const auto channel = session.channels.constFind(*name);
    return name->isEmpty() || channel == session.channels.cend() ? nullptr : &*channel;
}

// A pedal state from its measured channel, or inferred from longitudinal G
// when the pedal channel is missing (never when it is present but empty).
void pedalState(const TelemetrySession &session, const char *pedalAlias, const BrakingThreshold &measured,
    const BrakingThreshold &inferred, const double sign, const DrivingStateOptions &options,
    const double start, const double end, DrivingStateTrack &track)
{
    QString name;
    if (const auto *pedal = aliasChannel(session, pedalAlias, &name)) {
        track.channel = name; track.unit = pedal->unit; track.threshold = measured;
        if (!unitMatches(pedal->unit, {measured.unit})) { track.unresolvedReason = QStringLiteral("unitMismatch"); return; }
        track.provenance = drivingStateMeasured;
        classify(*pedal, [](double value) { return value; }, measured, options.minimumDurationSeconds, start, end, track);
        return;
    }
    const auto *longitudinal = aliasChannel(session, "longitudinalAcceleration", &name);
    if (!longitudinal) { track.unresolvedReason = QStringLiteral("noPedalOrAccelerationChannel"); return; }
    if (!options.allowInferred) { track.unresolvedReason = QStringLiteral("inferenceDisabled"); return; }
    track.channel = name; track.unit = longitudinal->unit; track.threshold = inferred;
    if (!unitMatches(longitudinal->unit, {inferred.unit})) { track.unresolvedReason = QStringLiteral("unitMismatch"); return; }
    track.provenance = drivingStateInferred;
    classify(*longitudinal, [sign](double value) { return sign * value; }, inferred, options.minimumDurationSeconds, start, end, track);
}

} // namespace

DrivingStateClassification classifyDrivingStates(const TelemetrySession &session, const double startTime,
    const double endTime, const DrivingStateOptions &options)
{
    DrivingStateClassification result;
    if (!std::isfinite(startTime) || !std::isfinite(endTime) || endTime <= startTime
        || !validThreshold(options.measuredBrake) || !validThreshold(options.measuredThrottle)
        || !validThreshold(options.inferredBraking) || !validThreshold(options.inferredAcceleration)
        || !validThreshold(options.cornering) || !std::isfinite(options.minimumSpeedKmh) || options.minimumSpeedKmh < 0.0
        || !std::isfinite(options.minimumDurationSeconds) || options.minimumDurationSeconds <= 0.0)
        return result;
    result.valid = true;
    result.start = startTime;
    result.end = endTime;

    pedalState(session, "brake", options.measuredBrake, options.inferredBraking, -1.0, options, startTime, endTime, result.braking);
    pedalState(session, "throttle", options.measuredThrottle, options.inferredAcceleration, 1.0, options, startTime, endTime, result.accelerating);

    QString name;
    if (const auto *lateral = aliasChannel(session, "lateralAcceleration", &name)) {
        auto &track = result.cornering;
        track.channel = name; track.unit = lateral->unit; track.threshold = options.cornering;
        if (!unitMatches(lateral->unit, {options.cornering.unit})) {
            track.unresolvedReason = QStringLiteral("unitMismatch");
        } else {
            track.provenance = name.endsWith(QStringLiteral("-calc"), Qt::CaseInsensitive)
                ? drivingStateCalculated : drivingStateMeasured;
            classify(*lateral, [](double value) { return std::abs(value); }, options.cornering,
                     options.minimumDurationSeconds, startTime, endTime, track);
        }
    } else {
        result.cornering.unresolvedReason = QStringLiteral("noLateralAccelerationChannel");
    }

    // Coasting: moving, both pedal states known, neither active.
    auto &coasting = result.coasting;
    const auto *speed = aliasChannel(session, "speed", &name);
    if (result.braking.provenance == drivingStateUnknown || result.accelerating.provenance == drivingStateUnknown) {
        coasting.unresolvedReason = QStringLiteral("pedalStateUnknown");
    } else if (!speed) {
        coasting.unresolvedReason = QStringLiteral("noSpeedChannel");
    } else if (!unitMatches(speed->unit, {QStringLiteral("km/h"), QStringLiteral("kmh")})) {
        coasting.unresolvedReason = QStringLiteral("unitMismatch");
    } else {
        DrivingStateTrack moving;
        const BrakingThreshold movingThreshold{options.minimumSpeedKmh, std::max(0.0, options.minimumSpeedKmh - 2.0), "km/h"};
        classify(*speed, [](double value) { return value; }, movingThreshold, options.minimumDurationSeconds,
                 startTime, endTime, moving);
        coasting.provenance = result.braking.provenance == drivingStateMeasured && result.accelerating.provenance == drivingStateMeasured
            ? drivingStateMeasured : drivingStateInferred;
        coasting.channel = name;
        coasting.unit = speed->unit;
        coasting.threshold = movingThreshold;
        coasting.known = intersect(intersect(unite(result.braking.known), unite(result.accelerating.known)), unite(moving.known));
        const auto candidates = subtract(intersect(coasting.known, unite(moving.active)),
            unite(result.braking.active + result.accelerating.active));
        for (const auto &interval : candidates) {
            if (interval.end - interval.start >= options.minimumDurationSeconds) coasting.active.append(interval);
            else ++coasting.rejectedSpikes;
        }
    }
    return result;
}

QVector<DrivingStateInterval> overlapOf(const QVector<DrivingStateInterval> &first,
    const QVector<DrivingStateInterval> &second)
{
    return intersect(unite(first), unite(second));
}

double travelledMeters(const TelemetrySession &session, const QVector<DrivingStateInterval> &intervals)
{
    const auto speed = session.channels.constFind(session.aliases.value(QStringLiteral("speed")));
    if (speed == session.channels.cend() || speed->timestamps().size() != speed->values().size()) return 0.0;
    const auto &times = speed->timestamps();
    // KAN-201: no distance is integrated across a telemetry gap.
    double meters = 0.0;
    for (const auto &interval : intervals) {
        auto index = std::distance(times.cbegin(), std::lower_bound(times.cbegin(), times.cend(), interval.start));
        for (; index + 1 < times.size() && times[index + 1] <= interval.end; ++index) {
            if (telemetryIsGap(*speed, times[index], times[index + 1])) continue;
            const double a = speed->values()[index], b = speed->values()[index + 1];
            if (std::isfinite(a) && std::isfinite(b)) meters += (a + b) / 2.0 / 3.6 * (times[index + 1] - times[index]);
        }
    }
    return meters;
}

} // namespace FlappedEar
