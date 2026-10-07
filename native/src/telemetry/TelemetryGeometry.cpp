#include "telemetry/TelemetryGeometry.h"

#include <cmath>
#include <numbers>

namespace FlappedEar {

std::optional<double> normalizeCoordinateDegrees(const CoordinateAxis axis, double value,
                                                  const CoordinateUnit unit)
{
    if (!std::isfinite(value)) {
        return std::nullopt;
    }
    if (unit == CoordinateUnit::ArcMinutes) value /= 60.0;
    const double limit = axis == CoordinateAxis::Latitude ? 90.0 : 180.0;
    return std::abs(value) <= limit ? std::optional<double>(value) : std::nullopt;
}

bool isValidCoordinate(const GeoCoordinate &coordinate)
{
    return std::isfinite(coordinate.latitudeDegrees)
        && std::isfinite(coordinate.longitudeDegrees)
        && std::abs(coordinate.latitudeDegrees) <= 90.0
        && std::abs(coordinate.longitudeDegrees) <= 180.0;
}

MetricPoint projectCoordinate(const GeoCoordinate &coordinate, const GeoCoordinate &origin)
{
    constexpr double earthRadiusMeters = 6'371'000.0;
    constexpr double radiansPerDegree = std::numbers::pi / 180.0;
    return {
        longitudeDeltaDegrees(coordinate.longitudeDegrees, origin.longitudeDegrees) * radiansPerDegree
            * earthRadiusMeters * std::cos(origin.latitudeDegrees * radiansPerDegree),
        (coordinate.latitudeDegrees - origin.latitudeDegrees) * radiansPerDegree
            * earthRadiusMeters,
    };
}

double wrapLongitudeDegrees(const double longitudeDegrees)
{
    if (!std::isfinite(longitudeDegrees) || std::abs(longitudeDegrees) <= 180.0) return longitudeDegrees;
    double wrapped = std::fmod(longitudeDegrees + 180.0, 360.0);
    if (wrapped < 0.0) wrapped += 360.0;
    return wrapped - 180.0;
}

double longitudeDeltaDegrees(const double toDegrees, const double fromDegrees)
{
    return wrapLongitudeDegrees(toDegrees - fromDegrees);
}

GeoCoordinate geoMidpoint(const GeoCoordinate &a, const GeoCoordinate &b)
{
    const double latitude = (a.latitudeDegrees + b.latitudeDegrees) / 2.0;
    const double delta = b.longitudeDegrees - a.longitudeDegrees;
    if (std::abs(delta) <= 180.0) return {latitude, (a.longitudeDegrees + b.longitudeDegrees) / 2.0};
    return {latitude, wrapLongitudeDegrees(a.longitudeDegrees + wrapLongitudeDegrees(delta) / 2.0)};
}

} // namespace FlappedEar
