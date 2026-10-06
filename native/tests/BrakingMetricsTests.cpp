// Braking point, distance and deceleration (native/src/telemetry/BrakingMetrics.*, KAN-53):
// an explicit spatial interval, measured vs inferred kept apart, and no
// distances or peaks from missing coverage.

#include "telemetry/BrakingMetrics.h"
#include "telemetry/TrackSegments.h"

#include <QtTest>
#include <cmath>
#include <functional>
#include <limits>

using namespace FlappedEar;

namespace {

constexpr double dt = 0.05;           // 20 Hz
constexpr int sampleCount = 1001;     // t = 0 .. 50 s
constexpr double lapLength = 1000.0;  // progress = 20 m/s * t
const double noData = std::numeric_limits<double>::quiet_NaN();

QString configuration() { return "compatibility-v1:" + QString(64, 'd'); }

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

// Brake: a short application at 5 s (outside the interval) and the real one from
// k = onset: 0 -> 80 % over 4 samples, held 2 s, released over 4 samples.
std::function<double(int)> brakeFrom(const int onset)
{
    return [onset](const int k) {
        if (k >= 100 && k < 110) return 60.0;
        if (k <= onset) return 0.0;
        if (k < onset + 4) return (k - onset) * 20.0;
        if (k <= onset + 40) return 80.0;
        if (k < onset + 44) return 80.0 - (k - onset - 40) * 20.0;
        return 0.0;
    };
}

// Longitudinal G: -0.9 g from k = 440 to 484 with one -1.1 g sample at k = 460.
double deceleration(const int k)
{
    if (k == 460) return -1.1;
    return k >= 440 && k <= 484 ? -0.9 : 0.0;
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

// The lap's projection: progress = 20 * t, split where time falls in the hole.
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

ApprovedSegmentation cornerAt(const double start, const double end)
{
    const QJsonArray segments{makeTrackSegment(TrackSegmentType::Corner, "T1", start, end, configuration())};
    return approvedSegmentation(segments, configuration());
}

QString idOf(const ApprovedSegmentation &approved) { return approved.segments.first().toObject().value("id").toString(); }

} // namespace

class BrakingMetricsTests final : public QObject {
    Q_OBJECT
private slots:
    void measuresBrakingPointDistanceAndDeceleration();
    void labelsDecelerationBasedBrakingAsInferred();
    void missingCoverageCreatesNoDistanceOrPeak();
    void reportsWhyNoBrakingPointExists();
    void comparesOnlyLikeWithLike();
    void stopsTheApproachAtThePreviousCorner();
};

void BrakingMetricsTests::measuresBrakingPointDistanceAndDeceleration()
{
    const auto approved = cornerAt(500.0, 600.0);
    const auto session = sessionWith({{"brake", makeChannel("brake_pos", "%", brakeFrom(440))},
        {"longitudinalAcceleration", makeChannel("longacc", "g", deceleration)}});
    const auto metrics = computeBrakingMetrics(lapLength, approved, idOf(approved), projectedLap(), session, std::nullopt, std::nullopt);
    QVERIFY(metrics.valid);
    QVERIFY2(metrics.unavailableReason.isEmpty(), qPrintable(metrics.unavailableReason));
    QCOMPARE(metrics.intervalStartMeters, 300.0);
    QCOMPARE(metrics.intervalEndMeters, 600.0);
    QCOMPARE(metrics.method, QString(brakingMethodMeasured));
    QCOMPARE(metrics.provenance, QString(brakingProvenanceMeasured));
    QCOMPARE(metrics.thresholdUnit, QString("%"));
    // 10 % is crossed half-way between 22.00 s (0 %) and 22.05 s (20 %): 22.025 s, 440.5 m.
    QVERIFY(std::abs(*metrics.brakingPointTime - 22.025) < 1e-3);
    QVERIFY(std::abs(*metrics.brakingPointMeters - 440.5) < 0.05);
    QVERIFY(std::abs(*metrics.distanceBeforeEntryMeters - 59.5) < 0.05);
    // Released below 5 % at 24.2 s (484 m).
    QVERIFY(std::abs(*metrics.brakingSeconds - 2.175) < 1e-3);
    QVERIFY(std::abs(*metrics.brakingDistanceMeters - 43.5) < 0.05);
    QVERIFY(metrics.limitations.isEmpty());
    QCOMPARE(metrics.decelerationChannel, QString("longacc"));
    QCOMPARE(metrics.decelerationUnit, QString("g"));
    QVERIFY(std::abs(*metrics.peakDeceleration - 1.1) < 1e-3);
    QVERIFY(std::abs(*metrics.meanDeceleration - 0.9045) < 0.001);
    QCOMPARE(metrics.stamp.calculationAlgorithm, QString(brakingMetricsAlgorithm));
    QVERIFY(segmentationResultCurrent(metrics.stamp, approved, brakingMetricsAlgorithm));
}

void BrakingMetricsTests::labelsDecelerationBasedBrakingAsInferred()
{
    const auto approved = cornerAt(500.0, 600.0);
    const auto session = sessionWith({{"longitudinalAcceleration", makeChannel("longacc", "g", deceleration)}});
    const auto metrics = computeBrakingMetrics(lapLength, approved, idOf(approved), projectedLap(), session, std::nullopt, std::nullopt);
    QVERIFY(metrics.brakingPointMeters);
    QCOMPARE(metrics.method, QString(brakingMethodInferred));
    QCOMPARE(metrics.provenance, QString(brakingProvenanceInferred));
    QCOMPARE(metrics.thresholdUnit, QString("g"));
    QVERIFY(*metrics.brakingPointMeters > 438.0 && *metrics.brakingPointMeters < 441.0);
    QVERIFY(std::abs(*metrics.peakDeceleration - 1.1) < 1e-3);

    // A measured brake without an acceleration channel has no deceleration values.
    const auto brakeOnly = sessionWith({{"brake", makeChannel("brake_pos", "%", brakeFrom(440))}});
    const auto noDeceleration = computeBrakingMetrics(lapLength, approved, idOf(approved), projectedLap(), brakeOnly, std::nullopt, std::nullopt);
    QVERIFY(noDeceleration.brakingPointMeters);
    QVERIFY(!noDeceleration.peakDeceleration && !noDeceleration.meanDeceleration);
    QCOMPARE(noDeceleration.decelerationUnavailableReason, QString(brakingDecelerationChannelMissing));
}

void BrakingMetricsTests::missingCoverageCreatesNoDistanceOrPeak()
{
    const auto approved = cornerAt(500.0, 600.0);
    const auto session = sessionWith({{"brake", makeChannel("brake_pos", "%", brakeFrom(440))},
        {"longitudinalAcceleration", makeChannel("longacc", "g", deceleration)}});

    // A projection hole inside the braking episode: the point stays, the distance does not.
    const auto holed = computeBrakingMetrics(lapLength, approved, idOf(approved), projectedLap(23.0, 23.5), session, std::nullopt, std::nullopt);
    QVERIFY(holed.brakingPointMeters);
    QVERIFY(holed.brakingSeconds);
    QVERIFY(!holed.brakingDistanceMeters);

    // A hole at the interval start: nothing is measured at all.
    const auto noStart = computeBrakingMetrics(lapLength, approved, idOf(approved), projectedLap(14.5, 15.5), session, std::nullopt, std::nullopt);
    QCOMPARE(noStart.unavailableReason, QString(brakingIncompleteCoverage));
    QVERIFY(!noStart.brakingPointMeters);

    // A missing acceleration sample inside the episode: no peak or mean.
    const auto gappedG = sessionWith({{"brake", makeChannel("brake_pos", "%", brakeFrom(440))},
        {"longitudinalAcceleration", makeChannel("longacc", "g", [](int k) { return k == 470 ? noData : deceleration(k); })}});
    const auto noPeak = computeBrakingMetrics(lapLength, approved, idOf(approved), projectedLap(), gappedG, std::nullopt, std::nullopt);
    QVERIFY(noPeak.brakingPointMeters);
    QVERIFY(!noPeak.peakDeceleration && !noPeak.meanDeceleration);
    QCOMPARE(noPeak.decelerationUnavailableReason, QString(brakingIncompleteCoverage));
}

void BrakingMetricsTests::reportsWhyNoBrakingPointExists()
{
    const auto approved = cornerAt(500.0, 600.0);
    const auto idle = sessionWith({{"brake", makeChannel("brake_pos", "%", [](int k) { return k >= 100 && k < 110 ? 60.0 : 0.0; })}});
    QCOMPARE(computeBrakingMetrics(lapLength, approved, idOf(approved), projectedLap(), idle, std::nullopt, std::nullopt).unavailableReason,
        QString(brakingNoneDetected)); // the 5 s application lies outside the interval

    QCOMPARE(computeBrakingMetrics(lapLength, approved, idOf(approved), projectedLap(), sessionWith({}), std::nullopt, std::nullopt).unavailableReason,
        QString(brakingNoChannel));

    const auto wrapping = cornerAt(900.0, 100.0);
    QCOMPARE(computeBrakingMetrics(lapLength, wrapping, idOf(wrapping), projectedLap(), idle, std::nullopt, std::nullopt).unavailableReason,
        QString(brakingSegmentCrossesGate));

    // An approach that would start before the gate is clipped and says so.
    const auto early = cornerAt(100.0, 200.0);
    const auto clipped = computeBrakingMetrics(lapLength, early, idOf(early), projectedLap(), idle, std::nullopt, std::nullopt);
    QCOMPARE(clipped.intervalStartMeters, 0.0);
    QVERIFY(clipped.limitations.contains(brakingApproachClipped));

    // A real lap's projection never lands on the gate exactly: without the lap's
    // timed start or end a gate-bounded interval has no coverage; with it, it has.
    const auto offGate = projectedLap(-1.0, 0.3);   // first projected sample at 6 m
    const auto shortEnd = projectedLap(49.7, 51.0); // last projected sample at 994 m
    QCOMPARE(computeBrakingMetrics(lapLength, early, idOf(early), offGate, idle, std::nullopt, std::nullopt).unavailableReason,
        QString(brakingIncompleteCoverage));
    const auto gateStart = computeBrakingMetrics(lapLength, early, idOf(early), offGate, idle, 0.0, 50.0);
    QVERIFY2(gateStart.unavailableReason.isEmpty(), qPrintable(gateStart.unavailableReason));
    QVERIFY(gateStart.brakingPointMeters); // the 5 s application at 100 m lies inside 0-200 m
    const auto last = cornerAt(900.0, 1000.0);
    QCOMPARE(computeBrakingMetrics(lapLength, last, idOf(last), shortEnd, idle, std::nullopt, std::nullopt).unavailableReason,
        QString(brakingIncompleteCoverage));
    QCOMPARE(computeBrakingMetrics(lapLength, last, idOf(last), shortEnd, idle, 0.0, 50.0).unavailableReason,
        QString(brakingNoneDetected));
    // The missing channel is reported ahead of coverage.
    QCOMPARE(computeBrakingMetrics(lapLength, early, idOf(early), offGate, sessionWith({}), std::nullopt, std::nullopt)
                 .unavailableReason,
        QString(brakingNoChannel));

    QVERIFY(!computeBrakingMetrics(lapLength, approved, "missing", projectedLap(), idle, std::nullopt, std::nullopt).valid);
    BrakingMetricsOptions negative;
    negative.approachMeters = -1.0;
    QVERIFY(!computeBrakingMetrics(lapLength, approved, idOf(approved), projectedLap(), idle, std::nullopt, std::nullopt, negative).valid);
}

void BrakingMetricsTests::comparesOnlyLikeWithLike()
{
    const auto approved = cornerAt(500.0, 600.0);
    const auto lap = projectedLap();
    const auto measuredA = computeBrakingMetrics(lapLength, approved, idOf(approved), lap,
        sessionWith({{"brake", makeChannel("brake_pos", "%", brakeFrom(440))},
            {"longitudinalAcceleration", makeChannel("longacc", "g", deceleration)}}), std::nullopt, std::nullopt);
    const auto measuredB = computeBrakingMetrics(lapLength, approved, idOf(approved), lap,
        sessionWith({{"brake", makeChannel("brake_pos", "%", brakeFrom(460))},
            {"longitudinalAcceleration", makeChannel("longacc", "g", deceleration)}}), std::nullopt, std::nullopt);
    const auto comparison = compareBrakingMetrics(measuredA, measuredB);
    QVERIFY(comparison.valid);
    QVERIFY(comparison.unavailableReason.isEmpty());
    // B brakes 1 s (20 m) later, so A minus B is -20 m: A brakes earlier.
    QVERIFY(std::abs(*comparison.brakingPointDeltaMeters + 20.0) < 0.05);
    QVERIFY(comparison.brakingSecondsDelta);

    const auto inferredB = computeBrakingMetrics(lapLength, approved, idOf(approved), lap,
        sessionWith({{"longitudinalAcceleration", makeChannel("longacc", "g", deceleration)}}), std::nullopt, std::nullopt);
    const auto mixed = compareBrakingMetrics(measuredA, inferredB);
    QVERIFY(mixed.valid);
    QCOMPARE(mixed.unavailableReason, QString(brakingMixedProvenance));
    QVERIFY(!mixed.brakingPointDeltaMeters);

    const auto otherRevision = cornerAt(500.0, 610.0);
    const auto other = computeBrakingMetrics(lapLength, otherRevision, idOf(otherRevision), lap,
        sessionWith({{"brake", makeChannel("brake_pos", "%", brakeFrom(440))}}), std::nullopt, std::nullopt);
    QVERIFY(!compareBrakingMetrics(measuredA, other).valid);
}

void BrakingMetricsTests::stopsTheApproachAtThePreviousCorner()
{
    // KAN-63: a corner ending at 450 m precedes T2 (500-600 m). Braking that
    // starts at 440 m is that corner's; the 200 m approach stops at 450 m.
    const QJsonArray segments{makeTrackSegment(TrackSegmentType::Corner, "T1", 300.0, 450.0, configuration()),
        makeTrackSegment(TrackSegmentType::Corner, "T2", 500.0, 600.0, configuration())};
    const auto approved = approvedSegmentation(segments, configuration());
    const auto id = segments[1].toObject().value("id").toString();
    const auto earlier = sessionWith({{"brake", makeChannel("brake_pos", "%", brakeFrom(440))}});
    auto metrics = computeBrakingMetrics(lapLength, approved, id, projectedLap(), earlier, std::nullopt, std::nullopt);
    QVERIFY(metrics.valid);
    QCOMPARE(metrics.intervalStartMeters, 450.0);
    QVERIFY(metrics.limitations.contains(brakingApproachClippedAtCorner));
    QVERIFY(!metrics.brakingPointMeters || *metrics.brakingPointMeters >= 450.0);
    // Braking that starts after the previous corner is still found.
    const auto later = sessionWith({{"brake", makeChannel("brake_pos", "%", brakeFrom(470))}});
    metrics = computeBrakingMetrics(lapLength, approved, id, projectedLap(), later, std::nullopt, std::nullopt);
    QVERIFY(metrics.brakingPointMeters);
    QVERIFY(std::abs(*metrics.brakingPointMeters - 470.5) < 0.05);
    // A straight in between does not clip the approach.
    const QJsonArray withStraight{makeTrackSegment(TrackSegmentType::Straight, "S", 300.0, 500.0, configuration()),
        makeTrackSegment(TrackSegmentType::Corner, "T2", 500.0, 600.0, configuration())};
    const auto straightApproved = approvedSegmentation(withStraight, configuration());
    metrics = computeBrakingMetrics(lapLength, straightApproved, withStraight[1].toObject().value("id").toString(),
        projectedLap(), earlier, std::nullopt, std::nullopt);
    QCOMPARE(metrics.intervalStartMeters, 300.0);
    QVERIFY(!metrics.limitations.contains(brakingApproachClippedAtCorner));
}

QTEST_GUILESS_MAIN(BrakingMetricsTests)
#include "BrakingMetricsTests.moc"
