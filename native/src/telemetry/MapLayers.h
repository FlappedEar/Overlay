#pragma once

#include "telemetry/ChannelSummary.h"
#include "telemetry/TelemetrySession.h"
#include "telemetry/TrackGeometry.h"
#include "telemetry/TrackProgress.h"

#include <QPointF>
#include <QVector>

#include <optional>

namespace FlappedEar {

// KAN-97: a lap's racing line coloured by a value (speed, delta, G, pedals,
// a temperature). Values are sampled along the lap's progress, then placed
// on the map through the lap's own time at that progress. Nothing is
// bridged: a progress, time, position or value that is missing (a GPS gap, a
// channel gap, an implausible or placeholder sample) ends a polyline.
inline constexpr auto mapLayerAlgorithm = "map-layer-v1";

struct MapLayerPoint {
    double x = 0.0;     // normalized map position, as the lap's track
    double y = 0.0;
    double value = 0.0; // in the source unit
};

struct MapLayerTrace {
    QVector<QVector<MapLayerPoint>> polylines;
    std::optional<double> minimum, maximum;
};

// (progress meters, value) pairs; a new inner vector starts after a gap.
using ProgressValueSegments = QVector<QVector<QPointF>>;

// A channel's value at `time` from its two neighbouring samples, linearly
// interpolated. None when either neighbour is missing, non-finite, not
// plausible under `policy`, or when they are further apart than the
// channel's gap threshold.
[[nodiscard]] std::optional<double> plausibleChannelValue(const TelemetryChannel &channel, double time,
    const ChannelSummaryPolicy &policy, bool zeroPlaceholder);

// The channel (or alias) at up to `maximumPoints` evenly spaced progress
// positions over [0, axisLengthMeters] of `trace` (clamped to 2..4000).
[[nodiscard]] ProgressValueSegments channelAlongProgress(const TelemetrySession &session,
    const QString &channelOrAlias, const QVector<ProgressSegment> &trace, double axisLengthMeters,
    int maximumPoints, const ChannelSummaryPolicy &policy = {});

// Places progress/value pairs on the map at the lap's position for that
// progress, with the layer's range.
[[nodiscard]] MapLayerTrace placeOnMap(const TelemetrySession &session, const QVector<ProgressSegment> &trace,
    const TrackGeometry &geometry, const ProgressValueSegments &values);

} // namespace FlappedEar
