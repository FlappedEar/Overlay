// Telemetry-core behaviour -- VBO parsing and its resource bounds, lap
// timing, coordinates, track geometry, lap references and exclusions -- in a
// test binary that links only flappedear_telemetry_core (KAN-124): no
// controller, overlay, video or Gui code.

#include "EventProjectFixture.h"
#include "project/BoundedJsonLoader.h"
#include "project/ProjectLimits.h"
#include "telemetry/LapTiming.h"
#include "telemetry/OutingLaps.h"
#include "telemetry/TelemetryFolderScan.h"
#include "telemetry/TelemetryGeometry.h"
#include "telemetry/TelemetryImportPlan.h"
#include "telemetry/TelemetrySession.h"
#include "telemetry/TelemetrySource.h"
#include "telemetry/TelemetrySyncEngine.h"
#include "telemetry/TrackGeometry.h"
#include "telemetry/TrackInference.h"
#include "telemetry/TyreData.h"
#include "telemetry/VboParser.h"

#include <QDir>
#include <QElapsedTimer>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtTest>
#include <atomic>
#include <cfloat>
#include <cmath>
#include <limits>
#include <numbers>

#if defined(Q_OS_UNIX)
#include <sys/resource.h>
#endif

using namespace FlappedEar;

namespace {
bool writeBytes(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate)
        && file.write(bytes) == bytes.size();
}
} // namespace

class TelemetryCoreTests final : public QObject {
    Q_OBJECT
private slots:
    void scansFoldersForRecordingsWithinBounds();
    void combinesDroppedFilesAndFolders();
    void derivesOutingLapSections();
    void rejectsMalformedLapReferences_data();
    void rejectsMalformedLapReferences();
    void infersRoutesFromOrderedCompleteLaps();
    void matchesLongCircuitsByCrossTrackDistance();
    void flagsALapThatLeavesTheOtherLapsLine();
    void mapsTyreChannelsPerCorner();
    void appliesChosenTyreChannels();
    void formatsLapTimesRoundedBeforeMinutes();
    void readsTyreValuesWithoutPlaceholdersOrGaps();
    void readsPrivateTyreData();
    void rejectsMalformedLapExclusions_data();
    void rejectsMalformedLapExclusions();
    void groupsOnlyDatedUnambiguousAlternatives();
    void prefersRaceChronoCalculatedAcceleration();
    void parsesRealisticFixture();
    void toleratesMalformedRows();
    void neverBridgesALossOfSignal();
    void syncIgnoresALossOfGpsFix();
    void preservesRepeatedDataSections();
    void rejectsMissingSections();
    void rejectsVboWithoutTimeColumn();
    void interpolatesByTime();
    void parsesTextFirstVboTimeFormats();
    void keepsVboTimestampsStrictlyMonotonic();
    void rejectsUnsafeVboDerivedTimes_data();
    void rejectsUnsafeVboDerivedTimes();
    void preservesMixedVboClocksAcrossMidnight();
    void rejectsOverflowingTelemetryChartRanges();
    void cancelsVboParsingDeterministically();
    void cachesTelemetryChannelCadence();
    void enforcesVboResourceLimits();
    void boundsVboHeaderAndDecodedValues();
    void normalizesVboHeaderEdgeCases();
    void buildsVboMetadataInFileOrder();
    void preservesVboScannerFormats();
    void boundsSeparatorHeavyVboRows();
    void enforcesVboScannerBoundaries();
    void cancelsVboScanningBeforeLimitFailures_data();
    void cancelsVboScanningBeforeLimitFailures();
    void cancelsVboFieldScanningAtEveryCheckpoint();
    void convertsArcMinuteCoordinates();
    void resolvesCoordinateEvidence_data();
    void resolvesCoordinateEvidence();
    void withholdsUnresolvedCoordinates_data();
    void withholdsUnresolvedCoordinates();
    void validatesDeclaredCoordinateBounds();
    void parsesBoundedRaceChronoTimingGates();
    void derivesDirectionalPassesAndCompleteLaps();
    void finalizesGatePassWhenTelemetryEndsInsideCorridor();
    void acceptsOnlyRealCrossingsOfTheStartLine();
    void parsesOptionalRealVbo();
    void derivesOptionalRealVboLaps();
    void buildsTrackGeometry();
    void cancelsTrackGeometryConstruction();
    void boundsExternalJsonDocuments();
};

void TelemetryCoreTests::derivesOutingLapSections()
{
    auto session = VboParser::parse(QString::fromUtf8(EventProjectFixture::lapsVbo()));
    session.metadata.insert("firstTimestampMilliseconds", "1780000000000");
    const auto laps = deriveSourceLapSession(session);
    QCOMPARE(laps.timedLaps.size(), 3);
    const auto rows = outingLapRows(session, laps, "run", "Morning", 0);
    QCOMPARE(rows.size(), 5);
    QCOMPARE(rows.first().type, LapSectionType::Out);
    QCOMPARE(rows.first().start, 0.0);
    QCOMPARE(rows.first().end, laps.acceptedPasses.first().telemetryTime);
    for (int i = 1; i <= 3; ++i) {
        QCOMPARE(rows[i].type, LapSectionType::Lap);
        QCOMPARE(rows[i].lapNumber, i);
        QCOMPARE(rows[i].start, laps.timedLaps[i - 1].startTelemetryTime);
        QCOMPARE(rows[i].end, laps.timedLaps[i - 1].endTelemetryTime);
    }
    QCOMPARE(rows.last().type, LapSectionType::In);
    QCOMPARE(rows.last().end, session.duration);
    const auto unknown = outingLapRows(session, {}, "unknown", "Unknown", 1);
    QCOMPARE(unknown.size(), 1); QCOMPARE(unknown[0].type, LapSectionType::Unknown);
    auto one = laps; one.acceptedPasses.resize(1); one.timedLaps.clear();
    const auto fragments = outingLapRows(session, one, "one", "One crossing", 2);
    QCOMPARE(fragments.size(), 2);
    QCOMPARE(fragments[0].type, LapSectionType::Out); QCOMPARE(fragments[1].type, LapSectionType::In);
    QVERIFY_THROWS_EXCEPTION(OperationCancelled, static_cast<void>(outingLapRows(session, laps, {}, {}, 0, [] { return true; })));
    auto tooMany = laps; tooMany.timedLaps.resize(maximumOutingLapRows);
    QVERIFY_THROWS_EXCEPTION(ResourceLimitError, static_cast<void>(outingLapRows(session, tooMany, {}, {}, 0)));
    auto mixed = rows;
    auto withoutClock = session; withoutClock.metadata.clear();
    mixed += outingLapRows(withoutClock, laps, "undated", "Undated", 1);
    std::reverse(mixed.begin(), mixed.end());
    sortOutingLaps(mixed);
    QCOMPARE(mixed.first().type, LapSectionType::Out);
    QVERIFY(mixed.first().timestampMilliseconds.has_value());
    QVERIFY(!mixed.last().timestampMilliseconds.has_value());
}

void TelemetryCoreTests::rejectsMalformedLapReferences_data()
{
    QTest::addColumn<QString>("field"); QTest::addColumn<QJsonValue>("value");
    QTest::newRow("old-number-only") << QString("version") << QJsonValue(QJsonValue::Undefined);
    QTest::newRow("future-version") << QString("version") << QJsonValue(2);
    QTest::newRow("fractional-version") << QString("version") << QJsonValue(1.1);
    QTest::newRow("missing-event") << QString("eventId") << QJsonValue("");
    QTest::newRow("long-run") << QString("runId") << QJsonValue(QString(129, 'r'));
    QTest::newRow("null-source") << QString("sourceId") << QJsonValue(QJsonValue::Null);
    QTest::newRow("bad-content-revision") << QString("sourceRevision") << QJsonValue("abc");
    QTest::newRow("bad-derivation") << QString("derivationKey") << QJsonValue(QString(64, 'z'));
    QTest::newRow("wrong-type") << QString("type") << QJsonValue("lap");
    QTest::newRow("negative-start") << QString("startTime") << QJsonValue(-1);
    QTest::newRow("empty-range") << QString("endTime") << QJsonValue(1.5);
    QTest::newRow("string-end") << QString("endTime") << QJsonValue("3.5");
    QTest::newRow("nonfinite-end") << QString("endTime") << QJsonValue(std::numeric_limits<double>::infinity());
}

void TelemetryCoreTests::rejectsMalformedLapReferences()
{
    QFETCH(QString, field); QFETCH(QJsonValue, value);
    OutingLapRow row; row.runId = "run"; row.type = LapSectionType::Lap; row.start = 1.5; row.end = 3.5;
    auto reference = makeLapReference(row, "event", "source", QByteArray(64, 'a'), QByteArray(64, 'b'));
    QVERIFY(validLapReference(reference)); reference.insert(field, value);
    QVERIFY(!validLapReference(reference));
}

void TelemetryCoreTests::matchesLongCircuitsByCrossTrackDistance()
{
    // A 3.6 km circuit (Silesia Ring size): 256 route points are ~14 m apart,
    // so two laps on the same line can be sampled up to 7 m apart along the
    // track. Only the cross-track distance decides whether routes differ.
    const auto circuit = [](const double lateralMeters, const double phase, const double bulgeMeters) {
        RouteShape route;
        route.direction = QStringLiteral("counterclockwise");
        route.origin = {50.0, 19.0};
        const double a = 800.0, b = 360.0; // ellipse semi-axes: ~3.6 km around
        QVector<QPointF> points;
        for (int i = 0; i < 256; ++i) {
            const double t = 2.0 * std::numbers::pi * (i + phase) / 256.0;
            const QPointF onTrack(a * std::cos(t), b * std::sin(t));
            const QPointF normal(b * std::cos(t), a * std::sin(t));
            const double length = std::hypot(normal.x(), normal.y());
            // A wider line (lateral) everywhere, plus a detour on a quarter of the lap.
            const double offset = lateralMeters + (t > 0.5 && t < 2.0 ? bulgeMeters : 0.0);
            points.append(onTrack + normal / length * offset);
        }
        double perimeter = 0;
        for (int i = 0; i < 256; ++i) perimeter += std::hypot(points[(i + 1) % 256].x() - points[i].x(), points[(i + 1) % 256].y() - points[i].y());
        route.points = points;
        route.lengthMeters = perimeter;
        return route;
    };
    const auto reference = circuit(0.0, 0.0, 0.0);
    QVERIFY(reference.lengthMeters > 3500 && reference.lengthMeters < 3800);
    // Same route, a wider line (8 m) sampled half a point out of step: the same circuit.
    QVERIFY(routesMatch(reference, circuit(8.0, 0.5, 0.0)));
    QVERIFY(routesMatch(circuit(-6.0, 0.25, 0.0), reference));
    // A 40 m detour over a quarter of the lap (a pit lane, another layout) is a different route.
    QVERIFY(!routesMatch(reference, circuit(0.0, 0.5, 40.0)));
    // The same shape driven the other way is never matched.
    auto reversed = circuit(0.0, 0.0, 0.0);
    reversed.direction = QStringLiteral("clockwise");
    QVERIFY(!routesMatch(reference, reversed));
}

void TelemetryCoreTests::flagsALapThatLeavesTheOtherLapsLine()
{
    // KAN-137: laps on a 200 m-radius circle driven on slightly different
    // lines (up to 5 m apart) stay on route; a lap that runs 17 m wide for
    // about 60 m (off track) leaves every other lap's line and is flagged.
    const auto lap = [](const int number, const double offset, const double excursion) {
        LapTrace trace;
        trace.lapNumber = number;
        for (int i = 0; i < 600; ++i) {
            const double angle = 2.0 * std::numbers::pi * i / 600.0;
            const bool wide = excursion > 0 && angle > 1.0 && angle < 1.3;
            const double radius = 200.0 + offset + (wide ? excursion : 0.0);
            trace.points.append({i * 0.1, radius * std::cos(angle), radius * std::sin(angle)});
        }
        return trace;
    };
    const QVector<LapTrace> traces{lap(1, 0.0, 0.0), lap(2, 3.0, 0.0), lap(3, -3.0, 0.0), lap(4, 5.0, 0.0), lap(5, 1.0, 17.0)};
    const auto deviations = lapLineDeviations(traces, {1, 2, 3, 4, 5});
    QCOMPARE(deviations.size(), 5);
    for (const int number : {1, 2, 3, 4}) QVERIFY2(deviations.value(number) <= maximumLineDeviationMeters,
        qPrintable(QString("lap %1: %2 m").arg(number).arg(deviations.value(number))));
    QVERIFY(deviations.value(5) > maximumLineDeviationMeters);
    // Only the given laps count, and fewer than three give no verdict.
    QVERIFY(lapLineDeviations(traces, {1, 5}).isEmpty());
    QVERIFY(!lapLineDeviations(traces, {1, 2, 5}).isEmpty());
    QVERIFY(lapLineDeviations({}, {}).isEmpty());
    QVERIFY_THROWS_EXCEPTION(OperationCancelled, static_cast<void>(lapLineDeviations(traces, {1, 2, 3, 4, 5}, [] { return true; })));
}

void TelemetryCoreTests::infersRoutesFromOrderedCompleteLaps()
{
    const auto derive = [](const QByteArray &bytes) { return deriveSourceLapSession(VboParser::parse(QString::fromUtf8(bytes))); };
    const auto laps = derive(EventProjectFixture::routeVbo());
    const auto base = inferTrack(laps); QVERIFY2(base.supported(), qPrintable(base.reason));
    QCOMPARE(base.route.direction, QString("counterclockwise")); QVERIFY(base.matchingLaps.size() >= 2);
    const auto noisy = inferTrack(derive(EventProjectFixture::routeVbo(130, -2, 2)));
    QVERIFY2(noisy.supported(), qPrintable(noisy.reason)); QVERIFY(routesMatch(base.route, noisy.route));
    const auto reverse = inferTrack(derive(EventProjectFixture::routeVbo(240, 1, 0, true)));
    QVERIFY(reverse.supported()); QCOMPARE(reverse.route.direction, QString("clockwise"));
    QVERIFY(!routesMatch(base.route, reverse.route));
    const auto alternative = inferTrack(derive(EventProjectFixture::routeVbo(240, -1, 0, false, 450)));
    QVERIFY(alternative.supported()); QVERIFY(!routesMatch(base.route, alternative.route));
    QVERIFY(!inferTrack(derive(EventProjectFixture::routeVbo(240, -1, 0, false, 300, 1))).supported());
    QVERIFY(!inferTrack(derive(EventProjectFixture::lapsVbo())).supported()); // Sparse evidence never invents a route.
    auto mixed = laps;
    const auto alternateLaps = derive(EventProjectFixture::routeVbo(240, -1, 0, false, 450));
    mixed.lapTraces = {laps.lapTraces[0], alternateLaps.lapTraces[0]};
    QVERIFY(!inferTrack(mixed).supported());
    mixed.lapTraces = {laps.lapTraces[0], laps.lapTraces[1], alternateLaps.lapTraces[2]};
    const auto pit = inferTrack(mixed); QVERIFY(pit.supported()); QVERIFY(!pit.matchingLaps.contains(alternateLaps.lapTraces[2].lapNumber));
    auto mirrored = laps;
    mirrored.selectedStartGate->endpointA.longitudeDegrees *= -1;
    mirrored.selectedStartGate->endpointB.longitudeDegrees *= -1;
    for (auto &trace : mirrored.lapTraces) for (auto &point : trace.points) point.eastMeters *= -1;
    QVERIFY(routesMatch(base.route, inferTrack(mirrored, true).route));
    QVERIFY_THROWS_EXCEPTION(OperationCancelled, static_cast<void>(inferTrack(laps, false, [] { return true; })));
    auto oversized = laps; oversized.lapTraces.resize(20'001);
    QVERIFY_THROWS_EXCEPTION(ResourceLimitError, static_cast<void>(inferTrack(oversized)));
    const QJsonObject config{{"layoutId", QJsonValue::Null}, {"direction", "unknown"}, {"gateRevision", "gates-v1:" + QString(64, 'a')}};
    const auto stableLayout = "gps-route-v1:" + QString(64, 'd');
    const QJsonObject provenance{{"algorithm", trackInferenceVersion}, {"sourceRevision", QString(64, 'b')},
        {"gateRevision", config.value("gateRevision")}, {"layoutId", stableLayout}, {"direction", "counterclockwise"}};
    QJsonArray sources{QJsonObject{{"runId", "a"}, {"trackConfiguration", config}, {"expectedRevision", QString(64, 'a')}},
        QJsonObject{{"runId", "b"}, {"trackConfiguration", config}, {"expectedRevision", QString(64, 'b')}, {"inference", provenance}}};
    const auto grouped = groupInferredTracks({{"a", base}, {"b", noisy}}, sources);
    QCOMPARE(grouped.configurations.value("a").value("layoutId").toString(), stableLayout);
    QCOMPARE(grouped.configurations.value("b").value("layoutId").toString(), stableLayout);
    const auto changed = groupInferredTracks({{"a", alternative}, {"b", noisy}}, sources);
    QCOMPARE(changed.configurations.value("b").value("layoutId").toString(), stableLayout);
    QVERIFY(changed.configurations.value("a").value("layoutId").toString() != stableLayout);
    sources.removeFirst();
    QCOMPARE(groupInferredTracks({{"b", noisy}}, sources).configurations.value("b").value("layoutId").toString(), stableLayout);
    auto stale = sources[0].toObject(); auto old = provenance; old.insert("algorithm", "old-version");
    stale.insert("inference", old); sources[0] = stale;
    QVERIFY(groupInferredTracks({{"b", noisy}}, sources).configurations.value("b").value("layoutId").toString() != stableLayout);
    // Spatial matching does not collapse distinct recorded timing definitions.
    auto timingConfig = config; timingConfig.insert("gateRevision", "gates-v1:" + QString(64, 'e'));
    stale.insert("trackConfiguration", timingConfig); sources.append(QJsonObject{{"runId", "a"}, {"trackConfiguration", config}});
    sources[0] = stale;
    const auto timingGroups = groupInferredTracks({{"a", base}, {"b", noisy}}, sources);
    QVERIFY(lapCompatibilityGroupId(timingGroups.configurations.value("a")) != lapCompatibilityGroupId(timingGroups.configurations.value("b")));
    auto middle = base, distant = base;
    for (auto &point : middle.route.points) point.rx() += 8;
    for (auto &point : distant.route.points) point.rx() += 16;
    QVERIFY(routesMatch(base.route, middle.route)); QVERIFY(routesMatch(middle.route, distant.route));
    QVERIFY(!routesMatch(base.route, distant.route));
    QJsonArray bridgeSources;
    for (const auto *id : {"a", "b", "c"}) bridgeSources.append(QJsonObject{{"runId", id}, {"trackConfiguration", config}});
    const auto bridge = groupInferredTracks({{"a", base}, {"b", middle}, {"c", distant}}, bridgeSources);
    QVERIFY(bridge.reasons.contains("b")); QVERIFY(bridge.configurations.value("b").value("layoutId").isNull());
    QJsonArray beforeReplacement;
    for (const auto *id : {"a", "b"}) beforeReplacement.append(QJsonObject{{"runId", id},
        {"trackConfiguration", config}, {"expectedRevision", QString(64, 'a')}});
    const auto before = groupInferredTracks({{"a", base}, {"b", noisy}}, beforeReplacement);
    for (qsizetype i = 0; i < beforeReplacement.size(); ++i) {
        auto source = beforeReplacement[i].toObject();
        source.insert("inference", before.provenance.value(source.value("runId").toString()));
        if (i == 0) source.insert("expectedRevision", QString(64, 'c'));
        beforeReplacement[i] = source;
    }
    const auto after = groupInferredTracks({{"a", alternative}, {"b", noisy}}, beforeReplacement);
    QCOMPARE(after.configurations.value("b"), before.configurations.value("b"));
    QVERIFY(lapCompatibilityGroupId(after.configurations.value("a"))
        != lapCompatibilityGroupId(after.configurations.value("b")));
    // Even conflicting persisted IDs cannot collapse two verified route shapes.
    auto forged = beforeReplacement[0].toObject();
    auto previous = forged.value("inference").toObject(); previous.insert("sourceRevision", QString(64, 'c'));
    forged.insert("inference", previous); beforeReplacement[0] = forged;
    const auto conflicting = groupInferredTracks({{"a", alternative}, {"b", noisy}}, beforeReplacement);
    QVERIFY(lapCompatibilityGroupId(conflicting.configurations.value("a"))
        != lapCompatibilityGroupId(conflicting.configurations.value("b")));
}

void TelemetryCoreTests::rejectsMalformedLapExclusions_data()
{
    QTest::addColumn<QString>("kind");
    for (const auto *kind : {"non-array", "duplicate", "blank", "long", "null-byte", "number-reason", "foreign-event", "out-section", "malformed-reference", "extra-field", "too-many"})
        QTest::newRow(kind) << QString(kind);
}

void TelemetryCoreTests::rejectsMalformedLapExclusions()
{
    QFETCH(QString, kind);
    auto project = EventProjectFixture::project(); auto event = project.value("event").toObject();
    OutingLapRow row; row.runId = "run-a"; row.type = LapSectionType::Lap; row.start = 1; row.end = 5;
    auto reference = makeLapReference(row, event.value("id").toString(), "run-a-source", QByteArray(64, 'a'), QByteArray(64, 'b'));
    QJsonObject entry{{"reference", reference}, {"reason", "Traffic"}};
    event.insert("lapExclusions", QJsonArray{entry}); project.insert("event", event);
    QString error; QVERIFY2(ProjectLimits::validateProject(project, &error), qPrintable(error));
    if (kind == "blank") entry.insert("reason", "  ");
    if (kind == "long") entry.insert("reason", QString(257, 'a'));
    if (kind == "null-byte") entry.insert("reason", QString("a") + QChar::Null);
    if (kind == "number-reason") entry.insert("reason", 42);
    if (kind == "foreign-event") reference.insert("eventId", "elsewhere");
    if (kind == "out-section") reference.insert("type", "OUT");
    if (kind == "malformed-reference") reference.remove("sourceRevision");
    entry.insert("reference", reference);
    if (kind == "extra-field") entry.insert("unknown", true);
    QJsonArray exclusions{entry};
    if (kind == "duplicate") exclusions.append(entry);
    if (kind == "too-many") for (int i = 0; i < maximumOutingLapRows; ++i) exclusions.append(entry);
    event.insert("lapExclusions", kind == "non-array" ? QJsonValue(QJsonObject{}) : QJsonValue(exclusions));
    project.insert("event", event);
    QVERIFY(!ProjectLimits::validateProject(project, &error));
}

void TelemetryCoreTests::groupsOnlyDatedUnambiguousAlternatives()
{
    TelemetryImportPlan plan;
    auto a = std::make_shared<TelemetrySession>(); a->duration = 600; a->metadata.insert("firstTimestampMilliseconds", "1780000000000");
    auto b = std::make_shared<TelemetrySession>(*a);
    TelemetryRunProposal vbo; vbo.id = "vbo"; vbo.format = "vbo"; vbo.telemetry = a;
    TelemetryRunProposal rcz; rcz.id = "rcz"; rcz.format = "rcz"; rcz.telemetry = b;
    plan.runs = {vbo, rcz}; plan.possibleSameRuns = {{"vbo", "rcz", 32, 1.0, 0.2, {}}};
    QCOMPARE(automaticVboPrimaries(plan).value("rcz"), QStringLiteral("vbo"));
    b->metadata.insert("firstTimestampMilliseconds", "1780086400000");
    QCOMPARE(automaticVboPrimaries(plan).value("rcz"), QStringLiteral("rcz"));
    b->metadata.clear();
    QCOMPARE(automaticVboPrimaries(plan).value("rcz"), QStringLiteral("rcz"));
    b->metadata = a->metadata;
    auto alternative = vbo; alternative.id = "other"; plan.runs.append(alternative);
    plan.possibleSameRuns.append({"other", "rcz", 32, 1.0, 0.2, {}});
    QCOMPARE(automaticVboPrimaries(plan).value("rcz"), QStringLiteral("rcz"));
}

void TelemetryCoreTests::prefersRaceChronoCalculatedAcceleration()
{
    const auto session = VboParser::parse(u"[column names]\ntime latacc longacc latacc-calc longacc-calc\n[data]\n"
        "0 0 0 0.5 -0.75\n1 0 0 invalid nan\n2 0 0 -0.25 0.125\n");
    QCOMPARE(session.valueAt("lateralAcceleration", 0).value(), 0.5);
    QCOMPARE(session.valueAt("longitudinalAcceleration", 0).value(), -0.75);
    QCOMPARE(session.valueAt("latacc", 0).value(), 0.0);
    QVERIFY(!session.valueAt("lateralAcceleration", 1));
    QVERIFY(!session.valueAt("longitudinalAcceleration", 1));
    QCOMPARE(session.valueAt("lateralAcceleration", 2).value(), -0.25);
    const auto calculatedOnly = VboParser::parse(u"[column names]\ntime latacc-calc longacc-calc\n[data]\n0 0.5 -0.75\n1 0.5 -0.75\n");
    QCOMPARE(calculatedOnly.valueAt("lateralAcceleration", 0).value(), 0.5);
    QCOMPARE(calculatedOnly.valueAt("longitudinalAcceleration", 0).value(), -0.75);
}

void TelemetryCoreTests::parsesRealisticFixture()
{
    const TelemetrySession session = VboParser::parseFile(TEST_FIXTURE_PATH);
    QCOMPARE(session.sampleCount, 3);
    QCOMPARE(session.duration, 1.0);
    QCOMPARE(session.metadata.value("vehicle"), QString("Test Car"));
    QVERIFY(session.channels.contains("mystery"));
    QCOMPARE(session.aliases.value("speed"), QString("velocity"));
    QCOMPARE(session.aliases.value("rpm"), QString("rpm"));
    QCOMPARE(session.aliases.value("throttle"), QString("throttle"));
    QCOMPARE(session.aliases.value("brake"), QString("brake"));
    QCOMPARE(session.aliases.value("heartRate"), QString("heart_rate"));
    QCOMPARE(session.channels.value("velocity").values, QVector<float>({0.0F, 50.0F, 100.0F}));
}

void TelemetryCoreTests::toleratesMalformedRows()
{
    const auto session = VboParser::parse(
        u"[column names]\ntime speed unknown\n[data]\n0   10  1\n1 bad\n2 30 3 extra");
    QCOMPARE(session.sampleCount, 3);
    QVERIFY(std::isnan(session.channels.value("speed").values[1]));
    QVERIFY(!session.valueAt("speed", 0.5));
    QVERIFY(!session.valueAt("speed", 1.0));
    QVERIFY(!session.valueAt("speed", 1.5));
    QCOMPARE(session.warnings.size(), 2);
}

void TelemetryCoreTests::neverBridgesALossOfSignal()
{
    // KAN-157: a 30 s loss of GPS fix (samples dropped, as the GoPro decoder
    // does) is no data in every interpolation mode; samples either side stay.
    TelemetrySession session;
    TelemetryChannel speed;
    speed.name = QStringLiteral("speed");
    for (int tick = 0; tick <= 600; ++tick) {
        if (tick > 200 && tick < 500) continue; // no fix from 20.0 s to 50.0 s
        speed.timestamps.append(tick / 10.0);
        speed.values.append(static_cast<float>(tick));
    }
    session.channels.insert(speed.name, speed);
    freezeCachedStatistics(session);
    for (const auto mode : {InterpolationMode::Linear, InterpolationMode::Nearest, InterpolationMode::Previous}) {
        QVERIFY(!session.valueAt(QStringLiteral("speed"), 20.05, mode));
        QVERIFY(!session.valueAt(QStringLiteral("speed"), 35.0, mode));
        QVERIFY(!session.valueAt(QStringLiteral("speed"), 49.95, mode));
    }
    QCOMPARE(session.valueAt(QStringLiteral("speed"), 20.0).value(), 200.0);
    QCOMPARE(session.valueAt(QStringLiteral("speed"), 50.0).value(), 500.0);
    QVERIFY(qAbs(session.valueAt(QStringLiteral("speed"), 10.05).value() - 100.5) < 1e-6);
    QCOMPARE(session.valueAt(QStringLiteral("speed"), 10.04, InterpolationMode::Previous).value(), 100.0);
    QCOMPARE(telemetryValueAt(session.channels.value(QStringLiteral("speed")), 35.0), std::nullopt);
}

void TelemetryCoreTests::syncIgnoresALossOfGpsFix()
{
    // KAN-157: auto-sync must not correlate the straight ramp it would draw
    // across a loss of fix. Dropped samples now count exactly like samples
    // explicitly marked as no data.
    const auto speedAt = [](const double time) {
        return static_cast<float>(50.0 + 18.0 * std::sin(time * 0.21) + 7.0 * std::sin(time * 0.73) + time * 0.08);
    };
    const auto session = [&](const bool lossOfFix, const bool markLoss) {
        TelemetrySession result;
        TelemetryChannel speed;
        speed.name = QStringLiteral("speed");
        for (int tick = 0; tick <= 600; ++tick) {
            const double time = tick * 0.2;
            const bool lost = lossOfFix && time > 40.0 && time < 70.0;
            if (lost && !markLoss) continue;
            speed.timestamps.append(time);
            speed.values.append(lost ? std::numeric_limits<float>::quiet_NaN() : speedAt(time));
        }
        result.channels.insert(speed.name, speed);
        result.aliases.insert(QStringLiteral("speed"), speed.name);
        return result;
    };
    const auto telemetry = session(false, false);
    const auto dropped = TelemetrySyncEngine::synchronize(session(true, false), telemetry);
    const auto marked = TelemetrySyncEngine::synchronize(session(true, true), telemetry);
    QVERIFY(qAbs(dropped.offset) < 0.05);
    QCOMPARE(dropped.diagnostics.validSamples, marked.diagnostics.validSamples);
    const auto continuous = TelemetrySyncEngine::synchronize(session(false, false), telemetry);
    QVERIFY(dropped.diagnostics.validSamples < continuous.diagnostics.validSamples - 250);
}

void TelemetryCoreTests::preservesRepeatedDataSections()
{
    const TelemetrySession session = VboParser::parse(
        u"[column names]\ntime speed\n[data]\n0 10\n[data]\n1 20");
    QCOMPARE(session.sampleCount, 2);
    QCOMPARE(session.channels.value("speed").values, QVector<float>({10.0F, 20.0F}));
}

void TelemetryCoreTests::rejectsMissingSections()
{
    QVERIFY_THROWS_EXCEPTION(VboParseError, (void) VboParser::parse(u"[header]\nfoo=bar"));
}

void TelemetryCoreTests::rejectsVboWithoutTimeColumn()
{
    // KAN-207: 6000 rows recorded at 10 Hz (10 minutes) under an exporter's own
    // time-column name must not load as 6000 seconds of 1 Hz data.
    QString rows;
    for (int row = 0; row < 6000; ++row)
        rows += QStringLiteral("%1 %2\n").arg(row / 10.0, 0, 'f', 1).arg(row % 200);
    QString message;
    try {
        (void) VboParser::parse(QStringLiteral("[column names]\nelapsed_sec speed\n[data]\n") + rows);
    } catch (const VboParseError &error) {
        message = QString::fromUtf8(error.what());
    }
    QVERIFY2(message.contains(QStringLiteral("no recognised time column")), qPrintable(message));
    QVERIFY_THROWS_EXCEPTION(VboParseError,
        (void) VboParser::parse(u"[column names]\nspeed rpm\n[data]\n10 100\n20 200"));

    // The recognised names still decode at their real cadence.
    for (const QString &name : {QStringLiteral("time"), QStringLiteral("Timestamp"), QStringLiteral("UTC time")}) {
        const auto session = VboParser::parse(QStringLiteral("[column names]\n%1 speed\n[data]\n")
                                                  .arg(QString(name).replace(' ', '_')) + rows);
        QCOMPARE(session.sampleCount, 6000);
        QVERIFY(qAbs(session.duration - 599.9) < 0.000001);
    }
}

void TelemetryCoreTests::interpolatesByTime()
{
    const auto session = VboParser::parse(u"[column names]\ntime speed\n[data]\n0 0\n1 10\n2 30");
    QCOMPARE(session.valueAt("speed", 0).value(), 0.0);
    QCOMPARE(session.valueAt("speed", 1).value(), 10.0);
    QCOMPARE(session.valueAt("speed", 2).value(), 30.0);
    QCOMPARE(session.valueAt("speed", 1.5).value(), 20.0);
    QCOMPARE(session.valueAt("speed", 1.6, InterpolationMode::Previous).value(), 10.0);
    QCOMPARE(session.valueAt("speed", 1.6, InterpolationMode::Nearest).value(), 30.0);
    QVERIFY(!session.valueAt("speed", -1));
    QVERIFY(!session.valueAt("speed", 9));
    QVERIFY(!session.valueAt("rpm", 1));
    QCOMPARE(videoToTelemetryTime(10, {2.5, 1.01}).value(), 12.6);
}

void TelemetryCoreTests::parsesTextFirstVboTimeFormats()
{
    const auto timestampsFor = [](QStringView rows) {
        return VboParser::parse(QStringLiteral("[column names]\ntime speed\n[data]\n")
                                    + rows.toString())
            .channels.value(QStringLiteral("speed")).timestamps;
    };
    const auto verifyTimes = [](const QVector<double> &actual, const QVector<double> &expected) {
        QCOMPARE(actual.size(), expected.size());
        for (qsizetype index = 0; index < expected.size(); ++index) {
            QVERIFY2(qAbs(actual[index] - expected[index]) < 0.000001,
                     qPrintable(QStringLiteral("timestamp %1: %2 != %3")
                                    .arg(index).arg(actual[index], 0, 'f', 6)
                                    .arg(expected[index], 0, 'f', 6)));
        }
    };

    verifyTimes(timestampsFor(u"00:00:00.000 1\n00:00:00.100 2\n00:00:00.200 3"),
                {0.0, 0.1, 0.2});
    verifyTimes(timestampsFor(u"003059.500 1\n003100.500 2\n003101.500 3"),
                {0.0, 1.0, 2.0});
    verifyTimes(timestampsFor(u"091428.380 1\n091428.480 2\n091428.580 3"),
                {0.0, 0.1, 0.2});
    verifyTimes(timestampsFor(u"0 1\n0.1 2\n0.2 3\n10.5 4"), {0.0, 0.1, 0.2, 10.5});
}

void TelemetryCoreTests::keepsVboTimestampsStrictlyMonotonic()
{
    const TelemetrySession midnight = VboParser::parse(
        u"[column names]\ntime speed rpm\n[data]\n"
        "235959.800 1 10\n235959.900 2 20\n000000.000 3 30\n000000.100 4 40");
    const TelemetryChannel midnightSpeed = midnight.channels.value(QStringLiteral("speed"));
    QCOMPARE(midnightSpeed.timestamps.size(), 4);
    QVERIFY(qAbs(midnightSpeed.timestamps[0]) < 0.000001);
    QVERIFY(qAbs(midnightSpeed.timestamps[1] - 0.1) < 0.000001);
    QVERIFY(qAbs(midnightSpeed.timestamps[2] - 0.2) < 0.000001);
    QVERIFY(qAbs(midnightSpeed.timestamps[3] - 0.3) < 0.000001);
    QCOMPARE(midnight.channels.value(QStringLiteral("rpm")).timestamps.size(), 4);
    QVERIFY(std::any_of(midnight.warnings.cbegin(), midnight.warnings.cend(), [](const QString &warning) {
        return warning.contains(QStringLiteral("midnight rollover"));
    }));

    const TelemetrySession guarded = VboParser::parse(
        u"[column names]\ntime speed rpm\n[data]\n"
        "120000.000 1 10\n120000.100 2 20\n120000.100 3 30\n115959.900 4 40\n"
        "126199 5 50\n246000 6 60\n12:61:00 7 70\n12:00:60 8 80\n120000.200 9 90");
    const TelemetryChannel speed = guarded.channels.value(QStringLiteral("speed"));
    QCOMPARE(speed.timestamps.size(), 3);
    QCOMPARE(speed.values, QVector<float>({1.0F, 2.0F, 9.0F}));
    QCOMPARE(guarded.channels.value(QStringLiteral("rpm")).values.size(), speed.timestamps.size());
    for (qsizetype index = 1; index < speed.timestamps.size(); ++index) {
        QVERIFY(speed.timestamps[index] > speed.timestamps[index - 1]);
    }
    QVERIFY(guarded.warnings.size() >= 6);
}

void TelemetryCoreTests::rejectsUnsafeVboDerivedTimes_data()
{
    QTest::addColumn<QString>("rows");
    QTest::newRow("positive-finite-extreme") << QStringLiteral("1e308 1\n1.1e308 2");
    QTest::newRow("negative-finite-extreme") << QStringLiteral("-1e308 1\n1e308 2");
    QTest::newRow("finite-elapsed-exceeds-microseconds")
        << QStringLiteral("-6000000000000 1\n6000000000000 2");
    QTest::newRow("finite-backward-difference-exceeds-range")
        << QStringLiteral("6000000000000 1\n-6000000000000 2");
    QTest::newRow("mixed-clock-extreme-relative")
        << QStringLiteral("23:59:59 1\n00:00:00 2\n1e308 3");
    QTest::newRow("elapsed-precision-collapse")
        << QStringLiteral("-1000000000000 1\n0 2\n0.000001 3");
    const double boundary = 0x1p63 / 1'000'000.0;
    QTest::newRow("rounded-up-int64-boundary")
        << QStringLiteral("0 1\n%1 2").arg(boundary, 0, 'g', 17);
}

void TelemetryCoreTests::rejectsUnsafeVboDerivedTimes()
{
    QFETCH(QString, rows);
    QVERIFY_THROWS_EXCEPTION(VboParseError,
        (void) VboParser::parse(QStringLiteral("[column names]\ntime speed\n[data]\n") + rows));
}

void TelemetryCoreTests::preservesMixedVboClocksAcrossMidnight()
{
    const auto session = VboParser::parse(
        u"[column names]\ntime speed rpm\n[data]\n"
        "23:59:59 10 100\n0 20 200\n00:00:00 30 300\n"
        "86401 40 400\n00:00:02 50 500\n00:00:02 60 600\n"
        "00:00:01 70 700\n00:00:03 80 800");
    QCOMPARE(session.duration, 4.0);
    QCOMPARE(session.sampleCount, 5);
    QCOMPARE(session.channels.value("speed").values, QVector<float>({10, 30, 40, 50, 80}));
    QCOMPARE(session.channels.value("rpm").values, QVector<float>({100, 300, 400, 500, 800}));
    for (const auto &channel : session.channels) {
        QCOMPARE(channel.timestamps, QVector<double>({0, 1, 2, 3, 4}));
        for (const double timestamp : channel.timestamps) QVERIFY(std::isfinite(timestamp));
    }
    QCOMPARE(session.warnings.size(), 4); // Backward, rollover, duplicate, backward.
}

void TelemetryCoreTests::rejectsOverflowingTelemetryChartRanges()
{
    const auto session = VboParser::parse(u"[column names]\ntime speed\n[data]\n0 1\n1 2");
    QVERIFY(session.sampledSegments("speed", -1e308, 1e308, 10).isEmpty());
    QVERIFY(session.sampledSegments("speed", 1e308, -1e308, 10).isEmpty());
    const auto segments = session.sampledSegments("speed", 0, 1, std::numeric_limits<int>::max());
    QCOMPARE(segments.size(), 1);
    QCOMPARE(segments[0], QVector<QPointF>({{0, 1}, {1, 2}}));
    const auto point = session.sampledSegments("speed", 1, 1, 10);
    QCOMPARE(point.size(), 1);
    QCOMPARE(point[0], QVector<QPointF>({{1, 2}}));
}

void TelemetryCoreTests::cancelsVboParsingDeterministically()
{
    QString text = QStringLiteral("[column names]\ntime speed\n[data]\n");
    text.reserve(200'000);
    for (int row = 0; row < 10'000; ++row) text += QStringLiteral("%1 %2\n").arg(row).arg(row % 200);
    int checks = 0;
    QVERIFY_THROWS_EXCEPTION(
        OperationCancelled,
        (void) VboParser::parse(text, [&checks] { return ++checks == 4; }));
    QVERIFY(checks >= 4);
}

void TelemetryCoreTests::cachesTelemetryChannelCadence()
{
    TelemetryChannel regular;
    regular.timestamps = {0.0, 0.1, 0.2, 0.3, 0.4};
    QCOMPARE(telemetryGapThreshold(regular, 0.0), 0.3);
    QCOMPARE(telemetryGapThreshold(regular, 0.75), 0.75);
    QCOMPARE(regular.cadenceStatisticComputationCount, qsizetype(1));

    TelemetryChannel sparse;
    sparse.timestamps = {0.0, 0.5, 2.0, 3.5};
    QCOMPARE(telemetryGapThreshold(sparse, 0.0), 4.5);
    QCOMPARE(sparse.cadenceStatisticComputationCount, qsizetype(1));

    TelemetryChannel guarded;
    guarded.timestamps = {0.0, 0.2, 0.2, std::numeric_limits<double>::quiet_NaN(), 0.8};
    QCOMPARE(telemetryGapThreshold(guarded, 0.4), 0.6);
    for (int lookup = 0; lookup < 10'000; ++lookup) {
        QCOMPARE(telemetryGapThreshold(guarded, 0.4), 0.6);
    }
    QCOMPARE(guarded.cadenceStatisticComputationCount, qsizetype(1));
}

void TelemetryCoreTests::enforcesVboResourceLimits()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QFile oversized(directory.filePath(QStringLiteral("oversized.vbo")));
    QVERIFY(oversized.open(QIODevice::WriteOnly));
    QVERIFY(oversized.resize(VboParser::kMaximumFileBytes + 1));
    oversized.close();
    QVERIFY_THROWS_EXCEPTION(ResourceLimitError, (void) VboParser::parseFile(oversized.fileName()));

    QStringList names;
    QStringList values;
    for (qsizetype column = 0; column <= VboParser::kMaximumColumns; ++column) {
        names.append(QStringLiteral("c%1").arg(column));
        values.append(QStringLiteral("1"));
    }
    QVERIFY_THROWS_EXCEPTION(
        ResourceLimitError,
        (void) VboParser::parse(QStringLiteral("[column names]\n%1\n[data]\n%2")
                                    .arg(names.join(' '), values.join(' '))));

    QString rows = QStringLiteral("[column names]\ntime speed\n[data]\n");
    rows.reserve(static_cast<qsizetype>(VboParser::kMaximumDataRows) * 4);
    for (qsizetype row = 0; row <= VboParser::kMaximumDataRows; ++row) rows += QStringLiteral("0 1\n");
    QVERIFY_THROWS_EXCEPTION(ResourceLimitError, (void) VboParser::parse(rows));

    const QString longField(VboParser::kMaximumFieldCharacters + 1, QLatin1Char('1'));
    QVERIFY_THROWS_EXCEPTION(
        ResourceLimitError,
        (void) VboParser::parse(
            QStringLiteral("[column names]\ntime speed\n[data]\n0 %1").arg(longField)));
}

namespace {

// Peak resident memory in MiB (macOS reports bytes, Linux kilobytes).
double peakResidentMiB()
{
#if defined(Q_OS_UNIX)
    rusage usage{};
    getrusage(RUSAGE_SELF, &usage);
#if defined(Q_OS_MACOS)
    return static_cast<double>(usage.ru_maxrss) / (1024.0 * 1024.0);
#else
    return static_cast<double>(usage.ru_maxrss) / 1024.0;
#endif
#else
    return 0.0;
#endif
}

QString writeVboFile(const QTemporaryDir &directory, const QString &name, const QByteArray &bytes)
{
    QFile file(directory.filePath(name));
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) return {};
    return file.fileName();
}

} // namespace

void TelemetryCoreTests::boundsVboHeaderAndDecodedValues()
{
    // KAN-147: untrusted VBO input is bounded before large allocation.
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const double before = peakResidentMiB();

    // 1. A long section name with many bare lines: each line became a
    // metadata key copying the whole name (81 KB grew to about 1 GB).
    QByteArray header = "[" + QByteArray(80'000, 'a') + "]\n";
    for (int line = 0; line < 1500; ++line) header += "x\n";
    header += "[column names]\ntime speed\n[data]\n0 1\n1 2\n";
    const QString longSection = writeVboFile(directory, "long-section.vbo", header);
    QVERIFY(!longSection.isEmpty());
    QVERIFY_THROWS_EXCEPTION(ResourceLimitError, (void) VboParser::parseFile(longSection));

    // Many short metadata lines are bounded too.
    QByteArray manyEntries = "[header]\n";
    for (int line = 0; line <= VboParser::kMaximumMetadataEntries; ++line) manyEntries += "x\n";
    manyEntries += "[column names]\ntime speed\n[data]\n0 1\n";
    const QString entries = writeVboFile(directory, "many-entries.vbo", manyEntries);
    QVERIFY_THROWS_EXCEPTION(ResourceLimitError, (void) VboParser::parseFile(entries));

    // 2. 512 columns and 400,000 one-value rows: rows x columns was not
    // budgeted (802 KB grew to 978 MB).
    QByteArray wide = "[column names]\n";
    wide += "time ";
    for (int column = 1; column < 512; ++column) wide += "c" + QByteArray::number(column) + ' ';
    wide += "\n[data]\n";
    wide.reserve(wide.size() + 400'000 * 2);
    for (int row = 0; row < 400'000; ++row) wide += "1\n";
    const QString wideFile = writeVboFile(directory, "wide.vbo", wide);
    QVERIFY_THROWS_EXCEPTION(ResourceLimitError, (void) VboParser::parseFile(wideFile));

    const double growth = peakResidentMiB() - before;
    qInfo().noquote() << QString("peak resident growth %1 MiB").arg(growth, 0, 'f', 1);
    QVERIFY2(growth < 300.0, qPrintable(QString("peak memory grew by %1 MiB").arg(growth)));
}

void TelemetryCoreTests::buildsVboMetadataInFileOrder()
{
    // KAN-182: metadata follows the file's section order, not QHash's
    // per-process seed. Generated keys number lines by the metadata size at
    // insertion, and a key repeated in a later section replaces the earlier one.
    QString text = QStringLiteral("free line\n[zeta]\nshared = from zeta\nzeta line\n");
    for (int index = 0; index < 24; ++index)
        text += QStringLiteral("[section%1]\nline %1\n").arg(index);
    text += QStringLiteral("[alpha]\nshared = from alpha\nalpha line\n"
                           "[column names]\ntime lat long\n[data]\n000000.00 0 0\n000000.10 0 0\n");
    const auto session = VboParser::parse(text);
    QCOMPARE(session.metadata.value(".0"), QString("free line"));
    QCOMPARE(session.metadata.value("zeta.2"), QString("zeta line"));
    for (int index = 0; index < 24; ++index)
        QCOMPARE(session.metadata.value(QStringLiteral("section%1.%2").arg(index).arg(index + 3)),
                 QStringLiteral("line %1").arg(index));
    QCOMPARE(session.metadata.value("shared"), QString("from alpha"));
    QCOMPARE(session.metadata.value("alpha.27"), QString("alpha line"));
}

void TelemetryCoreTests::normalizesVboHeaderEdgeCases()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    // Generated duplicate names never overwrite a column, and an empty name
    // becomes "column N" (so it cannot match a tyre corner or anything else).
    const QString duplicates = writeVboFile(directory, "duplicates.vbo",
        "[column names]\ntime,a,a,a (2),,b\n[data]\n0,1,2,3,4,5\n1,1,2,3,4,5\n");
    const auto session = VboParser::parseFile(duplicates);
    QStringList names = session.channelNames();
    names.sort();
    QCOMPARE(names.size(), 5);
    QCOMPARE(session.channels.value("a").values.first(), 1.0F);
    QCOMPARE(session.channels.value("a (2)").values.first(), 3.0F); // the header's own "a (2)"
    QVERIFY(session.channels.contains("a (3)"));
    QCOMPARE(session.channels.value("a (3)").values.first(), 2.0F);
    QCOMPARE(session.channels.value("column 5").values.first(), 4.0F);
    QCOMPARE(session.channels.value("b").values.first(), 5.0F);
    for (const auto &name : names) QVERIFY2(!name.isEmpty(), "an empty column name");

    // More than one data or column-names section is ambiguous: rejected.
    const QString twoData = writeVboFile(directory, "two-data.vbo",
        "[column names]\ntime speed\n[data]\n0 1\n[data2]\n0 9\n");
    QVERIFY_THROWS_EXCEPTION(VboParseError, (void) VboParser::parseFile(twoData));
    const QString twoColumns = writeVboFile(directory, "two-columns.vbo",
        "[column names]\ntime speed\n[column names 2]\ntime rpm\n[data]\n0 1\n");
    QVERIFY_THROWS_EXCEPTION(VboParseError, (void) VboParser::parseFile(twoColumns));

    // A file is read exactly as parse() reads its text: CRLF and lone CR alike.
    const QByteArray mixed = "[column names]\r\ntime speed\r\n[data]\r\n0 1\r\n1 2\r3 4\n2 5\n";
    const QString mixedFile = writeVboFile(directory, "mixed.vbo", mixed);
    const auto fromFile = VboParser::parseFile(mixedFile);
    const auto fromText = VboParser::parse(QString::fromUtf8(mixed));
    QCOMPARE(fromFile.channels.value("speed").values, fromText.channels.value("speed").values);
    QCOMPARE(fromFile.channels.value("speed").timestamps, fromText.channels.value("speed").timestamps);

    // A value beyond float range is no data, never infinity.
    const QString huge = writeVboFile(directory, "huge.vbo",
        "[column names]\ntime speed\n[data]\n0 1e39\n1 2\n2 -1e40\n");
    const auto hugeSession = VboParser::parseFile(huge);
    const auto &values = hugeSession.channels.value("speed").values;
    QCOMPARE(values.size(), qsizetype(3));
    QVERIFY(std::isnan(values[0]) && std::isnan(values[2]));
    QCOMPARE(values[1], 2.0F);
}

void TelemetryCoreTests::preservesVboScannerFormats()
{
    const auto comma = VboParser::parse(
        u"\ufeff# ignored\r\n[column names]\r\ntime, 'speed',\r\n"
        "rpm, sensor\u00a0name\r\n[data]\r\n0, 10, , 7, extra,\r\n"
        "1, 20, 200, 8\r\n2, 30\r\n");
    QCOMPARE(comma.sampleCount, 3);
    QCOMPARE(comma.channels.value("speed").values, QVector<float>({10, 20, 30}));
    QCOMPARE(comma.channels.value("speed").timestamps, QVector<double>({0, 1, 2}));
    QVERIFY(std::isnan(comma.channels.value("rpm").values[0]));
    QCOMPARE(comma.channels.value("rpm").values[1], 200.0F);
    QVERIFY(std::isnan(comma.channels.value("rpm").values[2]));
    QCOMPARE(comma.channels.value(QStringLiteral("sensor\u00a0name")).values[0], 7.0F);
    QCOMPARE(comma.warnings, QStringList({"Row 1: ignored 2 extra value(s).",
                                        "Row 3: missing 2 value(s)."}));

    const auto whitespace = VboParser::parse(
        u"[column names]\ntime\tspeed\nrpm\vsensor\u00a0name\n[data]\n"
        "0\t10\v100\f7\n1 20\r200 8");
    QCOMPARE(whitespace.sampleCount, 2);
    QCOMPARE(whitespace.channels.value("rpm").values, QVector<float>({100, 200}));
    QCOMPARE(whitespace.channels.value(QStringLiteral("sensor\u00a0name")).values,
             QVector<float>({7, 8}));
    QVERIFY(whitespace.warnings.isEmpty());
}

void TelemetryCoreTests::boundsSeparatorHeavyVboRows()
{
    const QString separators(200'000, QLatin1Char(','));
    const QString prefix = QStringLiteral("[column names]\ntime,speed\n[data]\n0,42");
    const auto parsed = VboParser::parse(prefix + separators);
    QCOMPARE(parsed.sampleCount, 1);
    QCOMPARE(parsed.channels.value("speed").values, QVector<float>({42}));
    QCOMPARE(parsed.warnings, QStringList({"Row 1: ignored 200000 extra value(s)."}));
    // Ignored values still have to respect the field limit.
    QVERIFY_THROWS_EXCEPTION(ResourceLimitError,
        (void) VboParser::parse(prefix + separators
            + QString(VboParser::kMaximumFieldCharacters + 1, QLatin1Char('x'))));
    QVERIFY_THROWS_EXCEPTION(ResourceLimitError,
        (void) VboParser::parse(QStringLiteral("[column names]\n") + separators
            + QStringLiteral("\n[data]\n0")));
}

void TelemetryCoreTests::enforcesVboScannerBoundaries()
{
    const QString valid = QStringLiteral("[column names]\ntime speed\n[data]\n0 42");
    const QString comment = QStringLiteral("#")
        + QString(VboParser::kMaximumLineCharacters - 1, QLatin1Char('x'));
    QCOMPARE(VboParser::parse(comment + QStringLiteral("\r\n") + valid).sampleCount, 1);
    QVERIFY_THROWS_EXCEPTION(ResourceLimitError,
        (void) VboParser::parse(comment + QStringLiteral("x\n") + valid));
    // A terminal CR is content, unlike CR in CRLF.
    QVERIFY_THROWS_EXCEPTION(ResourceLimitError,
        (void) VboParser::parse(valid + QStringLiteral("\n") + comment + QLatin1Char('\r')));

    const QString emptyLines(VboParser::kMaximumLines - 4, QLatin1Char('\n'));
    QCOMPARE(VboParser::parse(emptyLines + valid).sampleCount, 1);
    QVERIFY_THROWS_EXCEPTION(ResourceLimitError,
        (void) VboParser::parse(emptyLines + valid + QLatin1Char('\n')));

    QStringList names{"time"};
    QStringList values{"0"};
    for (qsizetype index = 1; index < VboParser::kMaximumColumns; ++index) {
        names.append(QStringLiteral("c%1").arg(index));
        values.append("1");
    }
    const QString wide = QStringLiteral("[column names]\n%1\n[data]\n%2")
        .arg(names.join(' '), values.join(' '));
    QCOMPARE(VboParser::parse(wide).channels.size(), VboParser::kMaximumColumns - 1);

    const QString field(VboParser::kMaximumFieldCharacters, QLatin1Char('x'));
    const QString header = QStringLiteral("[column names]\ntime,speed,%1\n[data]\n0,42,1");
    QVERIFY(VboParser::parse(header.arg(field)).channels.contains(field));
    QVERIFY_THROWS_EXCEPTION(ResourceLimitError,
        (void) VboParser::parse(header.arg(field + QLatin1Char('x'))));
    // Trimming must not turn harmless padding into an oversized field.
    const QString padding(VboParser::kMaximumFieldCharacters + 1, QChar(0x00a0));
    const QString padded = QStringLiteral("[column names]\ntime,speed\n[data]\n0,")
        + padding + QStringLiteral("42") + padding + QLatin1Char(',');
    QCOMPARE(VboParser::parse(padded).channels.value("speed").values[0], 42.0F);
    QVERIFY_THROWS_EXCEPTION(ResourceLimitError,
        (void) VboParser::parse(QStringLiteral("[column names]\ntime,speed\n[data]\n0,4")
            + padding + QStringLiteral("2")));
    // A logical comma field can span header lines; no unbounded join is needed.
    const QString half(VboParser::kMaximumFieldCharacters / 2, QLatin1Char('x'));
    QVERIFY_THROWS_EXCEPTION(ResourceLimitError,
        (void) VboParser::parse(QStringLiteral("[column names]\ntime,") + half
            + QLatin1Char('\n') + half + QStringLiteral("\n[data]\n0,1")));
}

void TelemetryCoreTests::cancelsVboScanningBeforeLimitFailures_data()
{
    QTest::addColumn<QString>("source");
    QTest::newRow("too-many-lines") << QString(VboParser::kMaximumLines, QLatin1Char('\n'));
    QTest::newRow("oversized-line")
        << QString(VboParser::kMaximumLineCharacters + 1, QLatin1Char('x'));
    QTest::newRow("oversized-field")
        << (QStringLiteral("[column names]\ntime,speed\n[data]\n0,")
            + QString(VboParser::kMaximumFieldCharacters + 1, QLatin1Char('x')));
    QTest::newRow("too-many-comma-columns")
        << (QStringLiteral("[column names]\n") + QString(200'000, QLatin1Char(','))
            + QStringLiteral("\n[data]\n0"));
}

void TelemetryCoreTests::cancelsVboScanningBeforeLimitFailures()
{
    QFETCH(QString, source);
    int checks = 0;
    QVERIFY_THROWS_EXCEPTION(OperationCancelled,
        (void) VboParser::parse(source, [&checks] { return ++checks == 4; }));
    QCOMPARE(checks, 4);
}

void TelemetryCoreTests::cancelsVboFieldScanningAtEveryCheckpoint()
{
    const QString source = QStringLiteral("[column names]\ntime,\nspeed\n[data]\n0,42")
        + QString(16'384, QLatin1Char(','));
    int totalChecks = 0;
    QCOMPARE(VboParser::parse(source, [&] { ++totalChecks; return false; }).sampleCount, 1);
    // Exercise cancellation throughout a successful parse, including scanning
    // discarded fields. Do not assume a particular number or ordering of polls.
    for (int stopAt = 1; stopAt <= totalChecks; ++stopAt) {
        int checks = 0;
        QVERIFY_THROWS_EXCEPTION(OperationCancelled,
            (void) VboParser::parse(source, [&] { return ++checks == stopAt; }));
        QCOMPARE(checks, stopAt);
    }
}

void TelemetryCoreTests::convertsArcMinuteCoordinates()
{
    const auto session = VboParser::parse(
        u"[header]\ncoordinate units = arc-minutes\n[column names]\ntime latitude longitude\n[data]\n0 3120 -1260\n1 3126 -1266");
    QCOMPARE(session.channels.value("latitude").values[0], 52.0F);
    QCOMPARE(session.channels.value("longitude").values[0], -21.0F);
}

void TelemetryCoreTests::resolvesCoordinateEvidence_data()
{
    QTest::addColumn<int>("format");
    QTest::addColumn<double>("latitude");
    QTest::addColumn<double>("longitude");
    for (int format = 0; format < 3; ++format) {
        for (const double lat : {-0.25, 0.25}) {
            for (const double lon : {-0.25, 0.25}) {
                const auto label = QString("format-%1-lat-%2-lon-%3").arg(format).arg(lat).arg(lon).toUtf8();
                QTest::newRow(label.constData()) << format << lat << lon;
            }
        }
        QTest::newRow(qPrintable(QString("format-%1-cross-both-zeroes").arg(format)))
            << format << -0.00009 << -0.00009;
        QTest::newRow(qPrintable(QString("format-%1-only-one-axis-large").arg(format)))
            << format << 52.0 << 0.25;
    }
}

void TelemetryCoreTests::resolvesCoordinateEvidence()
{
    QFETCH(int, format);
    QFETCH(double, latitude);
    QFETCH(double, longitude);
    const double multiplier = format == 0 ? 1.0 : 60.0;
    const auto number = [multiplier](double degrees) {
        return QString::number(degrees * multiplier, 'f', 10);
    };
    const QString prefix = format == 2
        ? "[comments]\nGenerated by RaceChrono Pro v10.2.4\n"
        : QString("[HEADER]\nCoordinate Units : %1\ncoordinate units = %1\n")
            .arg(format == 0 ? "Degrees" : "Arc-Minutes");
    const QString text = prefix
        + QString("[laptiming]\nStart %1 %2 %1 %3 synthetic\n"
                  "[column names]\ntime lat long speed\n[data]\n0 %2 %1 72\n1 %3 %4 73\n")
            .arg(number(longitude), number(latitude), number(latitude + .00018), number(longitude + .00018));
    const auto session = VboParser::parse(text);
    QVERIFY(session.warnings.isEmpty());
    QVERIFY(std::abs(session.valueAt("latitude", 0).value() - latitude) < 2e-6);
    QVERIFY(std::abs(session.valueAt("longitude", 0).value() - longitude) < 2e-6);
    QCOMPARE(session.metadata.value("gpsCoordinateUnit"), format == 0 ? QString("degrees") : QString("arc-minutes"));
    QCOMPARE(session.metadata.value("gpsCoordinateEvidence"), format == 2
        ? QString("racechrono-pro-10.2.4") : QString("header-coordinate-units"));
    const auto geometry = buildTrackGeometry(session);
    QVERIFY(geometry.valid);
    QVERIFY(std::abs(geometry.originLatitude - latitude) < 2e-6);
    QVERIFY(std::abs(geometry.originLongitude - longitude) < 2e-6);
    QCOMPARE(currentTrackPoint(session, 0, geometry).value(), geometry.points.first());
    QCOMPARE(session.timingGates.size(), 1);
    const auto &gate = session.timingGates.first();
    if (format == 2) {
        QVERIFY(std::abs((gate.endpointA.latitudeDegrees + gate.endpointB.latitudeDegrees) / 2 - latitude) < 1e-10);
        QVERIFY(std::abs((gate.endpointA.longitudeDegrees + gate.endpointB.longitudeDegrees) / 2 - longitude) < 1e-10);
    } else {
        QVERIFY(std::abs(gate.endpointA.latitudeDegrees - latitude) < 1e-10);
        QVERIFY(std::abs(gate.endpointA.longitudeDegrees - longitude) < 1e-10);
        QVERIFY(std::abs(gate.endpointB.latitudeDegrees - latitude - .00018) < 1e-10);
    }
    const auto width = projectCoordinate(gate.endpointB, gate.endpointA);
    QVERIFY(std::abs(std::hypot(width.eastMeters, width.northMeters) - 20.0151) < .01);
}

void TelemetryCoreTests::withholdsUnresolvedCoordinates_data()
{
    QTest::addColumn<QString>("prefix");
    QTest::newRow("missing") << "";
    QTest::newRow("unknown-exporter") << "[comments]\nGenerated by Unknown v1\n";
    QTest::newRow("unverified-racechrono") << "[comments]\nGenerated by RaceChrono Pro v99.0\n";
    QTest::newRow("unsupported-unit") << "[header]\ncoordinate units = radians\n";
    QTest::newRow("empty-unit") << "[header]\ncoordinate units =\n";
    QTest::newRow("conflicting-units") << "[header]\ncoordinate units = degrees\ncoordinate units = arc-minutes\n";
    QTest::newRow("invalid-then-valid") << "[header]\ncoordinate units = radians\ncoordinate units = degrees\n";
    QTest::newRow("exporter-unit-conflict") << "[comments]\nGenerated by RaceChrono Pro v10.2.4\n[header]\ncoordinate units = degrees\n";
    QTest::newRow("conflicting-exporters") << "[comments]\nGenerated by RaceChrono Pro v10.2.4\nGenerated by Unknown v1\n[header]\ncoordinate units = arc-minutes\n";
    QTest::newRow("reverse-exporter-conflict") << "[comments]\nGenerated by Unknown v1\nGenerated by RaceChrono Pro v10.2.4\n";
    QTest::newRow("spoofed-derived-metadata") << "[header]\ngpsCoordinateUnit = degrees\ngpsCoordinateEvidence = racechrono-pro-10.2.4\ntimingGateFormat = fake\ngpsLongitudeConvention = west-positive\n";
}

void TelemetryCoreTests::withholdsUnresolvedCoordinates()
{
    QFETCH(QString, prefix);
    const auto session = VboParser::parse(prefix
        + "[laptiming]\nStart 15 15 15 15.01 ambiguous\n"
          "[column names]\ntime latitude longitude speed\n[data]\n0 15 15 72\n1 3120 1260 73\n");
    QCOMPARE(session.sampleCount, 2);
    QCOMPARE(session.valueAt("speed", 0).value(), 72.0);
    QCOMPARE(session.valueAt("speed", 1).value(), 73.0);
    QVERIFY(!session.valueAt("latitude", 0));
    QVERIFY(!session.valueAt("longitude", 1));
    QVERIFY(!buildTrackGeometry(session).valid);
    QVERIFY(session.timingGates.isEmpty());
    QCOMPARE(session.metadata.value("gpsCoordinateUnit"), QString("unresolved"));
    QCOMPARE(session.metadata.value("gpsCoordinateEvidence"), QString("unresolved"));
    QVERIFY(!session.metadata.contains("timingGateFormat"));
    QVERIFY(!session.metadata.contains("gpsLongitudeConvention"));
    QVERIFY(session.warnings.join(' ').contains("GPS coordinates and timing gates unavailable"));
}

void TelemetryCoreTests::validatesDeclaredCoordinateBounds()
{
    for (const auto unit : {CoordinateUnit::Degrees, CoordinateUnit::ArcMinutes}) {
        const double multiplier = unit == CoordinateUnit::Degrees ? 1.0 : 60.0;
        for (const double sign : {-1.0, 1.0}) {
            QCOMPARE(normalizeCoordinateDegrees(CoordinateAxis::Latitude, sign * 90 * multiplier, unit).value(), sign * 90);
            QCOMPARE(normalizeCoordinateDegrees(CoordinateAxis::Longitude, sign * 180 * multiplier, unit).value(), sign * 180);
            QVERIFY(!normalizeCoordinateDegrees(CoordinateAxis::Latitude, sign * 91 * multiplier, unit));
            QVERIFY(!normalizeCoordinateDegrees(CoordinateAxis::Longitude, sign * 181 * multiplier, unit));
        }
        QVERIFY(!normalizeCoordinateDegrees(CoordinateAxis::Latitude, std::numeric_limits<double>::quiet_NaN(), unit));
        QVERIFY(!normalizeCoordinateDegrees(CoordinateAxis::Longitude, std::numeric_limits<double>::infinity(), unit));
    }
    const auto session = VboParser::parse(
        u"[header]\ncoordinate units = degrees\n[laptiming]\nStart 15 91 15 89 invalid\n"
        "[column names]\ntime latitude longitude speed\n[data]\n0 0 0 72\n1 91 181 73\n");
    QVERIFY(!session.valueAt("latitude", 1));
    QVERIFY(!session.valueAt("longitude", 1));
    QVERIFY(session.timingGates.isEmpty());
    const auto geometry = buildTrackGeometry(session);
    QVERIFY(geometry.valid);
    QVERIFY(!currentTrackPoint(session, 1, geometry));
    auto arbitrary = session;
    arbitrary.channels["latitude"].values[1] = 91;
    arbitrary.channels["longitude"].values[1] = 181;
    QVERIFY(!currentTrackPoint(arbitrary, 1, geometry));
}

void TelemetryCoreTests::parsesBoundedRaceChronoTimingGates()
{
    QString source = QStringLiteral(
        "[header]\ncoordinate units = degrees\n[laptiming]\n"
        "Start 21.0000 52.0000 21.0000 52.0002 main straight\n"
        "Split 21.1000 52.1000 21.1000 52.1002 sector one\n"
        "Start malformed gate\n"
        "[column names]\n"
        "time latitude longitude\n"
        "[data]\n"
        "0 52.0 21.0\n"
        "1 52.0001 21.0001\n");
    const TelemetrySession parsed = VboParser::parse(source);
    QCOMPARE(parsed.timingGates.size(), qsizetype(2));
    QCOMPARE(parsed.timingGates[0].type, TimingGateType::Start);
    QCOMPARE(parsed.timingGates[0].sourceDescription, QStringLiteral("main straight"));
    QCOMPARE(parsed.timingGates[1].type, TimingGateType::Split);
    QVERIFY(std::any_of(parsed.warnings.cbegin(), parsed.warnings.cend(), [](const QString &warning) {
        return warning.contains(QStringLiteral("Timing line 3 ignored"));
    }));

    QString bounded = QStringLiteral("[header]\ncoordinate units = degrees\n[laptiming]\n");
    for (int index = 0; index < 129; ++index) {
        bounded += QStringLiteral("Start 21.0000 52.0000 21.0000 52.0002 gate %1\n").arg(index);
    }
    bounded += QStringLiteral("[column names]\ntime latitude longitude\n[data]\n0 52.0 21.0\n");
    const TelemetrySession limited = VboParser::parse(bounded);
    QCOMPARE(limited.timingGates.size(), qsizetype(128));
    QVERIFY(std::any_of(limited.warnings.cbegin(), limited.warnings.cend(), [](const QString &warning) {
        return warning.contains(QStringLiteral("supported limit of 128"));
    }));
}

void TelemetryCoreTests::derivesDirectionalPassesAndCompleteLaps()
{
    const TimingGate startGate{TimingGateType::Start, QStringLiteral("Start"),
                               {52.0, 21.0}, {52.0002, 21.0}, {}};
    const auto sessionFor = [](const QVector<double> &times,
                               const QVector<float> &latitudes,
                               const QVector<float> &longitudes) {
        TelemetrySession session;
        TelemetryChannel latitude;
        latitude.name = QStringLiteral("latitude");
        latitude.timestamps = times;
        latitude.values = latitudes;
        TelemetryChannel longitude;
        longitude.name = QStringLiteral("longitude");
        longitude.timestamps = times;
        longitude.values = longitudes;
        session.channels.insert(latitude.name, latitude);
        session.channels.insert(longitude.name, longitude);
        session.aliases.insert(latitude.name, latitude.name);
        session.aliases.insert(longitude.name, longitude.name);
        session.duration = times.constLast();
        session.sampleCount = times.size();
        return session;
    };
    constexpr float midLatitude = 52.0001F;
    constexpr float northLatitude = 52.0008F;
    constexpr float eastLongitude = 21.0002F;
    constexpr float westLongitude = 20.9998F;
    const TelemetrySession laps = sessionFor(
        {0.0, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 10.0, 11.0,
         12.0, 13.0, 14.0, 15.0},
        {midLatitude, midLatitude, midLatitude, northLatitude, northLatitude,
         midLatitude, midLatitude, northLatitude, northLatitude, midLatitude,
         midLatitude, northLatitude, northLatitude, midLatitude, midLatitude},
        {eastLongitude, eastLongitude, westLongitude, westLongitude, eastLongitude,
         eastLongitude, westLongitude, westLongitude, eastLongitude, eastLongitude,
         westLongitude, westLongitude, eastLongitude, eastLongitude, westLongitude});
    const LapSession detected = detectLaps(laps, startGate);
    QCOMPARE(detected.status, LapSessionStatus::Available);
    QCOMPARE(detected.acceptedPasses.size(), qsizetype(4));
    QCOMPARE(detected.timedLaps.size(), qsizetype(3));
    QVERIFY(qAbs(detected.timedLaps[0].durationSeconds - 4.0) < 0.001);
    QVERIFY(qAbs(detected.timedLaps[1].durationSeconds - 5.0) < 0.001);
    QVERIFY(qAbs(detected.timedLaps[2].durationSeconds - 4.0) < 0.001);
    QCOMPARE(detected.fastestLapIndex, std::optional<qsizetype>(0));
    QVERIFY(qAbs(detected.timedLaps[1].deltaToBestSeconds - 1.0) < 0.001);

    const TelemetrySession reverseCrossing = sessionFor(
        {0.0, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0},
        {midLatitude, midLatitude, midLatitude, northLatitude, midLatitude,
         midLatitude, northLatitude},
        {eastLongitude, eastLongitude, westLongitude, westLongitude, westLongitude,
         eastLongitude, eastLongitude});
    const LapSession directional = detectLaps(reverseCrossing, startGate);
    QCOMPARE(directional.acceptedPasses.size(), qsizetype(1));
    QCOMPARE(directional.diagnostics.rejectedOppositeDirectionClusters, qsizetype(1));
}

void TelemetryCoreTests::finalizesGatePassWhenTelemetryEndsInsideCorridor()
{
    TelemetrySession session;
    const auto addCoordinateChannel = [&session](
                                          const QString &name, const QVector<float> &values) {
        TelemetryChannel channel;
        channel.name = name;
        channel.timestamps = {0.0, 1.0, 2.0};
        channel.values = values;
        session.channels.insert(name, channel);
        session.aliases.insert(name, name);
    };
    addCoordinateChannel(QStringLiteral("latitude"), {52.0001F, 52.0001F, 52.0001F});
    addCoordinateChannel(QStringLiteral("longitude"), {21.0002F, 21.0002F, 21.0F});
    session.duration = 2.0;

    TimingGate startGate;
    startGate.type = TimingGateType::Start;
    startGate.endpointA = {52.0, 21.0};
    startGate.endpointB = {52.0002, 21.0};

    const LapSession result = detectLaps(session, startGate);
    QCOMPARE(result.status, LapSessionStatus::InsufficientPasses);
    QCOMPARE(result.acceptedPasses.size(), qsizetype(1));
    QVERIFY(qAbs(result.acceptedPasses.constFirst().telemetryTime - 2.0) < 0.001);
}

// KAN-205: port of Telemetry FET-198's crossing truth tests (lap_crossing_test.dart).
void TelemetryCoreTests::acceptsOnlyRealCrossingsOfTheStartLine()
{
    // The gate runs north-south through the origin, about 22 m long: in metres
    // around the origin it spans north -11.1 ... +11.1 at east 0.
    const GeoCoordinate origin{52.0001, 21.0};
    const TimingGate gate{TimingGateType::Start, QStringLiteral("Start"),
                          {52.0, 21.0}, {52.0002, 21.0}, {}};
    struct Path {
        double step = 0.2;
        QVector<double> times;
        QVector<double> east;
        QVector<double> north;
        Path &points(std::initializer_list<std::pair<double, double>> list)
        {
            for (const auto &[e, n] : list) {
                times.append(times.isEmpty() ? 0.0 : times.constLast() + step);
                east.append(e);
                north.append(n);
            }
            return *this;
        }
        Path &travel(std::initializer_list<std::pair<double, double>> waypoints,
                     const double maximumStepMeters = 8.0)
        {
            for (const auto &[e, n] : waypoints) {
                const double fromE = east.constLast();
                const double fromN = north.constLast();
                const double distance = std::hypot(e - fromE, n - fromN);
                const int steps = std::max(1, int(std::ceil(distance / maximumStepMeters)));
                for (int index = 1; index <= steps; ++index) {
                    const double fraction = double(index) / steps;
                    points({{fromE + (e - fromE) * fraction, fromN + (n - fromN) * fraction}});
                }
            }
            return *this;
        }
        Path &crossWestward() { return travel({{30.0, 0.0}, {-30.0, 0.0}}, 6.0); }
        Path &loopBackEast()
        {
            return travel({{-30.0, 120.0}, {80.0, 120.0}, {80.0, 0.0}, {30.0, 0.0}});
        }
    };
    const auto sessionOf = [&origin](const Path &path) {
        constexpr double earthRadiusMeters = 6'371'000.0;
        constexpr double radiansPerDegree = std::numbers::pi / 180.0;
        TelemetrySession session;
        TelemetryChannel latitude;
        latitude.name = QStringLiteral("latitude");
        TelemetryChannel longitude;
        longitude.name = QStringLiteral("longitude");
        for (qsizetype index = 0; index < path.times.size(); ++index) {
            latitude.values.append(float(origin.latitudeDegrees
                + path.north[index] / earthRadiusMeters / radiansPerDegree));
            longitude.values.append(float(origin.longitudeDegrees
                + path.east[index] / (earthRadiusMeters
                    * std::cos(origin.latitudeDegrees * radiansPerDegree)) / radiansPerDegree));
        }
        latitude.timestamps = path.times;
        longitude.timestamps = path.times;
        for (TelemetryChannel *channel : {&latitude, &longitude}) {
            session.channels.insert(channel->name, *channel);
            session.aliases.insert(channel->name, channel->name);
        }
        session.duration = path.times.constLast();
        session.sampleCount = path.times.size();
        return session;
    };

    {
        Path path;
        path.points({{30.0, 0.0}}).crossWestward().loopBackEast().crossWestward();
        const LapSession laps = detectLaps(sessionOf(path), gate);
        QCOMPARE(laps.acceptedPasses.size(), qsizetype(2));
        QCOMPARE(laps.timedLaps.size(), qsizetype(1));
        QCOMPARE(laps.diagnostics.rejectedNotCrossingClusters, qsizetype(0));
    }
    {
        // Fast in from the east to 2 m short of the line, then away east again
        // more slowly: enough motion across the line for the old detector.
        Path path;
        path.points({{30.0, 0.0}}).crossWestward().loopBackEast().travel({{60.0, -20.0}});
        path.points({{30.0, -10.0}, {2.0, 0.0}, {6.0, 2.0}, {9.0, 4.0}, {13.0, 6.0},
                     {17.0, 8.0}});
        path.travel({{80.0, 30.0}, {80.0, 0.0}, {30.0, 0.0}}).crossWestward();
        const LapSession laps = detectLaps(sessionOf(path), gate);
        QCOMPARE(laps.diagnostics.rejectedNotCrossingClusters, qsizetype(1));
        QCOMPARE(laps.acceptedPasses.size(), qsizetype(2));
        QCOMPARE(laps.timedLaps.size(), qsizetype(1));
    }
    {
        // Approaching and stopping 1 m before the line at the end.
        Path path;
        path.points({{40.0, 0.0}, {30.0, 0.0}, {20.0, 0.0}, {10.0, 0.0}, {1.0, 0.0}});
        const LapSession laps = detectLaps(sessionOf(path), gate);
        QVERIFY(laps.acceptedPasses.isEmpty());
        QCOMPARE(laps.diagnostics.rejectedNotCrossingClusters, qsizetype(1));
    }
    {
        // Running along the line on one side.
        Path path;
        path.points({{3.0, -60.0}}).travel({{3.0, 60.0}}, 5.0);
        QVERIFY(detectLaps(sessionOf(path), gate).acceptedPasses.isEmpty());
    }
    {
        // The gate ends at north 11.1; crossing at north 14 is within the corridor.
        Path path;
        path.points({{30.0, 14.0}}).travel({{-30.0, 14.0}}, 6.0);
        QCOMPARE(detectLaps(sessionOf(path), gate).acceptedPasses.size(), qsizetype(1));
    }
    {
        // Crossing and coming back to leave on the starting side.
        Path path;
        path.points({{60.0, 0.0}, {40.0, 0.0}, {-3.0, 0.0}, {-6.0, 2.0}, {4.0, 5.0},
                     {30.0, 6.0}}).travel({{80.0, 6.0}});
        const LapSession laps = detectLaps(sessionOf(path), gate);
        QVERIFY(laps.acceptedPasses.isEmpty());
        QCOMPARE(laps.diagnostics.rejectedNotCrossingClusters, qsizetype(1));
    }
    {
        // Within 5 m of endpoint B on the east side, then over the line at about
        // north 20, beyond the 5 m widening.
        Path path;
        path.points({{40.0, 0.0}, {25.0, 5.0}, {4.0, 12.0}, {-1.0, 20.0}})
            .travel({{-30.0, 60.0}});
        const LapSession laps = detectLaps(sessionOf(path), gate);
        QVERIFY(laps.acceptedPasses.isEmpty());
        QCOMPARE(laps.diagnostics.rejectedNotCrossingClusters, qsizetype(1));
    }
    {
        // A pit lane crossing the line 8 m beyond the gate end.
        Path path;
        path.points({{40.0, 19.1}}).travel({{-40.0, 19.1}}, 4.0);
        QVERIFY(detectLaps(sessionOf(path), gate).acceptedPasses.isEmpty());
    }
    {
        // Arriving, oscillating by the line and leaving on the same side.
        Path path;
        path.points({{60.0, 0.0}, {30.0, 0.0}, {2.0, 0.0}});
        for (int index = 0; index < 4; ++index) path.points({{index % 2 == 0 ? -1.0 : 2.0, 0.0}});
        path.points({{9.0, 0.0}, {30.0, 0.0}, {60.0, 0.0}});
        const LapSession laps = detectLaps(sessionOf(path), gate);
        QVERIFY(laps.acceptedPasses.isEmpty());
        QCOMPARE(laps.diagnostics.rejectedNotCrossingClusters, qsizetype(1));
    }
    {
        // Crossings are found at 1 Hz and at 100 Hz; at 1 Hz the line is crossed a
        // fifth of the way from 40 m to -10 m, at 1.8 s.
        Path slow;
        slow.step = 1.0;
        slow.points({{80.0, 0.0}, {40.0, 0.0}, {-10.0, 0.0}, {-60.0, 0.0}});
        const LapSession slowLaps = detectLaps(sessionOf(slow), gate);
        QCOMPARE(slowLaps.acceptedPasses.size(), qsizetype(1));
        QVERIFY(std::abs(slowLaps.acceptedPasses.constFirst().telemetryTime - 1.8) < 0.02);
        Path fast;
        fast.step = 0.01;
        fast.points({{30.0, 0.0}}).travel({{-30.0, 0.0}}, 0.3);
        QCOMPARE(detectLaps(sessionOf(fast), gate).acceptedPasses.size(), qsizetype(1));
    }
    {
        // GPS oscillating around the line while stationary.
        Path path;
        for (int index = 0; index < 100; ++index) path.points({{index % 2 == 0 ? 3.0 : -3.0, 0.0}});
        QVERIFY(detectLaps(sessionOf(path), gate).acceptedPasses.isEmpty());
    }
    // Invariant: a path that never reaches the line never gives a pass.
    QRandomGenerator random(198);
    for (int trial = 0; trial < 200; ++trial) {
        Path path;
        path.step = 0.1 + random.generateDouble() * 0.4;
        path.points({{60.0, -40.0}});
        for (int leg = 0; leg < 12; ++leg) {
            // Random waypoints east of the line, some within a metre of it.
            const double e = random.bounded(2) == 0 ? 1.0 + random.generateDouble() * 4.0
                                                    : 1.0 + random.generateDouble() * 80.0;
            const double n = -40.0 + random.generateDouble() * 80.0;
            path.travel({{e, n}}, 2.0 + random.generateDouble() * 20.0);
        }
        QVERIFY2(detectLaps(sessionOf(path), gate).acceptedPasses.isEmpty(),
                 qPrintable(QStringLiteral("trial %1").arg(trial)));
    }
}

void TelemetryCoreTests::parsesOptionalRealVbo()
{
    const QString path = qEnvironmentVariable("FLAPPEDEAR_REAL_VBO");
    if (path.isEmpty()) {
        QSKIP("FLAPPEDEAR_REAL_VBO is not set");
    }
    const auto session = VboParser::parseFile(path);
    QVERIFY(session.sampleCount > 0);
    QVERIFY(!session.channels.isEmpty());
    QVERIFY(std::isfinite(session.duration));
    QVERIFY(session.duration >= 0.0);
    qsizetype nonFiniteSamples = 0;
    qsizetype finiteSamples = 0;
    for (const TelemetryChannel &channel : session.channels) {
        QCOMPARE(channel.timestamps.size(), session.sampleCount);
        QCOMPARE(channel.values.size(), session.sampleCount);
        for (qsizetype index = 0; index < channel.values.size(); ++index) {
            if (index > 0) QVERIFY(channel.timestamps[index] > channel.timestamps[index - 1]);
            const float value = channel.values[index];
            if (!std::isfinite(value)) {
                ++nonFiniteSamples;
            } else {
                ++finiteSamples;
            }
        }
    }
    QVERIFY(finiteSamples > 0);
    qInfo().noquote() << QStringLiteral(
        "real VBO: %1 samples, %2 channels, duration=%3 s, timing gates=%4, "
        "%5 parser warnings, %6 non-finite values")
                             .arg(session.sampleCount)
                             .arg(session.channels.size())
                             .arg(session.duration, 0, 'f', 3)
                             .arg(session.timingGates.size())
                             .arg(session.warnings.size())
                             .arg(nonFiniteSamples);
}

void TelemetryCoreTests::derivesOptionalRealVboLaps()
{
    const QString path = qEnvironmentVariable("FLAPPEDEAR_REAL_VBO");
    if (path.isEmpty()) QSKIP("FLAPPEDEAR_REAL_VBO is not set");
    const TelemetrySession session = VboParser::parseFile(path);
    const LapSession laps = deriveSourceLapSession(session);
    QVERIFY(laps.selectedStartGate.has_value());
    QCOMPARE(laps.selectedStartGate->type, TimingGateType::Start);
    for (qsizetype index = 1; index < laps.acceptedPasses.size(); ++index) {
        QVERIFY(laps.acceptedPasses[index].telemetryTime
                > laps.acceptedPasses[index - 1].telemetryTime);
    }
    QCOMPARE(laps.timedLaps.size(), std::max<qsizetype>(0, laps.acceptedPasses.size() - 1));
    if (!laps.timedLaps.isEmpty()) {
        QVERIFY(laps.fastestLapIndex.has_value());
        QVERIFY(*laps.fastestLapIndex < static_cast<qsizetype>(laps.timedLaps.size()));
    }
    const TimingGate &gate = *laps.selectedStartGate;
    qInfo().noquote() << QStringLiteral(
        "real laps: status=%1 start=(%2,%3)->(%4,%5) passes=%6 complete=%7 fastest=%8")
                             .arg(static_cast<int>(laps.status))
                             .arg(gate.endpointA.latitudeDegrees, 0, 'f', 8)
                             .arg(gate.endpointA.longitudeDegrees, 0, 'f', 8)
                             .arg(gate.endpointB.latitudeDegrees, 0, 'f', 8)
                             .arg(gate.endpointB.longitudeDegrees, 0, 'f', 8)
                             .arg(laps.acceptedPasses.size())
                             .arg(laps.timedLaps.size())
                             .arg(laps.fastestLapIndex ? QString::number(*laps.fastestLapIndex + 1)
                                                       : QStringLiteral("none"));
    for (const GatePass &pass : laps.acceptedPasses) {
        qInfo().noquote() << QStringLiteral(
            "real passage: telemetry=%1 direction=%2 gateFraction=%3 distance=%4 m")
                                 .arg(pass.telemetryTime, 0, 'f', 3)
                                 .arg(pass.direction)
                                 .arg(pass.gateFraction, 0, 'f', 3)
                                 .arg(pass.closestDistanceMeters, 0, 'f', 3);
    }
    for (const TimedLap &lap : laps.timedLaps) {
        qInfo().noquote() << QStringLiteral(
            "real lap %1: telemetryStart=%2 duration=%3 delta=%4")
                                 .arg(lap.number)
                                 .arg(lap.startTelemetryTime, 0, 'f', 3)
                                 .arg(lap.durationSeconds, 0, 'f', 3)
                                 .arg(lap.deltaToBestSeconds, 0, 'f', 3);
    }
}

void TelemetryCoreTests::buildsTrackGeometry()
{
    const TelemetrySession session = VboParser::parseFile(TEST_FIXTURE_PATH);
    const TrackGeometry geometry = buildTrackGeometry(session);
    QVERIFY(geometry.valid);
    QCOMPARE(geometry.points.size(), 3);
    QVERIFY(geometry.points.front().x() > 0.0);
    QCOMPARE(geometry.points.front().y(), 1.0);
    QVERIFY(geometry.points.back().x() < 1.0);
    QCOMPARE(geometry.points.back().y(), 0.0);
    const auto current = currentTrackPoint(session, 0.5, geometry);
    QVERIFY(current.has_value());
    QVERIFY2(
        qAbs(current->x() - 0.5) < 0.01,
        qPrintable(QStringLiteral("x=%1").arg(current->x(), 0, 'g', 12)));
    QVERIFY2(
        qAbs(current->y() - 0.5) < 0.01,
        qPrintable(QStringLiteral("y=%1").arg(current->y(), 0, 'g', 12)));
}

void TelemetryCoreTests::cancelsTrackGeometryConstruction()
{
    TelemetrySession session;
    TelemetryChannel latitude;
    latitude.name = QStringLiteral("latitude");
    TelemetryChannel longitude;
    longitude.name = QStringLiteral("longitude");
    constexpr qsizetype count = 50'000;
    latitude.timestamps.reserve(count);
    latitude.values.reserve(count);
    longitude.timestamps.reserve(count);
    longitude.values.reserve(count);
    for (qsizetype index = 0; index < count; ++index) {
        latitude.timestamps.append(static_cast<double>(index) / 10.0);
        longitude.timestamps.append(static_cast<double>(index) / 10.0);
        latitude.values.append(static_cast<float>(52.0 + index * 0.000001));
        longitude.values.append(static_cast<float>(21.0 + index * 0.000001));
    }
    session.channels.insert(latitude.name, latitude);
    session.channels.insert(longitude.name, longitude);
    session.aliases.insert(QStringLiteral("latitude"), latitude.name);
    session.aliases.insert(QStringLiteral("longitude"), longitude.name);
    int checks = 0;
    QVERIFY_THROWS_EXCEPTION(
        OperationCancelled,
        (void) buildTrackGeometry(session, [&checks] { return ++checks == 8; }));
    QVERIFY(checks >= 8);
}

void TelemetryCoreTests::boundsExternalJsonDocuments()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString validPath = directory.filePath(QStringLiteral("valid.fetproject"));
    const QByteArray valid = QJsonDocument(QJsonObject{{QStringLiteral("version"), 2}}).toJson();
    QVERIFY(writeBytes(validPath, valid));
    const auto accepted = BoundedJsonLoader::loadFile(
        validPath, valid.size(), QStringLiteral("Project"));
    QVERIFY2(accepted.success(), qPrintable(accepted.error));

    const QString oversizedPath = directory.filePath(QStringLiteral("oversized.fetproject"));
    QVERIFY(writeBytes(oversizedPath, QByteArray(ProjectLimits::projectBytes + 1, ' ')));
    const auto oversized = BoundedJsonLoader::loadFile(
        oversizedPath, ProjectLimits::projectBytes, QStringLiteral("Project"));
    QVERIFY(!oversized.success());
    QVERIFY(oversized.error.contains(QStringLiteral("limit")));

    const QString malformedPath = directory.filePath(QStringLiteral("malformed.fetproject"));
    QVERIFY(writeBytes(malformedPath, QByteArrayLiteral("{ not JSON")));
    const auto malformed = BoundedJsonLoader::loadFile(
        malformedPath, ProjectLimits::projectBytes, QStringLiteral("Project"));
    QVERIFY(!malformed.success());
    QVERIFY(malformed.error.contains(QStringLiteral("invalid JSON")));
}

void TelemetryCoreTests::scansFoldersForRecordingsWithinBounds()
{
    // KAN-87: a chosen folder yields its VBO/RCZ recordings under an explicit
    // subfolder policy, within depth and entry bounds, never following links
    // (so a link back up the tree cannot loop), refusing more than a batch
    // rather than truncating, and stopping when cancelled.
    QTemporaryDir directory; QVERIFY(directory.isValid());
    const QDir root(directory.path());
    QVERIFY(root.mkpath("sub/deeper/deepest"));
    QVERIFY(root.mkpath(".Trashes"));
    // KAN-173: dot entries, including the macOS AppleDouble sidecars ("._name") that
    // SD cards and exFAT drives carry, are skipped on every platform, not only where
    // a leading dot makes a file hidden.
    for (const auto *name : {"a.VBO", "b.rcz", "notes.txt", ".hidden.vbo", "._a.VBO", ".Trashes/f.vbo",
                             "sub/c.vbo", "sub/._c.vbo", "sub/deeper/d.rcz", "sub/deeper/deepest/e.vbo"})
        QVERIFY(writeBytes(root.filePath(name), "x"));

    auto top = scanTelemetryFolder(root.path(), false);
    QVERIFY(top.error.isEmpty());
    QCOMPARE(top.files, QStringList({root.filePath("a.VBO"), root.filePath("b.rcz")}));
    QVERIFY(top.notes.join(' ').contains("1 other file(s) were ignored"));

    auto all = scanTelemetryFolder(root.path(), true);
    QCOMPARE(all.files.size(), 5);
    QVERIFY(all.files.contains(root.filePath("sub/deeper/deepest/e.vbo")));
    QVERIFY(std::is_sorted(all.files.cbegin(), all.files.cend()));

    TelemetryFolderScanLimits shallow; shallow.maximumDepth = 1;
    auto bounded = scanTelemetryFolder(root.path(), true, {}, shallow);
    QCOMPARE(bounded.files.size(), 3); // a, b, sub/c
    QVERIFY(bounded.notes.join(' ').contains("1 folder(s) deeper than 1 levels were not scanned"));

#ifdef Q_OS_UNIX
    // A link back to the top and a link to a file: neither is followed.
    QVERIFY(QFile::link(root.path(), root.filePath("sub/loop")));
    QVERIFY(QFile::link(root.filePath("a.VBO"), root.filePath("sub/linked.vbo")));
    auto linked = scanTelemetryFolder(root.path(), true);
    QCOMPARE(linked.files.size(), 5);
    QVERIFY(linked.notes.join(' ').contains("2 link(s) were not followed"));
    QVERIFY(!scanTelemetryFolder(root.filePath("sub/loop"), true).error.isEmpty()); // a linked root is refused
#endif

    TelemetryFolderScanLimits two; two.maximumFiles = 2;
    auto tooMany = scanTelemetryFolder(root.path(), true, {}, two);
    QVERIFY(tooMany.files.isEmpty());
    QVERIFY(tooMany.error.contains("holds 5 recordings; import at most 2"));

    TelemetryFolderScanLimits few; few.maximumEntries = 2;
    auto stopped = scanTelemetryFolder(root.path(), true, {}, few);
    QVERIFY(stopped.notes.join(' ').contains("Stopped after 2 files and folders"));

    int checks = 0;
    auto cancelled = scanTelemetryFolder(root.path(), true, [&checks] { return ++checks > 3; });
    QVERIFY(cancelled.cancelled);
    QVERIFY(cancelled.files.isEmpty());

    QVERIFY(!scanTelemetryFolder(root.filePath("missing"), true).error.isEmpty());
    QVERIFY(!scanTelemetryFolder(root.filePath("a.VBO"), true).error.isEmpty());
    const QString empty = root.filePath("empty");
    QVERIFY(root.mkpath("empty"));
    QVERIFY(scanTelemetryFolder(empty, false).error.contains("No VBO or RCZ recordings were found (subfolders were not included)"));
}

void TelemetryCoreTests::combinesDroppedFilesAndFolders()
{
    // KAN-88: any mix of dropped items becomes one list of recordings, with
    // an explicit note for everything that is not imported.
    QTemporaryDir directory; QVERIFY(directory.isValid());
    const QDir root(directory.path());
    QVERIFY(root.mkpath("day/sub"));
    QVERIFY(root.mkpath("empty"));
    for (const auto *name : {"loose.vbo", "._loose.vbo", "photo.jpg", "day/a.vbo", "day/._a.vbo", "day/b.RCZ", "day/sub/c.vbo"})
        QVERIFY(writeBytes(root.filePath(name), "x"));

    const auto mixed = scanTelemetrySources({root.filePath("loose.vbo"), root.filePath("._loose.vbo"), root.filePath("photo.jpg"),
        root.filePath("missing.vbo"), root.filePath("day"), root.filePath("day/a.vbo"), root.filePath("empty")}, false);
    QVERIFY(mixed.error.isEmpty());
    // day/a.vbo arrives twice (itself and through its folder) and counts once; the
    // dropped and the in-folder AppleDouble sidecars are never recordings (KAN-173).
    QCOMPARE(mixed.files.size(), 3);
    const auto notes = mixed.notes.join('\n');
    QVERIFY(notes.contains("._loose.vbo: a macOS metadata file, not a recording; not imported."));
    QVERIFY(notes.contains("photo.jpg: not a VBO or RCZ recording; not imported."));
    QVERIFY(notes.contains("missing.vbo: not found; not imported."));
    QVERIFY(notes.contains("empty: No VBO or RCZ recordings were found"));

    QCOMPARE(scanTelemetrySources({root.filePath("day")}, true).files.size(), 3);
    // A single folder keeps its own error; only unsupported items is an error too.
    QVERIFY(scanTelemetrySources({root.filePath("empty")}, false).error.contains("No VBO or RCZ recordings were found"));
    const auto nothing = scanTelemetrySources({root.filePath("photo.jpg")}, false);
    QCOMPARE(nothing.error, QString("No VBO or RCZ recordings to import."));
    QVERIFY(nothing.notes.join(' ').contains("photo.jpg"));

    TelemetryFolderScanLimits two; two.maximumFiles = 2;
    QVERIFY(scanTelemetrySources({root.filePath("day"), root.filePath("loose.vbo")}, false, {}, two).error
        .contains("That is 3 recordings; import at most 2"));
    int checks = 0;
    QVERIFY(scanTelemetrySources({root.filePath("day"), root.filePath("loose.vbo")}, true, [&checks] { return ++checks > 2; }).cancelled);
}

QTEST_GUILESS_MAIN(TelemetryCoreTests)
namespace {

TelemetryChannel tyreChannel(const QString &name, const QVector<float> &values, const QString &unit = {})
{
    TelemetryChannel channel;
    channel.name = name;
    channel.unit = unit;
    for (qsizetype index = 0; index < values.size(); ++index) channel.timestamps.append(index / 10.0);
    channel.values = values;
    return channel;
}

} // namespace

void TelemetryCoreTests::formatsLapTimesRoundedBeforeMinutes()
{
    // KAN-149: rounding first, then minutes; never "x:60".
    QCOMPARE(formatLapTime(59.96, 1), QStringLiteral("1:00.0"));
    QCOMPARE(formatLapTime(59.95, 1), QStringLiteral("1:00.0")); // half away from zero
    QCOMPARE(formatLapTime(59.94, 1), QStringLiteral("0:59.9"));
    QCOMPARE(formatLapTime(60.0, 1), QStringLiteral("1:00.0"));
    QCOMPARE(formatLapTime(59.995, 2), QStringLiteral("1:00.00"));
    QCOMPARE(formatLapTime(119.996, 2), QStringLiteral("2:00.00"));
    QCOMPARE(formatLapTime(119.995, 2), QStringLiteral("2:00.00"));
    QCOMPARE(formatLapTime(119.994, 2), QStringLiteral("1:59.99"));
    QCOMPARE(formatLapTime(3599.9995, 3), QStringLiteral("60:00.000"));
    QCOMPARE(formatLapTime(3599.9994, 3), QStringLiteral("59:59.999"));
    QCOMPARE(formatLapTime(100.234, 2), QStringLiteral("1:40.23"));
    QCOMPARE(formatLapTime(100.234, 3), QStringLiteral("1:40.234"));
    QCOMPARE(formatLapTime(65.4, 0), QStringLiteral("1:05"));
    QCOMPARE(formatLapTime(0.0, 3), QStringLiteral("0:00.000"));
    QCOMPARE(formatLapTime(5.5, 9), QStringLiteral("0:05.500")); // at most 3 decimals
    QVERIFY(formatLapTime(-0.001, 2).isEmpty());
    QVERIFY(formatLapTime(std::numeric_limits<double>::quiet_NaN(), 2).isEmpty());
    QVERIFY(formatLapTime(std::numeric_limits<double>::infinity(), 2).isEmpty());
}

void TelemetryCoreTests::mapsTyreChannelsPerCorner()
{
    TelemetrySession session;
    const auto add = [&session](const TelemetryChannel &channel) { session.channels.insert(channel.name, channel); };
    // RaceChrono CAN names (kPa, °C), a declared unit, and names that are not
    // one corner's tyre value.
    add(tyreChannel(QStringLiteral("tyre_temp_fl-canbus"), {0, 41, 42}));
    add(tyreChannel(QStringLiteral("tyre_pressure_fl-canbus"), {0, 214, 217}));
    add(tyreChannel(QStringLiteral("Tire Pressure RR"), {32, 33}, QStringLiteral("psi")));
    add(tyreChannel(QStringLiteral("tyre_pressure_rl"), {2.1f, 2.2f}));
    add(tyreChannel(QStringLiteral("tyre_temp_fr"), {100, 104}, QStringLiteral("°F")));
    add(tyreChannel(QStringLiteral("tyre_pressure_fr"), {5000, 5000}));      // no plausible unit
    add(tyreChannel(QStringLiteral("tyre_temp"), {40}));                     // no corner
    add(tyreChannel(QStringLiteral("tyre_temp_fl_fr"), {40}));               // two corners
    add(tyreChannel(QStringLiteral("brake_temp_rl"), {300}));                // not a tyre
    add(tyreChannel(QStringLiteral("tyre_temp_pressure_rl"), {40}));         // both kinds

    const TyreChannelMap map = mapTyreChannels(session);
    QVERIFY(map.hasAny());
    QCOMPARE(map.temperature[0], QStringLiteral("tyre_temp_fl-canbus"));
    QCOMPARE(map.pressure[0], QStringLiteral("tyre_pressure_fl-canbus"));
    QCOMPARE(map.pressureUnit[0], PressureUnit::Kilopascal);
    QCOMPARE(map.temperature[1], QStringLiteral("tyre_temp_fr"));
    QVERIFY(map.temperatureFahrenheit[1]);
    QCOMPARE(map.pressureUnit[1], PressureUnit::Unknown);
    QVERIFY(map.temperature[2].isEmpty());
    QCOMPARE(map.pressureUnit[2], PressureUnit::Bar);
    QCOMPARE(map.pressureUnit[3], PressureUnit::Psi);
    QVERIFY(map.temperature[3].isEmpty());

    QVERIFY(!mapTyreChannels(TelemetrySession{}).hasAny());
    // Only placeholders: no unit, so no pressure is ever shown.
    QCOMPARE(classifyPressureUnit(tyreChannel(QStringLiteral("p"), {0, 0, 0})), PressureUnit::Unknown);
    QCOMPARE(classifyPressureUnit(tyreChannel(QStringLiteral("p"), {0, 0, 30})), PressureUnit::Psi);
}

void TelemetryCoreTests::appliesChosenTyreChannels()
{
    // KAN-203: a chosen channel replaces the automatic one for its corner; an
    // empty choice keeps it, and a channel the recording lacks is no data.
    TelemetrySession session;
    const auto add = [&session](const TelemetryChannel &channel) { session.channels.insert(channel.name, channel); };
    add(tyreChannel(QStringLiteral("tyre_temp_fl"), {40, 41}));
    add(tyreChannel(QStringLiteral("TPMS 1 temp"), {104, 104}, QStringLiteral("°F")));
    add(tyreChannel(QStringLiteral("TPMS 1 kpa"), {210, 212}));
    add(tyreChannel(QStringLiteral("TPMS 2"), {31, 32}));
    std::array<QString, tyreCornerCount> temperature{QString(), QStringLiteral("TPMS 1 temp"), QString(), QStringLiteral("absent")};
    std::array<QString, tyreCornerCount> pressure{QStringLiteral("TPMS 1 kpa"), QString(), QStringLiteral("TPMS 2"), QString()};
    QHash<QString, PressureUnit> units;
    const TyreChannelMap map = withTyreChannelChoices(session, mapTyreChannels(session), temperature, pressure, &units);
    QCOMPARE(map.temperature[0], QStringLiteral("tyre_temp_fl"));
    QCOMPARE(map.temperature[1], QStringLiteral("TPMS 1 temp"));
    QVERIFY(map.temperatureFahrenheit[1]);
    QCOMPARE(map.pressureUnit[0], PressureUnit::Kilopascal);
    QCOMPARE(map.pressureUnit[2], PressureUnit::Psi);
    QCOMPARE(units.value(QStringLiteral("TPMS 2")), PressureUnit::Psi);
    QCOMPARE(*tyreReadingAt(session, map, TyreCorner::FrontRight, 0.0).temperatureCelsius, 40.0);
    QCOMPARE(*tyreReadingAt(session, map, TyreCorner::FrontLeft, 0.0).pressureBar, 2.1);
    QVERIFY(!tyreReadingAt(session, map, TyreCorner::RearRight, 0.0).temperatureCelsius);

    // A cached unit is reused rather than classified again.
    units.insert(QStringLiteral("TPMS 2"), PressureUnit::Bar);
    QCOMPARE(withTyreChannelChoices(session, {}, {}, pressure, &units).pressureUnit[2], PressureUnit::Bar);
}

void TelemetryCoreTests::readsTyreValuesWithoutPlaceholdersOrGaps()
{
    TelemetrySession session;
    // FL: 0 placeholders until the sensor reports at 0.3 s, then kPa.
    auto temperature = tyreChannel(QStringLiteral("tyre_temp_fl-canbus"), {0, 0, 0, 40, 42, 44, 46});
    auto pressure = tyreChannel(QStringLiteral("tyre_pressure_fl-canbus"), {0, 0, 0, 200, 210, 220, 230});
    // A 2 s gap before the last pressure sample: nothing is bridged.
    pressure.timestamps.last() = 2.5;
    session.channels.insert(temperature.name, temperature);
    session.channels.insert(pressure.name, pressure);
    const auto fahrenheit = tyreChannel(QStringLiteral("tyre_temp_fr"), {104, 104}, QStringLiteral("F"));
    session.channels.insert(fahrenheit.name, fahrenheit);
    const TyreChannelMap map = mapTyreChannels(session);

    const auto fl = [&](double time) { return tyreReadingAt(session, map, TyreCorner::FrontLeft, time); };
    QVERIFY(!fl(0.1).temperatureCelsius && !fl(0.1).pressureBar);
    QVERIFY(!fl(0.25).temperatureCelsius); // between a placeholder and a reading
    QCOMPARE(*fl(0.3).temperatureCelsius, 40.0);
    QCOMPARE(*fl(0.3).pressureBar, 2.0);
    QVERIFY(qAbs(*fl(0.35).temperatureCelsius - 41.0) < 1e-6);
    QVERIFY(qAbs(*fl(0.35).pressureBar - 2.05) < 1e-6);
    QVERIFY(fl(0.55).temperatureCelsius);
    QVERIFY(!fl(1.5).pressureBar);          // inside the gap
    QVERIFY(!fl(3.0).pressureBar);          // after the recording
    QVERIFY(!fl(std::numeric_limits<double>::quiet_NaN()).temperatureCelsius);

    QCOMPARE(*tyreReadingAt(session, map, TyreCorner::FrontRight, 0.0).temperatureCelsius, 40.0);
    const TyreReading missing = tyreReadingAt(session, map, TyreCorner::RearLeft, 0.3);
    QVERIFY(!missing.temperatureCelsius && !missing.pressureBar);

    // Implausible values are no data, never clamped.
    TelemetrySession hot;
    const auto implausible = tyreChannel(QStringLiteral("tyre_temp_rr"), {400, 400});
    hot.channels.insert(implausible.name, implausible);
    QVERIFY(!tyreReadingAt(hot, mapTyreChannels(hot), TyreCorner::RearRight, 0.05).temperatureCelsius);
}

void TelemetryCoreTests::readsPrivateTyreData()
{
    // Opt-in: every session of a real day (FLAPPEDEAR_REAL_DAY, private VBO
    // recordings) sampled once a second, with each corner's coverage and range.
    const auto path = qEnvironmentVariable("FLAPPEDEAR_REAL_DAY");
    if (path.isEmpty()) QSKIP("FLAPPEDEAR_REAL_DAY is not set");
    for (const auto &file : QDir(path).entryInfoList({"*.vbo"}, QDir::Files, QDir::Name)) {
        const auto session = TelemetrySource::load(file.absoluteFilePath());
        const TyreChannelMap map = mapTyreChannels(session);
        if (!map.hasAny()) { qInfo().noquote() << file.baseName().left(24) << "no tyre channels"; continue; }
        const auto names = session.channelNames();
        double start = std::numeric_limits<double>::infinity(), end = -start;
        for (const auto &name : names) {
            const auto &timestamps = session.channels.value(name).timestamps;
            if (timestamps.isEmpty()) continue;
            start = std::min(start, timestamps.first());
            end = std::max(end, timestamps.last());
        }
        for (int corner = 0; corner < tyreCornerCount; ++corner) {
            QVERIFY(!map.pressure[corner].isEmpty() && !map.temperature[corner].isEmpty());
            QCOMPARE(map.pressureUnit[corner], PressureUnit::Kilopascal);
            int samples = 0, temperatures = 0, pressures = 0;
            double minT = 1e9, maxT = -1e9, minP = 1e9, maxP = -1e9;
            for (double time = start; time <= end; time += 1.0, ++samples) {
                const auto reading = tyreReadingAt(session, map, static_cast<TyreCorner>(corner), time);
                if (reading.temperatureCelsius) {
                    ++temperatures;
                    minT = std::min(minT, *reading.temperatureCelsius); maxT = std::max(maxT, *reading.temperatureCelsius);
                }
                if (reading.pressureBar) {
                    ++pressures;
                    minP = std::min(minP, *reading.pressureBar); maxP = std::max(maxP, *reading.pressureBar);
                }
            }
            qInfo().noquote() << QString("%1 %2: temperature %3% %4..%5 °C · pressure %6% %7..%8 bar")
                .arg(file.baseName().left(24), tyreCornerCode(static_cast<TyreCorner>(corner)))
                .arg(100.0 * temperatures / samples, 0, 'f', 0).arg(minT, 0, 'f', 0).arg(maxT, 0, 'f', 0)
                .arg(100.0 * pressures / samples, 0, 'f', 0).arg(minP, 0, 'f', 2).arg(maxP, 0, 'f', 2);
            QVERIFY(temperatures > samples / 2 && pressures > samples / 2);
            QVERIFY(minT > 0.0 && maxT < 150.0);
            QVERIFY(minP > 0.5 && maxP < 5.0);
        }
    }
}

#include "TelemetryCoreTests.moc"
