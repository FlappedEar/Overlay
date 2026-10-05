#include "telemetry/CornerSpeeds.h"

#include "telemetry/SectorTiming.h"

#include <QJsonObject>

#include <algorithm>
#include <cmath>
#include <iterator>

namespace FlappedEar {
namespace {

CornerSpeedValue unavailable(const double progress, const QString &reason)
{
    CornerSpeedValue value;
    value.progressMeters = progress;
    value.unavailableReason = reason;
    return value;
}

// The recorded speed at a progress value of this lap; never bridged across a gap.
CornerSpeedValue speedAt(const QVector<ProgressSegment> &lap, const TelemetrySession &session, const QString &channel,
    const double progress)
{
    const auto time = timeAtProgress(lap, progress);
    if (!time) return unavailable(progress, cornerPhaseIncompleteCoverage);
    const auto speed = session.valueAt(channel, *time);
    if (!speed) return unavailable(progress, cornerPhaseIncompleteCoverage);
    CornerSpeedValue value;
    value.value = speed;
    value.progressMeters = progress;
    value.telemetryTime = time;
    return value;
}

} // namespace

CornerSpeeds computeCornerSpeeds(const ProgressAxis &axis, const TrackFeatures &features,
    const ApprovedSegmentation &approved, const QString &segmentId, const QVector<ProgressSegment> &lapTrace,
    const TelemetrySession &session)
{
    CornerSpeeds result;
    if (!axis.valid || !features.valid || !approved.valid) return result;
    QJsonObject segment;
    for (const auto &value : approved.segments)
        if (value.toObject().value("id").toString() == segmentId) segment = value.toObject();
    if (segment.isEmpty()) return result;

    const double length = axis.lengthMeters;
    const double start = segment.value("startProgressMeters").toDouble();
    const double end = segment.value("endProgressMeters").toDouble();
    result.segmentId = segmentId;
    result.name = segment.value("name").toString();
    result.type = segment.value("type").toString();
    result.stamp = segmentationResultStamp(approved, cornerSpeedsAlgorithm);
    result.lengthMeters = end >= start ? end - start : end + length - start;
    result.valid = true;

    const QString channel = session.aliases.value("speed");
    const auto channelIt = session.channels.constFind(channel);
    if (channel.isEmpty() || channelIt == session.channels.cend()) {
        result.provenance = QStringLiteral("unavailable");
        for (auto *value : {&result.entry, &result.apex, &result.minimum, &result.exit})
            *value = unavailable(0.0, cornerPhaseSpeedChannelMissing);
        result.entry.progressMeters = start;
        result.exit.progressMeters = end;
        return result;
    }
    result.channel = channel;
    result.unit = channelIt->unit;
    result.provenance = QStringLiteral("measured");

    if (end < start) {
        // The corner's two halves lie at opposite ends of a gate-to-gate lap.
        result.coveredMeters = projectedCoverageMeters(lapTrace, start, length, length)
            + projectedCoverageMeters(lapTrace, 0.0, end, length);
        for (auto *value : {&result.entry, &result.apex, &result.minimum, &result.exit})
            *value = unavailable(0.0, cornerPhaseCrossesGate);
        result.entry.progressMeters = start;
        result.exit.progressMeters = end;
        return result;
    }
    result.coveredMeters = projectedCoverageMeters(lapTrace, start, end, length);
    result.entry = speedAt(lapTrace, session, channel, start);
    result.exit = speedAt(lapTrace, session, channel, end);

    const auto corner = cornerFromSegment(axis, features, segment);
    if (result.type != trackSegmentTypeName(TrackSegmentType::Corner)) {
        result.apex = unavailable(0.0, cornerSpeedNotACorner);
    } else {
        const auto phases = proposeCornerGeometryPhases(axis, features, corner);
        if (!phases.valid) {
            result.apex = unavailable(0.0, cornerPhaseInvalidInput);
        } else if (!phases.apex.resolved()) {
            result.apex = unavailable(0.0, phases.apex.unresolvedReason);
        } else {
            result.apex = speedAt(lapTrace, session, channel, phases.apex.progressMeters);
            result.apex.limitations.append(phases.apex.uncertaintyReasons);
        }
    }

    const auto minimum = locateMinimumSpeed(axis, corner, lapTrace, session, 1.0);
    if (minimum.resolved()) {
        result.minimum.value = minimum.evidence.value("minimumValue").toDouble();
        result.minimum.progressMeters = minimum.progressMeters;
        result.minimum.telemetryTime = minimum.evidence.value("telemetryTime").toDouble();
        result.minimum.limitations = minimum.uncertaintyReasons;
    } else {
        result.minimum = unavailable(0.0, minimum.unresolvedReason);
    }

    // Sample density of the recorded channel between the boundary crossings.
    if (result.entry.telemetryTime && result.exit.telemetryTime && *result.exit.telemetryTime > *result.entry.telemetryTime) {
        const auto &times = channelIt->timestamps;
        const auto from = std::lower_bound(times.cbegin(), times.cend(), *result.entry.telemetryTime);
        const auto to = std::upper_bound(times.cbegin(), times.cend(), *result.exit.telemetryTime);
        const auto samples = std::distance(from, to);
        result.meanSampleSpacingMeters = samples >= 2 ? result.lengthMeters / double(samples - 1) : result.lengthMeters;
        if (result.meanSampleSpacingMeters > sparseSampleSpacingMeters) {
            for (auto *value : {&result.entry, &result.apex, &result.minimum, &result.exit})
                if (value->value) value->limitations.append(cornerSpeedSparseSamples);
        }
    }
    return result;
}

CornerSpeedsComparison compareCornerSpeeds(const CornerSpeeds &a, const CornerSpeeds &b)
{
    CornerSpeedsComparison comparison;
    if (!a.valid || !b.valid || a.segmentId != b.segmentId || a.stamp.revision != b.stamp.revision
        || a.stamp.trackConfigurationReference != b.stamp.trackConfigurationReference) {
        comparison.unavailableReason = QString::fromLatin1(cornerSpeedDifferentSegmentOrRevision);
        return comparison;
    }
    if (a.provenance != b.provenance || a.channel != b.channel || a.unit != b.unit) {
        comparison.unavailableReason = QString::fromLatin1(cornerSpeedMixedProvenance);
        return comparison;
    }
    comparison.valid = true;
    const auto delta = [](const CornerSpeedValue &x, const CornerSpeedValue &y) -> std::optional<double> {
        return x.value && y.value ? std::optional<double>(*x.value - *y.value) : std::nullopt;
    };
    comparison.entryDelta = delta(a.entry, b.entry);
    comparison.apexDelta = delta(a.apex, b.apex);
    comparison.minimumDelta = delta(a.minimum, b.minimum);
    comparison.exitDelta = delta(a.exit, b.exit);
    return comparison;
}

} // namespace FlappedEar
