#include "telemetry/GgPairs.h"

#include <algorithm>
#include <cmath>
#include <optional>

namespace FlappedEar {

namespace {
// Factor to g, or nullopt for a unit that cannot be interpreted.
std::optional<double> toGFactor(const QString &unit)
{
    const auto normalized = unit.trimmed().toLower().remove(' ');
    if (normalized.isEmpty() || normalized == QLatin1String("g")) return 1.0;
    if (normalized == QLatin1String("m/s2") || normalized == QLatin1String("m/s^2") || normalized == QStringLiteral("m/s²"))
        return 1.0 / standardGravity;
    return std::nullopt;
}
}

GgPairs buildGgPairs(const TelemetrySession &session, const double startTime, const double endTime)
{
    GgPairs result;
    result.longitudinalChannel = session.aliases.value("longitudinalAcceleration");
    result.lateralChannel = session.aliases.value("lateralAcceleration");
    const auto longitudinal = session.channels.constFind(result.longitudinalChannel);
    const auto lateral = session.channels.constFind(result.lateralChannel);
    if (result.longitudinalChannel.isEmpty() || longitudinal == session.channels.cend()) {
        result.unavailableReason = ggMissingLongitudinal;
        return result;
    }
    if (result.lateralChannel.isEmpty() || lateral == session.channels.cend()) {
        result.unavailableReason = ggMissingLateral;
        return result;
    }
    result.longitudinalUnit = longitudinal->unit;
    result.lateralUnit = lateral->unit;
    result.unitsDeclared = !longitudinal->unit.trimmed().isEmpty() && !lateral->unit.trimmed().isEmpty();
    const auto longitudinalFactor = toGFactor(longitudinal->unit);
    const auto lateralFactor = toGFactor(lateral->unit);
    if (!longitudinalFactor || !lateralFactor) {
        result.unavailableReason = ggUnsupportedUnit;
        return result;
    }
    if (!std::isfinite(startTime) || !std::isfinite(endTime) || endTime <= startTime
        || longitudinal->timestamps.size() != longitudinal->values.size()
        || lateral->timestamps.size() != lateral->values.size() || lateral->timestamps.isEmpty()) {
        result.unavailableReason = ggNoOverlap;
        return result;
    }
    result.valid = true;
    const auto &lateralTimes = lateral->timestamps;
    const double gapLimit = telemetryGapThreshold(*lateral);
    bool shared = true;
    for (qsizetype i = 0; i < longitudinal->timestamps.size(); ++i) {
        const double time = longitudinal->timestamps[i];
        if (time < startTime || time > endTime) continue;
        const double longitudinalValue = longitudinal->values[i];
        if (!std::isfinite(longitudinalValue)) continue;
        ++result.candidateCount;
        const auto next = std::lower_bound(lateralTimes.cbegin(), lateralTimes.cend(), time);
        const qsizetype nextIndex = std::distance(lateralTimes.cbegin(), next);
        std::optional<double> lateralValue;
        double offset = 0.0;
        if (next != lateralTimes.cend() && *next == time) {
            if (std::isfinite(lateral->values[nextIndex])) lateralValue = lateral->values[nextIndex];
        } else {
            shared = false;
            if (nextIndex > 0 && nextIndex < lateralTimes.size()) {
                const double before = lateralTimes[nextIndex - 1], after = lateralTimes[nextIndex];
                const double a = lateral->values[nextIndex - 1], b = lateral->values[nextIndex];
                if (after - before <= gapLimit && std::isfinite(a) && std::isfinite(b)) {
                    lateralValue = a + (b - a) * (time - before) / (after - before);
                    offset = std::min(time - before, after - time);
                }
            }
        }
        if (!lateralValue) { ++result.skippedForGap; continue; }
        const double longitudinalG = longitudinalValue * *longitudinalFactor;
        const double lateralG = *lateralValue * *lateralFactor;
        if (!std::isfinite(longitudinalG) || !std::isfinite(lateralG)
            || std::abs(longitudinalG) > ggPlausibleLimitG || std::abs(lateralG) > ggPlausibleLimitG) {
            ++result.excludedOutliers;
            continue;
        }
        result.maximumPairingOffsetSeconds = std::max(result.maximumPairingOffsetSeconds, offset);
        result.points.append({time, longitudinalG, lateralG});
    }
    result.sharedClock = shared && result.candidateCount > 0;
    if (result.candidateCount == 0) result.unavailableReason = ggNoOverlap;
    return result;
}

GgPeaks computeGgPeaks(const QVector<GgPoint> &points)
{
    GgPeaks peaks;
    peaks.sampleCount = points.size();
    const auto consider = [](std::optional<GgPeak> &peak, const double value, const GgPoint &point) {
        if (value > 0.0 && (!peak || value > peak->value)) peak = GgPeak{value, point};
    };
    for (const auto &point : points) {
        consider(peaks.lateral, std::abs(point.lateralG), point);
        consider(peaks.braking, -point.longitudinalG, point);
        consider(peaks.acceleration, point.longitudinalG, point);
        consider(peaks.combined, std::hypot(point.longitudinalG, point.lateralG), point);
    }
    return peaks;
}

QVector<GgPoint> decimateGgPoints(const QVector<GgPoint> &points, const GgPeaks &peaks, const qsizetype maximumPoints)
{
    if (maximumPoints <= 0 || points.size() <= maximumPoints) return points;
    QVector<GgPoint> result;
    result.reserve(maximumPoints + 4);
    const double step = static_cast<double>(points.size()) / static_cast<double>(maximumPoints);
    for (qsizetype k = 0; k < maximumPoints; ++k) result.append(points[static_cast<qsizetype>(k * step)]);
    for (const auto *peak : {&peaks.lateral, &peaks.braking, &peaks.acceleration, &peaks.combined})
        if (*peak) result.append((*peak)->point);
    return result;
}

} // namespace FlappedEar
