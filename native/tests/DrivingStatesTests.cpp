// Driving states with explicit provenance (native/src/telemetry/DrivingStates.*, KAN-91):
// braking, accelerating, cornering and coasting from measured pedals or,
// labelled inferred, from longitudinal G; hysteresis, gaps and units handled.

#include "telemetry/CoastingAnalysis.h"
#include "telemetry/DrivingStates.h"
#include "telemetry/LapTiming.h"
#include "telemetry/TelemetrySource.h"

#include <QDir>
#include <QJsonObject>
#include <tuple>

#include <QtTest>
#include <cmath>
#include <functional>
#include <limits>

using namespace FlappedEar;

namespace {

constexpr double dt = 0.05; // 20 Hz
constexpr int sampleCount = 201; // t = 0 .. 10 s

TelemetryChannel makeChannel(const QString &name, const QString &unit, const std::function<double(double)> &value,
    const std::function<bool(double)> &present = [](double) { return true; })
{
    TelemetryChannel channel;
    channel.name = name;
    channel.unit = unit;
    for (int k = 0; k < sampleCount; ++k) {
        const double t = k * dt;
        if (!present(t)) continue;
        channel.timestamps.append(t);
        channel.values.append(static_cast<float>(value(t)));
    }
    return channel;
}

TelemetrySession sessionWith(const QVector<QPair<QString, TelemetryChannel>> &aliased)
{
    TelemetrySession session;
    for (const auto &[alias, channel] : aliased) {
        session.channels.insert(channel.name, channel);
        session.aliases.insert(alias, channel.name);
    }
    return session;
}

// A lap slice: throttle to 4 s, brake 5-7 s, throttle again from 8 s,
// cornering 4.5-7.5 s (trail braking), coasting 4-5 s and 7-8 s.
double brake(double t) { return t > 5.0 && t < 7.0 ? 60.0 : 0.0; }
double throttle(double t) { return t < 4.0 || t > 8.0 ? 90.0 : 0.0; }
double lateral(double t) { return t > 4.5 && t < 7.5 ? -0.9 : 0.05; }
double longitudinal(double t) { return t < 4.0 || t > 8.0 ? 0.25 : t > 5.0 && t < 7.0 ? -0.7 : 0.0; }
double speed(double) { return 110.0; }

bool covers(const QVector<DrivingStateInterval> &intervals, double from, double to)
{
    return std::any_of(intervals.cbegin(), intervals.cend(),
        [&](const DrivingStateInterval &i) { return i.start <= from + 1e-6 && i.end >= to - 1e-6; });
}
bool touches(const QVector<DrivingStateInterval> &intervals, double from, double to)
{
    return std::any_of(intervals.cbegin(), intervals.cend(),
        [&](const DrivingStateInterval &i) { return i.end > from && i.start < to; });
}

} // namespace

class DrivingStatesTests final : public QObject {
    Q_OBJECT
private slots:
    void classifiesMeasuredPedalsWithTrailBraking();
    void infersPedalsFromAccelerationWithoutClaimingMeasurement();
    void leavesGapsAndUnusableChannelsUnknown();
    void separatesOverlapsAndSpikes();
    void summarizesCoastingBySegmentAndLap();
    void measuresBrakingWhileCornering();
    void classifiesPrivateBestLaps();
};

void DrivingStatesTests::classifiesMeasuredPedalsWithTrailBraking()
{
    const auto session = sessionWith({{"brake", makeChannel("brake_pos-obd", "%", brake)},
        {"throttle", makeChannel("accelerator_pos-obd", "%", throttle)},
        {"lateralAcceleration", makeChannel("latacc-calc", "g", lateral)},
        {"longitudinalAcceleration", makeChannel("longacc-calc", "g", longitudinal)},
        {"speed", makeChannel("velocity", "km/h", speed)}});
    const auto states = classifyDrivingStates(session, 0.0, 10.0);
    QVERIFY(states.valid);
    QCOMPARE(states.algorithm, QString("driving-states-v1"));
    QCOMPARE(states.braking.provenance, QString("measured"));
    QCOMPARE(states.braking.channel, QString("brake_pos-obd"));
    QCOMPARE(states.accelerating.provenance, QString("measured"));
    // RaceChrono's GPS-derived lateral G is calculated, not a sensor reading.
    QCOMPARE(states.cornering.provenance, QString("calculated"));
    QCOMPARE(states.coasting.provenance, QString("measured"));

    QCOMPARE(states.braking.active.size(), 1);
    QVERIFY(covers(states.braking.active, 5.1, 6.9));
    QCOMPARE(states.accelerating.active.size(), 2);
    QVERIFY(covers(states.cornering.active, 4.6, 7.4));
    // Trail braking: cornering and braking overlap.
    QVERIFY(touches(states.cornering.active, 5.1, 6.9) && touches(states.braking.active, 5.1, 6.9));
    // Coasting between throttle and brake, and between brake and throttle.
    QVERIFY(covers(states.coasting.active, 4.1, 4.9));
    QVERIFY(covers(states.coasting.active, 7.1, 7.9));
    QVERIFY(!touches(states.coasting.active, 5.1, 6.9)); // never while braking
    QVERIFY(covers(states.braking.known, 0.0, 10.0));
}

void DrivingStatesTests::infersPedalsFromAccelerationWithoutClaimingMeasurement()
{
    const auto session = sessionWith({{"lateralAcceleration", makeChannel("latacc", "g", lateral)},
        {"longitudinalAcceleration", makeChannel("longacc", "g", longitudinal)},
        {"speed", makeChannel("velocity", "km/h", speed)}});
    const auto states = classifyDrivingStates(session, 0.0, 10.0);
    QCOMPARE(states.braking.provenance, QString("inferred"));
    QCOMPARE(states.accelerating.provenance, QString("inferred"));
    QCOMPARE(states.coasting.provenance, QString("inferred"));
    QCOMPARE(states.cornering.provenance, QString("measured")); // a sensor channel, not "-calc"
    QVERIFY(covers(states.braking.active, 5.1, 6.9));
    QVERIFY(covers(states.accelerating.active, 0.0, 3.9));
    // One acceleration channel cannot show braking and accelerating together.
    for (const auto &braking : states.braking.active)
        QVERIFY(!touches(states.accelerating.active, braking.start, braking.end));
    QVERIFY(covers(states.coasting.active, 4.1, 4.9));

    DrivingStateOptions noInference;
    noInference.allowInferred = false;
    const auto strict = classifyDrivingStates(session, 0.0, 10.0, noInference);
    QCOMPARE(strict.braking.provenance, QString("unknown"));
    QCOMPARE(strict.braking.unresolvedReason, QString("inferenceDisabled"));
    QVERIFY(strict.braking.active.isEmpty());
    QCOMPARE(strict.coasting.unresolvedReason, QString("pedalStateUnknown"));
}

void DrivingStatesTests::leavesGapsAndUnusableChannelsUnknown()
{
    // Brake samples missing 2-3 s: unknown there, and no coasting either.
    const auto gapped = sessionWith({{"brake", makeChannel("brake_pos-obd", "%", brake, [](double t) { return t < 2.0 || t > 3.0; })},
        {"throttle", makeChannel("accelerator_pos-obd", "%", [](double t) { return t > 1.0 && t < 4.0 ? 0.0 : 90.0; })},
        {"speed", makeChannel("velocity", "km/h", speed)}});
    const auto states = classifyDrivingStates(gapped, 0.0, 10.0);
    QCOMPARE(states.braking.known.size(), 2);
    QVERIFY(!touches(states.braking.known, 2.05, 2.95));
    QVERIFY(covers(states.coasting.active, 1.1, 1.9));
    QVERIFY(!touches(states.coasting.active, 2.05, 2.95));
    QVERIFY(covers(states.coasting.active, 3.1, 3.9));
    QCOMPARE(states.cornering.provenance, QString("unknown"));
    QCOMPARE(states.cornering.unresolvedReason, QString("noLateralAccelerationChannel"));

    // A brake recorded in bar is not a percentage: unknown, never rescaled.
    const auto bar = sessionWith({{"brake", makeChannel("brake_pressure", "bar", brake)},
        {"throttle", makeChannel("accelerator_pos-obd", "%", throttle)},
        {"longitudinalAcceleration", makeChannel("longacc", "g", longitudinal)},
        {"speed", makeChannel("velocity", "km/h", speed)}});
    const auto mismatched = classifyDrivingStates(bar, 0.0, 10.0);
    QCOMPARE(mismatched.braking.provenance, QString("unknown"));
    QCOMPARE(mismatched.braking.unresolvedReason, QString("unitMismatch"));
    QVERIFY(mismatched.braking.active.isEmpty()); // no fallback to deceleration for a present brake channel
    QCOMPARE(mismatched.coasting.unresolvedReason, QString("pedalStateUnknown"));

    // Slow: never coasting in the pit lane.
    const auto slow = sessionWith({{"brake", makeChannel("brake_pos-obd", "%", [](double) { return 0.0; })},
        {"throttle", makeChannel("accelerator_pos-obd", "%", [](double) { return 0.0; })},
        {"speed", makeChannel("velocity", "km/h", [](double t) { return t < 5.0 ? 5.0 : 60.0; })}});
    const auto pit = classifyDrivingStates(slow, 0.0, 10.0);
    QVERIFY(!touches(pit.coasting.active, 0.0, 4.9));
    QVERIFY(covers(pit.coasting.active, 5.1, 9.9));

    QVERIFY(!classifyDrivingStates(slow, 5.0, 5.0).valid);
}

void DrivingStatesTests::separatesOverlapsAndSpikes()
{
    // Left-foot braking on the throttle: both measured, both shown.
    const auto overlap = sessionWith({{"brake", makeChannel("brake_pos-obd", "%", [](double t) { return t > 5.0 && t < 6.0 ? 30.0 : 0.0; })},
        {"throttle", makeChannel("accelerator_pos-obd", "%", [](double t) { return t > 4.0 && t < 7.0 ? 50.0 : 0.0; })},
        {"speed", makeChannel("velocity", "km/h", speed)}});
    const auto states = classifyDrivingStates(overlap, 0.0, 10.0);
    QVERIFY(covers(states.braking.active, 5.1, 5.9));
    QVERIFY(covers(states.accelerating.active, 5.1, 5.9));
    QVERIFY(!touches(states.coasting.active, 4.05, 6.95));

    // A one-sample brake blip is a spike, not a braking episode.
    const auto blip = sessionWith({{"brake", makeChannel("brake_pos-obd", "%", [](double t) { return std::abs(t - 3.0) < 0.01 ? 50.0 : 0.0; })},
        {"throttle", makeChannel("accelerator_pos-obd", "%", [](double) { return 0.0; })},
        {"speed", makeChannel("velocity", "km/h", speed)}});
    const auto spiked = classifyDrivingStates(blip, 0.0, 10.0);
    QVERIFY(spiked.braking.active.isEmpty());
    QCOMPARE(spiked.braking.rejectedSpikes, 1);
}

void DrivingStatesTests::summarizesCoastingBySegmentAndLap()
{
    // KAN-92: at 36 km/h (10 m/s) the lap coasts 4-5 s and 7-8 s. Progress
    // is 10 m per second, so those are 40-50 m and 70-80 m, which fall in
    // the "Approach" and "Exit" segments.
    const auto session = sessionWith({{"brake", makeChannel("brake_pos-obd", "%", brake)},
        {"throttle", makeChannel("accelerator_pos-obd", "%", throttle)},
        {"speed", makeChannel("velocity", "km/h", [](double) { return 36.0; })}});
    QVector<ProgressSegment> trace(1);
    for (int k = 0; k < sampleCount; ++k) {
        ProjectedSample sample;
        sample.telemetryTime = k * dt;
        sample.progressMeters = k * dt * 10.0;
        trace[0].samples.append(sample);
    }
    ApprovedSegmentation approved;
    approved.valid = true;
    for (const auto &[id, start, end] : {std::tuple{"approach", 0.0, 50.0}, std::tuple{"corner", 50.0, 70.0},
                                         std::tuple{"exit", 70.0, 100.0}})
        approved.segments.append(QJsonObject{{"id", id}, {"name", QString(id).toUpper()}, {"type", "sector"},
            {"startProgressMeters", start}, {"endProgressMeters", end}});
    const auto summary = summarizeCoasting(session, 0.0, 10.0, &trace, &approved);
    QVERIFY(summary.valid);
    QCOMPARE(summary.algorithm, QString("coasting-v1"));
    QCOMPARE(summary.provenance, QString("measured"));
    QCOMPARE(summary.episodes.size(), 2);
    QVERIFY(std::abs(summary.coastingSeconds - 2.0) < 0.15);
    QVERIFY(std::abs(summary.coastingMeters - 20.0) < 1.5);
    QCOMPARE(summary.episodes[0].segmentId, QString("approach"));
    QCOMPARE(summary.episodes[1].segmentId, QString("exit"));
    QVERIFY(std::abs(*summary.episodes[0].startProgressMeters - 40.0) < 1.0);
    QCOMPARE(summary.segments.size(), 3); // every segment listed, zero rows kept
    QCOMPARE(summary.segments[1].segmentId, QString("corner"));
    QCOMPARE(summary.segments[1].episodes, 0);
    QVERIFY(summary.segments[1].seconds < 0.1);
    QVERIFY(std::abs(summary.segments[0].meters - 10.0) < 1.5);
    QVERIFY(std::abs(summary.segments[2].meters - 10.0) < 1.5);
    QVERIFY(std::abs(summary.knownSeconds - 10.0) < 0.1);

    // Without progress or segments the lap totals remain.
    const auto bare = summarizeCoasting(session, 0.0, 10.0);
    QCOMPARE(bare.episodes.size(), 2);
    QVERIFY(bare.segments.isEmpty());
    QVERIFY(!bare.episodes[0].startProgressMeters);

    // Without pedals and acceleration it cannot be told: unknown, not zero.
    const auto unknown = summarizeCoasting(sessionWith({{"speed", makeChannel("velocity", "km/h", speed)}}), 0.0, 10.0);
    QCOMPARE(unknown.provenance, QString("unknown"));
    QCOMPARE(unknown.unresolvedReason, QString("pedalStateUnknown"));
    QVERIFY(unknown.episodes.isEmpty());
}

void DrivingStatesTests::measuresBrakingWhileCornering()
{
    // KAN-93: braking 5-7 s and cornering 4.5-7.5 s overlap for 2 s; at
    // 110 km/h that is 61.1 m.
    const auto session = sessionWith({{"brake", makeChannel("brake_pos-obd", "%", brake)},
        {"throttle", makeChannel("accelerator_pos-obd", "%", throttle)},
        {"lateralAcceleration", makeChannel("latacc-calc", "g", lateral)},
        {"speed", makeChannel("velocity", "km/h", speed)}});
    const auto states = classifyDrivingStates(session, 0.0, 10.0);
    const auto overlap = overlapOf(states.braking.active, states.cornering.active);
    QCOMPARE(overlap.size(), 1);
    QVERIFY(std::abs(overlap[0].end - overlap[0].start - 2.0) < 0.15);
    QVERIFY(std::abs(travelledMeters(session, overlap) - 110.0 / 3.6 * 2.0) < 4.0);
    QVERIFY(overlapOf(states.braking.active, {}).isEmpty());
    QCOMPARE(travelledMeters(sessionWith({}), overlap), 0.0); // no speed: nothing invented
}

void DrivingStatesTests::classifiesPrivateBestLaps()
{
    // Opt-in: each session's best lap of a real day (FLAPPEDEAR_REAL_DAY,
    // private VBO recordings) with every state's share of the lap.
    const auto path = qEnvironmentVariable("FLAPPEDEAR_REAL_DAY");
    if (path.isEmpty()) QSKIP("FLAPPEDEAR_REAL_DAY is not set");
    const auto share = [](const QVector<DrivingStateInterval> &intervals, double length) {
        double total = 0; for (const auto &i : intervals) total += i.end - i.start; return 100.0 * total / length;
    };
    for (const auto &file : QDir(path).entryInfoList({"*.vbo"}, QDir::Files, QDir::Name)) {
        const auto session = TelemetrySource::load(file.absoluteFilePath());
        const auto laps = deriveSourceLapSession(session);
        if (!laps.fastestLapIndex) continue;
        const auto &lap = laps.timedLaps[*laps.fastestLapIndex];
        const auto states = classifyDrivingStates(session, lap.startTelemetryTime, lap.endTelemetryTime);
        QVERIFY(states.valid);
        const double length = lap.endTelemetryTime - lap.startTelemetryTime;
        qInfo().noquote() << QString("%1 lap %2 %3 s: braking %4% (%5) · accelerating %6% (%7) · cornering %8% (%9) · coasting %10% (%11) · known %12%")
            .arg(file.baseName().left(24)).arg(lap.number).arg(length, 0, 'f', 3)
            .arg(share(states.braking.active, length), 0, 'f', 1).arg(states.braking.provenance)
            .arg(share(states.accelerating.active, length), 0, 'f', 1).arg(states.accelerating.provenance)
            .arg(share(states.cornering.active, length), 0, 'f', 1).arg(states.cornering.provenance)
            .arg(share(states.coasting.active, length), 0, 'f', 1).arg(states.coasting.provenance)
            .arg(share(states.coasting.known, length), 0, 'f', 1);
        QCOMPARE(states.braking.provenance, QString("measured"));
        QCOMPARE(states.accelerating.provenance, QString("measured"));
        QVERIFY(!states.braking.active.isEmpty() && !states.accelerating.active.isEmpty() && !states.cornering.active.isEmpty());
    }
}

QTEST_GUILESS_MAIN(DrivingStatesTests)
#include "DrivingStatesTests.moc"
