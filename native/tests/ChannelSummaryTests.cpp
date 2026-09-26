// Interval summaries of recorded channels (native/src/telemetry/
// ChannelSummary.*, KAN-67/KAN-69): time-weighted mean, extrema, coverage,
// gaps never bridged, artifacts excluded and counted.

#include "telemetry/ChannelSummary.h"

#include <QtTest>
#include <cmath>
#include <functional>

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

TelemetrySession sessionWith(const TelemetryChannel &channel, const QString &alias = {})
{
    TelemetrySession session;
    session.channels.insert(channel.name, channel);
    if (!alias.isEmpty()) session.aliases.insert(alias, channel.name);
    return session;
}

} // namespace

class ChannelSummaryTests final : public QObject {
    Q_OBJECT
private slots:
    void computesTimeWeightedMeanAndExtrema();
    void neverBridgesAGap();
    void excludesPlaceholdersAndImplausibleValues();
    void reportsMissingSensorsAndListsTemperatures();
    void findsContinuouslyRecordedCoolingOnly();
};

void ChannelSummaryTests::computesTimeWeightedMeanAndExtrema()
{
    // Oil rises linearly from 100 to 120 °C over 10 s at 10 Hz: mean exactly 110.
    const auto session = sessionWith(channel("engine_oil_temp", "C", clock(0, 10, 0.1), [](double t) { return 100 + 2 * t; }));
    const auto summary = summarizeChannel(session, "engine_oil_temp", 0, 10, temperatureSummaryPolicy());
    QVERIFY(summary.valid);
    QCOMPARE(summary.unit, QString("C"));
    QVERIFY(std::abs(*summary.mean - 110.0) < 1e-3);
    QVERIFY(std::abs(*summary.minimum - 100.0) < 1e-4 && std::abs(*summary.maximum - 120.0) < 1e-4);
    QVERIFY(std::abs(*summary.maximumTime - 10.0) < 1e-6);
    QVERIFY(std::abs(summary.coverage - 1.0) < 1e-6);
    QCOMPARE(summary.sampleCount, 101);
    // An interval inside the recording.
    const auto part = summarizeChannel(session, "engine_oil_temp", 1.95, 4.05, temperatureSummaryPolicy()); // samples 2.0..4.0
    QVERIFY(std::abs(*part.mean - 106.0) < 1e-3);
}

void ChannelSummaryTests::neverBridgesAGap()
{
    // 60 °C for 0..4 s, a 4 s hole, 100 °C for 8..10 s. Bridging the hole
    // would pull the mean towards 80 and report full coverage.
    QVector<double> times = clock(0, 4, 0.1);
    times += clock(8, 10, 0.1);
    const auto session = sessionWith(channel("coolant_temp", "C", times, [](double t) { return t < 5 ? 60.0 : 100.0; }));
    const auto summary = summarizeChannel(session, "coolant_temp", 0, 10, temperatureSummaryPolicy());
    QVERIFY(summary.valid);
    QVERIFY(std::abs(summary.coveredSeconds - 6.0) < 1e-6);
    QVERIFY(std::abs(summary.coverage - 0.6) < 1e-6);
    QVERIFY(std::abs(*summary.mean - (60.0 * 4 + 100.0 * 2) / 6.0) < 1e-3);
}

void ChannelSummaryTests::excludesPlaceholdersAndImplausibleValues()
{
    // A 94 °C coolant channel with OBD placeholder zeros and one 900 °C glitch.
    const auto session = sessionWith(channel("coolant_temp", "", clock(0, 10, 0.1),
        [](double t) { return t < 0.35 ? 0.0 : std::abs(t - 5.0) < 1e-6 ? 900.0 : 94.0; }));
    const auto summary = summarizeChannel(session, "coolant_temp", 0, 10, temperatureSummaryPolicy());
    QVERIFY(summary.valid);
    QCOMPARE(summary.excludedArtifacts, 5); // four leading zeros and the glitch
    QVERIFY(std::abs(*summary.minimum - 94.0) < 1e-4 && std::abs(*summary.maximum - 94.0) < 1e-4);
    QVERIFY(summary.unit.isEmpty()); // undeclared, reported as such
    // A channel whose typical value is near zero keeps real zeros.
    const auto cold = sessionWith(channel("intake_temp", "C", clock(0, 10, 0.1), [](double t) { return t < 5 ? 0.0 : 2.0; }));
    QCOMPARE(summarizeChannel(cold, "intake_temp", 0, 10, temperatureSummaryPolicy()).excludedArtifacts, 0);
    // Heart rate outside 30..230 bpm is an artifact.
    const auto heart = sessionWith(channel("heart_rate", "bpm", clock(0, 10, 0.5), [](double t) { return t < 1 ? 255.0 : 140.0; }), "heartRate");
    const auto hr = summarizeChannel(heart, "heartRate", 0, 10, heartRateSummaryPolicy());
    QCOMPARE(hr.channel, QString("heart_rate"));
    QCOMPARE(hr.excludedArtifacts, 2);
    QVERIFY(std::abs(*hr.mean - 140.0) < 1e-3);
}

void ChannelSummaryTests::reportsMissingSensorsAndListsTemperatures()
{
    TelemetrySession session;
    for (const auto *name : {"coolant_temp-obd", "engine_oil_temp-obd", "velocity", "gearbox_temp-obd"})
        session.channels.insert(name, channel(name, "", clock(0, 1, 0.1), [](double) { return 90.0; }));
    QCOMPARE(recordedTemperatureChannels(session),
        (QStringList{"coolant_temp-obd", "engine_oil_temp-obd", "gearbox_temp-obd"}));
    const auto missing = summarizeChannel(session, "brake_temp", 0, 1, temperatureSummaryPolicy());
    QVERIFY(!missing.valid);
    QCOMPARE(missing.unavailableReason, QString(channelSummaryMissing));
    QVERIFY(!missing.mean);
    // No samples inside the interval.
    const auto outside = summarizeChannel(session, "coolant_temp-obd", 5, 6, temperatureSummaryPolicy());
    QVERIFY(!outside.valid);
    QCOMPARE(outside.unavailableReason, QString(channelSummaryNoSamples));
}

void ChannelSummaryTests::findsContinuouslyRecordedCoolingOnly()
{
    // Oil rises 80 -> 110 °C over 60 s, then cools to 90 °C over 120 s.
    const auto profile = [](double t) { return t <= 60 ? 80 + t / 2 : std::max(90.0, 110 - (t - 60) / 6); };
    const auto session = sessionWith(channel("engine_oil_temp", "C", clock(0, 200, 0.5), profile));
    auto intervals = findCoolingIntervals(session, "engine_oil_temp", temperatureSummaryPolicy());
    QCOMPARE(intervals.size(), 1);
    QVERIFY2(std::abs(intervals[0].drop() - 20.0) < 1.0, qPrintable(QString::number(intervals[0].drop())));
    QVERIFY(std::abs(intervals[0].seconds() - 120.0) < 6.0);
    QVERIFY(std::abs(intervals[0].startTime - 60.0) < 3.0);

    // The same cooling interrupted by a 60 s recording gap: nothing may span
    // the break, and no cooling is invented across it.
    QVector<double> times = clock(0, 100, 0.5);
    times += clock(160, 260, 0.5);
    const auto gapped = sessionWith(channel("engine_oil_temp", "C", times, [](double t) {
        return t <= 30 ? 80 + t : 110 - (t - 30) / 8; }));
    intervals = findCoolingIntervals(gapped, "engine_oil_temp", temperatureSummaryPolicy());
    QVERIFY(!intervals.isEmpty());
    for (const auto &interval : intervals)
        QVERIFY(interval.endTime <= 100.0 + 1e-6 || interval.startTime >= 160.0 - 1e-6);

    // Sensor noise of ±0.5 °C around a steady 95 °C is not cooling.
    const auto steady = sessionWith(channel("coolant_temp", "C", clock(0, 300, 0.5),
        [](double t) { return 95 + 0.5 * std::sin(t); }));
    QVERIFY(findCoolingIntervals(steady, "coolant_temp", temperatureSummaryPolicy()).isEmpty());
    QVERIFY(findCoolingIntervals(steady, "missing", temperatureSummaryPolicy()).isEmpty());
}

QTEST_GUILESS_MAIN(ChannelSummaryTests)
#include "ChannelSummaryTests.moc"
