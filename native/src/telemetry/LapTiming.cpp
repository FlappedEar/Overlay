#include "telemetry/LapTiming.h"

#include "telemetry/TelemetryGeometry.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <stdexcept>

namespace FlappedEar {
namespace {

struct Vector2 {
    double x = 0.0;
    double y = 0.0;
};

struct ClosestSegments {
    double distanceMeters = std::numeric_limits<double>::infinity();
    double vehicleFraction = 0.0;
    double gateFraction = 0.0;
};

struct GpsSample {
    double time = 0.0;
    Vector2 point;
};

struct PassageCluster {
    bool active = false;
    double firstTime = 0.0;
    double lastTime = 0.0;
    Vector2 firstPoint;
    Vector2 lastPoint;
    double candidateTime = 0.0;
    double candidateDistance = std::numeric_limits<double>::infinity();
    double candidateGateFraction = 0.0;
    // KAN-205 (Telemetry FET-198): the side of the gate line the pass starts
    // and ends on, and whether it reached the line within the widened span.
    int firstSide = 0;
    int lastSide = 0;
    bool reachedLineInSpan = false;
};

double dot(const Vector2 &left, const Vector2 &right)
{
    return left.x * right.x + left.y * right.y;
}

Vector2 subtract(const Vector2 &left, const Vector2 &right)
{
    return {left.x - right.x, left.y - right.y};
}

Vector2 add(const Vector2 &left, const Vector2 &right)
{
    return {left.x + right.x, left.y + right.y};
}

Vector2 multiply(const Vector2 &value, const double scale)
{
    return {value.x * scale, value.y * scale};
}

double length(const Vector2 &value)
{
    return std::hypot(value.x, value.y);
}

double cross(const Vector2 &left, const Vector2 &right)
{
    return left.x * right.y - left.y * right.x;
}

double clampedProjectionFraction(
    const Vector2 &point, const Vector2 &segmentStart, const Vector2 &segmentEnd)
{
    const Vector2 segment = subtract(segmentEnd, segmentStart);
    const double lengthSquared = dot(segment, segment);
    if (!std::isfinite(lengthSquared) || lengthSquared <= 0.0) return 0.0;
    return std::clamp(dot(subtract(point, segmentStart), segment) / lengthSquared, 0.0, 1.0);
}

ClosestSegments closestSegments(
    const Vector2 &vehicleStart,
    const Vector2 &vehicleEnd,
    const Vector2 &gateStart,
    const Vector2 &gateEnd)
{
    const Vector2 vehicle = subtract(vehicleEnd, vehicleStart);
    const Vector2 gate = subtract(gateEnd, gateStart);
    const double denominator = cross(vehicle, gate);
    if (std::isfinite(denominator) && std::abs(denominator) > 1e-12) {
        const Vector2 offset = subtract(gateStart, vehicleStart);
        const double vehicleFraction = cross(offset, gate) / denominator;
        const double gateFraction = cross(offset, vehicle) / denominator;
        if (vehicleFraction >= 0.0 && vehicleFraction <= 1.0
            && gateFraction >= 0.0 && gateFraction <= 1.0) {
            return {0.0, vehicleFraction, gateFraction};
        }
    }

    ClosestSegments best;
    const auto consider = [&best](
                              const Vector2 &first,
                              const Vector2 &second,
                              const double vehicleFraction,
                              const double gateFraction) {
        const double distance = length(subtract(first, second));
        if (std::isfinite(distance) && distance < best.distanceMeters) {
            best = {distance, vehicleFraction, gateFraction};
        }
    };

    const double gateForVehicleStart = clampedProjectionFraction(
        vehicleStart, gateStart, gateEnd);
    consider(vehicleStart, add(gateStart, multiply(gate, gateForVehicleStart)),
             0.0, gateForVehicleStart);
    const double gateForVehicleEnd = clampedProjectionFraction(vehicleEnd, gateStart, gateEnd);
    consider(vehicleEnd, add(gateStart, multiply(gate, gateForVehicleEnd)),
             1.0, gateForVehicleEnd);
    const double vehicleForGateStart = clampedProjectionFraction(
        gateStart, vehicleStart, vehicleEnd);
    consider(add(vehicleStart, multiply(vehicle, vehicleForGateStart)), gateStart,
             vehicleForGateStart, 0.0);
    const double vehicleForGateEnd = clampedProjectionFraction(gateEnd, vehicleStart, vehicleEnd);
    consider(add(vehicleStart, multiply(vehicle, vehicleForGateEnd)), gateEnd,
             vehicleForGateEnd, 1.0);
    return best;
}

void validateOptions(const LapDetectionOptions &options)
{
    const double values[] = {
        options.innerCorridorMeters,
        options.outerCorridorMeters,
        options.minimumGroundSpeedMetersPerSecond,
        options.minimumNormalSpeedMetersPerSecond,
        options.minimumNormalMotionRatio,
        options.refractorySeconds,
        options.maximumClusterSeconds,
        options.minimumGateLengthMeters,
        options.maximumGateLengthMeters,
    };
    if (std::any_of(std::begin(values), std::end(values), [](const double value) {
            return !std::isfinite(value) || value < 0.0;
        })
        || options.innerCorridorMeters > 50.0
        || options.outerCorridorMeters > 100.0
        || options.outerCorridorMeters <= options.innerCorridorMeters
        || options.minimumNormalMotionRatio > 1.0
        || options.refractorySeconds > 10.0
        || options.maximumClusterSeconds <= 0.0
        || options.minimumGateLengthMeters <= 0.0
        || options.maximumGateLengthMeters > 1'000.0
        || options.maximumGateLengthMeters < options.minimumGateLengthMeters
        || options.maximumAcceptedPasses <= 0
        || options.maximumAcceptedPasses > 100'000) {
        throw std::invalid_argument("Invalid lap-detection options.");
    }
}

std::optional<GpsSample> gpsSampleAt(
    const TelemetryChannel &latitude,
    const TelemetryChannel &longitude,
    const qsizetype index,
    const GeoCoordinate &origin)
{
    if (index < 0 || index >= latitude.timestamps.size() || index >= latitude.values.size()
        || index >= longitude.timestamps.size() || index >= longitude.values.size()) {
        return std::nullopt;
    }
    const double latitudeTime = latitude.timestamps[index];
    const double longitudeTime = longitude.timestamps[index];
    const GeoCoordinate coordinate{latitude.values[index], longitude.values[index]};
    if (!std::isfinite(latitudeTime) || latitudeTime != longitudeTime
        || !isValidCoordinate(coordinate)) {
        return std::nullopt;
    }
    const MetricPoint projected = projectCoordinate(coordinate, origin);
    if (!std::isfinite(projected.eastMeters) || !std::isfinite(projected.northMeters)) {
        return std::nullopt;
    }
    return GpsSample{latitudeTime, {projected.eastMeters, projected.northMeters}};
}

LapReferenceIssue referenceIssueForLap(const TelemetrySession &session, const TimedLap &lap,
    const GeoCoordinate &origin, const CancellationCheck &cancelled)
{
    const auto latitude = session.channels.constFind(session.aliases.value("latitude"));
    const auto longitude = session.channels.constFind(session.aliases.value("longitude"));
    if (latitude == session.channels.cend() || longitude == session.channels.cend()
        || latitude->timestamps.size() != latitude->values.size()
        || longitude->timestamps.size() != longitude->values.size()
        || latitude->timestamps.size() != longitude->timestamps.size()
        || latitude->timestamps.isEmpty()) return LapReferenceIssue::InvalidGps;
    const auto &times = latitude->timestamps;
    auto first = std::lower_bound(times.cbegin(), times.cend(), lap.startTelemetryTime);
    if (first != times.cbegin() && (first == times.cend() || *first > lap.startTelemetryTime)) --first;
    const auto last = std::lower_bound(first, times.cend(), lap.endTelemetryTime);
    if (first == times.cend() || *first > lap.startTelemetryTime || last == times.cend())
        return LapReferenceIssue::GpsGap;
    const double threshold = std::min(telemetryGapThreshold(*latitude), telemetryGapThreshold(*longitude));
    std::optional<double> previous;
    for (auto it = first; it <= last; ++it) {
        const qsizetype index = std::distance(times.cbegin(), it);
        if ((index & 0xff) == 0) throwIfCancelled(cancelled);
        const auto sample = gpsSampleAt(*latitude, *longitude, index, origin);
        if (!sample) return LapReferenceIssue::InvalidGps;
        if (previous && (sample->time <= *previous || threshold <= 0.0 || sample->time - *previous > threshold))
            return LapReferenceIssue::GpsGap;
        previous = sample->time;
    }
    return LapReferenceIssue::None;
}

void deriveTimedLaps(const TelemetrySession &session, LapSession &result,
    const GeoCoordinate &origin, const CancellationCheck &cancelled)
{
    for (qsizetype index = 1; index < result.acceptedPasses.size(); ++index) {
        const double start = result.acceptedPasses[index - 1].telemetryTime;
        const double end = result.acceptedPasses[index].telemetryTime;
        const double duration = end - start;
        if (!std::isfinite(duration) || duration <= 0.0) {
            ++result.diagnostics.invalidLapDurations;
            continue;
        }
        result.timedLaps.append({
            static_cast<int>(result.timedLaps.size() + 1), start, end, duration, 0.0});
        auto &lap = result.timedLaps.last();
        lap.referenceIssue = referenceIssueForLap(session, lap, origin, cancelled);
    }
    recomputeLapRanking(result);
}

void buildLapTraces(
    const TelemetrySession &session,
    LapSession &result,
    const GeoCoordinate &origin,
    const CancellationCheck &cancelled)
{
    const auto latitude = session.channels.constFind(session.aliases.value("latitude"));
    const auto longitude = session.channels.constFind(session.aliases.value("longitude"));
    if (latitude == session.channels.cend() || longitude == session.channels.cend()
        || latitude->timestamps.size() != latitude->values.size()
        || longitude->timestamps.size() != longitude->values.size()) {
        return;
    }

    constexpr qsizetype MaximumTracePoints = 700'000;
    constexpr qsizetype MaximumPointsPerLapTrace = 4'096;
    qsizetype totalPoints = 0;
    const auto appendCoordinate = [&](LapTrace &trace, const double time) {
        const auto latitudeValue = session.valueAt("latitude", time, InterpolationMode::Linear);
        const auto longitudeValue = session.valueAt("longitude", time, InterpolationMode::Linear);
        if (!latitudeValue || !longitudeValue) return;
        const GeoCoordinate coordinate{*latitudeValue, *longitudeValue};
        if (!isValidCoordinate(coordinate)) return;
        const MetricPoint point = projectCoordinate(coordinate, origin);
        if (!std::isfinite(point.eastMeters) || !std::isfinite(point.northMeters)) return;
        if (!trace.points.isEmpty()
            && std::abs(trace.points.constLast().telemetryTime - time) <= 1e-9) {
            return;
        }
        if (totalPoints >= MaximumTracePoints) {
            throw ResourceLimitError("Lap traces contain too many GPS points.");
        }
        trace.points.append({time, point.eastMeters, point.northMeters});
        ++totalPoints;
    };

    result.lapTraces.reserve(result.timedLaps.size());
    for (const TimedLap &lap : result.timedLaps) {
        throwIfCancelled(cancelled);
        if (!lap.referenceEligible()) continue;
        LapTrace trace{lap.number, lap.startTelemetryTime, lap.durationSeconds, {}};
        appendCoordinate(trace, lap.startTelemetryTime);
        auto latitudeTime = std::upper_bound(
            latitude->timestamps.cbegin(), latitude->timestamps.cend(), lap.startTelemetryTime);
        const auto latitudeEnd = std::lower_bound(
            latitudeTime, latitude->timestamps.cend(), lap.endTelemetryTime);
        const qsizetype rawPointCount = std::distance(latitudeTime, latitudeEnd);
        const qsizetype stride = std::max<qsizetype>(
            1, (rawPointCount + MaximumPointsPerLapTrace - 3)
                   / (MaximumPointsPerLapTrace - 2));
        qsizetype ordinal = 0;
        for (; latitudeTime != latitude->timestamps.cend() && *latitudeTime < lap.endTelemetryTime;
             ++latitudeTime, ++ordinal) {
            const qsizetype index = std::distance(latitude->timestamps.cbegin(), latitudeTime);
            if ((index & 0xff) == 0) throwIfCancelled(cancelled);
            if (ordinal % stride != 0) continue;
            if (index >= longitude->timestamps.size()
                || latitude->timestamps[index] != longitude->timestamps[index]) {
                continue;
            }
            const GeoCoordinate coordinate{latitude->values[index], longitude->values[index]};
            if (!isValidCoordinate(coordinate)) continue;
            const MetricPoint point = projectCoordinate(coordinate, origin);
            if (!std::isfinite(point.eastMeters) || !std::isfinite(point.northMeters)) continue;
            if (totalPoints >= MaximumTracePoints) {
                throw ResourceLimitError("Lap traces contain too many GPS points.");
            }
            trace.points.append({*latitudeTime, point.eastMeters, point.northMeters});
            ++totalPoints;
        }
        appendCoordinate(trace, lap.endTelemetryTime);
        if (trace.points.size() >= 2) result.lapTraces.append(std::move(trace));
    }
}

} // namespace

QVector<qsizetype> eligibleLapIndices(const LapSession &session)
{
    QVector<qsizetype> indices;
    for (qsizetype i = 0; i < session.timedLaps.size(); ++i)
        if (session.timedLaps[i].referenceEligible()) indices.append(i);
    return indices;
}

void recomputeLapRanking(LapSession &session)
{
    session.fastestLapIndex.reset();
    for (auto &lap : session.timedLaps) lap.deltaToBestSeconds = 0;
    for (const auto i : eligibleLapIndices(session)) {
        if (!session.fastestLapIndex || session.timedLaps[i].durationSeconds
            < session.timedLaps[*session.fastestLapIndex].durationSeconds) session.fastestLapIndex = i;
    }
    if (!session.fastestLapIndex) return;
    const auto best = session.timedLaps[*session.fastestLapIndex].durationSeconds;
    for (auto &lap : session.timedLaps)
        if (lap.referenceEligible()) lap.deltaToBestSeconds = lap.durationSeconds - best;
}

LapSession detectLaps(
    const TelemetrySession &session,
    const TimingGate &startGate,
    const LapDetectionOptions &options,
    const CancellationCheck &cancelled)
{
    validateOptions(options);
    throwIfCancelled(cancelled);
    LapSession result;
    result.selectedStartGate = startGate;

    const GeoCoordinate origin{
        (startGate.endpointA.latitudeDegrees + startGate.endpointB.latitudeDegrees) / 2.0,
        (startGate.endpointA.longitudeDegrees + startGate.endpointB.longitudeDegrees) / 2.0,
    };
    if (!isValidCoordinate(startGate.endpointA) || !isValidCoordinate(startGate.endpointB)
        || !isValidCoordinate(origin)) {
        result.status = LapSessionStatus::InvalidGate;
        return result;
    }
    const MetricPoint projectedGateA = projectCoordinate(startGate.endpointA, origin);
    const MetricPoint projectedGateB = projectCoordinate(startGate.endpointB, origin);
    const Vector2 gateA{projectedGateA.eastMeters, projectedGateA.northMeters};
    const Vector2 gateB{projectedGateB.eastMeters, projectedGateB.northMeters};
    const Vector2 gateVector = subtract(gateB, gateA);
    const double gateLength = length(gateVector);
    if (!std::isfinite(gateLength) || gateLength < options.minimumGateLengthMeters
        || gateLength > options.maximumGateLengthMeters) {
        result.status = LapSessionStatus::InvalidGate;
        return result;
    }
    const Vector2 gateNormal{-gateVector.y / gateLength, gateVector.x / gateLength};
    const auto signedDistance = [&](const Vector2 &point) {
        return dot(subtract(point, gateA), gateNormal);
    };
    const auto sideOf = [&](const Vector2 &point) {
        const double distance = signedDistance(point);
        return distance > 0.0 ? 1 : (distance < 0.0 ? -1 : 0);
    };
    const double spanTolerance = options.innerCorridorMeters / gateLength;
    // Whether the segment touches or crosses the gate line, and does so within
    // the gate span widened by the inner corridor at each end.
    const auto reachesLineInSpan = [&](const Vector2 &start, const Vector2 &end) {
        const double startDistance = signedDistance(start);
        const double endDistance = signedDistance(end);
        if (startDistance == 0.0) return false;
        if (endDistance != 0.0 && (endDistance > 0.0) == (startDistance > 0.0)) return false;
        const double fraction = startDistance / (startDistance - endDistance);
        const Vector2 point = add(start, multiply(subtract(end, start), fraction));
        const double along = dot(subtract(point, gateA), gateVector) / (gateLength * gateLength);
        return std::isfinite(along) && along >= -spanTolerance && along <= 1.0 + spanTolerance;
    };

    const auto latitude = session.channels.constFind(session.aliases.value("latitude"));
    const auto longitude = session.channels.constFind(session.aliases.value("longitude"));
    if (latitude == session.channels.cend() || longitude == session.channels.cend()) {
        result.status = LapSessionStatus::NoUsableGps;
        return result;
    }
    const qsizetype sampleCount = std::max(
        std::max(latitude->timestamps.size(), latitude->values.size()),
        std::max(longitude->timestamps.size(), longitude->values.size()));
    const double gapThreshold = std::max(
        telemetryGapThreshold(*latitude), telemetryGapThreshold(*longitude));
    std::optional<GpsSample> previous;
    PassageCluster cluster;
    bool armed = false;
    int acceptedDirection = 0;
    std::optional<double> lastAcceptedTime;

    const auto discardContinuity = [&] {
        if (cluster.active) ++result.diagnostics.discardedGapClusters;
        cluster = {};
        previous.reset();
        armed = false;
    };

    const auto finalizeCluster = [&] {
        if (!cluster.active) return;
        ++result.diagnostics.candidateClusters;
        const double duration = cluster.lastTime - cluster.firstTime;
        const Vector2 displacement = subtract(cluster.lastPoint, cluster.firstPoint);
        const double groundSpeed = duration > 0.0 ? length(displacement) / duration : 0.0;
        const double normalSpeed = duration > 0.0 ? dot(displacement, gateNormal) / duration : 0.0;
        const double normalRatio = groundSpeed > 0.0 ? std::abs(normalSpeed) / groundSpeed : 0.0;
        if (!std::isfinite(duration) || duration <= 0.0
            || duration > options.maximumClusterSeconds) {
            ++result.diagnostics.rejectedLongClusters;
        } else if (cluster.firstSide == 0 || cluster.lastSide == cluster.firstSide
                   || !cluster.reachedLineInSpan) {
            ++result.diagnostics.rejectedNotCrossingClusters;
        } else if (!std::isfinite(groundSpeed)
                   || groundSpeed < options.minimumGroundSpeedMetersPerSecond) {
            ++result.diagnostics.rejectedSlowClusters;
        } else if (!std::isfinite(normalSpeed)
                   || std::abs(normalSpeed) < options.minimumNormalSpeedMetersPerSecond
                   || !std::isfinite(normalRatio)
                   || normalRatio < options.minimumNormalMotionRatio) {
            ++result.diagnostics.rejectedParallelClusters;
        } else {
            const int direction = normalSpeed > 0.0 ? 1 : -1;
            if (acceptedDirection != 0 && direction != acceptedDirection) {
                ++result.diagnostics.rejectedOppositeDirectionClusters;
            } else {
                if (acceptedDirection == 0) acceptedDirection = direction;
                if (result.acceptedPasses.size() >= options.maximumAcceptedPasses) {
                    throw ResourceLimitError("Lap detector produced too many accepted passes.");
                }
                result.acceptedPasses.append({
                    cluster.candidateTime,
                    cluster.candidateDistance,
                    direction,
                    cluster.candidateGateFraction,
                    groundSpeed,
                    normalSpeed,
                });
                lastAcceptedTime = cluster.candidateTime;
            }
        }
        cluster = {};
        armed = false;
    };

    for (qsizetype index = 0; index < sampleCount; ++index) {
        if ((index & 0xff) == 0) throwIfCancelled(cancelled);
        const auto current = gpsSampleAt(*latitude, *longitude, index, origin);
        if (!current) {
            discardContinuity();
            continue;
        }
        if (!previous) {
            previous = current;
            continue;
        }
        const double interval = current->time - previous->time;
        if (!std::isfinite(interval) || interval <= 0.0 || gapThreshold <= 0.0
            || interval > gapThreshold) {
            discardContinuity();
            previous = current;
            continue;
        }
        ++result.diagnostics.usableGpsSegments;
        const ClosestSegments closest = closestSegments(
            previous->point, current->point, gateA, gateB);
        if (!std::isfinite(closest.distanceMeters)) {
            discardContinuity();
            previous = current;
            continue;
        }
        const bool outsideOuter = closest.distanceMeters > options.outerCorridorMeters;
        if (cluster.active && outsideOuter) {
            finalizeCluster();
        }
        if (!cluster.active && outsideOuter) {
            const bool refractoryComplete = !lastAcceptedTime
                || current->time - *lastAcceptedTime >= options.refractorySeconds;
            if (refractoryComplete) armed = true;
        }
        if (armed && !cluster.active
            && closest.distanceMeters <= options.innerCorridorMeters) {
            cluster.active = true;
            cluster.firstTime = previous->time;
            cluster.lastTime = current->time;
            cluster.firstPoint = previous->point;
            cluster.lastPoint = current->point;
            cluster.candidateTime = previous->time + closest.vehicleFraction * interval;
            cluster.candidateDistance = closest.distanceMeters;
            cluster.candidateGateFraction = closest.gateFraction;
            cluster.firstSide = sideOf(previous->point);
            cluster.lastSide = sideOf(current->point);
            cluster.reachedLineInSpan = reachesLineInSpan(previous->point, current->point);
        } else if (cluster.active && !outsideOuter) {
            cluster.lastTime = current->time;
            cluster.lastPoint = current->point;
            cluster.lastSide = sideOf(current->point);
            if (reachesLineInSpan(previous->point, current->point))
                cluster.reachedLineInSpan = true;
            if (closest.distanceMeters < cluster.candidateDistance) {
                cluster.candidateTime = previous->time + closest.vehicleFraction * interval;
                cluster.candidateDistance = closest.distanceMeters;
                cluster.candidateGateFraction = closest.gateFraction;
            }
        }
        previous = current;
    }
    finalizeCluster();
    throwIfCancelled(cancelled);
    if (result.diagnostics.usableGpsSegments == 0) {
        result.status = LapSessionStatus::NoUsableGps;
        return result;
    }
    deriveTimedLaps(session, result, origin, cancelled);
    if (result.acceptedPasses.isEmpty()) {
        result.status = LapSessionStatus::NoAcceptedPasses;
    } else if (result.timedLaps.isEmpty()) {
        result.status = LapSessionStatus::InsufficientPasses;
    } else {
        buildLapTraces(session, result, origin, cancelled);
        result.status = LapSessionStatus::Available;
    }
    return result;
}

QString timingGateRevision(const TelemetrySession &session, const CancellationCheck &cancelled)
{
    throwIfCancelled(cancelled);
    if (session.timingGates.size() > 128) return {};
    QJsonArray gates;
    int startCount = 0;
    const bool west = session.metadata.value("gpsLongitudeConvention") == "west-positive";
    for (const auto &gate : session.timingGates) {
        throwIfCancelled(cancelled);
        if (!isValidCoordinate(gate.endpointA) || !isValidCoordinate(gate.endpointB)
            || gate.type == TimingGateType::Unknown) return {};
        startCount += gate.type == TimingGateType::Start ? 1 : 0;
        const auto longitude = [west](double value) { return west ? -value : value; };
        gates.append(QJsonArray{gate.type == TimingGateType::Start ? "start" : "split",
            gate.endpointA.latitudeDegrees, longitude(gate.endpointA.longitudeDegrees),
            gate.endpointB.latitudeDegrees, longitude(gate.endpointB.longitudeDegrees)});
    }
    if (startCount != 1) return {};
    return "gates-v1:" + QString::fromLatin1(QCryptographicHash::hash(
        QJsonDocument(gates).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex());
}

LapSession deriveSourceLapSession(
    const TelemetrySession &session,
    const LapDetectionOptions &options,
    const CancellationCheck &cancelled)
{
    throwIfCancelled(cancelled);
    const TimingGate *startGate = nullptr;
    for (const TimingGate &gate : session.timingGates) {
        if (gate.type != TimingGateType::Start) continue;
        if (startGate) {
            LapSession result;
            result.status = LapSessionStatus::AmbiguousSourceStartGate;
            return result;
        }
        startGate = &gate;
    }
    if (!startGate) {
        LapSession result;
        result.status = LapSessionStatus::NoSourceStartGate;
        return result;
    }
    return detectLaps(session, *startGate, options, cancelled);
}

QString formatLapTime(const double seconds, const int decimals)
{
    if (!std::isfinite(seconds) || seconds < 0.0) return {};
    const int places = std::clamp(decimals, 0, 3);
    const qint64 unitsPerSecond = places == 0 ? 1 : places == 1 ? 10 : places == 2 ? 100 : 1000;
    // Bounded well below qint64 for any lap time; very long times still format.
    const double scaled = std::min(seconds * static_cast<double>(unitsPerSecond), 9.0e15);
    const qint64 total = std::llround(scaled);
    const qint64 perMinute = 60 * unitsPerSecond;
    const qint64 minutes = total / perMinute, rest = total % perMinute;
    QString text = QStringLiteral("%1:%2").arg(minutes).arg(rest / unitsPerSecond, 2, 10, QLatin1Char('0'));
    if (places > 0) text += QStringLiteral(".%1").arg(rest % unitsPerSecond, places, 10, QLatin1Char('0'));
    return text;
}

QString formatElapsedTime(const double seconds)
{
    if (!std::isfinite(seconds)) return QStringLiteral("—");
    const auto milliseconds = static_cast<qint64>(std::llround(std::abs(seconds) * 1000.0));
    const QString sign = seconds < 0 && milliseconds > 0 ? QStringLiteral("-") : QString();
    if (milliseconds < 60'000)
        return sign + QStringLiteral("%1.%2 s").arg(milliseconds / 1000).arg(milliseconds % 1000, 3, 10, QLatin1Char('0'));
    return sign + QStringLiteral("%1:%2.%3").arg(milliseconds / 60'000).arg(milliseconds / 1000 % 60, 2, 10, QLatin1Char('0'))
        .arg(milliseconds % 1000, 3, 10, QLatin1Char('0'));
}

} // namespace FlappedEar
