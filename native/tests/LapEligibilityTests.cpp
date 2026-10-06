#include "EventProjectFixture.h"
#include "telemetry/LapTiming.h"
#include "telemetry/OutingLaps.h"
#include "telemetry/TelemetryRenderContext.h"
#include "telemetry/VboParser.h"

#include <QtTest>
#include <limits>
#include <algorithm>

using namespace FlappedEar;

namespace {
TelemetrySession interiorGapFixture()
{
    auto session = VboParser::parse(QString::fromUtf8(EventProjectFixture::lapsVbo()));
    // Keep valid GPS before and after each injected interior defect. In the
    // sparse fixture every point also arms or finalizes a gate passage, so
    // removing one tests passage rejection rather than interior lap coverage.
    for (auto &channel : session.channels) {
        const auto times = channel.timestamps();
        const auto values = channel.values();
        channel.clear();
        for (qsizetype index = 1; index < times.size(); ++index) {
            for (double time = times[index - 1]; time < times[index]; time += 0.25) {
                const double fraction = (time - times[index - 1]) / (times[index] - times[index - 1]);
                channel.appendSample(time, static_cast<float>(values[index - 1]
                    + (values[index] - values[index - 1]) * fraction));
            }
        }
        channel.appendSample(times.last(), values.last());
    }
    return session;
}
}

class LapEligibilityTests final : public QObject {
    Q_OBJECT
private slots:
    void progressionUsesEligibleDistributions();
    void progressionPreservesOrderContextAndGaps();
    void ranksCompatibleLapsDeterministically();
    void rankingReportsAllAppliedExclusions();
    void exposesEligiblePopulationMatchingRanking();
    void separatesCompatibilityGroups_data();
    void separatesCompatibilityGroups();
    void preservesIndependentCompatibilityReasons();
    void preservesTimingWhenDensifyingFixture();
    void retainsMeasuredLapWithInteriorGpsGap();
    void rejectsInvalidAndUnpairedCoordinates_data();
    void rejectsInvalidAndUnpairedCoordinates();
    void exposesNoBestWhenEveryLapHasMissingGps();
    void rendererNeverUsesIneligibleCompletedLap();
    void holdsAChosenHotlap();
    void propagatesOutingEligibilityAndBest();
    void preservesNormalLapRanking();
    void rendererOmitsDeltaAtMissingCurrentCoordinate();
};

void LapEligibilityTests::progressionUsesEligibleDistributions()
{
    const QJsonObject config{{"layoutId", "Full"}, {"direction", "clockwise"}, {"gateRevision", "gates-v1:" + QString(64, 'a')}};
    const auto group = lapCompatibilityGroupId(config);
    QHash<QString, QJsonObject> configs{{"run", config}};
    QVector<OutingLapRow> rows;
    for (const double duration : {10., 20., 30., 40., 1000., 1., 2., 3.}) {
        OutingLapRow row; row.runId = "run"; row.runName = "Run"; row.type = LapSectionType::Lap;
        row.start = rows.size() * 2000.; row.end = row.start + duration;
        row.lapNumber = rows.size() + 1; row.referenceEligible = true;
        row.reference = makeLapReference(row, "event", "source", QByteArray(64, 'a'), QByteArray(64, 'b'));
        rows.append(row);
    }
    const QJsonArray exclusions{QJsonObject{{"reference", rows[5].reference}, {"reason", "Traffic"}}};
    rows[6].referenceIssue = LapReferenceIssue::GpsGap; rows[6].referenceEligible = false;
    rows[7].reference = {}; // An apparently fast malformed reference must not enter the distribution.
    const auto ranking = rankOutingLaps(rows, group, configs, exclusions);
    auto run = ranking.value("runs").toArray().first().toObject();
    QCOMPARE(run.value("eligibleLapCount").toInt(), 5); QCOMPARE(run.value("lapCount").toInt(), 8);
    QCOMPARE(run.value("distribution").toObject(), (QJsonObject{{"minimum", 10.}, {"q1", 20.},
        {"median", 30.}, {"q3", 40.}, {"maximum", 1000.}})); // No statistical outlier removal.
    QCOMPARE(run.value("bestLap").toObject().value("durationSeconds").toDouble(), 10.);
    std::reverse(rows.begin(), rows.end());
    QCOMPARE(rankOutingLaps(rows, group, configs, exclusions).value("runs"), ranking.value("runs"));
    auto singleton = rankOutingLaps({rows.last()}, group, configs, {}).value("runs").toArray().first().toObject();
    for (const auto *key : {"minimum", "q1", "median", "q3", "maximum"})
        QCOMPARE(singleton.value("distribution").toObject().value(key).toDouble(), 10.);
    const auto pair = rankOutingLaps({rows.last(), rows[rows.size() - 2]}, group, configs, {})
        .value("runs").toArray().first().toObject().value("distribution").toObject();
    QCOMPARE(pair.value("q1").toDouble(), 12.5); QCOMPARE(pair.value("median").toDouble(), 15.);
    QCOMPARE(pair.value("q3").toDouble(), 17.5);
    const auto stale = rankOutingLaps(rows, group, configs, exclusions, {"run"});
    QVERIFY(stale.value("runs").toArray().first().toObject().value("distribution").isNull());
    QCOMPARE(stale.value("eligibleLapCount").toInt(), 0);
}

void LapEligibilityTests::progressionPreservesOrderContextAndGaps()
{
    const QJsonObject config{{"layoutId", "Full"}, {"direction", "clockwise"}, {"gateRevision", "gates-v1:" + QString(64, 'a')}};
    const auto group = lapCompatibilityGroupId(config);
    auto other = config; other.insert("layoutId", "Short");
    QHash<QString, QJsonObject> configs{{"a", config}, {"b", config}, {"empty", config}, {"unknown", config}, {"other", other}};
    const auto make = [](const QString &id, double duration, std::optional<qint64> clock) {
        OutingLapRow row; row.runId = id; row.runName = id; row.type = LapSectionType::Lap;
        row.start = 0; row.end = duration; row.lapNumber = 1; row.timestampMilliseconds = clock; row.referenceEligible = true;
        row.reference = makeLapReference(row, "event", id + "-source", QByteArray(64, 'a'), QByteArray(64, 'b'));
        return row;
    };
    // A five-hour unrecorded break adds no rows, laps or synthetic timing samples.
    QVector<OutingLapRow> rows{make("b", 8, 18001000), make("unknown", 7, {}), make("a", 10, 1000), make("other", 1, 0)};
    const QJsonArray metadata{
        QJsonObject{{"id", "unknown"}, {"name", "Unknown clock"}, {"groupId", group}},
        QJsonObject{{"id", "b"}, {"name", "Afternoon"}, {"groupId", group}, {"notes", "Traffic observed"},
            {"conditions", "Damp"}, {"setupChanges", "Front pressure +0.1 bar"}},
        QJsonObject{{"id", "a"}, {"name", "Morning"}, {"groupId", group}},
        QJsonObject{{"id", "empty"}, {"name", "Unavailable recording"}, {"groupId", group}},
        QJsonObject{{"id", "other"}, {"name", "Other layout"}, {"groupId", lapCompatibilityGroupId(other)}}};
    auto ranking = rankOutingLaps(rows, group, configs, {}); ranking.insert("groupLabel", "Full clockwise");
    const auto progression = summarizeOutingProgression(rows, ranking, metadata);
    const auto runs = progression.value("runs").toArray(); QCOMPARE(runs.size(), 4);
    QCOMPARE(runs[0].toObject().value("runId").toString(), QString("a"));
    QCOMPARE(runs[1].toObject().value("runId").toString(), QString("b"));
    QCOMPARE(runs[2].toObject().value("runId").toString(), QString("unknown"));
    QCOMPARE(runs[3].toObject().value("runId").toString(), QString("empty"));
    QCOMPARE(runs[1].toObject().value("bestDeltaPreviousListedSeconds").toDouble(), -2.);
    QCOMPARE(runs[1].toObject().value("conditions").toString(), QString("Damp"));
    QCOMPARE(runs[1].toObject().value("notes").toString(), QString("Traffic observed"));
    QCOMPARE(runs[1].toObject().value("setupChanges").toString(), QString("Front pressure +0.1 bar"));
    QVERIFY(runs[0].toObject().value("conditions").isNull());
    QVERIFY(runs[2].toObject().value("firstSectionUtcMilliseconds").isNull());
    QCOMPARE(runs[3].toObject().value("state").toString(), QString("no-recorded-laps"));
    QVERIFY(runs[3].toObject().value("distribution").isNull());
    QVERIFY(runs[3].toObject().value("bestDeltaPreviousListedSeconds").isNull());
    QCOMPARE(progression.value("lapCount").toInt(), 3); QCOMPARE(progression.value("eligibleLapCount").toInt(), 3);
    QCOMPARE(progression.value("minimumSeconds").toDouble(), 7.); QCOMPARE(progression.value("maximumSeconds").toDouble(), 10.);
    std::reverse(rows.begin(), rows.end());
    QCOMPARE(summarizeOutingProgression(rows, rankOutingLaps(rows, group, configs, {}), metadata).value("runs"), runs);
    QJsonArray exclusions;
    for (const auto &row : rows) if (row.runId == "b") exclusions.append(QJsonObject{{"reference", row.reference}, {"reason", "Traffic"}});
    const auto filtered = summarizeOutingProgression(rows, rankOutingLaps(rows, group, configs, exclusions), metadata).value("runs").toArray();
    QVERIFY(filtered[1].toObject().value("distribution").isNull());
    QVERIFY(filtered[2].toObject().value("bestDeltaPreviousListedSeconds").isNull()); // Do not bridge the unavailable best.
    QCOMPARE(filtered[1].toObject().value("excludedLaps").toArray().first().toObject().value("userReason").toString(), QString("Traffic"));
    QVERIFY(summarizeOutingProgression(rows, rankOutingLaps(rows, "", configs, {}), metadata).value("runs").toArray().isEmpty());
    QJsonArray tooMany; for (int i = 0; i < 65; ++i) tooMany.append(metadata.first());
    QVERIFY_THROWS_EXCEPTION(ResourceLimitError, static_cast<void>(summarizeOutingProgression(rows, ranking, tooMany)));
}

void LapEligibilityTests::ranksCompatibleLapsDeterministically()
{
    const QJsonObject config{{"layoutId", "Full"}, {"direction", "clockwise"}, {"gateRevision", "gates-v1:" + QString(64, 'a')}};
    auto opposite = config; opposite.insert("direction", "counterclockwise");
    QHash<QString, QJsonObject> configs{{"a", config}, {"b", config}, {"c", opposite}, {"unknown", {}}};
    const auto make = [](QString run, double start, double duration, int number, std::optional<qint64> clock) {
        OutingLapRow row; row.runId = run; row.runName = run; row.type = LapSectionType::Lap;
        row.start = start; row.end = start + duration; row.lapNumber = number; row.timestampMilliseconds = clock;
        row.referenceEligible = true;
        row.reference = makeLapReference(row, "event", run + "-source", QByteArray(64, 'a'), QByteArray(64, 'b'));
        return row;
    };
    auto a = make("a", 20, 5, 2, 2000), b = make("b", 10, 5, 1, 1000);
    const auto fasterOther = make("c", 0, 1, 1, 0), fasterUnknown = make("unknown", 0, .5, 1, 0);
    QVector<OutingLapRow> rows{a, b, fasterOther, fasterUnknown};
    const auto group = lapCompatibilityGroupId(config);
    const auto ranked = rankOutingLaps(rows, group, configs, {});
    QCOMPARE(ranked.value("state").toString(), QString("available"));
    QCOMPARE(ranked.value("eligibleLapCount").toInt(), 2);
    QCOMPARE(ranked.value("tieCount").toInt(), 2);
    QCOMPARE(ranked.value("bestOfDay").toObject().value("reference").toObject(), b.reference);
    QCOMPARE(ranked.value("runs").toArray().first().toObject().value("runId").toString(), QString("b"));
    std::reverse(rows.begin(), rows.end());
    QCOMPARE(rankOutingLaps(rows, group, configs, {}), ranked);
    // With equal or absent clocks, stable run identity wins, not input order/name/number.
    a.timestampMilliseconds.reset(); b.timestampMilliseconds.reset();
    a.runName = "Zulu"; b.runName = "Alpha"; a.lapNumber = 99; b.lapNumber = 1;
    QCOMPARE(rankOutingLaps({b, a}, group, configs, {}).value("bestOfDay").toObject().value("reference").toObject(), a.reference);
    a.timestampMilliseconds = 3000;
    QCOMPARE(rankOutingLaps({b, a}, group, configs, {}).value("bestOfDay").toObject().value("reference").toObject(), a.reference);
    // Within one run, the earlier telemetry bounds win an otherwise exact tie.
    auto earlier = make("a", 0, 5, 77, {}); a.timestampMilliseconds.reset();
    QCOMPARE(rankOutingLaps({a, earlier}, group, configs, {}).value("bestOfDay").toObject().value("reference").toObject(), earlier.reference);
    QVERIFY(rankOutingLaps(rows, "", configs, {}).value("bestOfDay").isNull());
    QCOMPARE(rankOutingLaps(rows, "", configs, {}).value("state").toString(), QString("selection-required"));
}

void LapEligibilityTests::rankingReportsAllAppliedExclusions()
{
    const QJsonObject config{{"layoutId", "Full"}, {"direction", "clockwise"}, {"gateRevision", "gates-v1:" + QString(64, 'a')}};
    const auto group = lapCompatibilityGroupId(config);
    QHash<QString, QJsonObject> configs{{"a", config}, {"b", config}};
    OutingLapRow a; a.runId = "a"; a.runName = "Morning"; a.type = LapSectionType::Lap;
    a.start = 0; a.end = 4; a.lapNumber = 1; a.referenceEligible = true;
    a.reference = makeLapReference(a, "event", "a-source", QByteArray(64, 'a'), QByteArray(64, 'b'));
    auto b = a; b.runId = "b"; b.runName = "Afternoon"; b.end = 5;
    b.reference = makeLapReference(b, "event", "b-source", QByteArray(64, 'a'), QByteArray(64, 'b'));
    const QJsonArray exclusions{QJsonObject{{"reference", a.reference}, {"reason", "Traffic"}}};
    a.referenceIssue = LapReferenceIssue::GpsGap; a.referenceEligible = false;
    auto ranked = rankOutingLaps({a, b}, group, configs, exclusions);
    QCOMPARE(ranked.value("bestOfDay").toObject().value("reference").toObject(), b.reference);
    QCOMPARE(ranked.value("lapCount").toInt(), 2); QCOMPARE(ranked.value("eligibleLapCount").toInt(), 1);
    const auto rejected = ranked.value("excludedLaps").toArray().first().toObject();
    QCOMPARE(rejected.value("reasons").toArray(), (QJsonArray{"incomplete-gps", "user-exclusion"}));
    QCOMPARE(rejected.value("userReason").toString(), QString("Traffic"));
    QCOMPARE(ranked.value("runs").toArray().last().toObject().value("state").toString(), QString("no-eligible-laps"));
    ranked = rankOutingLaps({a, b}, group, configs, exclusions, {"b"});
    QCOMPARE(ranked.value("state").toString(), QString("no-eligible-laps"));
    QVERIFY(ranked.value("bestOfDay").isNull()); QCOMPARE(ranked.value("eligibleLapCount").toInt(), 0);
    QCOMPARE(ranked.value("excludedLaps").toArray().size(), 2);
    for (const auto &run : ranked.value("runs").toArray()) QVERIFY(run.toObject().value("bestLap").isNull());
    b.reference = {}; ranked = rankOutingLaps({b}, group, configs, {});
    QVERIFY(ranked.value("bestOfDay").isNull());
    QVERIFY(ranked.value("excludedLaps").toArray().first().toObject().value("reasons").toArray().contains("invalid-reference"));
    QVector<OutingLapRow> tooMany(maximumOutingLapRows + 1);
    QVERIFY_THROWS_EXCEPTION(ResourceLimitError, static_cast<void>(rankOutingLaps(tooMany, group, configs, {})));
}

// KAN-56: eligibleOutingLaps must return exactly rankOutingLaps' own eligible
// set (same reasons, same exclusions, same stale/group filtering) -- a second
// population consumer (theoretical best) can never silently drift from
// ranking's definition of "eligible".
void LapEligibilityTests::exposesEligiblePopulationMatchingRanking()
{
    const QJsonObject config{{"layoutId", "Full"}, {"direction", "clockwise"}, {"gateRevision", "gates-v1:" + QString(64, 'a')}};
    const auto group = lapCompatibilityGroupId(config);
    auto otherGroupConfig = config; otherGroupConfig.insert("layoutId", "Short");
    QHash<QString, QJsonObject> configs{{"a", config}, {"b", config}, {"stale", config}, {"other", otherGroupConfig}};
    OutingLapRow a; a.runId = "a"; a.runName = "Morning"; a.type = LapSectionType::Lap;
    a.start = 0; a.end = 4; a.lapNumber = 1; a.referenceEligible = true;
    a.reference = makeLapReference(a, "event", "a-source", QByteArray(64, 'a'), QByteArray(64, 'b'));
    auto b = a; b.runId = "b"; b.runName = "Afternoon"; b.end = 5; b.lapNumber = 1;
    b.reference = makeLapReference(b, "event", "b-source", QByteArray(64, 'a'), QByteArray(64, 'b'));
    auto excluded = a; excluded.runId = "a"; excluded.end = 6; excluded.lapNumber = 2;
    excluded.reference = makeLapReference(excluded, "event", "a-source", QByteArray(64, 'a'), QByteArray(64, 'b'));
    auto staleLap = a; staleLap.runId = "stale"; staleLap.end = 7; staleLap.lapNumber = 1;
    staleLap.reference = makeLapReference(staleLap, "event", "stale-source", QByteArray(64, 'a'), QByteArray(64, 'b'));
    auto otherGroup = a; otherGroup.runId = "other"; otherGroup.end = 1; otherGroup.lapNumber = 1;
    otherGroup.reference = makeLapReference(otherGroup, "event", "other-source", QByteArray(64, 'a'), QByteArray(64, 'b'));
    auto ineligible = a; ineligible.runId = "b"; ineligible.end = 8; ineligible.lapNumber = 2;
    ineligible.referenceEligible = false; ineligible.referenceIssue = LapReferenceIssue::GpsGap;
    ineligible.reference = makeLapReference(ineligible, "event", "b-source", QByteArray(64, 'a'), QByteArray(64, 'b'));
    const QVector<OutingLapRow> rows{a, b, excluded, staleLap, otherGroup, ineligible};
    const QJsonArray exclusions{QJsonObject{{"reference", excluded.reference}, {"reason", "Traffic"}}};
    const QSet<QString> staleRunIds{"stale"};

    const auto ranking = rankOutingLaps(rows, group, configs, exclusions, staleRunIds);
    const auto eligible = eligibleOutingLaps(rows, group, configs, exclusions, staleRunIds);
    QCOMPARE(eligible.size(), ranking.value("eligibleLapCount").toInt());
    QCOMPARE(eligible.size(), 2); // only a and b: excluded/stale/other-group/ineligible all withheld
    QVERIFY(std::any_of(eligible.cbegin(), eligible.cend(), [&](const auto *row) { return row->reference == a.reference; }));
    QVERIFY(std::any_of(eligible.cbegin(), eligible.cend(), [&](const auto *row) { return row->reference == b.reference; }));

    QVERIFY(eligibleOutingLaps(rows, "", configs, exclusions, staleRunIds).isEmpty());
    QVector<OutingLapRow> tooMany(maximumOutingLapRows + 1);
    QVERIFY_THROWS_EXCEPTION(ResourceLimitError, static_cast<void>(eligibleOutingLaps(tooMany, group, configs, {})));
}

void LapEligibilityTests::separatesCompatibilityGroups_data()
{
    QTest::addColumn<QString>("field"); QTest::addColumn<QJsonValue>("value");
    QTest::addColumn<QString>("reason"); QTest::addColumn<bool>("resolved");
    QTest::newRow("different-layout") << QString("layoutId") << QJsonValue("Short") << QString("changed-layout") << true;
    QTest::newRow("opposite-direction") << QString("direction") << QJsonValue("counterclockwise") << QString("opposite-direction") << true;
    QTest::newRow("different-gates") << QString("gateRevision") << QJsonValue("gates-v1:" + QString(64, 'b')) << QString("changed-timing-gate") << true;
    QTest::newRow("unknown-layout") << QString("layoutId") << QJsonValue(QJsonValue::Null) << QString("layout-unresolved") << false;
    QTest::newRow("blank-layout") << QString("layoutId") << QJsonValue(" ") << QString("layout-unresolved") << false;
    QTest::newRow("unknown-direction") << QString("direction") << QJsonValue("unknown") << QString("direction-unresolved") << false;
    QTest::newRow("unknown-gates") << QString("gateRevision") << QJsonValue(QJsonValue::Null) << QString("timing-gate-unresolved") << false;
    QTest::newRow("malformed-gates") << QString("gateRevision") << QJsonValue("gates-v1:bad") << QString("timing-gate-unresolved") << false;
}

void LapEligibilityTests::separatesCompatibilityGroups()
{
    QFETCH(QString, field); QFETCH(QJsonValue, value); QFETCH(QString, reason); QFETCH(bool, resolved);
    const QJsonObject reference{{"layoutId", "Full"}, {"direction", "clockwise"}, {"gateRevision", "gates-v1:" + QString(64, 'a')}};
    auto changed = reference; changed.insert(field, value);
    QVERIFY(!lapCompatibilityGroupId(reference).isEmpty());
    QVERIFY(lapCompatibilityGroupId(changed) != lapCompatibilityGroupId(reference));
    QCOMPARE(!lapCompatibilityGroupId(changed).isEmpty(), resolved);
    QCOMPARE(lapCompatibilityReasons(changed, reference), QStringList{reason});
    QCOMPARE(lapCompatibilityReasons(reference, changed), QStringList{reason});
    if (!resolved) QVERIFY(!lapCompatibilityReasons(changed, changed).isEmpty());
}

void LapEligibilityTests::preservesIndependentCompatibilityReasons()
{
    const QJsonObject reference{{"layoutId", "Full"}, {"direction", "clockwise"}, {"gateRevision", "gates-v1:" + QString(64, 'a')}};
    auto anotherSource = reference; anotherSource.insert("sourceId", "another"); anotherSource.insert("sourceFingerprint", QJsonObject{{"changed", true}});
    QCOMPARE(lapCompatibilityGroupId(reference), lapCompatibilityGroupId(anotherSource));
    QVERIFY(lapCompatibilityReasons(reference, anotherSource).isEmpty());
    const QJsonObject changed{{"layoutId", "Short"}, {"direction", "counterclockwise"}, {"gateRevision", "gates-v1:" + QString(64, 'b')}};
    QCOMPARE(lapCompatibilityReasons(changed, reference, LapReferenceIssue::GpsGap, true),
        (QStringList{"changed-layout", "opposite-direction", "changed-timing-gate", "incomplete-gps", "user-exclusion"}));
    QCOMPARE(lapCompatibilityReasons(reference, reference, LapReferenceIssue::InvalidGps, true),
        (QStringList{"invalid-gps", "user-exclusion"}));
    QVERIFY(lapCompatibilityGroupId({}).isEmpty());
    QCOMPARE(lapCompatibilityReasons({}, {}), (QStringList{"layout-unresolved", "direction-unresolved", "timing-gate-unresolved"}));
}

void LapEligibilityTests::preservesTimingWhenDensifyingFixture()
{
    const auto sparse = deriveSourceLapSession(
        VboParser::parse(QString::fromUtf8(EventProjectFixture::lapsVbo())));
    const auto dense = deriveSourceLapSession(interiorGapFixture());
    QCOMPARE(dense.acceptedPasses.size(), sparse.acceptedPasses.size());
    QCOMPARE(dense.timedLaps.size(), 3);
    QCOMPARE(dense.lapTraces.size(), 3);
    for (qsizetype index = 0; index < sparse.timedLaps.size(); ++index) {
        QVERIFY(qAbs(dense.timedLaps[index].startTelemetryTime
                     - sparse.timedLaps[index].startTelemetryTime) < 0.001);
        QVERIFY(qAbs(dense.timedLaps[index].endTelemetryTime
                     - sparse.timedLaps[index].endTelemetryTime) < 0.001);
        QVERIFY(dense.timedLaps[index].referenceEligible());
    }
}

void LapEligibilityTests::retainsMeasuredLapWithInteriorGpsGap()
{
    auto session = interiorGapFixture();
    for (const auto &name : {QStringLiteral("latitude"), QStringLiteral("longitude")}) {
        auto &channel = session.channels[session.aliases.value(name)];
        auto times = channel.timestamps();
        for (auto &time : times) if (time >= 3.5) time += 10.0;
        channel.setSamples(times, channel.values());
    }
    session.duration += 10.0;
    const auto laps = deriveSourceLapSession(session);
    QCOMPARE(laps.timedLaps.size(), 3);
    QVERIFY(laps.timedLaps[0].durationSeconds > 13.9);
    QCOMPARE(laps.timedLaps[0].referenceIssue, LapReferenceIssue::GpsGap);
    QVERIFY(!laps.timedLaps[0].referenceEligible());
    QCOMPARE(laps.lapTraces.size(), 2);
    for (const auto &trace : laps.lapTraces) QVERIFY(trace.lapNumber != 1);
    QVERIFY(laps.fastestLapIndex.has_value());
    QVERIFY(laps.timedLaps[*laps.fastestLapIndex].referenceEligible());
}

void LapEligibilityTests::rejectsInvalidAndUnpairedCoordinates_data()
{
    QTest::addColumn<int>("kind");
    QTest::newRow("missing-coordinate") << 0;
    QTest::newRow("out-of-range-coordinate") << 1;
    QTest::newRow("unaligned-coordinate-clock") << 2;
}

void LapEligibilityTests::rejectsInvalidAndUnpairedCoordinates()
{
    QFETCH(int, kind);
    auto session = interiorGapFixture();
    auto &latitude = session.channels[session.aliases.value("latitude")];
    auto &longitude = session.channels[session.aliases.value("longitude")];
    const auto interior = latitude.timestamps().indexOf(3.5);
    QVERIFY(interior >= 0);
    if (kind == 0) latitude.setValue(interior, std::numeric_limits<float>::quiet_NaN());
    if (kind == 1) latitude.setValue(interior, 100.0F);
    if (kind == 2) {
        auto times = longitude.timestamps();
        times[interior] += 0.01;
        longitude.setSamples(times, longitude.values());
    }
    const auto laps = deriveSourceLapSession(session);
    QCOMPARE(laps.timedLaps.size(), 3);
    QCOMPARE(laps.timedLaps[0].referenceIssue, LapReferenceIssue::InvalidGps);
    QCOMPARE(laps.lapTraces.size(), 2);
    QVERIFY(laps.fastestLapIndex.has_value());
    QVERIFY(*laps.fastestLapIndex != 0);
}

void LapEligibilityTests::exposesNoBestWhenEveryLapHasMissingGps()
{
    auto session = interiorGapFixture();
    auto &latitude = session.channels[session.aliases.value("latitude")];
    for (double time : {3.5, 7.5, 12.5}) {
        const auto index = latitude.timestamps().indexOf(time);
        QVERIFY(index >= 0);
        latitude.setValue(index, std::numeric_limits<float>::quiet_NaN());
    }
    const auto laps = deriveSourceLapSession(session);
    QCOMPARE(laps.timedLaps.size(), 3);
    QVERIFY(!laps.fastestLapIndex.has_value());
    QVERIFY(laps.lapTraces.isEmpty());
    const auto rows = outingLapRows(session, laps, "run", "Run", 0);
    QCOMPARE(rows.size(), 5);
    for (const auto &row : rows) {
        QVERIFY(!row.bestOfRun);
        QVERIFY(!row.referenceEligible);
        if (row.type == LapSectionType::Lap)
            QCOMPARE(row.referenceIssue, LapReferenceIssue::InvalidGps);
    }
}

void LapEligibilityTests::rendererNeverUsesIneligibleCompletedLap()
{
    auto session = interiorGapFixture();
    auto &latitude = session.channels[session.aliases.value("latitude")];
    const auto interior = latitude.timestamps().indexOf(3.5);
    QVERIFY(interior >= 0);
    latitude.setValue(interior, std::numeric_limits<float>::quiet_NaN());
    const auto laps = deriveSourceLapSession(session);
    TelemetryRenderContext context;
    context.setSession(&session);
    context.setLapSession(laps);
    context.setTime(6.0);
    const auto first = context.lapTiming();
    QCOMPARE(first.value("lastLapNumber").toInt(), 1);
    QVERIFY(!first.contains("bestLapNumber"));
    QVERIFY(!first.contains("liveDeltaSeconds"));
    QVERIFY(!first.contains("lastDeltaToBestSeconds"));
    context.setTime(11.0);
    QCOMPARE(context.lapTiming().value("bestLapNumber").toInt(), 2);
}

void LapEligibilityTests::holdsAChosenHotlap()
{
    // Hotlap mode of the current-lap tile: one chosen lap only. 0:00 before
    // its start-line crossing, running during the lap, and its final time
    // held afterwards, through every later lap. Lap 0 means the best lap.
    auto session = interiorGapFixture();
    const auto laps = deriveSourceLapSession(session);
    QVERIFY(laps.timedLaps.size() >= 2);
    TelemetryRenderContext context;
    context.setSession(&session);
    context.setLapSession(laps);
    const TimedLap *best = nullptr;
    for (const auto &lap : laps.timedLaps)
        if (lap.referenceEligible() && (!best || lap.durationSeconds < best->durationSeconds)) best = &lap;
    QVERIFY(best);
    for (const int chosen : {0, best->number, laps.timedLaps.first().number}) {
        const auto &lap = chosen == 0 ? *best : *std::find_if(laps.timedLaps.cbegin(), laps.timedLaps.cend(),
            [chosen](const TimedLap &candidate) { return candidate.number == chosen; });
        context.setTime(lap.startTelemetryTime - 0.5);
        auto timing = context.fixedLapTiming(chosen);
        QCOMPARE(timing.value("lapNumber").toInt(), lap.number);
        QCOMPARE(timing.value("state").toString(), QString("before"));
        QCOMPARE(timing.value("elapsedSeconds").toDouble(), 0.0);
        context.setTime(lap.startTelemetryTime + 1.0);
        timing = context.fixedLapTiming(chosen);
        QCOMPARE(timing.value("state").toString(), QString("running"));
        QVERIFY(std::abs(timing.value("elapsedSeconds").toDouble() - 1.0) < 1e-9);
        // After the lap, and still after every later lap: the final time.
        for (const double after : {lap.endTelemetryTime, lap.endTelemetryTime + 0.5, laps.timedLaps.last().endTelemetryTime + 5.0}) {
            context.setTime(after);
            timing = context.fixedLapTiming(chosen);
            QCOMPARE(timing.value("state").toString(), QString("finished"));
            QCOMPARE(timing.value("elapsedSeconds").toDouble(), lap.durationSeconds);
        }
        QCOMPARE(timing.value("isBest").toBool(), lap.number == best->number);
    }
    QCOMPARE(context.fixedLapTiming(99).value("state").toString(), QString("unavailable"));
    TelemetryRenderContext empty;
    QCOMPARE(empty.fixedLapTiming(0).value("state").toString(), QString("unavailable"));
}

void LapEligibilityTests::propagatesOutingEligibilityAndBest()
{
    auto session = interiorGapFixture();
    auto &latitude = session.channels[session.aliases.value("latitude")];
    const auto interior = latitude.timestamps().indexOf(3.5);
    QVERIFY(interior >= 0);
    latitude.setValue(interior, std::numeric_limits<float>::quiet_NaN());
    const auto laps = deriveSourceLapSession(session);
    const auto rows = outingLapRows(session, laps, "run", "Run", 0);
    QCOMPARE(rows.size(), 5);
    QVERIFY(!rows[1].referenceEligible);
    QCOMPARE(rows[1].referenceIssue, LapReferenceIssue::InvalidGps);
    QVERIFY(!rows[1].bestOfRun);
    QCOMPARE(rows[3].lapNumber, 3);
    QVERIFY(rows[3].referenceEligible);
    QVERIFY(rows[3].bestOfRun);
    QVERIFY(!rows.first().bestOfRun);
    QVERIFY(!rows.last().bestOfRun);
}

void LapEligibilityTests::preservesNormalLapRanking()
{
    const auto session = VboParser::parse(QString::fromUtf8(EventProjectFixture::lapsVbo()));
    const auto laps = deriveSourceLapSession(session);
    QCOMPARE(laps.timedLaps.size(), 3);
    QCOMPARE(laps.lapTraces.size(), 3);
    QCOMPARE(laps.fastestLapIndex, std::optional<qsizetype>(0));
    for (const auto &lap : laps.timedLaps) QVERIFY(lap.referenceEligible());
    QVERIFY(qAbs(laps.timedLaps[1].deltaToBestSeconds - 1.0) < 0.001);
    const auto rows = outingLapRows(session, laps, "run", "Run", 0);
    QVERIFY(rows[1].bestOfRun);
    QVERIFY(!rows[2].bestOfRun);
    QVERIFY(!rows[3].bestOfRun);
}

void LapEligibilityTests::rendererOmitsDeltaAtMissingCurrentCoordinate()
{
    auto session = interiorGapFixture();
    auto &latitude = session.channels[session.aliases.value("latitude")];
    const auto interior = latitude.timestamps().indexOf(12.5);
    QVERIFY(interior >= 0);
    latitude.setValue(interior, std::numeric_limits<float>::quiet_NaN());
    const auto laps = deriveSourceLapSession(session);
    TelemetryRenderContext context;
    context.setSession(&session);
    context.setLapSession(laps);
    context.setTime(12.5);
    const auto state = context.lapTiming();
    QCOMPARE(state.value("bestLapNumber").toInt(), 1);
    QVERIFY(!state.contains("liveDeltaSeconds"));
    QVERIFY(!state.contains("referenceSpeedKmh"));
}

QTEST_GUILESS_MAIN(LapEligibilityTests)
#include "LapEligibilityTests.moc"
