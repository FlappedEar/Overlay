#pragma once

#include <optional>

namespace FlappedEar {

enum class CoordinateAxis { Latitude, Longitude };
enum class CoordinateUnit { Degrees, ArcMinutes };

struct GeoCoordinate {
    double latitudeDegrees = 0.0;
    double longitudeDegrees = 0.0;
};

struct MetricPoint {
    double eastMeters = 0.0;
    double northMeters = 0.0;
};

[[nodiscard]] std::optional<double> normalizeCoordinateDegrees(
    CoordinateAxis axis, double value, CoordinateUnit unit);
[[nodiscard]] bool isValidCoordinate(const GeoCoordinate &coordinate);
[[nodiscard]] MetricPoint projectCoordinate(
    const GeoCoordinate &coordinate, const GeoCoordinate &origin);
// Longitudes the short way round across ±180° (KAN-235, Telemetry FET-213).
// Values already inside -180..180 are returned unchanged, bit for bit.
[[nodiscard]] double wrapLongitudeDegrees(double longitudeDegrees);
[[nodiscard]] double longitudeDeltaDegrees(double toDegrees, double fromDegrees);
[[nodiscard]] GeoCoordinate geoMidpoint(const GeoCoordinate &a, const GeoCoordinate &b);

} // namespace FlappedEar
