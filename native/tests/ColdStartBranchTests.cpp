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

#include "SyntheticTrackFixture.h"

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

using namespace SyntheticTrackFixture;

namespace {

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
        QVERIFY2(outcome.largestBackwardStep <= branchError,
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
