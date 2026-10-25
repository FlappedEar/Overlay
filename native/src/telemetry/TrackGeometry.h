#pragma once

#include "telemetry/TelemetrySession.h"
#include "telemetry/SourceOperation.h"

#include <QPointF>
#include <QRectF>
#include <QVariant>
#include <QVector>
#include <optional>

namespace FlappedEar {

struct TrackGeometry {
    QVector<QPointF> points;
    QRectF localBounds;
    QPointF localCenter;
    double normalizationScale = 1.0;
    double originLatitude = 0.0;
    double originLongitude = 0.0;
    // Origin remains in source coordinates; map x is always positive east.
    bool longitudeIsWestPositive = false;
    bool valid = false;
};

[[nodiscard]] TrackGeometry buildTrackGeometry(
    const TelemetrySession &session, const CancellationCheck &cancelled = {});
[[nodiscard]] std::optional<QPointF> currentTrackPoint(
    const TelemetrySession &session, double time, const TrackGeometry &geometry);
// A coordinate in the geometry's normalized map space (the space of `points`);
// empty for an invalid coordinate or a geometry without a valid frame.
[[nodiscard]] std::optional<QPointF> trackPointForCoordinate(
    const GeoCoordinate &coordinate, const TrackGeometry &geometry);

// One normalization shared by two laps' GPS traces, so both can be drawn
// overlaid on the same map to scale instead of each filling the frame on
// its own independent bounding box.
[[nodiscard]] TrackGeometry buildSharedTrackGeometry(
    const TelemetrySession &sessionA, double startA, double endA,
    const TelemetrySession &sessionB, double startB, double endB,
    const CancellationCheck &cancelled = {});

// Gap-preserving polyline segments for one lap's GPS trace, normalized
// against an externally supplied geometry (e.g. buildSharedTrackGeometry's
// result) rather than one computed from this lap alone.
[[nodiscard]] QVariantList buildTrackSegments(
    const TelemetrySession &session, double startTime, double endTime,
    const TrackGeometry &geometry, const CancellationCheck &cancelled = {});

} // namespace FlappedEar
