// Areas to inspect next (native/src/telemetry/FocusAreas.*, KAN-73): chosen
// from computed observations only, observation separate from hypothesis,
// no causal or "brake later" claims, evidence on every area.

#include "telemetry/FocusAreas.h"

#include <QtTest>

using namespace FlappedEar;

namespace {
QJsonObject lap(const QString &name) { return {{"runId", name}}; }

FocusLoss loss(const QString &segment, const double seconds, const QString &lapName)
{
    return {segment, "Corner " + segment, seconds, lap(lapName)};
}

CornerLapObservation braking(const double meters, const QString &provenance, const QString &lapName, const double minimumSpeed = 80.0)
{
    CornerLapObservation observation;
    observation.lapReference = lap(lapName);
    observation.brakingPointMeters = meters;
    observation.brakingProvenance = provenance;
    observation.minimumSpeed = minimumSpeed;
    return observation;
}

// Words that would turn an observation into advice or a causal claim.
void verifyNoAdviceWording(const FocusArea &area)
{
    for (const auto &text : {area.observation, area.hypothesis}) {
        for (const auto *word : {"brake later", "should", "because you", "you must", "caused", "guarantee"})
            QVERIFY2(!text.contains(QLatin1String(word), Qt::CaseInsensitive), qPrintable(text));
    }
    QVERIFY(!area.observation.isEmpty() && !area.hypothesis.isEmpty());
    QVERIFY(!area.lap.isEmpty() && !area.against.isEmpty());
}
}

class FocusAreasTests final : public QObject {
    Q_OBJECT
private slots:
    void selectsOneOfEachKindFirst();
    void requiresRepeatsAndThresholds();
    void keepsMeasuredBrakingApartAndMakesNoBrakingClaim();
    void isShortDeterministicAndEmptyWithoutObservations();
};

void FocusAreasTests::selectsOneOfEachKindFirst()
{
    FocusInputs inputs;
    inputs.referenceLap = lap("best");
    inputs.referenceLabel = "Session 5 · LAP 2";
    inputs.comparedLapCount = 5;
    inputs.gaps = {{"7", "Corner 7", 0.412, lap("best"), "Session 5 · LAP 2", lap("s3l2"), "Session 3 · LAP 2"},
        {"8", "Corner 8", 0.2, lap("best"), "Session 5 · LAP 2", lap("s4l1"), "Session 4 · LAP 1"}};
    // Corner 2 loses on four of five laps (median 0.3); corner 7 is taken by the gap.
    inputs.losses = {loss("2", 0.25, "a"), loss("2", 0.30, "b"), loss("2", 0.31, "c"), loss("2", 0.6, "d"),
        loss("7", 0.5, "a"), loss("7", 0.5, "b"), loss("7", 0.5, "c")};
    inputs.corners = {{"4", "Corner 4", {braking(100, "measured", "a"), braking(120, "measured", "b"),
        braking(140, "measured", "c"), braking(160, "measured", "d")}}};
    const auto areas = selectFocusAreas(inputs);
    QCOMPARE(areas.size(), 3);
    QCOMPARE(areas[0].kind, QString("sectorGap"));
    QCOMPARE(areas[0].segmentId, QString("7"));
    QVERIFY(areas[0].observation.contains("0.412 s"));
    QVERIFY(areas[0].observation.contains("Session 3 · LAP 2"));
    QCOMPARE(areas[0].against, lap("s3l2"));
    QCOMPARE(areas[1].kind, QString("repeatedLoss"));
    QCOMPARE(areas[1].segmentId, QString("2")); // corner 7 already has an area
    QCOMPARE(areas[1].sampleCount, 4);
    QVERIFY2(areas[1].observation.contains("In 4 of 5 compared laps"), qPrintable(areas[1].observation));
    QCOMPARE(areas[1].against, inputs.referenceLap);
    QVERIFY(areas[1].lap == lap("b") || areas[1].lap == lap("c")); // the typical lap, near the median
    QCOMPARE(areas[2].kind, QString("brakingSpread"));
    for (const auto &area : areas) verifyNoAdviceWording(area);
}

void FocusAreasTests::requiresRepeatsAndThresholds()
{
    FocusInputs inputs;
    inputs.referenceLap = lap("best");
    inputs.referenceLabel = "best";
    inputs.comparedLapCount = 5;
    // Two losses are not a pattern; tiny losses and gaps are not areas.
    inputs.losses = {loss("2", 0.4, "a"), loss("2", 0.4, "b"), loss("3", 0.01, "a"), loss("3", 0.02, "b"), loss("3", 0.03, "c")};
    inputs.gaps = {{"9", "Corner 9", 0.01, lap("best"), "best", lap("x"), "x"}};
    inputs.corners = {{"4", "Corner 4", {braking(100, "measured", "a"), braking(102, "measured", "b"), braking(104, "measured", "c")}}};
    QVERIFY(selectFocusAreas(inputs).isEmpty());
}

void FocusAreasTests::keepsMeasuredBrakingApartAndMakesNoBrakingClaim()
{
    FocusInputs inputs;
    // Inferred braking points spread widely but are never mixed with measured ones.
    inputs.corners = {{"4", "Corner 4", {braking(100, "measured", "a"), braking(101, "measured", "b"), braking(102, "measured", "c"),
        braking(40, "inferred", "d"), braking(200, "inferred", "e")}}};
    QVERIFY(selectFocusAreas(inputs).isEmpty());
    inputs.corners = {{"4", "Corner 4", {braking(100, "measured", "early"), braking(115, "measured", "b"),
        braking(130, "measured", "c"), braking(150, "measured", "late")}}};
    const auto areas = selectFocusAreas(inputs);
    QCOMPARE(areas.size(), 1);
    QCOMPARE(areas[0].unit, QString("m"));
    QVERIFY(areas[0].observation.contains("measured from the brake signal"));
    QVERIFY(areas[0].hypothesis.contains("does not show whether earlier or later braking is faster or safe"));
    QCOMPARE(areas[0].lap, lap("early"));
    QCOMPARE(areas[0].against, lap("late"));
    verifyNoAdviceWording(areas[0]);
    // A minimum-speed spread (8 of 80 km/h) with a steady braking point.
    inputs.speedUnit = "km/h";
    inputs.corners = {{"5", "Corner 5", {braking(100, "measured", "a", 70), braking(101, "measured", "b", 78),
        braking(102, "measured", "c", 86), braking(103, "measured", "d", 94)}}};
    const auto speed = selectFocusAreas(inputs);
    QCOMPARE(speed.size(), 1);
    QCOMPARE(speed[0].kind, QString("minimumSpeedSpread"));
    QVERIFY(speed[0].observation.contains("km/h"));
    QVERIFY(speed[0].hypothesis.contains("not by itself better"));
    verifyNoAdviceWording(speed[0]);
}

void FocusAreasTests::isShortDeterministicAndEmptyWithoutObservations()
{
    QVERIFY(selectFocusAreas({}).isEmpty());
    FocusInputs inputs;
    inputs.referenceLap = lap("best");
    for (int i = 0; i < 8; ++i)
        inputs.gaps.append({QString::number(i), QString("Corner %1").arg(i), 0.1 + 0.01 * i, lap("best"), "best", lap("x"), "x"});
    const auto areas = selectFocusAreas(inputs);
    QCOMPARE(areas.size(), 3);
    QCOMPARE(areas[0].segmentId, QString("7")); // largest gap first
    QCOMPARE(areas[1].segmentId, QString("6"));
    QCOMPARE(selectFocusAreas(inputs, 5).size(), 5);
    // Equal scores order by segment id, whatever the input order.
    FocusInputs tied;
    tied.gaps = {{"b", "B", 0.2, lap("best"), "best", lap("x"), "x"}, {"a", "A", 0.2, lap("best"), "best", lap("x"), "x"}};
    QCOMPARE(selectFocusAreas(tied)[0].segmentId, QString("a"));
}

QTEST_GUILESS_MAIN(FocusAreasTests)
#include "FocusAreasTests.moc"
