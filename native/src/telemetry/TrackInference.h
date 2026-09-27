#pragma once

#include "telemetry/LapTiming.h"
#include <QJsonObject>
#include <QHash>
#include <QSet>

namespace FlappedEar {

inline constexpr auto trackInferenceVersion = "gps-route-v1";

// Ephemeral, spatially resampled complete-lap geometry. Never serialized.
struct RouteShape {
    GeoCoordinate origin;
    QVector<QPointF> points;
    double lengthMeters = 0;
    QString direction;
};

struct TrackInference {
    RouteShape route;
    QSet<int> matchingLaps;
    QString reason;
    [[nodiscard]] bool supported() const { return !route.points.isEmpty(); }
};

[[nodiscard]] bool routesMatch(const RouteShape &a, const RouteShape &b,
    const CancellationCheck &cancelled = {});
// KAN-137: a lap that leaves the line every other lap of its run took --
// off track, a detour, the pit lane -- is not on the supported route, even
// when its overall shape matches. A lap's line deviation is the largest
// distance from any of its GPS points to the nearest path of any other of
// `lapNumbers` (capped at maximumLineDeviationMeters + 4). Needs at least
// three laps: with fewer there is no line to compare against.
inline constexpr double maximumLineDeviationMeters = 12.0;
[[nodiscard]] QHash<int, double> lapLineDeviations(const QVector<LapTrace> &traces, const QSet<int> &lapNumbers,
    const CancellationCheck &cancelled = {});

[[nodiscard]] TrackInference inferTrack(const LapSession &laps, bool longitudeIsWestPositive = false,
    const CancellationCheck &cancelled = {});
[[nodiscard]] bool manualTrackConfiguration(const QJsonObject &configuration);

struct InferredTrackGroups {
    QHash<QString, QJsonObject> configurations;
    QHash<QString, QJsonObject> provenance;
    QHash<QString, QString> reasons;
};

// Complete-link spatial groups: a near match cannot bridge incompatible routes.
// Saved IDs are reusable only for the same algorithm, full content and gates.
[[nodiscard]] InferredTrackGroups groupInferredTracks(
    const QHash<QString, TrackInference> &inferences, const QJsonArray &sources,
    const CancellationCheck &cancelled = {});

} // namespace FlappedEar
