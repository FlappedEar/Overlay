// The day-analysis pipeline in telemetry core alone (KAN-124): deriving laps,
// ranking and channel summaries without AppController, as the Telemetry app
// will. Links only flappedear_telemetry_core.

#include "EventProjectFixture.h"
#include "project/ProjectSourceReference.h"
#include "telemetry/OutingChannelSummaries.h"
#include "telemetry/LapTiming.h"
#include "telemetry/OutingLapDerivation.h"
#include "telemetry/TelemetrySource.h"

#include <QCryptographicHash>
#include <QTemporaryDir>
#include <QtTest>

using namespace FlappedEar;

namespace {
bool writeFile(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

// A run source as the project provides it: identity, reference with the
// recording's fingerprint, and a derivation key.
QJsonObject runSource(const QString &runId, const QString &name, const QString &path)
{
    const auto session = TelemetrySource::load(path);
    return {{"eventId", "event"}, {"runId", runId}, {"name", name}, {"sourceId", runId + "-source"},
        {"reference", QJsonObject{{"absolutePath", path}, {"fingerprint", ProjectSourceReferenceCodec::telemetryFingerprint(path, session)}}},
        {"derivationKey", QString::fromLatin1(QCryptographicHash::hash("pipeline-test", QCryptographicHash::Sha256).toHex())},
        // Recorded at import: the timing gate the laps were derived with.
        {"trackConfiguration", QJsonObject{{"gateRevision", timingGateRevision(session)}}}};
}
}

class OutingPipelineTests final : public QObject {
    Q_OBJECT
private slots:
    void derivesRanksAndSummarizesWithoutTheApplication();
    void reportsAMissingRecordingAndKeepsTheOthers();
    void cancels();
    void choosesTheComparisonGroup();
};

void OutingPipelineTests::derivesRanksAndSummarizesWithoutTheApplication()
{
    QTemporaryDir directory; QVERIFY(directory.isValid());
    const auto first = directory.filePath("first.vbo"), second = directory.filePath("second.vbo");
    QVERIFY(writeFile(first, EventProjectFixture::routeVbo()));
    QVERIFY(writeFile(second, EventProjectFixture::routeVbo()));
    const QJsonArray sources{runSource("r1", "Session 1", first), runSource("r2", "Session 2", second)};
    const auto cancellation = std::make_shared<std::atomic_bool>(false);

    const auto derived = deriveOutingLaps(sources, {}, {}, cancellation);
    QVERIFY(!derived.cancelled);
    for (const auto &message : derived.messages) QVERIFY2(message.state != "error", qPrintable(message.text));
    QVERIFY(derived.rows.size() >= 6);
    QCOMPARE(derived.runs.size(), 2);
    for (const auto &row : derived.rows) QVERIFY(validLapReference(row.reference));
    // Both recordings of the same route land in one compatibility group.
    const auto group = lapCompatibilityGroupId(derived.groups.configurations.value("r1"));
    QVERIFY(!group.isEmpty());
    QCOMPARE(lapCompatibilityGroupId(derived.groups.configurations.value("r2")), group);

    const auto ranking = rankOutingLaps(derived.rows, group, derived.groups.configurations, {});
    QCOMPARE(ranking.value("state").toString(), QString("available"));
    QVERIFY(validLapReference(ranking.value("bestOfDay").toObject().value("reference").toObject()));

    // An unchanged run is reused, not derived again.
    const auto again = deriveOutingLaps(sources, {}, derived.runs, cancellation);
    QCOMPARE(again.rows.size(), derived.rows.size());
    QCOMPARE(again.runs.value("r1").derivationSerial, derived.runs.value("r1").derivationSerial);
    QCOMPARE(again.runs.value("r1").rows.first().reference, derived.runs.value("r1").rows.first().reference);

    // Channel summaries from the same rows: the route has no temperature or
    // heart-rate channel, and none is invented.
    QHash<QString, QJsonObject> byRun;
    for (const auto &value : sources) byRun.insert(value.toObject().value("runId").toString(), value.toObject());
    const auto summaries = summarizeOutingChannels(derived.rows, byRun, {}, 1, cancellation);
    QVERIFY2(summaries.error.isEmpty(), qPrintable(summaries.error));
    QCOMPARE(summaries.runs.size(), 2);
    for (const auto &value : summaries.runs) {
        const auto run = value.toMap();
        QVERIFY(!run.contains("unavailableReason"));
        QVERIFY(run.value("channels").toList().isEmpty());
        QVERIFY(!run.contains("heartRate"));
    }
}

void OutingPipelineTests::reportsAMissingRecordingAndKeepsTheOthers()
{
    QTemporaryDir directory; QVERIFY(directory.isValid());
    const auto present = directory.filePath("present.vbo"), gone = directory.filePath("gone.vbo");
    QVERIFY(writeFile(present, EventProjectFixture::routeVbo()));
    QVERIFY(writeFile(gone, EventProjectFixture::routeVbo()));
    const QJsonArray sources{runSource("r1", "Session 1", present), runSource("r2", "Session 2", gone)};
    QVERIFY(QFile::remove(gone));
    const auto derived = deriveOutingLaps(sources, {}, {}, std::make_shared<std::atomic_bool>(false));
    QVERIFY(!derived.cancelled);
    QVERIFY(derived.runs.contains("r1") && !derived.runs.contains("r2"));
    bool missing = false;
    for (const auto &message : derived.messages)
        missing = missing || (message.runId == "r2" && message.state == "missing-source");
    QVERIFY(missing);
    for (const auto &row : derived.rows) QCOMPARE(row.runId, QString("r1"));
    // A tampered fingerprint is refused rather than silently accepted.
    auto tampered = runSource("r3", "Session 3", present);
    auto reference = tampered.value("reference").toObject();
    auto fingerprint = reference.value("fingerprint").toObject();
    fingerprint.insert("sampleCount", 1);
    reference.insert("fingerprint", fingerprint);
    tampered.insert("reference", reference);
    const auto refused = deriveOutingLaps(QJsonArray{tampered}, {}, {}, std::make_shared<std::atomic_bool>(false));
    QVERIFY(refused.rows.isEmpty());
    QVERIFY(!refused.messages.isEmpty());
}

void OutingPipelineTests::cancels()
{
    QTemporaryDir directory; QVERIFY(directory.isValid());
    const auto path = directory.filePath("run.vbo");
    QVERIFY(writeFile(path, EventProjectFixture::routeVbo()));
    const auto cancellation = std::make_shared<std::atomic_bool>(true);
    const auto derived = deriveOutingLaps(QJsonArray{runSource("r1", "Session 1", path)}, {}, {}, cancellation);
    QVERIFY(derived.cancelled);
    QVERIFY(derived.rows.isEmpty());
}

void OutingPipelineTests::choosesTheComparisonGroup()
{
    // KAN-185: the editor's automatic best lap and the analysis rank in the
    // same group: a saved choice while it still exists, else the first
    // resolved group with a current run.
    const auto configuration = [](const QString &layout) {
        return QJsonObject{{"layoutId", layout}, {"direction", "clockwise"}, {"gateRevision", "gates-v1:" + QString(64, QLatin1Char('a'))}};
    };
    const QHash<QString, QJsonObject> configurations{{"a", configuration("Full")}, {"b", configuration("Short")},
        {"c", QJsonObject{{"direction", "clockwise"}}}};
    const auto full = lapCompatibilityGroupId(configurations.value("a"));
    const auto shortLayout = lapCompatibilityGroupId(configurations.value("b"));
    QVERIFY(!full.isEmpty() && !shortLayout.isEmpty() && full != shortLayout);
    QVERIFY(lapCompatibilityGroupId(configurations.value("c")).isEmpty());
    QVector<OutingLapRow> rows;
    for (const auto *run : {"a", "b", "c"}) { OutingLapRow row; row.runId = run; rows.append(row); }
    const auto first = std::min(full, shortLayout), last = std::max(full, shortLayout);
    QCOMPARE(outingComparisonGroup(rows, configurations, {}), first);
    QCOMPARE(outingComparisonGroup(rows, configurations, last), last);
    QCOMPARE(outingComparisonGroup(rows, configurations, "compatibility-v1:gone"), QString());
    QCOMPARE(outingComparisonGroup(rows, configurations, "unresolved:c"), QString());
    // A group whose only run is stale is skipped automatically, but a saved
    // choice of it stands.
    const auto staleRun = first == full ? QString("a") : QString("b");
    QCOMPARE(outingComparisonGroup(rows, configurations, {}, {staleRun}), last);
    QCOMPARE(outingComparisonGroup(rows, configurations, first, {staleRun}), first);
    QCOMPARE(outingComparisonGroup({}, configurations, {}), QString());

    // Inferred configurations win over the saved ones.
    const QJsonArray sources{QJsonObject{{"runId", "a"}, {"trackConfiguration", configuration("Full")}},
        QJsonObject{{"runId", "b"}, {"trackConfiguration", configuration("Short")}}};
    InferredTrackGroups groups;
    groups.configurations.insert("b", configuration("Inferred"));
    const auto resolved = outingRunConfigurations(sources, groups);
    QCOMPARE(resolved.size(), 2);
    QCOMPARE(resolved.value("a"), configuration("Full"));
    QCOMPARE(resolved.value("b"), configuration("Inferred"));
}

QTEST_GUILESS_MAIN(OutingPipelineTests)
#include "OutingPipelineTests.moc"
