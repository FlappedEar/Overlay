// Truth tests for KAN-239 (Telemetry FET-257): while locked, a fix a few metres
// off the reference line is kept when no other part of the whole axis is
// nearly as near and it does not lie well inside a bend, and a fix beyond the
// search window's forward end is refused.
//
// Before, the runner-up of a fix only had to be 10 m along the axis, and on a
// straight that is the same line 10 m further on: a fix about 8 m off it was as
// near to that as to its own match, so the 0.7 ambiguity ratio refused it, long
// before the 20 m proximity. True ambiguity (a hairpin's legs, the other
// straight of a pair, a crossing) is still refused, and at 1 to 2 Hz another
// branch can lie beyond the window. A fix beyond the window's forward end used
// to be placed at the window's end.
//
// Each synthetic lap is driven with a known distance at every fix. A fix may be
// refused (a gap), never misplaced. Ported from Telemetry's
// locked_off_line_test.dart; the GPS error comes from another random
// generator, so its laps are not Telemetry's.

#include "SyntheticTrackFixture.h"

#include <QtTest>

using namespace SyntheticTrackFixture;

namespace {

// A projected fix may be this far from its true distance: 2 m axis chords plus
// GPS error.
constexpr double allowedError = 2.0;

// A 600 m-straight oval: no other part of the track within 400 m.
SyntheticTrack oval() {
    return SyntheticTrack::loop({straight(300), arc(180, 200), straight(300)});
}

// Two straights of 300 m, `separation` metres apart centre to centre and driven
// in opposite directions, joined by hairpins of half that radius.
SyntheticTrack parallelStraights(const double separation) {
    return SyntheticTrack::loop({straight(150), arc(180, separation / 2), straight(150)});
}

// A lock at `from` (moving east) and then the fix `to` `seconds` later at
// `speed`: the projected sample for `to`.
std::optional<ProjectedSample> lockedThen(const ProgressAxis &axis, const QPointF &from,
                                          const QPointF &to, const double seconds = 1.0,
                                          const double speed = 40.0) {
    ProjectionContext context;
    const auto first = projectSample(axis, from, 0.0, speed, QPointF(1.0, 0.0), context);
    if (!first.valid)
        return std::nullopt;
    const auto second = projectSample(axis, to, seconds, speed, to - from, context);
    if (!second.valid)
        return std::nullopt;
    return second;
}

// Every projected fix within the allowed error, every fix projected, one
// segment.
bool tracks(const Outcome &outcome) {
    return outcome.segments == 1 && outcome.projected == outcome.fixes &&
           outcome.maximumError <= allowedError;
}

// The largest distance of a projected fix of `lap` from where the car truly is,
// either way round the loop (a fix at the gate may be read at the end of the lap
// or at its start).
double worstError(const ProgressAxis &axis, const SyntheticTrack &track, const DrivenLap &lap) {
    const double scale = axis.lengthMeters / track.lengthMeters();
    std::map<double, double> truthAt;
    for (qsizetype i = 0; i < lap.times.size(); ++i)
        truthAt[lap.times[i]] = lap.truth[i] * scale;
    double worst = 0.0;
    for (const auto &segment : projectLapTrace(axis, lap.session, 0.0, lap.endTime())) {
        for (const auto &sample : segment.samples) {
            const double error = std::abs(sample.progressMeters - truthAt.at(sample.telemetryTime));
            worst = std::max(worst, std::min(error, std::abs(axis.lengthMeters - error)));
        }
    }
    return worst;
}

QByteArray text(const QString &value) { return value.toUtf8(); }

} // namespace

class LockedOffLineTests final : public QObject {
    Q_OBJECT
  private slots:
    void lapOffTheLineKeepsItsFixes_data();
    void lapOffTheLineKeepsItsFixes();
    void lapTowardTheOtherStraightIsRefusedThereNeverPlaced();
    void figureEightCrossingOffTheLineIsNeverMisplaced();
    void loopShorterThanTheWindowKeepsALapOffItsLine();
    void fixMidwayBetweenHairpinLegsIsRefused_data();
    void fixMidwayBetweenHairpinLegsIsRefused();
    void fixAtTheCentreOfAHairpinIsRefused_data();
    void fixAtTheCentreOfAHairpinIsRefused();
    void fixAtTheCentreOfACornerIsRefused_data();
    void fixAtTheCentreOfACornerIsRefused();
    void fixMidwayBetweenTwoStraightsInTheWindowIsRefused();
    void fixOnTheOtherLegBeyondTheWindowIsNotPlacedOnThisLeg_data();
    void fixOnTheOtherLegBeyondTheWindowIsNotPlacedOnThisLeg();
    void sparseLapsAreNeverMisplaced_data();
    void sparseLapsAreNeverMisplaced();
    void fixBeyondTheForwardWindowIsRefused();
};

void LockedOffLineTests::lapOffTheLineKeepsItsFixes_data() {
    QTest::addColumn<double>("offset");
    for (const double offset : {8.5, 10.0, 15.0})
        QTest::addRow("%g m off the line", offset) << offset;
}

void LockedOffLineTests::lapOffTheLineKeepsItsFixes() {
    QFETCH(double, offset);
    const auto track = oval();
    const auto axis = track.axis();
    QVERIFY(axis.valid);
    for (const double side : {-1.0, 1.0}) {
        for (unsigned seed = 1; seed <= 5; ++seed) {
            const auto lap =
                driveTrack(track, Drive()
                                      .withLateral([=](double) { return side * offset; })
                                      .withNoise(0.3)
                                      .withSeed(seed));
            const auto outcome = measureProjection(axis, track, lap);
            const QByteArray name = text(
                QString("%1 m, seed %2: %3").arg(side * offset).arg(seed).arg(outcome.describe()));
            QVERIFY2(tracks(outcome), name);
            QVERIFY2(outcome.largestBackwardStep == 0.0, name);
        }
    }
}

// Nearer the other straight than 0.7 of the way, a fix could be on either: the
// other straight lies beyond the window, but only the whole axis can show that
// it is not the car's.
void LockedOffLineTests::lapTowardTheOtherStraightIsRefusedThereNeverPlaced() {
    for (const double separation : {15.0, 20.0, 30.0}) {
        const auto track = parallelStraights(separation);
        const double half = track.lengthMeters() / 2;
        const double offset = separation / 1.7 + 0.5;
        const auto lap =
            driveTrack(track, Drive().withNoise(0.3).withLateral([=](const double meters) {
                const double along = std::fmod(meters, half);
                return along > 20.0 && along < 130.0 ? offset : 0.0;
            }));
        const auto outcome = measureProjection(track.axis(), track, lap);
        const QByteArray name =
            text(QString("%1 m apart: %2").arg(separation).arg(outcome.describe()));
        QVERIFY2(outcome.maximumError < allowedError, name);
        QVERIFY2(outcome.projected < outcome.fixes, name);
    }
}

// The 10 degree figure-eight's lobes have a radius of 13 m: 8.5 m inside one,
// the GPS error moves the nearest point of the axis about three times as far,
// up to 3.9 m. The other branch is hundreds of metres out.
void LockedOffLineTests::figureEightCrossingOffTheLineIsNeverMisplaced() {
    for (const double angle : {90.0, 30.0, 10.0}) {
        const auto track = SyntheticTrack::figureEight(angle);
        const auto axis = track.axis();
        for (const double side : {-1.0, 1.0}) {
            for (unsigned seed = 1; seed <= 5; ++seed) {
                const auto lap =
                    driveTrack(track, Drive()
                                          .withLateral([=](double) { return side * 8.5; })
                                          .withNoise(0.3)
                                          .withSeed(seed));
                const auto outcome = measureProjection(axis, track, lap);
                const QByteArray name =
                    text(QString("%1 degrees: %2").arg(angle).arg(outcome.describe()));
                QVERIFY2(outcome.maximumError < 5.0, name);
                QVERIFY2(outcome.largestBackwardStep == 0.0, name);
            }
        }
    }
}

// 103 m round: the window covers the whole loop and wraps.
void LockedOffLineTests::loopShorterThanTheWindowKeepsALapOffItsLine() {
    const auto track = SyntheticTrack::loop({straight(20), arc(180, 10), straight(20)});
    const auto axis = track.axis();
    QVERIFY(axis.valid);
    for (const double side : {-1.0, 1.0}) {
        const auto lap = driveTrack(
            track, Drive().withLateral([=](double) { return side * 5.0; }).withSpeed([](double) {
                return 10.0;
            }));
        const auto outcome = measureProjection(axis, track, lap);
        QVERIFY2(tracks(outcome),
                 text(QString("%1 m: %2").arg(side * 5.0).arg(outcome.describe())));
    }
}

void LockedOffLineTests::fixMidwayBetweenHairpinLegsIsRefused_data() {
    QTest::addColumn<double>("radius");
    for (const double radius : {3.0, 7.5, 15.0})
        QTest::addRow("%g m hairpin", radius) << radius;
}

// Locked on the eastbound leg 10 m before the hairpin; the next fix is 2 m
// before the hairpin's centre, as near the westbound leg as the eastbound one,
// and the window reaches both.
void LockedOffLineTests::fixMidwayBetweenHairpinLegsIsRefused() {
    QFETCH(double, radius);
    const auto track = parallelStraights(2 * radius);
    const auto axis = track.axis();
    QVERIFY(!lockedThen(axis, {140.0, 0.0}, {148.0, radius}));
    // Nearer the eastbound leg it is placed there.
    const auto near = lockedThen(axis, {140.0, 0.0}, {148.0, radius * 0.4});
    QVERIFY(near);
    QVERIFY(std::abs(near->progressMeters - 148.0 * axis.lengthMeters / track.lengthMeters()) <=
            1.0);
}

void LockedOffLineTests::fixAtTheCentreOfAHairpinIsRefused_data() {
    fixMidwayBetweenHairpinLegsIsRefused_data();
}

void LockedOffLineTests::fixAtTheCentreOfAHairpinIsRefused() {
    QFETCH(double, radius);
    const auto axis = parallelStraights(2 * radius).axis();
    QVERIFY(!lockedThen(axis, {140.0, 0.0}, {150.0, radius}));
}

void LockedOffLineTests::fixAtTheCentreOfACornerIsRefused_data() {
    QTest::addColumn<double>("radius");
    for (const double radius : {10.0, 15.0, 19.0})
        QTest::addRow("%g m corner", radius) << radius;
}

// A rounded rectangle: the first corner turns north at x = 150 m, its centre at
// (150, radius), as near all of it.
void LockedOffLineTests::fixAtTheCentreOfACornerIsRefused() {
    QFETCH(double, radius);
    const auto track = SyntheticTrack::loop(
        {straight(150), arc(90, radius), straight(100), arc(90, radius), straight(150)});
    const auto axis = track.axis();
    QVERIFY(axis.valid);
    QVERIFY(!lockedThen(axis, {130.0, 0.0}, {150.0, radius}));
}

// 1.0 s at 40 m/s: a 64 m window from x = 120 m reaches round the hairpin onto
// the westbound straight.
void LockedOffLineTests::fixMidwayBetweenTwoStraightsInTheWindowIsRefused() {
    const auto axis = parallelStraights(15.0).axis();
    QVERIFY(!lockedThen(axis, {120.0, 0.0}, {145.0, 7.5}));
}

void LockedOffLineTests::fixOnTheOtherLegBeyondTheWindowIsNotPlacedOnThisLeg_data() {
    QTest::addColumn<double>("radius");
    for (const double radius : {5.0, 6.0, 7.5, 8.0})
        QTest::addRow("%g m hairpin", radius) << radius;
}

// Locked 15 m before the hairpin, eastbound; a second later (1 Hz) the car is on
// the westbound leg, 10 m past the hairpin, exactly on the line. The window,
// sized by the 5 to 17 m the GPS moved, ends before the hairpin; the eastbound
// leg 2 x radius away was the nearest point in it, and was taken.
void LockedOffLineTests::fixOnTheOtherLegBeyondTheWindowIsNotPlacedOnThisLeg() {
    QFETCH(double, radius);
    const auto track = parallelStraights(2 * radius);
    const auto axis = track.axis();
    const double scale = axis.lengthMeters / track.lengthMeters();
    const QPointF to(140.0, 2 * radius);
    const QPointF moved(5.0, 2 * radius);
    const double speed = std::hypot(moved.x(), moved.y());
    const auto fix = lockedThen(axis, {135.0, 0.0}, to, 1.0, speed);
    const double truth = (150.0 + std::numbers::pi * radius + 10.0) * scale;
    QVERIFY2(!fix || std::abs(fix->progressMeters - truth) < allowedError,
             text(QString("placed at %1 m, truly at %2 m")
                      .arg(fix ? fix->progressMeters : 0.0)
                      .arg(truth)));
}

void LockedOffLineTests::sparseLapsAreNeverMisplaced_data() {
    QTest::addColumn<int>("shape"); // 0..2: hairpins of 6, 7.5 and 8 m; 3: a 20 degree figure-eight
    QTest::addRow("a 6 m hairpin") << 0;
    QTest::addRow("a 7.5 m hairpin") << 1;
    QTest::addRow("an 8 m hairpin") << 2;
    QTest::addRow("a 20 degree figure-eight with 80 m diagonals") << 3;
}

// Hairpins of 6 to 8 m (parallel straights 12 to 16 m apart) and a 20 degree
// figure-eight with 80 m diagonals, driven at 1 Hz and 2 Hz on the line and 3 to
// 4 m either side of it, at four top speeds, without and with 1 m of GPS error:
// no projected fix is more than 10 m from where the car is.
void LockedOffLineTests::sparseLapsAreNeverMisplaced() {
    QFETCH(int, shape);
    const double radii[] = {6.0, 7.5, 8.0};
    const auto track =
        shape < 3 ? parallelStraights(2 * radii[shape]) : SyntheticTrack::figureEight(20.0, 80.0);
    const double off = shape < 3 ? 3.0 : 4.0;
    const auto axis = track.axis();
    QVERIFY(axis.valid);
    for (const double interval : {1.0, 0.5}) {
        for (const double offset : {0.0, off, -off}) {
            for (const double top : {36.0, 40.0, 44.0, 48.0}) {
                for (unsigned seed = 1; seed <= 3; ++seed) {
                    const auto lap =
                        driveTrack(track, Drive()
                                              .every(interval)
                                              .withNoise(seed == 1 ? 0.0 : 1.0)
                                              .withSeed(seed)
                                              .withLateral([=](double) { return offset; })
                                              .withSpeed([&track, top](const double meters) {
                                                  return track.cornerSpeed(meters, top);
                                              }));
                    const double worst = worstError(axis, track, lap);
                    QVERIFY2(worst < 10.0, text(QString("%1 s, %2 m, %3 m/s, seed %4: %5 m out")
                                                    .arg(interval)
                                                    .arg(offset)
                                                    .arg(top)
                                                    .arg(seed)
                                                    .arg(worst)));
                }
            }
        }
    }
}

// 45 m/s with 3.4 to 3.8 s between fixes: 153 to 171 m, beyond the 150 m cap of
// the window; the fix is within the 20 m proximity of its end.
void LockedOffLineTests::fixBeyondTheForwardWindowIsRefused() {
    const auto track = oval();
    const auto axis = track.axis();
    for (const double interval : {3.4, 3.5, 3.6, 3.8}) {
        const auto lap =
            driveTrack(track, Drive().every(interval).withSpeed([](double) { return 45.0; }));
        const auto outcome = measureProjection(axis, track, lap);
        const QByteArray name = text(QString("%1 s: %2").arg(interval).arg(outcome.describe()));
        QVERIFY2(outcome.maximumError < allowedError, name);
        QVERIFY2(outcome.projected > 0, name);
    }
    // A single step: locked at 100 m, the next fix 160 m on.
    QVERIFY(!lockedThen(axis, {100.0, 0.0}, {260.0, 0.0}, 3.0, 30.0));
    // Inside the window it is placed where it is.
    const auto inside = lockedThen(axis, {100.0, 0.0}, {240.0, 0.0}, 3.0, 30.0);
    QVERIFY(inside);
    QVERIFY(std::abs(inside->progressMeters - 240.0 * axis.lengthMeters / track.lengthMeters()) <=
            1.0);
}

QTEST_GUILESS_MAIN(LockedOffLineTests)
#include "LockedOffLineTests.moc"
