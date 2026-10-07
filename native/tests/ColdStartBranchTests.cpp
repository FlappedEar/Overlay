// Truth tests for KAN-238 (Telemetry FET-256): a cold start in the middle of
// a lap (after a GPS gap, or after a fix the projection refused) must never
// lock on to another branch of the track, and one wrong fix must never shift
// the rest of the lap by whole laps. Each synthetic lap is driven with a
// known distance at every fix: a figure-eight whose GPS comes back next to
// the crossing, parallel straights driven in opposite directions with a lap
// off its line toward the other straight, and hairpins cut with 1 m of GPS
// error. A fix may be refused (a gap), never misplaced. Guards check that
// real cars keep their fixes (fast cars, long gaps, a lap timed from another
// line) and that a standing car keeps most of its fixes.
//
// Ported from Telemetry's cold_start_branch_test.dart. The GPS error comes
// from another random generator, so trial counts match Telemetry's but the
// individual laps do not.

#include "SyntheticLoopFixture.h"
#include "telemetry/TrackProgress.h"

#include <QtTest>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <numbers>
#include <optional>
#include <random>

using namespace FlappedEar;
using SyntheticLoop::arc;
using SyntheticLoop::straight;

namespace {

// A projected fix may be this far from its true distance: 2 m axis chords
// plus GPS error. A wrong branch is tens to thousands of metres out.
constexpr double allowedError = 5.0;
constexpr double earthRadiusMeters = 6'371'000.0;

// At the equator (origin 0, 0) east and north convert the same way.
double degreesForMeters(const double meters) {
    return meters / earthRadiusMeters * 180.0 / std::numbers::pi;
}

QPointF tangentOf(const QPointF &a, const QPointF &b) {
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
    [[nodiscard]] double cornerSpeed(const double meters) const {
        const double curvature = std::abs(curvatureAt(meters));
        if (curvature < 1e-6)
            return 45.0;
        return std::min(45.0, std::sqrt(1.2 * 9.81 / curvature));
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
double figureEightCrossing(const double crossingDegrees, const double diagonal = 300.0) {
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
DrivenLap driveTrack(const SyntheticTrack &track, const Drive &drive = {}) {
    constexpr double interval = 0.1;
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

Outcome measureProjection(const ProgressAxis &axis, const SyntheticTrack &track,
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
bool onTheRightBranch(const ProgressAxis &axis, const Outcome &outcome) {
    return outcome.maximumError <= allowedError && outcome.largestBackwardStep <= allowedError &&
           std::abs(outcome.lastProgress - axis.lengthMeters) <= 10.0;
}

SyntheticTrack parallelStraights(const double separation) {
    return SyntheticTrack::loop({straight(150), arc(180, separation / 2), straight(150)});
}

// Off its line by `offset` toward the other straight between `from` and
// 130 m along each straight.
std::function<double(double)> offLine(const SyntheticTrack &track, const double offset,
                                      const double from) {
    const double half = track.lengthMeters() / 2;
    return [offset, from, half](const double meters) {
        const double along = std::fmod(meters, half);
        return along > from && along < 130.0 ? offset : 0.0;
    };
}

SyntheticTrack openCircuit() {
    return SyntheticTrack::loop(
        {straight(600), arc(90, 60), straight(200), arc(90, 25), straight(300)});
}

// Runs every trial and reports the ones on the wrong branch.
struct Trials {
    int count = 0;
    QStringList wrong;
    void check(const ProgressAxis &axis, const Outcome &outcome, const QString &name) {
        ++count;
        if (!onTheRightBranch(axis, outcome))
            wrong.append(name + ": " + outcome.describe());
    }
    [[nodiscard]] QByteArray report() const {
        return QString("%1 of %2 trials on the wrong branch, first: %3")
            .arg(wrong.size())
            .arg(count)
            .arg(wrong.value(0))
            .toUtf8();
    }
};

} // namespace

class ColdStartBranchTests final : public QObject {
    Q_OBJECT
  private slots:
    void figureEightNeverLocksOnTheOtherDiagonal_data();
    void figureEightNeverLocksOnTheOtherDiagonal();
    void parallelStraightsNeverJumpAcross_data();
    void parallelStraightsNeverJumpAcross();
    void hairpinCutNeverRelocksOnTheOtherLeg_data();
    void hairpinCutNeverRelocksOnTheOtherLeg();
    void shortLoopsWithGapsLongerThanASecond_data();
    void shortLoopsWithGapsLongerThanASecond();
    void parallelStraightsWithARawGpsGap_data();
    void parallelStraightsWithARawGpsGap();
    void parallelStraightsWithAGapAtTheLapStart_data();
    void parallelStraightsWithAGapAtTheLapStart();
    void longGapEndingNearTheSecondPassOfTheCrossing_data();
    void longGapEndingNearTheSecondPassOfTheCrossing();
    void realCarsKeepTheirFixes();
    void standingCarKeepsMostOfItsFixes();
    void firstFixHasTheDirectionOfTheFixBeforeTheLap();
    void lapTimedFromAnotherLineKeepsEveryFix();
};

void ColdStartBranchTests::figureEightNeverLocksOnTheOtherDiagonal_data() {
    QTest::addColumn<double>("angle");
    for (const double angle : {90.0, 30.0, 10.0})
        QTest::addRow("%g degrees", angle) << angle;
}

// GPS back next to the crossing: a 30 m gap whose end is anywhere from 30 m
// before to 30 m after the crossing, on lines 1 m either side of the centre.
void ColdStartBranchTests::figureEightNeverLocksOnTheOtherDiagonal() {
    QFETCH(double, angle);
    const auto track = SyntheticTrack::figureEight(angle);
    const auto axis = track.axis();
    QVERIFY(axis.valid);
    const double crossing = figureEightCrossing(angle);
    Trials trials;
    for (double end = -30.0; end <= 30.0; end += 1.0) {
        for (const double side : {-1.0, 0.0, 1.0}) {
            for (unsigned seed = 1; seed <= 5; ++seed) {
                const double gapEnd = crossing + end;
                const auto lap = driveTrack(track, Drive()
                                                       .withLateral([side](double) { return side; })
                                                       .withNoise(0.3)
                                                       .withSeed(seed)
                                                       .dropping([gapEnd](double m) {
                                                           return m > gapEnd - 30.0 && m < gapEnd;
                                                       }));
                trials.check(axis, measureProjection(axis, track, lap),
                             QString("gap ends %1 m from the crossing, %2 m off, seed %3")
                                 .arg(end)
                                 .arg(side)
                                 .arg(seed));
            }
        }
    }
    QCOMPARE(trials.count, 915);
    QVERIFY2(trials.wrong.isEmpty(), trials.report());
}

void ColdStartBranchTests::parallelStraightsNeverJumpAcross_data() {
    QTest::addColumn<double>("separation");
    for (const double separation : {15.0, 20.0, 30.0})
        QTest::addRow("%g m apart", separation) << separation;
}

// Off its line beyond separation / 1.7 toward the other straight, which is
// then nearer than its own (and within 20 m of the fix).
void ColdStartBranchTests::parallelStraightsNeverJumpAcross() {
    QFETCH(double, separation);
    const auto track = parallelStraights(separation);
    const auto axis = track.axis();
    QVERIFY(axis.valid);
    Trials trials;
    for (const double share : {0.55, 0.6, 0.65}) {
        for (unsigned seed = 1; seed <= 5; ++seed) {
            const auto lap =
                driveTrack(track, Drive()
                                      .withLateral(offLine(track, share * separation, 20.0))
                                      .withNoise(0.3)
                                      .withSeed(seed));
            trials.check(axis, measureProjection(axis, track, lap),
                         QString("%1 m off, seed %2").arg(share * separation).arg(seed));
        }
    }
    QVERIFY2(trials.wrong.isEmpty(), trials.report());
}

void ColdStartBranchTests::hairpinCutNeverRelocksOnTheOtherLeg_data() {
    QTest::addColumn<double>("radius");
    QTest::addRow("3 m") << 3.0;
    QTest::addRow("5 m") << 5.0;
}

// With 1 m of GPS error a fix in a hairpin of 3 or 5 m radius can sit 4 m
// from where the car is, which moves the nearest point of the centre line by
// as much along the lap. So a fix is misplaced when it lands further from its
// true distance than 2 m (the axis chords) plus its own distance from the
// car's place on the centre line; one locked on to the other leg is out by
// the length of the hairpin. The 3 m hairpin fails without the other-leg
// check of the cold start.
void ColdStartBranchTests::hairpinCutNeverRelocksOnTheOtherLeg() {
    QFETCH(double, radius);
    const auto track = parallelStraights(2 * radius);
    const auto axis = track.axis();
    QVERIFY(axis.valid);
    const double scale = axis.lengthMeters / track.lengthMeters();
    QStringList wrong;
    for (unsigned seed = 1; seed <= 20; ++seed) {
        const auto lap = driveTrack(
            track, Drive()
                       .withLateral([&track, radius](const double meters) {
                           return 0.4 * radius *
                                  std::min(1.0, std::abs(track.curvatureAt(meters)) * radius);
                       })
                       .withNoise(1.0)
                       .withSeed(seed));
        std::map<double, qsizetype> index;
        for (qsizetype i = 0; i < lap.times.size(); ++i)
            index[lap.times[i]] = i;
        for (const auto &segment : projectLapTrace(axis, lap.session, 0.0, lap.endTime())) {
            for (const auto &sample : segment.samples) {
                const qsizetype i = index.at(sample.telemetryTime);
                const QPointF centre = track.at(lap.truth[i]).first;
                const double offTrue =
                    std::hypot(lap.local[i].x() - centre.x(), lap.local[i].y() - centre.y());
                const double error = std::abs(sample.progressMeters - lap.truth[i] * scale);
                if (error > 2.0 + offTrue)
                    wrong.append(QString("seed %1, %2 m: %3 m out, the fix %4 m from the car")
                                     .arg(seed)
                                     .arg(lap.truth[i], 0, 'f', 1)
                                     .arg(error, 0, 'f', 1)
                                     .arg(offTrue, 0, 'f', 1));
            }
        }
        const auto outcome = measureProjection(axis, track, lap);
        QVERIFY2(outcome.largestBackwardStep <= allowedError,
                 qPrintable(QString("seed %1: %2").arg(seed).arg(outcome.describe())));
        QVERIFY2(std::abs(outcome.lastProgress - axis.lengthMeters) <= 10.0,
                 qPrintable(QString("seed %1: %2").arg(seed).arg(outcome.describe())));
    }
    QVERIFY2(
        wrong.isEmpty(),
        qPrintable(QString("%1 fixes misplaced, first: %2").arg(wrong.size()).arg(wrong.value(0))));
}

void ColdStartBranchTests::shortLoopsWithGapsLongerThanASecond_data() {
    QTest::addColumn<double>("angle");
    QTest::addColumn<double>("gap");
    for (const double angle : {90.0, 30.0, 10.0})
        for (const double gap : {100.0, 200.0, 400.0})
            QTest::addRow("%g degrees, %g m", angle, gap) << angle << gap;
}

// On a short figure-eight a wrong cold start within reach of the time since
// the last fix was accepted, and the right fixes after it were moved on by a
// lap seconds later.
void ColdStartBranchTests::shortLoopsWithGapsLongerThanASecond() {
    QFETCH(double, angle);
    QFETCH(double, gap);
    const auto track = SyntheticTrack::figureEight(angle);
    const auto axis = track.axis();
    const double crossing = figureEightCrossing(angle);
    Trials trials;
    for (double end = -30.0; end <= 30.0; end += 2.0) {
        for (const double side : {-1.0, 0.0, 1.0}) {
            for (unsigned seed = 1; seed <= 3; ++seed) {
                const double gapEnd = crossing + end;
                const auto lap = driveTrack(track, Drive()
                                                       .withLateral([side](double) { return side; })
                                                       .withNoise(0.3)
                                                       .withSeed(seed)
                                                       .dropping([gapEnd, gap](double m) {
                                                           return m > gapEnd - gap && m < gapEnd;
                                                       }));
                trials.check(axis, measureProjection(axis, track, lap),
                             QString("gap ends %1 m from the crossing, %2 m off, seed %3")
                                 .arg(end)
                                 .arg(side)
                                 .arg(seed));
            }
        }
    }
    QCOMPARE(trials.count, 279);
    QVERIFY2(trials.wrong.isEmpty(), trials.report());
}

void ColdStartBranchTests::parallelStraightsWithARawGpsGap_data() {
    parallelStraightsNeverJumpAcross_data();
}

// A raw gap of 5 to 40 m on the straight used to forget the direction of
// travel, so the fix after it could lock on to the other straight.
void ColdStartBranchTests::parallelStraightsWithARawGpsGap() {
    QFETCH(double, separation);
    const auto track = parallelStraights(separation);
    const auto axis = track.axis();
    Trials trials;
    for (const double share : {0.55, 0.6, 0.65}) {
        for (double gapEnd = 25.0; gapEnd <= 130.0; gapEnd += 5.0) {
            for (const double gap : {5.0, 20.0, 40.0}) {
                for (unsigned seed = 1; seed <= 3; ++seed) {
                    const auto lap =
                        driveTrack(track, Drive()
                                              .withLateral(offLine(track, share * separation, 20.0))
                                              .withNoise(0.3)
                                              .withSeed(seed)
                                              .dropping([gapEnd, gap](double m) {
                                                  return m > gapEnd - gap && m < gapEnd;
                                              }));
                    trials.check(axis, measureProjection(axis, track, lap),
                                 QString("%1 m off, gap %2 m to %3 m, seed %4")
                                     .arg(share * separation)
                                     .arg(gap)
                                     .arg(gapEnd)
                                     .arg(seed));
                }
            }
        }
    }
    QCOMPARE(trials.count, 594);
    QVERIFY2(trials.wrong.isEmpty(), trials.report());
}

void ColdStartBranchTests::parallelStraightsWithAGapAtTheLapStart_data() {
    parallelStraightsNeverJumpAcross_data();
}

// The lap's first fix had no direction of travel and no bound on where it
// could be, so a lap could be moved on by whole laps or keep fixes on the
// other straight.
void ColdStartBranchTests::parallelStraightsWithAGapAtTheLapStart() {
    QFETCH(double, separation);
    const auto track = parallelStraights(separation);
    const auto axis = track.axis();
    Trials trials;
    for (const double share : {0.55, 0.6, 0.65}) {
        for (double gapEnd = 10.0; gapEnd <= 130.0; gapEnd += 10.0) {
            for (unsigned seed = 1; seed <= 3; ++seed) {
                const auto lap = driveTrack(
                    track, Drive()
                               .withLateral(offLine(track, share * separation, 5.0))
                               .withNoise(0.3)
                               .withSeed(seed)
                               .dropping([gapEnd](double m) { return m > 0.5 && m < gapEnd; }));
                trials.check(axis, measureProjection(axis, track, lap),
                             QString("%1 m off, gap to %2 m, seed %3")
                                 .arg(share * separation)
                                 .arg(gapEnd)
                                 .arg(seed));
            }
        }
    }
    QCOMPARE(trials.count, 117);
    QVERIFY2(trials.wrong.isEmpty(), trials.report());
}

void ColdStartBranchTests::longGapEndingNearTheSecondPassOfTheCrossing_data() {
    QTest::addColumn<double>("angle");
    QTest::addColumn<double>("gap");
    QTest::addRow("10 degrees, 300 m") << 10.0 << 300.0;
    QTest::addRow("10 degrees, 340 m") << 10.0 << 340.0;
    QTest::addRow("10 degrees, 400 m") << 10.0 << 400.0;
    QTest::addRow("30 degrees, 600 m") << 30.0 << 600.0;
}

// After a gap longer than (L - 60 m) / 100 m/s the cold start could match the
// other diagonal behind the car but within reach; the right fixes after it
// were out of its reach and every one was refused, so the lap ended about
// 400 m short.
void ColdStartBranchTests::longGapEndingNearTheSecondPassOfTheCrossing() {
    QFETCH(double, angle);
    QFETCH(double, gap);
    const auto track = SyntheticTrack::figureEight(angle);
    const auto axis = track.axis();
    const double crossing =
        track.lengthMeters() - 75.0; // the gate is a quarter of a diagonal past it
    Trials trials;
    for (double end = -30.0; end <= 30.0; end += 1.0) {
        for (const double side : {-1.0, 0.0, 1.0}) {
            for (unsigned seed = 1; seed <= 3; ++seed) {
                const double gapEnd = crossing + end;
                const auto lap = driveTrack(track, Drive()
                                                       .withLateral([side](double) { return side; })
                                                       .withNoise(0.3)
                                                       .withSeed(seed)
                                                       .dropping([gapEnd, gap](double m) {
                                                           return m > gapEnd - gap && m < gapEnd;
                                                       }));
                trials.check(axis, measureProjection(axis, track, lap),
                             QString("gap ends %1 m from the crossing, %2 m off, seed %3")
                                 .arg(end)
                                 .arg(side)
                                 .arg(seed));
            }
        }
    }
    QCOMPARE(trials.count, 549);
    QVERIFY2(trials.wrong.isEmpty(), trials.report());
}

// Guards: a lap with no other part of the track nearby keeps every fix after
// a gap however fast the car or long the gap.
void ColdStartBranchTests::realCarsKeepTheirFixes() {
    const auto track = openCircuit();
    const auto axis = track.axis();
    QVERIFY(axis.valid);
    const QVector<std::pair<double, QVector<double>>> cases{
        {95.0, {50.0, 200.0, 500.0, 1000.0}}, {40.0, {10.0, 30.0, 100.0, 300.0, 900.0}}};
    for (const auto &[speed, gaps] : cases) {
        for (const double gap : gaps) {
            const auto lap = driveTrack(
                track, Drive()
                           .withSpeed([speed](double) { return speed; })
                           .withNoise(0.3)
                           .dropping([gap](double m) { return m > 100.0 && m < 100.0 + gap; }));
            const auto outcome = measureProjection(axis, track, lap);
            const QByteArray name = QString("%1 m/s, gap %2 m: %3")
                                        .arg(speed)
                                        .arg(gap)
                                        .arg(outcome.describe())
                                        .toUtf8();
            QVERIFY2(outcome.projected == outcome.fixes, name);
            QVERIFY2(onTheRightBranch(axis, outcome), name);
        }
    }
}

// GPS jitter while standing trips the heading rule while locked; the cold
// start after it takes no heading from less than 0.5 m, so a car standing for
// a minute keeps most of its fixes.
void ColdStartBranchTests::standingCarKeepsMostOfItsFixes() {
    const auto track = openCircuit();
    const auto axis = track.axis();
    const double length = track.lengthMeters();
    qsizetype projected = 0, fixes = 0;
    for (unsigned seed = 1; seed <= 5; ++seed) {
        const auto lap = driveTrack(track, Drive().withNoise(0.5).withSeed(seed).following(
                                               [length](const int i) -> std::optional<double> {
                                                   const double t = i * 0.1;
                                                   if (t < 500.0 / 30)
                                                       return t * 30;
                                                   if (t < 500.0 / 30 + 60)
                                                       return 500.0;
                                                   const double meters =
                                                       500 + (t - 500.0 / 30 - 60) * 30;
                                                   if (meters > length)
                                                       return std::nullopt;
                                                   return meters;
                                               }));
        const auto outcome = measureProjection(axis, track, lap);
        QVERIFY2(onTheRightBranch(axis, outcome),
                 qPrintable(QString("seed %1: %2").arg(seed).arg(outcome.describe())));
        projected += outcome.projected;
        fixes += outcome.fixes;
    }
    qInfo("Standing a minute: %lld of %lld fixes projected", static_cast<long long>(projected),
          static_cast<long long>(fixes));
    // 6,113 before KAN-238 and 6,010 after (Telemetry: 6,215 and 6,133 with
    // its own random GPS error); 5,880 there with the jitter as a heading.
    QCOMPARE(fixes, 7115);
    QVERIFY2(projected >= 5950,
             qPrintable(QString("%1 of %2 fixes projected").arg(projected).arg(fixes)));
}

// The last fix before the lap's start gives its first fix a movement: a first
// fix 4.5 m behind it moved backwards and is refused, as any later fix would
// be. Before, the first fix had no heading.
void ColdStartBranchTests::firstFixHasTheDirectionOfTheFixBeforeTheLap() {
    const auto track = SyntheticTrack::loop({straight(300), arc(180, 200), straight(300)});
    const auto axis = track.axis();
    QVERIFY(axis.valid);
    const double length = track.lengthMeters();
    const auto lap =
        driveTrack(track, Drive().following([length](const int i) -> std::optional<double> {
            if (i == 0)
                return length - 4.5; // before the lap
            if (i == 1)
                return length - 9.0; // its first fix, 4.5 m back
            if (i < 40)
                return (i - 2) * 4.5;
            return std::nullopt;
        }));
    const auto trace = projectLapTrace(axis, lap.session, 0.05, lap.endTime());
    QVERIFY(!trace.isEmpty());
    QVERIFY(std::abs(trace.first().samples.first().telemetryTime - 0.2) < 1e-9);
}

// The lap's first fix is not bound to the axis gate: a timing line up to
// 100 m either side of it keeps every fix of the lap.
void ColdStartBranchTests::lapTimedFromAnotherLineKeepsEveryFix() {
    const auto track = openCircuit();
    const auto axis = track.axis();
    const double length = track.lengthMeters();
    const auto lap = driveTrack(
        track, Drive().withNoise(0.3).following([length](const int i) -> std::optional<double> {
            const double meters = i * 0.1 * 40.0;
            if (meters > 2 * length + 200)
                return std::nullopt;
            return meters;
        }));
    for (const double shift : {-100.0, -40.0, -20.0, 20.0, 40.0, 100.0}) {
        const double start = (length + shift) / 40.0;
        const double end = start + length / 40.0;
        const auto trace = projectLapTrace(axis, lap.session, start, end);
        qsizetype projected = 0;
        for (const auto &segment : trace)
            projected += segment.samples.size();
        const auto fixes = std::count_if(lap.times.cbegin(), lap.times.cend(),
                                         [&](double time) { return time >= start && time <= end; });
        const QByteArray name = QString("timing line %1 m from the gate").arg(shift).toUtf8();
        QVERIFY2(projected == fixes, name);
        QVERIFY2(std::abs(trace.first().samples.first().progressMeters - shift) <= 5.0, name);
        QVERIFY2(std::abs(trace.last().samples.last().progressMeters -
                          (axis.lengthMeters + shift)) <= 5.0,
                 name);
    }
}

QTEST_GUILESS_MAIN(ColdStartBranchTests)
#include "ColdStartBranchTests.moc"
