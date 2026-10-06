// Throttle pickup and downstream exit effects (native/src/telemetry/ExitMetrics.*, KAN-54):
// measured throttle first, inferred acceleration labelled, an explicit
// following-straight interval, and comparisons without assigned causes.

#include "telemetry/ExitMetrics.h"
#include "telemetry/TrackSegments.h"

#include <QtTest>
#include <cmath>
#include <functional>
#include <limits>
#include <tuple>

using namespace FlappedEar;

namespace {

constexpr double dt = 0.05;           // 20 Hz
constexpr int sampleCount = 1001;     // t = 0 .. 50 s
constexpr double lapLength = 1000.0;  // progress = 20 m/s * t
const double noData = std::numeric_limits<double>::quiet_NaN();

QString configuration() { return "compatibility-v1:" + QString(64, 'e'); }

TelemetryChannel makeChannel(const QString &name, const QString &unit, const std::function<double(int)> &value)
{
    TelemetryChannel channel;
    channel.name = name;
    channel.unit = unit;
    for (int k = 0; k < sampleCount; ++k) {
        channel.appendSample(k * dt, static_cast<float>(value(k)));
    }
    return channel;
}

// Lifted (0 %, then 5 %) in the corner, back on from k = rise: 25 % per sample to 100 %.
std::function<double(int)> throttleFrom(const int rise)
{
    return [rise](const int k) {
        if (k < 505) return 0.0;
        if (k < rise) return 5.0;
        return std::min(100.0, (k - rise + 1) * 25.0);
    };
}

double speed(const int k) { return 50.0 + k * 0.05; } // km/h: 80 at 600 m, 90 at 800 m

TelemetrySession sessionWith(const QVector<QPair<QString, TelemetryChannel>> &aliased)
{
    TelemetrySession session;
    for (const auto &[alias, channel] : aliased) {
        session.channels.insert(channel.name, channel);
        session.aliases.insert(alias, channel.name);
    }
    session.duration = (sampleCount - 1) * dt;
    return session;
}

TelemetrySession measuredSession(const int rise = 540, const std::function<double(int)> &speedProfile = speed)
{
    return sessionWith({{"throttle", makeChannel("throttle_pos", "%", throttleFrom(rise))},
        {"speed", makeChannel("velocity", "km/h", speedProfile)}});
}

QVector<ProgressSegment> projectedLap(const double holeFrom = -1.0, const double holeTo = -1.0)
{
    QVector<ProgressSegment> lap(1);
    for (int k = 0; k < sampleCount; ++k) {
        const double time = k * dt;
        if (time > holeFrom && time < holeTo) {
            if (!lap.last().samples.isEmpty()) lap.append(ProgressSegment{});
            continue;
        }
        lap.last().samples.append({time, 20.0 * time, true});
    }
    if (lap.last().samples.isEmpty()) lap.removeLast();
    return lap;
}

ApprovedSegmentation approvedOf(const QVector<std::tuple<TrackSegmentType, double, double>> &segments)
{
    QJsonArray array;
    int number = 1;
    for (const auto &[type, start, end] : segments)
        array.append(makeTrackSegment(type, QString("S%1").arg(number++), start, end, configuration()));
    return approvedSegmentation(array, configuration());
}

QString firstId(const ApprovedSegmentation &approved) { return approved.segments.first().toObject().value("id").toString(); }

const auto cornerAndStraight = [] {
    return approvedOf({{TrackSegmentType::Corner, 500.0, 600.0}, {TrackSegmentType::Straight, 600.0, 800.0}});
};

} // namespace

class ExitMetricsTests final : public QObject {
    Q_OBJECT
private slots:
    void measuresPickupAndTheFollowingStraight();
    void labelsAccelerationBasedPickupAsInferred();
    void handlesLiftsSpikesGapsAndUnits();
    void definesTheDownstreamIntervalExplicitly();
    void comparesWithoutAssigningCause();
};

void ExitMetricsTests::measuresPickupAndTheFollowingStraight()
{
    const auto approved = cornerAndStraight();
    const auto metrics = computeExitMetrics(lapLength, approved, firstId(approved), projectedLap(), measuredSession(), 50.0);
    QVERIFY(metrics.valid);
    QCOMPARE(metrics.pickup.method, QString(pickupMethodMeasured));
    QCOMPARE(metrics.pickup.provenance, QString("measured"));
    QCOMPARE(metrics.pickup.unit, QString("%"));
    // 20 % is crossed between 5 % at 26.95 s and 25 % at 27.00 s: 26.9875 s, 539.75 m.
    QVERIFY2(metrics.pickup.telemetryTime, qPrintable(metrics.pickup.unavailableReason));
    QVERIFY(std::abs(*metrics.pickup.telemetryTime - 26.9875) < 1e-3);
    QVERIFY(std::abs(*metrics.pickup.progressMeters - 539.75) < 0.05);
    QVERIFY(metrics.pickup.limitations.isEmpty());

    QCOMPARE(metrics.intervalSource, QString(intervalFollowingStraight));
    QCOMPARE(metrics.intervalStartMeters, 600.0);
    QCOMPARE(metrics.intervalEndMeters, 800.0);
    QCOMPARE(metrics.speedUnit, QString("km/h"));
    QVERIFY(std::abs(*metrics.exitSpeed - 80.0) < 1e-3);
    QVERIFY(std::abs(*metrics.intervalEndSpeed - 90.0) < 1e-3);
    QVERIFY(std::abs(*metrics.elapsedSeconds - 10.0) < 1e-9);
    QVERIFY(metrics.downstreamUnavailableReason.isEmpty());
    QCOMPARE(metrics.stamp.calculationAlgorithm, QString(exitMetricsAlgorithm));
}

void ExitMetricsTests::labelsAccelerationBasedPickupAsInferred()
{
    const auto approved = cornerAndStraight();
    const auto session = sessionWith({{"longitudinalAcceleration", makeChannel("longacc", "g",
        [](int k) { return k < 540 ? -0.5 : k < 545 ? 0.0 : 0.3; })}});
    const auto metrics = computeExitMetrics(lapLength, approved, firstId(approved), projectedLap(), session, 50.0);
    QCOMPARE(metrics.pickup.method, QString(pickupMethodInferred));
    QCOMPARE(metrics.pickup.provenance, QString("inferred"));
    QVERIFY(metrics.pickup.progressMeters);
    QVERIFY(std::abs(*metrics.pickup.progressMeters - 544.33) < 0.1);
    // No speed channel: elapsed time is still measured, speeds are not invented.
    QVERIFY(metrics.elapsedSeconds);
    QVERIFY(!metrics.exitSpeed && !metrics.intervalEndSpeed);
    QCOMPARE(metrics.downstreamUnavailableReason, QString(exitSpeedChannelMissing));

    const auto nothing = computeExitMetrics(lapLength, approved, firstId(approved), projectedLap(), sessionWith({}), 50.0);
    QCOMPARE(nothing.pickup.unavailableReason, QString(exitNoChannel));
}

void ExitMetricsTests::handlesLiftsSpikesGapsAndUnits()
{
    const auto approved = cornerAndStraight();
    const auto id = firstId(approved);
    const auto flatOut = sessionWith({{"throttle", makeChannel("throttle_pos", "%", [](int) { return 100.0; })}});
    QCOMPARE(computeExitMetrics(lapLength, approved, id, projectedLap(), flatOut).pickup.unavailableReason, QString(exitNoLift));

    const auto neverBack = sessionWith({{"throttle", makeChannel("throttle_pos", "%", [](int k) { return k < 505 ? 0.0 : 5.0; })}});
    QCOMPARE(computeExitMetrics(lapLength, approved, id, projectedLap(), neverBack).pickup.unavailableReason, QString(exitNoPickup));

    // A one-sample blip is not a pickup; the real one follows.
    const auto spiky = sessionWith({{"throttle", makeChannel("throttle_pos", "%",
        [](int k) { return k == 520 ? 50.0 : throttleFrom(540)(k); })}});
    const auto afterSpike = computeExitMetrics(lapLength, approved, id, projectedLap(), spiky);
    QVERIFY(std::abs(*afterSpike.pickup.progressMeters - 539.75) < 0.05);

    // Missing throttle samples around the rise: reported where data resumes, flagged.
    const auto gapped = sessionWith({{"throttle", makeChannel("throttle_pos", "%",
        [](int k) { return k >= 538 && k <= 541 ? noData : throttleFrom(540)(k); })}});
    const auto afterGap = computeExitMetrics(lapLength, approved, id, projectedLap(), gapped);
    QVERIFY(afterGap.pickup.progressMeters);
    QVERIFY(std::abs(*afterGap.pickup.progressMeters - 542.0) < 0.05);
    QVERIFY(afterGap.pickup.limitations.contains(exitFollowsGap));

    const auto raw = sessionWith({{"throttle", makeChannel("throttle_pos", "raw", throttleFrom(540))}});
    QCOMPARE(computeExitMetrics(lapLength, approved, id, projectedLap(), raw).pickup.unavailableReason, QString(exitUnitMismatch));
    const auto undeclared = sessionWith({{"throttle", makeChannel("throttle_pos", "", throttleFrom(540))}});
    QVERIFY(computeExitMetrics(lapLength, approved, id, projectedLap(), undeclared).pickup.limitations.contains(exitUnitUndeclared));
}

void ExitMetricsTests::definesTheDownstreamIntervalExplicitly()
{
    const auto session = measuredSession();
    const auto cornerOnly = approvedOf({{TrackSegmentType::Corner, 500.0, 600.0}});
    const auto fixed = computeExitMetrics(lapLength, cornerOnly, firstId(cornerOnly), projectedLap(), session, 50.0);
    QCOMPARE(fixed.intervalSource, QString(intervalFixedDistance));
    QCOMPARE(fixed.intervalEndMeters, 800.0);
    QVERIFY(std::abs(*fixed.elapsedSeconds - 10.0) < 1e-9);

    // A straight ending at the gate ends at the lap's timed end.
    const auto toGate = approvedOf({{TrackSegmentType::Corner, 500.0, 600.0}, {TrackSegmentType::Straight, 600.0, lapLength}});
    const auto atGate = computeExitMetrics(lapLength, toGate, firstId(toGate), projectedLap(), session, 50.0);
    QCOMPARE(atGate.intervalEndMeters, lapLength);
    QVERIFY(std::abs(*atGate.elapsedSeconds - 20.0) < 1e-9);
    QCOMPARE(computeExitMetrics(lapLength, toGate, firstId(toGate), projectedLap(), session).downstreamUnavailableReason,
        QString(exitIncompleteCoverage));

    // A corner ending at the gate searches for pickup up to the lap's timed end,
    // even though the projection stops short of the gate.
    const auto lastCorner = approvedOf({{TrackSegmentType::Corner, 900.0, lapLength}});
    const auto shortEnd = projectedLap(49.7, 51.0);
    QCOMPARE(computeExitMetrics(lapLength, lastCorner, firstId(lastCorner), shortEnd, measuredSession(960))
                 .pickup.unavailableReason,
        QString(exitIncompleteCoverage));
    const auto lastPickup = computeExitMetrics(lapLength, lastCorner, firstId(lastCorner), shortEnd, measuredSession(960), 50.0);
    QVERIFY2(lastPickup.pickup.unavailableReason.isEmpty(), qPrintable(lastPickup.pickup.unavailableReason));
    QVERIFY(lastPickup.pickup.progressMeters);
    QVERIFY(*lastPickup.pickup.progressMeters > 950.0 && *lastPickup.pickup.progressMeters < 965.0);

    const auto late = approvedOf({{TrackSegmentType::Corner, 900.0, 950.0}});
    QCOMPARE(computeExitMetrics(lapLength, late, firstId(late), projectedLap(), session, 50.0).downstreamUnavailableReason,
        QString(exitCrossesGate));

    // A projection hole inside the interval: no elapsed time, the exit speed stays.
    const auto approved = cornerAndStraight();
    const auto holed = computeExitMetrics(lapLength, approved, firstId(approved), projectedLap(35.0, 36.0), session, 50.0);
    QVERIFY(!holed.elapsedSeconds);
    QCOMPARE(holed.downstreamUnavailableReason, QString(exitIncompleteCoverage));
    QVERIFY(holed.exitSpeed);

    const auto wrapping = approvedOf({{TrackSegmentType::Corner, 950.0, 50.0}});
    const auto wrapped = computeExitMetrics(lapLength, wrapping, firstId(wrapping), projectedLap(), session, 50.0);
    QCOMPARE(wrapped.pickup.unavailableReason, QString(exitCrossesGate));
    QCOMPARE(wrapped.downstreamUnavailableReason, QString(exitCrossesGate));

    QVERIFY(!computeExitMetrics(lapLength, approved, "missing", projectedLap(), session).valid);
    ExitMetricsOptions bad;
    bad.followMeters = 0.0;
    QVERIFY(!computeExitMetrics(lapLength, approved, firstId(approved), projectedLap(), session, 50.0, bad).valid);
}

void ExitMetricsTests::comparesWithoutAssigningCause()
{
    const auto approved = cornerAndStraight();
    const auto id = firstId(approved);
    const auto a = computeExitMetrics(lapLength, approved, id, projectedLap(), measuredSession(540), 50.0);
    const auto b = computeExitMetrics(lapLength, approved, id, projectedLap(),
        measuredSession(560, [](int k) { return speed(k) - 5.0; }), 50.0);
    const auto comparison = compareExitMetrics(a, b);
    QVERIFY(comparison.valid);
    QVERIFY(std::abs(*comparison.pickupDeltaMeters + 20.0) < 0.05); // A picks up 20 m earlier
    QVERIFY(std::abs(*comparison.exitSpeedDelta - 5.0) < 1e-3);
    QVERIFY(std::abs(*comparison.intervalEndSpeedDelta - 5.0) < 1e-3);
    QVERIFY(std::abs(*comparison.elapsedSecondsDelta) < 1e-9);

    const auto inferred = computeExitMetrics(lapLength, approved, id, projectedLap(),
        sessionWith({{"longitudinalAcceleration", makeChannel("longacc", "g", [](int k) { return k < 545 ? 0.0 : 0.3; })},
            {"speed", makeChannel("velocity", "km/h", speed)}}), 50.0);
    const auto mixed = compareExitMetrics(a, inferred);
    QVERIFY(mixed.valid);
    QCOMPARE(mixed.pickupUnavailableReason, QString(exitMixedProvenance));
    QVERIFY(!mixed.pickupDeltaMeters);
    QVERIFY(mixed.exitSpeedDelta); // speeds still compare: same recorded channel and unit

    const auto other = approvedOf({{TrackSegmentType::Corner, 500.0, 610.0}});
    QVERIFY(!compareExitMetrics(a, computeExitMetrics(lapLength, other, firstId(other), projectedLap(), measuredSession(), 50.0)).valid);
}

QTEST_GUILESS_MAIN(ExitMetricsTests)
#include "ExitMetricsTests.moc"
