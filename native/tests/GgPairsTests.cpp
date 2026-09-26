// Timed longitudinal/lateral acceleration pairs for G-G analysis
// (native/src/telemetry/GgPairs.*, KAN-65).

#include "telemetry/GgPairs.h"

#include <QtTest>
#include <cmath>
#include <functional>
#include <limits>

using namespace FlappedEar;

namespace {

TelemetryChannel channel(const QString &name, const QString &unit, const QVector<double> &times,
    const std::function<double(double)> &value)
{
    TelemetryChannel result;
    result.name = name;
    result.unit = unit;
    for (const double time : times) {
        result.timestamps.append(time);
        result.values.append(static_cast<float>(value(time)));
    }
    return result;
}

QVector<double> clock(const double start, const double end, const double step)
{
    QVector<double> times;
    for (double time = start; time <= end + 1e-9; time += step) times.append(time);
    return times;
}

TelemetrySession sessionWith(const TelemetryChannel &longitudinal, const TelemetryChannel &lateral)
{
    TelemetrySession session;
    session.channels.insert(longitudinal.name, longitudinal);
    session.channels.insert(lateral.name, lateral);
    session.aliases.insert("longitudinalAcceleration", longitudinal.name);
    session.aliases.insert("lateralAcceleration", lateral.name);
    return session;
}

} // namespace

class GgPairsTests final : public QObject {
    Q_OBJECT
private slots:
    void pairsSamplesOnASharedClock();
    void interpolatesLateralOnAnotherClockWithoutBridgingGaps();
    void convertsOrRejectsUnits();
    void excludesOutliersAndMissingAxes();
};

void GgPairsTests::pairsSamplesOnASharedClock()
{
    // Braking (-0.8 g) while turning left (+0.6 g), then accelerating right.
    const auto times = clock(0.0, 1.0, 0.1);
    const auto session = sessionWith(
        channel("longacc", "g", times, [](double t) { return t < 0.5 ? -0.8 : 0.3; }),
        channel("latacc", "g", times, [](double t) { return t < 0.5 ? 0.6 : -0.4; }));
    const auto pairs = buildGgPairs(session, 0.0, 1.0);
    QVERIFY(pairs.valid);
    QVERIFY(pairs.sharedClock);
    QCOMPARE(pairs.points.size(), times.size());
    QCOMPARE(pairs.candidateCount, times.size());
    QCOMPARE(pairs.longitudinalChannel, QString("longacc"));
    QVERIFY(pairs.unitsDeclared);
    QCOMPARE(pairs.maximumPairingOffsetSeconds, 0.0);
    // Signs are kept as recorded.
    QVERIFY(std::abs(pairs.points.first().longitudinalG + 0.8) < 1e-6);
    QVERIFY(std::abs(pairs.points.first().lateralG - 0.6) < 1e-6);
    QVERIFY(std::abs(pairs.points.last().lateralG + 0.4) < 1e-6);
    // Only the requested range.
    QCOMPARE(buildGgPairs(session, 0.25, 0.55).points.size(), 3);
}

void GgPairsTests::interpolatesLateralOnAnotherClockWithoutBridgingGaps()
{
    // Longitudinal at 10 Hz; lateral at 20 Hz offset by 25 ms, linear in time,
    // with a 0.5 s hole from 0.4 to 0.9 s.
    const auto longitudinalTimes = clock(0.0, 1.5, 0.1);
    QVector<double> lateralTimes;
    for (double time = 0.025; time <= 1.525; time += 0.05)
        if (time < 0.4 || time > 0.9) lateralTimes.append(time);
    const auto session = sessionWith(channel("longacc", "g", longitudinalTimes, [](double) { return -0.5; }),
        channel("latacc", "g", lateralTimes, [](double t) { return t; }));
    const auto pairs = buildGgPairs(session, 0.0, 1.5);
    QVERIFY(pairs.valid);
    QVERIFY(!pairs.sharedClock);
    QVERIFY(std::abs(pairs.maximumPairingOffsetSeconds - 0.025) < 1e-6);
    for (const auto &point : pairs.points) {
        QVERIFY(std::abs(point.lateralG - point.time) < 1e-5); // interpolated exactly on a linear signal
        QVERIFY(point.time < 0.4 || point.time > 0.9);          // nothing across the hole
    }
    QVERIFY(pairs.skippedForGap >= 5);
    QCOMPARE(pairs.points.size() + pairs.skippedForGap, pairs.candidateCount);
    // No lateral sample on both sides of t = 0: skipped, not extrapolated.
    QVERIFY(pairs.points.first().time > 0.0);
}

void GgPairsTests::convertsOrRejectsUnits()
{
    const auto times = clock(0.0, 0.5, 0.1);
    auto session = sessionWith(channel("longacc", "m/s^2", times, [](double) { return -9.80665; }),
        channel("latacc", "m/s²", times, [](double) { return 4.903325; }));
    auto pairs = buildGgPairs(session, 0.0, 0.5);
    QVERIFY(pairs.valid);
    QVERIFY(std::abs(pairs.points.first().longitudinalG + 1.0) < 1e-5);
    QVERIFY(std::abs(pairs.points.first().lateralG - 0.5) < 1e-5);

    session = sessionWith(channel("longacc", "", times, [](double) { return 0.2; }),
        channel("latacc", "", times, [](double) { return 0.1; }));
    pairs = buildGgPairs(session, 0.0, 0.5);
    QVERIFY(pairs.valid);
    QVERIFY(!pairs.unitsDeclared); // kept, and reported as undeclared

    session = sessionWith(channel("longacc", "km/h", times, [](double) { return 1.0; }),
        channel("latacc", "g", times, [](double) { return 0.1; }));
    pairs = buildGgPairs(session, 0.0, 0.5);
    QVERIFY(!pairs.valid);
    QCOMPARE(pairs.unavailableReason, QString(ggUnsupportedUnit));
    QVERIFY(pairs.points.isEmpty());
}

void GgPairsTests::excludesOutliersAndMissingAxes()
{
    const auto times = clock(0.0, 0.9, 0.1);
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const auto session = sessionWith(
        channel("longacc", "g", times, [nan](double t) { return std::abs(t - 0.3) < 1e-6 ? 12.0 : std::abs(t - 0.5) < 1e-6 ? nan : -0.4; }),
        channel("latacc", "g", times, [](double) { return 0.7; }));
    const auto pairs = buildGgPairs(session, 0.0, 0.9);
    QCOMPARE(pairs.excludedOutliers, 1);         // 12 g is not plausible for a road car
    QCOMPARE(pairs.candidateCount, times.size() - 1); // the NaN sample is not a candidate
    QCOMPARE(pairs.points.size(), times.size() - 2);
    for (const auto &point : pairs.points) QVERIFY(std::abs(point.longitudinalG) <= ggPlausibleLimitG);

    TelemetrySession missingLateral;
    missingLateral.channels.insert("longacc", channel("longacc", "g", times, [](double) { return 0.1; }));
    missingLateral.aliases.insert("longitudinalAcceleration", "longacc");
    auto missing = buildGgPairs(missingLateral, 0.0, 0.9);
    QVERIFY(!missing.valid);
    QCOMPARE(missing.unavailableReason, QString(ggMissingLateral));
    QVERIFY(missing.points.isEmpty());
    QCOMPARE(buildGgPairs(TelemetrySession{}, 0.0, 1.0).unavailableReason, QString(ggMissingLongitudinal));
    QVERIFY(buildGgPairs(session, 1.0, 0.5).points.isEmpty()); // inverted range
}

QTEST_GUILESS_MAIN(GgPairsTests)
#include "GgPairsTests.moc"
