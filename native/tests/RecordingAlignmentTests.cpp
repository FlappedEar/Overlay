// Recording clock alignment (native/src/telemetry/RecordingAlignment.*,
// KAN-101): deterministic offsets and drift are measured; periodic,
// featureless or short signals stay ambiguous or insufficient; a declared
// logger clock that disagrees with the measurement is a conflict, never
// silently resolved.

#include "telemetry/RecordingAlignment.h"

#include <QtTest>
#include <cmath>
#include <functional>

using namespace FlappedEar;

namespace {

// A track day's speed on the absolute clock: a 110 s lap shape, slower
// changes across laps (traffic, tyres), a fixed wobble, and the events that
// make a session non-periodic: the pit-lane exit, a yellow-flag lap and the
// in-lap. Without them every lap would match every other lap.
double daySpeed(const double t)
{
    const double lap = 2.0 * M_PI * t / 110.0;
    const double racing = 90.0 + 35.0 * std::sin(lap) + 15.0 * std::sin(3.0 * lap + 0.4) + 10.0 * std::sin(0.013 * t)
        + 6.0 * std::sin(0.0071 * t + 1.0) + 2.0 * std::sin(1.7 * t) * std::sin(0.031 * t);
    if (t < 120.0) return std::min(racing, 60.0);          // pit-lane limit
    if (t > 800.0 && t < 910.0) return racing * 0.6;       // yellow flag
    if (t > 1650.0) return racing * 0.7;                   // in-lap
    return racing;
}

TelemetrySession recording(const double start, const double end, const double rateHz,
    const std::function<double(double)> &speedAt, const std::optional<qint64> startMilliseconds = std::nullopt)
{
    TelemetrySession session;
    TelemetryChannel speed;
    speed.name = "velocity";
    speed.unit = "km/h";
    for (double time = start; time <= end + 1e-9; time += 1.0 / rateHz) {
        speed.timestamps.append(time);
        speed.values.append(static_cast<float>(speedAt(time)));
    }
    session.channels.insert(speed.name, speed);
    session.aliases.insert("speed", speed.name);
    session.duration = end - start;
    session.sampleCount = speed.values.size();
    if (startMilliseconds) session.metadata.insert("firstTimestampMilliseconds", QString::number(*startMilliseconds));
    return session;
}

// The alternative logger's clock: primaryTime = candidateTime + offset + drift * candidateTime.
TelemetrySession candidateOf(const double offset, const double driftPpm, const double length = 1500.0,
    const std::optional<qint64> startMilliseconds = std::nullopt)
{
    return recording(0.0, length, 5.0, [=](double c) { return daySpeed(c + offset + driftPpm * 1e-6 * c); },
        startMilliseconds);
}

TelemetrySession primaryDay(const std::optional<qint64> startMilliseconds = std::nullopt)
{
    return recording(0.0, 1800.0, 5.0, daySpeed, startMilliseconds);
}

} // namespace

class RecordingAlignmentTests : public QObject {
    Q_OBJECT

private slots:
    void alignsAShiftedRecording();
    void measuresClockDrift();
    void keepsPeriodicAndFeaturelessSignalsAmbiguous();
    void reportsADeclaredClockConflict();
    void refusesInsufficientEvidence();
    void cancels();
};

void RecordingAlignmentTests::alignsAShiftedRecording()
{
    // Laps repeat, so the speed traces also match one lap away: measured
    // alone this is ambiguous, however well the offset is measured.
    const auto measuredOnly = alignRecordings(primaryDay(), candidateOf(123.4, 0.0));
    QCOMPARE(measuredOnly.status, QString(alignmentAmbiguous));
    QCOMPARE(measuredOnly.reason, QString("repeatedMatch"));
    QVERIFY2(std::abs(*measuredOnly.offset - 123.4) < 0.1, qPrintable(QString::number(*measuredOnly.offset)));
    QVERIFY(!measuredOnly.driftPpm); // identical clocks: no drift resolvable, none claimed
    QVERIFY(*measuredOnly.uncertaintySeconds <= 0.3);
    QVERIFY(measuredOnly.usedWindows >= 3);
    QVERIFY(measuredOnly.correlation > 0.95);
    QVERIFY(measuredOnly.overlapSeconds > 1400.0);
    QVERIFY(!measuredOnly.declaredOffset);

    // The loggers' clocks (RCZ always states one) choose the right lap.
    const qint64 start = 1'756'450'000'000;
    const auto declared = alignRecordings(primaryDay(start), candidateOf(123.4, 0.0, 1500.0, start + 123'900));
    QCOMPARE(declared.status, QString(alignmentAligned));
    QVERIFY(declared.resolvedByDeclaredClock);
    QCOMPARE(*declared.declaredOffset, 123.9);
    QVERIFY(std::abs(*declared.offset - 123.4) < 0.1); // measured, not the declared value

    // A session whose traces never repeat is decided by the match alone.
    const auto unique = [](double t) { return 80.0 + 30.0 * std::sin(0.002 * t * t / 10.0) + 10.0 * std::sin(0.05 * t); };
    const auto chirp = alignRecordings(recording(0.0, 900.0, 10.0, unique),
        recording(0.0, 700.0, 5.0, [&](double c) { return unique(c + 55.5); }));
    QCOMPARE(chirp.status, QString(alignmentAligned));
    QVERIFY(!chirp.resolvedByDeclaredClock);
    QVERIFY(std::abs(*chirp.offset - 55.5) < 0.1);
}

void RecordingAlignmentTests::measuresClockDrift()
{
    // 400 ppm: the alternative clock gains 0.6 s over 25 minutes.
    const qint64 start = 1'756'450'000'000;
    const auto result = alignRecordings(primaryDay(start), candidateOf(40.0, 400.0, 1500.0, start + 40'000));
    QCOMPARE(result.status, QString(alignmentAligned));
    QVERIFY2(std::abs(*result.driftPpm - 400.0) < 120.0, qPrintable(QString::number(*result.driftPpm)));
    QVERIFY(std::abs(*result.offset - 40.0) < 0.15);
    // Beyond what a logger clock plausibly does: reviewed, not approved.
    RecordingAlignmentOptions strict;
    strict.maximumPlausibleDriftPpm = 100.0;
    const auto implausible = alignRecordings(primaryDay(start), candidateOf(40.0, 400.0, 1500.0, start + 40'000), {}, strict);
    QCOMPARE(implausible.status, QString(alignmentAmbiguous));
    QCOMPARE(implausible.reason, QString("implausibleDrift"));
}

void RecordingAlignmentTests::keepsPeriodicAndFeaturelessSignalsAmbiguous()
{
    // Identical 20 s cycles: every cycle matches equally well.
    const auto periodic = [](double t) { return 80.0 + 30.0 * std::sin(2.0 * M_PI * t / 20.0); };
    const auto cyclic = alignRecordings(recording(0.0, 600.0, 10.0, periodic),
        recording(0.0, 500.0, 5.0, [&](double c) { return periodic(c + 37.0); }));
    QCOMPARE(cyclic.status, QString(alignmentAmbiguous));
    QCOMPARE(cyclic.reason, QString("repeatedMatch"));
    QVERIFY(cyclic.peakUniqueness < 0.1);
    // A constant speed carries no timing evidence at all.
    const auto flat = alignRecordings(recording(0.0, 600.0, 10.0, [](double) { return 100.0; }),
        recording(0.0, 500.0, 5.0, [](double) { return 100.0; }));
    QVERIFY(flat.status == QString(alignmentAmbiguous) || flat.status == QString(alignmentInsufficient));
    QVERIFY(flat.status != QString(alignmentAligned));
}

void RecordingAlignmentTests::reportsADeclaredClockConflict()
{
    const qint64 primaryStart = 1'756'450'000'000;
    // The loggers' clocks agree with the speed traces.
    const auto agreeing = alignRecordings(primaryDay(primaryStart), candidateOf(123.4, 0.0, 1500.0, primaryStart + 123'400));
    QCOMPARE(agreeing.status, QString(alignmentAligned));
    QCOMPARE(*agreeing.declaredOffset, 123.4);
    // The candidate logger's clock is a minute off: both are reported, the
    // alignment is not approved.
    const auto conflicting = alignRecordings(primaryDay(primaryStart), candidateOf(123.4, 0.0, 1500.0, primaryStart + 183'400));
    QCOMPARE(conflicting.status, QString(alignmentConflicting));
    QCOMPARE(conflicting.reason, QString("declaredClockDisagrees"));
    QCOMPARE(*conflicting.declaredOffset, 183.4);
    QVERIFY(std::abs(*conflicting.offset - 123.4) < 0.1);
}

void RecordingAlignmentTests::refusesInsufficientEvidence()
{
    TelemetrySession noSpeed;
    const auto missing = alignRecordings(primaryDay(), noSpeed);
    QCOMPARE(missing.status, QString(alignmentInsufficient));
    QCOMPARE(missing.reason, QString("noSpeed"));
    QVERIFY(!missing.offset);
    // Fifteen seconds of overlap is below the sync engine's minimum.
    const auto brief = alignRecordings(primaryDay(), candidateOf(200.0, 0.0, 15.0));
    QVERIFY(brief.status != QString(alignmentAligned));
    // Too few samples for the engine: insufficient with its reason.
    const auto tiny = alignRecordings(primaryDay(), recording(0.0, 1.0, 5.0, daySpeed));
    QCOMPARE(tiny.status, QString(alignmentInsufficient));
    QVERIFY(!tiny.reason.isEmpty());
}

void RecordingAlignmentTests::cancels()
{
    bool threw = false;
    try {
        static_cast<void>(alignRecordings(primaryDay(), candidateOf(10.0, 0.0), [] { return true; }));
    } catch (const OperationCancelled &) {
        threw = true;
    }
    QVERIFY(threw);
}

QTEST_GUILESS_MAIN(RecordingAlignmentTests)
#include "RecordingAlignmentTests.moc"
