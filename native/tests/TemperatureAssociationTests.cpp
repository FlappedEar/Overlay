// Temperature associations (native/src/telemetry/TemperatureAssociation.*,
// KAN-100): Spearman rank correlation with ties, minimum population, no
// spread, non-finite pairs, and the time-of-day confound.

#include "telemetry/TemperatureAssociation.h"

#include <QtTest>
#include <cmath>
#include <limits>

using namespace FlappedEar;

class TemperatureAssociationTests : public QObject {
    Q_OBJECT

private slots:
    void ranksMonotonicAssociations();
    void averagesTiedRanks();
    void requiresAPopulationAndSpread();
    void skipsNonFinitePairs();
    void flagsATimeOfDayConfound();
    void measuresALapsStrongAcceleration();
};

void TemperatureAssociationTests::ranksMonotonicAssociations()
{
    const QVector<double> temperature{90, 91, 92, 93, 94, 95, 96, 97, 98, 99};
    // Monotonic but not linear: still +1.
    QVector<double> rising;
    for (const double value : temperature) rising.append(std::exp(value / 10.0));
    QCOMPARE(*spearmanCorrelation(temperature, rising).coefficient, 1.0);
    QVector<double> falling(rising.crbegin(), rising.crend());
    QCOMPARE(*spearmanCorrelation(temperature, falling).coefficient, -1.0);
    // A known value: one swapped pair among 10 gives 1 - 6*2/(10*99).
    QVector<double> swapped{1, 2, 3, 4, 5, 6, 7, 8, 10, 9};
    QVERIFY(std::abs(*spearmanCorrelation(temperature, swapped).coefficient - (1.0 - 12.0 / 990.0)) < 1e-12);
    QCOMPARE(associationStrength(0.29), QString("weak"));
    QCOMPARE(associationStrength(-0.45), QString("moderate"));
    QCOMPARE(associationStrength(0.6), QString("strong"));
    QCOMPARE(associationStrength(-0.95), QString("strong"));
}

void TemperatureAssociationTests::averagesTiedRanks()
{
    // Quantized sensor values tie; ties share their average rank.
    const QVector<double> temperature{90, 90, 91, 91, 92, 92, 93, 93};
    const QVector<double> lapTime{110, 110, 111, 111, 112, 112, 113, 113};
    QCOMPARE(*spearmanCorrelation(temperature, lapTime).coefficient, 1.0);
    const QVector<double> mixed{110, 111, 110, 111, 112, 113, 112, 113};
    const auto result = spearmanCorrelation(temperature, mixed);
    QVERIFY(result.coefficient);
    QVERIFY(*result.coefficient > 0.5 && *result.coefficient < 1.0);
}

void TemperatureAssociationTests::requiresAPopulationAndSpread()
{
    const QVector<double> seven{1, 2, 3, 4, 5, 6, 7};
    const auto tooFew = spearmanCorrelation(seven, seven);
    QCOMPARE(tooFew.count, 7);
    QVERIFY(!tooFew.coefficient);
    QCOMPARE(tooFew.unavailableReason, QString(associationTooFewSamples));
    QVERIFY(spearmanCorrelation(seven, seven, 3).coefficient);
    // A temperature that never changes cannot be associated with anything.
    const QVector<double> flat(10, 95.0);
    QVector<double> times{1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    const auto noSpread = spearmanCorrelation(flat, times);
    QVERIFY(!noSpread.coefficient);
    QCOMPARE(noSpread.unavailableReason, QString(associationNoSpread));
    QVERIFY(!spearmanCorrelation({}, {}).coefficient);
    QVERIFY(!spearmanCorrelation({1.0}, {1.0}, 0).coefficient);
}

void TemperatureAssociationTests::skipsNonFinitePairs()
{
    const double nan = std::numeric_limits<double>::quiet_NaN(), inf = std::numeric_limits<double>::infinity();
    const QVector<double> temperature{90, 91, nan, 92, 93, 94, 95, 96, 97, inf, 98};
    const QVector<double> lapTime{100, 101, 102, 103, nan, 104, 105, 106, 107, 108, 109};
    const auto result = spearmanCorrelation(temperature, lapTime);
    QCOMPARE(result.count, 8);
    QCOMPARE(*result.coefficient, 1.0);
    // Mismatched lengths use the common prefix.
    QCOMPARE(spearmanCorrelation({1, 2, 3}, {1, 2}, 2).count, 2);
}

void TemperatureAssociationTests::flagsATimeOfDayConfound()
{
    // Oil warms through the day while the driver gets faster: a strong
    // association that cannot be told apart from the day's progression.
    QVector<AssociationObservation> warming;
    for (int lap = 0; lap < 12; ++lap)
        warming.append({100.0 + lap, 120.0 - lap * 0.8, lap * 150.0});
    const auto confounded = associateTemperature(warming);
    QCOMPARE(*confounded.withValue.coefficient, -1.0);
    QCOMPARE(*confounded.withOrder.coefficient, 1.0);
    QVERIFY(confounded.confoundedByOrder);

    // Temperature that varies independently of the order is not flagged.
    const QVector<double> temperatures{104, 99, 107, 101, 103, 98, 106, 100, 105, 102, 97, 108};
    QVector<AssociationObservation> independent;
    for (int lap = 0; lap < temperatures.size(); ++lap)
        independent.append({temperatures[lap], 110.0 + (temperatures[lap] - 100.0) * 0.2, lap * 150.0});
    const auto result = associateTemperature(independent);
    QCOMPARE(*result.withValue.coefficient, 1.0);
    QVERIFY(std::abs(*result.withOrder.coefficient) < associationOrderConfoundLevel);
    QVERIFY(!result.confoundedByOrder);

    // Too few laps: nothing is claimed either way.
    const auto few = associateTemperature(warming.mid(0, 5));
    QVERIFY(!few.withValue.coefficient && !few.confoundedByOrder);
}

void TemperatureAssociationTests::measuresALapsStrongAcceleration()
{
    // 100 samples over 10 s: 0.01 .. 0.50 g on the way out of corners,
    // braking in between, and one implausible 9 g spike that is ignored.
    TelemetrySession session;
    TelemetryChannel channel;
    channel.name = "longacc-calc";
    for (int k = 0; k < 100; ++k) {
        channel.appendSample(k * 0.1, k == 50 ? 9.0f : k % 2 ? static_cast<float>(0.01 * (k / 2 + 1)) : -0.6f);
    }
    session.channels.insert(channel.name, channel);
    session.aliases.insert("longitudinalAcceleration", channel.name);
    const auto lap = lapStrongAcceleration(session, 0.0, 10.0);
    QCOMPARE(lap.channel, QString("longacc-calc"));
    QCOMPARE(lap.sampleCount, 50);
    QVERIFY(std::abs(*lap.strongG - 0.45) < 1e-6); // the 45th of 50 values
    // A shorter window with too few positive samples has no value.
    QVERIFY(!lapStrongAcceleration(session, 0.0, 3.0).strongG);
    QVERIFY(!lapStrongAcceleration(session, 5.0, 5.0).strongG);
    // Units other than g are not rescaled.
    session.channels["longacc-calc"].unit = "m/s^2";
    QVERIFY(!lapStrongAcceleration(session, 0.0, 10.0).strongG);
    session.aliases.remove("longitudinalAcceleration");
    QVERIFY(lapStrongAcceleration(session, 0.0, 10.0).channel.isEmpty());
}

QTEST_GUILESS_MAIN(TemperatureAssociationTests)
#include "TemperatureAssociationTests.moc"
