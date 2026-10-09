// KAN-161: events, day import, video chapters, media probing, GoPro GPMF and synchronisation.
// Split from the former TelemetryTests.cpp; shared helpers are in NativeTestSupport.h.
#include "NativeTestSupport.h"
#include "gopro/GoProTelemetrySource.h"
#include "app/AppController.h"
#include "app/ExportJobPlan.h"
#include "app/SourceLoading.h"
#include "project/VideoChapters.h"
#include "app/TelemetryController.h"
#include "export/FfmpegTools.h"
#include "export/MediaProbe.h"
#include "export/ExportMediaProfile.h"
#include "telemetry/TelemetrySyncEngine.h"
#include "export/VideoFingerprint.h"
#include "telemetry/TelemetryRenderContext.h"
#include "telemetry/TrackGeometry.h"
#include "telemetry/VboParser.h"
#include "RczFixture.h"
#include "EventProjectFixture.h"
#include "project/EventProjectCodec.h"
#include "project/ProjectRecoveryStore.h"
#include "project/ProjectLimits.h"

#if defined(Q_OS_UNIX)
#include <array>
#include <sys/resource.h>
#endif
#include <optional>
#include <stdexcept>

using namespace NativeTestSupport;

class SourceTests final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void persistsEventSelectionAndRunLocalSync();
    void keepsAdditionalVideosThroughRunSwitchAndSaveAs();
    void addsAlignsAndChecksAdditionalVideos();
    void exportsWithAnAdditionalVideo();
    void recoversEventAndRelinksOnlyActiveSource();
    void rejectsInvalidEventWithoutReplacingDocument();
    void rejectsLateSourceResultsAfterRunSelection();
    void selectsEventRunThroughEditorQml();
    void importsSixRunsAndAppendsWithoutDuplicates();
    void confirmsExplicitSourceGroups();
    void cancelsAndRejectsChangedBatchSources();
    void invalidatesBatchReviewAfterDocumentChanges();
    void importsAnalysisRunsAutomatically();
    void guardsAutomaticAnalysisImport();
    void findsTheDayBestLapWithoutTheAnalysis();
    void restoresDayDecisionsAfterMoveMissingRelinkAndRecovery();
    void keepsEveryTelemetryFieldThroughAnOverlayEdit();
    void automaticallyGroupsPrivateTrackDay();
    void lapExclusionPolicySharesRankingAndRenderInputs();
    void lapExclusionsSurviveSaveRecoveryAndInvalidateSafely();
    void keepsAnalysisStateThroughOverlayEdits();
    void recordsVboUtcChronology();
    void rejectsBatchLinksWithDifferentPersistedFormats();
    void reviewsGoProChapterGroups();
    void keepsVideoChaptersAsOneTimeline();
    void loadsSourcesInTheBackgroundWorker();
    void derivesChapterPlaybackAndExportSource();
    void readsTheSavedChaptersOfAProject();
    void plansTheExportJobFromTheLoadedSources();
    void playsVideoChaptersAcrossBoundaries();
    void persistsAndInvalidatesRunTrackConfiguration();
    void decodesGps9Gpmf();
    void judgesGoProGpsQuality();
    void rejectsMalformedGpmf();
    void cancelsSlowGoProProbePromptly();
    void boundsGoProProbeOutput();
    void rejectsOutOfFileGpmfPackets();
    void boundsGpmfDepthAndRecordCount();
    void decodesNestedGpmfWithoutCopies();
    void normalizesGpmfTimestamps();
    void boundsTimeTransforms_data();
    void boundsTimeTransforms();
    void exposesNoDataForOverflowingTransforms();
    void rejectsUnsafeSynchronizationInputs_data();
    void rejectsUnsafeSynchronizationInputs();
    void preservesConfirmedTransformForAmbiguousResult();
    void rejectsInvalidAutomaticCandidates();
    void keepsExtremeFiniteSyncSignalsBounded();
    void synchronizesGpsSpeed();
    void cancelsSynchronizationDeterministically();
    void reportsAmbiguousGpsSpeed();
    void retainsGlobalSyncAmbiguity();
    void countsANearbyFalseSyncPeak();
    void rejectsAutomaticSyncWithShortOverlap();
    void synchronizesWhenTheRecordingsOnlyPartlyOverlap();
    void neverAutoAppliesAnotherLapOfPeriodicLaps();
    void gatesWeakSyncCandidates();
    void preservesTimingEditsDuringAutoSync_data();
    void preservesTimingEditsDuringAutoSync();
    void probesMediaInfoJson();
    void parsesMediaSummaryJson();
    void modelsExtendedMediaCharacteristics();
    void rejectsInvalidMediaProbeJson();
    void classifiesMediaProbeProcessFailures();
    void reportsMediaProbeLifecycleHeartbeat();
    void cancelsMediaProbeWithoutLeavingItRunning();
    void retainsTelemetryAfterFailedAsyncLoad();
    void replacesInFlightSourceLoad();
    void shutsDownWithInFlightSourceLoad();
    void fingerprintsSourcesDeterministically();
    void preservesInterleavedSourceRequests_data();
    void preservesInterleavedSourceRequests();
    void syncsOptionalRealRecording();
};

void SourceTests::initTestCase()
{
    isolateSettings(QStringLiteral("SourceTests"));
}

void SourceTests::cleanupTestCase()
{
    clearSettings();
}

namespace {
QByteArray klvRecord(
    const QByteArray &key,
    const char type,
    const quint8 size,
    const quint16 repeat,
    QByteArray data)
{
    QByteArray result = key.leftJustified(4, ' ').left(4);
    result.append(type);
    result.append(static_cast<char>(size));
    const quint16 bigRepeat = qToBigEndian(repeat);
    result.append(reinterpret_cast<const char *>(&bigRepeat), sizeof(bigRepeat));
    result.append(data);
    while (result.size() % 4 != 0) {
        result.append('\0');
    }
    return result;
}

void append32(QByteArray &data, const qint32 value)
{
    const qint32 big = qToBigEndian(value);
    data.append(reinterpret_cast<const char *>(&big), sizeof(big));
}

void append16(QByteArray &data, const quint16 value)
{
    const quint16 big = qToBigEndian(value);
    data.append(reinterpret_cast<const char *>(&big), sizeof(big));
}

} // namespace

void SourceTests::persistsEventSelectionAndRunLocalSync()
{
    namespace Fixture = EventProjectFixture;
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings; settings.clear(); settings.sync();
    QVERIFY(writeBytes(directory.filePath("run-a.vbo"), Fixture::lapsVbo()));
    QVERIFY(QFile::copy(QStringLiteral(TEST_FIXTURE_PATH), directory.filePath("run-b.vbo")));
    const QString path = directory.filePath("event.fetproject");
    QVERIFY(writeBytes(path, QJsonDocument(Fixture::project()).toJson()));
    AppController controller(nullptr, directory.filePath("recovery.json"));
    controller.requestOpenProject(QUrl::fromLocalFile(path));
    QTRY_COMPARE(controller.vboLoadState(), QStringLiteral("ready"));
    QCOMPARE(controller.eventName(), QStringLiteral("Development event"));
    QCOMPARE(controller.eventRuns().size(), 2);
    QCOMPARE(controller.activeRunId(), QStringLiteral("run-a"));
    QCOMPARE(controller.lapSummaries().size(), 3);
    QCOMPARE(controller.lapSummaries()[0].toMap().value("runId").toString(), QStringLiteral("run-a"));
    QCOMPARE(controller.videoLoadState(), QStringLiteral("missing"));
    QVERIFY(!controller.dirty());
    QCOMPARE(controller.lastSavedRevision(), quint64(4));
    const QString documentId = controller.m_document.m_documentId;
    controller.m_templatePicker.m_activeId = QStringLiteral("test-template");
    controller.syncController()->setOffset(9.0);
    controller.syncController()->setTimeScale(1.002);
    QVERIFY(controller.selectEventRun("run-b"));
    QTRY_COMPARE(controller.vboLoadState(), QStringLiteral("ready"));
    QCOMPARE(controller.sampleCount(), 3);
    QVERIFY(controller.lapSummaries().isEmpty());
    QCOMPARE(controller.syncController()->offset(), -1.5);
    QCOMPARE(controller.syncController()->timeScale(), 1.0);
    QCOMPARE(controller.videoLoadState(), QStringLiteral("idle"));
    QVERIFY(controller.videoSource().isEmpty());
    QCOMPARE(controller.templatePicker()->activeId(), QStringLiteral("test-template"));
    QVERIFY(controller.dirty());
    QCOMPARE(controller.lastSavedRevision(), quint64(4));
    QCOMPARE(controller.m_document.m_documentId, documentId);
    const quint64 revision = controller.m_document.m_documentState.revision();
    QVERIFY(controller.selectEventRun("run-b"));
    QVERIFY(!controller.selectEventRun("not-a-run"));
    QCOMPARE(controller.m_document.m_documentState.revision(), revision);
    QVERIFY(QDir().mkpath(directory.filePath("saved")));
    const QString savedPath = directory.filePath("saved/event.fetproject");
    QVERIFY(controller.saveProject(QUrl::fromLocalFile(savedPath)));
    QVERIFY(!controller.dirty());
    const QJsonObject saved = QJsonDocument::fromJson(readBytes(savedPath)).object();
    QCOMPARE(saved.value("version").toInt(), 3);
    QVERIFY(!saved.contains("sources")); QVERIFY(!saved.contains("sync"));
    const auto runs = Fixture::runs(saved);
    QCOMPARE(runs[0].toObject().value("sync").toObject().value("offset").toDouble(), 9.0);
    QCOMPARE(Fixture::reference(runs[0].toObject(), 1).value("relativePath").toString(), QStringLiteral("../run-a.rcz"));
    QCOMPARE(runs[0].toObject().value("primaryTelemetrySourceId").toString(), QStringLiteral("run-a-source"));
    QVERIFY(controller.selectEventRun("run-a"));
    QTRY_COMPARE(controller.vboLoadState(), QStringLiteral("ready"));
    QCOMPARE(controller.syncController()->offset(), 9.0);
    QCOMPARE(controller.syncController()->timeScale(), 1.002);
    QCOMPARE(controller.lapSummaries().size(), 3);
    QCOMPARE(controller.videoLoadState(), QStringLiteral("missing"));
    QCOMPARE(controller.lastSavedRevision(), revision);
}

void SourceTests::keepsAdditionalVideosThroughRunSwitchAndSaveAs()
{
    // KAN-131: a run's additional videos and layout survive switching runs
    // and Save As, with their references rebased and their sync unchanged.
    namespace Fixture = EventProjectFixture;
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings; settings.clear(); settings.sync();
    QVERIFY(writeBytes(directory.filePath("run-a.vbo"), Fixture::lapsVbo()));
    QVERIFY(QFile::copy(QStringLiteral(TEST_FIXTURE_PATH), directory.filePath("run-b.vbo")));
    auto project = Fixture::project();
    auto runs = Fixture::runs(project);
    auto runA = runs[0].toObject();
    auto sources = runA.value("sources").toObject();
    sources.insert("additionalVideos", QJsonArray{QJsonObject{{"id", "helmet"}, {"label", "Helmet"},
        {"relativePath", "helmet.mp4"}, {"sync", QJsonObject{{"offset", -4.5}, {"timeScale", 1.0005}}}}});
    runA.insert("sources", sources);
    runA.insert("videoLayout", QJsonObject{{"mode", "sideBySide"}});
    runs[0] = runA;
    Fixture::setRuns(project, runs);
    const QString path = directory.filePath("event.fetproject");
    QVERIFY(writeBytes(path, QJsonDocument(project).toJson()));
    AppController controller(nullptr, directory.filePath("recovery.json"));
    controller.requestOpenProject(QUrl::fromLocalFile(path));
    QTRY_COMPARE(controller.vboLoadState(), QStringLiteral("ready"));
    QCOMPARE(controller.m_additionalVideos.videos().size(), 1);
    QCOMPARE(controller.m_additionalVideos.videos()[0].sync.offset, -4.5);
    QCOMPARE(controller.m_additionalVideos.layoutMode(), VideoLayoutMode::SideBySide);
    QVERIFY(controller.selectEventRun("run-b"));
    QTRY_COMPARE(controller.vboLoadState(), QStringLiteral("ready"));
    QVERIFY(controller.m_additionalVideos.videos().isEmpty());
    QCOMPARE(controller.m_additionalVideos.layoutMode(), VideoLayoutMode::PictureInPicture);
    QVERIFY(controller.selectEventRun("run-a"));
    QTRY_COMPARE(controller.vboLoadState(), QStringLiteral("ready"));
    QCOMPARE(controller.m_additionalVideos.videos().size(), 1);
    QVERIFY(QDir().mkpath(directory.filePath("saved")));
    const QString savedPath = directory.filePath("saved/event.fetproject");
    QVERIFY(controller.saveProject(QUrl::fromLocalFile(savedPath)));
    const QJsonObject saved = QJsonDocument::fromJson(readBytes(savedPath)).object();
    QVERIFY(!saved.contains("videoLayout"));
    const auto savedRuns = Fixture::runs(saved);
    const auto helmet = savedRuns[0].toObject().value("sources").toObject().value("additionalVideos").toArray()[0].toObject();
    QCOMPARE(helmet.value("relativePath").toString(), QStringLiteral("../helmet.mp4"));
    QCOMPARE(helmet.value("label").toString(), QStringLiteral("Helmet"));
    QCOMPARE(helmet.value("sync").toObject().value("offset").toDouble(), -4.5);
    QCOMPARE(helmet.value("sync").toObject().value("timeScale").toDouble(), 1.0005);
    QCOMPARE(savedRuns[0].toObject().value("videoLayout").toObject().value("mode").toString(), QStringLiteral("sideBySide"));
    QVERIFY(!savedRuns[1].toObject().value("sources").toObject().contains("additionalVideos"));
    QVERIFY(!savedRuns[1].toObject().contains("videoLayout"));
    QCOMPARE(controller.m_additionalVideos.videos()[0].reference.relativePath, QStringLiteral("../helmet.mp4"));
}

void SourceTests::addsAlignsAndChecksAdditionalVideos()
{
    // KAN-131: additional videos are added after a probe (not the main video,
    // not twice, at most three), take the main video's sync, are aligned by
    // a moment seen in both, keep their sync and layout through Save and
    // reopening, and are checked by fingerprint: a changed file is kept but
    // marked, a missing one can be relinked, a different file cannot.
    const QString ffmpeg = FfmpegTools::ffmpegPath();
    if (ffmpeg.isEmpty()) QSKIP("FFmpeg is unavailable for the additional video test.");
    QTemporaryDir directory; QVERIFY(directory.isValid());
    QSettings settings; settings.clear(); settings.sync();
    const auto encode = [&](const QString &name, const QString &source) {
        QProcess encoder;
        encoder.start(ffmpeg, {"-hide_banner", "-loglevel", "error", "-y", "-f", "lavfi", "-i", source,
            "-c:v", "libx264", "-pix_fmt", "yuv420p", directory.filePath(name)});
        return encoder.waitForFinished(30'000) && encoder.exitCode() == 0;
    };
    QVERIFY(encode("main.mp4", "testsrc2=s=320x180:r=30:d=2"));
    QVERIFY(encode("helmet.mp4", "testsrc2=s=160x120:r=25:d=3"));
    QVERIFY(encode("chest.mp4", "color=c=red:s=64x64:r=30:d=1"));
    QVERIFY(encode("rear.mp4", "color=c=blue:s=64x64:r=30:d=1"));
    QVERIFY(encode("fourth.mp4", "color=c=green:s=64x64:r=30:d=1"));
    QVERIFY(writeBytes(directory.filePath("notes.txt"), "not a video"));
    const auto url = [&](const QString &name) { return QUrl::fromLocalFile(directory.filePath(name)); };

    AppController controller(nullptr, directory.filePath("recovery.json"));
    auto *videos = controller.additionalVideoController();
    controller.loadVideo(url("main.mp4"));
    QTRY_COMPARE(controller.videoLoadState(), QStringLiteral("ready"));
    controller.syncController()->setOffset(10.0);
    videos->addVideo(url("notes.txt"));
    videos->addVideo(url("main.mp4"));
    QVERIFY(controller.statusText().contains("already a video"));
    QCOMPARE(videos->count(), 0);
    QVERIFY(!videos->loading());
    videos->addVideo(url("helmet.mp4"));
    QVERIFY(videos->loading());
    QTRY_COMPARE(videos->count(), 1);
    QVERIFY(!videos->loading());
    QVERIFY(controller.dirty());
    auto entry = videos->videoList().first().toMap();
    QCOMPARE(entry.value("state").toString(), QStringLiteral("ready"));
    QCOMPARE(entry.value("label").toString(), QStringLiteral("helmet"));
    QCOMPARE(entry.value("width").toInt(), 160);
    QVERIFY(qAbs(entry.value("durationSeconds").toDouble() - 3.0) < 0.1);
    QCOMPARE(videos->videos()[0].sync.offset, 10.0);
    QCOMPARE(videos->videos()[0].sync.timeScale, 1.0);
    videos->addVideo(url("helmet.mp4"));
    QVERIFY(controller.statusText().contains("already a video"));

    // Sync edits: invalid values are ignored; alignment keeps the time scale.
    videos->setOffset(0, std::numeric_limits<double>::quiet_NaN());
    videos->setTimeScale(0, 0.0);
    videos->setTimeScale(0, -1.0);
    QCOMPARE(videos->videos()[0].sync.offset, 10.0);
    QCOMPARE(videos->videos()[0].sync.timeScale, 1.0);
    videos->setTimeScale(0, 1.001);
    // The main video at 2.0 s is telemetry 12.0 s; the helmet shows it at 0.5 s.
    videos->alignAt(0, 2.0, 0.5);
    QVERIFY(qAbs(videos->videos()[0].sync.offset - (12.0 - 0.5 * 1.001)) < 1e-9);
    QVERIFY(qAbs(videos->videoSecondsFor(0, 2.0) - 0.5) < 1e-9);
    QVERIFY(qAbs(videos->videoSecondsFor(0, 3.0) - (1.0 / 1.001 + 0.5)) < 1e-9);
    videos->setLabel(0, QStringLiteral("  Helmet camera  "));
    QCOMPARE(videos->videos()[0].label, QStringLiteral("Helmet camera"));
    videos->setLayout(QStringLiteral("grid"));
    QCOMPARE(videos->layout(), QStringLiteral("pictureInPicture"));
    videos->setLayout(QStringLiteral("sideBySide"));
    QCOMPARE(videos->layoutMode(), VideoLayoutMode::SideBySide);
    // The preview follows the export layout and the syncs' rate.
    const auto rects = videos->previewRects(320, 180);
    QCOMPARE(rects.size(), 2);
    QCOMPARE(rects[0].toMap().value("y").toInt(), 44);
    QCOMPARE(rects[0].toMap().value("width").toInt(), 160);
    QCOMPARE(rects[1].toMap().value("x").toInt(), 160);
    QCOMPARE(rects[1].toMap().value("height").toInt(), 120);
    QVERIFY(qAbs(videos->playbackRateFor(0) - 1.0 / 1.001) < 1e-12);

    // At most three.
    videos->addVideo(url("chest.mp4"));
    QTRY_COMPARE(videos->count(), 2); // probes finish in any order; keep chest second
    videos->addVideo(url("rear.mp4"));
    QTRY_COMPARE(videos->count(), 3);
    videos->addVideo(url("fourth.mp4"));
    QVERIFY(controller.statusText().contains("at most 3"));
    QCOMPARE(videos->count(), 3);
    videos->removeVideo(2);
    QCOMPARE(videos->count(), 2);

    const QString projectPath = directory.filePath("project.fetproject");
    QVERIFY(controller.saveProject(QUrl::fromLocalFile(projectPath)));
    QVERIFY(!controller.dirty());
    const auto saved = QJsonDocument::fromJson(readBytes(projectPath)).object();
    const auto savedVideos = saved.value("sources").toObject().value("additionalVideos").toArray();
    QCOMPARE(savedVideos.size(), 2);
    QCOMPARE(savedVideos[0].toObject().value("relativePath").toString(), QStringLiteral("helmet.mp4"));
    QCOMPARE(savedVideos[0].toObject().value("label").toString(), QStringLiteral("Helmet camera"));
    QVERIFY(savedVideos[0].toObject().value("fingerprint").isObject());
    QCOMPARE(saved.value("videoLayout").toObject().value("mode").toString(), QStringLiteral("sideBySide"));

    // Reopened: both found and ready, sync and layout as saved, not dirty.
    {
        AppController reopened(nullptr, directory.filePath("recovery-2.json"));
        reopened.requestOpenProject(QUrl::fromLocalFile(projectPath));
        auto *loaded = reopened.additionalVideoController();
        QTRY_COMPARE(loaded->count(), 2);
        QTRY_VERIFY(!loaded->loading());
        QCOMPARE(loaded->videoList()[0].toMap().value("state").toString(), QStringLiteral("ready"));
        QCOMPARE(loaded->videoList()[1].toMap().value("state").toString(), QStringLiteral("ready"));
        QVERIFY(qAbs(loaded->videos()[0].sync.offset - (12.0 - 0.5 * 1.001)) < 1e-9);
        QCOMPARE(loaded->videos()[0].sync.timeScale, 1.001);
        QCOMPARE(loaded->layoutMode(), VideoLayoutMode::SideBySide);
        QVERIFY(!reopened.dirty());
    }

    // A changed file is kept but marked; a different file is not taken as a relink.
    QVERIFY(QFile::rename(directory.filePath("chest.mp4"), directory.filePath("moved-chest.mp4")));
    QVERIFY(encode("helmet.mp4", "color=c=white:s=160x120:r=25:d=3"));
    AppController changed(nullptr, directory.filePath("recovery-3.json"));
    changed.requestOpenProject(QUrl::fromLocalFile(projectPath));
    auto *checked = changed.additionalVideoController();
    QTRY_COMPARE(checked->count(), 2);
    QTRY_VERIFY(!checked->loading());
    QCOMPARE(checked->videoList()[0].toMap().value("state").toString(), QStringLiteral("mismatch"));
    QVERIFY(checked->videoList()[0].toMap().value("url").toUrl().isEmpty());
    QCOMPARE(checked->videoList()[1].toMap().value("state").toString(), QStringLiteral("missing"));
    checked->relinkVideo(1, url("rear.mp4"));
    QTRY_VERIFY(!checked->loading());
    QVERIFY(changed.statusText().contains("not the video this project saved"));
    QCOMPARE(checked->videoList()[1].toMap().value("state").toString(), QStringLiteral("missing"));
    QVERIFY(!changed.dirty());
    checked->relinkVideo(1, url("moved-chest.mp4"));
    QTRY_COMPARE(checked->videoList()[1].toMap().value("state").toString(), QStringLiteral("ready"));
    QVERIFY(changed.dirty());
    QVERIFY(changed.saveProject(QUrl::fromLocalFile(projectPath)));
    const auto relinked = QJsonDocument::fromJson(readBytes(projectPath)).object()
        .value("sources").toObject().value("additionalVideos").toArray();
    QCOMPARE(relinked[1].toObject().value("relativePath").toString(), QStringLiteral("moved-chest.mp4"));
    // The changed helmet video keeps its saved reference and sync.
    QCOMPARE(relinked[0].toObject().value("relativePath").toString(), QStringLiteral("helmet.mp4"));
    QCOMPARE(relinked[0].toObject().value("fingerprint"), savedVideos[0].toObject().value("fingerprint"));

    // A document replaced while probes run ignores their results.
    changed.additionalVideoController()->addVideo(url("rear.mp4"));
    changed.additionalVideoController()->clear();
    QTest::qWait(500);
    QCOMPARE(changed.additionalVideoController()->count(), 0);
}

void SourceTests::exportsWithAnAdditionalVideo()
{
    // KAN-131: export composes a ready additional video by the layout,
    // never writes over it, and refuses while one is missing.
    const QString ffmpeg = FfmpegTools::ffmpegPath();
    if (ffmpeg.isEmpty()) QSKIP("FFmpeg is unavailable for the additional video export test.");
    QTemporaryDir directory; QVERIFY(directory.isValid());
    QSettings settings; settings.clear(); settings.sync();
    const auto encode = [&](const QString &name, const QString &source) {
        QProcess encoder;
        encoder.start(ffmpeg, {"-hide_banner", "-loglevel", "error", "-y", "-f", "lavfi", "-i", source,
            "-c:v", "libx264", "-pix_fmt", "yuv420p", directory.filePath(name)});
        return encoder.waitForFinished(30'000) && encoder.exitCode() == 0;
    };
    QVERIFY(encode("main.mp4", "color=c=red:s=320x180:r=30:d=2"));
    QVERIFY(encode("helmet.mp4", "color=c=blue:s=160x120:r=25:d=3"));
    const auto url = [&](const QString &name) { return QUrl::fromLocalFile(directory.filePath(name)); };
    const auto projectPath = directory.filePath("helmet.fetproject");
    {
        AppController controller(nullptr, directory.filePath("recovery.json"));
        QVERIFY(controller.m_widgetModel.fromJson({})); // no widgets over the pixels checked below
        auto *videos = controller.additionalVideoController();
        controller.loadVideo(url("main.mp4"));
        QTRY_COMPARE(controller.videoLoadState(), QStringLiteral("ready"));
        controller.loadVbo(QUrl::fromLocalFile(QFINDTESTDATA("fixtures/basic.vbo")));
        QTRY_COMPARE_WITH_TIMEOUT(controller.vboLoadState(), QString("ready"), 30000);
        videos->addVideo(url("helmet.mp4"));
        QVERIFY(!controller.startExport(url("out.mp4"), 320, 180, 30, 1, 1'000'000, false, false, {}, {}, false));
        QVERIFY(controller.exporter()->error().contains("still loading"));
        QTRY_COMPARE(videos->count(), 1);
        QTRY_COMPARE(videos->videoList().first().toMap().value("state").toString(), QStringLiteral("ready"));
        videos->setLayout(QStringLiteral("sideBySide"));
        // The additional video is a source: never the output.
        QVERIFY(!controller.startExport(url("helmet.mp4"), 320, 180, 30, 1, 1'000'000, false, false, {}, {}, true));
        QVERIFY2(controller.startExport(url("out.mp4"), 320, 180, 30, 1, 1'000'000, false, false, {}, {}, false),
                 qPrintable(controller.exporter()->error()));
        QTRY_VERIFY_WITH_TIMEOUT(!controller.exporter()->exporting(), 120000);
        QVERIFY2(controller.exporter()->state() == "complete",
                 qPrintable(controller.exporter()->error() + controller.exporter()->diagnosticLog().right(3000)));
        QVERIFY(controller.exporter()->diagnosticLog().contains("Additional video placed"));
        QCOMPARE(MediaProbe::probe(directory.filePath("out.mp4"), {}, true).videoFrameCount, qsizetype(60));
        QVERIFY(controller.saveProject(QUrl::fromLocalFile(projectPath)));
    }
    // Side by side: the red main video 160x90 at y 44 in the left half, the
    // blue helmet video 160x120 at y 30 in the right half, black around them.
    QProcess decoder;
    decoder.start(ffmpeg, {"-hide_banner", "-loglevel", "error", "-i", directory.filePath("out.mp4"),
        "-frames:v", "1", "-f", "rawvideo", "-pix_fmt", "rgb24", "-"});
    QVERIFY(decoder.waitForFinished(30'000) && decoder.exitCode() == 0);
    const QByteArray frame = decoder.readAllStandardOutput();
    QCOMPARE(frame.size(), qsizetype(320 * 180 * 3));
    const auto rgb = [&](int x, int y) {
        const auto *pixel = reinterpret_cast<const unsigned char *>(frame.constData()) + (y * 320 + x) * 3;
        return std::array<int, 3>{pixel[0], pixel[1], pixel[2]};
    };
    const auto red = rgb(80, 90), blue = rgb(240, 90), aboveMain = rgb(80, 20), aboveHelmet = rgb(240, 10);
    QVERIFY(red[0] > 180 && red[2] < 80);
    QVERIFY(blue[2] > 180 && blue[0] < 80);
    QVERIFY(aboveMain[0] < 40 && aboveMain[2] < 40);
    QVERIFY(aboveHelmet[0] < 40 && aboveHelmet[2] < 40);

    // Reopened with the helmet video gone: export is refused until it is
    // located or removed.
    QVERIFY(QFile::rename(directory.filePath("helmet.mp4"), directory.filePath("moved.mp4")));
    AppController reopened(nullptr, directory.filePath("recovery-2.json"));
    reopened.requestOpenProject(QUrl::fromLocalFile(projectPath));
    QTRY_COMPARE_WITH_TIMEOUT(reopened.videoLoadState(), QString("ready"), 30000);
    QTRY_COMPARE_WITH_TIMEOUT(reopened.vboLoadState(), QString("ready"), 30000);
    QCOMPARE(reopened.additionalVideoController()->layout(), QStringLiteral("sideBySide"));
    QCOMPARE(reopened.additionalVideoController()->videoList().first().toMap().value("state").toString(), QStringLiteral("missing"));
    QVERIFY(!reopened.startExport(url("again.mp4"), 320, 180, 30, 1, 1'000'000, false, false, {}, {}, false));
    QVERIFY(reopened.exporter()->error().contains("was not found"));
    QVERIFY(!QFileInfo::exists(directory.filePath("again.mp4")));
}

void SourceTests::recoversEventAndRelinksOnlyActiveSource()
{
    namespace Fixture = EventProjectFixture;
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings; settings.clear(); settings.sync();
    const QString path = directory.filePath("event.fetproject");
    const QString recovery = directory.filePath("recovery.json");
    auto project = Fixture::project();
    // A moved replacement must still pass explicit fingerprint review, even inside an event.
    auto runs = Fixture::runs(project);
    auto second = runs[1].toObject();
    auto sources = second.value("sources").toObject();
    auto telemetry = sources.value("telemetry").toArray();
    auto source = telemetry[0].toObject();
    auto reference = source.value("reference").toObject();
    reference.insert("fingerprint", QJsonObject{{"kind", "telemetry-v1"}, {"size", 1}});
    source.insert("reference", reference); telemetry[0] = source;
    sources.insert("telemetry", telemetry); second.insert("sources", sources); runs[1] = second;
    Fixture::setRuns(project, runs);
    QVERIFY(writeBytes(path, QJsonDocument(project).toJson()));
    {
        AppController controller(nullptr, recovery);
        QVERIFY(controller.m_document.beginProjectLoad(path, project));
        QCOMPARE(controller.vboLoadState(), QStringLiteral("missing"));
        controller.syncController()->setOffset(8.0);
        QVERIFY(controller.selectEventRun("run-b"));
        QCOMPARE(controller.vboLoadState(), QStringLiteral("missing"));
        controller.m_document.writeRecoverySnapshot();
        QVERIFY(QFileInfo::exists(recovery));
    }
    AppController restored(nullptr, recovery);
    QVERIFY(restored.recoveryPending());
    QVERIFY(!restored.selectEventRun("run-a"));
    restored.resolveStartupRecovery("recover");
    QCOMPARE(restored.activeRunId(), QStringLiteral("run-b"));
    QCOMPARE(restored.lastSavedRevision(), quint64(4));
    QVERIFY(restored.dirty());
    QCOMPARE(restored.m_document.m_documentId, QStringLiteral("event-document"));
    const auto beforeRelink = restored.currentProjectObject();
    restored.relinkVbo(QUrl::fromLocalFile(QStringLiteral(TEST_FIXTURE_PATH)));
    QTRY_COMPARE(restored.vboLoadState(), QStringLiteral("mismatch"));
    QVERIFY(restored.channelNames().isEmpty());
    restored.resolveSourceMismatch(true);
    QCOMPARE(restored.vboLoadState(), QStringLiteral("ready"));
    QVERIFY(restored.saveCurrentProject());
    const auto saved = QJsonDocument::fromJson(readBytes(path)).object();
    QCOMPARE(Fixture::runs(saved)[0], Fixture::runs(beforeRelink)[0]);
    QCOMPARE(Fixture::runs(saved)[1].toObject().value("primaryTelemetrySourceId").toString(), QStringLiteral("run-b-source"));
    QVERIFY(!Fixture::reference(Fixture::runs(saved)[1].toObject()).value("fingerprint").toObject().isEmpty());
    QVERIFY(!restored.dirty());
    QVERIFY(!QFileInfo::exists(recovery));
}

void SourceTests::rejectsInvalidEventWithoutReplacingDocument()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings; settings.clear(); settings.sync();
    AppController controller(nullptr, directory.filePath("recovery.json"));
    QVERIFY(controller.m_document.beginProjectLoad(directory.filePath("event.fetproject"), EventProjectFixture::project()));
    controller.syncController()->setOffset(7.0);
    const auto before = controller.currentProjectObject();
    const quint64 generation = controller.m_document.m_sourceGeneration;
    auto malformed = EventProjectFixture::project();
    malformed.insert("sync", QJsonObject{{"offset", 999.0}});
    QVERIFY(!controller.m_document.beginProjectLoad(directory.filePath("bad.fetproject"), malformed));
    QCOMPARE(controller.currentProjectObject(), before);
    QCOMPARE(controller.m_document.m_sourceGeneration, generation);
    controller.requestNewProject();
    QCOMPARE(controller.pendingDestructiveAction(), QStringLiteral("new"));
    QVERIFY(!controller.selectEventRun("run-b"));
    QCOMPARE(controller.currentProjectObject(), before);
    controller.cancelPendingDestructiveAction();
    controller.m_document.m_documentState.restoreUnsaved(controller.projectPath().toLocalFile(), std::numeric_limits<quint64>::max(), 4);
    QVERIFY(!controller.selectEventRun("run-b"));
}

void SourceTests::rejectsLateSourceResultsAfterRunSelection()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings; settings.clear(); settings.sync();
    AppController controller(nullptr, directory.filePath("recovery.json"));
    QVERIFY(controller.m_document.beginProjectLoad(directory.filePath("event.fetproject"), EventProjectFixture::project()));
    QPromise<AppController::VboLoadResult> pending;
    pending.start();
    const auto finish = qScopeGuard([&] { pending.finish(); });
    AppController::VboLoadResult late;
    late.success = true;
    late.path = QStringLiteral(TEST_FIXTURE_PATH);
    late.generation = controller.m_document.m_sourceGeneration;
    late.session = VboParser::parseFile(late.path);
    controller.m_vboLoadCancellation = std::make_shared<std::atomic_bool>(false);
    const auto cancellation = controller.m_vboLoadCancellation;
    controller.m_vboLoadState = QStringLiteral("loading");
    controller.m_vboLoadWatcher.setFuture(pending.future());
    QSignalSpy finished(&controller.m_vboLoadWatcher, &QFutureWatcher<AppController::VboLoadResult>::finished);
    QVERIFY(controller.selectEventRun("run-b"));
    QVERIFY(cancellation->load());
    pending.addResult(late);
    pending.finish();
    QTRY_COMPARE(finished.size(), 1);
    QCOMPARE(controller.activeRunId(), QStringLiteral("run-b"));
    QCOMPARE(controller.vboLoadState(), QStringLiteral("missing"));
    QVERIFY(controller.channelNames().isEmpty());
    QVERIFY(controller.lapSummaries().isEmpty());
    QCOMPARE(controller.syncController()->offset(), -1.5);
}

void SourceTests::selectsEventRunThroughEditorQml()
{
    // Without the Lap Analysis window, the editor header picks a day's run.
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings; settings.clear(); settings.sync();
    AppController controller(nullptr, directory.filePath("recovery.json"));
    QQmlEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("appController"), &controller);
    QSignalSpy warnings(&engine, &QQmlEngine::warnings);
    QQmlComponent component(&engine, QUrl::fromLocalFile(qmlSourcePath("Main.qml")));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    std::unique_ptr<QObject> window(component.create());
    QVERIFY2(window, qPrintable(component.errorString()));
    auto *picker = window->findChild<QObject *>(QStringLiteral("editorRunPicker"));
    QVERIFY(picker);
    QVERIFY(!picker->property("visible").toBool()); // a single recording has no runs to pick
    QVERIFY(controller.m_document.beginProjectLoad(directory.filePath("event.fetproject"), EventProjectFixture::project()));
    QTRY_VERIFY(picker->property("visible").toBool());
    QCOMPARE(picker->property("count").toInt(), 2);
    QCOMPARE(picker->property("currentText").toString(), QStringLiteral("run-a"));
    QVERIFY(QMetaObject::invokeMethod(picker, "activated", Q_ARG(int, 1)));
    QCOMPARE(controller.activeRunId(), QStringLiteral("run-b"));
    QCOMPARE(picker->property("currentText").toString(), QStringLiteral("run-b"));
    controller.requestNewProject();
    QVERIFY(!picker->property("enabled").toBool());
    QVERIFY(QMetaObject::invokeMethod(picker, "activated", Q_ARG(int, 0)));
    QCOMPARE(controller.activeRunId(), QStringLiteral("run-b"));
    QCOMPARE(picker->property("currentIndex").toInt(), 1);
    controller.cancelPendingDestructiveAction();
    QCOMPARE(warnings.size(), 0);
}

namespace {
QVariantList independentBatchChoices(const QVariantList &rows, const bool skipExisting = false)
{
    QVariantList result;
    for (const auto &value : rows) {
        const auto row = value.toMap();
        if (row.value("status") != "ready") continue;
        result.append(QVariantMap{{"proposalId", row.value("proposalId")},
            {"groupId", skipExisting && row.value("existing").toBool() ? QVariant(QString{}) : row.value("proposalId")}});
    }
    return result;
}

} // namespace

void SourceTests::importsSixRunsAndAppendsWithoutDuplicates()
{
    QTemporaryDir directory; QVERIFY(directory.isValid());
    QSettings settings; settings.clear(); settings.sync();
    QList<QUrl> urls;
    for (int i = 0; i < 6; ++i) {
        const auto path = directory.filePath(QStringLiteral("run-%1.vbo").arg(i));
        QVERIFY(writeBytes(path, "[column names]\ntime velocity\n[data]\n0 " + QByteArray::number(40+i) + "\n1 50\n"));
        urls.append(QUrl::fromLocalFile(path));
    }
    urls.append(urls[0]);
    const auto bad = directory.filePath("bad.rcz"); QVERIFY(writeBytes(bad, "not a ZIP"));
    urls.append(QUrl::fromLocalFile(bad));
    AppController controller(nullptr, directory.filePath("recovery.json"));
    const auto original = controller.currentProjectObject();
    QVERIFY(controller.m_document.beginBatchImport(urls));
    QTRY_COMPARE(controller.m_document.batchImportState(), QStringLiteral("review"));
    QCOMPARE(controller.m_document.batchImportRows().size(), 8);
    QCOMPARE(controller.m_document.batchImportProcessed(), 8);
    QCOMPARE(controller.currentProjectObject(), original);
    QCOMPARE(controller.m_document.batchImportRows()[6].toMap().value("status").toString(), QStringLiteral("duplicate"));
    QCOMPARE(controller.m_document.batchImportRows()[7].toMap().value("status").toString(), QStringLiteral("error"));
    QVERIFY(controller.m_document.confirmBatchImport("Track day", false, independentBatchChoices(controller.m_document.batchImportRows())));
    QTRY_COMPARE(controller.m_document.batchImportState(), QStringLiteral("idle"));
    QTRY_COMPARE(controller.vboLoadState(), QStringLiteral("ready"));
    QCOMPARE(controller.eventRuns().size(), 6);
    QVERIFY(controller.dirty()); QVERIFY(controller.projectPath().isEmpty());
    QVERIFY(controller.videoSource().isEmpty());
    controller.m_document.writeRecoverySnapshot();
    ProjectRecoverySnapshot snapshot;
    QVERIFY(ProjectRecoveryStore(directory.filePath("recovery.json")).load(&snapshot));
    QCOMPARE(EventProjectFixture::runs(snapshot.project).size(), 6);
    QCOMPARE(snapshot.lastSavedRevision, quint64(0));
    QVERIFY(snapshot.revision > 0);
    const QString active = controller.activeRunId();
    const auto projectPath = directory.filePath("day.fetproject");
    QVERIFY(controller.saveProject(QUrl::fromLocalFile(projectPath)));
    controller.syncController()->setOffset(3.0); // Appending must retain unsaved active-run edits.
    const auto savedRuns = EventProjectFixture::runs(controller.currentProjectObject());
    const auto extra = directory.filePath("new.vbo"); QVERIFY(QFile::copy(QStringLiteral(TEST_FIXTURE_PATH), extra));
    QVERIFY(controller.m_document.beginBatchImport({urls[0], QUrl::fromLocalFile(extra)}));
    QTRY_COMPARE(controller.m_document.batchImportState(), QStringLiteral("review"));
    QVERIFY(controller.m_document.batchImportRows()[0].toMap().value("existing").toBool());
    QVERIFY(!controller.m_document.confirmBatchImport({}, true, independentBatchChoices(controller.m_document.batchImportRows())));
    QVERIFY(controller.m_document.confirmBatchImport({}, true, independentBatchChoices(controller.m_document.batchImportRows(), true)));
    QTRY_COMPARE(controller.m_document.batchImportState(), QStringLiteral("idle"));
    QCOMPARE(controller.eventRuns().size(), 7);
    QCOMPARE(controller.activeRunId(), active);
    QCOMPARE(controller.syncController()->offset(), 3.0);
    const auto appended = EventProjectFixture::runs(controller.currentProjectObject());
    for (int i = 0; i < 6; ++i) QCOMPARE(appended[i], savedRuns[i]);
    QVERIFY(controller.saveCurrentProject());
    QVERIFY(controller.m_document.beginProjectLoad(projectPath, QJsonDocument::fromJson(readBytes(projectPath)).object()));
    QTRY_COMPARE(controller.vboLoadState(), QStringLiteral("ready"));
    QCOMPARE(controller.eventRuns().size(), 7);
    QCOMPARE(controller.activeRunId(), active);
    QVERIFY(!controller.dirty());
}

void SourceTests::confirmsExplicitSourceGroups()
{
    QTemporaryDir directory; QVERIFY(directory.isValid());
    QSettings settings; settings.clear(); settings.sync();
    const auto vbo = QUrl::fromLocalFile(QStringLiteral(TEST_FIXTURE_PATH));
    const auto rcz = directory.filePath("alternative.rcz");
    QVERIFY(writeBytes(rcz, RczFixture::zip(RczFixture::members())));
    AppController controller(nullptr, directory.filePath("recovery.json"));
    QVERIFY(controller.m_document.beginBatchImport({vbo, QUrl::fromLocalFile(rcz)}));
    QTRY_COMPARE(controller.m_document.batchImportState(), QStringLiteral("review"));
    auto choices = independentBatchChoices(controller.m_document.batchImportRows());
    QCOMPARE(choices.size(), 2);
    const auto first = choices[0].toMap().value("proposalId");
    const auto second = choices[1].toMap().value("proposalId");
    // Cycles and duplicate choices must not silently lose sources.
    QVERIFY(!controller.m_document.confirmBatchImport("Day", false, {QVariantMap{{"proposalId", first}, {"groupId", second}},
        QVariantMap{{"proposalId", second}, {"groupId", first}}}));
    QVERIFY(!controller.m_document.confirmBatchImport("Day", false, {choices[0], choices[0]}));
    choices[1] = QVariantMap{{"proposalId", second}, {"groupId", first}};
    QVERIFY(controller.m_document.confirmBatchImport("Day", false, choices));
    QTRY_COMPARE(controller.m_document.batchImportState(), QStringLiteral("idle"));
    QTRY_COMPARE(controller.vboLoadState(), QStringLiteral("ready"));
    QCOMPARE(controller.eventRuns().size(), 1);
    QCOMPARE(controller.sampleCount(), 3); // Explicit VBO primary, not the RCZ alternative.
    const auto runs = EventProjectFixture::runs(controller.currentProjectObject());
    QCOMPARE(runs[0].toObject().value("sources").toObject().value("telemetry").toArray().size(), 2);
    QVERIFY(controller.m_document.m_batchPlan == nullptr);
}

void SourceTests::cancelsAndRejectsChangedBatchSources()
{
    QTemporaryDir directory; QVERIFY(directory.isValid());
    QSettings settings; settings.clear(); settings.sync();
    AppController controller(nullptr, directory.filePath("recovery.json"));
    const auto source = directory.filePath("source.vbo");
    QVERIFY(QFile::copy(QStringLiteral(TEST_FIXTURE_PATH), source));
    const auto before = controller.currentProjectObject();
    QVERIFY(controller.m_document.beginBatchImport({QUrl::fromLocalFile(source)}));
    controller.m_document.cancelBatchImport();
    QTRY_VERIFY(!controller.m_document.m_batchPending);
    QCOMPARE(controller.m_document.batchImportState(), QStringLiteral("idle"));
    QCOMPARE(controller.currentProjectObject(), before);
    QVERIFY(controller.m_document.beginBatchImport({QUrl::fromLocalFile(source)}));
    QTRY_COMPARE(controller.m_document.batchImportState(), QStringLiteral("review"));
    const auto choices = independentBatchChoices(controller.m_document.batchImportRows());
    QVERIFY(writeBytes(source, "changed"));
    QVERIFY(controller.m_document.confirmBatchImport("Day", false, choices));
    QTRY_COMPARE(controller.m_document.batchImportState(), QStringLiteral("review"));
    QVERIFY(!controller.m_document.batchImportError().isEmpty());
    QCOMPARE(controller.currentProjectObject(), before);
    QVERIFY(writeBytes(source, readBytes(QStringLiteral(TEST_FIXTURE_PATH))));
    QVERIFY(controller.m_document.confirmBatchImport("Day", false, choices));
    controller.m_document.cancelBatchImport();
    QTRY_VERIFY(!controller.m_document.m_batchPending);
    QCOMPARE(controller.currentProjectObject(), before);
    QVERIFY(controller.eventRuns().isEmpty());
}

void SourceTests::invalidatesBatchReviewAfterDocumentChanges()
{
    QTemporaryDir directory; QVERIFY(directory.isValid());
    QSettings settings; settings.clear(); settings.sync();
    AppController controller(nullptr, directory.filePath("recovery.json"));
    const QList<QUrl> urls{QUrl::fromLocalFile(QStringLiteral(TEST_FIXTURE_PATH))};
    QVERIFY(controller.m_document.beginBatchImport(urls));
    QTRY_COMPARE(controller.m_document.batchImportState(), QStringLiteral("review"));
    controller.syncController()->setOffset(8);
    QCOMPARE(controller.m_document.batchImportState(), QStringLiteral("error"));
    QCOMPARE(controller.syncController()->offset(), 8.0);
    QVERIFY(controller.m_document.beginBatchImport(urls));
    QTRY_COMPARE(controller.m_document.batchImportState(), QStringLiteral("review"));
    QVERIFY(!controller.m_document.confirmBatchImport("Day", false, independentBatchChoices(controller.m_document.batchImportRows())));
    QVERIFY(controller.m_document.batchImportError().contains("Save"));
    controller.m_document.cancelBatchImport();
    QVERIFY(controller.saveProject(QUrl::fromLocalFile(directory.filePath("old.fetproject"))));
    QVERIFY(controller.m_document.beginBatchImport(urls));
    QTRY_COMPARE(controller.m_document.batchImportState(), QStringLiteral("review"));
    QVERIFY(controller.saveProject(QUrl::fromLocalFile(directory.filePath("new.fetproject"))));
    QCOMPARE(controller.m_document.batchImportState(), QStringLiteral("error"));
    // Source generation invalidates preparation even if its file finishes later.
    QVERIFY(controller.m_document.beginBatchImport(urls));
    controller.loadVbo(urls[0]);
    QTRY_VERIFY(!controller.m_document.m_batchPending);
    QCOMPARE(controller.m_document.batchImportState(), QStringLiteral("error"));
}

void SourceTests::importsAnalysisRunsAutomatically()
{
    QTemporaryDir directory; QVERIFY(directory.isValid());
    QSettings settings; settings.clear(); settings.sync();
    const auto vbo = QUrl::fromLocalFile(QStringLiteral(TEST_FIXTURE_PATH));
    const auto rcz = directory.filePath("second.rcz");
    QVERIFY(writeBytes(rcz, RczFixture::zip(RczFixture::members())));
    const auto bad = directory.filePath("broken.rcz"); QVERIFY(writeBytes(bad, "broken"));
    AppController controller(nullptr, directory.filePath("recovery.json"));
    QSignalSpy committed(&controller.m_document, &DocumentController::batchImportCommitted);
    QVERIFY(controller.m_document.importAnalysisRuns("  Track Saturday  ", {vbo, QUrl::fromLocalFile(rcz), vbo, QUrl::fromLocalFile(bad)}));
    QTRY_COMPARE(committed.size(), 1);
    QTRY_COMPARE(controller.vboLoadState(), QStringLiteral("ready"));
    QCOMPARE(controller.eventName(), QStringLiteral("Track Saturday"));
    QCOMPARE(controller.eventRuns().size(), 2);
    QCOMPARE(controller.m_document.analysisImportMessages().size(), 2);
    QVERIFY(controller.m_document.batchImportError().isEmpty());
    QVERIFY(controller.videoSource().isEmpty());
    QVERIFY(controller.dirty());
    const auto active = controller.activeRunId();
    controller.syncController()->setOffset(4);
    const auto laps = directory.filePath("third.vbo");
    QVERIFY(writeBytes(laps, EventProjectFixture::lapsVbo()));
    QVERIFY(controller.m_document.importAnalysisRuns({}, {vbo, QUrl::fromLocalFile(laps)}));
    QTRY_COMPARE(committed.size(), 2);
    QCOMPARE(controller.eventRuns().size(), 3);
    QCOMPARE(controller.activeRunId(), active);
    QCOMPARE(controller.syncController()->offset(), 4.0);
    QCOMPARE(controller.m_document.analysisImportMessages().size(), 1);
    const auto path = directory.filePath("outing.fetproject");
    QVERIFY(controller.saveProject(QUrl::fromLocalFile(path)));
    QVERIFY(controller.m_document.beginProjectLoad(path, QJsonDocument::fromJson(readBytes(path)).object()));
    QTRY_COMPARE(controller.vboLoadState(), QStringLiteral("ready"));
    QCOMPARE(controller.eventRuns().size(), 3);
    QCOMPARE(controller.eventName(), QStringLiteral("Track Saturday"));
}

void SourceTests::guardsAutomaticAnalysisImport()
{
    QTemporaryDir directory; QVERIFY(directory.isValid());
    QSettings settings; settings.clear(); settings.sync();
    AppController controller(nullptr, directory.filePath("recovery.json"));
    const QList<QUrl> urls{QUrl::fromLocalFile(QStringLiteral(TEST_FIXTURE_PATH))};
    QVERIFY(!controller.m_document.importAnalysisRuns(" ", urls));
    QVERIFY(controller.eventRuns().isEmpty());
    QVERIFY(controller.m_document.importAnalysisRuns("Cancelled", urls));
    controller.m_document.cancelBatchImport();
    QTRY_VERIFY(!controller.m_document.m_batchPending);
    QVERIFY(controller.eventRuns().isEmpty());
    QVERIFY(!controller.dirty());
    QVERIFY(controller.m_document.importAnalysisRuns("Stale", urls));
    controller.syncController()->setOffset(2);
    QTRY_VERIFY(!controller.m_document.m_batchPending);
    QVERIFY(controller.eventRuns().isEmpty());
    QCOMPARE(controller.syncController()->offset(), 2.0);
    QVERIFY(!controller.m_document.importAnalysisRuns("Dirty", urls));
    QVERIFY(controller.m_document.batchImportError().contains("Save"));
    QVERIFY(controller.saveProject(QUrl::fromLocalFile(directory.filePath("old.fetproject"))));
    const auto bad = directory.filePath("bad.rcz"); QVERIFY(writeBytes(bad, "broken"));
    const auto before = controller.currentProjectObject();
    QVERIFY(controller.m_document.importAnalysisRuns("Broken", {QUrl::fromLocalFile(bad)}));
    QTRY_VERIFY(!controller.m_document.m_batchPending);
    QVERIFY(!controller.m_document.batchImportError().isEmpty());
    QCOMPARE(controller.m_document.analysisImportMessages().size(), 1);
    QCOMPARE(controller.currentProjectObject(), before);
    QVERIFY(controller.eventRuns().isEmpty());
}

namespace {
// Since KAN-166 a day's decisions (exclusions, notes, track configuration)
// come from FlappedEar Telemetry. The reference analysis, TelemetryController,
// stands in for it: this imports a day there and waits for its laps.
bool importReferenceDay(TelemetryController &reference, const QString &name, const QList<QUrl> &urls, const int timeout = 30000)
{
    QSignalSpy committed(reference.document(), &DocumentController::batchImportCommitted);
    if (!reference.document()->importAnalysisRuns(name, urls)) return false;
    const auto &analysis = *reference.analysis();
    return QTest::qWaitFor([&] { return committed.size() == 1; }, timeout)
        && QTest::qWaitFor([&] { return !analysis.outingLapsLoading() && !analysis.outingLaps().isEmpty(); }, timeout);
}

} // namespace

void SourceTests::findsTheDayBestLapWithoutTheAnalysis()
{
    // KAN-185: the export dialog's day's best lap comes from lap detection
    // alone, and matches the analysis ranking through exclusions and renames.
    // Since KAN-166 those decisions come from FlappedEar Telemetry; the
    // reference analysis (TelemetryController) stands in for it here.
    QTemporaryDir directory; QVERIFY(directory.isValid());
    QSettings settings; settings.clear(); settings.sync();
    {
        AppController empty(nullptr, directory.filePath("empty.json"));
        QCOMPARE(empty.dayBestLap().value("state").toString(), QString("idle"));
        empty.requestDayBestLap();
        QCOMPARE(empty.dayBestLap().value("state").toString(), QString("none"));
    }
    const auto first = directory.filePath("first.vbo"), second = directory.filePath("second.vbo");
    QVERIFY(writeBytes(first, EventProjectFixture::routeVbo()));
    QVERIFY(writeBytes(second, EventProjectFixture::routeVbo(130, -2, 2)));
    TelemetryController reference(directory.filePath("reference.json"));
    auto &referenceDocument = *reference.document();
    auto &analysis = *reference.analysis();
    QVERIFY(importReferenceDay(reference, "Best lap", {QUrl::fromLocalFile(first), QUrl::fromLocalFile(second)}));
    QTRY_COMPARE(analysis.outingRanking().value("state").toString(), QString("available"));
    const auto dayPath = directory.filePath("day.fetproject");
    QVERIFY(referenceDocument.saveProject(QUrl::fromLocalFile(dayPath)));

    AppController controller(nullptr, directory.filePath("recovery.json"));
    controller.requestOpenProject(QUrl::fromLocalFile(dayPath));
    QTRY_COMPARE(controller.eventRuns().size(), 2);
    QTRY_COMPARE(controller.vboLoadState(), QString("ready"));
    QCOMPARE(controller.dayBestLap().value("state").toString(), QString("idle")); // nothing derived until asked
    QSignalSpy changed(&controller, &AppController::dayBestLapChanged);
    controller.requestDayBestLap();
    QTRY_COMPARE(controller.dayBestLap().value("state").toString(), QString("available"));
    QVERIFY(changed.size() >= 1);
    const auto expected = analysis.outingRanking().value("bestOfDay").toMap();
    auto best = controller.dayBestLap().value("bestOfDay").toMap();
    QVERIFY(!expected.isEmpty());
    for (const auto *field : {"reference", "runId", "runName", "lapNumber", "durationSeconds", "groupId"})
        QCOMPARE(best.value(field), expected.value(field));

    // An exclusion made in Telemetry re-ranks without deriving the laps again.
    const auto serial = controller.m_bestLapFinder.m_derived.runs.value(best.value("runId").toString()).derivationSerial;
    QVERIFY(analysis.setOutingLapExcluded(best.value("reference").toMap(), true, "Traffic"));
    QTRY_COMPARE(analysis.outingRanking().value("state").toString(), QString("available"));
    auto project = controller.m_document.analysisProject();
    auto event = project.value("event").toObject();
    event.insert("lapExclusions", referenceDocument.analysisProject().value("event").toObject().value("lapExclusions"));
    project.insert("event", event);
    controller.m_document.commitAnalysisProject(project);
    QTRY_VERIFY(controller.dayBestLap().value("bestOfDay").toMap().value("reference") != best.value("reference"));
    QCOMPARE(controller.dayBestLap().value("bestOfDay").toMap().value("reference"),
        analysis.outingRanking().value("bestOfDay").toMap().value("reference"));
    QCOMPARE(controller.m_bestLapFinder.m_derived.runs.value(best.value("runId").toString()).derivationSerial, serial);

    // A run renamed in Telemetry shows its new name.
    best = controller.dayBestLap().value("bestOfDay").toMap();
    const auto runId = best.value("runId").toString();
    project = controller.m_document.analysisProject();
    event = project.value("event").toObject();
    auto runs = event.value("runs").toArray();
    for (qsizetype i = 0; i < runs.size(); ++i) {
        auto run = runs[i].toObject();
        if (run.value("id").toString() == runId) { run.insert("name", "Renamed run"); runs[i] = run; }
    }
    event.insert("runs", runs); project.insert("event", event);
    controller.m_document.commitAnalysisProject(project);
    QTRY_COMPARE(controller.dayBestLap().value("bestOfDay").toMap().value("runName").toString(), QString("Renamed run"));
    QCOMPARE(controller.dayBestLap().value("bestOfDay").toMap().value("reference"), best.value("reference"));
}

void SourceTests::restoresDayDecisionsAfterMoveMissingRelinkAndRecovery()
{
    // Decisions made in FlappedEar Telemetry (exclusions, notes, track
    // configurations, the comparison group) survive Overlays' Save As, a move,
    // a missing and relinked recording, a refused wrong relink and recovery,
    // and keep excluding the lap from the editor's timing.
    QTemporaryDir directory; QVERIFY(directory.isValid());
    QSettings settings; settings.clear(); settings.sync();
    const auto original = directory.filePath("original"), moved = directory.filePath("moved");
    QVERIFY(QDir().mkpath(original)); QVERIFY(QDir().mkpath(QDir(original).filePath("save-as")));
    const auto first = QDir(original).filePath("first.vbo"), second = QDir(original).filePath("second.vbo");
    QVERIFY(writeBytes(first, EventProjectFixture::lapsVbo()));
    QVERIFY(writeBytes(second, EventProjectFixture::lapsVbo().replace("52.0008", "52.0007")));
    const auto recovery = directory.filePath("recovery.json");
    const auto savedPath = QDir(original).filePath("day.fetproject");
    QString runA, runB, group;
    QJsonObject savedEvent;
    {
        TelemetryController reference(directory.filePath("reference.json"));
        auto &analysis = *reference.analysis();
        QVERIFY(importReferenceDay(reference, "Portable decisions", {QUrl::fromLocalFile(first), QUrl::fromLocalFile(second)}));
        QTRY_COMPARE(analysis.outingLaps().size(), 10);
        runA = reference.document()->eventRuns()[0].toMap().value("id").toString();
        runB = reference.document()->eventRuns()[1].toMap().value("id").toString();
        QVERIFY(analysis.setRunTrackConfiguration(runA, "Circuit A", "clockwise"));
        QVERIFY(analysis.setRunTrackConfiguration(runB, "Circuit B", "counterclockwise"));
        QTRY_VERIFY(!analysis.outingLapsLoading() && analysis.m_outingLapRequestedKey == analysis.outingLapKey());
        QVariantMap excludedA, excludedB;
        for (const auto &value : analysis.outingLaps()) {
            const auto row = value.toMap(); if (row.value("type") != "LAP") continue;
            if (row.value("runId") == runA) { excludedA = row.value("reference").toMap(); group = row.value("compatibilityGroupId").toString(); }
            else excludedB = row.value("reference").toMap();
        }
        QVERIFY(analysis.setOutingLapExcluded(excludedA, true, "Traffic A"));
        QVERIFY(analysis.setOutingLapExcluded(excludedB, true, "Cooldown B"));
        for (const auto &id : {runA, runB}) {
            const auto metadata = analysis.runMetadata(id);
            QVERIFY(analysis.updateRunMetadata(id, metadata.value("editToken").toString(), metadata.value("name").toString(),
                id == runA ? "Morning notes" : "Afternoon notes", "Dry", "No changes"));
        }
        QVERIFY(analysis.selectOutingComparisonGroup(group));
        QVERIFY(reference.document()->saveProject(QUrl::fromLocalFile(savedPath)));
        savedEvent = QJsonDocument::fromJson(readBytes(savedPath)).object().value("event").toObject();
        QVERIFY(!savedEvent.value("analysisDecisions").toObject().isEmpty());
        QCOMPARE(savedEvent.value("lapExclusions").toArray().size(), 2);
    }
    // The decisions Overlays must carry unchanged, and its view of them.
    const auto sameDecisions = [&](AppController &controller) {
        const auto event = controller.currentProjectObject().value("event").toObject();
        if (event.value("analysisDecisions") != savedEvent.value("analysisDecisions")) return QString("analysisDecisions changed");
        if (event.value("lapExclusions") != savedEvent.value("lapExclusions")) return QString("lapExclusions changed");
        const auto runs = event.value("runs").toArray(), savedRuns = savedEvent.value("runs").toArray();
        if (runs.size() != savedRuns.size()) return QString("run count changed");
        for (qsizetype i = 0; i < runs.size(); ++i)
            for (const auto *field : {"id", "name", "notes", "trackConfiguration"})
                if (runs[i].toObject().value(field) != savedRuns[i].toObject().value(field)) return QString("run ") + field + " changed";
        return QString();
    };
    const auto excludedInEditor = [](AppController &controller) {
        qsizetype excluded = 0;
        for (const auto &lap : controller.m_lapSession.timedLaps) excluded += lap.referenceEligible() ? 0 : 1;
        return excluded;
    };
    {
        AppController controller(nullptr, recovery);
        controller.requestOpenProject(QUrl::fromLocalFile(savedPath));
        QTRY_COMPARE(controller.vboLoadState(), QString("ready"));
        QCOMPARE(controller.activeRunId(), runA);
        QCOMPARE(excludedInEditor(controller), qsizetype(1));
        const auto saveAs = QDir(original).filePath("save-as/day.fetproject");
        QVERIFY(controller.saveProject(QUrl::fromLocalFile(saveAs)));
        const auto failure = sameDecisions(controller); QVERIFY2(failure.isEmpty(), qPrintable(failure));
        const auto runs = QJsonDocument::fromJson(readBytes(saveAs)).object().value("event").toObject().value("runs").toArray();
        for (qsizetype i = 0; i < runs.size(); ++i)
            QCOMPARE(EventProjectFixture::reference(runs[i].toObject()).value("relativePath").toString(),
                i == 0 ? QString("../first.vbo") : QString("../second.vbo"));
    }
    QVERIFY(QDir().rename(original, moved));
    const auto movedProject = QDir(moved).filePath("save-as/day.fetproject");
    settings.setValue("project/path", movedProject); settings.sync();
    {
        AppController controller(nullptr, recovery);
        QTRY_COMPARE(controller.vboLoadState(), QString("ready")); QVERIFY(!controller.dirty());
        QCOMPARE(excludedInEditor(controller), qsizetype(1));
        const auto failure = sameDecisions(controller); QVERIFY2(failure.isEmpty(), qPrintable(failure));
    }
    const auto missing = QDir(moved).filePath("first.vbo"), relocated = QDir(moved).filePath("relocated.vbo");
    QVERIFY(QFile::rename(missing, relocated));
    {
        AppController controller(nullptr, recovery);
        QTRY_COMPARE(controller.vboLoadState(), QString("missing"));
        QVERIFY(!controller.dirty());
        auto failure = sameDecisions(controller); QVERIFY2(failure.isEmpty(), qPrintable(failure));
        // The other run's recording is refused by identity.
        controller.relinkVbo(QUrl::fromLocalFile(QDir(moved).filePath("second.vbo")));
        QTRY_COMPARE(controller.sourceMismatchType(), QString("telemetry"));
        controller.resolveSourceMismatch(false);
        QVERIFY(controller.vboLoadState() != "ready");
        controller.relinkVbo(QUrl::fromLocalFile(relocated));
        QTRY_COMPARE(controller.vboLoadState(), QString("ready"));
        QCOMPARE(excludedInEditor(controller), qsizetype(1));
        QVERIFY(controller.saveCurrentProject()); QVERIFY(!controller.dirty());
        failure = sameDecisions(controller); QVERIFY2(failure.isEmpty(), qPrintable(failure));
        QVERIFY(controller.widgetModel()->addWidget("lapCurrent") >= 0);
        QVERIFY(controller.dirty());
        controller.m_document.writeRecoverySnapshot(); QVERIFY(QFileInfo::exists(recovery));
    }
    {
        AppController recovered(nullptr, recovery); QVERIFY(recovered.recoveryPending());
        recovered.resolveStartupRecovery("recover");
        QTRY_COMPARE(recovered.vboLoadState(), QString("ready"));
        QVERIFY(recovered.dirty());
        QCOMPARE(excludedInEditor(recovered), qsizetype(1));
        const auto failure = sameDecisions(recovered); QVERIFY2(failure.isEmpty(), qPrintable(failure));
        QVERIFY(recovered.saveCurrentProject()); QVERIFY(!recovered.dirty());
    }
    AppController clean(nullptr, recovery);
    QVERIFY(!clean.recoveryPending()); QTRY_COMPARE(clean.vboLoadState(), QString("ready"));
    QVERIFY(!clean.dirty());
    const auto failure = sameDecisions(clean); QVERIFY2(failure.isEmpty(), qPrintable(failure));
}

void SourceTests::keepsEveryTelemetryFieldThroughAnOverlayEdit()
{
    // KAN-170: a day saved by FlappedEar Telemetry, carrying every analysis
    // field and newer versions of the versioned ones, opens in Overlays.
    // After an overlay edit and a save, everything Overlays does not own is
    // unchanged; the newer fields are kept, not applied.
    QTemporaryDir directory; QVERIFY(directory.isValid());
    QSettings settings; settings.clear(); settings.sync();
    const auto first = directory.filePath("first.vbo"), second = directory.filePath("second.vbo");
    QVERIFY(writeBytes(first, EventProjectFixture::lapsVbo()));
    QVERIFY(writeBytes(second, EventProjectFixture::lapsVbo().replace("52.0008", "52.0007")));
    const auto dayPath = directory.filePath("day.fetproject");
    QString group;
    {
        TelemetryController reference(directory.filePath("reference.json"));
        auto &analysis = *reference.analysis();
        QVERIFY(importReferenceDay(reference, "Shared day", {QUrl::fromLocalFile(first), QUrl::fromLocalFile(second)}));
        QTRY_COMPARE(analysis.outingLaps().size(), 10);
        const auto runA = reference.document()->eventRuns()[0].toMap().value("id").toString();
        QVERIFY(analysis.setRunTrackConfiguration(runA, "Circuit A", "clockwise"));
        QTRY_VERIFY(!analysis.outingLapsLoading() && analysis.m_outingLapRequestedKey == analysis.outingLapKey());
        for (const auto &value : analysis.outingLaps()) {
            const auto row = value.toMap();
            if (row.value("type") != "LAP" || row.value("runId") != runA) continue;
            group = row.value("compatibilityGroupId").toString();
            QVERIFY(analysis.setOutingLapExcluded(row.value("reference").toMap(), true, "Traffic"));
            break;
        }
        const auto metadata = analysis.runMetadata(runA);
        QVERIFY(analysis.updateRunMetadata(runA, metadata.value("editToken").toString(), "Morning", "Notes", "Dry", "Softer rear"));
        QVERIFY(analysis.selectOutingComparisonGroup(group));
        QVERIFY(reference.document()->saveProject(QUrl::fromLocalFile(dayPath)));
    }
    // What a newer Telemetry may add: the remaining analysis fields, newer
    // versions of the versioned ones and keys this build does not know.
    auto day = QJsonDocument::fromJson(readBytes(dayPath)).object();
    auto event = day.value("event").toObject();
    auto runs = event.value("runs").toArray();
    QCOMPARE(runs.size(), 2);
    auto runA = runs[0].toObject(), runB = runs[1].toObject();
    runA.insert("trackSegments", QJsonArray{QJsonObject{{"id", "s1"}, {"type", "sector"}, {"name", "Sector 1"},
        {"startProgressMeters", 0.0}, {"endProgressMeters", 40.0}, {"trackConfigurationReference", group}}});
    runA.insert("trackSegmentReview", QJsonObject{{"version", "track-segment-review-v1"},
        {"trackConfigurationReference", group}, {"proposalAlgorithm", "proposals-v1"}, {"rejected", QJsonArray{}}});
    runA.insert("futureRunField", QJsonObject{{"kept", true}});
    runB.insert("trackSegments", QJsonArray{QJsonObject{{"id", "s2"}, {"type", "corner"}, {"name", "Turn 1"},
        {"startProgressMeters", 10.0}, {"endProgressMeters", 30.0}, {"trackConfigurationReference", "compatibility-v2:next"}}});
    runB.insert("trackSegmentReview", QJsonObject{{"version", "track-segment-review-v2"}, {"axis", QJsonObject{{"lap", 3}}}});
    runB.insert("trackInference", QJsonObject{{"algorithm", "gps-route-v2"}, {"layoutId", "gps-route-v2:next"}});
    runB.insert("fusion", QJsonObject{{"algorithm", "channel-fusion-v2"}, {"sources", QJsonArray{"a", "b"}}});
    runs[0] = runA; runs[1] = runB;
    event.insert("runs", runs);
    auto decisions = event.value("analysisDecisions").toObject();
    decisions.insert("comparisonRange", QJsonObject{{"startMeters", 5.0}, {"endMeters", 60.0}});
    decisions.insert("comparisonChannels", QJsonArray{"speed"});
    event.insert("analysisDecisions", decisions);
    event.insert("futureEventField", 3);
    day.insert("event", event);
    day.insert("futureRootField", QJsonObject{{"kept", true}});
    day.remove("scene"); // Telemetry has no editor and may write no scene.
    QString error; QVERIFY2(ProjectLimits::validateProject(day, &error), qPrintable(error));
    QVERIFY(writeBytes(dayPath, QJsonDocument(day).toJson()));
    // Everything but what Overlays owns: the scene, the chart channels, the
    // map and export settings, each run's sync and video, and the revision.
    const auto notOverlays = [](QJsonObject project) {
        for (const auto *key : {"scene", "analysis", "mapSettings", "exportSettings", "documentState"}) project.remove(key);
        auto event = project.value("event").toObject();
        auto runs = event.value("runs").toArray();
        for (qsizetype i = 0; i < runs.size(); ++i) {
            auto run = runs[i].toObject(); run.remove("sync");
            auto sources = run.value("sources").toObject(); sources.remove("video");
            run.insert("sources", sources); runs[i] = run;
        }
        event.insert("runs", runs); project.insert("event", event);
        return project;
    };

    AppController controller(nullptr, directory.filePath("recovery.json"));
    controller.requestOpenProject(QUrl::fromLocalFile(dayPath));
    QTRY_COMPARE(controller.vboLoadState(), QString("ready"));
    QVERIFY(!controller.dirty());
    controller.syncController()->setOffset(4.5);
    QVERIFY(controller.widgetModel()->addWidget("lapCurrent") >= 0);
    QVERIFY(controller.saveCurrentProject());
    const auto saved = QJsonDocument::fromJson(readBytes(dayPath)).object();
    QCOMPARE(notOverlays(saved), notOverlays(day));
    QCOMPARE(saved.value("event").toObject().value("runs").toArray()[0].toObject().value("sync").toObject().value("offset").toDouble(), 4.5);
    QVERIFY(!saved.value("scene").toObject().value("widgets").toArray().isEmpty());
    // The newer fusion is not applied: the day's laps derive from each primary.
    controller.requestDayBestLap();
    QTRY_COMPARE(controller.dayBestLap().value("state").toString(), QString("available"));
    QVERIFY(controller.selectEventRun(runB.value("id").toString()));
    QTRY_COMPARE(controller.vboLoadState(), QString("ready"));
}

void SourceTests::automaticallyGroupsPrivateTrackDay()
{
    const auto path = qEnvironmentVariable("FLAPPEDEAR_REAL_DAY");
    if (path.isEmpty()) QSKIP("FLAPPEDEAR_REAL_DAY is not set");
    QTemporaryDir directory; QVERIFY(directory.isValid());
    QSettings settings; settings.clear(); settings.sync();
    QList<QUrl> recordings;
    for (const auto &name : QDir(path).entryList({"*.vbo"}, QDir::Files, QDir::Name)) recordings.append(QUrl::fromLocalFile(QDir(path).filePath(name)));
    QVERIFY(recordings.size() > 1);
    // The reference analysis groups the day and ranks it.
    TelemetryController reference(directory.filePath("reference.json"));
    auto &analysis = *reference.analysis();
    QVERIFY(importReferenceDay(reference, "Local track day", recordings, 120000));
    QTRY_VERIFY_WITH_TIMEOUT(!analysis.outingLapsLoading() && analysis.m_outingLapRequestedKey == analysis.outingLapKey(), 120000);
    for (auto it = analysis.m_outingRunCache.cbegin(); it != analysis.m_outingRunCache.cend(); ++it)
        qInfo() << "Run route:" << it->inference.route.lengthMeters << it->inference.route.direction
            << "supported laps:" << it->inference.matchingLaps.size() << it->inference.reason;
    qInfo() << "Groups:" << analysis.outingCompatibilityGroups().size() << "Ranking:" << analysis.outingRanking().value("state")
        << "Eligible:" << analysis.outingRanking().value("eligibleLapCount") << analysis.outingLapMessages();
    QCOMPARE(analysis.outingCompatibilityGroups().size(), 1);
    QCOMPARE(analysis.outingRanking().value("state").toString(), QString("available"));
    QCOMPARE(analysis.outingRanking().value("runs").toList().size(), recordings.size());
    QCOMPARE(analysis.outingProgression().value("runs").toList().size(), recordings.size());
    QCOMPARE(analysis.outingCompatibilityGroups().first().toMap().value("eligibleLapCount"),
        analysis.outingRanking().value("eligibleLapCount"));
    const auto dayPath = directory.filePath("day.fetproject");
    QVERIFY(reference.document()->saveProject(QUrl::fromLocalFile(dayPath)));
    // KAN-185: Overlays' automatic best lap is the analysis's best of the day.
    AppController controller(nullptr, directory.filePath("recovery.json"));
    controller.requestOpenProject(QUrl::fromLocalFile(dayPath));
    QTRY_COMPARE_WITH_TIMEOUT(controller.eventRuns().size(), recordings.size(), 120000);
    controller.requestDayBestLap();
    QTRY_COMPARE_WITH_TIMEOUT(controller.dayBestLap().value("state").toString(), QString("available"), 120000);
    qInfo() << "Day's best lap:" << controller.dayBestLap().value("bestOfDay").toMap().value("runName")
            << controller.dayBestLap().value("bestOfDay").toMap().value("lapNumber")
            << controller.dayBestLap().value("bestOfDay").toMap().value("durationSeconds");
    QCOMPARE(controller.dayBestLap().value("bestOfDay").toMap().value("reference"),
        analysis.outingRanking().value("bestOfDay").toMap().value("reference"));
    const auto output = qEnvironmentVariable("FLAPPEDEAR_DAY_REVIEW_PROJECT");
    if (!output.isEmpty()) QVERIFY(controller.saveProject(QUrl::fromLocalFile(output)));
}

void SourceTests::lapExclusionPolicySharesRankingAndRenderInputs()
{
    LapSession laps;
    laps.status = LapSessionStatus::Available;
    laps.acceptedPasses = {{1}, {5}, {10}, {16}};
    laps.timedLaps = {{1, 1, 5, 4, 0}, {2, 5, 10, 5, 0}, {3, 10, 16, 6, 0}};
    laps.timedLaps[2].referenceIssue = LapReferenceIssue::GpsGap;
    OutingLapRow row; row.runId = "run"; row.type = LapSectionType::Lap; row.start = 1; row.end = 5;
    const auto reference = makeLapReference(row, "event", "source", QByteArray(64, 'a'), QByteArray(64, 'b'));
    const QJsonArray exclusions{QJsonObject{{"reference", reference}, {"reason", "Traffic"}}};
    applyLapExclusions(laps, reference, exclusions);
    QCOMPARE(laps.timedLaps.size(), 3);
    QCOMPARE(eligibleLapIndices(laps), QVector<qsizetype>{1});
    QCOMPARE(laps.fastestLapIndex, std::optional<qsizetype>(1));
    QVERIFY(!laps.timedLaps[0].referenceEligible());
    QVERIFY(!laps.timedLaps[2].referenceEligible());
    TelemetrySession source; source.duration = 20;
    TelemetryRenderContext preview, offscreen;
    for (auto *context : {&preview, &offscreen}) {
        context->setSession(&source); context->setLapSession(laps); context->setTime(16);
    }
    QCOMPARE(preview.lapTiming(), offscreen.lapTiming());
    QCOMPARE(preview.lapTiming().value("bestLapSeconds").toDouble(), 5.0);
    row.start = 5; row.end = 10;
    auto all = exclusions;
    all.append(QJsonObject{{"reference", makeLapReference(row, "event", "source", QByteArray(64, 'a'), QByteArray(64, 'b'))}, {"reason", "Cooldown"}});
    applyLapExclusions(laps, reference, all);
    QVERIFY(eligibleLapIndices(laps).isEmpty()); QVERIFY(!laps.fastestLapIndex);
    for (const auto &lap : laps.timedLaps) QCOMPARE(lap.deltaToBestSeconds, 0.0);
    applyLapExclusions(laps, reference, {});
    QCOMPARE(eligibleLapIndices(laps), (QVector<qsizetype>{0, 1}));
    QCOMPARE(laps.fastestLapIndex, std::optional<qsizetype>(0));
    QVERIFY(!laps.timedLaps[2].referenceEligible());
    auto stale = reference; stale.insert("sourceRevision", QString(64, 'c'));
    applyLapExclusions(laps, stale, exclusions);
    QVERIFY(laps.timedLaps[0].referenceEligible());
}

void SourceTests::lapExclusionsSurviveSaveRecoveryAndInvalidateSafely()
{
    // An exclusion made in FlappedEar Telemetry removes the lap from the
    // editor's timing through save and recovery, and stops applying (while
    // staying saved) once its reference no longer matches the run.
    QTemporaryDir directory; QVERIFY(directory.isValid());
    QSettings settings; settings.clear(); settings.sync();
    const auto path = directory.filePath("run.vbo"); QVERIFY(writeBytes(path, EventProjectFixture::lapsVbo()));
    const auto savedPath = directory.filePath("day.fetproject"); const auto recoveryPath = directory.filePath("recovery.json");
    {
        TelemetryController reference(directory.filePath("reference.json"));
        auto &analysis = *reference.analysis();
        QVERIFY(importReferenceDay(reference, "Exclusions", {QUrl::fromLocalFile(path)}));
        QTRY_COMPARE(analysis.outingLaps().size(), 5);
        QVERIFY(analysis.setOutingLapExcluded(analysis.outingLaps()[1].toMap().value("reference").toMap(), true, "Traffic"));
        QVERIFY(reference.document()->saveProject(QUrl::fromLocalFile(savedPath)));
    }
    {
        AppController controller(nullptr, recoveryPath);
        controller.requestOpenProject(QUrl::fromLocalFile(savedPath));
        QTRY_COMPARE(controller.vboLoadState(), QString("ready"));
        QVERIFY(!controller.m_lapSession.timedLaps[0].referenceEligible());
        QVERIFY(controller.m_lapSession.timedLaps[1].referenceEligible());
        QVERIFY(controller.widgetModel()->addWidget("lapCurrent") >= 0);
        controller.m_document.writeRecoverySnapshot(); QVERIFY(QFileInfo::exists(recoveryPath));
    }
    {
        AppController controller(nullptr, recoveryPath); QVERIFY(controller.recoveryPending());
        controller.resolveStartupRecovery("recover");
        QTRY_COMPARE(controller.vboLoadState(), QString("ready"));
        QVERIFY(controller.dirty());
        QVERIFY(!controller.m_lapSession.timedLaps[0].referenceEligible());
        // Another track configuration changes the lap identity: the exclusion is stale.
        auto project = controller.m_document.analysisProject();
        auto runs = EventProjectFixture::runs(project); auto run = runs[0].toObject();
        auto configuration = EventProjectCodec::trackConfiguration(run);
        configuration.insert("layoutId", "other-layout");
        run.insert("trackConfiguration", configuration); runs[0] = run; EventProjectFixture::setRuns(project, runs);
        controller.m_document.commitAnalysisProject(project);
        QTRY_VERIFY(controller.m_lapSession.timedLaps[0].referenceEligible());
        QCOMPARE(controller.currentProjectObject().value("event").toObject().value("lapExclusions").toArray().size(), 1);
    }
}

void SourceTests::keepsAnalysisStateThroughOverlayEdits()
{
    // KAN-166 steps 1 and 2: the editor reads lap exclusions from the document,
    // and an overlay edit saves every analysis field it did not touch unchanged.
    QTemporaryDir directory; QVERIFY(directory.isValid());
    QSettings settings; settings.clear(); settings.sync();
    const auto path = directory.filePath("run.vbo"); QVERIFY(writeBytes(path, EventProjectFixture::lapsVbo()));
    const auto analysedPath = directory.filePath("analysed.fetproject"), editedPath = directory.filePath("edited.fetproject");
    QJsonObject analysed;
    {
        TelemetryController reference(directory.filePath("reference.json"));
        auto &analysis = *reference.analysis();
        QVERIFY(importReferenceDay(reference, "Round trip", {QUrl::fromLocalFile(path)}));
        QTRY_COMPARE(analysis.outingLaps().size(), 5);
        QVERIFY(analysis.setOutingLapExcluded(analysis.outingLaps()[1].toMap().value("reference").toMap(), true, "Traffic"));
        QVERIFY(reference.document()->saveProject(QUrl::fromLocalFile(analysedPath)));
        analysed = QJsonDocument::fromJson(readBytes(analysedPath)).object();
        // The editor's document binding names the same recording the analysis loads.
        AppController controller(nullptr, directory.filePath("recovery.json"));
        controller.requestOpenProject(QUrl::fromLocalFile(analysedPath));
        QTRY_COMPARE(controller.vboLoadState(), QString("ready"));
        const auto binding = controller.activeLapBinding();
        QJsonObject analysisSource;
        for (const auto &value : analysis.outingLapSources())
            if (value.toObject().value("runId").toString() == controller.activeRunId()) analysisSource = value.toObject();
        QVERIFY(!analysisSource.isEmpty());
        for (const auto *field : {"eventId", "runId", "sourceId", "derivationKey"})
            QCOMPARE(binding.value(field), analysisSource.value(field));
        QCOMPARE(binding.value("expectedRevision"), analysisSource.value("expectedRevision"));
        QCOMPARE(binding.value("sourceRevision").toString(), QString::fromLatin1(controller.m_loadedSourceRevision));
        QVERIFY(!controller.m_lapSession.timedLaps[0].referenceEligible());
    }
    QCOMPARE(analysed.value("version").toInt(), 3);
    // Fields a newer analysis app writes ride along untouched.
    auto event = analysed.value("event").toObject();
    event.insert("futureAnalysis", QJsonObject{{"kept", true}});
    auto runs = event.value("runs").toArray(); auto run = runs[0].toObject();
    run.insert("futureRunAnalysis", 3); runs[0] = run; event.insert("runs", runs);
    analysed.insert("event", event);
    // Step 2: chart channels the editor cannot resolve are saved as loaded.
    analysed.insert("analysis", QJsonObject{{"channels", QJsonArray{"notInThisRecording", "speed"}}, {"futureSetting", 1}});
    QVERIFY(writeBytes(analysedPath, QJsonDocument(analysed).toJson()));
    {
        AppController controller(nullptr, directory.filePath("edit-recovery.json"));
        QVERIFY(controller.m_document.beginProjectLoad(analysedPath, analysed));
        QTRY_COMPARE(controller.vboLoadState(), QString("ready"));
        QVERIFY(!controller.m_lapSession.timedLaps[0].referenceEligible());
        QVERIFY(controller.widgetModel()->addWidget("lapCurrent") >= 0);
        QVERIFY(controller.saveProject(QUrl::fromLocalFile(editedPath)));
    }
    auto edited = QJsonDocument::fromJson(readBytes(editedPath)).object();
    QVERIFY(edited.value("scene").toObject().value("widgets").toArray().size()
        > analysed.value("scene").toObject().value("widgets").toArray().size());
    for (const auto *key : {"scene", "documentState"}) { edited.remove(key); analysed.remove(key); }
    QCOMPARE(edited, analysed);
}

void SourceTests::recordsVboUtcChronology()
{
    const QString header = "File created on 29/08/2026 at 14:06:49\n";
    const QString body = "[comments]\nGenerated by RaceChrono Pro v10.2.4\n[column names]\ntime speed\n[data]\n140659.610 10\n140659.710 11\n";
    const auto valid = VboParser::parse(header + body);
    const auto expected = QDateTime::fromString("2026-08-29T14:06:59.610Z", Qt::ISODateWithMs).toMSecsSinceEpoch();
    QCOMPARE(recordingTimestamp(valid).value(), expected);
    QCOMPARE(valid.valueAt("speed", 0).value(), 10.0);
    QVERIFY(!recordingTimestamp(VboParser::parse(body)));
    QVERIFY(!recordingTimestamp(VboParser::parse(QString(header).replace("29/08", "31/02") + body)));
    QVERIFY(!recordingTimestamp(VboParser::parse(QString(header).replace("14:06:49", "99:06:49") + body)));
    QVERIFY(!recordingTimestamp(VboParser::parse(header + header + body)));
    QVERIFY(!recordingTimestamp(VboParser::parse(header + QString(body).replace("Generated by RaceChrono Pro v10.2.4", "Other exporter"))));
    QVERIFY(!recordingTimestamp(VboParser::parse(header + QString(body).replace("140659.610", "0").replace("140659.710", "1"))));
    const auto midnight = VboParser::parse(QString(header).replace("29/08/2026 at 14:06:49", "31/12/2026 at 23:59:59")
        + QString(body).replace("140659.610", "000000.100").replace("140659.710", "000000.200"));
    QCOMPARE(recordingTimestamp(midnight).value(), QDateTime::fromString("2027-01-01T00:00:00.100Z", Qt::ISODateWithMs).toMSecsSinceEpoch());
}

void SourceTests::rejectsBatchLinksWithDifferentPersistedFormats()
{
#ifdef Q_OS_UNIX
    QTemporaryDir directory; QVERIFY(directory.isValid());
    QSettings settings; settings.clear(); settings.sync();
    const auto backing = directory.filePath("recording.bin");
    const auto link = directory.filePath("recording.vbo");
    QVERIFY(QFile::copy(QStringLiteral(TEST_FIXTURE_PATH), backing));
    QVERIFY(QFile::link(backing, link));
    AppController controller(nullptr, directory.filePath("recovery.json"));
    // A parser can dispatch via the selected link extension, but a saved project
    // resolves the backing path. Do not offer a source that cannot reopen.
    QVERIFY(controller.m_document.beginBatchImport({QUrl::fromLocalFile(link)}));
    QTRY_COMPARE(controller.m_document.batchImportState(), QStringLiteral("review"));
    QCOMPARE(controller.m_document.batchImportRows()[0].toMap().value("status").toString(), QStringLiteral("error"));
    QVERIFY(independentBatchChoices(controller.m_document.batchImportRows()).isEmpty());
    QVERIFY(!controller.m_document.confirmBatchImport("Day", false, {}));
#else
    QSKIP("Native source-link dispatch is covered on macOS/Unix.");
#endif
}

void SourceTests::reviewsGoProChapterGroups()
{
    // KAN-104: GoPro chapter files are grouped and ordered by name, checked
    // against their probed metadata (the creation times here follow each
    // chapter's end, except where noted), reviewable and reorderable; an
    // ordinary video still loads directly.
    const QString ffmpeg = FfmpegTools::ffmpegPath();
    if (ffmpeg.isEmpty()) QSKIP("FFmpeg is unavailable for the chapter review test.");
    QTemporaryDir directory; QVERIFY(directory.isValid());
    QSettings settings; settings.clear(); settings.sync();
    const auto encode = [&](const QString &name, const int seconds, const QString &created) {
        QProcess encoder;
        encoder.start(ffmpeg, {"-hide_banner", "-loglevel", "error", "-y", "-f", "lavfi", "-i",
            QString("testsrc2=s=320x180:r=30:d=%1").arg(seconds), "-c:v", "libx264", "-pix_fmt", "yuv420p",
            "-metadata", "creation_time=" + created, directory.filePath(name)});
        return encoder.waitForFinished(30'000) && encoder.exitCode() == 0;
    };
    QVERIFY(encode("GX010123.MP4", 3, "2026-08-29T10:00:00Z"));
    QVERIFY(encode("GX020123.MP4", 2, "2026-08-29T10:00:03Z"));
    QVERIFY(encode("GX030123.MP4", 2, "2026-08-29T10:01:00Z")); // a minute later: something is missing between
    QVERIFY(encode("onboard.mp4", 2, "2026-08-29T11:00:00Z"));
    AppController controller(nullptr, directory.filePath("recovery.json"));
    const auto url = [&](const QString &name) { return QUrl::fromLocalFile(directory.filePath(name)); };
    QVERIFY(!controller.videoFilesNeedReview({url("onboard.mp4")}));
    QVERIFY(controller.videoFilesNeedReview({url("GX010123.MP4")}));
    QVERIFY(controller.videoFilesNeedReview({url("onboard.mp4"), url("GX010123.MP4")}));

    auto *review = controller.videoChapters();
    QVERIFY(review->review({url("GX030123.MP4"), url("onboard.mp4"), url("GX020123.MP4"), url("GX010123.MP4")}));
    QCOMPARE(review->state(), QString("probing"));
    QTRY_COMPARE_WITH_TIMEOUT(review->state(), QString("ready"), 30000);
    const auto groups = review->groups();
    QCOMPARE(groups.size(), 2);
    const auto gopro = groups[0].toMap();
    QCOMPARE(gopro.value("key").toString(), QString("GX0123"));
    QStringList names;
    for (const auto &value : gopro.value("chapters").toList()) names.append(value.toMap().value("name").toString());
    QCOMPARE(names, (QStringList{"GX010123.MP4", "GX020123.MP4", "GX030123.MP4"}));
    QCOMPARE(gopro.value("issues").toStringList(), QStringList{"timingGap"});
    QVERIFY(gopro.value("needsReview").toBool());
    QVERIFY(std::abs(gopro.value("totalDuration").toDouble() - 7.0) < 0.2);
    QCOMPARE(groups[1].toMap().value("key").toString(), QString("onboard.mp4"));

    // The dialog: issues shown, a chapter moved, the group used.
    QQmlEngine engine; engine.rootContext()->setContextProperty("appController", &controller);
    QSignalSpy warnings(&engine, &QQmlEngine::warnings);
    QQmlComponent component(&engine);
    component.setData("import QtQuick\nWindow { width: 900; height: 640; visible: true; VideoChaptersDialog { id: d } Component.onCompleted: d.open() }",
        QUrl::fromLocalFile(qmlSourcePath("Main.qml")));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    std::unique_ptr<QObject> object(component.create()); QVERIFY2(object, qPrintable(component.errorString()));
    auto *window = qobject_cast<QQuickWindow *>(object.get()); QVERIFY(window);
    QVERIFY(QTest::qWaitForWindowExposed(window));
    const auto findVisible = [&](const QString &name) {
        QQuickItem *found = nullptr;
        std::function<void(QQuickItem *)> search = [&](QQuickItem *item) {
            if (found || !item->isVisible()) return;
            if (item->objectName() == name) { found = item; return; }
            for (auto *child : item->childItems()) search(child);
        };
        search(window->contentItem());
        return found;
    };
    QQuickItem *issues = nullptr;
    QTRY_VERIFY((issues = findVisible("videoChapterIssues-0")));
    QVERIFY(issues->property("text").toString().contains("well after the previous one ended"));
    const QString layoutReview = qEnvironmentVariable("FLAPPEDEAR_LAYOUT_REVIEW_DIR");
    if (!layoutReview.isEmpty()) { QTest::qWait(300); static_cast<void>(window->grabWindow().save(QDir(layoutReview).filePath("video-chapters.png"))); }
    auto *down = findVisible("videoChapterDown-0-1"); QVERIFY(down && down->isEnabled());
    down->forceActiveFocus(); QTest::keyClick(window, Qt::Key_Space);
    QTRY_VERIFY(review->groups().value(0).toMap().value("manualOrder").toBool());
    QTRY_VERIFY(findVisible("videoChapterIssues-0")->property("text").toString().contains("Order changed by you"));
    auto *use = findVisible("useVideoChapterGroup-0"); QVERIFY(use && use->isEnabled());
    use->forceActiveFocus(); QTest::keyClick(window, Qt::Key_Space);
    QTRY_COMPARE(review->state(), QString("idle"));
    QTRY_VERIFY_WITH_TIMEOUT(controller.videoLoadState() == "ready", 30000);
    QCOMPARE(QFileInfo(controller.videoSource().toLocalFile()).fileName(), QString("GX010123.MP4"));
    // KAN-105: the group plays as one timeline, in the order chosen (1, 3, 2).
    QTRY_VERIFY(controller.videoChaptered());
    QStringList order;
    for (const auto &value : controller.videoChapterList()) order.append(value.toMap().value("name").toString());
    QCOMPARE(order, (QStringList{"GX010123.MP4", "GX030123.MP4", "GX020123.MP4"}));
    QCOMPARE(warnings.size(), 0);

    // Stale probing never lands: a second review supersedes the first.
    QVERIFY(review->review({url("GX010123.MP4")}));
    QVERIFY(review->review({url("onboard.mp4"), url("GX020123.MP4")}));
    QTRY_COMPARE_WITH_TIMEOUT(review->state(), QString("ready"), 30000);
    QCOMPARE(review->groups().size(), 2);
    review->cancel();
    QCOMPARE(review->state(), QString("idle"));
}

void SourceTests::derivesChapterPlaybackAndExportSource()
{
    // KAN-215: what a probed video's chapters mean, without a controller.
    using SourceLoading::VideoChapterState;
    const auto chapter = [](const QString &path, const double seconds, const bool available) {
        VideoChapterState state;
        state.path = path;
        state.durationSeconds = seconds;
        state.available = available;
        return state;
    };

    // One video, or none: no chapters and no timeline to play.
    auto derived = SourceLoading::deriveChapters({});
    QVERIFY(derived.chapters.isEmpty());
    QVERIFY(!derived.timeline.isValid());
    QCOMPARE(derived.gaps, 0);
    QVERIFY(derived.exportPaths.isEmpty());
    QVERIFY(derived.exportProblem.isEmpty());
    QVERIFY(!derived.gapsBlockExport);

    // A missing chapter plays as a gap of its saved duration and blocks export.
    derived = SourceLoading::deriveChapters({chapter("a.mp4", 10.0, true), chapter({}, 5.0, false)});
    QCOMPARE(derived.chapters.size(), 2);
    QCOMPARE(derived.timeline.chapterCount(), 2);
    QCOMPARE(derived.timeline.durationSeconds(), 15.0);
    QCOMPARE(derived.gaps, 1);
    QVERIFY(derived.gapsBlockExport);
    QVERIFY(derived.exportPaths.isEmpty());
    QVERIFY(!derived.exportInfo);

    // A chapter without a known duration cannot hold its place: only the first opens.
    derived = SourceLoading::deriveChapters({chapter("a.mp4", 10.0, true), chapter("b.mp4", 0.0, true)});
    QVERIFY(derived.unusableDurations);
    QVERIFY(derived.chapters.isEmpty());
    QVERIFY(!derived.timeline.isValid());
    QCOMPARE(derived.gaps, 0);

    // Chapters whose probes cannot be joined name the reason instead of an export source.
    derived = SourceLoading::deriveChapters({chapter("a.mp4", 10.0, true), chapter("b.mp4", 5.0, true)});
    QCOMPARE(derived.timeline.chapterCount(), 2);
    QVERIFY(!derived.gapsBlockExport);
    QVERIFY(!derived.exportInfo);
    QVERIFY(derived.exportPaths.isEmpty());
    QVERIFY(!derived.exportProblem.isEmpty());
}

void SourceTests::readsTheSavedChaptersOfAProject()
{
    // KAN-215: what a saved project says about its video's chapters, before any probe.
    auto none = SourceLoading::savedChapters({}, "/p/day.fetproject");
    QVERIFY(none.pending.isEmpty());
    QVERIFY(none.further.isEmpty());
    QVERIFY(!none.timeline.isValid());

    const auto chapter = [](const QString &path, const double seconds) {
        return QJsonObject{{"absolutePath", path}, {"durationSeconds", seconds}};
    };
    const QJsonObject video{{"absolutePath", "/v/GX01.MP4"},
        {"chapters", QJsonArray{chapter("/v/GX01.MP4", 10.0), chapter("/v/GX02.MP4", 5.0)}}};
    QVERIFY2(VideoChaptersCodec::valid(video), "fixture must be a valid chaptered video");
    const auto saved = SourceLoading::savedChapters(video, "/p/day.fetproject");
    QCOMPARE(saved.pending.size(), 2);
    QVERIFY(!saved.pending.at(0).available);
    QCOMPARE(saved.pending.at(1).problem, QString("loading"));
    QCOMPARE(saved.timeline.chapterCount(), 2);
    QCOMPARE(saved.timeline.durationSeconds(), 15.0);
    QCOMPARE(saved.further.size(), 1);
    QCOMPARE(saved.further.at(0).durationSeconds, 5.0);
}

void SourceTests::plansTheExportJobFromTheLoadedSources()
{
    // KAN-215: whether an export can start and what it must never overwrite,
    // without a controller.
    ExportJobPlan::Sources sources{true, "/v/GX01.MP4", "/t/run.vbo", "/o/out.mov", false, {}, {}};
    QVERIFY(ExportJobPlan::startProblem(sources).isEmpty());
    auto missing = sources; missing.telemetryLoaded = false;
    QVERIFY(!ExportJobPlan::startProblem(missing).isEmpty());
    missing = sources; missing.videoPath.clear();
    QVERIFY(!ExportJobPlan::startProblem(missing).isEmpty());
    missing = sources; missing.outputPath.clear();
    QVERIFY(!ExportJobPlan::startProblem(missing).isEmpty());

    // Chapters that cannot be exported together never export the first alone (KAN-106).
    auto chaptered = sources; chaptered.chaptered = true;
    QVERIFY(ExportJobPlan::startProblem(chaptered).contains("chapters"));
    chaptered.chapterProblem = "Chapter 2 differs.";
    QCOMPARE(ExportJobPlan::startProblem(chaptered), QString("Chapter 2 differs."));
    chaptered.chapterPaths = {"/v/GX01.MP4", "/v/GX02.MP4"};
    QVERIFY(ExportJobPlan::startProblem(chaptered).isEmpty());

    // Every source is protected from the output: telemetry, each chapter, the event
    // document and its sources, each additional video.
    ExportJobPlan::JobInputs inputs;
    inputs.sources = chaptered;
    inputs.chapters.resize(2);
    inputs.chapters[0].mediaInfo.videoDurationTicks = 100;
    inputs.chapters[1].mediaInfo.videoDurationTicks = 250;
    inputs.isEvent = true;
    inputs.documentPath = "/d/day.fetproject";
    inputs.referencedPaths = {"/t/other.vbo"};
    inputs.additionalVideos = {{"/v/helmet.MP4", "Helmet", {}}};
    inputs.lapBinding = QJsonObject{{"lap", 3}};
    const auto job = ExportJobPlan::buildJob(inputs);
    QCOMPARE(job.inputPath, QString("/v/GX01.MP4"));
    QCOMPARE(job.chapterPaths, chaptered.chapterPaths);
    QCOMPARE(job.chapterDurationTicks, (QJsonArray{100, 250}));
    QCOMPARE(job.telemetryPath, QString("/t/run.vbo"));
    QCOMPARE(job.protectedPaths, (QStringList{"/t/run.vbo", "/v/GX01.MP4", "/v/GX02.MP4", "/d/day.fetproject",
        "/t/other.vbo", "/v/helmet.MP4"}));
    QCOMPARE(job.additionalVideos.size(), 1);
    QCOMPARE(job.additionalVideos[0].label, QString("Helmet"));
    QCOMPARE(job.lapBinding, (QJsonObject{{"lap", 3}}));

    // One video outside an event: only the telemetry and the additional videos are protected,
    // and no chapter durations are listed.
    ExportJobPlan::JobInputs plain;
    plain.sources = sources;
    const auto single = ExportJobPlan::buildJob(plain);
    QVERIFY(single.chapterDurationTicks.isEmpty());
    QCOMPARE(single.protectedPaths, (QStringList{"/t/run.vbo"}));
}

void SourceTests::loadsSourcesInTheBackgroundWorker()
{
    // KAN-215: the work behind opening a recording or a video, without a controller.
    QTemporaryDir directory; QVERIFY(directory.isValid());
    const QString vbo = directory.filePath("run.vbo");
    QVERIFY(writeBytes(vbo, EventProjectFixture::lapsVbo()));
    const auto flag = [](const bool value) { return std::make_shared<std::atomic_bool>(value); };

    const auto loaded = SourceLoading::loadTelemetry(vbo, 7, flag(false), {}, {}, false);
    QVERIFY2(loaded.success, qPrintable(loaded.error));
    QCOMPARE(loaded.generation, quint64(7));
    QCOMPARE(loaded.path, vbo);
    QCOMPARE(loaded.contentRevision.size(), 64);
    QVERIFY(!loaded.contentMismatch);
    QVERIFY(loaded.session.sampleCount > 0);
    QVERIFY(!loaded.geometry.points.isEmpty());
    QVERIFY(!loaded.fingerprint.isEmpty());

    // The saved revision decides whether the recording is the same one.
    QVERIFY(!SourceLoading::loadTelemetry(vbo, 1, flag(false), loaded.contentRevision, {}, false).contentMismatch);
    const auto changed = SourceLoading::loadTelemetry(vbo, 1, flag(false), QByteArray(64, 'a'), {}, true);
    QVERIFY(changed.success);
    QVERIFY(changed.contentMismatch);
    QVERIFY(changed.relink);

    const auto cancelled = SourceLoading::loadTelemetry(vbo, 1, flag(true), {}, {}, false);
    QVERIFY(!cancelled.success);
    QVERIFY(cancelled.cancelled);
    const auto missing = SourceLoading::loadTelemetry(directory.filePath("none.vbo"), 1, flag(false), {}, {}, false);
    QVERIFY(!missing.success);
    QVERIFY(!missing.cancelled);
    QVERIFY(!missing.error.isEmpty());

    const QString ffmpeg = FfmpegTools::ffmpegPath();
    if (ffmpeg.isEmpty()) QSKIP("FFmpeg is unavailable for the video half of this test.");
    const auto encode = [&](const QString &name, const QString &source) {
        QProcess encoder;
        encoder.start(ffmpeg, {"-hide_banner", "-loglevel", "error", "-y", "-f", "lavfi", "-i", source,
            "-c:v", "libx264", "-pix_fmt", "yuv420p", directory.filePath(name)});
        return encoder.waitForFinished(30'000) && encoder.exitCode() == 0;
    };
    QVERIFY(encode("a.mp4", "testsrc2=s=160x90:r=30:d=1"));
    QVERIFY(encode("b.mp4", "testsrc2=s=160x90:r=30:d=2"));

    const auto single = SourceLoading::probeVideo(directory.filePath("a.mp4"), 3, flag(false), {}, false, {});
    QVERIFY2(single.success, qPrintable(single.error));
    QCOMPARE(single.generation, quint64(3));
    QVERIFY(!single.fingerprint.isEmpty());
    QVERIFY(single.chapters.isEmpty());

    // Further chapters: one found, one missing (a gap), one that is another file.
    const auto foundReference = ProjectSourceReferenceCodec::forLoadedSource(
        directory.filePath("b.mp4"), videoSourceFingerprint(directory.filePath("b.mp4"),
            MediaProbe::probeSummary(directory.filePath("b.mp4"), {}, 30'000, {}, {})));
    QVector<SourceLoading::VideoChapterInput> inputs;
    inputs.append({foundReference, directory.filePath("b.mp4"), 2.0});
    inputs.append({foundReference, QString(), 5.0});
    inputs.append({foundReference, directory.filePath("a.mp4"), 1.0});
    const auto chaptered = SourceLoading::probeVideo(directory.filePath("a.mp4"), 3, flag(false), {}, false, inputs);
    QVERIFY2(chaptered.success, qPrintable(chaptered.error));
    QCOMPARE(chaptered.chapters.size(), 4);
    QVERIFY(chaptered.chapters.at(0).available);
    QVERIFY(chaptered.chapters.at(1).available);
    QVERIFY(!chaptered.chapters.at(2).available);
    QCOMPARE(chaptered.chapters.at(2).problem, QStringLiteral("missing"));
    QCOMPARE(chaptered.chapters.at(2).durationSeconds, 5.0);
    QVERIFY(chaptered.chapters.at(2).path.isEmpty());
    QVERIFY(!chaptered.chapters.at(3).available);
    QCOMPARE(chaptered.chapters.at(3).problem, QStringLiteral("mismatch"));

    QVERIFY(SourceLoading::probeVideo(directory.filePath("a.mp4"), 3, flag(true), {}, false, {}).cancelled);
    const auto notVideo = SourceLoading::probeVideo(vbo, 3, flag(false), {}, false, {});
    QVERIFY(!notVideo.success);
    QVERIFY(!notVideo.error.isEmpty());
}

void SourceTests::keepsVideoChaptersAsOneTimeline()
{
    // KAN-105: chapters form one continuous timeline, are saved with their
    // durations, reopen as chapters, and a missing chapter keeps its time as
    // a gap. Export reads every chapter (KAN-106); a gap is refused, never
    // closed up.
    const QString ffmpeg = FfmpegTools::ffmpegPath();
    if (ffmpeg.isEmpty()) QSKIP("FFmpeg is unavailable for the chapter timeline test.");
    QTemporaryDir directory; QVERIFY(directory.isValid());
    QSettings settings; settings.clear(); settings.sync();
    QVERIFY(encodeChapter(ffmpeg, directory.filePath("GX010200.MP4"), 3));
    QVERIFY(encodeChapter(ffmpeg, directory.filePath("GX020200.MP4"), 2));
    QVERIFY(encodeChapter(ffmpeg, directory.filePath("GX030200.MP4"), 2));
    const auto file = [&](const QString &name) { return QUrl::fromLocalFile(directory.filePath(name)); };
    const auto projectPath = directory.filePath("chapters.fetproject");
    {
        AppController controller(nullptr, directory.filePath("recovery.json"));
        controller.loadVideoChapters({file("GX010200.MP4"), file("GX020200.MP4"), file("GX030200.MP4")});
        QTRY_COMPARE_WITH_TIMEOUT(controller.videoLoadState(), QString("ready"), 30000);
        QVERIFY(controller.videoChaptered());
        const auto chapters = controller.videoChapterList();
        QCOMPARE(chapters.size(), 3);
        QCOMPARE(chapters[1].toMap().value("startMilliseconds").toLongLong(), 3000);
        QCOMPARE(chapters[2].toMap().value("startMilliseconds").toLongLong(), 5000);
        // The timeline's last frame is the last chapter's last frame: 7 s at 30 fps.
        QCOMPARE(controller.previewEndPositionMilliseconds(), 6966); // frame 209 at 30 fps
        QCOMPARE(controller.previewEndTimecode(), QString("00:00:06:29"));
        QCOMPARE(controller.clampPreviewPositionMilliseconds(99'000), 6966);
        const auto located = controller.locateVideoTimeline(4200);
        QCOMPARE(located.value("chapter").toInt(), 1);
        QCOMPARE(located.value("localMilliseconds").toLongLong(), 1200);
        QVERIFY(controller.setVideoChapter(2));
        QCOMPARE(QFileInfo(controller.videoChapterSource().toLocalFile()).fileName(), QString("GX030200.MP4"));
        QCOMPARE(controller.videoChapterStartMilliseconds(), 5000);
        QVERIFY(!controller.setVideoChapter(3));
        // Export covers all three chapters, which it protects.
        controller.loadVbo(QUrl::fromLocalFile(QFINDTESTDATA("fixtures/basic.vbo")));
        QTRY_COMPARE_WITH_TIMEOUT(controller.vboLoadState(), QString("ready"), 30000);
        QCOMPARE(controller.m_exportChapterPaths.size(), 3);
        QVERIFY(!controller.startExport(file("GX020200.MP4"), 320, 180, 30, 1, 1'000'000, false, false, {}, {}, true));
        QVERIFY2(controller.startExport(QUrl::fromLocalFile(directory.filePath("out.mp4")), 320, 180, 30, 1, 1'000'000, false, false, {}, {}, false),
                 qPrintable(controller.exporter()->error()));
        QTRY_VERIFY_WITH_TIMEOUT(!controller.exporter()->exporting(), 120000); // the worker exited and the output was committed
        QVERIFY2(controller.exporter()->state() == "complete", qPrintable(controller.exporter()->error() + controller.exporter()->diagnosticLog().right(3000)));
        QCOMPARE(MediaProbe::probe(directory.filePath("out.mp4"), {}, true).videoFrameCount, qsizetype(210));
        QVERIFY(controller.saveProject(QUrl::fromLocalFile(projectPath)));
    }
    const auto saved = QJsonDocument::fromJson(readBytes(projectPath)).object();
    const auto video = saved.value("sources").toObject().value("video").toObject();
    const auto savedChapters = video.value("chapters").toArray();
    QCOMPARE(savedChapters.size(), 3);
    QCOMPARE(savedChapters[0].toObject().value("relativePath"), video.value("relativePath"));
    QVERIFY(std::abs(savedChapters[1].toObject().value("durationSeconds").toDouble() - 2.0) < 0.05);
    QVERIFY(ProjectLimits::validateProject(saved));

    // Reopened: chapters again. Then with the middle chapter gone: a gap of 2 s.
    for (const bool removeMiddle : {false, true}) {
        if (removeMiddle) QVERIFY(QFile::remove(directory.filePath("GX020200.MP4")));
        AppController reopened(nullptr, directory.filePath(removeMiddle ? "recovery-3.json" : "recovery-2.json"));
        reopened.requestOpenProject(QUrl::fromLocalFile(projectPath));
        QTRY_COMPARE_WITH_TIMEOUT(reopened.videoLoadState(), QString("ready"), 30000);
        QVERIFY(reopened.videoChaptered());
        const auto chapters = reopened.videoChapterList();
        QCOMPARE(chapters.size(), 3);
        QCOMPARE(chapters[1].toMap().value("available").toBool(), !removeMiddle);
        QCOMPARE(chapters[2].toMap().value("startMilliseconds").toLongLong(), 5000); // time kept across the gap
        QCOMPARE(reopened.previewEndPositionMilliseconds(), 6966);
        if (removeMiddle) {
            QVERIFY(reopened.locateVideoTimeline(4000).value("gap").toBool());
            QVERIFY(reopened.statusText().contains("gap"));
            QVERIFY(!reopened.startExport(QUrl::fromLocalFile(directory.filePath("gap.mp4")), 320, 180, 30, 1, 1'000'000, false, false, {}, {}, false));
            QVERIFY(reopened.exporter()->error().contains("missing or changed"));
            // Saving keeps the missing chapter's reference and duration.
            QVERIFY(reopened.saveProject(QUrl::fromLocalFile(projectPath)));
            const auto again = QJsonDocument::fromJson(readBytes(projectPath)).object()
                .value("sources").toObject().value("video").toObject().value("chapters").toArray();
            QCOMPARE(again.size(), 3);
            QVERIFY(std::abs(again[1].toObject().value("durationSeconds").toDouble() - 2.0) < 0.05);
        }
    }
    // An ordinary video replaces the chapters.
    AppController single(nullptr, directory.filePath("recovery-4.json"));
    single.loadVideo(file("GX010200.MP4"));
    QTRY_COMPARE_WITH_TIMEOUT(single.videoLoadState(), QString("ready"), 30000);
    QVERIFY(!single.videoChaptered());
    QVERIFY(single.videoChapterList().isEmpty());
    QCOMPARE(single.videoChapterSource(), single.videoSource());
}

void SourceTests::playsVideoChaptersAcrossBoundaries()
{
    // KAN-105: in the editor window, a seek past the first chapter opens the
    // second at the right local position, and playing through a chapter's end
    // continues into the next one with telemetry time still running on.
    const QString ffmpeg = FfmpegTools::ffmpegPath();
    if (ffmpeg.isEmpty()) QSKIP("FFmpeg is unavailable for the chapter playback test.");
    QTemporaryDir directory; QVERIFY(directory.isValid());
    QSettings settings; settings.clear(); settings.sync();
    QVERIFY(encodeChapter(ffmpeg, directory.filePath("GX010300.MP4"), 3));
    QVERIFY(encodeChapter(ffmpeg, directory.filePath("GX020300.MP4"), 3));
    AppController controller(nullptr, directory.filePath("recovery.json"));
    controller.loadVideoChapters({QUrl::fromLocalFile(directory.filePath("GX010300.MP4")),
        QUrl::fromLocalFile(directory.filePath("GX020300.MP4"))});
    QTRY_COMPARE_WITH_TIMEOUT(controller.videoLoadState(), QString("ready"), 30000);
    QQmlEngine engine; engine.rootContext()->setContextProperty("appController", &controller);
    QSignalSpy warnings(&engine, &QQmlEngine::warnings);
    QQmlComponent component(&engine, QUrl::fromLocalFile(
        qmlSourcePath("Main.qml")));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    std::unique_ptr<QObject> object(component.create());
    QVERIFY2(object, qPrintable(component.errorString()));
    auto *window = qobject_cast<QQuickWindow *>(object.get()); QVERIFY(window);
    window->show(); QVERIFY(QTest::qWaitForWindowExposed(window));
    // The first chapter primes to its first frame; wait for the player.
    if (!QTest::qWaitFor([&] { return controller.playbackTime() > 0.0; }, 15000))
        QSKIP("Media playback is unavailable here.");
    QVERIFY(QMetaObject::invokeMethod(window, "seekTimeline", Q_ARG(QVariant, 4500)));
    QTRY_COMPARE(controller.videoChapterIndex(), 1);
    QTRY_VERIFY_WITH_TIMEOUT(std::abs(controller.playbackTime() - 4.5) < 0.1, 15000);
    QVERIFY(window->property("timelinePosition").toDouble() > 4400);
    // Back into the first chapter, then play through its end.
    QVERIFY(QMetaObject::invokeMethod(window, "seekTimeline", Q_ARG(QVariant, 2300)));
    QTRY_COMPARE(controller.videoChapterIndex(), 0);
    QTRY_VERIFY_WITH_TIMEOUT(std::abs(controller.playbackTime() - 2.3) < 0.15, 15000);
    // Silent priming of the reopened chapter ends first; then play.
    QTRY_VERIFY_WITH_TIMEOUT(!window->property("previewPrimeFramePending").toBool(), 15000);
    QVERIFY(QMetaObject::invokeMethod(window, "togglePlayback"));
    QTRY_COMPARE_WITH_TIMEOUT(controller.videoChapterIndex(), 1, 10000);
    QTRY_VERIFY_WITH_TIMEOUT(controller.playbackTime() > 3.3, 10000); // time runs on past the boundary
    QVERIFY(QMetaObject::invokeMethod(window, "togglePlayback"));
    for (const auto &arguments : warnings)
        for (const auto &error : arguments.first().value<QList<QQmlError>>())
            // The branding image is not bundled into the test binary.
            QVERIFY2(!error.toString().contains("Main.qml") || error.toString().contains("Cannot open: qrc:"),
                qPrintable(error.toString()));
}

void SourceTests::persistsAndInvalidatesRunTrackConfiguration()
{
    // A track configuration confirmed in FlappedEar Telemetry is kept by
    // Overlays' save, and replacing the recording clears its assertions.
    QTemporaryDir directory; QVERIFY(directory.isValid());
    QSettings settings; settings.clear(); settings.sync();
    const auto path = directory.filePath("run.vbo");
    QVERIFY(writeBytes(path, EventProjectFixture::lapsVbo()));
    const auto analysedPath = directory.filePath("analysed.fetproject");
    QJsonObject imported;
    {
        TelemetryController reference(directory.filePath("reference.json"));
        QVERIFY(importReferenceDay(reference, "Identity", {QUrl::fromLocalFile(path)}));
        const auto runId = reference.document()->activeRunId();
        imported = EventProjectCodec::trackConfiguration(EventProjectFixture::runs(reference.document()->analysisProject())[0].toObject());
        QVERIFY(imported.value("layoutId").isNull());
        QCOMPARE(imported.value("direction").toString(), QString("unknown"));
        QVERIFY(reference.analysis()->setRunTrackConfiguration(runId, "jastrzab-full", "clockwise"));
        QVERIFY(reference.document()->saveProject(QUrl::fromLocalFile(analysedPath)));
    }
    // A route inference as FlappedEar Telemetry stores it.
    auto analysed = QJsonDocument::fromJson(readBytes(analysedPath)).object();
    auto analysedRuns = EventProjectFixture::runs(analysed); auto analysedRun = analysedRuns[0].toObject();
    const QJsonObject inference{{"algorithm", "gps-route-v1"}, {"sourceRevision", QString(64, 'a')},
        {"gateRevision", imported.value("gateRevision")}, {"layoutId", "gps-route-v1:" + QString(64, 'c')},
        {"direction", "clockwise"}};
    analysedRun.insert("trackInference", inference); analysedRuns[0] = analysedRun;
    EventProjectFixture::setRuns(analysed, analysedRuns);
    QVERIFY(writeBytes(analysedPath, QJsonDocument(analysed).toJson()));
    AppController controller(nullptr, directory.filePath("recovery.json"));
    controller.requestOpenProject(QUrl::fromLocalFile(analysedPath));
    QTRY_COMPARE(controller.vboLoadState(), QString("ready"));
    QCOMPARE(imported.value("gateRevision").toString(), timingGateRevision(*controller.m_session));
    QVERIFY(controller.widgetModel()->addWidget("lapCurrent") >= 0);
    const auto savedPath = directory.filePath("identity.fetproject");
    QVERIFY(controller.saveProject(QUrl::fromLocalFile(savedPath)));
    auto run = EventProjectFixture::runs(QJsonDocument::fromJson(readBytes(savedPath)).object())[0].toObject();
    const auto config = EventProjectCodec::trackConfiguration(run);
    QCOMPARE(config.value("layoutId").toString(), QString("jastrzab-full"));
    QCOMPARE(config.value("direction").toString(), QString("clockwise"));
    QCOMPARE(config.value("gateRevision"), imported.value("gateRevision"));
    QCOMPARE(run.value("trackInference").toObject(), inference); // kept as the analysis saved it
    // Replacing the source clears the source-bound assertions in the real editor path.
    controller.loadVbo(QUrl::fromLocalFile(QStringLiteral(TEST_FIXTURE_PATH)));
    QTRY_COMPARE(controller.vboLoadState(), QString("ready"));
    run = EventProjectFixture::runs(controller.currentProjectObject())[0].toObject();
    const auto unknown = EventProjectCodec::trackConfiguration(run);
    QVERIFY(unknown.value("layoutId").isNull()); QVERIFY(unknown.value("gateRevision").isNull());
    QCOMPARE(unknown.value("direction").toString(), QString("unknown"));
    QVERIFY(!run.contains("trackInference"));
    QString error; QVERIFY2(ProjectLimits::validateProject(controller.currentProjectObject(), &error), qPrintable(error));
}

void SourceTests::preservesTimingEditsDuringAutoSync_data()
{
    QTest::addColumn<int>("edit");
    QTest::newRow("offset") << 1;
    QTest::newRow("scale") << 2;
    QTest::newRow("edit-and-restore") << 3;
    QTest::newRow("unedited-result-applies") << 0;
}

void SourceTests::preservesTimingEditsDuringAutoSync()
{
    QFETCH(int, edit);
    QSettings settings;
    settings.clear();
    settings.sync();
    QTemporaryDir directory;
    AppController controller(nullptr, directory.filePath("recovery.json"));
    controller.m_syncController.m_syncCancellation = std::make_shared<std::atomic_bool>(false);
    SyncController::AutoSyncResult result;
    result.generation = controller.m_document.m_sourceGeneration;
    result.syncRevision = controller.m_syncController.m_syncRevision;
    result.success = true;
    result.candidate.offset = 12.5;
    result.candidate.timeScale = 1.002;
    result.candidate.confidence = 1.0;
    QPromise<SyncController::AutoSyncResult> promise;
    promise.start();
    controller.m_syncController.m_syncWatcher.setFuture(promise.future());
    QSignalSpy finished(controller.syncController(), &SyncController::runningChanged);
    if (edit == 1) controller.syncController()->setOffset(7.0);
    if (edit == 2) controller.syncController()->setTimeScale(1.01);
    if (edit == 3) { controller.syncController()->setOffset(7.0); controller.syncController()->setOffset(0.0); }
    const bool cancelled = controller.m_syncController.m_syncCancellation->load();
    promise.addResult(result); // A completed worker can still deliver an already-queued result.
    promise.finish();
    QTRY_VERIFY(!finished.isEmpty());
    QCOMPARE(cancelled, edit != 0);
    QCOMPARE(controller.syncController()->offset(), edit == 0 ? 12.5 : edit == 1 ? 7.0 : 0.0);
    QCOMPARE(controller.syncController()->timeScale(), edit == 0 ? 1.002 : edit == 2 ? 1.01 : 1.0);
    if (edit != 0) QVERIFY(controller.syncController()->candidate().isEmpty());
    else {
        QCOMPARE(controller.syncController()->candidate().value("timeScale").toDouble(), 1.002);
        controller.syncController()->applyCandidate();
        QCOMPARE(controller.syncController()->timeScale(), 1.002);
        controller.syncController()->setOffset(8.0);
        QVERIFY(controller.syncController()->candidate().isEmpty());
        controller.syncController()->applyCandidate();
        QCOMPARE(controller.syncController()->offset(), 8.0);
    }
}

void SourceTests::gatesWeakSyncCandidates()
{
    SyncCandidate strong;
    strong.confidence = 0.75;
    QVERIFY(shouldAutoApplySyncCandidate(strong));
    QVERIFY(syncConfidenceLevel(strong.confidence) == SyncConfidenceLevel::High);

    SyncCandidate weak;
    weak.confidence = 0.43;
    QVERIFY(!shouldAutoApplySyncCandidate(weak));
    QVERIFY(syncConfidenceLevel(weak.confidence) == SyncConfidenceLevel::Low);

    TelemetrySession rectangle;
    TelemetryChannel latitude;
    latitude.name = "latitude";
    latitude.setSamples({0.0, 1.0, 2.0, 3.0}, {0.0F, 0.0F, 0.001F, 0.001F});
    TelemetryChannel longitude;
    longitude.name = "longitude";
    longitude.setSamples(latitude.timestamps(), {0.0F, 0.004F, 0.004F, 0.0F});
    rectangle.channels.insert(latitude.name, latitude);
    rectangle.channels.insert(longitude.name, longitude);
    rectangle.aliases.insert("latitude", latitude.name);
    rectangle.aliases.insert("longitude", longitude.name);
    const TrackGeometry geometry = buildTrackGeometry(rectangle);
    QVERIFY(geometry.valid);
    const double width = geometry.points[1].x() - geometry.points[0].x();
    const double height = geometry.points[0].y() - geometry.points[2].y();
    QVERIFY2(qAbs(width / height - 4.0) < 0.05,
             qPrintable(QStringLiteral("aspect=%1").arg(width / height, 0, 'f', 3)));
    const auto current = currentTrackPoint(rectangle, 1.0, geometry);
    QVERIFY(current.has_value());
    QVERIFY(qAbs(current->x() - geometry.points[1].x()) < 0.001);
    QVERIFY(qAbs(current->y() - geometry.points[1].y()) < 0.001);
}

void SourceTests::probesMediaInfoJson()
{
    const QByteArray json = R"({"format":{"duration":"3.000000","start_time":"0.500000"},"streams":[{"codec_type":"video","codec_name":"h264","width":320,"height":180,"r_frame_rate":"30000/1001","avg_frame_rate":"30000/1001","time_base":"1/90000","pix_fmt":"yuv420p","start_time":"0.500000","duration":"3.003000","nb_read_frames":"90","nb_read_packets":"90"},{"codec_type":"audio","codec_name":"aac","start_time":"0.500000","duration":"3.021333","time_base":"1/48000","sample_rate":"48000"}]})";
    const MediaInfo info = MediaProbe::parseJson(json, "/fixture.mp4");
    QCOMPARE(info.path, QString("/fixture.mp4"));
    QCOMPARE(info.videoSize, QSize(320, 180));
    QCOMPARE(info.videoCodec, QString("h264"));
    QCOMPARE(info.videoFrameCount, qsizetype(90));
    QCOMPARE(info.videoPacketCount, qsizetype(90));
    QCOMPARE(info.audioCodecs, QStringList({"aac"}));
    QCOMPARE(info.videoStartTime, 0.5);
    QCOMPARE(info.videoDuration, 3.003);
    QCOMPARE(info.audioStartTime, 0.5);
    QCOMPARE(info.audioDuration, 3.021333);
    QCOMPARE(info.audioSampleRate, 48000);
    QVERIFY(info.audioTimeBase.isEquivalentTo({1, 48000}));
    QVERIFY(qAbs(info.frameRate.value() - 29.97002997) < 0.00001);
    QVERIFY(!info.likelyVariableFrameRate);
}

void SourceTests::parsesMediaSummaryJson()
{
    const QByteArray json = R"({"format":{"duration":"120.003000"},"streams":[{"index":0,"codec_type":"video","codec_name":"hevc","width":3840,"height":2160,"r_frame_rate":"60000/1001","avg_frame_rate":"60000/1001"},{"index":1,"codec_type":"audio","codec_name":"aac"}]})";
    const MediaInfo info = MediaProbe::parseJson(json, "/summary.mp4");
    QCOMPARE(info.videoCodec, QStringLiteral("hevc"));
    QCOMPARE(info.videoSize, QSize(3840, 2160));
    QVERIFY(qAbs(info.duration - 120.003) < 0.0001);
    QVERIFY(qAbs(info.averageFrameRate.value() - 59.94005994) < 0.00001);
    QCOMPARE(info.audioCodecs, QStringList({QStringLiteral("aac")}));

    const QByteArray silentJson = R"({"format":{"duration":"12.000000"},"streams":[{"index":0,"codec_type":"video","codec_name":"hevc","width":1920,"height":1080,"r_frame_rate":"30/1","avg_frame_rate":"30/1"}]})";
    const MediaInfo silentInfo = MediaProbe::parseJson(silentJson, "/silent.mp4");
    QVERIFY(silentInfo.audioCodecs.isEmpty());
}

void SourceTests::modelsExtendedMediaCharacteristics()
{
    const QByteArray tenBitJson = R"({"format":{"duration":"1.0","bit_rate":"91000000"},"streams":[{"codec_type":"video","codec_name":"hevc","profile":"Main 10","width":5312,"height":2988,"coded_width":5312,"coded_height":3008,"r_frame_rate":"60000/1001","avg_frame_rate":"60000/1001","pix_fmt":"yuv420p10le","bits_per_raw_sample":"10","bit_rate":"90000000","sample_aspect_ratio":"1:1","color_range":"tv","color_space":"bt709","color_transfer":"bt709","color_primaries":"bt709"}]})";
    const MediaInfo tenBit = MediaProbe::parseJson(tenBitJson, QStringLiteral("/5k.mp4"));
    QCOMPARE(tenBit.videoSize, QSize(5312, 2988));
    QCOMPARE(tenBit.codedVideoSize, QSize(5312, 3008));
    QCOMPARE(tenBit.displayVideoSize, QSize(5312, 2988));
    QCOMPARE(tenBit.videoCodecProfile, QStringLiteral("Main 10"));
    QCOMPARE(tenBit.pixelFormat, QStringLiteral("yuv420p10le"));
    QCOMPARE(tenBit.bitDepth, std::optional<int>(10));
    QCOMPARE(tenBit.sourceVideoBitrate, std::optional<qint64>(90'000'000));
    QVERIFY(tenBit.sampleAspectRatio.isEquivalentTo({1, 1}));
    QCOMPARE(tenBit.sourceColorClass, SourceColorClass::Sdr);
    QCOMPARE(tenBit.colorRange, QStringLiteral("tv"));
    QCOMPARE(tenBit.colorSpace, QStringLiteral("bt709"));
    QCOMPARE(tenBit.colorTransfer, QStringLiteral("bt709"));
    QCOMPARE(tenBit.colorPrimaries, QStringLiteral("bt709"));

    const QByteArray rotatedHlgJson = R"({"format":{"duration":"1.0"},"streams":[{"codec_type":"video","codec_name":"hevc","width":5312,"height":4648,"r_frame_rate":"30/1","avg_frame_rate":"30/1","pix_fmt":"p010le","color_space":"bt2020nc","color_transfer":"arib-std-b67","color_primaries":"bt2020","side_data_list":[{"side_data_type":"Display Matrix","rotation":90},{"side_data_type":"Mastering display metadata","red_x":"1/2"},{"side_data_type":"Content light level metadata","max_content":1000}]}]})";
    const MediaInfo hlg = MediaProbe::parseJson(rotatedHlgJson, QStringLiteral("/8-7.mp4"));
    QCOMPARE(hlg.videoSize, QSize(5312, 4648));
    QCOMPARE(hlg.displayVideoSize, QSize(4648, 5312));
    QCOMPARE(hlg.rotationDegrees, std::optional<int>(90));
    QCOMPARE(hlg.bitDepth, std::optional<int>(10));
    QCOMPARE(hlg.sourceColorClass, SourceColorClass::HdrHlg);
    QVERIFY(!hlg.masteringDisplayMetadata.isEmpty());
    QVERIFY(!hlg.contentLightMetadata.isEmpty());

    QCOMPARE(MediaProbe::classifyColor(QStringLiteral("smpte2084"), {}, {}),
             SourceColorClass::HdrPq);
    QCOMPARE(MediaProbe::classifyColor(QStringLiteral("log316"), {}, {}),
             SourceColorClass::LogOrExtended);
    QCOMPARE(MediaProbe::classifyColor({}, QStringLiteral("bt2020nc"), QStringLiteral("bt2020")),
             SourceColorClass::PossibleHdr);
    QCOMPARE(MediaProbe::classifyColor(QStringLiteral("unknown"), {}, QStringLiteral("bt2020")),
             SourceColorClass::PossibleHdr);
    QCOMPARE(MediaProbe::classifyColor(QStringLiteral("bt2020-10"), QStringLiteral("bt2020nc"),
                                       QStringLiteral("bt2020")),
             SourceColorClass::Sdr);
    QCOMPARE(MediaProbe::classifyColor(QStringLiteral("bt709"), QStringLiteral("bt709"),
                                       QStringLiteral("bt709"), true),
             SourceColorClass::PossibleHdr);
    QVERIFY(isUnsupportedColorManagedClass(SourceColorClass::PossibleHdr));
    QVERIFY(!isUnsupportedColorManagedClass(SourceColorClass::Unknown));

    const QByteArray strippedHdrJson = R"({"format":{"duration":"1.0"},"streams":[{"codec_type":"video","codec_name":"hevc","width":3840,"height":2160,"r_frame_rate":"30/1","avg_frame_rate":"30/1","pix_fmt":"yuv420p10le","side_data_list":[{"side_data_type":"Content light level metadata","max_content":1000}]}]})";
    const MediaInfo strippedHdr = MediaProbe::parseJson(strippedHdrJson, QStringLiteral("/hdr.mp4"));
    QCOMPARE(strippedHdr.sourceColorClass, SourceColorClass::PossibleHdr);
    const ExportMediaProfile strippedProfile = ExportMediaProfile::derive(
        strippedHdr, {3840, 2160}, {30, 1}, 20'000'000, QStringLiteral("libx265"));
    QVERIFY(!strippedProfile.supported);
    QVERIFY2(strippedProfile.error.contains(QStringLiteral("Possible HDR")),
             qPrintable(strippedProfile.error));
    QCOMPARE(MediaProbe::bitDepthForPixelFormat(QStringLiteral("yuv420p")),
             std::optional<int>(8));
    QCOMPARE(MediaProbe::bitDepthForPixelFormat(QStringLiteral("p010le")),
             std::optional<int>(10));
    QVERIFY(!MediaProbe::bitDepthForPixelFormat(QStringLiteral("mystery444")).has_value());

    const QByteArray unknownJson = R"({"format":{"duration":"1.0"},"streams":[{"codec_type":"video","codec_name":"hevc","width":7680,"height":4320,"r_frame_rate":"30/1","avg_frame_rate":"30/1","pix_fmt":"mystery444"}]})";
    const MediaInfo unknown = MediaProbe::parseJson(unknownJson, QStringLiteral("/8k.mp4"));
    QCOMPARE(unknown.videoSize, QSize(7680, 4320));
    QVERIFY(!unknown.bitDepth.has_value());
    QCOMPARE(unknown.sourceColorClass, SourceColorClass::Unknown);
}

void SourceTests::rejectsInvalidMediaProbeJson()
{
    try {
        static_cast<void>(MediaProbe::parseJson("not-json", "/invalid.mp4"));
        QFAIL("Invalid ffprobe JSON should throw.");
    } catch (const std::runtime_error &error) {
        QCOMPARE(QString::fromUtf8(error.what()), QStringLiteral("ffprobe returned invalid JSON."));
    }
}

void SourceTests::classifiesMediaProbeProcessFailures()
{
    const auto errorFrom = [](const std::function<void()> &operation) {
        try {
            operation();
        } catch (const std::runtime_error &error) {
            return QString::fromUtf8(error.what());
        }
        return QString();
    };

    const QString startError = errorFrom([] {
        static_cast<void>(MediaProbe::probe(
            "/fixture.mp4", "/definitely/missing/flappedear-ffprobe", false, 100));
    });
    QVERIFY2(startError.startsWith("Could not start ffprobe while probing: /fixture.mp4"),
             qPrintable(startError));

#ifdef Q_OS_UNIX
    const QString exitError = errorFrom([] {
        static_cast<void>(MediaProbe::probe("/fixture.mp4", "/usr/bin/false", false, 1'000));
    });
    QVERIFY2(exitError.contains("ffprobe exited with code 1 while probing: /fixture.mp4"),
             qPrintable(exitError));
    QVERIFY2(exitError.contains("stderr: <no stderr output>"), qPrintable(exitError));

    const QString jsonError = errorFrom([] {
        static_cast<void>(MediaProbe::probe("/fixture.mp4", "/usr/bin/true", false, 1'000));
    });
    QCOMPARE(jsonError, QStringLiteral("ffprobe returned invalid JSON."));

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString slowProbePath = directory.filePath("slow-ffprobe");
    QFile slowProbe(slowProbePath);
    QVERIFY(slowProbe.open(QIODevice::WriteOnly));
    QVERIFY(slowProbe.write("#!/bin/sh\nwhile :; do :; done\n") > 0);
    slowProbe.close();
    QVERIFY(slowProbe.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                     | QFileDevice::ExeOwner));
    const QString timeoutError = errorFrom([&slowProbePath] {
        static_cast<void>(MediaProbe::probe("/fixture.mp4", slowProbePath, false, 10));
    });
    QVERIFY2(timeoutError.startsWith("ffprobe timed out after 0.010 seconds while probing: /fixture.mp4"),
             qPrintable(timeoutError));
    QVERIFY2(timeoutError.contains("stderr: <no stderr output>"), qPrintable(timeoutError));

    const QString crashingProbePath = directory.filePath("crashing-ffprobe");
    QFile crashingProbe(crashingProbePath);
    QVERIFY(crashingProbe.open(QIODevice::WriteOnly));
    QVERIFY(crashingProbe.write("#!/bin/sh\nkill -SEGV $$\n") > 0);
    crashingProbe.close();
    QVERIFY(crashingProbe.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                         | QFileDevice::ExeOwner));
    const QString crashError = errorFrom([&crashingProbePath] {
        static_cast<void>(MediaProbe::probe("/fixture.mp4", crashingProbePath, false, 1'000));
    });
    QVERIFY2(crashError.startsWith("ffprobe crashed while probing: /fixture.mp4"),
             qPrintable(crashError));
#endif
}

void SourceTests::reportsMediaProbeLifecycleHeartbeat()
{
#ifdef Q_OS_UNIX
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString probePath = directory.filePath("heartbeat-ffprobe");
    QFile probe(probePath);
    QVERIFY(probe.open(QIODevice::WriteOnly));
    QVERIFY(probe.write(
        "#!/bin/sh\n"
        "sleep 0.7\n"
        "printf '%s\\n' '{\"format\":{\"duration\":\"1.0\"},\"streams\":[{\"codec_type\":\"video\",\"codec_name\":\"hevc\",\"width\":16,\"height\":16,\"r_frame_rate\":\"30/1\",\"avg_frame_rate\":\"30/1\"}]}'\n") > 0);
    probe.close();
    QVERIFY(probe.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                 | QFileDevice::ExeOwner));
    QList<MediaProbeEvent::Phase> phases;
    const MediaInfo info = MediaProbe::probe(
        "/fixture.mp4", probePath, false, 2'000,
        [&phases](const MediaProbeEvent &event) { phases.append(event.phase); });
    QCOMPARE(info.videoCodec, QStringLiteral("hevc"));
    QCOMPARE(phases.first(), MediaProbeEvent::Phase::Started);
    QVERIFY(phases.contains(MediaProbeEvent::Phase::Heartbeat));
    QCOMPARE(phases.last(), MediaProbeEvent::Phase::Finished);
#else
    QSKIP("Lifecycle helper script requires a POSIX shell.");
#endif
}

void SourceTests::cancelsMediaProbeWithoutLeavingItRunning()
{
#ifdef Q_OS_UNIX
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString probePath = directory.filePath("cancel-probe.sh");
    QFile probe(probePath);
    QVERIFY(probe.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QCOMPARE(probe.write("#!/bin/sh\nsleep 10\n"), qint64(19));
    probe.close();
    QVERIFY(probe.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                 | QFileDevice::ExeOwner));
    QString error;
    try {
        static_cast<void>(MediaProbe::probe(
            "/fixture.mp4", probePath, false, 20'000, {}, [] { return true; }));
    } catch (const std::exception &exception) {
        error = QString::fromUtf8(exception.what());
    }
    QVERIFY2(error.startsWith("ffprobe cancelled while probing: /fixture.mp4"), qPrintable(error));
#else
    QSKIP("Lifecycle helper script requires a POSIX shell.");
#endif
}

void SourceTests::retainsTelemetryAfterFailedAsyncLoad()
{
    QSettings settings;
    settings.clear();
    settings.sync();
    AppController controller;
    controller.loadVbo(QUrl::fromLocalFile(QStringLiteral(TEST_FIXTURE_PATH)));
    QTRY_COMPARE(controller.vboLoadState(), QStringLiteral("ready"));
    const QString loadedName = controller.telemetryName();
    const QStringList loadedChannels = controller.channelNames();
    QVERIFY(!loadedName.isEmpty());

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString malformedPath = directory.filePath("malformed.vbo");
    QVERIFY(writeBytes(malformedPath, "not a VBOX file"));
    controller.loadVbo(QUrl::fromLocalFile(malformedPath));
    QTRY_COMPARE(controller.vboLoadState(), QStringLiteral("ready"));
    QCOMPARE(controller.telemetryName(), loadedName);
    QCOMPARE(controller.channelNames(), loadedChannels);
}

void SourceTests::replacesInFlightSourceLoad()
{
    QSettings settings;
    settings.clear();
    settings.sync();
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QString large = QStringLiteral("[column names]\ntime speed\n[data]\n");
    large.reserve(4'000'000);
    for (int row = 0; row < 250'000; ++row) large += QStringLiteral("%1 %2\n").arg(row).arg(row % 200);
    const QString sourceA = directory.filePath(QStringLiteral("source-a.vbo"));
    QVERIFY(writeBytes(sourceA, large.toUtf8()));

    AppController controller;
    controller.loadVbo(QUrl::fromLocalFile(sourceA));
    controller.loadVbo(QUrl::fromLocalFile(QStringLiteral(TEST_FIXTURE_PATH)));
    QTRY_COMPARE_WITH_TIMEOUT(controller.vboLoadState(), QStringLiteral("ready"), 10'000);
    QCOMPARE(controller.telemetryName(), QStringLiteral("basic.vbo"));
    QCOMPARE(controller.sampleCount(), 3);
}

void SourceTests::shutsDownWithInFlightSourceLoad()
{
    QSettings settings;
    settings.clear();
    settings.sync();
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QString large = QStringLiteral("[column names]\ntime speed\n[data]\n");
    large.reserve(4'000'000);
    for (int row = 0; row < 250'000; ++row) large += QStringLiteral("%1 %2\n").arg(row).arg(row % 200);
    const QString source = directory.filePath(QStringLiteral("shutdown.vbo"));
    QVERIFY(writeBytes(source, large.toUtf8()));
    QElapsedTimer elapsed;
    elapsed.start();
    {
        AppController controller;
        controller.loadVbo(QUrl::fromLocalFile(source));
    }
    QVERIFY2(elapsed.elapsed() < 3'000, qPrintable(QString::number(elapsed.elapsed())));
}

void SourceTests::fingerprintsSourcesDeterministically()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString first = directory.filePath(QStringLiteral("first.bin"));
    const QString second = directory.filePath(QStringLiteral("second.bin"));
    QByteArray bytes(256 * 1024, 'a');
    QVERIFY(writeBytes(first, bytes));
    QVERIFY(writeBytes(second, bytes));
    MediaInfo info;
    info.duration = 10.0;
    info.videoSize = QSize(1920, 1080);
    info.averageFrameRate = {30000, 1001};
    info.videoCodec = QStringLiteral("h264");
    const QJsonObject expected = videoSourceFingerprint(first, info);
    MediaInfo enriched = info;
    enriched.bitDepth = 10;
    enriched.pixelFormat = QStringLiteral("yuv420p10le");
    enriched.colorTransfer = QStringLiteral("bt709");
    enriched.colorPrimaries = QStringLiteral("bt709");
    enriched.sourceColorClass = SourceColorClass::Sdr;
    QCOMPARE(videoSourceFingerprint(first, enriched), expected);
    QCOMPARE(ProjectSourceReferenceCodec::compareFingerprints(
                 expected, videoSourceFingerprint(second, info)),
             SourceFingerprintMatch::Match);
    bytes[bytes.size() / 2] = 'b';
    QVERIFY(writeBytes(second, bytes));
    QCOMPARE(ProjectSourceReferenceCodec::compareFingerprints(
                 expected, videoSourceFingerprint(second, info)),
             SourceFingerprintMatch::Mismatch);
    info.videoSize = QSize(1280, 720);
    QCOMPARE(ProjectSourceReferenceCodec::compareFingerprints(
                 expected, videoSourceFingerprint(first, info)),
             SourceFingerprintMatch::Mismatch);
    QVERIFY(writeBytes(second, QByteArrayLiteral("different size")));
    info.videoSize = QSize(1920, 1080);
    QCOMPARE(ProjectSourceReferenceCodec::compareFingerprints(
                 expected, videoSourceFingerprint(second, info)),
             SourceFingerprintMatch::Mismatch);

    TelemetrySession sessionA;
    sessionA.duration = 1.0;
    sessionA.sampleCount = 2;
    TelemetryChannel speed;
    speed.name = QStringLiteral("speed");
    speed.unit = QStringLiteral("km/h");
    speed.setSamples({0.0, 1.0}, {1.0F, 2.0F});
    sessionA.channels.insert(speed.name, speed);
    TelemetrySession sessionB = sessionA;
    TelemetryChannel rpm;
    rpm.name = QStringLiteral("rpm");
    rpm.unit = QStringLiteral("rpm");
    rpm.setSamples({0.0, 1.0}, {1000.0F, 2000.0F});
    sessionB.channels.insert(rpm.name, rpm);
    const QJsonObject telemetryA = ProjectSourceReferenceCodec::telemetryFingerprint(first, sessionA);
    QCOMPARE(ProjectSourceReferenceCodec::compareFingerprints(
                 telemetryA, ProjectSourceReferenceCodec::telemetryFingerprint(first, sessionB)),
             SourceFingerprintMatch::Mismatch);
}

void SourceTests::preservesInterleavedSourceRequests_data()
{
    QTest::addColumn<bool>("videoFirst");
    QTest::addColumn<bool>("secondRelink");
    QTest::addColumn<bool>("mismatch");
    for (bool video : {false, true}) for (bool relink : {false, true}) for (bool mismatch : {false, true})
        QTest::newRow(qPrintable(QStringLiteral("video%1-relink%2-mismatch%3").arg(video).arg(relink).arg(mismatch)))
            << video << relink << mismatch;
}

void SourceTests::preservesInterleavedSourceRequests()
{
    QFETCH(bool, videoFirst); QFETCH(bool, secondRelink); QFETCH(bool, mismatch);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings; settings.clear(); settings.sync();
    const auto video = directory.filePath("clip.mp4");
    QProcess encoder;
    encoder.start(FfmpegTools::ffmpegPath(), {"-hide_banner", "-loglevel", "error", "-y", "-f", "lavfi", "-i",
        "color=c=black:s=64x64:r=30:d=1", "-c:v", "libx264", "-pix_fmt", "yuv420p", video});
    QVERIFY(encoder.waitForFinished(30'000));
    QCOMPARE(encoder.exitCode(), 0);
    auto project = testProject(0.0);
    QJsonObject videoReference{{"relativePath", "missing.mp4"}};
    QJsonObject telemetryReference{{"relativePath", "missing.vbo"}};
    if (mismatch) (videoFirst ? videoReference : telemetryReference).insert("fingerprint",
        QJsonObject{{"kind", videoFirst ? "video-v1" : "telemetry-v1"}, {"size", 1}});
    project.insert("sources", QJsonObject{{"video", videoReference}, {"telemetry", telemetryReference}});
    const auto path = directory.filePath("project.fetproject");
    QVERIFY(writeBytes(path, QJsonDocument(project).toJson()));
    AppController controller(nullptr, directory.filePath("recovery.json"));
    controller.requestOpenProject(QUrl::fromLocalFile(path));
    QTRY_COMPARE(controller.videoLoadState(), QStringLiteral("missing"));
    QTRY_COMPARE(controller.vboLoadState(), QStringLiteral("missing"));
    const auto vbo = QUrl::fromLocalFile(QStringLiteral(TEST_FIXTURE_PATH));
    if (videoFirst) {
        controller.relinkVideo(QUrl::fromLocalFile(video));
        if (secondRelink) controller.relinkVbo(vbo); else controller.loadVbo(vbo);
    } else {
        controller.relinkVbo(vbo);
        if (secondRelink) controller.relinkVideo(QUrl::fromLocalFile(video)); else controller.loadVideo(QUrl::fromLocalFile(video));
    }
    QTRY_COMPARE(videoFirst ? controller.vboLoadState() : controller.videoLoadState(), QStringLiteral("ready"));
    QTRY_COMPARE(videoFirst ? controller.videoLoadState() : controller.vboLoadState(),
                 mismatch ? QStringLiteral("mismatch") : QStringLiteral("ready"));
    if (mismatch) {
        QCOMPARE(controller.sourceMismatchType(), videoFirst ? QStringLiteral("video") : QStringLiteral("telemetry"));
        controller.resolveSourceMismatch(true);
    }
    QCOMPARE(controller.videoLoadState(), QStringLiteral("ready"));
    QCOMPARE(controller.vboLoadState(), QStringLiteral("ready"));
}

void SourceTests::decodesGps9Gpmf()
{
    QByteArray scale;
    for (const qint32 value : {10000000, 10000000, 1000, 1000, 100, 1, 1000, 100, 1}) {
        append32(scale, value);
    }
    QByteArray gps;
    for (int index = 0; index < 2; ++index) {
        append32(gps, 500000000 + index * 100);
        append32(gps, 190000000 + index * 100);
        append32(gps, 250000);
        append32(gps, 1250 + index * 250);
        append32(gps, 130);
        append32(gps, 10000);
        append32(gps, 200000 + index * 100);
        append16(gps, 150);
        append16(gps, 3);
    }
    QByteArray stream;
    stream += klvRecord("SCAL", 'l', 4, 9, scale);
    stream += klvRecord("GPS9", '?', 32, 2, gps);
    const QByteArray streamRecord = klvRecord("STRM", 0, 1, stream.size(), stream);
    const QByteArray packet = klvRecord("DEVC", 0, 1, streamRecord.size(), streamRecord);

    const GoProTelemetryResult result =
        GoProTelemetrySource::decodeGpsPackets({{packet, 10.0, 1.0}}, 20.0);
    QCOMPARE(result.gpsStream, QString("GPS9"));
    QCOMPARE(result.session.sampleCount, 2);
    const TelemetryChannel speed = result.session.channels.value("GoPro GPS speed");
    QCOMPARE(speed.timestamps(), QVector<double>({10.0, 10.5}));
    QVERIFY(qAbs(speed.values()[0] - 4.5F) < 0.001F);
    QVERIFY(qAbs(speed.values()[1] - 5.4F) < 0.001F);
}

void SourceTests::judgesGoProGpsQuality()
{
    // KAN-211. GPS5: lat, lon, alt, 2-D speed (m/s * 1000), 3-D speed.
    const auto gps5Packet = [](const qint32 speed, const std::optional<qint32> fix) {
        QByteArray scale;
        for (const qint32 value : {10000000, 10000000, 1000, 1000, 100}) append32(scale, value);
        QByteArray gps;
        for (const qint32 value : {500000000, 190000000, 250000, speed, speed}) append32(gps, value);
        QByteArray stream = klvRecord("SCAL", 'l', 4, 5, scale);
        if (fix) {
            QByteArray fixBytes;
            append32(fixBytes, *fix);
            stream += klvRecord("GPSF", 'L', 4, 1, fixBytes);
        }
        stream += klvRecord("GPS5", 'l', 20, 1, gps);
        return klvRecord("STRM", 0, 1, stream.size(), stream);
    };
    const auto gps9Stream = [](const qint32 speed, const quint16 fix) {
        QByteArray scale;
        for (const qint32 value : {10000000, 10000000, 1000, 1000, 100, 1, 1000, 100, 1}) append32(scale, value);
        QByteArray gps;
        for (const qint32 value : {500000000, 190000000, 250000, speed, 130, 10000, 200000}) append32(gps, value);
        append16(gps, 150);
        append16(gps, fix);
        QByteArray stream = klvRecord("SCAL", 'l', 4, 9, scale);
        stream += klvRecord("GPS9", '?', 32, 1, gps);
        return klvRecord("STRM", 0, 1, stream.size(), stream);
    };
    const auto device = [](const QByteArray &streams) { return klvRecord("DEVC", 0, 1, streams.size(), streams); };
    const auto speedsOf = [](const GoProTelemetryResult &result) {
        return result.session.channels.value(QStringLiteral("GoPro GPS speed")).values();
    };

    // GPS5 without GPSF: fix quality unknown, so not used.
    QString message;
    try {
        (void) GoProTelemetrySource::decodeGpsPackets({{device(gps5Packet(10000, std::nullopt)), 0.0, 1.0}}, 1.0);
    } catch (const std::runtime_error &error) {
        message = QString::fromUtf8(error.what());
    }
    QVERIFY2(message.contains(QStringLiteral("no fix information")), qPrintable(message));
    const auto mixedFix = GoProTelemetrySource::decodeGpsPackets(
        {{device(gps5Packet(10000, 3)), 0.0, 1.0}, {device(gps5Packet(20000, std::nullopt)), 1.0, 1.0}}, 2.0);
    QCOMPARE(mixedFix.session.sampleCount, 1);
    QCOMPARE(mixedFix.session.warnings.size(), 1);

    // Sparse GPS9 (first packet only) with complete GPS5: GPS5 fills the rest.
    const auto sparse = GoProTelemetrySource::decodeGpsPackets({
        {device(gps9Stream(1000, 3) + gps5Packet(9000, 3)), 0.0, 1.0},
        {device(gps5Packet(2000, 3)), 1.0, 1.0},
        {device(gps5Packet(3000, 3)), 2.0, 1.0}}, 3.0);
    QCOMPARE(sparse.gpsStream, QStringLiteral("GPS9+GPS5"));
    QCOMPARE(sparse.session.channels.value(QStringLiteral("GoPro GPS speed")).timestamps(),
             QVector<double>({0.0, 1.0, 2.0}));
    QVERIFY(qAbs(speedsOf(sparse)[0] - 3.6F) < 0.001F);
    QVERIFY(qAbs(speedsOf(sparse)[1] - 7.2F) < 0.001F);

    // The same timestamp twice, a 2-D fix then a 3-D fix: the 3-D one is kept.
    const auto duplicate = GoProTelemetrySource::decodeGpsPackets(
        {{device(gps9Stream(1000, 2)), 1.0, 1.0}, {device(gps9Stream(2000, 3)), 1.0, 1.0}}, 2.0);
    QCOMPARE(duplicate.session.sampleCount, 1);
    QVERIFY(qAbs(speedsOf(duplicate)[0] - 7.2F) < 0.001F);

    // Valid GPS followed by a malformed KLV tail keeps the valid samples.
    const auto tail = GoProTelemetrySource::decodeGpsPackets(
        {{device(gps9Stream(1000, 3)) + QByteArray("GPS9?\x20\xff\xff\x01\x02", 10), 0.0, 1.0}}, 1.0);
    QCOMPARE(tail.session.sampleCount, 1);
    QVERIFY(qAbs(speedsOf(tail)[0] - 3.6F) < 0.001F);
}

void SourceTests::rejectsMalformedGpmf()
{
    QVERIFY_THROWS_EXCEPTION(
        std::runtime_error,
        (void) GoProTelemetrySource::decodeGpsPackets({{{"broken"}, 0.0, 1.0}}, 1.0));
}

void SourceTests::cancelsSlowGoProProbePromptly()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString media = directory.filePath(QStringLiteral("slow.mp4"));
    QVERIFY(writeBytes(media, "media"));
    std::atomic_bool cancelled = false;
    std::thread canceller([&cancelled] {
        QThread::msleep(150);
        cancelled.store(true);
    });
    const auto join = qScopeGuard([&canceller] { canceller.join(); });
    QElapsedTimer elapsed;
    elapsed.start();
    QVERIFY_THROWS_EXCEPTION(
        OperationCancelled,
        (void) GoProTelemetrySource::load(
            media, [&cancelled] { return cancelled.load(); }, QStringLiteral(PROBE_TEST_HELPER_PATH)));
    QVERIFY2(elapsed.elapsed() < 3'000, qPrintable(QString::number(elapsed.elapsed())));
}

void SourceTests::boundsGoProProbeOutput()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString media = directory.filePath(QStringLiteral("large-output.mp4"));
    QVERIFY(writeBytes(media, "media"));
    try {
        (void) GoProTelemetrySource::load(media, {}, QStringLiteral(PROBE_TEST_HELPER_PATH));
        QFAIL("Expected ffprobe output to be rejected");
    } catch (const ResourceLimitError &error) {
        QVERIFY(QString::fromUtf8(error.what()).contains(QStringLiteral("ffprobe output")));
    }
}

void SourceTests::rejectsOutOfFileGpmfPackets()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    for (const QString &name : {QStringLiteral("outside.mp4"), QStringLiteral("overflow.mp4")}) {
        const QString media = directory.filePath(name);
        QVERIFY(writeBytes(media, "tiny"));
        try {
            (void) GoProTelemetrySource::load(media, {}, QStringLiteral(PROBE_TEST_HELPER_PATH));
            QFAIL("Expected malformed packet bounds to be rejected");
        } catch (const std::runtime_error &error) {
            QVERIFY(QString::fromUtf8(error.what()).contains(QStringLiteral("outside the media file")));
        }
    }
}

void SourceTests::boundsGpmfDepthAndRecordCount()
{
    QVector<GpmfPacket> tooManyPackets(GoProTelemetrySource::kMaximumPacketCount + 1);
    QVERIFY_THROWS_EXCEPTION(
        ResourceLimitError,
        (void) GoProTelemetrySource::decodeGpsPackets(tooManyPackets, 1.0));

    QByteArray nested = klvRecord("JUNK", 'c', 1, 1, QByteArray(1, 'x'));
    for (int depth = 0; depth <= GoProTelemetrySource::kMaximumContainerDepth; ++depth) {
        nested = klvRecord("DEVC", 0, 1, static_cast<quint16>(nested.size()), nested);
    }
    QVERIFY_THROWS_EXCEPTION(
        ResourceLimitError,
        (void) GoProTelemetrySource::decodeGpsPackets({{nested, 0.0, 1.0}}, 1.0));

    QByteArray manyRecords;
    manyRecords.reserve((GoProTelemetrySource::kMaximumRecordCount + 1) * 8);
    const QByteArray emptyRecord = klvRecord("JUNK", 'c', 1, 0, {});
    for (qsizetype index = 0; index <= GoProTelemetrySource::kMaximumRecordCount; ++index) {
        manyRecords += emptyRecord;
    }
    try {
        (void) GoProTelemetrySource::decodeGpsPackets({{manyRecords, 0.0, 1.0}}, 1.0);
        QFAIL("Expected excessive GPMF record headers to be rejected");
    } catch (const ResourceLimitError &error) {
        const QString diagnostic = QString::fromUtf8(error.what());
        QVERIFY(diagnostic.contains(QString::number(GoProTelemetrySource::kMaximumRecordCount + 1)));
        QVERIFY(diagnostic.contains(QString::number(GoProTelemetrySource::kMaximumRecordCount)));
        QVERIFY(diagnostic.contains(QStringLiteral("packet 1")));
        QVERIFY(diagnostic.contains(QStringLiteral("KLV header")));
    }
}

// KAN-197: nested GPMF containers are parsed in place. Copying each level's
// payload made a 15 MiB packet nested 31 deep cost about 470 MiB.
void SourceTests::decodesNestedGpmfWithoutCopies()
{
    QByteArray nested = klvRecord("JUNK", 'c', 255, 61'680, QByteArray(255 * 61'680, 'x'));
    for (int depth = 1; depth < GoProTelemetrySource::kMaximumContainerDepth; ++depth) {
        nested.append(QByteArray((255 - nested.size() % 255) % 255, '\0'));
        nested = klvRecord("DEVC", 0, 255, static_cast<quint16>(nested.size() / 255), nested);
    }
    QVERIFY(nested.size() < GoProTelemetrySource::kMaximumPacketBytes);
    const auto peakResidentMiB = [] {
#if defined(Q_OS_UNIX)
        rusage usage{};
        getrusage(RUSAGE_SELF, &usage);
#if defined(Q_OS_MACOS)
        return static_cast<double>(usage.ru_maxrss) / (1024.0 * 1024.0);
#else
        return static_cast<double>(usage.ru_maxrss) / 1024.0;
#endif
#else
        return 0.0; // Not measured on Windows.
#endif
    };
    const double before = peakResidentMiB();
    // No GPS stream: the decode reports that after walking every level.
    QVERIFY_THROWS_EXCEPTION(std::runtime_error,
        (void) GoProTelemetrySource::decodeGpsPackets({{nested, 0.0, 1.0}}, 1.0));
    const double growth = peakResidentMiB() - before;
    qInfo().noquote() << QString("peak resident growth %1 MiB").arg(growth, 0, 'f', 1);
    QVERIFY2(growth < 150.0, qPrintable(QString("peak memory grew by %1 MiB").arg(growth)));

    // One packet may not take the whole metadata budget.
    const QByteArray oversized(GoProTelemetrySource::kMaximumPacketBytes + 1, '\0');
    QVERIFY_THROWS_EXCEPTION(ResourceLimitError,
        (void) GoProTelemetrySource::decodeGpsPackets({{oversized, 0.0, 1.0}}, 1.0));
}

void SourceTests::normalizesGpmfTimestamps()
{
    const auto packetAt = [](const double pts, const qint32 speed) {
        QByteArray scale;
        for (const qint32 value : {10000000, 10000000, 1000, 1000, 100, 1, 1000, 100, 1}) append32(scale, value);
        QByteArray gps;
        for (const qint32 value : {500000000, 190000000, 250000, speed, 130, 10000, 200000}) append32(gps, value);
        append16(gps, 150);
        append16(gps, 3);
        QByteArray stream = klvRecord("SCAL", 'l', 4, 9, scale);
        stream += klvRecord("GPS9", '?', 32, 1, gps);
        const QByteArray streamRecord = klvRecord("STRM", 0, 1, stream.size(), stream);
        return GpmfPacket{klvRecord("DEVC", 0, 1, streamRecord.size(), streamRecord), pts, 1.0};
    };
    const GoProTelemetryResult result = GoProTelemetrySource::decodeGpsPackets(
        {packetAt(2.0, 1000), packetAt(1.0, 2000), packetAt(1.0, 3000)}, 3.0);
    const TelemetryChannel speed = result.session.channels.value(QStringLiteral("GoPro GPS speed"));
    QCOMPARE(speed.timestamps(), QVector<double>({1.0, 2.0}));
    QVERIFY(speed.timestamps()[1] > speed.timestamps()[0]);
    QVERIFY(qAbs(speed.values()[0] - 7.2F) < 0.001F);
}

void SourceTests::boundsTimeTransforms_data()
{
    QTest::addColumn<double>("time");
    QTest::addColumn<double>("offset");
    QTest::addColumn<double>("scale");
    QTest::addColumn<bool>("forwardValid");
    QTest::addColumn<bool>("inverseValid");
    const double maximum = std::numeric_limits<double>::max();
    const double tiny = std::numeric_limits<double>::denorm_min();
    const double inf = std::numeric_limits<double>::infinity();
    const double nan = std::numeric_limits<double>::quiet_NaN();
    QTest::newRow("ordinary") << 10.0 << 2.5 << 1.01 << true << true;
    QTest::newRow("negative-offset") << 10.0 << -20.0 << 2.0 << true << true;
    QTest::newRow("large-finite") << maximum / 4 << maximum / 4 << 2.0 << true << true;
    QTest::newRow("product-overflow") << 2.0 << 0.0 << maximum << false << true;
    QTest::newRow("sum-overflow") << maximum << maximum << 1.0 << false << true;
    QTest::newRow("inverse-subtraction-overflow") << maximum << -maximum << 1.0 << true << false;
    QTest::newRow("inverse-division-overflow") << 1.0 << 0.0 << tiny << true << false;
    QTest::newRow("tiny-scale-zero-time") << 0.0 << 0.0 << tiny << true << true;
    QTest::newRow("zero-scale") << 1.0 << 0.0 << 0.0 << false << false;
    QTest::newRow("negative-scale") << 1.0 << 0.0 << -1.0 << false << false;
    QTest::newRow("infinite-time") << inf << 0.0 << 1.0 << false << false;
    QTest::newRow("nan-time") << nan << 0.0 << 1.0 << false << false;
    QTest::newRow("infinite-offset") << 1.0 << inf << 1.0 << false << false;
    QTest::newRow("nan-offset") << 1.0 << nan << 1.0 << false << false;
    QTest::newRow("infinite-scale") << 1.0 << 0.0 << inf << false << false;
    QTest::newRow("nan-scale") << 1.0 << 0.0 << nan << false << false;
}

void SourceTests::boundsTimeTransforms()
{
    QFETCH(double, time); QFETCH(double, offset); QFETCH(double, scale);
    QFETCH(bool, forwardValid); QFETCH(bool, inverseValid);
    const auto forward = videoToTelemetryTime(time, {offset, scale});
    const auto inverse = telemetryToVideoTime(time, {offset, scale});
    QCOMPARE(forward.has_value(), forwardValid);
    QCOMPARE(inverse.has_value(), inverseValid);
    if (forward) { QVERIFY(std::isfinite(*forward)); QCOMPARE(*forward, time * scale + offset); }
    if (inverse) { QVERIFY(std::isfinite(*inverse)); QCOMPARE(*inverse, (time - offset) / scale); }
}

void SourceTests::exposesNoDataForOverflowingTransforms()
{
    const auto session = VboParser::parse(
        u"[header]\ncoordinate units = degrees\n[column names]\ntime speed latitude longitude\n[data]\n0 0 0 0\n2 20 .0002 .0002\n3 30 .0003 .0003\n10 100 .001 .001\n");
    const auto geometry = buildTrackGeometry(session);
    const auto laps = deriveSourceLapSession(VboParser::parse(QString::fromUtf8(EventProjectFixture::lapsVbo())));
    QVERIFY(laps.status == LapSessionStatus::Available);
    TelemetryRenderContext context;
    context.setSession(&session);
    context.setTrackGeometry(&geometry);
    context.setLapSession(laps);
    context.setSyncTransform({0, std::numeric_limits<double>::max()});
    context.setTime(2);
    // This exact context is shared by preview and offscreen export rendering.
    QVERIFY(!context.telemetryTime().isValid());
    QVERIFY(!context.telemetryValue("speed").isValid());
    QCOMPARE(context.valueText("speed"), QString("—"));
    QVERIFY(context.currentTrackPoint().isEmpty());
    QVERIFY(!context.lapTiming().value("available").toBool());
    context.setSyncTransform({0, 1});
    QCOMPARE(context.telemetryTime().toDouble(), 2.0);
    QVERIFY(context.telemetryValue("speed").isValid());
    QVERIFY(!context.currentTrackPoint().isEmpty());

    QTemporaryDir directory;
    AppController controller(nullptr, directory.filePath("recovery.json"));
    controller.m_session = std::make_unique<TelemetrySession>(session);
    controller.syncController()->setTimeScale(std::numeric_limits<double>::max());
    controller.m_playbackTime = 2;
    QVERIFY(!controller.telemetryValue("speed").isValid());
    QCOMPARE(controller.valueText("speed"), QString("—"));
    QVERIFY(controller.telemetrySeries("speed", 2, 3, 100).isEmpty());
    controller.syncController()->setTimeScale(1);
    QCOMPARE(controller.telemetryValue("speed").toDouble(), 20.0);
    QVERIFY(!controller.telemetrySeries("speed", 2, 3, 100).isEmpty());
}

void SourceTests::rejectsUnsafeSynchronizationInputs_data()
{
    QTest::addColumn<int>("fault");
    const char *names[] = {"empty", "mismatched", "nan-time", "infinite-time", "duplicate-time",
        "backward-time", "stalled-grid", "overflowing-difference", "sample-grid-budget",
        "offset-grid-budget", "pair-work-budget", "source-sample-budget"};
    for (int i = 0; i < 12; ++i) QTest::newRow(names[i]) << i;
}

void SourceTests::rejectsUnsafeSynchronizationInputs()
{
    QFETCH(int, fault);
    auto video = speedSession(0, 30, 0);
    auto telemetry = video;
    auto &a = video.channels["speed"];
    auto &b = telemetry.channels["speed"];
    auto ta = a.timestamps();
    auto va = a.values();
    auto tb = b.timestamps();
    auto vb = b.values();
    if (fault == 0) { ta.clear(); va.clear(); }
    if (fault == 1) va.removeLast();
    if (fault == 2) ta[1] = std::numeric_limits<double>::quiet_NaN();
    if (fault == 3) ta[1] = std::numeric_limits<double>::infinity();
    if (fault == 4) ta[1] = ta[0];
    if (fault == 5) ta[1] = -1;
    if (fault >= 1 && fault <= 5) {
        // KAN-209: a channel cannot hold these samples at all.
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, a.setSamples(ta, va));
        return;
    }
    if (fault >= 6) {
        ta.resize(20); va.resize(20);
        tb.resize(20); vb.resize(20);
        double negative = -std::numeric_limits<double>::max();
        double positive = std::numeric_limits<double>::max() * .9;
        for (int i = 0; i < 20; ++i) {
            ta[i] = tb[i] = i;
            if (fault == 6) ta[i] = tb[i] = 1e16 + i * 2.0;
            if (fault == 7) {
                ta[i] = negative; tb[i] = positive;
                negative = std::nextafter(negative, 0.0);
                positive = std::nextafter(positive, std::numeric_limits<double>::infinity());
            }
            if (fault == 8) ta[i] = tb[i] = i * 100000.0;
            if (fault == 9) tb[i] = i * 100000.0;
            if (fault == 10) { ta[i] = i * 1000.0; tb[i] = i * 1500.0; }
        }
        if (fault == 11) {
            ta.resize(kMaximumSyncSignalSamples + 1);
            va.resize(kMaximumSyncSignalSamples + 1);
            for (qsizetype i = 20; i < ta.size(); ++i) ta[i] = static_cast<double>(i);
        }
    }
    a.setSamples(ta, va);
    b.setSamples(tb, vb);
    int checks = 0;
    bool rejected = false;
    try {
        (void) TelemetrySyncEngine::synchronize(video, telemetry, [&] { return ++checks > 1000; });
    } catch (const OperationCancelled &) {
        QFAIL("Unsafe input reached the cancellation watchdog instead of a bounded error.");
    } catch (const std::runtime_error &error) {
        rejected = true;
        QVERIFY(QString::fromUtf8(error.what()).contains("Synchronization"));
    }
    QVERIFY(rejected);
    QVERIFY(checks <= 1000);
}

void SourceTests::preservesConfirmedTransformForAmbiguousResult()
{
    QTemporaryDir directory;
    AppController controller(nullptr, directory.filePath("recovery.json"));
    controller.syncController()->setOffset(7.25);
    controller.syncController()->setTimeScale(1.003);
    auto video = speedSession(0, 30, 0);
    auto telemetry = speedSession(0, 35, 0);
    for (qsizetype i = 0; i < video.channels["speed"].sampleCount(); ++i) video.channels["speed"].setValue(i, 42.0F);
    for (qsizetype i = 0; i < telemetry.channels["speed"].sampleCount(); ++i) telemetry.channels["speed"].setValue(i, 42.0F);
    SyncController::AutoSyncResult result;
    result.success = true;
    result.generation = controller.m_document.m_sourceGeneration;
    result.syncRevision = controller.m_syncController.m_syncRevision;
    result.candidate = TelemetrySyncEngine::synchronize(video, telemetry);
    QVERIFY(!shouldAutoApplySyncCandidate(result.candidate));
    QPromise<SyncController::AutoSyncResult> promise;
    promise.start();
    controller.m_syncController.m_syncWatcher.setFuture(promise.future());
    QSignalSpy finished(controller.syncController(), &SyncController::runningChanged);
    promise.addResult(result); promise.finish();
    QTRY_VERIFY(!finished.isEmpty());
    QCOMPARE(controller.syncController()->offset(), 7.25);
    QCOMPARE(controller.syncController()->timeScale(), 1.003);
    QVERIFY(!controller.syncController()->candidate().isEmpty());
}

void SourceTests::rejectsInvalidAutomaticCandidates()
{
    SyncCandidate candidate;
    candidate.confidence = 1;
    for (const double confidence : {std::numeric_limits<double>::quiet_NaN(),
             std::numeric_limits<double>::infinity(), -1.0, 1.01}) {
        candidate.confidence = confidence;
        QVERIFY(!shouldAutoApplySyncCandidate(candidate));
    }
    candidate.confidence = 1;
    candidate.offset = std::numeric_limits<double>::infinity();
    QVERIFY(!shouldAutoApplySyncCandidate(candidate));
    candidate.offset = 0;
    for (const double scale : {0.0, -1.0, std::numeric_limits<double>::infinity()}) {
        candidate.timeScale = scale;
        QVERIFY(!shouldAutoApplySyncCandidate(candidate));
    }
}

void SourceTests::keepsExtremeFiniteSyncSignalsBounded()
{
    auto session = speedSession(0, 30, 0);
    auto &speed = session.channels["speed"];
    for (qsizetype i = 0; i < speed.sampleCount(); ++i)
        speed.setValue(i, (i % 2 ? 1.0F : -1.0F) * std::numeric_limits<float>::max());
    const auto candidate = TelemetrySyncEngine::synchronize(session, session);
    QVERIFY(std::isfinite(candidate.offset));
    QVERIFY(std::isfinite(candidate.confidence));
    QVERIFY(candidate.confidence >= 0 && candidate.confidence <= 1);
    QVERIFY(candidate.diagnostics.correlation > .99);
}

void SourceTests::synchronizesGpsSpeed()
{
    const TelemetrySession video = speedSession(0.0, 60.0, 0.0);
    const TelemetrySession telemetry = speedSession(0.0, 70.0, 3.2);
    const SyncCandidate candidate = TelemetrySyncEngine::synchronize(video, telemetry);
    QVERIFY2(qAbs(candidate.offset - 3.2) <= 0.11, qPrintable(QString::number(candidate.offset)));
    QVERIFY(candidate.diagnostics.correlation > 0.99);
    QVERIFY(shouldAutoApplySyncCandidate(candidate));
}

void SourceTests::cancelsSynchronizationDeterministically()
{
    const TelemetrySession video = speedSession(0.0, 600.0, 0.0);
    const TelemetrySession telemetry = speedSession(0.0, 700.0, 3.2);
    int checks = 0;
    QVERIFY_THROWS_EXCEPTION(
        OperationCancelled,
        (void) TelemetrySyncEngine::synchronize(
            video, telemetry, [&checks] { return ++checks == 20; }));
    QVERIFY(checks >= 20);
}

void SourceTests::reportsAmbiguousGpsSpeed()
{
    TelemetrySession video = speedSession(0.0, 30.0, 0.0);
    TelemetrySession telemetry = speedSession(0.0, 35.0, 0.0);
    for (qsizetype i = 0; i < video.channels["speed"].sampleCount(); ++i) video.channels["speed"].setValue(i, 42.0F);
    for (qsizetype i = 0; i < telemetry.channels["speed"].sampleCount(); ++i) telemetry.channels["speed"].setValue(i, 42.0F);
    const SyncCandidate candidate = TelemetrySyncEngine::synchronize(video, telemetry);
    QCOMPARE(candidate.diagnostics.correlation, -1.0);
    QCOMPARE(candidate.confidence, 0.0);
}

void SourceTests::retainsGlobalSyncAmbiguity()
{
    const auto periodicSession = [](const int seconds) {
        TelemetrySession session;
        TelemetryChannel speed;
        speed.name = QStringLiteral("speed");
        for (int index = 0; index <= seconds * 10; ++index) {
            const double time = index / 10.0;
            speed.appendSample(time, static_cast<float>(70.0
                + 20.0 * std::sin(2.0 * std::numbers::pi * time / 20.0)
                + 8.0 * std::sin(2.0 * std::numbers::pi * time / 5.0)));
        }
        session.channels.insert(speed.name, speed);
        session.aliases.insert(QStringLiteral("speed"), speed.name);
        return session;
    };
    // Equally valid offsets of 0, 20, and 40 seconds; only one is inside refinement.
    const auto candidate = TelemetrySyncEngine::synchronize(periodicSession(40), periodicSession(80));
    QVERIFY(candidate.diagnostics.correlation > 0.99);
    QVERIFY(candidate.diagnostics.peakUniqueness < 0.01);
    QVERIFY(candidate.confidence < kAutomaticSyncConfidenceThreshold);
    QVERIFY(!shouldAutoApplySyncCandidate(candidate));
}

void SourceTests::countsANearbyFalseSyncPeak()
{
    // KAN-214: the telemetry speed holds the video's trace twice, 3 s apart
    // (an echo of equal strength), so offsets 100 s and 103 s match about
    // equally. Only offsets at least 5 s away used to count as competitors.
    const auto knot = [](double second) {
        const double hashed = std::sin(second * 12.9898 + 78.233) * 43758.5453;
        return 40.0 + 120.0 * (hashed - std::floor(hashed));
    };
    const auto world = [&](double time) {
        const double base = std::floor(time), fraction = time - base;
        return knot(base) + (knot(base + 1.0) - knot(base)) * fraction;
    };
    const auto session = [](const auto &value, double from, double to) {
        TelemetrySession result;
        TelemetryChannel speed;
        speed.name = QStringLiteral("speed");
        for (double time = from; time <= to + 1e-9; time += 0.2) {
            speed.appendSample(time, static_cast<float>(value(time)));
        }
        result.channels.insert(speed.name, speed);
        result.aliases.insert(QStringLiteral("speed"), speed.name);
        return result;
    };
    const auto video = session(world, 0.0, 300.0);
    const auto echoed = session([&](double time) { return world(time - 100.0) + world(time - 103.0); }, 50.0, 450.0);
    const auto candidate = TelemetrySyncEngine::synchronize(video, echoed);
    QVERIFY2(qAbs(candidate.offset - 100.0) <= 0.11 || qAbs(candidate.offset - 103.0) <= 0.11,
             qPrintable(QString::number(candidate.offset)));
    QVERIFY2(candidate.diagnostics.peakUniqueness < 0.2,
             qPrintable(QString::number(candidate.diagnostics.peakUniqueness)));
    QVERIFY(!shouldAutoApplySyncCandidate(candidate));

    // Without the echo the same trace is unique and applied automatically.
    const auto clean = TelemetrySyncEngine::synchronize(video, session([&](double time) { return world(time - 100.0); }, 50.0, 450.0));
    QVERIFY(qAbs(clean.offset - 100.0) <= 0.11);
    QVERIFY2(clean.diagnostics.peakUniqueness > 0.9, qPrintable(QString::number(clean.diagnostics.peakUniqueness)));
    QVERIFY(shouldAutoApplySyncCandidate(clean));
}

void SourceTests::rejectsAutomaticSyncWithShortOverlap()
{
    // Both inputs pass the sample-count check and correlate strongly, but the
    // shorter recording cannot provide twenty seconds of usable overlap.
    const auto candidate = TelemetrySyncEngine::synchronize(
        speedSession(0.0, 60.0, 0.0), speedSession(0.0, 10.0, 3.2));
    QVERIFY(candidate.diagnostics.correlation > 0.9);
    QVERIFY(candidate.diagnostics.validSamples <
            candidate.diagnostics.sampleRate * kMinimumSyncOverlapSeconds + 1.0);
    QVERIFY(!shouldAutoApplySyncCandidate(candidate));
}

void SourceTests::synchronizesWhenTheRecordingsOnlyPartlyOverlap()
{
    // KAN-146: one speed trace in world time; the telemetry clock is the
    // video clock plus 100 s. The camera may start before the logger, stop
    // after it, run about as long, or run much longer: the true offset is
    // found in every case, not only when the video lies inside the telemetry.
    constexpr double offset = 100.0;
    // Aperiodic and trend-free, like a real speed trace: deterministic
    // pseudo-random speeds at whole seconds, linearly interpolated, so no
    // shift of a long stretch matches another.
    const auto knot = [](double second) {
        const double hashed = std::sin(second * 12.9898 + 78.233) * 43758.5453;
        return 40.0 + 120.0 * (hashed - std::floor(hashed));
    };
    const auto trace = [&](double from, double to, double clockOffset) {
        TelemetrySession session;
        TelemetryChannel speed;
        speed.name = QStringLiteral("speed");
        for (double time = from; time <= to + 1e-9; time += 0.2) {
            const double world = time - clockOffset, base = std::floor(world), fraction = world - base;
            speed.appendSample(time, static_cast<float>(knot(base) + (knot(base + 1.0) - knot(base)) * fraction));
        }
        session.channels.insert(speed.name, speed);
        session.aliases.insert(QStringLiteral("speed"), speed.name);
        return session;
    };
    const auto video = [&](double from, double to) { return trace(from, to, 0.0); };
    const auto logger = [&](double from, double to) { return trace(from + offset, to + offset, offset); };
    struct Case { const char *name; TelemetrySession video, telemetry; };
    const QList<Case> cases{
        {"camera starts before the logger", video(0, 300), logger(30, 900)},
        {"camera stops after the logger", video(0, 300), logger(-500, 260)},
        {"near-equal durations", video(0, 300), logger(5, 302)},
        {"video longer than the telemetry", video(0, 600), logger(200, 300)},
    };
    for (const auto &item : cases) {
        const auto candidate = TelemetrySyncEngine::synchronize(item.video, item.telemetry);
        QVERIFY2(qAbs(candidate.offset - offset) <= 0.11,
                 qPrintable(QString("%1: offset %2").arg(item.name).arg(candidate.offset)));
        QVERIFY2(candidate.diagnostics.validSamples >= candidate.diagnostics.sampleRate * kMinimumSyncOverlapSeconds,
                 qPrintable(QString("%1: %2 samples").arg(item.name).arg(candidate.diagnostics.validSamples)));
        QVERIFY2(shouldAutoApplySyncCandidate(candidate), qPrintable(QString("%1: confidence %2").arg(item.name).arg(candidate.confidence)));
    }
}

void SourceTests::neverAutoAppliesAnotherLapOfPeriodicLaps()
{
    // KAN-146: exactly periodic 90 s laps at 10 Hz. Offsets a whole lap apart
    // match equally well, so whichever is reported must not be applied
    // automatically unless it is the true one.
    const auto laps = [](double from, double to, double clockOffset) {
        TelemetrySession session;
        TelemetryChannel speed;
        speed.name = QStringLiteral("speed");
        for (double time = from; time <= to + 1e-9; time += 0.1) {
            const double world = time - clockOffset;
            const double phase = 2.0 * std::numbers::pi * std::fmod(world + 9000.0, 90.0) / 90.0;
            speed.appendSample(time, static_cast<float>(120.0 + 40.0 * std::sin(phase) + 15.0 * std::sin(3.0 * phase + 0.4)));
        }
        session.channels.insert(speed.name, speed);
        session.aliases.insert(QStringLiteral("speed"), speed.name);
        return session;
    };
    struct Case { const char *name; double trueOffset; TelemetrySession video, telemetry; };
    const QList<Case> cases{
        {"video inside the logger", 70.0, laps(0, 300, 0), laps(-70 + 70, 600 + 70, 70)},
        {"camera starts 30 s before the logger", -30.0, laps(0, 300, 0), laps(30 - 30, 600 - 30, -30)},
        {"camera stops 40 s after the logger", 70.0, laps(0, 300, 0), laps(-300 + 70, 260 + 70, 70)},
    };
    for (const auto &item : cases) {
        const auto candidate = TelemetrySyncEngine::synchronize(item.video, item.telemetry);
        const bool applied = shouldAutoApplySyncCandidate(candidate);
        qInfo().noquote() << QString("%1: offset %2 confidence %3 uniqueness %4 applied %5").arg(item.name)
            .arg(candidate.offset, 0, 'f', 2).arg(candidate.confidence, 0, 'f', 3)
            .arg(candidate.diagnostics.peakUniqueness, 0, 'f', 3).arg(applied);
        QVERIFY2(!applied || qAbs(candidate.offset - item.trueOffset) <= 0.11,
                 qPrintable(QString("%1: auto-applied %2 instead of %3").arg(item.name).arg(candidate.offset).arg(item.trueOffset)));
    }
}

void SourceTests::syncsOptionalRealRecording()
{
    const QString videoPath = qEnvironmentVariable("FLAPPEDEAR_REAL_GOPRO");
    const QString vboPath = qEnvironmentVariable("FLAPPEDEAR_REAL_VBO");
    if (videoPath.isEmpty() || vboPath.isEmpty()) {
        QSKIP("FLAPPEDEAR_REAL_GOPRO and FLAPPEDEAR_REAL_VBO are not set");
    }
    const GoProTelemetryResult video = GoProTelemetrySource::load(videoPath);
    const TelemetrySession telemetry = VboParser::parseFile(vboPath);
    const SyncCandidate candidate = TelemetrySyncEngine::synchronize(video.session, telemetry);
    qInfo().noquote()
        << QStringLiteral("real GoPro: %1 packets, %2 records, %3 %4 samples; offset=%5 correlation=%6 confidence=%7")
               .arg(video.packetCount)
               .arg(video.recordCount)
               .arg(video.session.sampleCount)
               .arg(video.gpsStream)
               .arg(candidate.offset, 0, 'f', 3)
               .arg(candidate.diagnostics.correlation, 0, 'f', 3)
               .arg(candidate.confidence, 0, 'f', 3);
    QVERIFY(video.packetCount > 0);
    QVERIFY(video.session.sampleCount > 100);
    QVERIFY(candidate.diagnostics.correlation > 0.8);
    QVERIFY(candidate.confidence > 0.5);
    const LapSession laps = deriveSourceLapSession(telemetry);
    for (const TimedLap &lap : laps.timedLaps) {
        const auto videoTime = telemetryToVideoTime(
            lap.startTelemetryTime, {candidate.offset, candidate.timeScale});
        QVERIFY(videoTime.has_value());
        qInfo().noquote() << QStringLiteral(
            "real lap video mapping: lap=%1 telemetryStart=%2 videoStart=%3")
                                 .arg(lap.number)
                                 .arg(lap.startTelemetryTime, 0, 'f', 3)
                                 .arg(*videoTime, 0, 'f', 3);
    }
}

#define main nativeTestMain
QTEST_MAIN(SourceTests)
#undef main

int main(int argc, char *argv[])
{
    return runWithExportWorker(argc, argv, nativeTestMain);
}
#include "NativeSourceTests.moc"
