#include "telemetry/TrackProgress.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace FlappedEar {
namespace {

double pointDistance(const QPointF &a, const QPointF &b) { return std::hypot(a.x() - b.x(), a.y() - b.y()); }

// A projection may step back this far before it is distrusted.
constexpr double backwardToleranceMeters = 3.0;
// A lock older than this is not continued: the next fix is a cold start.
constexpr double maximumGapSeconds = 5.0;

double cross(const QPointF &a, const QPointF &b) { return a.x() * b.y() - a.y() * b.x(); }

struct GateCrossing {
    qsizetype edge = -1; // the path's edge from points[edge] to points[edge + 1]
    QPointF point;
};

// Where a closed path (last point == first) crosses the gate segment [a, b]:
// the crossing nearest the gate midpoint when there are several, or none.
GateCrossing gateCrossing(const QVector<QPointF> &closed, const QPointF &a, const QPointF &b)
{
    GateCrossing result;
    const QPointF gate = b - a;
    const QPointF midpoint = (a + b) / 2.0;
    double bestDistance = std::numeric_limits<double>::infinity();
    for (qsizetype i = 0; i + 1 < closed.size(); ++i) {
        const QPointF edge = closed[i + 1] - closed[i];
        const double denominator = cross(edge, gate);
        if (!(std::abs(denominator) > 1e-12)) continue; // parallel: no single crossing
        const QPointF offset = a - closed[i];
        const double alongEdge = cross(offset, gate) / denominator;
        const double alongGate = cross(offset, edge) / denominator;
        if (alongEdge < 0.0 || alongEdge >= 1.0 || alongGate < 0.0 || alongGate > 1.0) continue;
        const QPointF point = closed[i] + edge * alongEdge;
        const double distance = pointDistance(point, midpoint);
        if (distance < bestDistance) {
            bestDistance = distance;
            result = {i, point};
        }
    }
    return result;
}

// Resamples an ordered point sequence into `count` points evenly spaced by
// arc length (points[0] included, points[count] implicitly wrapping back to
// points[0] -- callers close the loop first if that is what they want).
// Mirrors TrackInference::shape()'s resampling loop; kept as its own small
// copy here rather than shared, so this module never risks perturbing that
// separately-tested route-matching code.
QVector<QPointF> resampleByArcLength(const QVector<QPointF> &points, const int count)
{
    if (points.size() < 2 || count < 1) return {};
    QVector<double> cumulative{0};
    cumulative.reserve(points.size());
    for (qsizetype i = 1; i < points.size(); ++i)
        cumulative.append(cumulative.last() + pointDistance(points[i - 1], points[i]));
    const double length = cumulative.last();
    if (!(length > 0)) return {};
    QVector<QPointF> result;
    result.reserve(count);
    qsizetype segment = 1;
    for (int i = 0; i < count; ++i) {
        const double target = length * i / count;
        while (segment + 1 < cumulative.size() && cumulative[segment] < target) ++segment;
        const double span = cumulative[segment] - cumulative[segment - 1];
        if (!(span > 0)) return {};
        result.append(points[segment - 1]
            + (points[segment] - points[segment - 1]) * ((target - cumulative[segment - 1]) / span));
    }
    return result;
}

struct SegmentProjection {
    double fraction = 0.0;
    double distance = 0.0;
};

SegmentProjection projectOntoSegment(const QPointF &point, const QPointF &a, const QPointF &b)
{
    const QPointF ab = b - a;
    const double lengthSquared = ab.x() * ab.x() + ab.y() * ab.y();
    if (!(lengthSquared > 1e-9)) return {0.0, pointDistance(point, a)};
    const QPointF ap = point - a;
    const double t = std::clamp((ap.x() * ab.x() + ap.y() * ab.y()) / lengthSquared, 0.0, 1.0);
    return {t, pointDistance(point, QPointF(a.x() + ab.x() * t, a.y() + ab.y() * t))};
}

double progressAtSegment(const ProgressAxis &axis, const int segmentIndex, const double fraction)
{
    const int n = axis.points.size();
    const int next = (segmentIndex + 1) % n;
    const double segmentLength = next == 0
        ? axis.lengthMeters - axis.cumulative[segmentIndex]
        : axis.cumulative[next] - axis.cumulative[segmentIndex];
    return axis.cumulative[segmentIndex] + segmentLength * fraction;
}

struct Candidate {
    int index = -1;
    double progressMeters = 0.0;
    double distance = std::numeric_limits<double>::infinity();
};

// Best match within `count` axis segments starting at `startIndex` (ring
// topology: indices wrap via modulo, count may exceed axis size).
Candidate bestCandidateInRange(const ProgressAxis &axis, const QPointF &point, const int startIndex, const int count)
{
    Candidate best;
    const int n = axis.points.size();
    const int bounded = std::min(count, n);
    for (int step = 0; step < bounded; ++step) {
        const int index = ((startIndex + step) % n + n) % n;
        const auto projection = projectOntoSegment(point, axis.points[index], axis.points[(index + 1) % n]);
        if (projection.distance < best.distance) {
            best.index = index;
            best.distance = projection.distance;
            best.progressMeters = progressAtSegment(axis, index, projection.fraction);
        }
    }
    return best;
}

// Best match within the same window, excluding anything within
// `minimumSeparation` axis points of `excludeIndex` -- a genuinely different
// candidate crossing, not just a neighboring segment of the same one.
Candidate secondBestCandidate(const ProgressAxis &axis, const QPointF &point, const int startIndex, const int count,
    const int excludeIndex, const int minimumSeparation)
{
    Candidate best;
    const int n = axis.points.size();
    const int bounded = std::min(count, n);
    for (int step = 0; step < bounded; ++step) {
        const int index = ((startIndex + step) % n + n) % n;
        int separation = std::abs(index - excludeIndex);
        separation = std::min(separation, n - separation);
        if (separation < minimumSeparation) continue;
        const auto projection = projectOntoSegment(point, axis.points[index], axis.points[(index + 1) % n]);
        if (projection.distance < best.distance) {
            best.index = index;
            best.distance = projection.distance;
            best.progressMeters = progressAtSegment(axis, index, projection.fraction);
        }
    }
    return best;
}

// Unit tangent of the axis at `index`, pointing toward index+1 (the
// direction of travel around the track). Returns a zero vector for a
// degenerate (repeated-point) segment, which the caller treats as "no
// heading opinion" rather than a rejection.
QPointF axisTangent(const ProgressAxis &axis, const int index)
{
    const int n = axis.points.size();
    const QPointF edge = axis.points[(index + 1) % n] - axis.points[index];
    const double length = std::hypot(edge.x(), edge.y());
    return length > 1e-6 ? edge / length : QPointF(0, 0);
}

// A candidate match is rejected when the car's actual direction of travel
// disagrees with the axis's direction there -- nearest-point distance alone
// cannot distinguish the correct branch from a nearby parallel section
// running the opposite way, or a hairpin's other side.
bool headingAgrees(const ProgressAxis &axis, const int index, const QPointF &movementDirection, const double minimumCosine)
{
    const double movementLength = std::hypot(movementDirection.x(), movementDirection.y());
    if (!(movementLength > 1e-6)) return true; // no heading evidence available: don't reject on it
    const QPointF tangent = axisTangent(axis, index);
    if (tangent.x() == 0 && tangent.y() == 0) return true; // degenerate axis segment: no opinion
    const double cosine = (movementDirection.x() * tangent.x() + movementDirection.y() * tangent.y()) / movementLength;
    return cosine >= minimumCosine;
}

// Signed shortest angular distance from `from` to `to`, in (-pi, pi] -- never
// a naive subtraction, which breaks across the +-pi wrap.
double angularDifference(const double to, const double from)
{
    return std::atan2(std::sin(to - from), std::cos(to - from));
}

} // namespace

ProgressAxis buildProgressAxis(
    const LapTrace &referenceTrace, const GeoCoordinate &origin, const TimingGate &gate,
    const CancellationCheck &cancelled)
{
    throwIfCancelled(cancelled);
    ProgressAxis axis;
    if (referenceTrace.points.size() < 12) return axis;

    QVector<QPointF> points;
    points.reserve(referenceTrace.points.size());
    for (const auto &sample : referenceTrace.points) {
        const QPointF point(sample.eastMeters, sample.northMeters);
        if (!std::isfinite(point.x()) || !std::isfinite(point.y())) return axis;
        // Drop sub-0.5m GPS jitter: fine enough to preserve ~2m target
        // spacing, coarse enough that exact-duplicate fixes don't produce a
        // degenerate zero-length resampling segment.
        if (points.isEmpty() || pointDistance(points.last(), point) >= 0.5) points.append(point);
    }
    if (points.size() < 12) return axis;
    points.append(points.first()); // close the loop

    double length = 0;
    for (qsizetype i = 1; i < points.size(); ++i) length += pointDistance(points[i - 1], points[i]);
    if (!(length >= 50 && length <= 30'000)) return axis;

    const int pointCount = std::clamp(static_cast<int>(std::lround(length / 2.0)), 32, 15'000);

    // Progress 0 is where the path crosses the timing gate (KAN-152): the
    // closed path is restarted at that crossing before it is resampled. The
    // point nearest the gate midpoint is not enough: an oblique gate whose
    // midpoint lies off the racing line puts it metres after the crossing,
    // and each lap's first fix would project just before progress 0.
    const MetricPoint gateA = projectCoordinate(gate.endpointA, origin);
    const MetricPoint gateB = projectCoordinate(gate.endpointB, origin);
    const auto crossing = gateCrossing(
        points, QPointF(gateA.eastMeters, gateA.northMeters), QPointF(gateB.eastMeters, gateB.northMeters));
    QVector<QPointF> rotated;
    if (crossing.edge >= 0) {
        const qsizetype open = points.size() - 1; // points.last() repeats points.first()
        QVector<QPointF> restarted{crossing.point};
        restarted.reserve(open + 2);
        for (qsizetype step = 1; step <= open; ++step) {
            const QPointF &point = points[(crossing.edge + step) % open];
            if (pointDistance(point, restarted.last()) > 1e-9) restarted.append(point);
        }
        if (pointDistance(crossing.point, restarted.last()) > 1e-9) restarted.append(crossing.point);
        rotated = resampleByArcLength(restarted, pointCount);
        if (rotated.size() != pointCount) return axis;
    } else {
        // The path never crosses the gate's segment: rotate so index 0 is
        // the resampled point nearest the gate midpoint.
        const QVector<QPointF> resampled = resampleByArcLength(points, pointCount);
        if (resampled.size() != pointCount) return axis;
        const GeoCoordinate gateMidpoint{
            (gate.endpointA.latitudeDegrees + gate.endpointB.latitudeDegrees) / 2,
            (gate.endpointA.longitudeDegrees + gate.endpointB.longitudeDegrees) / 2};
        const MetricPoint gateLocal = projectCoordinate(gateMidpoint, origin);
        int gateIndex = 0;
        double bestDistance = std::numeric_limits<double>::infinity();
        for (int i = 0; i < resampled.size(); ++i) {
            const double d = pointDistance(resampled[i], QPointF(gateLocal.eastMeters, gateLocal.northMeters));
            if (d < bestDistance) { bestDistance = d; gateIndex = i; }
        }
        rotated.reserve(resampled.size());
        for (int i = 0; i < resampled.size(); ++i) rotated.append(resampled[(gateIndex + i) % resampled.size()]);
    }

    axis.points = std::move(rotated);
    QVector<double> cumulative{0};
    cumulative.reserve(axis.points.size());
    for (qsizetype i = 1; i < axis.points.size(); ++i)
        cumulative.append(cumulative.last() + pointDistance(axis.points[i - 1], axis.points[i]));
    axis.cumulative = std::move(cumulative);
    axis.lengthMeters = length;
    axis.spacingMeters = length / axis.points.size();
    axis.origin = origin;
    axis.valid = true;
    return axis;
}

TrackFeatures computeTrackFeatures(const ProgressAxis &axis, const double smoothingMeters)
{
    TrackFeatures features;
    if (!axis.valid || axis.points.size() < 4 || !(axis.spacingMeters > 0.0)
        || !std::isfinite(smoothingMeters) || smoothingMeters <= 0.0)
        return features;

    const int n = static_cast<int>(axis.points.size());
    const int radius = std::clamp(
        static_cast<int>(std::lround(smoothingMeters / axis.spacingMeters)), 1, n / 2 - 1);

    // Smooth by averaging unit tangent vectors, not raw angles: naively
    // averaging e.g. +179 degrees and -179 degrees gives 0, not +-180.
    QVector<double> heading(n, 0.0);
    for (int i = 0; i < n; ++i) {
        double sumEast = 0.0, sumNorth = 0.0;
        for (int offset = -radius; offset <= radius; ++offset) {
            const int index = ((i + offset) % n + n) % n;
            const QPointF tangent = axisTangent(axis, index);
            sumEast += tangent.x();
            sumNorth += tangent.y();
        }
        heading[i] = std::atan2(sumNorth, sumEast);
    }

    features.samples.reserve(n);
    for (int i = 0; i < n; ++i) {
        const int next = (i + 1) % n;
        const double curvature = angularDifference(heading[next], heading[i]) / axis.spacingMeters;
        features.samples.append({axis.cumulative[i], heading[i], curvature});
    }
    features.smoothingMeters = smoothingMeters;
    features.valid = true;
    return features;
}

ProjectedSample projectSample(const ProgressAxis &axis, const QPointF &localPoint, const double telemetryTime,
    const double speedMetersPerSecond, const QPointF &movementDirection, ProjectionContext &context)
{
    ProjectedSample result;
    result.telemetryTime = telemetryTime;
    if (!axis.valid || axis.points.size() < 8 || !(axis.spacingMeters > 0)) return result;
    if (!std::isfinite(localPoint.x()) || !std::isfinite(localPoint.y()) || !std::isfinite(telemetryTime)) return result;

    constexpr double coldStartProximityMeters = 20.0;
    constexpr double lockProximityMeters = 20.0;
    constexpr double ambiguityRatio = 0.7; // reject when the runner-up is within 70% of the best distance
    constexpr double coldStartSeparationMeters = 30.0;
    constexpr double windowedSeparationMeters = 10.0;
    // Direction must not disagree outright (>90 degrees off): this is what
    // rejects a nearby parallel section running the opposite way. Cornering
    // can still dip well below a "mostly agrees" bar, so this stays loose.
    constexpr double minimumHeadingCosine = 0.0;

    const double dt = context.hasLock ? telemetryTime - context.lastTelemetryTime : 0.0;
    const bool coldStart = !context.hasLock || !(dt > 0) || dt > maximumGapSeconds;

    if (coldStart) {
        // No usable window yet (or too much time has passed to trust one):
        // an unambiguous global match is required before locking on at all.
        const auto best = bestCandidateInRange(axis, localPoint, 0, axis.points.size());
        if (best.index < 0 || best.distance > coldStartProximityMeters) return result;
        const int minimumSeparation = std::max(4, static_cast<int>(std::lround(coldStartSeparationMeters / axis.spacingMeters)));
        const auto second = secondBestCandidate(axis, localPoint, 0, axis.points.size(), best.index, minimumSeparation);
        if (second.index >= 0 && best.distance > second.distance * ambiguityRatio) return result;
        if (!headingAgrees(axis, best.index, movementDirection, minimumHeadingCosine)) return result;
        result.progressMeters = best.progressMeters;
        result.valid = true;
        context.hasLock = true;
        context.lastProgressMeters = best.progressMeters;
        context.lastTelemetryTime = telemetryTime;
        return result;
    }

    // Adaptive, forward-biased local window: this bounded search is what
    // keeps a hairpin apex or a nearby parallel straight from being confused
    // with the correct branch -- the rest of the track is never considered.
    const double expectedTravel = dt * std::max(0.0, speedMetersPerSecond);
    const double forwardWindow = std::clamp(expectedTravel * 1.6, 15.0, 150.0);
    const double backwardWindow = std::min(15.0, forwardWindow * 0.3);
    const int forwardCount = std::max(1, static_cast<int>(std::lround(forwardWindow / axis.spacingMeters)));
    const int backwardCount = std::max(1, static_cast<int>(std::lround(backwardWindow / axis.spacingMeters)));
    const int n = axis.points.size();
    const int centerIndex = ((static_cast<int>(std::lround(context.lastProgressMeters / axis.spacingMeters)) % n) + n) % n;
    const int startIndex = centerIndex - backwardCount;
    const int count = forwardCount + backwardCount + 1;

    const auto best = bestCandidateInRange(axis, localPoint, startIndex, count);
    if (best.index < 0 || best.distance > lockProximityMeters) return result; // lost lock; caller ends the segment
    const int minimumSeparation = std::max(3, static_cast<int>(std::lround(windowedSeparationMeters / axis.spacingMeters)));
    const auto second = secondBestCandidate(axis, localPoint, startIndex, count, best.index, minimumSeparation);
    if (second.index >= 0 && best.distance > second.distance * ambiguityRatio) return result;
    if (!headingAgrees(axis, best.index, movementDirection, minimumHeadingCosine)) return result;

    double delta = best.progressMeters - context.lastProgressMeters;
    if (delta < -axis.lengthMeters / 2) delta += axis.lengthMeters; // wrapped past the start/finish line
    else if (delta > axis.lengthMeters / 2) delta -= axis.lengthMeters;
    if (delta < -backwardToleranceMeters) return result; // meaningful backward jump: not trustworthy

    result.progressMeters = best.progressMeters;
    result.valid = true;
    context.hasLock = true;
    context.lastProgressMeters = best.progressMeters;
    context.lastTelemetryTime = telemetryTime;
    return result;
}

QVector<ProgressSegment> projectLapTrace(const ProgressAxis &axis, const TelemetrySession &session,
    const double startTime, const double endTime, const CancellationCheck &cancelled)
{
    QVector<ProgressSegment> result;
    if (!axis.valid) return result;
    const auto latitudeSegments = session.sampledSegments("latitude", startTime, endTime, 4000);

    ProgressSegment current;
    ProjectionContext context;
    std::optional<QPointF> previousLocal;
    std::optional<double> previousTime;
    std::optional<double> lastProgress; // unwrapped; kept across segments of this lap
    const auto flush = [&] {
        if (!current.samples.isEmpty()) { result.append(std::move(current)); current = {}; }
        context = ProjectionContext{};
        previousLocal.reset();
        previousTime.reset();
    };

    for (const auto &segment : latitudeSegments) {
        flush(); // a raw GPS gap between sampledSegments runs is never bridged
        for (const auto &sample : segment) {
            throwIfCancelled(cancelled);
            const double time = sample.x();
            const double latitude = sample.y();
            const auto longitude = session.valueAt("longitude", time);
            if (!longitude || !isValidCoordinate({latitude, *longitude})) { flush(); continue; }

            const MetricPoint projected = projectCoordinate({latitude, *longitude}, axis.origin);
            const QPointF local(projected.eastMeters, projected.northMeters);
            double speed = 0.0;
            QPointF movement(0, 0);
            if (previousLocal && previousTime && time > *previousTime) {
                movement = local - *previousLocal;
                speed = std::hypot(movement.x(), movement.y()) / (time - *previousTime);
            }
            previousLocal = local;
            previousTime = time;

            auto projectedSample = projectSample(axis, local, time, speed, movement, context);
            if (!projectedSample.valid) { flush(); continue; }

            // Unwrap at the gate (KAN-152). The lap's first fix, taken just
            // after its timed start, may project onto the end of the axis:
            // it is just before progress 0. Later fixes continue forward from
            // the last one by whole laps of the axis, so the fixes past the
            // finish run beyond the axis length.
            double &progress = projectedSample.progressMeters;
            if (!lastProgress) {
                if (progress > axis.lengthMeters / 2 && time - startTime <= maximumGapSeconds) progress -= axis.lengthMeters;
            } else {
                progress += std::ceil((*lastProgress - backwardToleranceMeters - progress) / axis.lengthMeters)
                    * axis.lengthMeters;
            }
            // A fix projecting up to the backward tolerance behind the last one
            // in this segment (GPS jitter, often while stopped) is held at the
            // last progress, so progress never falls within a segment and the
            // segment still covers the fix's time. A segment after a gap may
            // start a little behind the last one, as before.
            if (!current.samples.isEmpty()) progress = std::max(progress, current.samples.last().progressMeters);
            lastProgress = progress;
            current.samples.append(projectedSample);
        }
    }
    if (!current.samples.isEmpty()) result.append(std::move(current));
    return result;
}

namespace {

std::optional<double> timeAtProgressInSegment(const ProgressSegment &segment, const double progress)
{
    const auto &samples = segment.samples;
    if (samples.isEmpty() || progress < samples.first().progressMeters || progress > samples.last().progressMeters)
        return std::nullopt;
    const auto next = std::lower_bound(samples.cbegin(), samples.cend(), progress,
        [](const ProjectedSample &sample, const double value) { return sample.progressMeters < value; });
    if (next == samples.cbegin()) return next->telemetryTime;
    if (next == samples.cend()) return std::prev(next)->telemetryTime;
    const auto &previous = *std::prev(next);
    const double span = next->progressMeters - previous.progressMeters;
    if (!(span > 0)) return previous.telemetryTime;
    const double ratio = (progress - previous.progressMeters) / span;
    return previous.telemetryTime + (next->telemetryTime - previous.telemetryTime) * ratio;
}

// Reference "lap start" time: the exact progress=0 crossing when a segment's
// coverage reaches it, otherwise the earliest projected sample as the
// closest available approximation.
double referenceTime(const QVector<ProgressSegment> &lap)
{
    if (const auto time = timeAtProgress(lap, 0.0)) return *time;
    for (const auto &segment : lap)
        if (!segment.samples.isEmpty()) return segment.samples.first().telemetryTime;
    return 0.0;
}

} // namespace

std::optional<double> timeAtProgress(const QVector<ProgressSegment> &lap, const double progressMeters)
{
    for (const auto &segment : lap) {
        if (const auto time = timeAtProgressInSegment(segment, progressMeters)) return time;
    }
    return std::nullopt;
}

namespace {

// The lap at its timed start at progress 0 and its timed end at the axis
// length, when its projection stops within the gate tolerance of them.
QVector<ProgressSegment> anchoredAtGate(
    QVector<ProgressSegment> lap, const double start, const double end, const double length)
{
    if (lap.isEmpty() || lap.first().samples.isEmpty() || lap.last().samples.isEmpty()) return lap;
    auto &first = lap.first().samples;
    if (first.first().progressMeters > 0.0 && first.first().progressMeters <= gateCoverageToleranceMeters
        && start < first.first().telemetryTime)
        first.prepend({start, 0.0, true});
    auto &last = lap.last().samples;
    if (last.last().progressMeters < length && last.last().progressMeters >= length - gateCoverageToleranceMeters
        && end > last.last().telemetryTime)
        last.append({end, length, true});
    return lap;
}

} // namespace

QVector<QVector<DeltaPoint>> computeDeltaSeries(const QVector<ProgressSegment> &lapA,
    const QVector<ProgressSegment> &lapB, const double progressStepMeters, const DeltaTiming &timing,
    const CancellationCheck &cancelled)
{
    QVector<QVector<DeltaPoint>> result;
    const double length = timing.lengthMeters;
    if (lapA.isEmpty() || lapB.isEmpty() || !(progressStepMeters > 0) || !std::isfinite(length) || !(length > 0))
        return result;
    const auto anchoredA = anchoredAtGate(lapA, timing.lapStartA, timing.lapEndA, length);
    const auto anchoredB = anchoredAtGate(lapB, timing.lapStartB, timing.lapEndB, length);

    const int steps = static_cast<int>(std::ceil(length / progressStepMeters));
    QVector<DeltaPoint> current;
    for (int i = 0; i <= steps; ++i) {
        throwIfCancelled(cancelled);
        const double progress = std::min(length, i * progressStepMeters);
        const auto timeA = timeAtProgress(anchoredA, progress);
        const auto timeB = timeAtProgress(anchoredB, progress);
        if (!timeA || !timeB) {
            if (!current.isEmpty()) { result.append(std::move(current)); current = {}; }
            continue;
        }
        current.append({progress, (*timeA - timing.lapStartA) - (*timeB - timing.lapStartB)});
    }
    if (!current.isEmpty()) result.append(std::move(current));
    return result;
}

QVector<QVector<DeltaPoint>> computeDeltaSeries(const QVector<ProgressSegment> &lapA,
    const QVector<ProgressSegment> &lapB, const double progressStepMeters, const CancellationCheck &cancelled)
{
    QVector<QVector<DeltaPoint>> result;
    if (lapA.isEmpty() || lapB.isEmpty() || !(progressStepMeters > 0)) return result;

    double maxProgress = 0;
    bool haveCoverage = false;
    for (const auto &segment : lapA)
        if (!segment.samples.isEmpty()) { maxProgress = std::max(maxProgress, segment.samples.last().progressMeters); haveCoverage = true; }
    for (const auto &segment : lapB)
        if (!segment.samples.isEmpty()) { maxProgress = std::max(maxProgress, segment.samples.last().progressMeters); haveCoverage = true; }
    if (!haveCoverage || !(maxProgress > 0)) return result;

    const double referenceA = referenceTime(lapA);
    const double referenceB = referenceTime(lapB);
    const int steps = static_cast<int>(std::floor(maxProgress / progressStepMeters));

    QVector<DeltaPoint> current;
    for (int i = 0; i <= steps; ++i) {
        throwIfCancelled(cancelled);
        const double progress = std::min(maxProgress, i * progressStepMeters);
        const auto timeA = timeAtProgress(lapA, progress);
        const auto timeB = timeAtProgress(lapB, progress);
        if (!timeA || !timeB) {
            if (!current.isEmpty()) { result.append(std::move(current)); current = {}; }
            continue;
        }
        current.append({progress, (*timeA - referenceA) - (*timeB - referenceB)});
    }
    if (!current.isEmpty()) result.append(std::move(current));
    return result;
}

std::optional<double> progressAtTime(const QVector<ProgressSegment> &lap, const double time)
{
    for (const auto &segment : lap) {
        const auto &samples = segment.samples;
        if (samples.isEmpty() || time < samples.first().telemetryTime || time > samples.last().telemetryTime) continue;
        const auto next = std::lower_bound(samples.cbegin(), samples.cend(), time,
            [](const ProjectedSample &sample, const double value) { return sample.telemetryTime < value; });
        if (next == samples.cbegin()) return next->progressMeters;
        const auto &previous = *std::prev(next);
        const double span = next->telemetryTime - previous.telemetryTime;
        if (!(span > 0.0)) return previous.progressMeters;
        return previous.progressMeters
            + (next->progressMeters - previous.progressMeters) * (time - previous.telemetryTime) / span;
    }
    return std::nullopt;
}

} // namespace FlappedEar
