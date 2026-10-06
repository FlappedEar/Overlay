#include "telemetry/MapLayers.h"

#include <algorithm>
#include <cmath>

namespace FlappedEar {

std::optional<double> plausibleChannelValue(const TelemetryChannel &channel, const double time,
    const ChannelSummaryPolicy &policy, const bool zeroPlaceholder)
{
    const auto &times = channel.timestamps();
    if (!std::isfinite(time) || times.isEmpty() || times.size() != channel.values().size()) return std::nullopt;
    const auto next = std::lower_bound(times.cbegin(), times.cend(), time);
    if (next == times.cend()) return std::nullopt;
    const auto index = std::distance(times.cbegin(), next);
    const double nextValue = channel.values()[index];
    if (!plausibleSample(nextValue, policy, zeroPlaceholder)) return std::nullopt;
    if (*next == time) return nextValue;
    if (index == 0) return std::nullopt;
    const double previousTime = times[index - 1], previousValue = channel.values()[index - 1];
    if (!plausibleSample(previousValue, policy, zeroPlaceholder)) return std::nullopt;
    const double gapLimit = telemetryGapThreshold(channel);
    if (gapLimit > 0.0 && telemetryIsGap(channel, previousTime, *next)) return std::nullopt;
    const double span = *next - previousTime;
    if (!(span > 0.0)) return std::nullopt;
    return previousValue + (nextValue - previousValue) * (time - previousTime) / span;
}

ProgressValueSegments channelAlongProgress(const TelemetrySession &session, const QString &channelOrAlias,
    const QVector<ProgressSegment> &trace, const double axisLengthMeters, const int maximumPoints,
    const ChannelSummaryPolicy &policy)
{
    ProgressValueSegments result;
    const auto found = session.channels.constFind(session.aliases.value(channelOrAlias, channelOrAlias));
    if (found == session.channels.cend() || !std::isfinite(axisLengthMeters) || !(axisLengthMeters > 0.0)) return result;
    const bool zeroPlaceholder = zeroIsPlaceholder(*found, policy);
    const int points = std::clamp(maximumPoints, 2, 4000);
    QVector<QPointF> current;
    for (int index = 0; index < points; ++index) {
        const double progress = axisLengthMeters * index / (points - 1);
        const auto time = timeAtProgress(trace, progress);
        const auto value = time ? plausibleChannelValue(*found, *time, policy, zeroPlaceholder) : std::nullopt;
        if (!value) {
            if (!current.isEmpty()) result.append(std::exchange(current, {}));
            continue;
        }
        current.append({progress, *value});
    }
    if (!current.isEmpty()) result.append(current);
    return result;
}

MapLayerTrace placeOnMap(const TelemetrySession &session, const QVector<ProgressSegment> &trace,
    const TrackGeometry &geometry, const ProgressValueSegments &values)
{
    MapLayerTrace result;
    if (!geometry.valid) return result;
    for (const auto &segment : values) {
        QVector<MapLayerPoint> current;
        const auto flush = [&] {
            if (current.size() >= 2) result.polylines.append(current);
            current.clear();
        };
        for (const auto &sample : segment) {
            const auto time = timeAtProgress(trace, sample.x());
            const auto position = time ? currentTrackPoint(session, *time, geometry) : std::nullopt;
            if (!position || !std::isfinite(sample.y())) { flush(); continue; }
            current.append({position->x(), position->y(), sample.y()});
        }
        flush();
    }
    for (const auto &polyline : result.polylines) {
        for (const auto &point : polyline) {
            if (!result.minimum || point.value < *result.minimum) result.minimum = point.value;
            if (!result.maximum || point.value > *result.maximum) result.maximum = point.value;
        }
    }
    return result;
}

} // namespace FlappedEar
