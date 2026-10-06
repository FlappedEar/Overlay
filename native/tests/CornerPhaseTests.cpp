// Corner entry/apex/exit and per-lap minimum speed (native/src/telemetry/CornerPhases.*, KAN-46):
// geometric phases come from the shared axis; the minimum-speed location is
// measured per lap and must never be conflated with the apex.

#include "SyntheticLoopFixture.h"
#include "telemetry/CornerPhases.h"
#include "telemetry/CornerSpeeds.h"
#include "telemetry/TrackSegmentReview.h"
#include "telemetry/TrackSegmentProposals.h"

#include <QtTest>
#include <cmath>
#include <functional>
#include <limits>
#include <numbers>

using namespace FlappedEar;
using namespace SyntheticLoop;

namespace {

constexpr double earthRadiusMeters = 6'371'000.0;
double degreesForMeters(const double meters) { return meters / earthRadiusMeters * 180.0 / std::numbers::pi; }

// Tight 60-degree arc between two gentle ones: one short high-curvature region.
QVector<Step> singleApexHalf()
{
    return {straight(100), arc(60, 50), arc(60, 12), arc(60, 50), straight(100)};
}

// Two tight 45-degree arcs joined by a long gentle one: two separate regions.
QVector<Step> doubleApexHalf()
{
    return {straight(100), arc(45, 12), arc(90, 60), arc(45, 12), straight(100)};
}

struct Corner {
    ProgressAxis axis;
    TrackFeatures features;
    TrackSegmentProposal proposal;
};

Corner firstCorner(const QVector<Step> &half)
{
    Corner corner;
    corner.axis = buildLoopAxis(half);
    corner.features = computeTrackFeatures(corner.axis, 6.0);
    const auto proposals = proposeTrackSegments(corner.axis, corner.features);
    for (const auto &proposal : proposals.proposals) {
        if (proposal.type == TrackSegmentType::Corner) { corner.proposal = proposal; break; }
    }
    return corner;
}

struct Lap {
    TelemetrySession session;
    double endTime = 0.0;
};

// Drives one lap of the loop at 20 Hz, gate to gate, with speed v(s) m/s as
// a function of path distance s. Fixes whose s falls in `gpsGap` are dropped;
// speed samples whose s falls in `speedGap` are NaN (no data).
Lap driveLap(const QVector<Step> &half, const std::function<double(double)> &speed,
    const QPair<double, double> gpsGap = {-1.0, -1.0}, const QPair<double, double> speedGap = {-1.0, -1.0})
{
    auto points = loopPoints(half);
    points.append(points.first());
    QVector<double> cumulative{0.0};
    for (qsizetype i = 1; i < points.size(); ++i)
        cumulative.append(cumulative.last() + std::hypot(points[i].x() - points[i - 1].x(), points[i].y() - points[i - 1].y()));
    const double length = cumulative.last();

    Lap lap;
    auto &session = lap.session;
    session.aliases = {{"latitude", "lat"}, {"longitude", "lon"}, {"speed", "velocity"}};
    session.channels.insert("lat", {});
    session.channels.insert("lon", {});
    TelemetryChannel speedChannel;
    speedChannel.name = "velocity";
    speedChannel.unit = "km/h";
    session.channels.insert("velocity", speedChannel);
    auto &lat = session.channels["lat"];
    auto &lon = session.channels["lon"];
    auto &velocity = session.channels["velocity"];

    constexpr double dt = 0.05;
    double s = 0.0;
    double time = 0.0;
    qsizetype segment = 1;
    while (s < length) {
        while (segment + 1 < cumulative.size() && cumulative[segment] < s) ++segment;
        const double fraction = (s - cumulative[segment - 1]) / (cumulative[segment] - cumulative[segment - 1]);
        const QPointF position = points[segment - 1] + (points[segment] - points[segment - 1]) * fraction;
        if (!(s >= gpsGap.first && s <= gpsGap.second)) {
            lat.appendSample(time, static_cast<float>(degreesForMeters(position.y())));
            lon.appendSample(time, static_cast<float>(degreesForMeters(position.x())));
        }
        const bool speedMissing = s >= speedGap.first && s <= speedGap.second;
        velocity.appendSample(time, speedMissing ? std::numeric_limits<float>::quiet_NaN()
                                            : static_cast<float>(speed(s) * 3.6));
        s += speed(s) * dt;
        time += dt;
    }
    session.duration = time;
    lap.endTime = time - dt;
    return lap;
}

// Minimum 12 m/s at path distance 185 m, 30 m/s elsewhere.
double speedDipAt185(const double s) { return 30.0 - 18.0 * std::exp(-std::pow((s - 185.0) / 15.0, 2.0)); }

QString speedConfiguration() { return "compatibility-v1:" + QString(64, 'c'); }

ApprovedSegmentation approvedAs(const TrackSegmentProposal &proposal, const TrackSegmentType type = TrackSegmentType::Corner)
{
    const QJsonArray segments{makeTrackSegment(type, proposal.name, proposal.start.progressMeters,
        proposal.end.progressMeters, speedConfiguration())};
    return approvedSegmentation(segments, speedConfiguration());
}

QString onlyId(const ApprovedSegmentation &approved) { return approved.segments.first().toObject().value("id").toString(); }

} // namespace

class CornerPhaseTests final : public QObject {
    Q_OBJECT
private slots:
    void entryAndExitReuseCornerBoundaries();
    void placesASingleApexAtTheHighCurvatureRegion();
    void leavesMultipleApexesUnresolved();
    void widensABroadConstantRadiusApex();
    void locatesMinimumSpeedSeparatelyFromTheApex();
    void leavesMinimumSpeedUnresolvedOnIncompleteData();
    void rejectsInvalidInputs();
    void reportsEntryApexMinimumAndExitSpeedsSeparately();
    void limitsOrWithholdsCornerSpeedsOnPoorData();
    void cornerSpeedsHandleOtherSegmentShapes();
    void comparesCornerSpeedsBetweenTwoLaps();
};

void CornerPhaseTests::entryAndExitReuseCornerBoundaries()
{
    const auto corner = firstCorner(singleApexHalf());
    QVERIFY(corner.proposal.type == TrackSegmentType::Corner);
    const auto phases = proposeCornerGeometryPhases(corner.axis, corner.features, corner.proposal);
    QVERIFY(phases.valid);
    QCOMPARE(phases.entry.method, QString(cornerPhaseCurvatureOnset));
    QCOMPARE(phases.entry.progressMeters, corner.proposal.start.progressMeters);
    QCOMPARE(phases.entry.toleranceMeters, corner.proposal.start.toleranceMeters);
    QCOMPARE(phases.exit.method, QString(cornerPhaseCurvatureRelease));
    QCOMPARE(phases.exit.progressMeters, corner.proposal.end.progressMeters);
    QVERIFY(phases.entry.resolved());
    QVERIFY(phases.exit.resolved());
    QCOMPARE(phases.entry.evidence.value("source").toString(), QString(trackSegmentProposalAlgorithm));
}

void CornerPhaseTests::placesASingleApexAtTheHighCurvatureRegion()
{
    const auto corner = firstCorner(singleApexHalf());
    const auto phases = proposeCornerGeometryPhases(corner.axis, corner.features, corner.proposal);
    QVERIFY(phases.valid);
    QVERIFY(phases.apex.resolved());
    QCOMPARE(phases.apex.method, QString(cornerPhasePeakCurvature));
    QVERIFY(phases.apex.uncertaintyReasons.isEmpty());
    QCOMPARE(phases.apexCandidatesMeters.size(), 1);
    // The tight arc spans path distance ~152.4..165.0 m; its centre is ~158.7 m.
    QVERIFY2(std::abs(phases.apex.progressMeters - 158.7) <= phases.apex.toleranceMeters,
        qPrintable(QString("apex at %1 m").arg(phases.apex.progressMeters)));
    QVERIFY(phases.apex.evidence.value("peakCurvaturePerMeter").toDouble() > 1.0 / 20.0);
}

void CornerPhaseTests::leavesMultipleApexesUnresolved()
{
    const auto corner = firstCorner(doubleApexHalf());
    const auto phases = proposeCornerGeometryPhases(corner.axis, corner.features, corner.proposal);
    QVERIFY(phases.valid);
    QCOMPARE(phases.apex.unresolvedReason, QString(cornerPhaseMultipleApexes));
    QVERIFY(!phases.apex.resolved());
    QCOMPARE(phases.apexCandidatesMeters.size(), 2);
    // Tight arcs centred at path distance ~104.7 m and ~208.4 m.
    QVERIFY(std::abs(phases.apexCandidatesMeters[0] - 104.7) < 8.0);
    QVERIFY(std::abs(phases.apexCandidatesMeters[1] - 208.4) < 8.0);
    QCOMPARE(phases.apex.evidence.value("candidatesMeters").toArray().size(), 2);
    QVERIFY(phases.entry.resolved());
    QVERIFY(phases.exit.resolved());
}

void CornerPhaseTests::widensABroadConstantRadiusApex()
{
    const auto corner = firstCorner(stadium());
    const auto phases = proposeCornerGeometryPhases(corner.axis, corner.features, corner.proposal);
    QVERIFY(phases.valid);
    QVERIFY(phases.apex.resolved());
    QVERIFY(phases.apex.uncertaintyReasons.contains(cornerPhaseBroadPeak));
    // A 125.7 m semicircle from 100 m: its middle is ~162.8 m and the whole arc is one region.
    QVERIFY(std::abs(phases.apex.progressMeters - 162.8) < 6.0);
    QVERIFY(phases.apex.toleranceMeters > 40.0);
}

void CornerPhaseTests::locatesMinimumSpeedSeparatelyFromTheApex()
{
    const auto corner = firstCorner(singleApexHalf());
    const auto lap = driveLap(singleApexHalf(), speedDipAt185);
    const auto trace = projectLapTrace(corner.axis, lap.session, 0.0, lap.endTime);
    QVERIFY(!trace.isEmpty());
    const auto minimum = locateMinimumSpeed(corner.axis, corner.proposal, trace, lap.session, 1.0);
    QVERIFY2(minimum.resolved(), qPrintable(minimum.unresolvedReason));
    QCOMPARE(minimum.method, QString(cornerPhaseMinimumSpeed));
    QVERIFY2(std::abs(minimum.progressMeters - 185.0) <= minimum.toleranceMeters + 2.0,
        qPrintable(QString("minimum speed at %1 m").arg(minimum.progressMeters)));
    QVERIFY(minimum.toleranceMeters < 10.0);
    QVERIFY(minimum.uncertaintyReasons.isEmpty());
    QCOMPARE(minimum.evidence.value("channel").toString(), QString("velocity"));
    QCOMPARE(minimum.evidence.value("unit").toString(), QString("km/h"));
    QVERIFY(std::abs(minimum.evidence.value("minimumValue").toDouble() - 12.0 * 3.6) < 1.0);

    const auto phases = proposeCornerGeometryPhases(corner.axis, corner.features, corner.proposal);
    QVERIFY(phases.apex.resolved());
    QVERIFY2(std::abs(phases.apex.progressMeters - minimum.progressMeters) > 20.0,
        "the apex is geometry; the minimum-speed point is where this lap was slowest");
}

void CornerPhaseTests::leavesMinimumSpeedUnresolvedOnIncompleteData()
{
    const auto corner = firstCorner(singleApexHalf());

    const auto gpsGapLap = driveLap(singleApexHalf(), speedDipAt185, {170.0, 200.0});
    const auto gpsGap = locateMinimumSpeed(corner.axis, corner.proposal,
        projectLapTrace(corner.axis, gpsGapLap.session, 0.0, gpsGapLap.endTime), gpsGapLap.session, 1.0);
    QCOMPARE(gpsGap.unresolvedReason, QString(cornerPhaseIncompleteCoverage));
    QVERIFY(gpsGap.evidence.value("missingSamples").toInt() > 0);

    const auto speedGapLap = driveLap(singleApexHalf(), speedDipAt185, {-1.0, -1.0}, {175.0, 195.0});
    const auto speedGap = locateMinimumSpeed(corner.axis, corner.proposal,
        projectLapTrace(corner.axis, speedGapLap.session, 0.0, speedGapLap.endTime), speedGapLap.session, 1.0);
    QCOMPARE(speedGap.unresolvedReason, QString(cornerPhaseIncompleteCoverage));

    auto noSpeed = driveLap(singleApexHalf(), speedDipAt185);
    const auto trace = projectLapTrace(corner.axis, noSpeed.session, 0.0, noSpeed.endTime);
    noSpeed.session.aliases.remove("speed");
    QCOMPARE(locateMinimumSpeed(corner.axis, corner.proposal, trace, noSpeed.session, 1.0).unresolvedReason,
        QString(cornerPhaseSpeedChannelMissing));

    const auto flatLap = driveLap(singleApexHalf(), [](double) { return 25.0; });
    QCOMPARE(locateMinimumSpeed(corner.axis, corner.proposal,
                 projectLapTrace(corner.axis, flatLap.session, 0.0, flatLap.endTime), flatLap.session, 1.0)
                 .unresolvedReason,
        QString(cornerPhaseFlatSpeed));

    auto wrapping = corner.proposal;
    wrapping.start.progressMeters = corner.axis.lengthMeters - 30.0;
    wrapping.end.progressMeters = 30.0;
    const auto normal = driveLap(singleApexHalf(), speedDipAt185);
    QCOMPARE(locateMinimumSpeed(corner.axis, wrapping,
                 projectLapTrace(corner.axis, normal.session, 0.0, normal.endTime), normal.session, 1.0)
                 .unresolvedReason,
        QString(cornerPhaseCrossesGate));
}

void CornerPhaseTests::rejectsInvalidInputs()
{
    const auto corner = firstCorner(singleApexHalf());
    QVERIFY(proposeCornerGeometryPhases(corner.axis, corner.features, corner.proposal).valid);
    QVERIFY(!proposeCornerGeometryPhases(ProgressAxis{}, corner.features, corner.proposal).valid);
    QVERIFY(!proposeCornerGeometryPhases(corner.axis, TrackFeatures{}, corner.proposal).valid);

    auto straightProposal = corner.proposal;
    straightProposal.type = TrackSegmentType::Straight;
    QVERIFY(!proposeCornerGeometryPhases(corner.axis, corner.features, straightProposal).valid);
    auto outOfAxis = corner.proposal;
    outOfAxis.end.progressMeters = corner.axis.lengthMeters + 1.0;
    QVERIFY(!proposeCornerGeometryPhases(corner.axis, corner.features, outOfAxis).valid);

    CornerPhaseOptions options;
    options.apexSeparationRatio = options.apexRegionRatio;
    QVERIFY(!proposeCornerGeometryPhases(corner.axis, corner.features, corner.proposal, options).valid);
    options = {};
    options.flatSpeedFraction = std::numeric_limits<double>::quiet_NaN();
    QVERIFY(!proposeCornerGeometryPhases(corner.axis, corner.features, corner.proposal, options).valid);

    const auto lap = driveLap(singleApexHalf(), speedDipAt185);
    const auto trace = projectLapTrace(corner.axis, lap.session, 0.0, lap.endTime);
    QCOMPARE(locateMinimumSpeed(corner.axis, corner.proposal, trace, lap.session, 0.0).unresolvedReason,
        QString(cornerPhaseInvalidInput));
    QCOMPARE(locateMinimumSpeed(corner.axis, corner.proposal, trace, lap.session, 1e-6).unresolvedReason,
        QString(cornerPhaseInvalidInput));
    QCOMPARE(locateMinimumSpeed(corner.axis, straightProposal, trace, lap.session, 1.0).unresolvedReason,
        QString(cornerPhaseInvalidInput));
}

void CornerPhaseTests::reportsEntryApexMinimumAndExitSpeedsSeparately()
{
    // KAN-52: four separate values; the apex speed is read at the geometric apex,
    // the minimum is where this lap was slowest, ~28 m later.
    const auto corner = firstCorner(singleApexHalf());
    const auto approved = approvedAs(corner.proposal);
    const auto lap = driveLap(singleApexHalf(), speedDipAt185);
    const auto trace = projectLapTrace(corner.axis, lap.session, 0.0, lap.endTime);
    const auto speeds = computeCornerSpeeds(corner.axis, corner.features, approved, onlyId(approved), trace, lap.session);
    QVERIFY(speeds.valid);
    QCOMPARE(speeds.provenance, QString("measured"));
    QCOMPARE(speeds.channel, QString("velocity"));
    QCOMPARE(speeds.unit, QString("km/h"));
    QCOMPARE(speeds.stamp.calculationAlgorithm, QString(cornerSpeedsAlgorithm));
    QVERIFY(segmentationResultCurrent(speeds.stamp, approved, cornerSpeedsAlgorithm));
    for (const auto *value : {&speeds.entry, &speeds.apex, &speeds.minimum, &speeds.exit}) {
        QVERIFY2(value->value, qPrintable(value->unavailableReason));
        QVERIFY(value->telemetryTime);
        QVERIFY(value->limitations.isEmpty());
    }
    QCOMPARE(speeds.entry.progressMeters, corner.proposal.start.progressMeters);
    QCOMPARE(speeds.exit.progressMeters, corner.proposal.end.progressMeters);
    QVERIFY(*speeds.entry.value > 100.0);
    QVERIFY(*speeds.exit.value > 100.0);
    QVERIFY(std::abs(*speeds.minimum.value - 12.0 * 3.6) < 1.0);
    QVERIFY(std::abs(speeds.minimum.progressMeters - 185.0) < 5.0);
    QVERIFY(std::abs(speeds.apex.progressMeters - speeds.minimum.progressMeters) > 20.0);
    QVERIFY(*speeds.apex.value - *speeds.minimum.value > 50.0);
    QVERIFY(std::abs(speeds.coveredMeters - speeds.lengthMeters) < 1e-6);
    QVERIFY(speeds.meanSampleSpacingMeters > 0.0 && speeds.meanSampleSpacingMeters <= sparseSampleSpacingMeters);
}

void CornerPhaseTests::limitsOrWithholdsCornerSpeedsOnPoorData()
{
    const auto corner = firstCorner(singleApexHalf());
    const auto approved = approvedAs(corner.proposal);
    const auto id = onlyId(approved);

    // A GPS gap inside the corner: no minimum, but boundary speeds outside the gap remain.
    const auto gapped = driveLap(singleApexHalf(), speedDipAt185, {170.0, 200.0});
    const auto gapTrace = projectLapTrace(corner.axis, gapped.session, 0.0, gapped.endTime);
    const auto withGap = computeCornerSpeeds(corner.axis, corner.features, approved, id, gapTrace, gapped.session);
    QVERIFY(!withGap.minimum.value);
    QCOMPARE(withGap.minimum.unavailableReason, QString(cornerPhaseIncompleteCoverage));
    QVERIFY(withGap.entry.value && withGap.exit.value);
    QVERIFY(withGap.coveredMeters < withGap.lengthMeters - 20.0);

    // Sparse speed samples: values are kept but explicitly limited.
    auto sparse = driveLap(singleApexHalf(), speedDipAt185);
    auto &velocity = sparse.session.channels["velocity"];
    TelemetryChannel thinned;
    thinned.name = velocity.name;
    thinned.unit = velocity.unit;
    for (qsizetype i = 0; i < velocity.timestamps().size(); i += 40) {
        thinned.appendSample(velocity.timestamps()[i], velocity.values()[i]);
    }
    velocity = thinned;
    const auto sparseTrace = projectLapTrace(corner.axis, sparse.session, 0.0, sparse.endTime);
    const auto limited = computeCornerSpeeds(corner.axis, corner.features, approved, id, sparseTrace, sparse.session);
    QVERIFY(limited.meanSampleSpacingMeters > sparseSampleSpacingMeters);
    QVERIFY(limited.entry.value);
    QVERIFY(limited.entry.limitations.contains(cornerSpeedSparseSamples));
    QVERIFY(limited.exit.limitations.contains(cornerSpeedSparseSamples));

    // No speed channel: nothing is derived from GPS positions instead.
    auto noSpeed = driveLap(singleApexHalf(), speedDipAt185);
    const auto noSpeedTrace = projectLapTrace(corner.axis, noSpeed.session, 0.0, noSpeed.endTime);
    noSpeed.session.aliases.remove("speed");
    const auto missing = computeCornerSpeeds(corner.axis, corner.features, approved, id, noSpeedTrace, noSpeed.session);
    QVERIFY(missing.valid);
    QCOMPARE(missing.provenance, QString("unavailable"));
    for (const auto *value : {&missing.entry, &missing.apex, &missing.minimum, &missing.exit}) {
        QVERIFY(!value->value);
        QCOMPARE(value->unavailableReason, QString(cornerPhaseSpeedChannelMissing));
    }
}

void CornerPhaseTests::cornerSpeedsHandleOtherSegmentShapes()
{
    // Two separate apexes: no apex speed, the minimum is still measured.
    const auto doubleCorner = firstCorner(doubleApexHalf());
    const auto doubleApproved = approvedAs(doubleCorner.proposal);
    const auto doubleLap = driveLap(doubleApexHalf(), speedDipAt185);
    const auto doubleTrace = projectLapTrace(doubleCorner.axis, doubleLap.session, 0.0, doubleLap.endTime);
    const auto twoApexes = computeCornerSpeeds(doubleCorner.axis, doubleCorner.features, doubleApproved,
        onlyId(doubleApproved), doubleTrace, doubleLap.session);
    QVERIFY(!twoApexes.apex.value);
    QCOMPARE(twoApexes.apex.unavailableReason, QString(cornerPhaseMultipleApexes));
    QVERIFY(twoApexes.minimum.value);

    const auto corner = firstCorner(singleApexHalf());
    const auto lap = driveLap(singleApexHalf(), speedDipAt185);
    const auto trace = projectLapTrace(corner.axis, lap.session, 0.0, lap.endTime);

    // A straight segment has no apex, but its minimum speed is still measured.
    const auto straight = approvedAs(corner.proposal, TrackSegmentType::Straight);
    const auto straightSpeeds = computeCornerSpeeds(corner.axis, corner.features, straight, onlyId(straight), trace, lap.session);
    QCOMPARE(straightSpeeds.apex.unavailableReason, QString(cornerSpeedNotACorner));
    QVERIFY(straightSpeeds.minimum.value);

    // A segment across the gate cannot be measured within one lap.
    auto wrapping = corner.proposal;
    wrapping.start.progressMeters = corner.axis.lengthMeters - 30.0;
    wrapping.end.progressMeters = 30.0;
    const auto wrapped = approvedAs(wrapping);
    const auto acrossGate = computeCornerSpeeds(corner.axis, corner.features, wrapped, onlyId(wrapped), trace, lap.session);
    QVERIFY(acrossGate.valid);
    for (const auto *value : {&acrossGate.entry, &acrossGate.apex, &acrossGate.minimum, &acrossGate.exit})
        QCOMPARE(value->unavailableReason, QString(cornerPhaseCrossesGate));

    QVERIFY(!computeCornerSpeeds(corner.axis, corner.features, approvedAs(corner.proposal), "missing", trace, lap.session).valid);
    QVERIFY(!computeCornerSpeeds(ProgressAxis{}, corner.features, approvedAs(corner.proposal), "x", trace, lap.session).valid);
}

void CornerPhaseTests::comparesCornerSpeedsBetweenTwoLaps()
{
    // KAN-55: A minus B for the same approved segment/revision. Lap B is
    // driven 10% slower everywhere, so every phase delta is exactly
    // predictable (A - B = 0.1 * A) -- this is the "compare two whole laps'
    // worth of independently-computed CornerSpeeds", not a single shared
    // computation, mirroring how the app will call this once per slot.
    const auto corner = firstCorner(singleApexHalf());
    const auto approved = approvedAs(corner.proposal);
    const auto id = onlyId(approved);
    const auto lapA = driveLap(singleApexHalf(), speedDipAt185);
    const auto lapB = driveLap(singleApexHalf(), [](double s) { return speedDipAt185(s) * 0.9; });
    const auto traceA = projectLapTrace(corner.axis, lapA.session, 0.0, lapA.endTime);
    const auto traceB = projectLapTrace(corner.axis, lapB.session, 0.0, lapB.endTime);
    const auto speedsA = computeCornerSpeeds(corner.axis, corner.features, approved, id, traceA, lapA.session);
    const auto speedsB = computeCornerSpeeds(corner.axis, corner.features, approved, id, traceB, lapB.session);
    QVERIFY(speedsA.valid && speedsB.valid);

    const auto comparison = compareCornerSpeeds(speedsA, speedsB);
    QVERIFY(comparison.valid);
    QVERIFY(comparison.unavailableReason.isEmpty());
    for (const auto &delta : {comparison.entryDelta, comparison.apexDelta, comparison.minimumDelta, comparison.exitDelta})
        QVERIFY(delta.has_value());
    QVERIFY2(std::abs(*comparison.entryDelta - 0.1 * *speedsA.entry.value) < 0.5,
        qPrintable(QString("entryDelta=%1 expected~%2").arg(*comparison.entryDelta).arg(0.1 * *speedsA.entry.value)));
    QVERIFY(*comparison.apexDelta > 0.0); // A was faster everywhere
    QVERIFY(*comparison.minimumDelta > 0.0);
    QVERIFY(*comparison.exitDelta > 0.0);

    // A different segment (even if geometrically identical bounds) is never compared.
    auto otherSegments = QJsonArray{makeTrackSegment(TrackSegmentType::Corner, "Other",
        corner.proposal.start.progressMeters, corner.proposal.end.progressMeters, speedConfiguration())};
    const auto otherApproved = approvedSegmentation(otherSegments, speedConfiguration());
    const auto speedsOther = computeCornerSpeeds(corner.axis, corner.features, otherApproved, onlyId(otherApproved), traceB, lapB.session);
    QVERIFY(speedsOther.valid);
    const auto differentRevision = compareCornerSpeeds(speedsA, speedsOther);
    QVERIFY(!differentRevision.valid);
    QCOMPARE(differentRevision.unavailableReason, QString(cornerSpeedDifferentSegmentOrRevision));

    // An invalid input on either side withholds the whole comparison.
    QVERIFY(!compareCornerSpeeds(CornerSpeeds{}, speedsB).valid);
}

QTEST_GUILESS_MAIN(CornerPhaseTests)
#include "CornerPhaseTests.moc"
