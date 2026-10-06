#include "EventProjectFixture.h"
#include "project/EventProjectCodec.h"
#include "project/ProjectLimits.h"
#include "project/VideoChapters.h"
#include "project/ProjectRecoveryStore.h"
#include "export/ExportOutputTransaction.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QJsonArray>
#include <QtTest>
#include <functional>
#include <utility>
#include <limits>
#include "telemetry/TelemetrySession.h"
#include "telemetry/LapTiming.h"
#include "telemetry/VboParser.h"

using namespace FlappedEar;
namespace Fixture = EventProjectFixture;

class EventProjectTests final : public QObject {
    Q_OBJECT
private slots:
    void acceptsLegacyAndEventDocuments();
    void acceptsAnalysisOnlyEventDocuments();
    void boundsAndPreservesAnalysisDecisions();
    void bindsFullContentWithoutMigratingOnLoad();
    void boundsInferenceProvenanceAndRecoversIt();
    void boundsAndPreservesRunMetadata();
    void boundsSourceFusionAndRecoversIt();
    void keepsNewerVersionsOfKnownFields();
    void boundsReferenceContentIdentity();
    void boundsAndRebasesVideoChapters();
    void persistsTrackConfigurationAndUnknownLegacyState();
    void rejectsInvalidTrackConfigurations_data();
    void rejectsInvalidTrackConfigurations();
    void invalidatesDerivationOnConfigurationAndSourceChanges();
    void bindsPrimaryTelemetryFromTheDocument();
    void clearsConfigurationOnSourceReplacementOnly();
    void revisionsReflectPhysicalGatesWithoutInferringDirection();
    void preservesFiniteExtremeSyncForGuardedConsumers();
    void rejectsMalformedEvents_data();
    void rejectsMalformedEvents();
    void boundsRunsAndSources();
    void rebasesInactiveAlternativeAndMissingReferences();
    void prefersMovedRelativeSourceToStaleAbsoluteFallback();
    void rejectsDeepTraversalOnSourceResolve();
    void retainsEventRecoveryIdentity();
    void protectsInactiveSourcesFromExportOverwrite();
    void keepsMissingReferencesPortableThroughDirectorySymlinks();
};

void EventProjectTests::acceptsLegacyAndEventDocuments()
{
    const QJsonObject event = Fixture::project();
    QString error;
    QVERIFY2(ProjectLimits::validateProject(event, &error), qPrintable(error));
    const QJsonObject editor = EventProjectCodec::editorProjection(event);
    QVERIFY(ProjectLimits::validateProject(editor, &error));
    QVERIFY(!editor.contains("event"));
    QCOMPARE(editor.value("version").toInt(), 2);
    QCOMPARE(editor.value("sync").toObject().value("offset").toDouble(), 2.5);
    QCOMPARE(editor.value("sources").toObject().value("telemetry").toObject(), Fixture::reference(Fixture::runs(event)[0].toObject()));
    QCOMPARE(EventProjectCodec::editorProjection(editor), editor);
    QCOMPARE(EventProjectCodec::withEditorState(event, editor, {}, {}), event);
    QFile example(QStringLiteral(EVENT_PROJECT_FIXTURE_PATH));
    QVERIFY(example.open(QIODevice::ReadOnly));
    const auto sample = QJsonDocument::fromJson(example.readAll()).object();
    QVERIFY2(ProjectLimits::validateProject(sample, &error), qPrintable(error));
}

void EventProjectTests::acceptsAnalysisOnlyEventDocuments()
{
    // KAN-123: the Telemetry app's documents carry no overlay scene.
    auto analysisOnly = Fixture::project();
    for (const auto *key : {"scene", "exportSettings", "mapSettings"}) analysisOnly.remove(key);
    QString error;
    QVERIFY2(ProjectLimits::validateProject(analysisOnly, &error), qPrintable(error));
    // A scene that is present must still be valid.
    auto malformed = analysisOnly;
    malformed.insert("scene", QJsonObject{{"widgets", "not an array"}});
    QVERIFY(!ProjectLimits::validateProject(malformed, &error));
    // A single-recording (v2) editor project still needs its scene.
    auto editor = EventProjectCodec::editorProjection(Fixture::project());
    editor.remove("scene");
    QVERIFY(!ProjectLimits::validateProject(editor, &error));
}

void EventProjectTests::boundsAndPreservesAnalysisDecisions()
{
    const auto original = Fixture::project();
    QVERIFY(!original.value("event").toObject().contains("analysisDecisions"));
    for (const auto &selection : {QJsonValue(QJsonValue::Undefined), QJsonValue(QJsonValue::Null),
         QJsonValue("compatibility-v1:" + QString(64, 'a'))}) {
        auto project = original; auto event = project.value("event").toObject();
        event.insert("analysisDecisions", QJsonObject{{"comparisonGroupId", selection}});
        project.insert("event", event);
        QString error; QVERIFY2(ProjectLimits::validateProject(project, &error), qPrintable(error));
        QCOMPARE(EventProjectCodec::withEditorState(project, EventProjectCodec::editorProjection(project), {}, {}), project);
        QTemporaryDir directory; ProjectRecoveryStore store(directory.filePath("recovery.json"));
        QVERIFY2(store.write({{}, "event-document", 5, 4, "2026-09-13T00:00:00.000Z", project, true}, &error), qPrintable(error));
        ProjectRecoverySnapshot restored; QVERIFY2(store.load(&restored, &error), qPrintable(error));
        QCOMPARE(restored.project, project);
    }
    for (const auto &selection : {QJsonValue(""), QJsonValue(0), QJsonValue(false), QJsonValue(QJsonObject{}),
         QJsonValue(QJsonArray{}), QJsonValue("Group 1"), QJsonValue("compatibility-v1:" + QString(65, 'a')),
         QJsonValue("compatibility-v1:" + QString(64, 'a') + '\n'), QJsonValue(QString(4097, 'x'))}) {
        auto project = original; auto event = project.value("event").toObject();
        event.insert("analysisDecisions", QJsonObject{{"comparisonGroupId", selection}});
        project.insert("event", event); QVERIFY(!ProjectLimits::validateProject(project));
    }
    for (const auto &value : {QJsonValue(QJsonValue::Null), QJsonValue(1), QJsonValue(QJsonArray{})}) {
        auto project = original; auto event = project.value("event").toObject();
        event.insert("analysisDecisions", value); project.insert("event", event);
        QVERIFY(!ProjectLimits::validateProject(project));
    }

    // KAN-41: persisted comparison-view range (shared-progress meters) and
    // visible-channel selection, following the same undefined/null/valid vs.
    // malformed split as comparisonGroupId above.
    for (const auto &range : {QJsonValue(QJsonValue::Undefined), QJsonValue(QJsonValue::Null),
         QJsonValue(QJsonObject{{"startMeters", 0.0}, {"endMeters", 1234.5}})}) {
        auto project = original; auto event = project.value("event").toObject();
        auto decisions = QJsonObject{};
        if (!range.isUndefined()) decisions.insert("comparisonRange", range);
        event.insert("analysisDecisions", decisions);
        project.insert("event", event);
        QString error; QVERIFY2(ProjectLimits::validateProject(project, &error), qPrintable(error));
    }
    for (const auto &range : {
             QJsonValue(""), QJsonValue(0), QJsonValue(QJsonArray{}),
             QJsonValue(QJsonObject{{"startMeters", 10.0}, {"endMeters", 5.0}}),      // inverted
             QJsonValue(QJsonObject{{"startMeters", 10.0}, {"endMeters", 10.0}}),     // empty span
             QJsonValue(QJsonObject{{"startMeters", -1.0}, {"endMeters", 5.0}}),      // negative start
             QJsonValue(QJsonObject{{"endMeters", 5.0}}),                            // missing start
             QJsonValue(QJsonObject{{"startMeters", 0.0}, {"endMeters", 2'000'000.0}}), // over budget
             QJsonValue(QJsonObject{{"startMeters", QString("0")}, {"endMeters", 5.0}}), // wrong type
         }) {
        auto project = original; auto event = project.value("event").toObject();
        event.insert("analysisDecisions", QJsonObject{{"comparisonRange", range}});
        project.insert("event", event); QVERIFY(!ProjectLimits::validateProject(project));
    }
    for (const auto &channels : {QJsonValue(QJsonValue::Undefined), QJsonValue(QJsonValue::Null),
         QJsonValue(QJsonArray{}), QJsonValue(QJsonArray{"speed", "throttle"})}) {
        auto project = original; auto event = project.value("event").toObject();
        auto decisions = QJsonObject{};
        if (!channels.isUndefined()) decisions.insert("comparisonChannels", channels);
        event.insert("analysisDecisions", decisions);
        project.insert("event", event);
        QString error; QVERIFY2(ProjectLimits::validateProject(project, &error), qPrintable(error));
    }
    for (const auto &channels : {
             QJsonValue(""), QJsonValue(0),
             QJsonValue(QJsonArray{"speed", "speed"}),                     // duplicate
             QJsonValue(QJsonArray{"a", "b", "c", "d", "e"}),              // over budget (>4)
             QJsonValue(QJsonArray{""}),                                  // empty name
             QJsonValue(QJsonArray{QString(129, 'x')}),                   // over length
         }) {
        auto project = original; auto event = project.value("event").toObject();
        event.insert("analysisDecisions", QJsonObject{{"comparisonChannels", channels}});
        project.insert("event", event); QVERIFY(!ProjectLimits::validateProject(project));
    }
}

void EventProjectTests::bindsFullContentWithoutMigratingOnLoad()
{
    auto project = Fixture::project(); auto runs = Fixture::runs(project); auto run = runs[0].toObject();
    auto sources = run.value("sources").toObject(); auto telemetry = sources.value("telemetry").toArray();
    auto source = telemetry[0].toObject(); const auto digest = QByteArray(64, 'a');
    source.insert("contentSha256", QString::fromLatin1(digest)); telemetry[0] = source;
    sources.insert("telemetry", telemetry); run.insert("sources", sources);
    auto config = EventProjectCodec::trackConfiguration(run); config.insert("layoutId", "Circuit");
    config.insert("direction", "clockwise"); run.insert("trackConfiguration", config);
    runs[0] = run; Fixture::setRuns(project, runs);
    QVERIFY(ProjectLimits::validateProject(project));
    QCOMPARE(EventProjectCodec::sourceContentRevision(source), digest);
    QCOMPARE(EventProjectCodec::withEditorState(project, EventProjectCodec::editorProjection(project), {}, {}, digest), project);
    auto changed = EventProjectCodec::withEditorState(project, EventProjectCodec::editorProjection(project), {}, {}, QByteArray(64, 'b'));
    QVERIFY(Fixture::runs(changed)[0].toObject().value("trackConfiguration").toObject().value("layoutId").isNull());
    QCOMPARE(Fixture::runs(changed)[1], runs[1]);
    for (const auto &value : {QJsonValue(""), QJsonValue("a"), QJsonValue(42), QJsonValue(QJsonValue::Null),
         QJsonValue(QString(64, 'a') + '\n')}) {
        source.insert("contentSha256", value); telemetry[0] = source; sources.insert("telemetry", telemetry);
        run.insert("sources", sources); runs[0] = run; Fixture::setRuns(project, runs);
        QVERIFY(!ProjectLimits::validateProject(project));
    }
    const auto legacy = Fixture::project();
    QCOMPARE(EventProjectCodec::withEditorState(legacy, EventProjectCodec::editorProjection(legacy), {}, {}, digest), legacy);
}

void EventProjectTests::boundsInferenceProvenanceAndRecoversIt()
{
    auto project = Fixture::project(); auto runs = Fixture::runs(project); auto run = runs[0].toObject();
    const QJsonObject inference{{"algorithm", "gps-route-v1"}, {"sourceRevision", QString(64, 'a')},
        {"gateRevision", "gates-v1:" + QString(64, 'b')}, {"layoutId", "gps-route-v1:" + QString(64, 'c')},
        {"direction", "clockwise"}};
    run.insert("trackInference", inference); runs[0] = run; Fixture::setRuns(project, runs);
    QVERIFY(ProjectLimits::validateProject(project));
    QTemporaryDir directory; ProjectRecoveryStore store(directory.filePath("recovery.json")); QString error;
    QVERIFY2(store.write({{}, "event-document", 5, 4, "2026-09-13T00:00:00.000Z", project, true}, &error), qPrintable(error));
    ProjectRecoverySnapshot restored; QVERIFY2(store.load(&restored, &error), qPrintable(error));
    QCOMPARE(restored.project, project);
    for (const auto &[field, value] : QList<QPair<QString, QJsonValue>>{
        {"algorithm", QString(129, 'x')}, {"algorithm", QString("bad") + QChar::Null},
        {"sourceRevision", QString(63, 'a')}, {"sourceRevision", 12}, {"gateRevision", "gates-v1:bad"},
        {"layoutId", "gps-route-v1:"}, {"layoutId", QString(129, 'x')}, {"direction", "unknown"}}) {
        auto invalid = inference; invalid.insert(field, value); run.insert("trackInference", invalid);
        runs[0] = run; Fixture::setRuns(project, runs); QVERIFY(!ProjectLimits::validateProject(project));
    }
    // Provenance that is not an object at all is rejected too.
    for (const QJsonValue &notAnObject : {QJsonValue(QStringLiteral("gps-route-v1")), QJsonValue(QJsonArray{inference})}) {
        run.insert("trackInference", notAnObject);
        runs[0] = run; Fixture::setRuns(project, runs); QVERIFY(!ProjectLimits::validateProject(project));
    }
}

void EventProjectTests::boundsAndPreservesRunMetadata()
{
    const auto original = Fixture::project();
    for (const auto *field : {"notes", "conditions", "setupChanges"}) {
        for (const QJsonValue &value : {QJsonValue(QJsonValue::Undefined), QJsonValue(QJsonValue::Null),
            QJsonValue(""), QJsonValue(QString(4096, 'x'))}) {
            auto project = original; auto runs = Fixture::runs(project); auto run = runs[0].toObject();
            const auto key = EventProjectCodec::lapDerivationKey(run);
            run.insert(field, value); run.insert("futureRun", QJsonObject{{"retained", true}});
            runs[0] = run; Fixture::setRuns(project, runs);
            QString error; QVERIFY2(ProjectLimits::validateProject(project, &error), qPrintable(error));
            QCOMPARE(EventProjectCodec::lapDerivationKey(run), key);
            QCOMPARE(EventProjectCodec::withEditorState(project, EventProjectCodec::editorProjection(project), {}, {}), project);
            QTemporaryDir directory; QVERIFY(directory.isValid());
            ProjectRecoveryStore store(directory.filePath("recovery.json"));
            const ProjectRecoverySnapshot snapshot{{}, "event-document", 5, 4, "2026-09-13T00:00:00.000Z", project, true};
            QVERIFY2(store.write(snapshot, &error), qPrintable(error));
            ProjectRecoverySnapshot restored; QVERIFY2(store.load(&restored, &error), qPrintable(error));
            QCOMPARE(restored.project, project);
        }
        for (const QJsonValue &value : {QJsonValue(QString(4097, 'x')), QJsonValue(42), QJsonValue(false),
            QJsonValue(QJsonObject{}), QJsonValue(QJsonArray{}), QJsonValue(QString("x") + QChar::Null)}) {
            auto project = original; auto runs = Fixture::runs(project); auto run = runs[0].toObject();
            run.insert(field, value); runs[0] = run; Fixture::setRuns(project, runs);
            QVERIFY(!ProjectLimits::validateProject(project));
        }
    }
    for (const auto &name : {QString(" "), QString(161, 'x'), QString("x") + QChar::Null}) {
        auto project = original; auto runs = Fixture::runs(project); auto run = runs[0].toObject();
        run.insert("name", name); runs[0] = run; Fixture::setRuns(project, runs);
        QVERIFY(!ProjectLimits::validateProject(project));
    }
}

void EventProjectTests::boundsSourceFusionAndRecoversIt()
{
    // KAN-103: an approved fusion names an alternative of the same run, the
    // content revisions of both recordings, a finite bounded clock and at
    // most 64 unique per-channel rules; it round-trips through recovery.
    auto project = Fixture::project(); auto runs = Fixture::runs(project); auto run = runs[0].toObject();
    const QJsonObject fusion{{"algorithm", "channel-fusion-v1"}, {"alternativeSourceId", "run-a-alternative"},
        {"primarySourceRevision", QString(64, 'a')}, {"alternativeSourceRevision", QString(64, 'b')},
        {"clock", QJsonObject{{"offsetSeconds", 12.5}, {"driftPpm", -40.0}, {"uncertaintySeconds", 0.05},
            {"alignmentAlgorithm", "recording-alignment-v1"}}},
        {"rules", QJsonArray{QJsonObject{{"key", "speed"}, {"rule", "fillGaps"}}}}};
    run.insert("fusion", fusion); runs[0] = run; Fixture::setRuns(project, runs);
    QVERIFY(ProjectLimits::validateProject(project));
    QTemporaryDir directory; ProjectRecoveryStore store(directory.filePath("recovery.json")); QString error;
    QVERIFY2(store.write({{}, "event-document", 5, 4, "2026-09-27T00:00:00.000Z", project, true}, &error), qPrintable(error));
    ProjectRecoverySnapshot restored; QVERIFY2(store.load(&restored, &error), qPrintable(error));
    QCOMPARE(restored.project, project);

    const auto rejected = [&](const QJsonObject &candidate) {
        auto copy = Fixture::project(); auto copyRuns = Fixture::runs(copy); auto copyRun = copyRuns[0].toObject();
        copyRun.insert("fusion", candidate); copyRuns[0] = copyRun; Fixture::setRuns(copy, copyRuns);
        return !ProjectLimits::validateProject(copy);
    };
    const auto with = [&](const QString &key, const QJsonValue &value) { auto copy = fusion; copy.insert(key, value); return copy; };
    const auto withClock = [&](const QString &key, const QJsonValue &value) {
        auto clock = fusion.value("clock").toObject(); clock.insert(key, value); return with("clock", clock);
    };
    QVERIFY(rejected(with("algorithm", "channel-fusion-v0")));
    QVERIFY(rejected(with("alternativeSourceId", "run-a-source")));      // the primary itself
    QVERIFY(rejected(with("alternativeSourceId", "run-b-source")));      // another run's source
    QVERIFY(rejected(with("alternativeSourceId", QString(129, 'x'))));
    QVERIFY(rejected(with("primarySourceRevision", QString(63, 'a'))));
    QVERIFY(rejected(with("alternativeSourceRevision", QString(64, 'G'))));
    QVERIFY(rejected(withClock("offsetSeconds", 1e9)));
    QVERIFY(rejected(withClock("offsetSeconds", "12")));
    QVERIFY(rejected(withClock("driftPpm", 5000.0)));
    QVERIFY(rejected(withClock("uncertaintySeconds", -1.0)));
    QVERIFY(rejected(with("clock", 3)));
    QVERIFY(rejected(with("rules", QJsonObject{})));
    QVERIFY(rejected(with("rules", QJsonArray{QJsonObject{{"key", "speed"}, {"rule", "overwrite"}}})));
    QVERIFY(rejected(with("rules", QJsonArray{QJsonObject{{"key", "speed"}, {"rule", "fillGaps"}},
                                              QJsonObject{{"key", "speed"}, {"rule", "primaryOnly"}}})));
    QJsonArray many;
    for (int index = 0; index < 65; ++index) many.append(QJsonObject{{"key", QString("c%1").arg(index)}, {"rule", "fillGaps"}});
    QVERIFY(rejected(with("rules", many)));
    many.removeLast();
    QVERIFY(!rejected(with("rules", many)));
    QVERIFY(rejected(QJsonObject{}));
    auto notObject = Fixture::project(); auto notRuns = Fixture::runs(notObject); auto notRun = notRuns[0].toObject();
    notRun.insert("fusion", 7); notRuns[0] = notRun; Fixture::setRuns(notObject, notRuns);
    QVERIFY(!ProjectLimits::validateProject(notObject));
}

void EventProjectTests::keepsNewerVersionsOfKnownFields()
{
    // KAN-170: a well-formed version tag this build does not know comes from
    // a newer app. The document opens, the field round-trips unchanged and
    // lap derivation ignores it. A malformed tag is still refused.
    const auto with = [](const QString &field, const QJsonValue &value) {
        auto project = Fixture::project(); auto runs = Fixture::runs(project); auto run = runs[0].toObject();
        run.insert(field, value); runs[0] = run; Fixture::setRuns(project, runs);
        return project;
    };
    const auto segment = [](const QString &reference) {
        return QJsonObject{{"id", reference}, {"type", "sector"}, {"name", "Sector 1"},
            {"startProgressMeters", 0.0}, {"endProgressMeters", 40.0}, {"trackConfigurationReference", reference}};
    };
    const QJsonObject newerFusion{{"algorithm", "channel-fusion-v2"}, {"sources", QJsonArray{"a", "b"}}};
    const QList<QJsonObject> accepted{
        with("fusion", newerFusion),
        with("trackSegmentReview", QJsonObject{{"version", "track-segment-review-v2"}, {"axis", 3}}),
        with("trackSegmentReview", QJsonObject{{"version", "track-segment-review-v1"},
            {"trackConfigurationReference", "compatibility-v2:next"}, {"proposalAlgorithm", "p"}, {"rejected", QJsonArray{}}}),
        with("trackInference", QJsonObject{{"algorithm", "gps-route-v2"}, {"layoutId", "gps-route-v2:next"}}),
        with("trackSegments", QJsonArray{segment("compatibility-v1:" + QString(64, 'a')), segment("compatibility-v2:next")}),
    };
    for (const auto &project : accepted) {
        QString error; QVERIFY2(ProjectLimits::validateProject(project, &error), qPrintable(error));
        QCOMPARE(EventProjectCodec::withEditorState(project, EventProjectCodec::editorProjection(project), {}, {}), project);
        QTemporaryDir directory; ProjectRecoveryStore store(directory.filePath("recovery.json"));
        QVERIFY2(store.write({{}, "event-document", 5, 4, "2026-10-06T00:00:00.000Z", project, true}, &error), qPrintable(error));
        ProjectRecoverySnapshot restored; QVERIFY2(store.load(&restored, &error), qPrintable(error));
        QCOMPARE(restored.project, project);
    }
    auto group = Fixture::project(); auto event = group.value("event").toObject();
    event.insert("analysisDecisions", QJsonObject{{"comparisonGroupId", "compatibility-v2:next"}});
    group.insert("event", event);
    QVERIFY(ProjectLimits::validateProject(group));
    // The newer fusion is not handed to lap derivation.
    const auto sources = EventProjectCodec::outingLapSources(with("fusion", newerFusion));
    QVERIFY(!sources[0].toObject().contains("fusion"));
    QVERIFY(!sources[0].toObject().contains("fusionNeedsRevalidation"));

    // Malformed tags, a current tag with a malformed body and a newer
    // segment reference with a malformed segment are refused.
    for (const auto &project : {
             with("fusion", QJsonObject{{"algorithm", "Channel Fusion 2"}}),
             with("fusion", QJsonObject{{"algorithm", "channel-fusion-v0"}}),
             with("fusion", QJsonObject{{"algorithm", "channel-fusion-v1"}}),
             with("trackSegmentReview", QJsonObject{{"version", "track-segment-review"}}),
             with("trackSegmentReview", QJsonObject{{"version", "track-segment-review-v1"}}),
             with("trackInference", QJsonObject{{"algorithm", "gps-route-v1"}, {"layoutId", "gps-route-v2:next"}}),
             with("trackInference", QJsonObject{{"algorithm", QString("gps-route-v2") + QChar::Null}}),
             with("trackSegments", QJsonArray{[&] { auto s = segment("compatibility-v2:next"); s.insert("extra", 1); return s; }()}),
             with("trackSegments", QJsonArray{segment("compatibility-v2:")}),
             with("trackSegments", QJsonArray{segment("Compatibility-v2:next")}),
         })
        QVERIFY(!ProjectLimits::validateProject(project));
    for (const auto &value : {QJsonValue("compatibility-v1:next"), QJsonValue("compatibility:next"), QJsonValue("compatibility-v2: ")}) {
        auto copy = Fixture::project(); auto copyEvent = copy.value("event").toObject();
        copyEvent.insert("analysisDecisions", QJsonObject{{"comparisonGroupId", value}});
        copy.insert("event", copyEvent);
        QVERIFY(!ProjectLimits::validateProject(copy));
    }
}

void EventProjectTests::boundsReferenceContentIdentity()
{
    // KAN-208: a reference may carry its file's full SHA-256. It survives a
    // save and Save As, is validated when present, and an event's telemetry
    // source keeps it on the source, not duplicated into the reference.
    const QString digest(64, 'a');
    auto project = Fixture::project(); auto runs = Fixture::runs(project); auto run = runs[1].toObject();
    auto sources = run.value("sources").toObject();
    sources.insert("video", QJsonObject{{"relativePath", "run-b.mp4"}, {"contentSha256", digest}});
    run.insert("sources", sources); runs[1] = run; Fixture::setRuns(project, runs);
    QString error; QVERIFY2(ProjectLimits::validateProject(project, &error), qPrintable(error));
    QTemporaryDir directory; QVERIFY(directory.isValid());
    const auto saved = EventProjectCodec::withEditorState(project, EventProjectCodec::editorProjection(project),
        directory.filePath("a/event.fetproject"), directory.filePath("b/event.fetproject"));
    QCOMPARE(Fixture::runs(saved)[1].toObject().value("sources").toObject().value("video").toObject()
                 .value("contentSha256").toString(), digest);

    auto editor = EventProjectCodec::editorProjection(project);
    auto editorSources = editor.value("sources").toObject();
    auto telemetry = editorSources.value("telemetry").toObject();
    telemetry.insert("contentSha256", QString(64, 'b'));
    editorSources.insert("telemetry", telemetry); editor.insert("sources", editorSources);
    const auto merged = EventProjectCodec::withEditorState(project, editor, {}, {});
    QVERIFY(!Fixture::reference(Fixture::runs(merged)[0].toObject()).contains("contentSha256"));

    for (const QJsonValue &bad : {QJsonValue(QString(64, 'A')), QJsonValue(QString(63, 'a')), QJsonValue(7)}) {
        auto invalid = project; auto invalidRuns = Fixture::runs(invalid); auto invalidRun = invalidRuns[1].toObject();
        auto invalidSources = invalidRun.value("sources").toObject();
        invalidSources.insert("video", QJsonObject{{"relativePath", "run-b.mp4"}, {"contentSha256", bad}});
        invalidRun.insert("sources", invalidSources); invalidRuns[1] = invalidRun; Fixture::setRuns(invalid, invalidRuns);
        QVERIFY(!ProjectLimits::validateProject(invalid));
    }
}

void EventProjectTests::boundsAndRebasesVideoChapters()
{
    // KAN-105: a video made of chapters keeps its first chapter as the video
    // reference and lists every chapter with its duration; malformed lists
    // are rejected in both project versions; Save As rebases an inactive
    // run's chapters; export protection sees every chapter.
    const QJsonObject first{{"relativePath", "GX010123.MP4"}, {"fingerprint", QJsonObject{{"kind", "video-v1"}}}};
    const auto chapter = [](const QString &path, const double duration) {
        return QJsonObject{{"relativePath", path}, {"durationSeconds", duration}};
    };
    auto withChapters = [&](const QJsonArray &chapters) {
        auto video = first; video.insert("chapters", chapters); return video;
    };
    auto firstChapter = first; firstChapter.insert("durationSeconds", 530.53);
    const QJsonArray valid{firstChapter, chapter("GX020123.MP4", 530.53), chapter("GX030123.MP4", 100.0)};
    QVERIFY(VideoChaptersCodec::valid(withChapters(valid)));
    QVERIFY(VideoChaptersCodec::valid(first)); // an ordinary video
    QCOMPARE(VideoChaptersCodec::read(withChapters(valid)).size(), 3);
    QCOMPARE(VideoChaptersCodec::read(withChapters(valid))[2].durationSeconds, 100.0);
    QVector<QJsonObject> bad;
    bad.append(withChapters({firstChapter}));                                   // one chapter is not a list
    bad.append(withChapters({chapter("GX020123.MP4", 530.0), chapter("GX030123.MP4", 100.0)})); // does not start with the video
    bad.append(withChapters({firstChapter, chapter("GX020123.MP4", 0.0)}));
    bad.append(withChapters({firstChapter, chapter("GX020123.MP4", -3.0)}));
    bad.append(withChapters({firstChapter, chapter("GX020123.MP4", 90000.0)}));
    bad.append(withChapters({firstChapter, QJsonObject{{"relativePath", "GX020123.MP4"}}}));  // no duration
    bad.append(withChapters({firstChapter, QJsonObject{{"durationSeconds", 1.0}}}));         // no path
    bad.append(withChapters({firstChapter, QJsonObject{{"relativePath", "/absolute.MP4"}, {"durationSeconds", 1.0}}}));
    bad.append(withChapters({firstChapter, 7}));
    QJsonArray many{firstChapter};
    for (int index = 2; index <= 65; ++index) many.append(chapter(QString("GX%1.MP4").arg(index), 1.0));
    bad.append(withChapters(many));
    auto notArray = first; notArray.insert("chapters", QJsonObject{}); bad.append(notArray);
    for (const auto &video : bad) {
        QVERIFY(!VideoChaptersCodec::valid(video));
        QVERIFY(VideoChaptersCodec::read(video).isEmpty());
        // v3: the run's video; v2: the editor project's video.
        auto project = Fixture::project(); auto runs = Fixture::runs(project); auto run = runs[0].toObject();
        auto sources = run.value("sources").toObject(); sources.insert("video", video); run.insert("sources", sources);
        runs[0] = run; Fixture::setRuns(project, runs);
        QVERIFY(!ProjectLimits::validateProject(project));
        auto editor = EventProjectCodec::editorProjection(Fixture::project());
        auto editorSources = editor.value("sources").toObject(); editorSources.insert("video", video); editor.insert("sources", editorSources);
        QVERIFY(!ProjectLimits::validateProject(editor));
    }

    // An inactive run's chapters are rebased on Save As and protected from export.
    QTemporaryDir directory; QVERIFY(directory.isValid());
    const QString oldPath = directory.filePath("old/event.fetproject");
    const QString newPath = directory.filePath("new/event.fetproject");
    auto project = Fixture::project(); auto runs = Fixture::runs(project); auto inactive = runs[1].toObject();
    auto inactiveSources = inactive.value("sources").toObject();
    inactiveSources.insert("video", withChapters(valid)); inactive.insert("sources", inactiveSources);
    runs[1] = inactive; Fixture::setRuns(project, runs);
    QVERIFY(ProjectLimits::validateProject(project));
    const auto saved = EventProjectCodec::withEditorState(project, EventProjectCodec::editorProjection(project), oldPath, newPath);
    QString error;
    QVERIFY2(ProjectLimits::validateProject(saved, &error), qPrintable(error));
    const auto video = Fixture::runs(saved)[1].toObject().value("sources").toObject().value("video").toObject();
    const auto chapters = video.value("chapters").toArray();
    QCOMPARE(chapters.size(), 3);
    QCOMPARE(video.value("relativePath").toString(), QString("../old/GX010123.MP4"));
    QCOMPARE(chapters[0].toObject().value("relativePath").toString(), QString("../old/GX010123.MP4"));
    QCOMPARE(chapters[2].toObject().value("relativePath").toString(), QString("../old/GX030123.MP4"));
    QCOMPARE(chapters[2].toObject().value("durationSeconds").toDouble(), 100.0);
    const auto protectedPaths = EventProjectCodec::referencedPaths(project, oldPath);
    QVERIFY(protectedPaths.contains(QDir(directory.filePath("old")).absoluteFilePath("GX030123.MP4")));
}

void EventProjectTests::persistsTrackConfigurationAndUnknownLegacyState()
{
    auto project = Fixture::project();
    auto runs = Fixture::runs(project);
    auto run = runs[0].toObject();
    const auto unknown = EventProjectCodec::trackConfiguration(run);
    QVERIFY(unknown.value("layoutId").isNull());
    QVERIFY(unknown.value("gateRevision").isNull());
    QCOMPARE(unknown.value("direction").toString(), QString("unknown"));
    QVERIFY(!run.contains("trackConfiguration"));
    auto config = unknown;
    config.insert("layoutId", "jastrzab-full");
    config.insert("direction", "clockwise");
    config.insert("gateRevision", "gates-v1:" + QString(64, 'a'));
    run.insert("trackConfiguration", config); runs[0] = run; Fixture::setRuns(project, runs);
    QString error;
    QVERIFY2(ProjectLimits::validateProject(project, &error), qPrintable(error));
    const auto reopened = QJsonDocument::fromJson(QJsonDocument(project).toJson()).object();
    QCOMPARE(reopened, project);
    QCOMPARE(EventProjectCodec::withEditorState(project, EventProjectCodec::editorProjection(project), {}, {}), project);
    QTemporaryDir directory; QVERIFY(directory.isValid());
    ProjectRecoveryStore store(directory.filePath("identity-recovery.json"));
    const ProjectRecoverySnapshot snapshot{{}, "event-document", 5, 4, "2026-09-13T00:00:00.000Z", project, true};
    QVERIFY2(store.write(snapshot, &error), qPrintable(error));
    ProjectRecoverySnapshot restored;
    QVERIFY2(store.load(&restored, &error), qPrintable(error));
    QCOMPARE(restored.project, project);
}

void EventProjectTests::rejectsInvalidTrackConfigurations_data()
{
    QTest::addColumn<QString>("field"); QTest::addColumn<QJsonValue>("value");
    QTest::newRow("foreign-source") << QString("sourceId") << QJsonValue("run-b-source");
    QTest::newRow("alternative-source") << QString("sourceId") << QJsonValue("run-a-alternative");
    QTest::newRow("stale-fingerprint") << QString("sourceFingerprint") << QJsonValue(QJsonObject{{"digest", "changed"}});
    QTest::newRow("null-fingerprint") << QString("sourceFingerprint") << QJsonValue(QJsonValue::Null);
    QTest::newRow("empty-layout") << QString("layoutId") << QJsonValue("  ");
    QTest::newRow("oversized-layout") << QString("layoutId") << QJsonValue(QString(129, 'x'));
    QTest::newRow("numeric-layout") << QString("layoutId") << QJsonValue(42);
    QTest::newRow("missing-layout") << QString("layoutId") << QJsonValue(QJsonValue::Undefined);
    QTest::newRow("direction-crossing-sign") << QString("direction") << QJsonValue(1);
    QTest::newRow("unsupported-direction") << QString("direction") << QJsonValue("forward");
    QTest::newRow("empty-revision") << QString("gateRevision") << QJsonValue("");
    QTest::newRow("bad-revision") << QString("gateRevision") << QJsonValue("gates-v1:abc");
    QTest::newRow("revision-trailing-newline") << QString("gateRevision") << QJsonValue("gates-v1:" + QString(64, 'a') + "\n"); // KAN-181
}

void EventProjectTests::rejectsInvalidTrackConfigurations()
{
    QFETCH(QString, field); QFETCH(QJsonValue, value);
    auto project = Fixture::project(); auto runs = Fixture::runs(project); auto run = runs[0].toObject();
    auto config = EventProjectCodec::trackConfiguration(run); config.insert(field, value);
    run.insert("trackConfiguration", config); runs[0] = run; Fixture::setRuns(project, runs);
    QString error; QVERIFY(!ProjectLimits::validateProject(project, &error));
    QVERIFY(error.contains("Track configuration"));
}

void EventProjectTests::invalidatesDerivationOnConfigurationAndSourceChanges()
{
    const auto original = Fixture::runs(Fixture::project())[0].toObject();
    const auto key = EventProjectCodec::lapDerivationKey(original);
    auto run = original;
    run.insert("name", "Renamed"); run.insert("notes", "New notes");
    run.insert("sync", QJsonObject{{"offset", 10}, {"timeScale", 2}});
    QCOMPARE(EventProjectCodec::lapDerivationKey(run), key);
    for (const QString &field : {QString("layoutId"), QString("direction"), QString("gateRevision")}) {
        run = original; auto config = EventProjectCodec::trackConfiguration(run);
        config.insert(field, field == "layoutId" ? "layout-b" : field == "direction" ? "counterclockwise" : "gates-v1:" + QString(64, 'b'));
        run.insert("trackConfiguration", config);
        QVERIFY(EventProjectCodec::lapDerivationKey(run) != key);
    }
    run = original; run.insert("primaryTelemetrySourceId", "run-a-alternative");
    QVERIFY(EventProjectCodec::lapDerivationKey(run) != key);
    run = original; auto sources = run.value("sources").toObject(); auto telemetry = sources.value("telemetry").toArray();
    auto source = telemetry[0].toObject(); auto reference = source.value("reference").toObject();
    reference.insert("relativePath", "moved.vbo"); source.insert("reference", reference); telemetry[0] = source;
    sources.insert("telemetry", telemetry); run.insert("sources", sources);
    QCOMPARE(EventProjectCodec::lapDerivationKey(run), key);
    reference.insert("fingerprint", QJsonObject{{"digest", "new"}}); source.insert("reference", reference); telemetry[0] = source;
    sources.insert("telemetry", telemetry); run.insert("sources", sources);
    QVERIFY(EventProjectCodec::lapDerivationKey(run) != key);
}

void EventProjectTests::bindsPrimaryTelemetryFromTheDocument()
{
    auto project = Fixture::project();
    const auto run = Fixture::runs(project)[0].toObject();
    QJsonObject primary;
    for (const auto &value : run.value("sources").toObject().value("telemetry").toArray())
        if (value.toObject().value("id") == run.value("primaryTelemetrySourceId")) primary = value.toObject();
    QVERIFY(!primary.isEmpty());
    const auto binding = EventProjectCodec::primaryTelemetryBinding(project, "run-a");
    QCOMPARE(binding.value("eventId").toString(), QString("event-identity"));
    QCOMPARE(binding.value("runId").toString(), QString("run-a"));
    QCOMPARE(binding.value("sourceId"), run.value("primaryTelemetrySourceId"));
    QCOMPARE(binding.value("reference"), primary.value("reference"));
    QCOMPARE(binding.value("expectedRevision").toString(), QString::fromLatin1(EventProjectCodec::sourceContentRevision(primary)));
    QCOMPARE(binding.value("derivationKey").toString(), QString::fromLatin1(EventProjectCodec::lapDerivationKey(run)));
    QCOMPARE(EventProjectCodec::primaryTelemetryBinding(project, "run-b").value("runId").toString(), QString("run-b"));
    QVERIFY(EventProjectCodec::primaryTelemetryBinding(project, "missing-run").isEmpty());
    auto runs = Fixture::runs(project); auto orphan = runs[0].toObject();
    orphan.insert("primaryTelemetrySourceId", "no-such-source"); runs[0] = orphan; Fixture::setRuns(project, runs);
    QVERIFY(EventProjectCodec::primaryTelemetryBinding(project, "run-a").isEmpty());
    QVERIFY(EventProjectCodec::primaryTelemetryBinding(QJsonObject{{"version", 2}}, "run-a").isEmpty());
}

void EventProjectTests::clearsConfigurationOnSourceReplacementOnly()
{
    auto project = Fixture::project(); auto runs = Fixture::runs(project); auto run = runs[0].toObject();
    auto config = EventProjectCodec::trackConfiguration(run); config.insert("layoutId", "known-layout");
    config.insert("direction", "clockwise"); config.insert("gateRevision", "gates-v1:" + QString(64, 'a'));
    run.insert("trackConfiguration", config); runs[0] = run; Fixture::setRuns(project, runs);
    auto editor = EventProjectCodec::editorProjection(project); auto sources = editor.value("sources").toObject();
    auto reference = sources.value("telemetry").toObject(); reference.insert("relativePath", "moved.vbo");
    sources.insert("telemetry", reference); editor.insert("sources", sources);
    auto saved = EventProjectCodec::withEditorState(project, editor, {}, {});
    QCOMPARE(Fixture::runs(saved)[0].toObject().value("trackConfiguration").toObject(), config);
    reference.insert("fingerprint", QJsonObject{{"digest", "replacement"}});
    sources.insert("telemetry", reference); editor.insert("sources", sources);
    saved = EventProjectCodec::withEditorState(project, editor, {}, {});
    const auto replaced = Fixture::runs(saved)[0].toObject();
    const auto unknown = EventProjectCodec::trackConfiguration(replaced);
    QVERIFY(unknown.value("layoutId").isNull()); QVERIFY(unknown.value("gateRevision").isNull());
    QCOMPARE(unknown.value("direction").toString(), QString("unknown"));
    QVERIFY(EventProjectCodec::lapDerivationKey(replaced) != EventProjectCodec::lapDerivationKey(run));
    QString error; QVERIFY2(ProjectLimits::validateProject(saved, &error), qPrintable(error));
    QCOMPARE(Fixture::runs(saved)[1], runs[1]);
}

void EventProjectTests::revisionsReflectPhysicalGatesWithoutInferringDirection()
{
    auto east = VboParser::parse(QString::fromUtf8(Fixture::lapsVbo()));
    const auto revision = timingGateRevision(east);
    QCOMPARE(revision.size(), 73); QVERIFY(revision.startsWith("gates-v1:"));
    auto west = east; west.metadata.insert("gpsLongitudeConvention", "west-positive");
    for (auto &gate : west.timingGates) {
        gate.endpointA.longitudeDegrees *= -1; gate.endpointB.longitudeDegrees *= -1;
    }
    QCOMPARE(timingGateRevision(west), revision);
    auto changed = east; changed.timingGates[0].sourceName = "Renamed";
    QCOMPARE(timingGateRevision(changed), revision);
    changed.timingGates[0].endpointA.latitudeDegrees += .00001;
    QVERIFY(timingGateRevision(changed) != revision);
    changed = east; changed.timingGates[0].type = TimingGateType::Split;
    QVERIFY(timingGateRevision(changed).isEmpty());
    changed = east; changed.timingGates.append(changed.timingGates[0]);
    QVERIFY(timingGateRevision(changed).isEmpty());
    changed = east; changed.timingGates[0].endpointA.latitudeDegrees = std::numeric_limits<double>::quiet_NaN();
    QVERIFY(timingGateRevision(changed).isEmpty());
    changed = east; auto split = changed.timingGates[0]; split.type = TimingGateType::Split;
    changed.timingGates.append(split); const auto withSplit = timingGateRevision(changed);
    QVERIFY(withSplit != revision); QVERIFY(!withSplit.isEmpty());
    std::swap(changed.timingGates[0], changed.timingGates[1]);
    QVERIFY(timingGateRevision(changed) != withSplit);
    changed.timingGates.resize(129); QVERIFY(timingGateRevision(changed).isEmpty());
    QVERIFY(timingGateRevision(TelemetrySession{}).isEmpty());
    QVERIFY_THROWS_EXCEPTION(OperationCancelled, static_cast<void>(timingGateRevision(east, [] { return true; })));
}

void EventProjectTests::preservesFiniteExtremeSyncForGuardedConsumers()
{
    auto project = Fixture::project();
    auto event = project.value("event").toObject();
    auto runs = event.value("runs").toArray();
    auto run = runs[0].toObject();
    run.insert("sync", QJsonObject{{"offset", 0.0}, {"timeScale", std::numeric_limits<double>::max()}});
    runs[0] = run; event.insert("runs", runs); project.insert("event", event);
    QString error;
    QVERIFY2(ProjectLimits::validateProject(project, &error), qPrintable(error));
    const auto reopened = QJsonDocument::fromJson(QJsonDocument(project).toJson()).object();
    QVERIFY2(ProjectLimits::validateProject(reopened, &error), qPrintable(error));
    const auto sync = EventProjectCodec::editorProjection(reopened).value("sync").toObject();
    QCOMPARE(sync.value("timeScale").toDouble(), std::numeric_limits<double>::max());
    const SyncTransform transform{sync.value("offset").toDouble(), sync.value("timeScale").toDouble()};
    QVERIFY(!videoToTelemetryTime(2, transform));
    QCOMPARE(videoToTelemetryTime(0, transform).value(), 0.0);
}

void EventProjectTests::rejectsMalformedEvents_data()
{
    QTest::addColumn<QJsonObject>("project");
    const auto row = [](const char *name, const std::function<void(QJsonObject &)> &mutate) {
        QJsonObject project = Fixture::project();
        mutate(project);
        QTest::newRow(name) << project;
    };
    for (const QString &key : {QStringLiteral("sources"), QStringLiteral("sync"), QStringLiteral("videoPath"), QStringLiteral("vboPath")}) {
        row(qPrintable("root-" + key), [key](QJsonObject &p) { p.insert(key, QJsonObject{}); });
    }
    row("v2-event", [](QJsonObject &p) { p.insert("version", 2); });
    row("fractional-version", [](QJsonObject &p) { p.insert("version", 3.1); });
    row("missing-event", [](QJsonObject &p) { p.remove("event"); });
    row("empty-runs", [](QJsonObject &p) { Fixture::setRuns(p, {}); });
    row("unknown-active", [](QJsonObject &p) { auto e = p.value("event").toObject(); e.insert("activeRunId", "missing"); p.insert("event", e); });
    row("duplicate-run", [](QJsonObject &p) { auto runs = Fixture::runs(p); runs.append(runs[0]); Fixture::setRuns(p, runs); });
    const auto runRow = [&row](const char *name, const std::function<void(QJsonObject &)> &mutate) {
        row(name, [mutate](QJsonObject &p) { auto runs = Fixture::runs(p); auto run = runs[0].toObject(); mutate(run); runs[0] = run; Fixture::setRuns(p, runs); });
    };
    runRow("foreign-primary", [](QJsonObject &r) { r.insert("primaryTelemetrySourceId", "run-b-source"); });
    runRow("empty-name", [](QJsonObject &r) { r.insert("name", "  "); });
    runRow("long-id", [](QJsonObject &r) { r.insert("id", QString(129, 'x')); });
    runRow("bad-sync", [](QJsonObject &r) { r.insert("sync", QJsonObject{{"offset", "2.5"}, {"timeScale", 1.0}}); });
    runRow("zero-scale", [](QJsonObject &r) { r.insert("sync", QJsonObject{{"offset", 0.0}, {"timeScale", 0.0}}); });
    runRow("null-sync", [](QJsonObject &r) { r.insert("sync", QJsonValue::Null); });
    runRow("no-sources", [](QJsonObject &r) { r.insert("sources", QJsonObject{}); });
    runRow("bad-video", [](QJsonObject &r) { auto s = r.value("sources").toObject(); s.insert("video", QJsonObject{}); r.insert("sources", s); });
    runRow("duplicate-source", [](QJsonObject &r) { auto s = r.value("sources").toObject(); auto t = s.value("telemetry").toArray(); t.append(t[0]); s.insert("telemetry", t); r.insert("sources", s); });
    runRow("cross-run-source-id", [](QJsonObject &r) { auto s = r.value("sources").toObject(); auto t = s.value("telemetry").toArray(); auto a = t[1].toObject(); a.insert("id", "run-b-source"); t[1] = a; s.insert("telemetry", t); r.insert("sources", s); });
    runRow("relative-as-absolute", [](QJsonObject &r) { auto s = r.value("sources").toObject(); s.insert("video", QJsonObject{{"absolutePath", "camera.mp4"}}); r.insert("sources", s); });
    runRow("bad-fingerprint", [](QJsonObject &r) { auto s = r.value("sources").toObject(); s.insert("video", QJsonObject{{"relativePath", "camera.mp4"}, {"fingerprint", 3}}); r.insert("sources", s); });
}

void EventProjectTests::rejectsMalformedEvents()
{
    QFETCH(QJsonObject, project);
    QString error;
    QVERIFY(!ProjectLimits::validateProject(project, &error));
    QVERIFY(!error.isEmpty());
}

void EventProjectTests::boundsRunsAndSources()
{
    QJsonObject project = Fixture::project();
    QJsonArray runs;
    for (int i = 0; i < 64; ++i) runs.append(Fixture::run(i == 0 ? "run-a" : QString::number(i), "a.vbo", 0));
    Fixture::setRuns(project, runs);
    QString error;
    QVERIFY(ProjectLimits::validateProject(project, &error));
    runs.append(Fixture::run("excess", "a.vbo", 0));
    Fixture::setRuns(project, runs);
    QVERIFY(!ProjectLimits::validateProject(project, &error));
    project = Fixture::project();
    runs = Fixture::runs(project);
    auto first = runs[0].toObject();
    auto sources = first.value("sources").toObject();
    auto telemetry = sources.value("telemetry").toArray();
    for (int i = 2; i < 9; ++i) telemetry.append(Fixture::source(QString::number(i), "a.rcz"));
    sources.insert("telemetry", telemetry); first.insert("sources", sources); runs[0] = first; Fixture::setRuns(project, runs);
    QVERIFY(!ProjectLimits::validateProject(project, &error));
    // 64 runs with two sources each fit; one more source breaches the total ceiling.
    runs = {};
    for (int i = 0; i < 64; ++i) {
        auto run = Fixture::run(i == 0 ? "run-a" : QString::number(i), "a.vbo", 0);
        auto s = run.value("sources").toObject();
        auto t = s.value("telemetry").toArray();
        t.append(Fixture::source(QStringLiteral("alternative-%1").arg(i), "a.rcz"));
        s.insert("telemetry", t); run.insert("sources", s); runs.append(run);
    }
    Fixture::setRuns(project, runs);
    QVERIFY(ProjectLimits::validateProject(project, &error));
    auto last = runs.last().toObject();
    auto lastSources = last.value("sources").toObject();
    auto lastTelemetry = lastSources.value("telemetry").toArray();
    lastTelemetry.append(Fixture::source("excess", "b.rcz"));
    lastSources.insert("telemetry", lastTelemetry); last.insert("sources", lastSources);
    runs[runs.size() - 1] = last; Fixture::setRuns(project, runs);
    QVERIFY(!ProjectLimits::validateProject(project, &error));
}

void EventProjectTests::rebasesInactiveAlternativeAndMissingReferences()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString oldPath = directory.filePath("old/event.fetproject");
    const QString newPath = directory.filePath("new/event.fetproject");
    QJsonObject project = Fixture::project();
    auto originalRuns = Fixture::runs(project);
    auto inactive = originalRuns[1].toObject();
    auto inactiveSources = inactive.value("sources").toObject();
    inactiveSources.insert("video", QJsonObject{{"relativePath", "run-b.mp4"}, {"futureVideo", true}});
    inactive.insert("sources", inactiveSources); originalRuns[1] = inactive; Fixture::setRuns(project, originalRuns);
    QJsonObject editor = EventProjectCodec::editorProjection(project);
    auto sources = editor.value("sources").toObject();
    for (const QString &key : {QStringLiteral("video"), QStringLiteral("telemetry")}) {
        const auto ref = ProjectSourceReferenceCodec::fromProject(editor, key, {});
        auto object = sources.value(key).toObject();
        const auto known = EventProjectCodec::referenceForSave(ref, oldPath, newPath);
        for (auto it = known.begin(); it != known.end(); ++it) object.insert(it.key(), it.value());
        sources.insert(key, object);
    }
    editor.insert("sources", sources);
    auto sync = editor.value("sync").toObject(); sync.insert("offset", 9.0); editor.insert("sync", sync);
    const auto saved = EventProjectCodec::withEditorState(project, editor, oldPath, newPath);
    QString error;
    QVERIFY2(ProjectLimits::validateProject(saved, &error), qPrintable(error));
    QVERIFY(!saved.contains("sources")); QVERIFY(!saved.contains("sync"));
    const auto runs = Fixture::runs(saved);
    QCOMPARE(Fixture::reference(runs[0].toObject()).value("relativePath").toString(), QStringLiteral("../old/run-a.vbo"));
    QCOMPARE(Fixture::reference(runs[0].toObject(), 1).value("relativePath").toString(), QStringLiteral("../old/run-a.rcz"));
    QCOMPARE(Fixture::reference(runs[1].toObject()).value("relativePath").toString(), QStringLiteral("../old/run-b.vbo"));
    QCOMPARE(Fixture::reference(runs[1].toObject()).value("futureReference").toInt(), 42);
    const auto inactiveVideo = runs[1].toObject().value("sources").toObject().value("video").toObject();
    QCOMPARE(inactiveVideo.value("relativePath").toString(), QStringLiteral("../old/run-b.mp4"));
    QVERIFY(inactiveVideo.value("futureVideo").toBool());
    QCOMPARE(runs[0].toObject().value("sync").toObject().value("offset").toDouble(), 9.0);
    QCOMPARE(runs[1].toObject().value("sync"), Fixture::runs(project)[1].toObject().value("sync"));
    QCOMPARE(saved.value("futureRoot"), project.value("futureRoot"));
    QCOMPARE(saved.value("event").toObject().value("id"), project.value("event").toObject().value("id"));
    const auto second = EventProjectCodec::withEditorState(saved, EventProjectCodec::editorProjection(saved), newPath, newPath);
    QCOMPARE(second, saved);
}

void EventProjectTests::prefersMovedRelativeSourceToStaleAbsoluteFallback()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    // Save As requires an existing destination directory. Resolve its real path
    // too on macOS, where the temporary root can be reached through /var.
    QVERIFY(QDir().mkpath(directory.filePath("new")));
    const QString actual = directory.filePath("a.vbo");
    QFile file(actual); QVERIFY(file.open(QIODevice::WriteOnly)); file.close();
    const auto json = EventProjectCodec::referenceForSave(
        {"a.vbo", directory.filePath("stale/a.vbo"), {{"digest", "retained"}}},
        directory.filePath("event.fetproject"), directory.filePath("new/event.fetproject"));
    QCOMPARE(json.value("relativePath").toString(), QStringLiteral("../a.vbo"));
    QCOMPARE(json.value("fingerprint").toObject().value("digest").toString(), QStringLiteral("retained"));
}

void EventProjectTests::rejectsDeepTraversalOnSourceResolve()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString farPath = directory.filePath("secret.vbo");
    QFile farFile(farPath);
    QVERIFY(farFile.open(QIODevice::WriteOnly));
    farFile.close();

    // A relative reference reaching more than two parent segments above the
    // project directory must not resolve, even though the save path would
    // never write one this deep and the target file genuinely exists there.
    const QString deepProjectPath = directory.filePath("layout/day/event.fetproject");
    QVERIFY(QDir().mkpath(QFileInfo(deepProjectPath).absolutePath()));
    const ProjectSourceReference deep{"../../../secret.vbo", {}, {}};
    QVERIFY(ProjectSourceReferenceCodec::resolve(deep, deepProjectPath).isEmpty());

    // Exactly two parent segments matches the save-side bound and must still resolve.
    const QString shallowProjectPath = directory.filePath("a/b/event.fetproject");
    QVERIFY(QDir().mkpath(QFileInfo(shallowProjectPath).absolutePath()));
    const ProjectSourceReference within{"../../secret.vbo", {}, {}};
    QCOMPARE(ProjectSourceReferenceCodec::resolve(within, shallowProjectPath),
             QFileInfo(farPath).canonicalFilePath());

    // An absolute path smuggled into the relativePath field must not bypass
    // project-directory confinement either.
    const ProjectSourceReference smuggled{farPath, {}, {}};
    QVERIFY(ProjectSourceReferenceCodec::resolve(smuggled, deepProjectPath).isEmpty());
}

void EventProjectTests::retainsEventRecoveryIdentity()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    ProjectRecoveryStore store(directory.filePath("recovery.json"));
    const ProjectRecoverySnapshot snapshot{directory.filePath("event.fetproject"), "event-document", 5, 4,
        "2026-09-12T12:00:00.000Z", Fixture::project(), true};
    QString error;
    QVERIFY2(store.write(snapshot, &error), qPrintable(error));
    ProjectRecoverySnapshot restored;
    QVERIFY2(store.load(&restored, &error), qPrintable(error));
    QCOMPARE(restored.project, snapshot.project);
    QCOMPARE(restored.documentId, snapshot.documentId);
    QCOMPARE(restored.revision, quint64(5));
    QCOMPARE(restored.lastSavedRevision, quint64(4));
}

void EventProjectTests::protectsInactiveSourcesFromExportOverwrite()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto project = Fixture::project();
    const auto paths = EventProjectCodec::referencedPaths(project, directory.filePath("event.fetproject"));
    QCOMPARE(paths.size(), 4);
    for (const QString &path : paths) {
        QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); file.close();
        ExportOutputTransaction transaction;
        QCOMPARE(transaction.prepare(path, directory.filePath("active-video.mp4"), paths, true).status,
                 ExportOutputTransaction::PreparationStatus::Error);
    }
}

void EventProjectTests::keepsMissingReferencesPortableThroughDirectorySymlinks()
{
#ifdef Q_OS_UNIX
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString real = directory.filePath("real");
    const QString alias = directory.filePath("alias");
    QVERIFY(QDir().mkpath(real));
    QVERIFY(QFile::link(real, alias));
    const auto rebased = EventProjectCodec::referenceForSave(
        {"missing.vbo", {}, {}}, QDir(alias).filePath("event.fetproject"), QDir(alias).filePath("saved.fetproject"));
    QCOMPARE(rebased.value("relativePath").toString(), QStringLiteral("missing.vbo"));
    QCOMPARE(rebased.value("absolutePath").toString(), QDir(QFileInfo(real).canonicalFilePath()).filePath("missing.vbo"));
#else
    QSKIP("Directory symlink portability is covered on macOS/Unix.");
#endif
}

QTEST_GUILESS_MAIN(EventProjectTests)
#include "EventProjectTests.moc"
