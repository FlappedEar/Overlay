// The shared cross-lap track-progress alignment axis
// (native/src/telemetry/TrackProgress.*, KAN-31/32/33/34): projecting two
// independently recorded laps onto one common, gate-anchored, crossing-safe
// arc-length axis so they can be compared corner-for-corner even on
// different racing lines.

#include "EventProjectFixture.h"
#include "telemetry/LapTiming.h"
#include "telemetry/SectorTiming.h"
#include "telemetry/TrackProgress.h"
#include "telemetry/TrackSegments.h"
#include "telemetry/VboParser.h"

#include <QtTest>
#include <cmath>
#include <limits>
#include <numbers>

using namespace FlappedEar;

namespace {

constexpr double earthRadiusMeters = 6'371'000.0;
// At the equator (origin latitude 0), east and north both convert the same
// way, so a single helper suffices for both axes of a hand-built local shape.
double degreesForMeters(const double meters)
{
    return meters / earthRadiusMeters * 180.0 / std::numbers::pi;
}

// A synthetic axis wide enough to be legal (buildProgressAxis requires
// length in [50, 30000]) shaped like two long, close, opposite-direction
// straights joined by short connectors -- deliberately similar to a hairpin
// or a start/finish straight with a return road nearby: exactly the shape
// nearest-GPS-point matching alone cannot resolve (KAN-32). Point (0,0) is
// the trace's first point, and the timing gate is placed there too, so
// progress runs 0 (outbound start) -> 100 (outbound end) -> ~104 (connector)
// -> ~154 (return end) -> ~208 back to 0.
QVector<LapTracePoint> hairpinTracePoints()
{
    QVector<LapTracePoint> points;
    double time = 0;
    const auto add = [&](const double east, const double north) { points.append({time, east, north}); time += 1.0; };
    for (int i = 0; i <= 50; ++i) add(100.0 * i / 50.0, 0.0); // outbound leg: (0,0) -> (100,0)
    for (int i = 1; i <= 4; ++i) add(100.0, 4.0 * i / 4.0); // connector up
    for (int i = 1; i <= 50; ++i) add(100.0 - 100.0 * i / 50.0, 4.0); // return leg: (100,4) -> (0,4), opposite direction
    for (int i = 1; i < 4; ++i) add(0.0, 4.0 - 4.0 * i / 4.0); // connector down, back toward (0,0)
    return points;
}

ProgressAxis buildHairpinAxis()
{
    LapTrace trace;
    trace.points = hairpinTracePoints();
    const GeoCoordinate origin{0.0, 0.0};
    const TimingGate gate{TimingGateType::Start, "test", origin, origin, {}};
    return buildProgressAxis(trace, origin, gate);
}

// A GPS session that walks the outbound leg of the hairpin shape, hits one
// wildly-off-track outlier, then (after a real time gap) resumes further
// along the same leg.
TelemetrySession hairpinGpsSessionWithOutlierAndGap()
{
    TelemetrySession session;
    session.aliases = {{"latitude", "lat"}, {"longitude", "lon"}};
    session.channels.insert("lat", {});
    session.channels.insert("lon", {});
    auto &lat = session.channels["lat"];
    auto &lon = session.channels["lon"];
    const auto addFix = [&](const double time, const double east, const double north) {
        lat.appendSample(time, static_cast<float>(degreesForMeters(north)));
        lon.appendSample(time, static_cast<float>(degreesForMeters(east)));
    };
    for (int i = 0; i <= 20; ++i) addFix(i * 1.0, i * 2.0, 0.0); // t=0..20s, east 0..40m, steady 2 m/s
    addFix(21.0, 10.0, 50.0); // one sample far off either leg: a GPS outlier
    for (int i = 0; i <= 20; ++i) addFix(30.0 + i, 40.0 + i * 2.0, 0.0); // t=30..50s, east 40..80m, after a real gap
    session.duration = 50.0;
    return session;
}

struct RouteFixture {
    TelemetrySession session;
    LapSession laps;
    ProgressAxis axis;
};

// A real (synthetic-GPS) closed circuit, run through the actual lap-timing
// and route machinery, not a hand-built shape -- exercises buildProgressAxis
// against the same LapTrace production code produces.
RouteFixture buildRouteFixture(const int turns = 2)
{
    RouteFixture fixture;
    fixture.session = VboParser::parse(QString::fromUtf8(EventProjectFixture::routeVbo(240, -1.0, 0, false, 300, turns)));
    fixture.laps = deriveSourceLapSession(fixture.session);
    if (!fixture.laps.selectedStartGate || fixture.laps.lapTraces.isEmpty()) return fixture;
    const auto &gate = *fixture.laps.selectedStartGate;
    const GeoCoordinate origin{
        (gate.endpointA.latitudeDegrees + gate.endpointB.latitudeDegrees) / 2,
        (gate.endpointA.longitudeDegrees + gate.endpointB.longitudeDegrees) / 2};
    fixture.axis = buildProgressAxis(fixture.laps.lapTraces.first(), origin, gate);
    return fixture;
}

// The route fixture with its timing gate turned 45 degrees and moved along
// it, so the gate still crosses the racing line at (300, 0) but its midpoint
// sits about 10 m away, 7 m ahead along the direction of travel (KAN-152).
// Progress 0 anchored at the point nearest the midpoint would then fall
// after the gate, and each lap's first fix would project just before it.
RouteFixture buildObliqueGateRouteFixture()
{
    constexpr double latitude = 52, longitude = 21, earth = 6'371'000;
    const auto lat = [](double north) { return latitude + north / earth * 180 / std::numbers::pi; };
    const auto lon = [](double east) {
        return longitude + east / (earth * std::cos(latitude * std::numbers::pi / 180)) * 180 / std::numbers::pi;
    };
    const auto gateLine = [](double eastA, double northA, double eastB, double northB, auto lat, auto lon) {
        return QString("Start %1 %2 %3 %4 start").arg(lon(eastA), 0, 'f', 9).arg(lat(northA), 0, 'f', 9)
            .arg(lon(eastB), 0, 'f', 9).arg(lat(northB), 0, 'f', 9);
    };
    QString text = QString::fromUtf8(EventProjectFixture::routeVbo(240, -1.0, 0, false, 300, 4));
    const QString original = gateLine(280, 0, 320, 0, lat, lon);
    if (!text.contains(original)) return {};
    text.replace(original, gateLine(282, -18, 332, 32, lat, lon));

    RouteFixture fixture;
    fixture.session = VboParser::parse(text);
    fixture.laps = deriveSourceLapSession(fixture.session);
    if (!fixture.laps.selectedStartGate || fixture.laps.lapTraces.isEmpty()) return fixture;
    const auto &gate = *fixture.laps.selectedStartGate;
    const GeoCoordinate origin{
        (gate.endpointA.latitudeDegrees + gate.endpointB.latitudeDegrees) / 2,
        (gate.endpointA.longitudeDegrees + gate.endpointB.longitudeDegrees) / 2};
    fixture.axis = buildProgressAxis(fixture.laps.lapTraces.first(), origin, gate);
    return fixture;
}

ApprovedSegmentation quarterSectors(const double length)
{
    const QString configuration = "compatibility-v1:" + QString(64, 'a');
    QJsonArray segments;
    for (int quarter = 0; quarter < 4; ++quarter)
        segments.append(makeTrackSegment(TrackSegmentType::Sector, QString("S%1").arg(quarter + 1),
            length * quarter / 4, length * (quarter + 1) / 4, configuration));
    return approvedSegmentation(segments, configuration);
}

} // namespace

class TrackProgressTests final : public QObject {
    Q_OBJECT
private slots:
    void buildsAxisAnchoredAtTheGate();
    void identicalLapsProduceZeroDelta();
    void knownDelayHasCorrectSignAndFinishLineMagnitude();
    void ambiguousEquidistantCrossingIsRejected();
    void headingRejectsOppositeDirectionParallelSection();
    void outlierAndGapBreakSegmentsWithoutBridging();
    void deltaSeriesOnlyCoversSharedValidRange();
    void anchorsProgressZeroWhereTheGateCrossesTheAxis();
    void rejectsAStartGateThatMissesTheTrack();
    void anchorsAtTheTimedCrossingWhenTheGateCrossesTwice();
    void spreadsDenseLapTracesEvenly();
    void unwrapsAFirstFixJustBeforeTheGate();
    void keepsTheLockOnAnAxisFromAJitteryTrace();
    void restartsASegmentAfterAGapWithoutSkippingALap();
    void obliqueOffLineGateKeepsEveryLapsSectors();
    void deltaAtTheFinishEqualsTheLapTimeDifference();
    void marksStraightsWithZeroCurvatureAndCornersWithASpike();
    void producesConsistentlySignedCurvatureOnAConvexLoop();
    void rejectsInvalidSmoothingAndNeverModifiesTheAxis();
};

void TrackProgressTests::buildsAxisAnchoredAtTheGate()
{
    const auto fixture = buildRouteFixture();
    QVERIFY2(fixture.laps.selectedStartGate.has_value(), "fixture must resolve a start gate");
    QVERIFY2(!fixture.laps.lapTraces.isEmpty(), "fixture must produce at least one lap trace");
    QVERIFY(fixture.axis.valid);
    QVERIFY(fixture.axis.points.size() >= 32);
    QCOMPARE(fixture.axis.cumulative.size(), fixture.axis.points.size());
    QCOMPARE(fixture.axis.cumulative.first(), 0.0);
    // Evenly arc-spaced by construction.
    QVERIFY(std::abs(fixture.axis.spacingMeters - fixture.axis.lengthMeters / fixture.axis.points.size()) < 1e-6);
    // Progress must stay within [0, lengthMeters) and start right at the gate.
    const GeoCoordinate origin = fixture.axis.origin;
    const auto &gate = *fixture.laps.selectedStartGate;
    const GeoCoordinate gateMidpoint{
        (gate.endpointA.latitudeDegrees + gate.endpointB.latitudeDegrees) / 2,
        (gate.endpointA.longitudeDegrees + gate.endpointB.longitudeDegrees) / 2};
    const MetricPoint gateLocal = projectCoordinate(gateMidpoint, origin);
    const double distanceFromGateToAxisStart = std::hypot(
        fixture.axis.points.first().x() - gateLocal.eastMeters, fixture.axis.points.first().y() - gateLocal.northMeters);
    QVERIFY(distanceFromGateToAxisStart < fixture.axis.spacingMeters * 2);
}

void TrackProgressTests::identicalLapsProduceZeroDelta()
{
    const auto fixture = buildRouteFixture();
    QVERIFY(fixture.axis.valid);
    QVERIFY(!fixture.laps.timedLaps.isEmpty());
    const auto &lap = fixture.laps.timedLaps.first();
    const auto trace = projectLapTrace(fixture.axis, fixture.session, lap.startTelemetryTime, lap.endTelemetryTime);
    QVERIFY(!trace.isEmpty());
    qsizetype covered = 0;
    for (const auto &segment : trace) covered += segment.samples.size();
    QVERIFY2(covered > 60, "the synthetic lap should mostly lock on");

    const auto delta = computeDeltaSeries(trace, trace, 20.0);
    QVERIFY(!delta.isEmpty());
    for (const auto &segment : delta)
        for (const auto &point : segment)
            QVERIFY(std::abs(point.deltaSeconds) < 0.05);
}

void TrackProgressTests::knownDelayHasCorrectSignAndFinishLineMagnitude()
{
    const auto fixture = buildRouteFixture();
    QVERIFY(fixture.axis.valid);
    QVERIFY(!fixture.laps.timedLaps.isEmpty());
    const auto &lap = fixture.laps.timedLaps.first();
    const auto lapA = projectLapTrace(fixture.axis, fixture.session, lap.startTelemetryTime, lap.endTelemetryTime);
    QVERIFY(!lapA.isEmpty());

    // Lap B: the identical physical path, uniformly 10% slower.
    constexpr double slowdownFactor = 1.10;
    TelemetrySession sessionB = fixture.session;
    for (auto &channel : sessionB.channels) {
        auto times = channel.timestamps();
        for (auto &timestamp : times) timestamp *= slowdownFactor;
        channel.setSamples(times, channel.values());
    }
    sessionB.duration *= slowdownFactor;
    const auto lapB = projectLapTrace(
        fixture.axis, sessionB, lap.startTelemetryTime * slowdownFactor, lap.endTelemetryTime * slowdownFactor);
    QVERIFY(!lapB.isEmpty());

    const auto delta = computeDeltaSeries(lapA, lapB, 20.0);
    QVERIFY(!delta.isEmpty());
    double lastDelta = 0.0;
    bool sawClearlyAhead = false;
    for (const auto &segment : delta) {
        for (const auto &point : segment) {
            // A is faster throughout (B is uniformly slower): A must never
            // read as meaningfully behind.
            QVERIFY(point.deltaSeconds < 0.5);
            if (point.deltaSeconds < -0.2) sawClearlyAhead = true;
            lastDelta = point.deltaSeconds;
        }
    }
    QVERIFY2(sawClearlyAhead, "A should read as ahead of the 10%-slower B somewhere along the lap");
    const double actualDurationDifference = lap.durationSeconds - lap.durationSeconds * slowdownFactor; // A - B
    QVERIFY2(std::abs(lastDelta - actualDurationDifference) < 1.0,
        "the delta near the finish line should approximate the true lap-time difference");
}

void TrackProgressTests::ambiguousEquidistantCrossingIsRejected()
{
    const auto axis = buildHairpinAxis();
    QVERIFY(axis.valid);
    ProjectionContext context;
    context.hasLock = true;
    // Near the apex (progress ~100, where the outbound leg ends and the
    // close-by return leg begins a few meters later in progress) rather than
    // the middle of the straights: that is where the two branches are close
    // in both space AND progress, so a bounded search window actually sees
    // both -- the real shape of a hairpin/parallel-section ambiguity.
    context.lastProgressMeters = 90.0;
    context.lastTelemetryTime = 0.0;

    // Exactly between the outbound leg (north=0) and the return leg
    // (north=4) right at the apex: a pure nearest-point match cannot tell
    // them apart.
    const QPointF equidistant(98.0, 2.0);
    const QPointF movingOutbound(1.0, 0.0);
    const auto result = projectSample(axis, equidistant, 0.5, 20.0, movingOutbound, context);
    QVERIFY2(!result.valid, "an equidistant crossing must return unavailable alignment, not a guess");
    // Rejection must not corrupt the lock for the next, unambiguous sample.
    QVERIFY(context.hasLock);
    QCOMPARE(context.lastProgressMeters, 90.0);
}

void TrackProgressTests::headingRejectsOppositeDirectionParallelSection()
{
    const auto axis = buildHairpinAxis();
    QVERIFY(axis.valid);
    ProjectionContext context;
    context.hasLock = true;
    context.lastProgressMeters = 90.0; // approaching the apex along the outbound leg
    context.lastTelemetryTime = 0.0;
    const QPointF movingOutbound(1.0, 0.0);

    // Closer to the return leg (0.5m) than the outbound leg (3.5m) by pure
    // distance -- an ambiguity-gap check alone would accept this as the
    // return leg. But the car is moving in the outbound leg's direction, and
    // the return leg runs the opposite way there: sample order/direction
    // (KAN-32) must reject it rather than jump to the wrong branch.
    const QPointF closerToReturnLeg(98.0, 3.5);
    const auto rejected = projectSample(axis, closerToReturnLeg, 0.5, 20.0, movingOutbound, context);
    QVERIFY2(!rejected.valid, "nearest-point distance alone must not override a direction conflict");

    // The same context, given a point that is actually on the outbound leg,
    // still locks on normally -- the heading check does not just reject
    // everything near the ambiguous zone.
    const QPointF onOutboundLeg(98.0, 0.2);
    const auto accepted = projectSample(axis, onOutboundLeg, 0.5, 20.0, movingOutbound, context);
    QVERIFY(accepted.valid);
    QVERIFY(accepted.progressMeters > 93.0 && accepted.progressMeters < 100.0);
}

void TrackProgressTests::outlierAndGapBreakSegmentsWithoutBridging()
{
    const auto axis = buildHairpinAxis();
    QVERIFY(axis.valid);
    const auto session = hairpinGpsSessionWithOutlierAndGap();
    const auto trace = projectLapTrace(axis, session, 0, session.duration);

    QVERIFY2(trace.size() >= 2, "the outlier and the real time gap must each break the segment");
    qsizetype totalSamples = 0;
    for (const auto &segment : trace) {
        totalSamples += segment.samples.size();
        // No segment may contain a meaningful backward jump: continuity is
        // enforced per-segment, and a break must never bridge a gap by
        // interpolating across it.
        for (qsizetype i = 1; i < segment.samples.size(); ++i)
            QVERIFY(segment.samples[i].progressMeters >= segment.samples[i - 1].progressMeters - 3.0);
    }
    // 21 samples before the outlier, 21 after the gap; the outlier itself
    // (and any sample it could have contaminated) must not appear.
    QVERIFY2(totalSamples <= 42, "the off-track outlier must not be counted as a locked sample");
    QVERIFY2(totalSamples >= 30, "most of the on-track samples should still lock");
}

void TrackProgressTests::deltaSeriesOnlyCoversSharedValidRange()
{
    QVector<ProgressSegment> lapA(1);
    lapA[0].samples = {{0.0, 0.0, true}, {10.0, 100.0, true}}; // covers progress [0,100] over 10s
    QVector<ProgressSegment> lapB(1);
    lapB[0].samples = {{5.0, 50.0, true}, {15.0, 150.0, true}}; // covers progress [50,150] over 10s

    const auto delta = computeDeltaSeries(lapA, lapB, 10.0);
    QVERIFY(!delta.isEmpty());
    bool sawNearFifty = false, sawNearHundred = false;
    for (const auto &segment : delta) {
        for (const auto &point : segment) {
            // Only the overlap [50,100] is covered by both laps; nothing
            // outside it may appear rather than extrapolating a guess.
            QVERIFY(point.progressMeters >= 50.0 - 1e-6 && point.progressMeters <= 100.0 + 1e-6);
            if (point.progressMeters <= 51.0) sawNearFifty = true;
            if (point.progressMeters >= 99.0) sawNearHundred = true;
        }
    }
    QVERIFY(sawNearFifty);
    QVERIFY(sawNearHundred);
}

void TrackProgressTests::anchorsProgressZeroWhereTheGateCrossesTheAxis()
{
    const auto fixture = buildObliqueGateRouteFixture();
    QVERIFY2(fixture.laps.selectedStartGate.has_value(), "fixture must resolve the oblique start gate");
    QVERIFY(fixture.axis.valid);
    // The gate crosses the line at (300, 0) in the fixture's frame, which is
    // (-7, -7) from the gate midpoint the axis uses as its origin.
    const QPointF crossing(-7.0, -7.0);
    const double distance = std::hypot(
        fixture.axis.points.first().x() - crossing.x(), fixture.axis.points.first().y() - crossing.y());
    QVERIFY2(distance < 1.0, qPrintable(QString("progress 0 is %1 m from the crossing").arg(distance)));
    QVERIFY(std::abs(fixture.axis.spacingMeters - fixture.axis.lengthMeters / fixture.axis.points.size()) < 1e-6);
}

namespace {
// A 200 x 50 m rectangle driven anticlockwise, starting at (102, 0) on the
// bottom straight, points every 2 m.
LapTrace rectangleLap()
{
    QVector<QPointF> corners{{0, 0}, {200, 0}, {200, 50}, {0, 50}, {0, 0}};
    QVector<QPointF> loop;
    for (qsizetype c = 1; c < corners.size(); ++c) {
        const QPointF from = corners[c - 1], to = corners[c];
        const int steps = static_cast<int>(std::hypot(to.x() - from.x(), to.y() - from.y()) / 2.0);
        for (int i = 0; i < steps; ++i) loop.append(from + (to - from) * (double(i) / steps));
    }
    const qsizetype start = std::find(loop.cbegin(), loop.cend(), QPointF(102, 0)) - loop.cbegin();
    LapTrace trace;
    for (qsizetype i = 0; i < loop.size(); ++i) {
        const QPointF &point = loop[(start + i) % loop.size()];
        trace.points.append({double(i), point.x(), point.y()});
    }
    return trace;
}

TimingGate gateBetween(const QPointF &a, const QPointF &b)
{
    return {TimingGateType::Start, "start",
            {degreesForMeters(a.y()), degreesForMeters(a.x())},
            {degreesForMeters(b.y()), degreesForMeters(b.x())}, {}};
}
} // namespace

void TrackProgressTests::rejectsAStartGateThatMissesTheTrack()
{
    // KAN-212: a 10 m line across x = 100 below the bottom straight, its near
    // end 5, 25 or 100 m from the path.
    const GeoCoordinate origin{0.0, 0.0};
    const auto axisFor = [&](const double gap) {
        return buildProgressAxis(rectangleLap(), origin, gateBetween({100, -gap - 10}, {100, -gap}));
    };
    const auto near = axisFor(5);
    QVERIFY(near.valid);
    QVERIFY(near.problem.isEmpty());
    QVERIFY(std::hypot(near.points.first().x() - 100.0, near.points.first().y()) < 3.0);
    for (const double gap : {25.0, 100.0}) {
        const auto far = axisFor(gap);
        QVERIFY2(!far.valid, qPrintable(QString::number(gap)));
        QVERIFY2(far.problem.contains(QStringLiteral("%1 m away").arg(gap)), qPrintable(far.problem));
    }
    // A line across a pit lane 30 m below the racing surface, not the track.
    QVERIFY(!buildProgressAxis(rectangleLap(), origin, gateBetween({100, -35}, {100, -25})).valid);
}

void TrackProgressTests::anchorsAtTheTimedCrossingWhenTheGateCrossesTwice()
{
    // KAN-212: a long line over both straights. Its midpoint (100, 30) is
    // nearer the top straight, but the lap was timed on the bottom one, where
    // it starts.
    const auto axis = buildProgressAxis(rectangleLap(), GeoCoordinate{0.0, 0.0}, gateBetween({100, -10}, {100, 70}));
    QVERIFY(axis.valid);
    QVERIFY2(std::hypot(axis.points.first().x() - 100.0, axis.points.first().y()) < 0.5,
             qPrintable(QString("%1, %2").arg(axis.points.first().x()).arg(axis.points.first().y())));
}

void TrackProgressTests::spreadsDenseLapTracesEvenly()
{
    // KAN-220: 100 Hz GPS around the 500 m rectangle at 10 m/s, 5,000 fixes
    // a lap. A whole-number stride kept every second fix (about 2,500 of the
    // 4,096 a trace may hold); latitude buckets kept one fix per bucket on
    // the east-west straights, where latitude does not change.
    const TimingGate gate = gateBetween({100, -10}, {100, 10});
    TelemetrySession session;
    session.aliases = {{"latitude", "lat"}, {"longitude", "lon"}};
    session.timingGates = {gate};
    TelemetryChannel lat(QStringLiteral("lat"), {}), lon(QStringLiteral("lon"), {});
    const auto position = [](double distance) {
        distance = std::fmod(distance, 500.0);
        if (distance < 200) return QPointF(distance, 0);
        if (distance < 250) return QPointF(200, distance - 200);
        if (distance < 450) return QPointF(200 - (distance - 250), 50);
        return QPointF(0, 50 - (distance - 450));
    };
    for (int fix = 0; fix <= 16'500; ++fix) {
        const double time = fix / 100.0;
        const QPointF at = position(50.0 + 10.0 * time);
        lat.appendSample(time, static_cast<float>(degreesForMeters(at.y())));
        lon.appendSample(time, static_cast<float>(degreesForMeters(at.x())));
    }
    session.channels.insert(lat.name, lat);
    session.channels.insert(lon.name, lon);
    session.duration = 165.0;

    const auto laps = deriveSourceLapSession(session);
    QVERIFY(!laps.lapTraces.isEmpty());
    for (const auto &trace : laps.lapTraces) {
        QVERIFY2(trace.points.size() > 4'000 && trace.points.size() <= 4'096,
                 qPrintable(QString::number(trace.points.size())));
    }

    const auto axis = buildProgressAxis(rectangleLap(), GeoCoordinate{0.0, 0.0}, gate);
    QVERIFY(axis.valid);
    const auto projected = projectLapTrace(axis, session, 5.0, 105.0);
    qsizetype count = 0;
    double widest = 0.0;
    for (const auto &segment : projected) {
        count += segment.samples.size();
        for (qsizetype index = 1; index < segment.samples.size(); ++index)
            widest = std::max(widest, segment.samples[index].telemetryTime - segment.samples[index - 1].telemetryTime);
    }
    QVERIFY2(count >= 7'900 && count <= 8'010, qPrintable(QString::number(count)));
    QVERIFY2(widest <= 0.0201, qPrintable(QString::number(widest)));
}

void TrackProgressTests::unwrapsAFirstFixJustBeforeTheGate()
{
    // The hairpin's gate is a point, so progress 0 is the axis point nearest
    // it; the lap's first fix is on the connector just before it.
    const auto axis = buildHairpinAxis();
    QVERIFY(axis.valid);
    TelemetrySession session;
    session.aliases = {{"latitude", "lat"}, {"longitude", "lon"}};
    auto &lat = session.channels["lat"];
    auto &lon = session.channels["lon"];
    const auto addFix = [&](const double time, const double east, const double north) {
        lat.appendSample(time, static_cast<float>(degreesForMeters(north)));
        lon.appendSample(time, static_cast<float>(degreesForMeters(east)));
    };
    addFix(0.0, 0.0, 1.0);
    for (int i = 1; i <= 20; ++i) addFix(i * 1.0, i * 2.0 - 0.5, 0.0);
    session.duration = 20.0;

    const auto trace = projectLapTrace(axis, session, 0.0, session.duration);
    QCOMPARE(trace.size(), 1);
    QCOMPARE(trace.first().samples.size(), 21);
    const auto &first = trace.first().samples.first();
    QVERIFY2(first.progressMeters < 0.0 && first.progressMeters > -3.0,
        qPrintable(QString("the first fix is at %1 m").arg(first.progressMeters)));
    for (qsizetype i = 1; i < trace.first().samples.size(); ++i)
        QVERIFY(trace.first().samples[i].progressMeters > trace.first().samples[i - 1].progressMeters);
    const auto atGate = timeAtProgress(trace, 0.0);
    QVERIFY(atGate.has_value());
    QVERIFY(*atGate > 0.0 && *atGate < 1.0);
}

// KAN-237 (as Telemetry FET-249): an axis built from a jittery recording.
// The recorded path zig-zags 0.8 m across a 600 x 60 m rectangle every metre,
// so it is about 28% longer than the axis resampled from it, and progress
// divided by the spacing falls behind the car. The search window must follow
// the axis's own distances, or a clean lap loses its lock within 100 m.
void TrackProgressTests::keepsTheLockOnAnAxisFromAJitteryTrace()
{
    const QVector<QPointF> corners{{0, 0}, {600, 0}, {600, 60}, {0, 60}, {0, 0}};
    const auto alongRectangle = [&corners](const double step, const double zigzag) {
        QVector<QPointF> path;
        int ordinal = 0;
        for (qsizetype side = 1; side < corners.size(); ++side) {
            const QPointF from = corners[side - 1];
            const QPointF delta = corners[side] - from;
            const double length = std::hypot(delta.x(), delta.y());
            const QPointF across(-delta.y() / length, delta.x() / length);
            const int steps = static_cast<int>(std::lround(length / step));
            for (int i = 0; i < steps; ++i, ++ordinal)
                path.append(from + delta * (double(i) / steps) + across * (ordinal % 2 == 0 ? zigzag : -zigzag));
        }
        return path;
    };
    LapTrace trace;
    double time = 0;
    for (const QPointF &point : alongRectangle(1.0, 0.4)) trace.points.append({time += 0.04, point.x(), point.y()});
    const GeoCoordinate origin{0.0, 0.0};
    const TimingGate gate{TimingGateType::Start, "test", origin, origin, {}};
    const auto axis = buildProgressAxis(trace, origin, gate);
    QVERIFY(axis.valid);
    QVERIFY2(axis.cumulative.last() < 0.85 * axis.lengthMeters,
             qPrintable(QString("axis %1 m of a %2 m path").arg(axis.cumulative.last()).arg(axis.lengthMeters)));

    // A clean lap at 20 m/s, 10 Hz, along the rectangle's centre line.
    TelemetrySession session;
    session.aliases = {{"latitude", "lat"}, {"longitude", "lon"}};
    auto &lat = session.channels["lat"];
    auto &lon = session.channels["lon"];
    time = 0;
    for (const QPointF &point : alongRectangle(2.0, 0.0)) {
        lat.appendSample(time, static_cast<float>(degreesForMeters(point.y())));
        lon.appendSample(time, static_cast<float>(degreesForMeters(point.x())));
        time += 0.1;
    }
    session.duration = time - 0.1;
    const auto projected = projectLapTrace(axis, session, 0.0, session.duration);
    QCOMPARE(projected.size(), 1);
    const auto &samples = projected.first().samples;
    QVERIFY2(samples.last().progressMeters > 0.95 * axis.cumulative.last(),
             qPrintable(QString("lock held to %1 m of %2 m").arg(samples.last().progressMeters)
                            .arg(axis.cumulative.last())));
}

// KAN-237: after a GPS gap the first fix may be found a few metres behind
// the last one. It is the same lap, not the next: with the 3 m jitter
// tolerance a fix 8 m back moved the rest of the lap one lap on.
void TrackProgressTests::restartsASegmentAfterAGapWithoutSkippingALap()
{
    const auto axis = buildHairpinAxis();
    QVERIFY(axis.valid);
    TelemetrySession session;
    session.aliases = {{"latitude", "lat"}, {"longitude", "lon"}};
    auto &lat = session.channels["lat"];
    auto &lon = session.channels["lon"];
    const auto addFix = [&](const double time, const double east, const double north) {
        lat.appendSample(time, static_cast<float>(degreesForMeters(north)));
        lon.appendSample(time, static_cast<float>(degreesForMeters(east)));
    };
    for (int i = 0; i <= 30; ++i) addFix(i * 1.0, i * 2.0, 0.0); // east 0..60 m
    for (int i = 0; i <= 14; ++i) addFix(40.0 + i, 52.0 + i * 2.0, 0.0); // after a 10 s gap, from 8 m back
    session.duration = 54.0;

    const auto trace = projectLapTrace(axis, session, 0.0, session.duration);
    QCOMPARE(trace.size(), 2);
    const double restart = trace.last().samples.first().progressMeters;
    QVERIFY2(restart > 45.0 && restart < 55.0, qPrintable(QString("the restart is at %1 m").arg(restart)));
    QVERIFY(trace.last().samples.last().progressMeters < axis.lengthMeters);
}

void TrackProgressTests::obliqueOffLineGateKeepsEveryLapsSectors()
{
    const auto fixture = buildObliqueGateRouteFixture();
    QVERIFY(fixture.axis.valid);
    QVERIFY(fixture.laps.timedLaps.size() >= 3);
    const auto approved = quarterSectors(fixture.axis.lengthMeters);
    QVERIFY(approved.valid);
    for (const auto &lap : fixture.laps.timedLaps) {
        const auto trace = projectLapTrace(fixture.axis, fixture.session, lap.startTelemetryTime, lap.endTelemetryTime);
        QVERIFY(!trace.isEmpty());
        for (const auto &segment : trace)
            for (qsizetype i = 1; i < segment.samples.size(); ++i)
                QVERIFY2(segment.samples[i].progressMeters >= segment.samples[i - 1].progressMeters,
                    "progress must never fall within a segment, across the gate too");
        const auto times = computeLapSectorTimes(approved, fixture.axis.lengthMeters, trace,
            lap.startTelemetryTime, lap.endTelemetryTime, {});
        QVERIFY(times.valid);
        for (const auto &sector : times.sectors)
            QVERIFY2(sector.seconds.has_value(), qPrintable(QString("lap %1 lost %2").arg(lap.number).arg(sector.name)));
        QVERIFY(times.sumSeconds.has_value());
        QVERIFY(std::abs(*times.sumSeconds - times.lapSeconds) < 1e-6);
    }
}

void TrackProgressTests::deltaAtTheFinishEqualsTheLapTimeDifference()
{
    const auto fixture = buildObliqueGateRouteFixture();
    QVERIFY(fixture.axis.valid);
    QVERIFY(!fixture.laps.timedLaps.isEmpty());
    const auto &lap = fixture.laps.timedLaps.first();
    const auto lapA = projectLapTrace(fixture.axis, fixture.session, lap.startTelemetryTime, lap.endTelemetryTime);

    // Lap B: the same path 10% slower, its clock also shifted so the lap's
    // first fix lands at a different point within the GPS interval.
    constexpr double slowdownFactor = 1.10;
    constexpr double shiftSeconds = 0.13;
    TelemetrySession sessionB = fixture.session;
    for (auto &channel : sessionB.channels) {
        auto times = channel.timestamps();
        for (auto &timestamp : times) timestamp = timestamp * slowdownFactor + shiftSeconds;
        channel.setSamples(times, channel.values());
    }
    sessionB.duration = sessionB.duration * slowdownFactor + shiftSeconds;
    const auto lapsB = deriveSourceLapSession(sessionB);
    QVERIFY(!lapsB.timedLaps.isEmpty());
    const auto &lapB = lapsB.timedLaps.first();
    const auto traceB = projectLapTrace(fixture.axis, sessionB, lapB.startTelemetryTime, lapB.endTelemetryTime);

    const auto delta = computeDeltaSeries(lapA, traceB, 20.0,
        DeltaTiming{lap.startTelemetryTime, lap.endTelemetryTime, lapB.startTelemetryTime, lapB.endTelemetryTime,
            fixture.axis.lengthMeters});
    QVERIFY(!delta.isEmpty());
    QVERIFY(!delta.first().isEmpty());
    QCOMPARE(delta.first().first().progressMeters, 0.0);
    QVERIFY2(std::abs(delta.first().first().deltaSeconds) < 1e-6, "both laps start at their timed start");
    const auto &finish = delta.last().last();
    QCOMPARE(finish.progressMeters, fixture.axis.lengthMeters);
    const double lapTimeDifference = lap.durationSeconds - lapB.durationSeconds;
    QVERIFY2(std::abs(finish.deltaSeconds - lapTimeDifference) < 1e-6,
        qPrintable(QString("finish delta %1 s, lap-time difference %2 s").arg(finish.deltaSeconds).arg(lapTimeDifference)));
}

void TrackProgressTests::marksStraightsWithZeroCurvatureAndCornersWithASpike()
{
    // KAN-44: the hairpin fixture's two straights are exactly colinear by
    // construction and its two connectors turn the path ~180 degrees over
    // only a few meters -- the sharpest, most unambiguous "predictable
    // feature" case: zero on a straight, a real spike at a real corner.
    const auto axis = buildHairpinAxis();
    QVERIFY(axis.valid);
    const auto features = computeTrackFeatures(axis, 6.0);
    QVERIFY(features.valid);
    QCOMPARE(features.samples.size(), axis.points.size());
    const auto n = features.samples.size();

    // Deep inside the outbound straight (heading pointing east, +x): both
    // heading and curvature must be the exact straight-line values -- these
    // axis points are exactly colinear, so smoothing changes nothing.
    constexpr int outboundIndex = 25;
    QVERIFY(std::abs(features.samples[outboundIndex].headingRadians) < 1e-6);
    QVERIFY(std::abs(features.samples[outboundIndex].curvaturePerMeter) < 1e-6);

    // Deep inside the return straight (heading pointing west, -x).
    constexpr int returnIndex = 75;
    QVERIFY(std::abs(std::abs(features.samples[returnIndex].headingRadians) - std::numbers::pi) < 1e-6);
    QVERIFY(std::abs(features.samples[returnIndex].curvaturePerMeter) < 1e-6);

    // Generous windows around where the resampled axis places each
    // connector (progress ~100m and ~204-208m of the ~208m loop, i.e. axis
    // indices near 50 and near the 104/0 wrap at ~2m spacing) -- wide enough
    // to tolerate exact resampling-index drift without including so much of
    // either straight that a real spike could be diluted away.
    double cornerMax = 0.0;
    for (int index = 40; index <= 60; ++index)
        cornerMax = std::max(cornerMax, std::abs(features.samples[index % n].curvaturePerMeter));
    for (int index = 92; index <= 112; ++index)
        cornerMax = std::max(cornerMax, std::abs(features.samples[index % n].curvaturePerMeter));
    QVERIFY2(cornerMax > 0.2, "the sharp connector must show a real curvature spike");
}

void TrackProgressTests::producesConsistentlySignedCurvatureOnAConvexLoop()
{
    // routeVbo()'s default (reverse=false) ellipse is traced counterclockwise
    // in the local east/north frame; a convex loop traversed consistently in
    // one rotational direction never reverses its turning direction, so
    // curvature must stay positive (left-turning) everywhere it is sampled,
    // not just on average -- this is what "noise ... produce[s] predictable
    // features" means for a real closed track shape.
    const auto fixture = buildRouteFixture();
    QVERIFY(fixture.axis.valid);
    const auto features = computeTrackFeatures(fixture.axis, 6.0);
    QVERIFY(features.valid);
    QCOMPARE(features.samples.size(), fixture.axis.points.size());

    const auto n = features.samples.size();
    for (int tenth = 0; tenth < 10; ++tenth) {
        const auto index = (n * tenth) / 10;
        QVERIFY2(features.samples[index].curvaturePerMeter > 0.0,
            qPrintable(QString("curvature at index %1 (progress %2 m) should be positive")
                .arg(index).arg(features.samples[index].progressMeters)));
    }
}

void TrackProgressTests::rejectsInvalidSmoothingAndNeverModifiesTheAxis()
{
    const auto axis = buildHairpinAxis();
    QVERIFY(axis.valid);
    const auto snapshotPoints = axis.points;
    const auto snapshotLength = axis.lengthMeters;

    QVERIFY(!computeTrackFeatures(ProgressAxis{}, 6.0).valid); // invalid axis
    QVERIFY(!computeTrackFeatures(axis, 0.0).valid); // zero scale
    QVERIFY(!computeTrackFeatures(axis, -1.0).valid); // negative scale
    QVERIFY(!computeTrackFeatures(axis, std::numeric_limits<double>::infinity()).valid);
    QVERIFY(!computeTrackFeatures(axis, std::numeric_limits<double>::quiet_NaN()).valid);

    const auto features = computeTrackFeatures(axis, 6.0);
    QVERIFY(features.valid);
    QCOMPARE(features.smoothingMeters, 6.0);

    // A pure function of the axis: deriving features must never touch the
    // geometry (or any telemetry channel) they were derived from.
    QCOMPARE(axis.points, snapshotPoints);
    QCOMPARE(axis.lengthMeters, snapshotLength);
}

QTEST_GUILESS_MAIN(TrackProgressTests)
#include "TrackProgressTests.moc"
