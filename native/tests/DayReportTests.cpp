// The computed day report (native/src/telemetry/DayReport.*, KAN-71):
// provenance on every result, stale results never show a value, and a report
// read from elsewhere is validated and bounded.

#include "telemetry/DayReport.h"

#include <QtTest>
#include <stdexcept>

using namespace FlappedEar;

namespace {
DayReportResult bestLap(const QByteArray &key)
{
    DayReportResult result;
    result.id = "bestLap";
    result.algorithm = "outing-ranking-v1";
    result.status = DayResultStatus::Available;
    result.range = {{"scope", "day"}, {"groupId", "g1"}, {"eligibleLapCount", 12}};
    result.value = {{"seconds", 109.898}, {"label", "Session 5 · LAP 2"}};
    result.evidence = {QJsonObject{{"kind", "lap"}, {"reference", QJsonObject{{"runId", "r5"}}}}};
    result.decisionsKey = key;
    return result;
}
}

class DayReportTests final : public QObject {
    Q_OBJECT
private slots:
    void carriesProvenanceForEveryResult();
    void dropsValuesComputedUnderOtherDecisions();
    void rejectsMalformedInput();
    void validatesReadReports();
};

void DayReportTests::carriesProvenanceForEveryResult()
{
    DayReportInput input{"event", "g1", "Group 1", "key-1", {}};
    input.results.append(bestLap("key-1"));
    DayReportResult pending;
    pending.id = "theoreticalBest";
    pending.algorithm = "theoretical-best-v1";
    pending.status = DayResultStatus::NotComputed;
    pending.reason = "Not calculated yet.";
    pending.range = {{"scope", "day"}, {"groupId", "g1"}};
    input.results.append(pending);
    const auto report = buildDayReport(input);
    QCOMPARE(report.value("schema").toString(), QString(dayReportSchema));
    QCOMPARE(report.value("version").toInt(), dayReportSchemaVersion);
    QCOMPARE(report.value("decisionsKey").toString(), QString(QByteArray("key-1").toHex()));
    const auto results = report.value("results").toArray();
    QCOMPARE(results.size(), 2);
    const auto best = results[0].toObject();
    QCOMPARE(best.value("status").toString(), QString("available"));
    QCOMPARE(best.value("algorithm").toString(), QString("outing-ranking-v1"));
    QCOMPARE(best.value("range").toObject().value("eligibleLapCount").toInt(), 12);
    QCOMPARE(best.value("evidence").toArray().size(), 1);
    QVERIFY(std::abs(best.value("value").toObject().value("seconds").toDouble() - 109.898) < 1e-9);
    const auto theoretical = results[1].toObject();
    QCOMPARE(theoretical.value("status").toString(), QString("notComputed"));
    QVERIFY(!theoretical.contains("value"));
    QCOMPARE(theoretical.value("reason").toString(), QString("Not calculated yet."));
    QCOMPARE(validateDayReport(report), QString());
}

void DayReportTests::dropsValuesComputedUnderOtherDecisions()
{
    // A lap was excluded after the ranking ran: the old number must not show.
    DayReportInput input{"event", "g1", "Group 1", "key-2", {bestLap("key-1")}};
    auto result = buildDayReport(input).value("results").toArray().first().toObject();
    QCOMPARE(result.value("status").toString(), QString("stale"));
    QVERIFY(!result.contains("value"));
    QVERIFY(result.value("evidence").toArray().isEmpty());
    QVERIFY(!result.value("reason").toString().isEmpty());
    // A result that does not depend on the decisions is never stale by them.
    auto independent = bestLap({});
    independent.id = "temperatures";
    input.results = {independent};
    result = buildDayReport(input).value("results").toArray().first().toObject();
    QCOMPARE(result.value("status").toString(), QString("available"));
}

void DayReportTests::rejectsMalformedInput()
{
    DayReportInput input{"event", "g1", "Group 1", "k", {bestLap("k"), bestLap("k")}};
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, [&] { (void)buildDayReport(input); }());
    auto noAlgorithm = bestLap("k");
    noAlgorithm.algorithm.clear();
    input.results = {noAlgorithm};
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, [&] { (void)buildDayReport(input); }());
    auto badEvidence = bestLap("k");
    badEvidence.evidence = {QJsonObject{{"reference", QJsonObject{}}}};
    input.results = {badEvidence};
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, [&] { (void)buildDayReport(input); }());
}

void DayReportTests::validatesReadReports()
{
    const auto good = buildDayReport(DayReportInput{"event", "g1", "Group 1", "k", {bestLap("k")}});
    QCOMPARE(validateDayReport(good), QString());
    QVERIFY(!validateDayReport({}).isEmpty());
    auto wrongVersion = good; wrongVersion.insert("version", 99);
    QVERIFY(!validateDayReport(wrongVersion).isEmpty());
    auto notArray = good; notArray.insert("results", "x");
    QVERIFY(!validateDayReport(notArray).isEmpty());
    // A value on a result that is not available.
    auto results = good.value("results").toArray();
    auto entry = results[0].toObject(); entry.insert("status", "stale"); results[0] = entry;
    auto mismatched = good; mismatched.insert("results", results);
    QVERIFY(!validateDayReport(mismatched).isEmpty());
    // Unknown status and unknown evidence kind.
    entry = good.value("results").toArray()[0].toObject(); entry.insert("status", "guessed");
    auto unknown = good; unknown.insert("results", QJsonArray{entry});
    QVERIFY(!validateDayReport(unknown).isEmpty());
    entry = good.value("results").toArray()[0].toObject(); entry.insert("evidence", QJsonArray{QJsonObject{{"kind", "video"}}});
    auto kind = good; kind.insert("results", QJsonArray{entry});
    QVERIFY(!validateDayReport(kind).isEmpty());
    // Bounded: too many results or too much evidence is refused before use.
    QJsonArray many;
    for (qsizetype i = 0; i <= maximumDayReportResults; ++i) {
        auto item = good.value("results").toArray()[0].toObject(); item.insert("id", QString("r%1").arg(i)); many.append(item);
    }
    auto large = good; large.insert("results", many);
    QVERIFY(!validateDayReport(large).isEmpty());
    QJsonArray evidence;
    for (qsizetype i = 0; i <= maximumDayReportEvidence; ++i) evidence.append(QJsonObject{{"kind", "lap"}});
    entry = good.value("results").toArray()[0].toObject(); entry.insert("evidence", evidence);
    auto heavy = good; heavy.insert("results", QJsonArray{entry});
    QVERIFY(!validateDayReport(heavy).isEmpty());
}

QTEST_GUILESS_MAIN(DayReportTests)
#include "DayReportTests.moc"
