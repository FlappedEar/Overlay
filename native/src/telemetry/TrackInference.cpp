#include "telemetry/TrackInference.h"

#include <QJsonArray>
#include <QCryptographicHash>
#include <algorithm>
#include <cmath>
#include <numbers>

namespace FlappedEar {
namespace {
constexpr int shapePoints = 256;
constexpr int maximumRepresentatives = 64;
double distance(const QPointF &a, const QPointF &b) { return std::hypot(a.x() - b.x(), a.y() - b.y()); }

RouteShape shape(const LapTrace &trace, const GeoCoordinate &origin, bool westPositive)
{
    RouteShape result;
    if (trace.points.size() < 12 || trace.points.size() > 4096) return result;
    QVector<QPointF> points;
    for (const auto &sample : trace.points) {
        const QPointF point(westPositive ? -sample.eastMeters : sample.eastMeters, sample.northMeters);
        if (!std::isfinite(point.x()) || !std::isfinite(point.y())) return {};
        // Drop sub-3m GPS jitter before distance resampling. Timing is unused.
        if (points.isEmpty() || distance(points.last(), point) >= 3) points.append(point);
    }
    if (points.size() < 12 || distance(points.first(), points.last()) > 25) return {};
    points.append(points.first());
    QVector<double> cumulative{0};
    double area = 0;
    for (qsizetype i = 1; i < points.size(); ++i) {
        const auto a = points[i - 1], b = points[i];
        cumulative.append(cumulative.last() + distance(a, b));
        area += a.x() * b.y() - b.x() * a.y();
    }
    const double length = cumulative.last();
    // Reject tiny/degenerate or self-cancelling winding, including figure eights.
    if (length < 100 || length > 30'000 || std::abs(area) * .5 < .005 * length * length) return {};
    result.origin = origin; result.lengthMeters = length;
    result.direction = area < 0 ? "clockwise" : "counterclockwise";
    qsizetype segment = 1;
    for (int i = 0; i < shapePoints; ++i) {
        const double target = length * i / shapePoints;
        while (segment + 1 < cumulative.size() && cumulative[segment] < target) ++segment;
        const double span = cumulative[segment] - cumulative[segment - 1];
        if (span <= 0) return {};
        result.points.append(points[segment - 1] + (points[segment] - points[segment - 1])
            * ((target - cumulative[segment - 1]) / span));
    }
    return result;
}
}

bool routesMatch(const RouteShape &a, const RouteShape &b, const CancellationCheck &cancelled)
{
    throwIfCancelled(cancelled);
    if (a.points.size() != shapePoints || b.points.size() != shapePoints || a.direction != b.direction
        || std::abs(a.lengthMeters - b.lengthMeters) > .05 * std::max(a.lengthMeters, b.lengthMeters)) return false;
    const auto offset = projectCoordinate(b.origin, a.origin);
    const double scale = std::cos(a.origin.latitudeDegrees * std::numbers::pi / 180)
        / std::cos(b.origin.latitudeDegrees * std::numbers::pi / 180);
    if (!std::isfinite(scale) || std::abs(scale) > 2) return false;
    QVector<QPointF> projected; projected.reserve(shapePoints);
    for (const auto &point : b.points) projected.append({point.x() * scale + offset.eastMeters, point.y() + offset.northMeters});
    // Cyclic shifts tolerate a different recording/trace start without rotating,
    // translating or reversing the physical route to manufacture a match.
    // Each point is measured against the other route's path (its two
    // neighbouring resampled segments), not its nearest resampled point: on a
    // long circuit the points are ~14 m apart, and an along-track offset of up
    // to half that is not a different route. Only the cross-track distance is.
    const auto toSegment = [](const QPointF &p, const QPointF &a, const QPointF &b) {
        const QPointF ab = b - a;
        const double length2 = ab.x() * ab.x() + ab.y() * ab.y();
        const double t = length2 > 0 ? std::clamp(((p - a).x() * ab.x() + (p - a).y() * ab.y()) / length2, 0.0, 1.0) : 0.0;
        return distance(p, a + ab * t);
    };
    for (int shift = 0; shift < shapePoints; ++shift) {
        throwIfCancelled(cancelled);
        double squared = 0; bool within = true;
        for (int i = 0; i < shapePoints; ++i) {
            const int k = (i + shift) % shapePoints;
            const auto &previous = projected[(k + shapePoints - 1) % shapePoints];
            const auto &next = projected[(k + 1) % shapePoints];
            const double d = std::min(toSegment(a.points[i], previous, projected[k]), toSegment(a.points[i], projected[k], next));
            if (d > 25) { within = false; break; }
            squared += d * d;
        }
        if (within && squared <= shapePoints * 100.0) return true; // RMS <= 10m, max <= 25m.
    }
    return false;
}

QHash<int, double> lapLineDeviations(const QVector<LapTrace> &traces, const QSet<int> &lapNumbers, const CancellationCheck &cancelled)
{
    QHash<int, double> result;
    constexpr double cell = 4.0;
    const double cap = maximumLineDeviationMeters + 4.0;
    QVector<const LapTrace *> used;
    qsizetype points = 0;
    for (const auto &trace : traces)
        if (lapNumbers.contains(trace.lapNumber) && trace.points.size() >= 2) { used.append(&trace); points += trace.points.size(); }
    if (used.size() < 3 || points > 4'000'000) return result;
    // Every point of every lap in a 4 m grid, to find other laps' paths nearby.
    const auto key = [](const qint64 x, const qint64 y) { return (x << 32) ^ (y & 0xffffffff); };
    QHash<qint64, QVector<QPair<int, int>>> grid;
    for (int lap = 0; lap < used.size(); ++lap)
        for (int index = 0; index < used[lap]->points.size(); ++index) {
            const auto &point = used[lap]->points[index];
            grid[key(qint64(std::floor(point.eastMeters / cell)), qint64(std::floor(point.northMeters / cell)))].append({lap, index});
        }
    const auto pointOf = [](const LapTracePoint &point) { return QPointF(point.eastMeters, point.northMeters); };
    const auto toSegment = [](const QPointF &p, const QPointF &a, const QPointF &b) {
        const QPointF ab = b - a;
        const double length2 = ab.x() * ab.x() + ab.y() * ab.y();
        const double t = length2 > 0 ? std::clamp(((p - a).x() * ab.x() + (p - a).y() * ab.y()) / length2, 0.0, 1.0) : 0.0;
        return distance(p, a + ab * t);
    };
    const int reach = int(std::ceil(cap / cell));
    for (int lap = 0; lap < used.size(); ++lap) {
        throwIfCancelled(cancelled);
        double worst = 0.0;
        for (const auto &sample : used[lap]->points) {
            const QPointF p = pointOf(sample);
            const qint64 cx = qint64(std::floor(p.x() / cell)), cy = qint64(std::floor(p.y() / cell));
            double nearest = cap;
            for (int dx = -reach; dx <= reach; ++dx)
                for (int dy = -reach; dy <= reach; ++dy) {
                    const auto found = grid.constFind(key(cx + dx, cy + dy));
                    if (found == grid.cend()) continue;
                    for (const auto &[other, index] : *found) {
                        if (other == lap) continue;
                        const auto &path = used[other]->points;
                        const QPointF q = pointOf(path[index]);
                        if (index > 0) nearest = std::min(nearest, toSegment(p, pointOf(path[index - 1]), q));
                        if (index + 1 < path.size()) nearest = std::min(nearest, toSegment(p, q, pointOf(path[index + 1])));
                    }
                }
            worst = std::max(worst, nearest);
        }
        result.insert(used[lap]->lapNumber, worst);
    }
    return result;
}

TrackInference inferTrack(const LapSession &laps, const bool longitudeIsWestPositive, const CancellationCheck &cancelled)
{
    throwIfCancelled(cancelled);
    if (laps.lapTraces.size() > 20'000) throw ResourceLimitError("Too many lap traces for route inference.");
    TrackInference result;
    result.reason = "Not enough repeated, complete GPS laps to identify a route automatically.";
    if (!laps.selectedStartGate || laps.lapTraces.size() < 2) return result;
    const auto &gate = *laps.selectedStartGate;
    const GeoCoordinate midpoint = geoMidpoint(gate.endpointA, gate.endpointB);
    const GeoCoordinate origin{midpoint.latitudeDegrees, (longitudeIsWestPositive ? -1 : 1) * midpoint.longitudeDegrees};
    QVector<RouteShape> candidates;
    const auto stride = std::max<qsizetype>(1, (laps.lapTraces.size() + maximumRepresentatives - 1) / maximumRepresentatives);
    for (qsizetype i = 0; i < laps.lapTraces.size(); i += stride) {
        throwIfCancelled(cancelled);
        auto candidate = shape(laps.lapTraces[i], origin, longitudeIsWestPositive);
        if (!candidate.points.isEmpty()) candidates.append(std::move(candidate));
    }
    if (candidates.size() < 2) return result;
    QVector<QVector<int>> clusters;
    for (int i = 0; i < candidates.size(); ++i) {
        bool added = false;
        for (auto &cluster : clusters) {
            if (std::all_of(cluster.cbegin(), cluster.cend(), [&](int j) { return routesMatch(candidates[i], candidates[j], cancelled); })) {
                cluster.append(i); added = true; break;
            }
        }
        if (!added) clusters.append(QVector<int>{i});
    }
    const auto best = std::max_element(clusters.cbegin(), clusters.cend(), [](const auto &a, const auto &b) { return a.size() < b.size(); });
    if (best->size() < 2 || best->size() * 5 < candidates.size() * 3) {
        result.reason = "Complete laps follow conflicting routes; review this recording's layout.";
        return result;
    }
    result.route = candidates[best->first()];
    for (const auto &trace : laps.lapTraces) {
        throwIfCancelled(cancelled);
        if (routesMatch(result.route, shape(trace, origin, longitudeIsWestPositive), cancelled)) result.matchingLaps.insert(trace.lapNumber);
    }
    // KAN-137: the overall shape can match while a lap leaves the line every
    // other lap took (off track, a detour, the pit lane); that lap is off route.
    const auto deviations = lapLineDeviations(laps.lapTraces, result.matchingLaps, cancelled);
    for (auto it = deviations.cbegin(); it != deviations.cend(); ++it)
        if (it.value() > maximumLineDeviationMeters) result.matchingLaps.remove(it.key());
    result.reason.clear();
    return result;
}

bool manualTrackConfiguration(const QJsonObject &configuration)
{
    return configuration.value("layoutId").isString()
        || configuration.value("direction") == "clockwise"
        || configuration.value("direction") == "counterclockwise";
}

InferredTrackGroups groupInferredTracks(const QHash<QString, TrackInference> &inferences,
    const QJsonArray &sources, const CancellationCheck &cancelled)
{
    InferredTrackGroups result;
    if (sources.size() > 64 || inferences.size() > 64) throw ResourceLimitError("Too many runs for route grouping.");
    QHash<QString, QJsonObject> byId;
    for (const auto &value : sources) {
        const auto source = value.toObject(); const auto id = source.value("runId").toString();
        byId.insert(id, source); result.configurations.insert(id, source.value("trackConfiguration").toObject());
    }
    auto ids = byId.keys(); std::sort(ids.begin(), ids.end());
    QVector<QStringList> clusters;
    for (const auto &id : ids) {
        throwIfCancelled(cancelled);
        if (!inferences.value(id).supported()) continue;
        bool added = false;
        for (auto &cluster : clusters) {
            if (std::all_of(cluster.cbegin(), cluster.cend(), [&](const QString &other) {
                return routesMatch(inferences.value(id).route, inferences.value(other).route, cancelled);
            })) { cluster.append(id); added = true; break; }
        }
        if (!added) clusters.append(QStringList{id});
    }
    // A recording close to two incompatible clusters has no unique automatic
    // assignment. Do not let sort/input order silently choose a side.
    if (clusters.size() > 1) {
        for (const auto &id : ids) {
            if (!inferences.value(id).supported()) continue;
            int matches = 0;
            for (const auto &cluster : clusters) {
                if (std::all_of(cluster.cbegin(), cluster.cend(), [&](const QString &other) {
                    return other == id || routesMatch(inferences.value(id).route, inferences.value(other).route, cancelled);
                })) ++matches;
            }
            if (matches > 1) result.reasons.insert(id, "GPS route matches more than one incompatible group; review this recording's layout.");
        }
    }
    for (auto &cluster : clusters)
        cluster.removeIf([&result](const QString &id) { return result.reasons.contains(id); });
    clusters.removeIf([](const QStringList &cluster) { return cluster.isEmpty(); });
    QVector<QStringList> previousByCluster;
    QHash<QString, int> previousClaims;
    QSet<QString> reservedIds;
    for (const auto &cluster : clusters) {
        QStringList previousIds;
        for (const auto &id : cluster) {
            const auto source = byId.value(id), previous = source.value("inference").toObject();
            if (previous.value("algorithm") == trackInferenceVersion
                && previous.value("sourceRevision") == source.value("expectedRevision")
                && previous.value("gateRevision") == source.value("trackConfiguration").toObject().value("gateRevision")
                && previous.value("direction").toString() == inferences.value(id).route.direction
                && previous.value("layoutId").toString().startsWith("gps-route-v1:")) previousIds.append(previous.value("layoutId").toString());
        }
        std::sort(previousIds.begin(), previousIds.end());
        previousIds.removeDuplicates();
        for (const auto &id : previousIds) { ++previousClaims[id]; reservedIds.insert(id); }
        previousByCluster.append(previousIds);
    }
    for (qsizetype index = 0; index < clusters.size(); ++index) {
        const auto &cluster = clusters[index];
        QString layoutId;
        for (const auto &id : previousByCluster[index])
            if (previousClaims.value(id) == 1) { layoutId = id; break; }
        if (layoutId.isEmpty()) {
            // Content replacement must not recreate the old representative's ID
            // while an unchanged run still uses it. Conflicting persisted IDs
            // also never join independently verified, incompatible clusters.
            const auto source = byId.value(cluster.first());
            const QByteArray seed = cluster.first().toUtf8() + '\0'
                + source.value("expectedRevision").toString().toUtf8();
            int salt = 0;
            do {
                layoutId = "gps-route-v1:" + QString::fromLatin1(QCryptographicHash::hash(
                    seed + '\0' + QByteArray::number(salt++), QCryptographicHash::Sha256).toHex());
            } while (reservedIds.contains(layoutId));
            reservedIds.insert(layoutId);
        }
        for (const auto &id : cluster) {
            const auto source = byId.value(id); auto config = result.configurations.value(id);
            const auto direction = inferences.value(id).route.direction;
            result.provenance.insert(id, {{"algorithm", trackInferenceVersion}, {"sourceRevision", source.value("expectedRevision")},
                {"gateRevision", config.value("gateRevision")}, {"layoutId", layoutId}, {"direction", direction}});
            if (!manualTrackConfiguration(config)) {
                config.insert("layoutId", layoutId); config.insert("direction", direction);
                result.configurations.insert(id, config);
            }
        }
    }
    return result;
}
} // namespace FlappedEar
