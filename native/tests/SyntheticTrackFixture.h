#pragma once

// Synthetic tracks with a known centre line, laps driven along them with a
// known distance at every fix, and the projection's outcome measured against
// that ground truth (KAN-238, ported from Telemetry's projection_tracks.dart).
// No real data. Used by ColdStartBranchTests and LockedOffLineTests.

#include "SyntheticLoopFixture.h"
#include "telemetry/TrackProgress.h"

#include <QPointF>
#include <QString>
#include <QVector>
#include <QtGlobal>
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <numbers>
#include <optional>
#include <random>

namespace SyntheticTrackFixture {

using namespace FlappedEar;
using SyntheticLoop::arc;
using SyntheticLoop::straight;

// A projected fix may be this far from its true distance: 2 m axis chords
// plus GPS error. A wrong branch is tens to thousands of metres out.
constexpr double branchError = 5.0;
constexpr double earthRadiusMeters = 6'371'000.0;

// At the equator (origin 0, 0) east and north convert the same way.
inline double degreesForMeters(const double meters) {
    return meters / earthRadiusMeters * 180.0 / std::numbers::pi;
}

inline QPointF tangentOf(const QPointF &a, const QPointF &b) {
    const double span = std::hypot(b.x() - a.x(), b.y() - a.y());
    return span > 0 ? (b - a) / span : QPointF(0, 0);
}

// A closed centre line in metres around (0, 0), starting at the timing gate,
// sampled about every metre. The last point is not repeated.
class SyntheticTrack {
  public:
    explicit SyntheticTrack(QVector<QPointF> points) : m_points(std::move(points)) {
        m_closed = m_points;
        m_closed.append(m_points.first());
        m_cumulative.append(0.0);
        for (qsizetype i = 1; i < m_closed.size(); ++i)
            m_cumulative.append(m_cumulative.last() +
                                std::hypot(m_closed[i].x() - m_closed[i - 1].x(),
                                           m_closed[i].y() - m_closed[i - 1].y()));
    }

    static SyntheticTrack loop(const QVector<SyntheticLoop::Step> &half) {
        return SyntheticTrack(SyntheticLoop::loopPoints(half));
    }

    // A figure-eight whose two diagonals, each `diagonal` long, cross at
    // `crossingDegrees` in the middle, joined by two circular lobes. The gate
    // is a quarter of a diagonal past the crossing, heading away from it; the
    // lap reaches the crossing again at figureEightCrossing().
    static SyntheticTrack figureEight(const double crossingDegrees, const double diagonal = 300.0) {
        const double theta = crossingDegrees / 2.0 * std::numbers::pi / 180.0;
        const double arm = diagonal / 2.0;
        const double radius = arm * std::tan(theta);
        const double lobe = 180.0 + crossingDegrees;
        QVector<QPointF> points{QPointF(0, 0)};
        double heading = theta;
        const auto straightOn = [&](const double meters) {
            const int steps = std::max(1, static_cast<int>(std::lround(meters)));
            for (int i = 0; i < steps; ++i)
                points.append(points.last() + QPointF(meters / steps * std::cos(heading),
                                                      meters / steps * std::sin(heading)));
        };
        const auto turn = [&](const double degrees) {
            const double angle = degrees * std::numbers::pi / 180.0;
            const int steps = std::max(1, static_cast<int>(std::lround(std::abs(angle) * radius)));
            const double ds = std::abs(angle) * radius / steps;
            for (int i = 0; i < steps; ++i) {
                heading += angle / steps / 2.0;
                points.append(points.last() +
                              QPointF(ds * std::cos(heading), ds * std::sin(heading)));
                heading += angle / steps / 2.0;
            }
        };
        straightOn(arm);
        turn(-lobe);
        straightOn(2 * arm);
        turn(lobe);
        straightOn(arm);
        points.removeLast(); // back at the crossing
        // Start halfway along the first arm, heading away from the crossing.
        const auto start = static_cast<qsizetype>(std::lround(arm / 2));
        const QPointF origin = points[start];
        QVector<QPointF> rotated;
        for (qsizetype i = 0; i < points.size(); ++i)
            rotated.append(points[(start + i) % points.size()] - origin);
        return SyntheticTrack(rotated);
    }

    [[nodiscard]] double lengthMeters() const { return m_cumulative.last(); }

    // The centre line at `meters` from the gate, wrapping round the loop, and
    // its unit tangent.
    [[nodiscard]] std::pair<QPointF, QPointF> at(const double meters) const {
        double s = std::fmod(meters, lengthMeters());
        if (s < 0)
            s += lengthMeters();
        qsizetype low = 0, high = m_cumulative.size() - 1;
        while (high - low > 1) {
            const qsizetype middle = (low + high) / 2;
            if (m_cumulative[middle] <= s)
                low = middle;
            else
                high = middle;
        }
        const QPointF a = m_closed[low], b = m_closed[high];
        const double span = m_cumulative[high] - m_cumulative[low];
        const double f = span > 0 ? (s - m_cumulative[low]) / span : 0.0;
        return {a + (b - a) * f, tangentOf(a, b)};
    }

    // Signed curvature at `meters` (positive turns left), from the heading
    // change over 5 m either side.
    [[nodiscard]] double curvatureAt(const double meters) const {
        const QPointF a = at(meters - 5.0).second, b = at(meters + 5.0).second;
        return std::atan2(a.x() * b.y() - a.y() * b.x(), a.x() * b.x() + a.y() * b.y()) / 10.0;
    }

    // The fastest speed (m/s) at `meters` for 1.2 g of cornering, at most
    // 45 m/s: slow in hairpins, fast on straights.
    [[nodiscard]] double cornerSpeed(const double meters, const double topSpeed = 45.0) const {
        const double curvature = std::abs(curvatureAt(meters));
        if (curvature < 1e-6)
            return topSpeed;
        return std::min(topSpeed, std::sqrt(1.2 * 9.81 / curvature));
    }

    // The progress axis of the centre line itself, as the reference lap, with
    // a gate 20 m across the centre line at the start.
    [[nodiscard]] ProgressAxis axis() const {
        LapTrace trace;
        for (qsizetype i = 0; i < m_points.size(); ++i)
            trace.points.append({static_cast<double>(i), m_points[i].x(), m_points[i].y()});
        const auto [centre, tangent] = at(0.0);
        const auto geo = [](const QPointF &point) {
            return GeoCoordinate{degreesForMeters(point.y()), degreesForMeters(point.x())};
        };
        const QPointF across(-tangent.y() * 10.0, tangent.x() * 10.0);
        const TimingGate gate{
            TimingGateType::Start, "Start", geo(centre + across), geo(centre - across), {}};
        return buildProgressAxis(trace, {0.0, 0.0}, gate);
    }

  private:
    QVector<QPointF> m_points;
    QVector<QPointF> m_closed;
    QVector<double> m_cumulative;
};

// Where a lap of SyntheticTrack::figureEight with the same arguments crosses
// its other diagonal, in metres from the gate.
inline double figureEightCrossing(const double crossingDegrees, const double diagonal = 300.0) {
    const double arm = diagonal / 2.0;
    const double radius = arm * std::tan(crossingDegrees / 2.0 * std::numbers::pi / 180.0);
    return arm / 2.0 + radius * (180.0 + crossingDegrees) * std::numbers::pi / 180.0 + arm;
}

// One lap driven on a track: the GPS session and, for each fix, its time,
// its true distance from the gate along the centre line, and where it is.
struct DrivenLap {
    TelemetrySession session;
    QVector<double> times;
    QVector<double> truth;
    QVector<QPointF> local;
    [[nodiscard]] double endTime() const { return times.last(); }
};

// How a lap is driven. Each setter returns the drive, so a test names only
// what it changes.
struct Drive {
    std::function<double(double)> speed;   // m/s by distance; the track's corner speed when empty
    double interval = 0.1;                 // seconds between fixes
    std::function<double(double)> lateral; // metres left of the centre line by distance
    double noise = 0.0;                    // GPS error, standard deviation in metres
    unsigned seed = 1;
    std::function<bool(double)> dropFix;
    std::function<std::optional<double>(int)>
        schedule; // the distance of each fix, or none when over

    Drive &withSpeed(std::function<double(double)> value) {
        speed = std::move(value);
        return *this;
    }
    Drive &withLateral(std::function<double(double)> value) {
        lateral = std::move(value);
        return *this;
    }
    Drive &every(const double seconds) {
        interval = seconds;
        return *this;
    }
    Drive &withNoise(const double value) {
        noise = value;
        return *this;
    }
    Drive &withSeed(const unsigned value) {
        seed = value;
        return *this;
    }
    Drive &dropping(std::function<bool(double)> value) {
        dropFix = std::move(value);
        return *this;
    }
    Drive &following(std::function<std::optional<double>(int)> value) {
        schedule = std::move(value);
        return *this;
    }
};

// Drives one lap of `track` from the gate, a fix every 0.1 s. Like a real
// receiver's, the GPS error is correlated over a second (a first-order random
// walk back to zero), uniform with the given standard deviation.
inline DrivenLap driveTrack(const SyntheticTrack &track, const Drive &drive = {}) {
    const double interval = drive.interval;
    std::mt19937 random(drive.seed);
    std::uniform_real_distribution<double> uniform(-1.0, 1.0);
    const double keep = std::exp(-interval / 1.0);
    const auto draw = [&] { return drive.noise * std::sqrt(3.0) * uniform(random); };
    double errorX = draw(), errorY = draw();

    DrivenLap lap;
    lap.session.aliases = {{"latitude", "latitude"}, {"longitude", "longitude"}};
    auto &latitude = lap.session.channels["latitude"];
    auto &longitude = lap.session.channels["longitude"];
    double s = 0.0, t = 0.0;
    for (int index = 0;; ++index) {
        double meters = s;
        if (drive.schedule) {
            const auto next = drive.schedule(index);
            if (!next)
                break;
            meters = *next;
        } else if (s > track.lengthMeters()) {
            break;
        }
        if (!drive.dropFix || !drive.dropFix(meters)) {
            const auto [centre, tangent] = track.at(meters);
            const double offset = drive.lateral ? drive.lateral(meters) : 0.0;
            errorX = keep * errorX + std::sqrt(1 - keep * keep) * draw();
            errorY = keep * errorY + std::sqrt(1 - keep * keep) * draw();
            const QPointF point(centre.x() - tangent.y() * offset + errorX,
                                centre.y() + tangent.x() * offset + errorY);
            latitude.appendSample(t, static_cast<float>(degreesForMeters(point.y())));
            longitude.appendSample(t, static_cast<float>(degreesForMeters(point.x())));
            lap.times.append(t);
            lap.truth.append(meters);
            lap.local.append(point);
        }
        s += (drive.speed ? drive.speed(s) : track.cornerSpeed(s)) * interval;
        t += interval;
    }
    lap.session.duration = lap.times.last() - lap.times.first();
    return lap;
}

// How a lap's projection compares with its ground truth.
struct Outcome {
    qsizetype segments = 0;
    qsizetype fixes = 0;
    qsizetype projected = 0;
    double maximumError = 0.0;        // largest |projected - true|, metres
    double largestBackwardStep = 0.0; // largest fall in progress across the whole lap
    double lastProgress = std::numeric_limits<double>::quiet_NaN();

    [[nodiscard]] QString describe() const {
        return QString("%1 segment(s), %2/%3 fixes, max error %4 m, largest backward step %5 m, "
                       "last %6 m")
            .arg(segments)
            .arg(projected)
            .arg(fixes)
            .arg(maximumError, 0, 'f', 2)
            .arg(largestBackwardStep, 0, 'f', 2)
            .arg(lastProgress, 0, 'f', 1);
    }
};

inline Outcome measureProjection(const ProgressAxis &axis, const SyntheticTrack &track,
                                 const DrivenLap &lap) {
    const auto segments = projectLapTrace(axis, lap.session, 0.0, lap.endTime());
    const double scale = axis.lengthMeters / track.lengthMeters();
    std::map<double, double> truthAt;
    for (qsizetype i = 0; i < lap.times.size(); ++i)
        truthAt[lap.times[i]] = lap.truth[i] * scale;
    Outcome outcome;
    outcome.segments = segments.size();
    outcome.fixes = lap.times.size();
    std::optional<double> previous;
    for (const auto &segment : segments) {
        for (const auto &sample : segment.samples) {
            ++outcome.projected;
            outcome.maximumError =
                std::max(outcome.maximumError,
                         std::abs(sample.progressMeters - truthAt.at(sample.telemetryTime)));
            if (previous)
                outcome.largestBackwardStep =
                    std::max(outcome.largestBackwardStep, *previous - sample.progressMeters);
            previous = sample.progressMeters;
        }
    }
    if (!segments.isEmpty())
        outcome.lastProgress = segments.last().samples.last().progressMeters;
    return outcome;
}

// No fix misplaced and no segment shifted: every projected fix within the
// allowed error of its true distance, progress never falling by more than
// that, and the lap still reaching the finish.
inline bool onTheRightBranch(const ProgressAxis &axis, const Outcome &outcome) {
    return outcome.maximumError <= branchError && outcome.largestBackwardStep <= branchError &&
           std::abs(outcome.lastProgress - axis.lengthMeters) <= 10.0;
}

} // namespace SyntheticTrackFixture
