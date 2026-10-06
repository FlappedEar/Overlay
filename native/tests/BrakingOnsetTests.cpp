// Braking-onset candidates with explicit provenance (native/src/telemetry/BrakingOnset.*, KAN-47):
// measured brake with explicit hysteresis thresholds, otherwise deceleration
// labelled inferred; spikes, gaps and units are handled, never manufactured.

#include "telemetry/BrakingOnset.h"

#include <QtTest>
#include <cmath>
#include <functional>
#include <limits>

using namespace FlappedEar;

namespace {

constexpr double dt = 0.05; // 20 Hz
constexpr int sampleCount = 201; // t = 0 .. 10 s
const double noData = std::numeric_limits<double>::quiet_NaN();

// Sample k at t = k * dt. `value` may return NaN (no data); `present` false omits the sample.
TelemetryChannel makeChannel(const QString &name, const QString &unit, const std::function<double(int)> &value,
    const std::function<bool(int)> &present = [](int) { return true; })
{
    TelemetryChannel channel;
    channel.name = name;
    channel.unit = unit;
    for (int k = 0; k < sampleCount; ++k) {
        if (!present(k)) continue;
        channel.appendSample(k * dt, static_cast<float>(value(k)));
    }
    return channel;
}

// 0 until k=100 (5.0 s), ramps to 80 over 4 samples, holds, releases over 4 samples from k=140 (7.0 s).
double brakeProfile(const int k)
{
    if (k <= 100) return 0.0;
    if (k < 104) return (k - 100) * 20.0;
    if (k <= 140) return 80.0;
    if (k < 144) return 80.0 - (k - 140) * 20.0;
    return 0.0;
}

// 0 until k=120 (6.0 s), ramps to -0.8 g over 4 samples, holds until k=140.
double decelerationProfile(const int k)
{
    if (k <= 120) return 0.0;
    if (k < 124) return -(k - 120) * 0.2;
    if (k <= 140) return -0.8;
    return 0.0;
}

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

} // namespace

class BrakingOnsetTests final : public QObject {
    Q_OBJECT
private slots:
    void measuredBrakeUsesExplicitThresholdCrossing();
    void rejectsBriefSpikesAndHoldsThroughChatter();
    void neverBridgesGaps();
    void flagsWindowEdges();
    void infersFromDecelerationOnlyWithoutABrakeChannel();
    void neverSubstitutesDecelerationForMissingBrakeData();
    void enforcesThresholdUnits();
    void mapsOnsetsToProgress();
    void rejectsInvalidInputs();
};

void BrakingOnsetTests::measuredBrakeUsesExplicitThresholdCrossing()
{
    // A deceleration event at 2 s must not appear: a brake channel exists.
    const auto session = sessionWith({{"brake", makeChannel("brake_pos", "%", brakeProfile)},
        {"longitudinalAcceleration", makeChannel("longacc", "g", [](int k) { return k >= 40 && k < 60 ? -0.9 : 0.0; })}});
    const auto result = detectBrakingOnsets(session, 0.0, 10.0);
    QVERIFY(result.valid);
    QVERIFY(result.unresolvedReason.isEmpty());
    QCOMPARE(result.method, QString(brakingMethodMeasured));
    QCOMPARE(result.provenance, QString(brakingProvenanceMeasured));
    QCOMPARE(result.channel, QString("brake_pos"));
    QCOMPARE(result.channelUnit, QString("%"));
    QCOMPARE(result.threshold.on, 10.0);
    QCOMPARE(result.threshold.off, 5.0);
    QCOMPARE(result.threshold.unit, QString("%"));
    QCOMPARE(result.candidates.size(), 1);
    const auto &onset = result.candidates.first();
    // 0 at 5.00 s, 20 at 5.05 s: the 10% crossing is interpolated at 5.025 s.
    QVERIFY2(std::abs(onset.telemetryTime - 5.025) < 1e-3, qPrintable(QString::number(onset.telemetryTime)));
    QVERIFY(std::abs(onset.toleranceSeconds - dt) < 1e-6);
    QVERIFY(std::abs(onset.peakValue - 80.0) < 1e-3);
    QVERIFY(onset.durationSeconds > 2.0 && onset.durationSeconds < 2.3);
    QVERIFY(onset.uncertaintyReasons.isEmpty());
    QVERIFY(!onset.progressMeters.has_value());
    QCOMPARE(result.rejectedSpikes, 0);
    QCOMPARE(result.gaps, 0);
}

void BrakingOnsetTests::rejectsBriefSpikesAndHoldsThroughChatter()
{
    const auto spiky = sessionWith({{"brake", makeChannel("brake_pos", "%",
        [](int k) { return k == 60 ? 50.0 : brakeProfile(k); })}});
    const auto spikes = detectBrakingOnsets(spiky, 0.0, 10.0);
    QCOMPARE(spikes.rejectedSpikes, 1);
    QCOMPARE(spikes.candidates.size(), 1);
    QVERIFY(std::abs(spikes.candidates.first().telemetryTime - 5.025) < 1e-3);

    // 12/8 alternating around the 10% on-threshold stays above the 5% off-threshold: one episode.
    const auto chatter = sessionWith({{"brake", makeChannel("brake_pos", "%",
        [](int k) { return k > 100 && k <= 120 ? (k % 2 == 0 ? 12.0 : 8.0) : 0.0; })}});
    const auto held = detectBrakingOnsets(chatter, 0.0, 10.0);
    QCOMPARE(held.candidates.size(), 1);
    QCOMPARE(held.rejectedSpikes, 0);
    QVERIFY(held.candidates.first().durationSeconds > 0.9);
}

void BrakingOnsetTests::neverBridgesGaps()
{
    // Samples 99..109 (4.95..5.45 s) are missing while the brake is applied.
    const auto missing = sessionWith({{"brake", makeChannel("brake_pos", "%", brakeProfile,
        [](int k) { return k < 99 || k > 109; })}});
    const auto afterGap = detectBrakingOnsets(missing, 0.0, 10.0);
    QCOMPARE(afterGap.gaps, 1);
    QCOMPARE(afterGap.candidates.size(), 1);
    QVERIFY(std::abs(afterGap.candidates.first().telemetryTime - 5.5) < 1e-6);
    QVERIFY(afterGap.candidates.first().uncertaintyReasons.contains(brakingFollowsGap));

    // Non-finite samples 120..125 split one application into two flagged candidates.
    const auto interrupted = sessionWith({{"brake", makeChannel("brake_pos", "%",
        [](int k) { return k >= 120 && k <= 125 ? noData : brakeProfile(k); })}});
    const auto split = detectBrakingOnsets(interrupted, 0.0, 10.0);
    QCOMPARE(split.gaps, 1);
    QCOMPARE(split.candidates.size(), 2);
    QVERIFY(split.candidates[0].uncertaintyReasons.contains(brakingInterruptedByGap));
    QVERIFY(std::abs(split.candidates[0].durationSeconds - (5.95 - 5.025)) < 1e-3);
    QVERIFY(split.candidates[1].uncertaintyReasons.contains(brakingFollowsGap));
    QVERIFY(std::abs(split.candidates[1].telemetryTime - 6.3) < 1e-6);
}

void BrakingOnsetTests::flagsWindowEdges()
{
    const auto session = sessionWith({{"brake", makeChannel("brake_pos", "%", brakeProfile)}});
    const auto started = detectBrakingOnsets(session, 5.5, 10.0);
    QCOMPARE(started.candidates.size(), 1);
    QVERIFY(started.candidates.first().uncertaintyReasons.contains(brakingAlreadyActive));

    const auto truncated = detectBrakingOnsets(session, 0.0, 6.0);
    QCOMPARE(truncated.candidates.size(), 1);
    QVERIFY(truncated.candidates.first().uncertaintyReasons.contains(brakingTruncatedAtWindowEnd));
}

void BrakingOnsetTests::infersFromDecelerationOnlyWithoutABrakeChannel()
{
    const auto session = sessionWith({{"longitudinalAcceleration", makeChannel("longacc", "g", decelerationProfile)}});
    const auto result = detectBrakingOnsets(session, 0.0, 10.0);
    QVERIFY(result.valid);
    QCOMPARE(result.method, QString(brakingMethodInferred));
    QCOMPARE(result.provenance, QString(brakingProvenanceInferred));
    QCOMPARE(result.channel, QString("longacc"));
    QCOMPARE(result.threshold.unit, QString("g"));
    QCOMPARE(result.candidates.size(), 1);
    // -0.2 g at 6.05 s, -0.4 g at 6.10 s: the 0.3 g crossing is at 6.075 s.
    QVERIFY(std::abs(result.candidates.first().telemetryTime - 6.075) < 1e-3);
    QVERIFY(std::abs(result.candidates.first().peakValue + 0.8) < 1e-3); // original sign: braking is negative

    BrakingOnsetOptions noInference;
    noInference.allowInferred = false;
    const auto disabled = detectBrakingOnsets(session, 0.0, 10.0, noInference);
    QCOMPARE(disabled.unresolvedReason, QString(brakingInferenceDisabled));
    QVERIFY(disabled.candidates.isEmpty());

    const auto empty = detectBrakingOnsets(sessionWith({}), 0.0, 10.0);
    QVERIFY(empty.valid);
    QCOMPARE(empty.unresolvedReason, QString(brakingNoChannel));
}

void BrakingOnsetTests::neverSubstitutesDecelerationForMissingBrakeData()
{
    const auto session = sessionWith({{"brake", makeChannel("brake_pos", "%", [](int) { return noData; })},
        {"longitudinalAcceleration", makeChannel("longacc", "g", decelerationProfile)}});
    const auto result = detectBrakingOnsets(session, 0.0, 10.0);
    QCOMPARE(result.method, QString(brakingMethodMeasured));
    QCOMPARE(result.unresolvedReason, QString(brakingNoSamples));
    QVERIFY(result.candidates.isEmpty());
}

void BrakingOnsetTests::enforcesThresholdUnits()
{
    const auto bar = sessionWith({{"brake", makeChannel("brake_pressure", "bar", [](int k) { return brakeProfile(k) / 2.0; })}});
    QCOMPARE(detectBrakingOnsets(bar, 0.0, 10.0).unresolvedReason, QString(brakingUnitMismatch));
    BrakingOnsetOptions barOptions;
    barOptions.measuredBrake = {5.0, 2.0, "bar"};
    const auto inBar = detectBrakingOnsets(bar, 0.0, 10.0, barOptions);
    QVERIFY(inBar.unresolvedReason.isEmpty());
    QCOMPARE(inBar.candidates.size(), 1);
    QVERIFY(std::abs(inBar.candidates.first().telemetryTime - 5.025) < 1e-3);

    const auto undeclared = sessionWith({{"brake", makeChannel("brake", "", brakeProfile)}});
    const auto assumed = detectBrakingOnsets(undeclared, 0.0, 10.0);
    QCOMPARE(assumed.candidates.size(), 1);
    QVERIFY(assumed.channelUnit.isEmpty());
    QVERIFY(assumed.candidates.first().uncertaintyReasons.contains(brakingUnitUndeclared));
}

void BrakingOnsetTests::mapsOnsetsToProgress()
{
    const auto session = sessionWith({{"brake", makeChannel("brake_pos", "%", brakeProfile)}});
    ProgressSegment segment;
    segment.samples = {{0.0, 0.0, true}, {10.0, 500.0, true}};
    const QVector<ProgressSegment> lap{segment};
    const auto mapped = detectBrakingOnsets(session, 0.0, 10.0, {}, &lap);
    QCOMPARE(mapped.candidates.size(), 1);
    QVERIFY(mapped.candidates.first().progressMeters.has_value());
    QVERIFY(std::abs(*mapped.candidates.first().progressMeters - 5.025 * 50.0) < 0.1);

    ProgressSegment early;
    early.samples = {{0.0, 0.0, true}, {4.0, 200.0, true}};
    const QVector<ProgressSegment> partial{early};
    const auto unmapped = detectBrakingOnsets(session, 0.0, 10.0, {}, &partial);
    QVERIFY(!unmapped.candidates.first().progressMeters.has_value());
}

void BrakingOnsetTests::rejectsInvalidInputs()
{
    const auto session = sessionWith({{"brake", makeChannel("brake_pos", "%", brakeProfile)}});
    QVERIFY(!detectBrakingOnsets(session, 5.0, 5.0).valid);
    QVERIFY(!detectBrakingOnsets(session, noData, 5.0).valid);
    BrakingOnsetOptions options;
    options.measuredBrake = {5.0, 5.0, "%"};
    QVERIFY(!detectBrakingOnsets(session, 0.0, 10.0, options).valid);
    options = {};
    options.inferredDeceleration.unit = " ";
    QVERIFY(!detectBrakingOnsets(session, 0.0, 10.0, options).valid);
    options = {};
    options.minimumDurationSeconds = 0.0;
    QVERIFY(!detectBrakingOnsets(session, 0.0, 10.0, options).valid);
}

QTEST_GUILESS_MAIN(BrakingOnsetTests)
#include "BrakingOnsetTests.moc"
