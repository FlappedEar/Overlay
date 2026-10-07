// KAN-161: export targets, storage, process supervision, FFmpeg composition and diagnostics.
// Split from the former TelemetryTests.cpp; shared helpers are in NativeTestSupport.h.
#include "NativeTestSupport.h"
#include "app/AppController.h"
#include "export/EncoderDetector.h"
#include "export/BoundedProcessOutput.h"
#include "export/ChapterSource.h"
#include "export/ExportEngine.h"
#include "export/FinalOutputValidation.h"
#include "export/ExportFormat.h"
#include "export/ExportMediaProfile.h"
#include "export/PersistentExportLog.h"
#include "export/RawFrameTransport.h"
#include "export/ExportProgress.h"
#include "export/ExportOutputTransaction.h"
#include "export/ExportTargetIdentity.h"
#include "export/ExportArtifactManifest.h"
#include "export/ExportCancellation.h"
#include "export/ExportProcessSupervisor.h"
#include "export/ExportStoragePolicy.h"
#include "export/FfmpegTools.h"
#include "export/MediaProbe.h"
#include "export/TelemetryFrameRenderer.h"
#include "export/TemporaryOverlayValidation.h"
#include "export/VideoComposition.h"
#include "telemetry/TelemetryRenderContext.h"
#include "telemetry/VboParser.h"
#include "RczFixture.h"
#include "EventProjectFixture.h"

using namespace NativeTestSupport;

class ExportTests final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void exportsSyntheticRczThroughWorker_data();
    void exportsSyntheticRczThroughWorker();
    void workerRefusesPathsOutsideItsManifest();
    void exportsEditListSourceThroughWorker();
    void exportsChapteredSourceThroughWorker();
    void protectsEveryDaySourceFromExport();
    void keepsOutputSafeWhenTheDestinationFills_data();
    void keepsOutputSafeWhenTheDestinationFills();
    void describesOutOfSpaceExportFailures();
    void rendersTelemetryAtExplicitTime();
    void resolvesExportFilesystemsForFutureArtifacts();
    void evaluatesIndependentExportStorageVolumes();
    void estimatesTemporaryStorageFromRepresentativeSample();
    void streamsRawFramesToSlowConsumer();
    void failsRawFrameTransportWhenConsumerExits();
    void timesOutStalledRawFrameTransport();
    void cancelsBlockedRawFrameTransportPromptly();
    void cleansOnlyManifestOwnedArtifacts();
    void preservesLiveManifestForStartupRecovery();
    void recoversManifestsFromDataAndLegacyFolders();
    void keepsStaleExportArtifactsFromCommandLineRuns();
    void noticesWhenTheParentProcessExits();
    void supervisesUnixExportProcessTree();
    void stopsUnixWritersAcrossLeaderExit_data();
    void stopsUnixWritersAcrossLeaderExit();
    void stopsUnixWritersBeforeControllerCleanup_data();
    void stopsUnixWritersBeforeControllerCleanup();
    void boundsImmediateProcessTreeStop();
    void stopsExportWorkerWhenCancellationMarkerCannotBeCreated();
    void detectsHevcEncoders();
    void cancelsEncoderDiscovery();
    void calculatesTimestampDrivenExportFrames();
    void resolvesExplicitExportFormats();
    void derivesSourceDrivenExportProfiles();
    void rejectsUnsupportedExportDisplayTransforms();
    void validatesHighResolutionCapabilitiesAndCache();
    void preservesTenBitSdrThroughComposition();
    void preservesTenBitFullRangeColorThroughVideoToolboxExport();
    void preservesExactExportRateRationals();
    void schedulesFrameAddressedExportRangesExactly();
    void floorsConvertedFrameCounts_data();
    void floorsConvertedFrameCounts();
    void enforcesStrictTerminalFrameDeficitEvidence();
    void boundsExportValidationAndWorkerDiagnostics();
    void plansBoundedStageBSourceAccess();
    void checksCompositionFiltersBeforeRendering();
    void preservesFramesWithPositiveSourcePts_data();
    void preservesFramesWithPositiveSourcePts();
    void placesAdditionalVideosByLayout();
    void timesAdditionalVideosAgainstTheMainVideo();
    void composesAnAdditionalVideoOnItsOwnTimeline();
    void preservesCfrCadenceForCommonRates();
    void validatesQuantizedTemporaryOverlayCadence();
    void preservesAbsoluteExportTimestamps();
    void composesNonZeroExportRangeWithZeroBasedOutput();
    void composes5994SixtySecondNonZeroRange();
    void normalizesNonZeroStreamPtsForVideoAndAudio();
    void convertsVfrInputToCfrWithFrameCorrectOverlay();
    void estimatesExportProgress();
    void tracksExportStageElapsedTime();
    void boundsVerboseDiagnosticStorage();
    void showsVeryVerboseDiagnosticsLive();
    void persistsExportDiagnosticsAndRetainsKnownLogs();
    void formatsStageAFailureDiagnostics();
    void throttlesDiagnosticHeartbeats();
    void tracksValidationSubstepStages();
    void parsesStructuredFfmpegProgress();
    void boundsProcessOutputAndProgressLines();
    void calculatesEncodedOutputProgress();
    void preservesFrameIdentityThroughCompletedOverlayComposition();
    void preservesPremultipliedAlphaThroughOverlayComposition();
    void cancelsExportWorkerDuringTelemetryPreparation();
    void rejectsUnsafeExportPaths();
    void capturesExportTargetIdentity();
    void rejectsChangedExportTargets();
    void rejectsSymlinkExportTargets();
    void preservesExistingExportTargetOnFailures_data();
    void preservesExistingExportTargetOnFailures();
    void commitsNewAndReplacementExports();
};

void ExportTests::initTestCase()
{
    isolateSettings(QStringLiteral("ExportTests"));
}

void ExportTests::cleanupTestCase()
{
    clearSettings();
}

namespace {
// routeVbo() with each 48 s lap split into two halves of the revolution
// driven at 0.9x and 1.1x the nominal time: every lap still takes exactly
// 48 s, but one half is 10% quicker. `fastFirstHalf` picks which half.
QByteArray warpedRouteVbo(const bool fastFirstHalf, const int samplesPerLap = 240)
{
    const auto lines = QString::fromUtf8(EventProjectFixture::routeVbo(samplesPerLap)).split('\n');
    QStringList out;
    bool data = false;
    int index = 0;
    double time = 0.0;
    for (const auto &line : lines) {
        if (!data || line.trimmed().isEmpty()) {
            out << line;
            data = data || line == "[data]";
            continue;
        }
        if (index > 0) {
            const bool firstHalf = (index - 1) % samplesPerLap < samplesPerLap / 2;
            time += 48.0 / samplesPerLap * (firstHalf == fastFirstHalf ? 0.9 : 1.1);
        }
        auto fields = line.split(' ');
        fields[0] = QString::number(time, 'f', 6);
        out << fields.join(' ');
        ++index;
    }
    return out.join('\n').toUtf8();
}

} // namespace

void ExportTests::rejectsUnsafeExportPaths()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString input = directory.filePath("input.mp4");
    const QString vbo = directory.filePath("telemetry.vbo");
    QVERIFY(writeBytes(input, "input"));
    QVERIFY(writeBytes(vbo, "vbo"));

    ExportOutputTransaction sameInput;
    QCOMPARE(sameInput.prepare(input, input, {vbo}, false).status,
             ExportOutputTransaction::PreparationStatus::Error);

    ExportOutputTransaction sameVbo;
    QCOMPARE(sameVbo.prepare(vbo, input, {vbo}, false).status,
             ExportOutputTransaction::PreparationStatus::Error);

    ExportOutputTransaction missingDestination;
    QCOMPARE(missingDestination.prepare(directory.filePath("missing/out.mp4"), input, {vbo}, false).status,
             ExportOutputTransaction::PreparationStatus::Error);

#ifndef Q_OS_WIN
    const QString unwritablePath = directory.filePath("unwritable");
    QVERIFY(QDir().mkdir(unwritablePath));
    const QFile::Permissions originalPermissions = QFileInfo(unwritablePath).permissions();
    QVERIFY(QFile::setPermissions(
        unwritablePath, QFileDevice::ReadOwner | QFileDevice::ExeOwner));
    const auto restorePermissions = qScopeGuard([&] {
        QFile::setPermissions(unwritablePath, originalPermissions);
    });
    ExportOutputTransaction unwritableDestination;
    QCOMPARE(unwritableDestination.prepare(
                 QDir(unwritablePath).filePath("out.mp4"), input, {vbo}, false).status,
             ExportOutputTransaction::PreparationStatus::Error);
#endif
}

void ExportTests::capturesExportTargetIdentity()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString target = directory.filePath(QStringLiteral("target.mp4"));
    QVERIFY(writeBytes(target, "original"));

    ExportTargetIdentity original;
    QString error;
    QCOMPARE(ExportTargetIdentity::capture(target, &original, &error),
             ExportTargetIdentity::CaptureStatus::Captured);
    QVERIFY2(original.isValid(), qPrintable(error));
    ExportTargetIdentity unchanged;
    QCOMPARE(ExportTargetIdentity::capture(target, &unchanged, &error),
             ExportTargetIdentity::CaptureStatus::Captured);
    QVERIFY(original.matches(unchanged));

    QVERIFY(writeBytes(target, "changed!"));
    QFile modifiedFile(target);
    QVERIFY(modifiedFile.open(QIODevice::ReadWrite));
    QVERIFY(modifiedFile.setFileTime(
        QDateTime::fromMSecsSinceEpoch(946684800000LL), QFileDevice::FileModificationTime));
    modifiedFile.close();
    ExportTargetIdentity modified;
    QCOMPARE(ExportTargetIdentity::capture(target, &modified, &error),
             ExportTargetIdentity::CaptureStatus::Captured);
    QVERIFY(!original.matches(modified));

    const QString displaced = directory.filePath(QStringLiteral("displaced.mp4"));
    QVERIFY(QFile::rename(target, displaced));
    QVERIFY(writeBytes(target, "other!!!"));
    ExportTargetIdentity replaced;
    QCOMPARE(ExportTargetIdentity::capture(target, &replaced, &error),
             ExportTargetIdentity::CaptureStatus::Captured);
    QVERIFY(!original.matches(replaced));

    QVERIFY(QFile::remove(target));
    ExportTargetIdentity missing;
    QCOMPARE(ExportTargetIdentity::capture(target, &missing, &error),
             ExportTargetIdentity::CaptureStatus::Missing);
}

void ExportTests::rejectsChangedExportTargets()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString input = directory.filePath(QStringLiteral("input.mp4"));
    QVERIFY(writeBytes(input, "input"));

    const auto prepareStaged = [&](const QString &target, ExportOutputTransaction &transaction) {
        QCOMPARE(transaction.prepare(target, input, {}, true).status,
                 ExportOutputTransaction::PreparationStatus::Ready);
        QVERIFY(writeBytes(transaction.stagingPath(), "validated staging output"));
    };

    const QString modifiedTarget = directory.filePath(QStringLiteral("modified.mp4"));
    QVERIFY(writeBytes(modifiedTarget, "approved target"));
    {
        ExportOutputTransaction transaction;
        prepareStaged(modifiedTarget, transaction);
        const QByteArray externalBytes("externally modified target with a new size");
        QVERIFY(writeBytes(modifiedTarget, externalBytes));
        QString error;
        QVERIFY(!transaction.commit(&error));
        QVERIFY2(error.contains(QStringLiteral("changed")), qPrintable(error));
        QCOMPARE(readBytes(modifiedTarget), externalBytes);
        QCOMPARE(readBytes(transaction.stagingPath()), QByteArray("validated staging output"));
    }

    const QString replacedTarget = directory.filePath(QStringLiteral("replaced.mp4"));
    const QString displacedTarget = directory.filePath(QStringLiteral("approved-original.mp4"));
    QVERIFY(writeBytes(replacedTarget, "approved target A"));
    {
        ExportOutputTransaction transaction;
        prepareStaged(replacedTarget, transaction);
        QVERIFY(QFile::rename(replacedTarget, displacedTarget));
        const QByteArray replacementBytes("external replacement B");
        QVERIFY(writeBytes(replacedTarget, replacementBytes));
        QString error;
        QVERIFY(!transaction.commit(&error));
        QVERIFY2(error.contains(QStringLiteral("changed")), qPrintable(error));
        QCOMPARE(readBytes(replacedTarget), replacementBytes);
        QCOMPARE(readBytes(displacedTarget), QByteArray("approved target A"));
    }

    const QString disappearedTarget = directory.filePath(QStringLiteral("disappeared.mp4"));
    QVERIFY(writeBytes(disappearedTarget, "approved target"));
    {
        ExportOutputTransaction transaction;
        prepareStaged(disappearedTarget, transaction);
        QVERIFY(QFile::remove(disappearedTarget));
        QString error;
        QVERIFY(!transaction.commit(&error));
        QVERIFY2(error.contains(QStringLiteral("disappeared")), qPrintable(error));
        QVERIFY(!QFileInfo::exists(disappearedTarget));
        QCOMPARE(readBytes(transaction.stagingPath()), QByteArray("validated staging output"));
    }

    const QString appearedTarget = directory.filePath(QStringLiteral("appeared.mp4"));
    {
        ExportOutputTransaction transaction;
        QCOMPARE(transaction.prepare(appearedTarget, input, {}, false).status,
                 ExportOutputTransaction::PreparationStatus::Ready);
        QVERIFY(writeBytes(transaction.stagingPath(), "validated staging output"));
        const QByteArray externalBytes("external newly appeared target");
        QVERIFY(writeBytes(appearedTarget, externalBytes));
        QString error;
        QVERIFY(!transaction.commit(&error));
        QVERIFY2(error.contains(QStringLiteral("appeared")), qPrintable(error));
        QCOMPARE(readBytes(appearedTarget), externalBytes);
    }
}

void ExportTests::rejectsSymlinkExportTargets()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString input = directory.filePath(QStringLiteral("input.mp4"));
    const QString referent = directory.filePath(QStringLiteral("referent.mp4"));
    const QString link = directory.filePath(QStringLiteral("linked-output.mp4"));
    QVERIFY(writeBytes(input, "input"));
    QVERIFY(writeBytes(referent, "referent bytes"));
    if (!QFile::link(referent, link)) {
        QSKIP("The test environment does not permit symbolic-link creation.");
    }
    ExportTargetIdentity linkedIdentity;
    if (ExportTargetIdentity::capture(link, &linkedIdentity)
        != ExportTargetIdentity::CaptureStatus::Link) {
        QSKIP("The platform link API did not create a native symbolic link/reparse point.");
    }
    ExportOutputTransaction transaction;
    const ExportOutputTransaction::PreparationResult prepared = transaction.prepare(
        link, input, {}, true);
    QCOMPARE(prepared.status, ExportOutputTransaction::PreparationStatus::Error);
    QVERIFY2(prepared.error.contains(QStringLiteral("symbolic link"), Qt::CaseInsensitive)
                 || prepared.error.contains(QStringLiteral("reparse point"), Qt::CaseInsensitive),
             qPrintable(prepared.error));
    QCOMPARE(readBytes(referent), QByteArray("referent bytes"));
}

void ExportTests::preservesExistingExportTargetOnFailures_data()
{
    QTest::addColumn<QString>("scenario");
    QTest::addColumn<bool>("overwriteAllowed");
    for (const QString &scenario : {
             QStringLiteral("cancel during Stage A"),
             QStringLiteral("Stage A FFmpeg failure"),
             QStringLiteral("temporary overlay validation failure"),
             QStringLiteral("Stage B failure"),
             QStringLiteral("final validation failure"),
             QStringLiteral("worker termination"),
             QStringLiteral("application shutdown cleanup")}) {
        QTest::newRow(qPrintable(scenario)) << scenario << true;
    }
    QTest::newRow("overwrite denied") << QStringLiteral("overwrite denied") << false;
}

void ExportTests::preservesExistingExportTargetOnFailures()
{
    QFETCH(QString, scenario);
    QFETCH(bool, overwriteAllowed);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QByteArray original("known existing target bytes\0unchanged", 37);
    const QString input = directory.filePath("input.mp4");
    const QString target = directory.filePath("target.mp4");
    QVERIFY(writeBytes(input, "input"));
    QVERIFY(writeBytes(target, original));

    {
        ExportOutputTransaction transaction;
        const auto prepared = transaction.prepare(target, input, {}, overwriteAllowed);
        if (!overwriteAllowed) {
            QCOMPARE(prepared.status,
                     ExportOutputTransaction::PreparationStatus::OverwriteConfirmationRequired);
            QVERIFY(transaction.stagingPath().isEmpty());
        } else {
            QCOMPARE(prepared.status, ExportOutputTransaction::PreparationStatus::Ready);
            QVERIFY(transaction.ownsPath(transaction.stagingPath()));
            QVERIFY(writeBytes(transaction.stagingPath(), "partial staged output"));
            if (scenario != QStringLiteral("application shutdown cleanup")) {
                transaction.cleanup();
            }
        }
    }
    QFile targetFile(target);
    QVERIFY(targetFile.open(QIODevice::ReadOnly));
    QCOMPARE(targetFile.readAll(), original);
    const QStringList artifacts = QDir(directory.path()).entryList(
        {QStringLiteral("*.part.*"), QStringLiteral("*.backup"), QStringLiteral("*.cancel")},
        QDir::Files | QDir::Hidden);
    QVERIFY2(artifacts.isEmpty(), qPrintable(artifacts.join(',')));
}

void ExportTests::commitsNewAndReplacementExports()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString input = directory.filePath("input.mp4");
    QVERIFY(writeBytes(input, "input"));

    const QString newTarget = directory.filePath("new.mp4");
    ExportOutputTransaction newFile;
    QCOMPARE(newFile.prepare(newTarget, input, {}, false).status,
             ExportOutputTransaction::PreparationStatus::Ready);
    QCOMPARE(QFileInfo(newFile.stagingPath()).absolutePath(), QFileInfo(newTarget).absolutePath());
    QVERIFY(writeBytes(newFile.stagingPath(), "new valid output"));
    QString error;
    QVERIFY2(newFile.commit(&error), qPrintable(error));
    QFile newTargetFile(newTarget);
    QVERIFY(newTargetFile.open(QIODevice::ReadOnly));
    QCOMPARE(newTargetFile.readAll(), QByteArray("new valid output"));

    const QString existingTarget = directory.filePath("existing.mp4");
    QVERIFY(writeBytes(existingTarget, "old known target"));
    ExportOutputTransaction replacement;
    QCOMPARE(replacement.prepare(existingTarget, input, {}, true).status,
             ExportOutputTransaction::PreparationStatus::Ready);
    QVERIFY(replacement.targetExistedBeforeExport());
    QVERIFY(writeBytes(replacement.stagingPath(), "replacement output"));
    QVERIFY2(replacement.commit(&error), qPrintable(error));
    QFile replacementFile(existingTarget);
    QVERIFY(replacementFile.open(QIODevice::ReadOnly));
    QCOMPARE(replacementFile.readAll(), QByteArray("replacement output"));
    const QStringList artifacts = QDir(directory.path()).entryList(
        {QStringLiteral("*.part.*"), QStringLiteral("*.backup"), QStringLiteral("*.cancel")},
        QDir::Files | QDir::Hidden);
    QVERIFY2(artifacts.isEmpty(), qPrintable(artifacts.join(',')));
}

void ExportTests::protectsEveryDaySourceFromExport()
{
    // KAN-76: exporting the active run of an analysed day must never
    // replace a recording of any run, an alternative source, the video or
    // the project -- by its own path or an alias (a symlinked folder, a hard
    // link, different letter case) -- even with overwrite consent. An
    // unrelated existing file still needs consent.
    const QString ffmpeg = FfmpegTools::ffmpegPath();
    if (ffmpeg.isEmpty()) QSKIP("FFmpeg is unavailable for the export-protection test.");
    QSettings settings; settings.clear(); settings.sync();
    QTemporaryDir directory; QVERIFY(directory.isValid());
    const QString video = directory.filePath("clip.mp4");
    QProcess encoder;
    encoder.start(ffmpeg, {"-hide_banner", "-loglevel", "error", "-y", "-f", "lavfi", "-i",
                           "color=c=black:s=32x32:r=30:d=4", "-c:v", "mpeg4", "-q:v", "3", video});
    QVERIFY2(encoder.waitForFinished(30'000) && encoder.exitCode() == 0, encoder.readAllStandardError().constData());
    const QString first = directory.filePath("first.vbo"), second = directory.filePath("second.vbo"),
                  alternative = directory.filePath("second-alternative.vbo");
    QVERIFY(writeBytes(first, warpedRouteVbo(true)));
    QVERIFY(writeBytes(second, warpedRouteVbo(false)));
    QVERIFY(writeBytes(alternative, warpedRouteVbo(false, 200)));

    AppController controller(nullptr, directory.filePath("recovery.json"));
    // Two runs; the second carries an alternative recording.
    QVERIFY(controller.m_document.beginBatchImport({QUrl::fromLocalFile(first), QUrl::fromLocalFile(second), QUrl::fromLocalFile(alternative)}));
    QTRY_COMPARE_WITH_TIMEOUT(controller.m_document.batchImportState(), QString("review"), 20000);
    QHash<QString, QString> proposals;
    for (const auto &value : controller.m_document.batchImportRows())
        proposals.insert(QFileInfo(value.toMap().value("path").toString()).fileName(), value.toMap().value("proposalId").toString());
    QCOMPARE(proposals.size(), 3);
    QSignalSpy committed(&controller.m_document, &DocumentController::batchImportCommitted);
    QVERIFY(controller.m_document.confirmBatchImport("Protected day", false, {
        QVariantMap{{"proposalId", proposals.value("first.vbo")}, {"groupId", proposals.value("first.vbo")}},
        QVariantMap{{"proposalId", proposals.value("second.vbo")}, {"groupId", proposals.value("second.vbo")}},
        QVariantMap{{"proposalId", proposals.value("second-alternative.vbo")}, {"groupId", proposals.value("second.vbo")}}}));
    QTRY_COMPARE_WITH_TIMEOUT(committed.size(), 1, 20000);
    QCOMPARE(controller.eventRuns().size(), 2);
    QTRY_COMPARE_WITH_TIMEOUT(controller.vboLoadState(), QString("ready"), 20000);
    controller.loadVideo(QUrl::fromLocalFile(video));
    QTRY_COMPARE_WITH_TIMEOUT(controller.videoLoadState(), QString("ready"), 20000);

    const QString project = directory.filePath("day.fetproject");
    QVERIFY(controller.saveProject(QUrl::fromLocalFile(project)));
    const QString activeTelemetry = QFileInfo(controller.m_telemetryPath).fileName();
    QVERIFY(activeTelemetry == "first.vbo" || activeTelemetry == "second.vbo");

    const auto exportTo = [&](const QString &target) {
        return controller.startExport(QUrl::fromLocalFile(target), 32, 32, 30, 1, 8'000'000, false, false, {}, {}, true);
    };
    const auto refused = [&](const QString &target, const QString &protectedFile) {
        const QByteArray before = readBytes(protectedFile);
        if (before.isEmpty()) return QString("could not read ") + protectedFile;
        if (exportTo(target)) { controller.exporter()->cancel(); return QString("export started onto ") + target; }
        if (controller.exporter()->state() != "failed") return QString("state ") + controller.exporter()->state() + " for " + target;
        if (readBytes(protectedFile) != before) return QString("changed ") + protectedFile;
        return QString();
    };
    // Every source of every run, the video and the project, by their own paths.
    for (const QString &path : {first, second, alternative, video, project}) {
        const auto failure = refused(path, path);
        QVERIFY2(failure.isEmpty(), qPrintable(failure));
    }
#ifdef Q_OS_UNIX
    // Through a symlinked folder.
    const QString alias = directory.filePath("alias");
    QVERIFY(QFile::link(directory.path(), alias));
    for (const QString name : {"first.vbo", "second-alternative.vbo", "day.fetproject"}) {
        const auto failure = refused(QDir(alias).filePath(name), directory.filePath(name));
        QVERIFY2(failure.isEmpty(), qPrintable(failure));
    }
    // A hard link: another name for the same file.
    const QString hardLink = directory.filePath("hard-link.vbo");
    QCOMPARE(::link(QFile::encodeName(second).constData(), QFile::encodeName(hardLink).constData()), 0);
    {
        const auto failure = refused(hardLink, second);
        QVERIFY2(failure.isEmpty(), qPrintable(failure));
        QVERIFY(controller.exporter()->error().contains("hard link"));
    }
#endif
    // Different letter case, where the file system ignores case (APFS default).
    const QString upper = directory.filePath("SECOND-ALTERNATIVE.VBO");
    if (QFileInfo::exists(upper)) {
        const auto failure = refused(upper, alternative);
        QVERIFY2(failure.isEmpty(), qPrintable(failure));
    }
    // An unrelated existing file is not a source, but needs overwrite consent.
    const QString unrelated = directory.filePath("previous-export.mp4");
    QVERIFY(writeBytes(unrelated, "earlier export"));
    QVERIFY(!controller.startExport(QUrl::fromLocalFile(unrelated), 32, 32, 30, 1, 8'000'000, false, false, {}, {}, false));
    QCOMPARE(controller.exporter()->state(), QString("overwriteConfirmationRequired"));
    QCOMPARE(readBytes(unrelated), QByteArray("earlier export"));
}

void ExportTests::keepsOutputSafeWhenTheDestinationFills_data()
{
    QTest::addColumn<QString>("stage");
    QTest::newRow("full before export (preflight)") << QStringLiteral("preflight");
    QTest::newRow("fills during Stage A (overlay)") << QStringLiteral("renderingOverlay");
    QTest::newRow("fills during Stage B (final encode)") << QStringLiteral("encodingVideo");
    QTest::newRow("cancelled during Stage B") << QStringLiteral("cancel");
    QTest::newRow("full at final publication") << QStringLiteral("publication");
}

void ExportTests::keepsOutputSafeWhenTheDestinationFills()
{
    // KAN-75 (C07): a real, small volume (a disk image) that runs out of
    // space before or during the export. The worker must fail within a
    // bounded time, the existing target and unrelated files on the volume
    // stay intact, and cleanup removes only transaction-owned artifacts.
    // Stage A's overlay lives on the volume in the overlay case, Stage B's
    // staging output in the other two.
#ifndef Q_OS_MACOS
    QSKIP("The filling-volume test uses hdiutil (macOS).");
#else
    QFETCH(QString, stage);
    const QString ffmpeg = FfmpegTools::ffmpegPath();
    if (ffmpeg.isEmpty()) QSKIP("FFmpeg is unavailable for the filling-destination test.");
    QTemporaryDir directory; QVERIFY(directory.isValid());
    const QString image = directory.filePath("volume.sparseimage"), mount = directory.filePath("volume");
    QVERIFY(QDir().mkpath(mount));
    const auto hdiutil = [](const QStringList &arguments) {
        QProcess process;
        process.start(QStringLiteral("/usr/bin/hdiutil"), arguments);
        return process.waitForFinished(60'000) && process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
    };
    // Sparse, so it costs real disk only as it fills. Export preflight keeps
    // a 2 GiB reserve, so the volume must be larger than that to start.
    if (!hdiutil({"create", "-size", "2560m", "-type", "SPARSE", "-fs", "HFS+", "-volname", "FlappedEarFill", "-o", image})
        || !hdiutil({"attach", "-nobrowse", "-noautoopen", "-mountpoint", mount, image}))
        QSKIP("Could not create or attach a disk image here.");
    const auto detach = qScopeGuard([&] { hdiutil({"detach", mount, "-force"}); });

    const QString source = directory.filePath("synthetic.rcz"), video = directory.filePath("input.mov");
    QVERIFY(writeBytes(source, RczFixture::zip(RczFixture::members())));
    QProcess encoder;
    encoder.start(ffmpeg, {"-hide_banner", "-loglevel", "error", "-y", "-f", "lavfi", "-i", "testsrc2=s=640x360:r=30:d=20",
                           "-c:v", "libx264", "-pix_fmt", "yuv420p", video});
    QVERIFY(encoder.waitForFinished(30'000) && encoder.exitCode() == 0);

    const bool overlayOnVolume = stage == "renderingOverlay";
    const bool cancelling = stage == "cancel";
    const bool publishing = stage == "publication";
    const QString cancelPath = directory.filePath("cancel");
    // Stage A writes its overlay to the temporary directory: put that on the volume.
    const QByteArray previousTemp = qgetenv("TMPDIR");
    if (overlayOnVolume) qputenv("TMPDIR", QFile::encodeName(mount));
    const auto restoreTemp = qScopeGuard([&] {
        if (previousTemp.isEmpty()) qunsetenv("TMPDIR"); else qputenv("TMPDIR", previousTemp);
    });
    const QString outputDirectory = overlayOnVolume ? directory.filePath("out") : mount;
    QVERIFY(QDir().mkpath(outputDirectory));
    const QString output = QDir(outputDirectory).filePath("export.mp4");
    const QString bystander = QDir(mount).filePath("bystander.mp4");
    QVERIFY(writeBytes(output, "previous export"));
    QVERIFY(writeBytes(bystander, "unrelated file"));
    auto transaction = std::make_unique<ExportOutputTransaction>();
    QCOMPARE(transaction->prepare(output, video, {source}, true).status, ExportOutputTransaction::PreparationStatus::Ready);
    const auto id = transaction->transactionId();
    const QString overlay = QDir::temp().filePath(QStringLiteral("flappedear-overlay-%1.mkv").arg(id));
    const auto manifestPath = ExportArtifactManifest::manifestPathFor(id);
    QString error;
    QVERIFY2(ExportArtifactManifest::create({id, QDateTime::currentMSecsSinceEpoch(), overlay,
        transaction->stagingPath(), output, QCoreApplication::applicationPid(), "preparing"}, &error), qPrintable(error));
    auto manifestCleanup = qScopeGuard([&] { static_cast<void>(ExportArtifactManifest::cleanupOwned(manifestPath)); });

    // Fills the volume at once: reserve every free block for a filler file
    // (F_PREALLOCATE, no data written), then write until the file system refuses.
    const QString filler = QDir(mount).filePath("filler.bin");
    const auto fill = [&filler, &mount] {
        QFile file(filler);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Append)) return;
        const qint64 available = QStorageInfo(mount).bytesAvailable();
        fstore_t store{F_ALLOCATEALL, F_PEOFPOSMODE, 0, static_cast<off_t>(available), 0};
        if (fcntl(file.handle(), F_PREALLOCATE, &store) == 0) static_cast<void>(ftruncate(file.handle(), store.fst_bytesalloc));
        file.seek(file.size());
        const QByteArray small(4096, '\0');
        while (file.write(small) == small.size() && file.flush()) {}
    };
    if (stage == "preflight") fill();

    WidgetModel widgets;
    const QString config = directory.filePath("worker.json");
    QVERIFY(writeBytes(config, QJsonDocument(QJsonObject{{"vboPath", source}, {"inputPath", video},
        {"outputPath", transaction->stagingPath()}, {"manifestPath", manifestPath}, {"temporaryOverlayPath", overlay},
        {"widgets", widgets.toJson()}, {"sync", QJsonObject{{"offset", .1}, {"timeScale", 1.0}}},
        {"firstFrame", 0}, {"lastFrame", 599}, {"audioEnabled", false}, {"encoder", "libx265"},
        {"cancelPath", cancelPath}}).toJson()));
    QProcess worker;
    QByteArray events;
    bool filled = stage == "preflight" || publishing;
    QElapsedTimer sinceCancel;
    const QByteArray trigger = QByteArray("\"state\":\"") + (cancelling ? QByteArray("encodingVideo") : stage.toUtf8()) + '"';
    QObject::connect(&worker, &QProcess::readyReadStandardOutput, &worker, [&] {
        const auto chunk = worker.readAllStandardOutput();
        events += chunk;
        if (filled || !chunk.contains(trigger)) return;
        filled = true;
        if (!cancelling) { fill(); return; }
        sinceCancel.start();
        static_cast<void>(writeBytes(cancelPath, "cancel"));
    });
    QElapsedTimer elapsed; elapsed.start();
    worker.start(QStringLiteral(FLAPPEDEAR_NATIVE_PATH), {"--export-worker", config});
    QVERIFY(worker.waitForStarted());
    QTRY_VERIFY_WITH_TIMEOUT(worker.state() == QProcess::NotRunning, 90'000);
    events += worker.readAllStandardOutput() + worker.readAllStandardError();
    QByteArray reason;
    for (const auto &line : events.split('\n'))
        if (line.contains("\"state\":\"failed\"") || line.contains("\"passed\":false")) reason = line;
    qInfo().noquote() << stage << "worker ended after" << elapsed.elapsed() << "ms" << reason;
    QVERIFY2(filled, "the export never reached the stage that should fill the volume");
    if (publishing) {
        // The export completed and validated; the volume fills before the
        // atomic replace. Publication either succeeds with the new, valid
        // file or fails leaving the previous export exactly as it was.
        QVERIFY2(worker.exitStatus() == QProcess::NormalExit && worker.exitCode() == 0, events.right(2000).constData());
        fill();
        QVERIFY(QStorageInfo(mount).bytesAvailable() < 1024 * 1024);
        const bool committed = transaction->commit(&error);
        qInfo().noquote() << "publication on a full volume" << (committed ? "succeeded" : "failed: " + error);
        if (committed) {
            QCOMPARE(MediaProbe::probe(output, {}, true).videoFrameCount, 600);
            QCOMPARE(readBytes(bystander), QByteArray("unrelated file"));
            QVERIFY(QFile::remove(filler));
            return;
        }
        QCOMPARE(readBytes(output), QByteArray("previous export"));
    } else if (cancelling) {
        qInfo().noquote() << "cancel honoured after" << sinceCancel.elapsed() << "ms";
        QVERIFY2(events.contains("\"state\":\"cancelled\""), events.right(2000).constData());
        QVERIFY2(sinceCancel.elapsed() < 10'000, "cancellation must be bounded");
    } else {
        // The worker fails; it never reports a finished export on a full volume.
        QVERIFY2(worker.exitStatus() != QProcess::NormalExit || worker.exitCode() != 0, events.right(2000).constData());
        QVERIFY2(elapsed.elapsed() < 60'000, "failure on a full volume must be bounded");
        // It failed where intended: refused before writing, or out of space mid-stage.
        const QString userError = QJsonDocument::fromJson(reason).object().value("error").toString();
        if (stage == "preflight") QVERIFY2(userError.contains("Required estimate"), reason.constData());
        else {
            QVERIFY2(!userError.contains("Required estimate") && !userError.isEmpty(), reason.constData());
            // The user is told the disk is full, not an FFmpeg pipe error.
            QVERIFY2(userError.contains("ran out of space") || userError.contains("fell below"), qPrintable(userError));
        }
    }
    // The controller's failure path: remove what the manifest owns, drop the transaction.
    QVERIFY2(ExportArtifactManifest::cleanupOwned(manifestPath, &error), qPrintable(error));
    manifestCleanup.dismiss();
    const QString staging = transaction->stagingPath();
    transaction.reset(); // as the controller drops it
    QCOMPARE(readBytes(output), QByteArray("previous export"));
    QCOMPARE(readBytes(bystander), QByteArray("unrelated file"));
    QVERIFY2(!QFileInfo::exists(staging), qPrintable(staging));
    QVERIFY2(!QFileInfo::exists(overlay), qPrintable(overlay));
    QStringList left = QDir(mount).entryList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot);
    left.removeAll("filler.bin"); left.removeAll("bystander.mp4"); left.removeAll("export.mp4");
    left.removeIf([](const QString &name) { return name.startsWith("._") || name == ".DS_Store"; });
    QVERIFY2(left.isEmpty(), qPrintable(left.join(", ")));
#endif
}

void ExportTests::describesOutOfSpaceExportFailures()
{
    const auto full = describeExportFailure(QStringLiteral("FFmpeg composition failed."),
        QStringLiteral("[out#0/mp4] Could not write header: No space left on device"));
    QVERIFY(full.error.contains("ran out of space"));
    QVERIFY(full.diagnostics.startsWith("Original error: FFmpeg composition failed."));
    QVERIFY(full.diagnostics.contains("No space left on device"));
    const auto other = describeExportFailure(QStringLiteral("FFmpeg composition failed."), QStringLiteral("Invalid argument"));
    QCOMPARE(other.error, QStringLiteral("FFmpeg composition failed."));
    QCOMPARE(other.diagnostics, QStringLiteral("Invalid argument"));
}

void ExportTests::rendersTelemetryAtExplicitTime()
{
    const TelemetrySession session = VboParser::parse(
        u"[column names]\ntime speed\n[data]\n0 0\n10 100");
    TelemetryRenderContext context;
    context.setSession(&session);
    context.setSyncTransform({2.0, 1.5});
    context.setTime(4.0);
    QCOMPARE(context.telemetryTime().toDouble(), 8.0);
    QCOMPARE(context.telemetryValue("speed").toDouble(), 80.0);
    context.setTime(2.0);
    QCOMPARE(context.telemetryValue("speed").toDouble(), 50.0);
}

void ExportTests::evaluatesIndependentExportStorageVolumes()
{
    const ExportStorageEstimate estimate{100, 50, 25};
    const auto provider = [](const QString &path) {
        return path.startsWith(QStringLiteral("/temp"))
            ? ExportFilesystemInfo{QStringLiteral("/temporary"), path, path, 130, 1'000}
            : ExportFilesystemInfo{QStringLiteral("/destination"), path, path, 80, 1'000};
    };
    const ExportStoragePreflight enough = ExportStoragePolicy::evaluate("/temp/overlay", "/output/final", estimate, provider);
    QVERIFY(enough.sufficient);
    const auto insufficientTemp = [](const QString &path) {
        return path.startsWith(QStringLiteral("/temp"))
            ? ExportFilesystemInfo{QStringLiteral("/temporary"), path, path, 124, 1'000}
            : ExportFilesystemInfo{QStringLiteral("/destination"), path, path, 80, 1'000};
    };
    QVERIFY(!ExportStoragePolicy::evaluate("/temp/overlay", "/output/final", estimate, insufficientTemp).sufficient);
    const auto insufficientOutput = [](const QString &path) {
        return path.startsWith(QStringLiteral("/temp"))
            ? ExportFilesystemInfo{QStringLiteral("/temporary"), path, path, 130, 1'000}
            : ExportFilesystemInfo{QStringLiteral("/destination"), path, path, 74, 1'000};
    };
    QVERIFY(!ExportStoragePolicy::evaluate("/temp/overlay", "/output/final", estimate, insufficientOutput).sufficient);
    const auto sharedFilesystem = [](const QString &path) {
        return ExportFilesystemInfo{QStringLiteral("/shared"), path, path, 174, 1'000};
    };
    QVERIFY(!ExportStoragePolicy::evaluate("/temp/overlay", "/output/final", estimate, sharedFilesystem).sufficient);
}

void ExportTests::resolvesExportFilesystemsForFutureArtifacts()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString existingFile = directory.filePath(QStringLiteral("existing.mkv"));
    QVERIFY(writeBytes(existingFile, "fixture"));

    const auto verifyUsable = [](const ExportFilesystemInfo &filesystem, const QString &requested) {
        QVERIFY2(filesystem.isUsable(), qPrintable(requested));
        QCOMPARE(filesystem.inspectedPath, requested);
        QVERIFY(QFileInfo::exists(filesystem.probePath));
        QVERIFY(filesystem.availableBytes > 0);
        QVERIFY(filesystem.totalBytes > 0);
    };

    const ExportFilesystemInfo fileFilesystem = ExportStoragePolicy::filesystemForPath(existingFile);
    verifyUsable(fileFilesystem, existingFile);
    QCOMPARE(fileFilesystem.probePath, QFileInfo(existingFile).absoluteFilePath());

    const ExportFilesystemInfo directoryFilesystem = ExportStoragePolicy::filesystemForPath(directory.path());
    verifyUsable(directoryFilesystem, directory.path());
    QCOMPARE(directoryFilesystem.probePath, QFileInfo(directory.path()).absoluteFilePath());

    const QString futureFile = directory.filePath(QStringLiteral("future.mkv"));
    const ExportFilesystemInfo futureFilesystem = ExportStoragePolicy::filesystemForPath(futureFile);
    verifyUsable(futureFilesystem, futureFile);
    QCOMPARE(futureFilesystem.probePath, QFileInfo(directory.path()).absoluteFilePath());

    const QString nestedFuture = directory.filePath(QStringLiteral("a/b/c/output.mkv"));
    const ExportFilesystemInfo nestedFilesystem = ExportStoragePolicy::filesystemForPath(nestedFuture);
    verifyUsable(nestedFilesystem, nestedFuture);
    QCOMPARE(nestedFilesystem.probePath, QFileInfo(directory.path()).absoluteFilePath());

    const ExportFilesystemInfo unresolved = ExportStoragePolicy::filesystemForPath(QString());
    QVERIFY(!unresolved.isUsable());
    QCOMPARE(unresolved.inspectedPath, QString());
    QCOMPARE(ExportStoragePolicy::bytesText(unresolved.availableBytes), QStringLiteral("unavailable"));
}

void ExportTests::estimatesTemporaryStorageFromRepresentativeSample()
{
    constexpr qsizetype expectedFrames = 7'193;
    // 513,343,525 bytes was the observed complete 4K FFV1 overlay size.
    constexpr qint64 representativeBytesPerFrame = 71'368;
    const ExportStorageEstimate measured = ExportStoragePolicy::estimateFromSample(
        representativeBytesPerFrame * 24, 24, expectedFrames, 120.0, 12'000'000);
    QCOMPARE(measured.basis, ExportStorageEstimate::Basis::MeasuredSample);
    QCOMPARE(measured.sampleFrames, qsizetype(24));
    QCOMPARE(measured.bytesPerFrame, representativeBytesPerFrame);
    QCOMPARE(measured.safetyMargin, 1.5);
    QVERIFY(measured.temporaryOverlayBytes > 700LL * 1024 * 1024);
    QVERIFY2(measured.temporaryOverlayBytes < 2LL * 1024 * 1024 * 1024,
             "Measured representative sample must not regress to a tens-of-GiB estimate.");

    const ExportStorageEstimate fallback = ExportStoragePolicy::estimate(
        expectedFrames, {3840, 2160}, 120.0, 12'000'000);
    QCOMPARE(fallback.basis, ExportStorageEstimate::Basis::ConservativeFallback);
    QCOMPARE(fallback.bytesPerFrame, 512LL * 1024LL);
    QCOMPARE(fallback.safetyMargin, 1.75);
    QVERIFY(fallback.temporaryOverlayBytes > measured.temporaryOverlayBytes);
    QVERIFY(fallback.temporaryOverlayBytes < 10LL * 1024 * 1024 * 1024);

    const ExportStorageEstimate overflow = ExportStoragePolicy::estimateFromSample(
        std::numeric_limits<qint64>::max(), 1, std::numeric_limits<qsizetype>::max(),
        1.0, 12'000'000);
    QCOMPARE(overflow.temporaryOverlayBytes, std::numeric_limits<qint64>::max());
}

void ExportTests::streamsRawFramesToSlowConsumer()
{
    QProcess consumer;
    consumer.start(QStringLiteral(RAW_TRANSPORT_CONSUMER_PATH), {QStringLiteral("slow")});
    QVERIFY2(consumer.waitForStarted(5'000), qPrintable(consumer.errorString()));
    const QByteArray fullHdFrame(1920 * 1080 * 4, 'x');
    const QByteArray ultraHdFrame(3840 * 2160 * 4, 'y');
    RawFrameTransport transport(consumer);
    const RawFrameTransportResult fullHdResult = transport.writeFrame(fullHdFrame);
    QVERIFY2(fullHdResult.succeeded(), qPrintable(fullHdResult.error));
    const RawFrameTransportResult ultraHdResult = transport.writeFrame(ultraHdFrame);
    QVERIFY2(ultraHdResult.succeeded(), qPrintable(ultraHdResult.error));
    consumer.closeWriteChannel();
    QVERIFY2(consumer.waitForFinished(15'000), qPrintable(consumer.errorString()));
    QCOMPARE(consumer.exitStatus(), QProcess::NormalExit);
    QCOMPARE(consumer.exitCode(), 0);
    QCOMPARE(consumer.readAllStandardOutput().trimmed().toLongLong(),
             qint64(fullHdFrame.size() + ultraHdFrame.size()));
    const RawFrameTransportConfig config;
    QVERIFY(ultraHdResult.maximumQueuedBytes <= config.highWaterBytes + config.writeChunkBytes);
}

void ExportTests::failsRawFrameTransportWhenConsumerExits()
{
    QProcess consumer;
    consumer.start(QStringLiteral(RAW_TRANSPORT_CONSUMER_PATH), {QStringLiteral("early-exit")});
    QVERIFY2(consumer.waitForStarted(5'000), qPrintable(consumer.errorString()));
    const auto stopConsumer = qScopeGuard([&] {
        if (consumer.state() != QProcess::NotRunning) {
            consumer.kill();
            static_cast<void>(consumer.waitForFinished(5'000));
        }
    });
    const QByteArray frame(32 * 1024 * 1024, 'x');
    RawFrameTransport transport(consumer);
    const RawFrameTransportResult result = transport.writeFrame(frame);
    QVERIFY(!result.succeeded());
    QCOMPARE(result.status, RawFrameTransportResult::Status::Failure);
    QVERIFY2(result.error.contains(QStringLiteral("exited"), Qt::CaseInsensitive)
                 || result.error.contains(QStringLiteral("write failed"), Qt::CaseInsensitive)
                 || result.error.contains(QStringLiteral("device error"), Qt::CaseInsensitive),
             qPrintable(result.error));
}

void ExportTests::timesOutStalledRawFrameTransport()
{
    QProcess consumer;
    consumer.start(QStringLiteral(RAW_TRANSPORT_CONSUMER_PATH), {QStringLiteral("stall")});
    QVERIFY2(consumer.waitForStarted(5'000), qPrintable(consumer.errorString()));
    RawFrameTransportConfig config;
    config.stallTimeoutMilliseconds = 700;
    const QByteArray frame(32 * 1024 * 1024, 'x');
    RawFrameTransport transport(consumer, config);
    QElapsedTimer elapsed;
    elapsed.start();
    const RawFrameTransportResult result = transport.writeFrame(frame);
    QVERIFY(!result.succeeded());
    QVERIFY2(result.error.contains(QStringLiteral("stalled")), qPrintable(result.error));
    QVERIFY(elapsed.elapsed() >= config.stallTimeoutMilliseconds);
    QVERIFY(elapsed.elapsed() < 5'000);
    consumer.kill();
    QVERIFY(consumer.waitForFinished(5'000));
}

void ExportTests::cancelsBlockedRawFrameTransportPromptly()
{
    QProcess consumer;
    consumer.start(QStringLiteral(RAW_TRANSPORT_CONSUMER_PATH), {QStringLiteral("stall")});
    QVERIFY2(consumer.waitForStarted(5'000), qPrintable(consumer.errorString()));
    std::atomic_bool cancelled = false;
    RawFrameTransport transport(consumer, {}, [&cancelled] { return cancelled.load(); });
    std::thread canceller([&cancelled] {
        QThread::msleep(300);
        cancelled.store(true);
    });
    const auto joinCanceller = qScopeGuard([&] { canceller.join(); });
    const QByteArray frame(32 * 1024 * 1024, 'x');
    QElapsedTimer elapsed;
    elapsed.start();
    const RawFrameTransportResult result = transport.writeFrame(frame);
    QCOMPARE(result.status, RawFrameTransportResult::Status::Cancelled);
    QVERIFY(elapsed.elapsed() < 2'000);
    consumer.kill();
    QVERIFY(consumer.waitForFinished(5'000));
}

void ExportTests::cleansOnlyManifestOwnedArtifacts()
{
    QTemporaryDir destination;
    QVERIFY(destination.isValid());
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString overlay = QDir::temp().filePath(QStringLiteral("flappedear-overlay-%1.mkv").arg(id));
    const QString staging = destination.filePath(QStringLiteral(".result.flappedear-%1.part.mp4").arg(id));
    const QString target = destination.filePath("result.mp4");
    const QString unrelated = destination.filePath("unrelated.mkv");
    QVERIFY(writeBytes(overlay, "overlay"));
    QVERIFY(writeBytes(staging, "staging"));
    QVERIFY(writeBytes(unrelated, "keep"));
    const ExportArtifactManifestData manifest{id, QDateTime::currentMSecsSinceEpoch(), overlay, staging, target, 0, "stageA"};
    QString error;
    QVERIFY2(ExportArtifactManifest::create(manifest, &error), qPrintable(error));
    QVERIFY2(ExportArtifactManifest::cleanupOwned(ExportArtifactManifest::manifestPathFor(id), &error), qPrintable(error));
    QVERIFY2(ExportArtifactManifest::cleanupOwned(ExportArtifactManifest::manifestPathFor(id), &error), qPrintable(error));
    QVERIFY(!QFileInfo::exists(overlay));
    QVERIFY(!QFileInfo::exists(staging));
    QVERIFY(QFileInfo::exists(unrelated));

    const QString malformed = QDir::temp().filePath(QStringLiteral("flappedear-export-%1.manifest.json")
        .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    QVERIFY(writeBytes(malformed, "not json"));
    QVERIFY(!ExportArtifactManifest::cleanupOwned(malformed, &error));
    QVERIFY(QFile::remove(malformed));
}

void ExportTests::preservesLiveManifestForStartupRecovery()
{
    QTemporaryDir destination;
    QVERIFY(destination.isValid());
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString overlay = QDir::temp().filePath(QStringLiteral("flappedear-overlay-%1.mkv").arg(id));
    const QString staging = destination.filePath(QStringLiteral(".result.flappedear-%1.part.mp4").arg(id));
    QVERIFY(writeBytes(overlay, "overlay"));
    QVERIFY(writeBytes(staging, "staging"));
    const ExportArtifactManifestData manifest{id, QDateTime::currentMSecsSinceEpoch(), overlay, staging,
        destination.filePath("result.mp4"), QCoreApplication::applicationPid(), "stageB"};
    QString error;
    QVERIFY2(ExportArtifactManifest::create(manifest, &error), qPrintable(error));
    const QString manifestPath = ExportArtifactManifest::manifestPathFor(id);
    const QStringList recovered = ExportArtifactManifest::recoverStale();
    QVERIFY(!recovered.contains(manifestPath));
    QVERIFY(QFileInfo::exists(overlay));
    QVERIFY2(ExportArtifactManifest::cleanupOwned(manifestPath, &error), qPrintable(error));
}

void ExportTests::recoversManifestsFromDataAndLegacyFolders()
{
    // KAN-164: new manifests live in the application data folder, which the
    // system does not purge; manifests left in the temporary folder by older
    // versions are still recovered.
    QTemporaryDir destination;
    QVERIFY(destination.isValid());
    const auto leaveCrashedExport = [&destination](const QString &id, ExportArtifactManifestData *manifest) {
        const QString overlay = QDir::temp().filePath(QStringLiteral("flappedear-overlay-%1.mkv").arg(id));
        const QString staging = destination.filePath(QStringLiteral(".result.flappedear-%1.part.mp4").arg(id));
        *manifest = {id, QDateTime::currentMSecsSinceEpoch(), overlay, staging,
                     destination.filePath("result.mp4"), 0, "stageA"};
        return writeBytes(overlay, "overlay") && writeBytes(staging, "staging");
    };
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    ExportArtifactManifestData manifest;
    QVERIFY(leaveCrashedExport(id, &manifest));
    QString error;
    QVERIFY2(ExportArtifactManifest::create(manifest, &error), qPrintable(error));
    const QString manifestPath = ExportArtifactManifest::manifestPathFor(id);
    QVERIFY(QFileInfo::exists(manifestPath));
    QCOMPARE(QFileInfo(manifestPath).absolutePath(), QDir(ExportArtifactManifest::manifestDirectory()).absolutePath());
    QVERIFY(QFileInfo(manifestPath).absolutePath() != QDir(QDir::tempPath()).absolutePath());
    QVERIFY(!QFileInfo::exists(ExportArtifactManifest::legacyManifestPathFor(id)));

    const QString legacyId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    ExportArtifactManifestData legacy;
    QVERIFY(leaveCrashedExport(legacyId, &legacy));
    const QString legacyPath = ExportArtifactManifest::legacyManifestPathFor(legacyId);
    QVERIFY2(ExportArtifactManifest::update(legacyPath, legacy, &error), qPrintable(error));

    const QStringList recovered = ExportArtifactManifest::recoverStale();
    QVERIFY(recovered.contains(manifestPath));
    QVERIFY(recovered.contains(legacyPath));
    for (const auto &data : {manifest, legacy}) {
        QVERIFY(!QFileInfo::exists(data.temporaryOverlayPath));
        QVERIFY(!QFileInfo::exists(data.outputStagingPath));
    }
    QVERIFY(!QFileInfo::exists(manifestPath) && !QFileInfo::exists(legacyPath));

    // The worker is told the manifest path; it may resolve the data folder
    // differently, so the path is accepted wherever it is, by name and content.
    const QString otherFolderId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    ExportArtifactManifestData elsewhere;
    QVERIFY(leaveCrashedExport(otherFolderId, &elsewhere));
    const QString elsewherePath = destination.filePath(QStringLiteral("flappedear-export-%1.manifest.json").arg(otherFolderId));
    QVERIFY2(ExportArtifactManifest::update(elsewherePath, elsewhere, &error), qPrintable(error));
    QVERIFY(ExportArtifactManifest::read(elsewherePath, nullptr, &error));
    QVERIFY(!ExportArtifactManifest::update(destination.filePath("renamed.manifest.json"), elsewhere, &error));
    QVERIFY2(ExportArtifactManifest::cleanupOwned(elsewherePath, &error), qPrintable(error));
}

void ExportTests::keepsStaleExportArtifactsFromCommandLineRuns()
{
    // KAN-156: only the application, holding its session lock, cleans stale
    // export artifacts. A command-line run must not delete them: another
    // export may own them before its worker PID is recorded.
    QTemporaryDir destination;
    QVERIFY(destination.isValid());
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString overlay = QDir::temp().filePath(QStringLiteral("flappedear-overlay-%1.mkv").arg(id));
    const QString staging = destination.filePath(QStringLiteral(".result.flappedear-%1.part.mp4").arg(id));
    QVERIFY(writeBytes(overlay, "overlay"));
    QVERIFY(writeBytes(staging, "staging"));
    const ExportArtifactManifestData manifest{id, QDateTime::currentMSecsSinceEpoch(), overlay, staging,
        destination.filePath("result.mp4"), 0, "stageA"};
    QString error;
    QVERIFY2(ExportArtifactManifest::create(manifest, &error), qPrintable(error));
    const QString manifestPath = ExportArtifactManifest::manifestPathFor(id);

    QProcess cli;
    cli.start(QStringLiteral(FLAPPEDEAR_NATIVE_PATH), {QStringLiteral("--render-visual-smoke"), destination.filePath("smoke.png")});
    QVERIFY(cli.waitForFinished(60'000));
    QVERIFY2(QFileInfo::exists(manifestPath) && QFileInfo::exists(overlay) && QFileInfo::exists(staging),
             "a command-line run deleted export artifacts it does not own");

    // The application's janitor (here in-process) still recovers them.
    QVERIFY(ExportArtifactManifest::recoverStale().contains(manifestPath));
    QVERIFY(!QFileInfo::exists(overlay) && !QFileInfo::exists(staging));
}

void ExportTests::noticesWhenTheParentProcessExits()
{
    // KAN-156: an export worker stops when the application that started it
    // is gone. A grandchild watches its parent, which then exits.
#ifdef Q_OS_UNIX
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QByteArray report = directory.filePath("report").toLocal8Bit();
    const pid_t child = ::fork();
    QVERIFY(child >= 0);
    if (child == 0) {
        int ready[2];
        if (::pipe(ready) != 0) ::_exit(2);
        const pid_t grandchild = ::fork();
        if (grandchild == 0) {
            ::close(ready[0]);
            const ParentProcessWatch watch;
            const bool before = watch.parentExited();
            ::write(ready[1], "1", 1);
            ::close(ready[1]);
            bool after = false;
            for (int poll = 0; poll < 500 && !after; ++poll) { after = watch.parentExited(); ::usleep(10'000); }
            if (FILE *file = std::fopen(report.constData(), "w")) {
                std::fprintf(file, "%d %d", before ? 1 : 0, after ? 1 : 0);
                std::fclose(file);
            }
            ::_exit(0);
        }
        ::close(ready[1]);
        char byte = 0;
        static_cast<void>(::read(ready[0], &byte, 1)); // the watch exists, then this parent exits
        ::_exit(0);
    }
    // Qt's own child handling may already have reaped the child (ECHILD):
    // either way it has exited, and the grandchild's report decides. Its
    // SIGCHLD can interrupt the wait (EINTR), so retry.
    int status = 0;
    pid_t reaped = -1;
    do {
        reaped = ::waitpid(child, &status, 0);
    } while (reaped == -1 && errno == EINTR);
    QVERIFY2(reaped == child || (reaped == -1 && errno == ECHILD), qPrintable(QString("waitpid: %1").arg(errno)));
    QTRY_VERIFY_WITH_TIMEOUT(QFileInfo(QString::fromLocal8Bit(report)).size() > 0, 10'000);
    QCOMPARE(readBytes(QString::fromLocal8Bit(report)), QByteArray("0 1"));
    // This process's parent is alive.
    QVERIFY(!ParentProcessWatch().parentExited());
#else
    QSKIP("Parent-process watching is Unix-only here.");
#endif
}

void ExportTests::supervisesUnixExportProcessTree()
{
#ifdef Q_OS_UNIX
    QProcess process;
    process.setProcessChannelMode(QProcess::SeparateChannels);
    ExportProcessSupervisor supervisor(process);
    supervisor.start(QStringLiteral("/bin/sh"), {QStringLiteral("-c"), QStringLiteral("sleep 30 & echo $!; wait")});
    QVERIFY2(supervisor.waitForStarted(), qPrintable(process.errorString()));
    QVERIFY(supervisor.supervisionActive());
    QVERIFY(process.waitForReadyRead(2'000));
    bool ok = false;
    const qint64 grandchildPid = QString::fromUtf8(process.readAllStandardOutput()).trimmed().toLongLong(&ok);
    QVERIFY(ok && grandchildPid > 0);
    QVERIFY(supervisor.stopAndWait(500, 2'000));
    QTRY_VERIFY(!ExportArtifactManifest::processIsActive(grandchildPid));
#else
    QSKIP("Unix process-group behavior is runtime-tested on this platform only.");
#endif
}

void ExportTests::stopsUnixWritersAcrossLeaderExit_data()
{
    QTest::addColumn<QString>("action");
    QTest::newRow("leader-already-exited") << QStringLiteral("stop");
    QTest::newRow("leader-exits-during-grace") << QStringLiteral("grace");
    QTest::newRow("destructor-after-leader-exit") << QStringLiteral("destructor");
    QTest::newRow("failed-marker-after-leader-exit") << QStringLiteral("marker");
}

void ExportTests::stopsUnixWritersAcrossLeaderExit()
{
#ifdef Q_OS_UNIX
    QFETCH(QString, action);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString artifact = directory.filePath("staging.part.mp4");
    const QString ready = directory.filePath("writer.ready");
    QProcess process;
    auto supervisor = std::make_unique<ExportProcessSupervisor>(process);
    supervisor->start(QStringLiteral(RAW_TRANSPORT_CONSUMER_PATH),
        {action == "grace" ? QStringLiteral("tree-leader-waits") : QStringLiteral("tree-leader-exits"),
         artifact, ready});
    QVERIFY2(supervisor->waitForStarted(), qPrintable(process.errorString()));
    const auto leaderPid = static_cast<pid_t>(process.processId());
    const auto emergencyStop = qScopeGuard([leaderPid] { if (leaderPid > 1) ::kill(-leaderPid, SIGKILL); });
    QTRY_VERIFY_WITH_TIMEOUT(!readBytes(ready).isEmpty(), 2'000);
    const qint64 writerPid = readBytes(ready).toLongLong();
    QVERIFY(writerPid > 1);
    QCOMPARE(process.write("R", 1), qint64(1));
    QVERIFY(process.waitForBytesWritten(2'000));
    if (action != "grace") QVERIFY(process.state() == QProcess::NotRunning || process.waitForFinished(2'000));
    QVERIFY(ExportArtifactManifest::processIsActive(writerPid));

    QElapsedTimer elapsed;
    elapsed.start();
    if (action == "destructor") {
        supervisor.reset();
    } else if (action == "marker") {
        const auto result = ExportCancellation::request(directory.filePath("cancel"), supervisor.get(),
            [](const QString &, QString *error) {
                if (error) *error = QStringLiteral("injected marker failure");
                return false;
            });
        QVERIFY(!result.markerCreated);
        QVERIFY(result.workerStopped);
    } else {
        QVERIFY(supervisor->stopAndWait(200, 2'000));
    }
    QVERIFY2(elapsed.elapsed() < 12'000, "Process-tree shutdown exceeded the bounded budgets.");
    // This must hold on return, before any owned-path cleanup is allowed.
    QVERIFY(!ExportArtifactManifest::processIsActive(writerPid));
    if (supervisor) {
        QVERIFY(!supervisor->isRunning());
        QVERIFY(supervisor->stopAndWait(0, 0));
    }
    QVERIFY(QFile::remove(artifact));
    QTest::qWait(150);
    QVERIFY2(!QFileInfo::exists(artifact), "A surviving writer recreated the cleaned transaction artifact.");
#else
    QSKIP("Unix leader-exit and SIGTERM-resistant writer regression.");
#endif
}

void ExportTests::stopsUnixWritersBeforeControllerCleanup_data()
{
    QTest::addColumn<bool>("cancelled");
    QTest::newRow("cancelled") << true;
    QTest::newRow("reported-success-with-live-writer") << false;
}

void ExportTests::stopsUnixWritersBeforeControllerCleanup()
{
#ifdef Q_OS_UNIX
    QFETCH(bool, cancelled);
    QSettings settings;
    settings.clear();
    settings.sync();
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    AppController controller;
    controller.m_export.m_exportOutputTransaction = std::make_unique<ExportOutputTransaction>();
    const QString target = directory.filePath("result.mp4");
    QVERIFY(writeBytes(target, "existing user target"));
    const auto prepared = controller.m_export.m_exportOutputTransaction->prepare(target, {}, {}, true);
    QCOMPARE(prepared.status, ExportOutputTransaction::PreparationStatus::Ready);
    const QString staging = controller.m_export.m_exportOutputTransaction->stagingPath();
    const QString id = controller.m_export.m_exportOutputTransaction->transactionId();
    const QString overlay = QDir::temp().filePath(QStringLiteral("flappedear-overlay-%1.mkv").arg(id));
    QVERIFY(writeBytes(overlay, "owned overlay"));
    controller.m_export.m_exportCancelPath = directory.filePath("cancel");
    if (cancelled) QVERIFY(writeBytes(controller.m_export.m_exportCancelPath, {}));
    controller.m_export.m_exportState = cancelled ? QStringLiteral("cancelling") : QStringLiteral("complete");
    controller.m_export.m_exportProcess = std::make_unique<QProcess>();
    controller.m_export.m_exportSupervisor = std::make_unique<ExportProcessSupervisor>(*controller.m_export.m_exportProcess);
    const QString ready = directory.filePath("writer.ready");
    controller.m_export.m_exportSupervisor->start(QStringLiteral(RAW_TRANSPORT_CONSUMER_PATH),
        {QStringLiteral("tree-leader-exits"), staging, ready});
    QVERIFY(controller.m_export.m_exportSupervisor->waitForStarted());
    const auto leaderPid = static_cast<pid_t>(controller.m_export.m_exportProcess->processId());
    const auto emergencyStop = qScopeGuard([leaderPid] { if (leaderPid > 1) ::kill(-leaderPid, SIGKILL); });
    QTRY_VERIFY_WITH_TIMEOUT(!readBytes(ready).isEmpty(), 2'000);
    const qint64 writerPid = readBytes(ready).toLongLong();
    QVERIFY(writerPid > 1);
    const ExportArtifactManifestData manifest{id, QDateTime::currentMSecsSinceEpoch(), overlay,
        staging, target, leaderPid, QStringLiteral("stageB")};
    QString error;
    QVERIFY2(ExportArtifactManifest::create(manifest, &error), qPrintable(error));
    const QString manifestPath = ExportArtifactManifest::manifestPathFor(id);
    controller.m_export.m_exportManifestPath = manifestPath;
    QCOMPARE(controller.m_export.m_exportProcess->write("R", 1), qint64(1));
    QVERIFY(controller.m_export.m_exportProcess->waitForBytesWritten(2'000));
    QVERIFY(controller.m_export.m_exportProcess->state() == QProcess::NotRunning
            || controller.m_export.m_exportProcess->waitForFinished(2'000));

    QVERIFY(!ExportArtifactManifest::recoverStale().contains(manifestPath));
    QVERIFY(QFileInfo::exists(staging));
    QVERIFY(QFileInfo::exists(overlay));
    QVERIFY(controller.exporter()->exporting());
    controller.m_export.finishExport(0, QProcess::NormalExit);
    QVERIFY(!controller.exporter()->exporting());
    QCOMPARE(controller.exporter()->state(), cancelled ? QStringLiteral("cancelled") : QStringLiteral("failed"));
    QVERIFY(!ExportArtifactManifest::processIsActive(writerPid));
    QVERIFY(!QFileInfo::exists(manifestPath));
    QVERIFY(!QFileInfo::exists(overlay));
    QVERIFY(!QFileInfo::exists(staging));
    QTest::qWait(150);
    QVERIFY(!QFileInfo::exists(staging));
    QCOMPARE(readBytes(target), QByteArray("existing user target"));
#else
    QSKIP("Unix controller cleanup after leader exit regression.");
#endif
}

void ExportTests::boundsImmediateProcessTreeStop()
{
    QProcess process;
    ExportProcessSupervisor supervisor(process);
    QVERIFY(supervisor.stopAndWait(0, 0));
    supervisor.start(QStringLiteral(RAW_TRANSPORT_CONSUMER_PATH), {QStringLiteral("stall")});
    QVERIFY(supervisor.waitForStarted());
    QElapsedTimer elapsed;
    elapsed.start();
    static_cast<void>(supervisor.stopAndWait(-1, -1));
    QVERIFY2(elapsed.elapsed() < 1'000, "Negative shutdown budgets must not become infinite Qt waits.");
    QVERIFY(supervisor.stopAndWait(0, 2'000));
    QVERIFY(!supervisor.isRunning());
}

void ExportTests::stopsExportWorkerWhenCancellationMarkerCannotBeCreated()
{
    QProcess process;
    ExportProcessSupervisor supervisor(process);
    supervisor.start(QStringLiteral(RAW_TRANSPORT_CONSUMER_PATH), {QStringLiteral("stall")});
    QVERIFY2(supervisor.waitForStarted(), qPrintable(process.errorString()));

    const ExportCancellationResult result = ExportCancellation::request(
        QStringLiteral("/unwritable/cancel"), &supervisor,
        [](const QString &, QString *error) {
            if (error) *error = QStringLiteral("injected cancellation-marker write failure");
            return false;
        });

    QVERIFY(!result.markerCreated);
    QVERIFY(result.workerStopped);
    QVERIFY(!supervisor.isRunning());
    QCOMPARE(result.error, QStringLiteral("injected cancellation-marker write failure"));
}

void ExportTests::boundsProcessOutputAndProgressLines()
{
    BoundedProcessOutput payload(BoundedProcessOutput::Mode::CompletePayload, 8);
    payload.append(QByteArrayLiteral("1234"));
    payload.append(QByteArrayLiteral("56789"));
    QVERIFY(payload.exceeded());
    QCOMPARE(payload.observedBytes(), 9);
    QCOMPARE(payload.bytes(), QByteArrayLiteral("1234"));

    BoundedProcessOutput tail(BoundedProcessOutput::Mode::DiagnosticTail, 4);
    tail.append(QByteArrayLiteral("1234"));
    tail.append(QByteArrayLiteral("5678"));
    QVERIFY(tail.truncated());
    QCOMPARE(tail.bytes(), QByteArrayLiteral("5678"));

#ifdef Q_OS_UNIX
    // KAN-201: a child that floods its output is stopped close to the cap, not
    // after QProcess has buffered everything written during a long wait.
    QProcess flood;
    flood.start(QStringLiteral("/bin/sh"), {QStringLiteral("-c"), QStringLiteral("exec head -c 268435456 /dev/zero")});
    QVERIFY(flood.waitForStarted());
    constexpr qint64 cap = 1024 * 1024;
    BoundedProcessOutput flooded(BoundedProcessOutput::Mode::CompletePayload, cap);
    bool exited = false;
    while (!exited && !flooded.exceeded()) {
        exited = waitForOutputOrExit(flood, 250);
        flooded.append(flood.readAllStandardOutput());
    }
    flood.kill();
    QVERIFY(flood.waitForFinished(5'000));
    QVERIFY(flooded.exceeded());
    QVERIFY2(flooded.observedBytes() <= 2 * cap, qPrintable(QString::number(flooded.observedBytes())));
#endif

    FfmpegProgressParser parser;
    static_cast<void>(parser.append(QByteArray(ProcessOutputLimits::ffmpegProgressLineBytes + 1, 'x')));
    QVERIFY(parser.overflowed());
    const QList<FfmpegProgress> progress = parser.append(QByteArrayLiteral("frame=12\nprogress=end\n"));
    QCOMPARE(progress.size(), 1);
    QCOMPARE(progress.first().encodedFrames, qsizetype(12));
}

void ExportTests::workerRefusesPathsOutsideItsManifest()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString ffmpeg = FfmpegTools::ffmpegPath();
    QVERIFY2(!ffmpeg.isEmpty(), "FFmpeg is required for the worker manifest check.");
    const auto source = directory.filePath("synthetic.rcz");
    const auto video = directory.filePath("input.mov");
    QVERIFY(writeBytes(source, RczFixture::zip(RczFixture::members())));
    QProcess encoder;
    encoder.start(ffmpeg, {"-hide_banner", "-loglevel", "error", "-y", "-f", "lavfi", "-i",
                           "color=c=black:s=320x180:r=30:d=1", "-c:v", "libx264", "-pix_fmt", "yuv420p", video});
    QVERIFY(encoder.waitForFinished(30'000));
    QCOMPARE(encoder.exitCode(), 0);
    ExportOutputTransaction transaction;
    QCOMPARE(transaction.prepare(directory.filePath("output.mp4"), video, {source}, false).status,
             ExportOutputTransaction::PreparationStatus::Ready);
    const auto id = transaction.transactionId();
    const auto overlay = QDir::temp().filePath(QStringLiteral("flappedear-overlay-%1.mkv").arg(id));
    const auto manifestPath = ExportArtifactManifest::manifestPathFor(id);
    QString error;
    QVERIFY2(ExportArtifactManifest::create({id, QDateTime::currentMSecsSinceEpoch(), overlay,
        transaction.stagingPath(), directory.filePath("output.mp4"), QCoreApplication::applicationPid(), "preparing"},
        &error), qPrintable(error));
    const auto cleanup = qScopeGuard([&] { static_cast<void>(ExportArtifactManifest::cleanupOwned(manifestPath)); });
    const QString rogueOutput = directory.filePath("rogue.mp4");
    const QString rogueOverlay = directory.filePath("rogue.mkv");
    WidgetModel widgets;
    const QJsonObject base{{"vboPath", source}, {"inputPath", video}, {"outputPath", transaction.stagingPath()},
        {"manifestPath", manifestPath}, {"temporaryOverlayPath", overlay},
        {"widgets", widgets.toJson()}, {"sync", QJsonObject{{"offset", .1}, {"timeScale", 1.0}}},
        {"firstFrame", 0}, {"lastFrame", 9}, {"audioEnabled", false}, {"encoder", "libx265"}};
    const QList<std::pair<QJsonObject, QString>> cases{
        {QJsonObject{{"manifestPath", QString()}}, "no ownership manifest"},
        {QJsonObject{{"outputPath", rogueOutput}}, "output path does not match"},
        {QJsonObject{{"temporaryOverlayPath", rogueOverlay}}, "overlay path does not match"},
        {QJsonObject{{"manifestPath", directory.filePath("missing.json")}}, "Could not read export ownership manifest"},
    };
    for (const auto &[override, expected] : cases) {
        QJsonObject settings = base;
        for (auto it = override.begin(); it != override.end(); ++it) settings.insert(it.key(), it.value());
        const auto config = directory.filePath("worker.json");
        QVERIFY(writeBytes(config, QJsonDocument(settings).toJson()));
        QProcess worker;
        worker.start(QStringLiteral(FLAPPEDEAR_NATIVE_PATH), {"--export-worker", config});
        QVERIFY(worker.waitForStarted());
        QVERIFY2(worker.waitForFinished(60'000), qPrintable(worker.errorString()));
        const auto events = worker.readAllStandardOutput() + worker.readAllStandardError();
        QVERIFY2(worker.exitCode() != 0, qPrintable(expected));
        QVERIFY2(events.contains(expected.toUtf8()), events.right(2000).constData());
        QVERIFY(!QFileInfo::exists(rogueOutput));
        QVERIFY(!QFileInfo::exists(rogueOverlay));
        QVERIFY(!QFileInfo::exists(transaction.stagingPath()) || QFileInfo(transaction.stagingPath()).size() == 0);
    }
}

void ExportTests::exportsSyntheticRczThroughWorker_data()
{
    QTest::addColumn<int>("sourcePts"); QTest::addColumn<bool>("withAudio");
    QTest::addColumn<int>("firstFrame"); QTest::addColumn<int>("lastFrame");
    QTest::newRow("rcz") << 0 << false << 0 << 2;
    QTest::newRow("positive-pts") << 2 << false << 0 << 29;
    QTest::newRow("delayed-short-audio") << 2 << true << 0 << 89;
    QTest::newRow("range-after-audio") << 2 << true << 60 << 89;
    QTest::newRow("range-within-audio") << 2 << true << 36 << 41;
    QTest::newRow("range-before-audio") << 2 << true << 0 << 14;
}

void ExportTests::exportsSyntheticRczThroughWorker()
{
    QFETCH(int, sourcePts); QFETCH(bool, withAudio);
    QFETCH(int, firstFrame); QFETCH(int, lastFrame);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString ffmpeg = FfmpegTools::ffmpegPath();
    QVERIFY2(!ffmpeg.isEmpty(), "FFmpeg is required for the RCZ pipeline regression.");
    const auto source = directory.filePath("synthetic.rcz");
    const auto video = directory.filePath("input.mov");
    const auto output = directory.filePath("output.mp4");
    const auto config = directory.filePath("worker.json");
    QVERIFY(writeBytes(source, RczFixture::zip(RczFixture::members())));
    QProcess encoder;
    QStringList inputArgs{"-hide_banner", "-loglevel", "error", "-y", "-f", "lavfi", "-i",
        "color=c=black:s=640x360:r=30:d=4"};
    if (withAudio) inputArgs += QStringList{"-itsoffset", "1", "-f", "lavfi", "-i", "sine=frequency=440:sample_rate=48000:duration=0.5",
        "-map", "0:v", "-map", "1:a", "-c:a", "pcm_s16le"};
    inputArgs += QStringList{"-c:v", "libx264", "-pix_fmt", "yuv420p", "-output_ts_offset", QString::number(sourcePts), video};
    encoder.start(ffmpeg, inputArgs);
    QVERIFY(encoder.waitForFinished(30'000));
    QCOMPARE(encoder.exitCode(), 0);
    ExportOutputTransaction transaction;
    QCOMPARE(transaction.prepare(output, video, {source}, false).status,
             ExportOutputTransaction::PreparationStatus::Ready);
    const auto id = transaction.transactionId();
    const auto overlay = QDir::temp().filePath(QStringLiteral("flappedear-overlay-%1.mkv").arg(id));
    const auto manifestPath = ExportArtifactManifest::manifestPathFor(id);
    QString error;
    QVERIFY2(ExportArtifactManifest::create({id, QDateTime::currentMSecsSinceEpoch(), overlay,
        transaction.stagingPath(), output, QCoreApplication::applicationPid(), "preparing"}, &error), qPrintable(error));
    const auto cleanup = qScopeGuard([&] { static_cast<void>(ExportArtifactManifest::cleanupOwned(manifestPath)); });
    WidgetModel widgets;
    const QJsonObject settings{{"vboPath", source}, {"inputPath", video}, {"outputPath", transaction.stagingPath()},
        {"manifestPath", manifestPath}, {"temporaryOverlayPath", overlay},
        {"widgets", widgets.toJson()}, {"sync", QJsonObject{{"offset", .1}, {"timeScale", 1.0}}},
        {"firstFrame", firstFrame}, {"lastFrame", lastFrame}, {"audioEnabled", withAudio}, {"encoder", "libx265"}};
    QVERIFY(writeBytes(config, QJsonDocument(settings).toJson()));
    QProcess worker;
    worker.start(QStringLiteral(FLAPPEDEAR_NATIVE_PATH), {"--export-worker", config});
    QVERIFY(worker.waitForStarted());
    QVERIFY2(worker.waitForFinished(60'000), qPrintable(worker.errorString()));
    const auto events = worker.readAllStandardOutput() + worker.readAllStandardError();
    QCOMPARE(worker.exitStatus(), QProcess::NormalExit);
    QByteArray failures;
    for (const auto &line : events.split('\n'))
        if (line.contains("\"passed\":false") || line.contains("\"state\":\"failed\"")) failures += line + '\n';
    // Qt Test truncates long assertion messages; preserve the terminal error.
    QVERIFY2(worker.exitCode() == 0, (failures.isEmpty() ? events.right(4000) : failures.left(4000)).constData());
    QVERIFY2(events.contains("initializeRenderer"), events.constData());
    QVERIFY2(transaction.commit(&error), qPrintable(error));
    const auto media = MediaProbe::probe(output, {}, true);
    QVERIFY(QFileInfo(output).size() > 0);
    QCOMPARE(media.videoFrameCount, lastFrame - firstFrame + 1);
    const double start = firstFrame / 30.0, end = (lastFrame + 1) / 30.0;
    const double expectedAudioDuration = withAudio ? std::max(0.0, std::min(end, 1.5) - std::max(start, 1.0)) : 0.0;
    if (expectedAudioDuration > 0) {
        QVERIFY(!media.audioCodecs.isEmpty());
        const double expectedStart = std::max(0.0, 1.0 - start);
        QVERIFY(std::abs(media.audioStartTime - media.videoStartTime - expectedStart) < .023);
        QVERIFY(std::abs(media.audioDuration - expectedAudioDuration) < .023);
        QProcess decoder;
        decoder.start(ffmpeg, {"-v", "error", "-i", output, "-map", "0:a", "-ac", "1", "-f", "f32le", "pipe:1"});
        QVERIFY(decoder.waitForFinished(30'000));
        QCOMPARE(decoder.exitCode(), 0);
        const auto samples = decoder.readAllStandardOutput();
        QVERIFY(samples.size() >= 4800 * 4);
        double power = 0;
        for (int index = 0; index < 4800; ++index) {
            const float value = std::bit_cast<float>(qFromLittleEndian<quint32>(samples.constData() + index * 4));
            power += value * value;
        }
        QVERIFY(std::sqrt(power / 4800) > .03); // Audible tone starts with the delayed stream.
    } else QVERIFY(media.audioCodecs.isEmpty());
}

// KAN-175: an MP4 trimmed losslessly keeps the packets from the keyframe before
// the cut, and its edit list hides them. The header's nb_frames counts them; the
// export must schedule only the frames that are presented.
void ExportTests::exportsEditListSourceThroughWorker()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString ffmpeg = FfmpegTools::ffmpegPath();
    QVERIFY2(!ffmpeg.isEmpty(), "FFmpeg is required for the edit-list export regression.");
    const auto source = directory.filePath("synthetic.rcz");
    const auto untrimmed = directory.filePath("untrimmed.mp4");
    const auto video = directory.filePath("input.mp4");
    const auto output = directory.filePath("output.mp4");
    const auto config = directory.filePath("worker.json");
    QVERIFY(writeBytes(source, RczFixture::zip(RczFixture::members())));
    const auto runFfmpeg = [&ffmpeg](const QStringList &arguments) {
        QProcess process;
        process.start(ffmpeg, arguments);
        QVERIFY2(process.waitForFinished(30'000), qPrintable(process.errorString()));
        QVERIFY2(process.exitCode() == 0, process.readAllStandardError().constData());
    };
    // Keyframes every second; a copy-trim at 1.5 s starts at the 1 s keyframe
    // and hides its first 15 frames with an edit list.
    runFfmpeg({"-hide_banner", "-loglevel", "error", "-y", "-f", "lavfi", "-i", "testsrc2=s=320x240:r=30:d=4",
               "-c:v", "libx264", "-g", "30", "-bf", "2", "-pix_fmt", "yuv420p", untrimmed});
    runFfmpeg({"-hide_banner", "-loglevel", "error", "-y", "-ss", "1.5", "-i", untrimmed, "-c", "copy", video});
    QCOMPARE(MediaProbe::probe(video, {}, false, -1, {}, {}, true).videoPacketCount, qsizetype(90));
    const MediaInfo input = MediaProbe::probe(video);
    QCOMPARE(input.videoFrameCount, qsizetype(75));
    const auto fullRange = ExportEngine::fullVideoFrameRange(input, {30, 1});
    QVERIFY(fullRange);
    QCOMPARE(fullRange->firstFrame, 0);
    QCOMPARE(fullRange->lastFrame, 74);
    // Without an edit list, the header count is used as before.
    QCOMPARE(MediaProbe::probe(untrimmed).videoFrameCount, qsizetype(120));

    ExportOutputTransaction transaction;
    QCOMPARE(transaction.prepare(output, video, {source}, false).status,
             ExportOutputTransaction::PreparationStatus::Ready);
    const auto id = transaction.transactionId();
    const auto overlay = QDir::temp().filePath(QStringLiteral("flappedear-overlay-%1.mkv").arg(id));
    const auto manifestPath = ExportArtifactManifest::manifestPathFor(id);
    QString error;
    QVERIFY2(ExportArtifactManifest::create({id, QDateTime::currentMSecsSinceEpoch(), overlay,
        transaction.stagingPath(), output, QCoreApplication::applicationPid(), "preparing"}, &error), qPrintable(error));
    const auto cleanup = qScopeGuard([&] { static_cast<void>(ExportArtifactManifest::cleanupOwned(manifestPath)); });
    WidgetModel widgets;
    // No frame range: the worker exports the whole source, as the editor does.
    const QJsonObject settings{{"vboPath", source}, {"inputPath", video}, {"outputPath", transaction.stagingPath()},
        {"manifestPath", manifestPath}, {"temporaryOverlayPath", overlay},
        {"widgets", widgets.toJson()}, {"sync", QJsonObject{{"offset", .1}, {"timeScale", 1.0}}},
        {"audioEnabled", false}, {"encoder", "libx265"}};
    QVERIFY(writeBytes(config, QJsonDocument(settings).toJson()));
    QProcess worker;
    worker.start(QStringLiteral(FLAPPEDEAR_NATIVE_PATH), {"--export-worker", config});
    QVERIFY(worker.waitForStarted());
    QVERIFY2(worker.waitForFinished(60'000), qPrintable(worker.errorString()));
    const auto events = worker.readAllStandardOutput() + worker.readAllStandardError();
    QCOMPARE(worker.exitStatus(), QProcess::NormalExit);
    QByteArray failures;
    for (const auto &line : events.split('\n'))
        if (line.contains("\"passed\":false") || line.contains("\"state\":\"failed\"")) failures += line + '\n';
    QVERIFY2(worker.exitCode() == 0, (failures.isEmpty() ? events.right(4000) : failures.left(4000)).constData());
    QVERIFY2(transaction.commit(&error), qPrintable(error));
    QCOMPARE(MediaProbe::probe(output, {}, true).videoFrameCount, qsizetype(75));
}

// KAN-106: three chapter files export as one source. A range that crosses
// both joins keeps its exact frame count, each frame comes from the right
// chapter, audio follows the video, and every chapter is protected.
void ExportTests::exportsChapteredSourceThroughWorker()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString ffmpeg = FfmpegTools::ffmpegPath();
    QVERIFY2(!ffmpeg.isEmpty(), "FFmpeg is required for the chaptered export regression.");
    const auto source = directory.filePath("synthetic.rcz");
    const auto output = directory.filePath("output.mp4");
    const auto config = directory.filePath("worker.json");
    QVERIFY(writeBytes(source, RczFixture::zip(RczFixture::members())));
    const auto runFfmpeg = [&ffmpeg](const QStringList &arguments, QByteArray *standardOutput = nullptr) {
        QProcess process;
        process.start(ffmpeg, arguments);
        QVERIFY2(process.waitForFinished(60'000), qPrintable(process.errorString()));
        QVERIFY2(process.exitCode() == 0, process.readAllStandardError().constData());
        if (standardOutput) *standardOutput = process.readAllStandardOutput();
    };
    // 60 frames each at 29.97 fps, solid red, green and blue. Chapters 1 and
    // 2 are silent, and chapter 2's audio ends 0.5 s before its video; chapter
    // 3 carries a tone, which must start exactly where chapter 3 does.
    QStringList chapters;
    const QStringList colours{"red", "green", "blue"};
    const QStringList sounds{"anullsrc=r=48000:cl=mono", "anullsrc=r=48000:cl=mono", "sine=frequency=1000:sample_rate=48000"};
    const QStringList soundLengths{"2.002", "1.5", "2.002"};
    for (int index = 0; index < 3; ++index) {
        chapters.append(directory.filePath(QStringLiteral("GX0%10091.MP4").arg(index + 1)));
        runFfmpeg({"-hide_banner", "-loglevel", "error", "-y",
                   "-f", "lavfi", "-i", QStringLiteral("color=%1:s=320x240:r=30000/1001").arg(colours[index]),
                   "-f", "lavfi", "-t", soundLengths[index], "-i", sounds[index],
                   "-frames:v", "60", "-c:v", "libx264", "-g", "30", "-pix_fmt", "yuv420p",
                   "-colorspace", "bt709", "-color_primaries", "bt709", "-color_trc", "bt709", "-color_range", "tv",
                   "-c:a", "aac", "-video_track_timescale", "30000", chapters.last()});
    }
    const auto combined = ChapterSource::probe(chapters, {60060, 60060, 60060}).info;
    QCOMPARE(combined.videoFrameCount, qsizetype(180));
    QCOMPARE(ExportEngine::fullVideoFrameRange(combined, {30000, 1001})->lastFrame, 179);
    // A chapter that differs from what the editor loaded is refused.
    QVERIFY_THROWS_EXCEPTION(std::runtime_error, static_cast<void>(ChapterSource::probe(chapters, {60060, 60060, 30030})));

    // An output onto any chapter is refused.
    ExportOutputTransaction refused;
    QVERIFY(refused.prepare(chapters[1], chapters[0], {source, chapters[1], chapters[2]}, true).status
            == ExportOutputTransaction::PreparationStatus::Error);
    ExportOutputTransaction transaction;
    QCOMPARE(transaction.prepare(output, chapters[0], {source, chapters[1], chapters[2]}, false).status,
             ExportOutputTransaction::PreparationStatus::Ready);
    const auto id = transaction.transactionId();
    const auto overlay = QDir::temp().filePath(QStringLiteral("flappedear-overlay-%1.mkv").arg(id));
    const auto manifestPath = ExportArtifactManifest::manifestPathFor(id);
    QString error;
    QVERIFY2(ExportArtifactManifest::create({id, QDateTime::currentMSecsSinceEpoch(), overlay,
        transaction.stagingPath(), output, QCoreApplication::applicationPid(), "preparing"}, &error), qPrintable(error));
    const auto cleanup = qScopeGuard([&] { static_cast<void>(ExportArtifactManifest::cleanupOwned(manifestPath)); });
    WidgetModel widgets;
    // Frames 50..129: the last 10 of chapter 1, all of chapter 2, the first 10 of chapter 3.
    const QJsonObject settings{{"vboPath", source}, {"inputPath", chapters[0]},
        {"chapterPaths", QJsonArray::fromStringList(chapters)},
        {"chapterDurationTicks", QJsonArray{60060, 60060, 60060}}, {"outputPath", transaction.stagingPath()},
        {"manifestPath", manifestPath}, {"temporaryOverlayPath", overlay},
        {"widgets", widgets.toJson()}, {"sync", QJsonObject{{"offset", .1}, {"timeScale", 1.0}}},
        {"frameRateNumerator", 30000}, {"frameRateDenominator", 1001}, {"firstFrame", 50}, {"lastFrame", 129},
        {"audioEnabled", true}, {"encoder", "libx265"}};
    QVERIFY(writeBytes(config, QJsonDocument(settings).toJson()));
    QProcess worker;
    worker.start(QStringLiteral(FLAPPEDEAR_NATIVE_PATH), {"--export-worker", config});
    QVERIFY(worker.waitForStarted());
    QVERIFY2(worker.waitForFinished(120'000), qPrintable(worker.errorString()));
    const auto events = worker.readAllStandardOutput() + worker.readAllStandardError();
    QCOMPARE(worker.exitStatus(), QProcess::NormalExit);
    QByteArray failures;
    for (const auto &line : events.split('\n'))
        if (line.contains("\"passed\":false") || line.contains("\"state\":\"failed\"")) failures += line + '\n';
    QVERIFY2(worker.exitCode() == 0, (failures.isEmpty() ? events.right(4000) : failures.left(4000)).constData());
    QVERIFY2(transaction.commit(&error), qPrintable(error));

    const MediaInfo result = MediaProbe::probe(output, {}, true);
    QCOMPARE(result.videoFrameCount, qsizetype(80));
    QVERIFY(!result.audioCodecs.isEmpty());
    QVERIFY2(std::abs(result.audioDuration - 80 * 1001.0 / 30000.0) < 0.05,
             qPrintable(QString::number(result.audioDuration)));
    // The tone starts at output frame 70, where chapter 3 starts, not 0.5 s
    // early because chapter 2's audio was short.
    QByteArray silence;
    {
        QProcess process;
        process.start(ffmpeg, {"-hide_banner", "-nostats", "-i", output, "-vn", "-af", "silencedetect=n=-40dB:d=0.05", "-f", "null", "-"});
        QVERIFY(process.waitForFinished(30'000));
        silence = process.readAllStandardError();
    }
    const auto silenceEnd = QRegularExpression(QStringLiteral("silence_end: ([0-9.]+)")).match(QString::fromUtf8(silence));
    QVERIFY2(silenceEnd.hasMatch(), silence.constData());
    QVERIFY2(std::abs(silenceEnd.captured(1).toDouble() - 70 * 1001.0 / 30000.0) < 0.03, qPrintable(silenceEnd.captured(1)));
    // One 1x1 average per frame: the colour changes exactly at both joins.
    QByteArray pixels;
    runFfmpeg({"-hide_banner", "-loglevel", "error", "-i", output, "-vf", "scale=1:1:flags=area",
               "-f", "rawvideo", "-pix_fmt", "rgb24", "-"}, &pixels);
    QCOMPARE(pixels.size(), 80 * 3);
    const auto dominant = [&pixels](const int frame) {
        const auto *rgb = reinterpret_cast<const unsigned char *>(pixels.constData()) + frame * 3;
        return rgb[0] > 150 && rgb[1] < 100 && rgb[2] < 100 ? 0
             : rgb[1] > 80 && rgb[0] < 100 && rgb[2] < 100 ? 1
             : rgb[2] > 150 && rgb[0] < 100 && rgb[1] < 100 ? 2 : -1;
    };
    for (int frame = 0; frame < 80; ++frame)
        QVERIFY2(dominant(frame) == (frame < 10 ? 0 : frame < 70 ? 1 : 2), qPrintable(QString::number(frame)));
}

void ExportTests::tracksExportStageElapsedTime()
{
    ExportStageTimer timer;
    timer.start(100, QStringLiteral("preparing"));
    QCOMPARE(timer.totalElapsedMilliseconds(250), 150);
    QCOMPARE(timer.stageElapsedMilliseconds(250), 150);
    timer.transition(300, QStringLiteral("renderingOverlay"));
    QCOMPARE(timer.totalElapsedMilliseconds(350), 250);
    QCOMPARE(timer.stageElapsedMilliseconds(350), 50);
    timer.transition(375, QStringLiteral("renderingOverlay"));
    QCOMPARE(timer.stageElapsedMilliseconds(400), 100);
    QCOMPARE(timer.completedStageDurations().value(QStringLiteral("preparing")), 200);
}

void ExportTests::boundsVerboseDiagnosticStorage()
{
    BoundedDiagnosticLog log(4);
    log.append(QStringLiteral("one"));
    log.append(QStringLiteral("two"));
    log.append(QStringLiteral("three"));
    log.append(QStringLiteral("four"));
    log.append(QStringLiteral("five\nwith details"));
    QCOMPARE(log.size(), 4);
    QCOMPARE(log.maximumEntries(), 4);
    QVERIFY(log.text().startsWith(QStringLiteral("[older diagnostic entries omitted]\n")));
    QVERIFY(!log.text().contains(QStringLiteral("one")));
    QVERIFY(log.text().endsWith(QStringLiteral("five\nwith details")));
    // "one" and "two" left the head, each with its line break.
    QCOMPARE(log.droppedCharacters(), qint64(8));
    log.clear();
    QCOMPARE(log.droppedCharacters(), qint64(0));
}

// The Very verbose view follows the log as entries arrive, and a reader scrolled back
// keeps the same history in view while the bounded log trims its head.
void ExportTests::showsVeryVerboseDiagnosticsLive()
{
    AppController controller;
    QSignalSpy logChanged(controller.exporter(), &ExportController::diagnosticLogChanged);
    for (int entry = 0; entry < 5; ++entry)
        controller.m_export.appendExportDiagnostic(QStringLiteral("entry %1").arg(entry));
    QTRY_COMPARE(logChanged.count(), 1); // Coalesced.

    QQmlEngine engine; engine.rootContext()->setContextProperty("appController", &controller);
    QQmlComponent component(&engine, QUrl::fromLocalFile(qmlSourcePath("Main.qml")));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    std::unique_ptr<QObject> object(component.create());
    auto *window = qobject_cast<QQuickWindow *>(object.get()); QVERIFY(window);
    window->show(); QVERIFY(QTest::qWaitForWindowExposed(window));
    // The log lives in the export progress popup.
    controller.m_export.m_exportProgressVisible = true;
    emit controller.m_export.changed();
    QObject *details = window->findChild<QObject *>("exportDetails");
    QObject *veryVerbose = window->findChild<QObject *>("exportVeryVerbose");
    auto *log = window->findChild<QQuickItem *>("verboseExportLog");
    auto *scroll = window->findChild<QQuickItem *>("verboseExportScroll");
    QVERIFY(details && veryVerbose && log && scroll);
    details->setProperty("checked", true);
    veryVerbose->setProperty("checked", true);
    QTRY_VERIFY(scroll->isVisible());
    QTRY_COMPARE(log->property("text").toString(), controller.exporter()->diagnosticLog());

    controller.m_export.appendExportDiagnostic(QStringLiteral("arrived while open"));
    QTRY_VERIFY(log->property("text").toString().endsWith(QStringLiteral("arrived while open")));

    // Scroll back into history, detached from the tail, then let the log trim its head.
    for (int entry = 5; entry < 1000; ++entry)
        controller.m_export.appendExportDiagnostic(QStringLiteral("entry %1").arg(entry));
    QTRY_VERIFY(log->property("text").toString().endsWith(QStringLiteral("entry 999")));
    // Let the deferred tail scroll of that update finish before detaching from the tail.
    QTRY_VERIFY(!log->property("programmaticScroll").toBool());
    auto *flickable = scroll->property("contentItem").value<QQuickItem *>();
    QVERIFY(flickable);
    QTRY_VERIFY(flickable->property("contentHeight").toDouble() > scroll->height() * 2);
    log->setProperty("followTail", false);
    const auto lineTop = [&](const QString &line) {
        const int position = int(log->property("text").toString().indexOf(line));
        QRectF rectangle;
        QMetaObject::invokeMethod(log, "positionToRectangle", Q_RETURN_ARG(QRectF, rectangle),
                                  Q_ARG(int, position));
        return rectangle.y();
    };
    const double anchor = lineTop(QStringLiteral("entry 600\n"));
    flickable->setProperty("contentY", anchor);
    QTRY_COMPARE(flickable->property("contentY").toDouble(), anchor);
    for (int entry = 1000; entry < 1700; ++entry)
        controller.m_export.appendExportDiagnostic(QStringLiteral("entry %1").arg(entry));
    QTRY_VERIFY(log->property("text").toString().endsWith(QStringLiteral("entry 1699")));
    QVERIFY(controller.exporter()->diagnosticDroppedCharacters() > 0);
    QVERIFY(log->property("text").toString().contains(QStringLiteral("entry 600\n")));
    QTRY_VERIFY(std::abs(flickable->property("contentY").toDouble()
                         - lineTop(QStringLiteral("entry 600\n"))) < 1.0);
    QVERIFY(!log->property("followTail").toBool());

    // KAN-153: dragging the scroll bar to the very end does not resume
    // following; only Jump to latest does (AGENTS.md).
    auto *bar = window->findChild<QQuickItem *>("verboseExportScrollBar");
    QVERIFY(bar);
    auto *handle = bar->property("contentItem").value<QQuickItem *>();
    QVERIFY(handle && handle->height() > 0);
    const QPoint grab = handle->mapToScene(QPointF(handle->width() / 2, handle->height() / 2)).toPoint();
    const QPoint end = bar->mapToScene(QPointF(bar->width() / 2, bar->height() + 40)).toPoint();
    QTest::mousePress(window, Qt::LeftButton, {}, grab);
    for (int step = 1; step <= 10; ++step)
        QTest::mouseMove(window, grab + (end - grab) * step / 10);
    QTest::mouseRelease(window, Qt::LeftButton, {}, end);
    QTRY_VERIFY(bar->property("position").toDouble() + bar->property("size").toDouble() > 0.999);
    QVERIFY(!log->property("followTail").toBool());
    controller.m_export.appendExportDiagnostic(QStringLiteral("after the drag"));
    QTRY_VERIFY(log->property("text").toString().endsWith(QStringLiteral("after the drag")));
    QVERIFY(!log->property("followTail").toBool());
    QVERIFY(QMetaObject::invokeMethod(log, "jumpToLatest"));
    QVERIFY(log->property("followTail").toBool());
}

void ExportTests::persistsExportDiagnosticsAndRetainsKnownLogs()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QDateTime started(QDate(2026, 8, 22), QTime(21, 25, 30), QTimeZone::UTC);
    QCOMPARE(PersistentExportLog::fileName(started, QStringLiteral("a83f91c2d4e5f678")),
             QStringLiteral("export-20260822-212530-a83f91c2d4e5f678.log"));

    QString error;
    auto log = PersistentExportLog::create(
        directory.path(), QStringLiteral("a83f91c2d4e5f678"),
        QStringLiteral("FlappedEar Overlays Export Log\nExport ID: a83f91c2d4e5f678"), &error, started);
    QVERIFY2(log, qPrintable(error));
    const QString activePath = log->path();
    QVERIFY(log->append(QStringLiteral("[lifecycle] Preparing")));
    QVERIFY(log->append(QStringLiteral("[00:00:01.000] Stage A started")));
    QVERIFY(log->append(QStringLiteral("Result: SUCCESS")));
    QFile saved(activePath);
    QVERIFY(saved.open(QIODevice::ReadOnly));
    const QString contents = QString::fromUtf8(saved.readAll());
    QVERIFY(contents.contains(QStringLiteral("Export ID: a83f91c2d4e5f678")));
    QVERIFY(contents.contains(QStringLiteral("Stage A started")));
    QVERIFY(contents.contains(QStringLiteral("Result: SUCCESS")));

    QFile unrelated(directory.filePath(QStringLiteral("keep-me.log")));
    QVERIFY(unrelated.open(QIODevice::WriteOnly));
    unrelated.close();
    for (int index = 0; index < 12; ++index) {
        const QDateTime time = started.addSecs(index + 1);
        const QString id = QStringLiteral("%1abcdef012345678").arg(index, 8, 16, QLatin1Char('0'));
        auto oldLog = PersistentExportLog::create(directory.path(), id, QStringLiteral("Result: CANCELLED"),
                                                   &error, time);
        QVERIFY2(oldLog, qPrintable(error));
    }
    PersistentExportLog::retainNewest(directory.path(), activePath, 10);
    QVERIFY(QFileInfo::exists(activePath));
    QVERIFY(QFileInfo::exists(unrelated.fileName()));
    const QStringList remaining = QDir(directory.path()).entryList({QStringLiteral("export-*.log")}, QDir::Files);
    QCOMPARE(remaining.size(), 11); // ten most recent plus the active log
}

void ExportTests::formatsStageAFailureDiagnostics()
{
    const StageAFailureDiagnostics diagnostics{
        QStringLiteral("FFmpeg stopped before the next overlay frame was rendered"),
        819,
        8992,
        1,
        QStringLiteral("NormalExit"),
        QStringLiteral("WriteError"),
        QStringLiteral("Broken pipe"),
        817,
        13'630'000,
        59.94,
        0.21,
        31'457'280,
        94'371'840,
        QStringLiteral("/tmp/flappedear-overlay-test.mkv"),
        59'391'756,
        QStringLiteral("No space left on device"),
        QStringLiteral("/"),
        123'456,
        987'654,
        QStringLiteral("/Volumes/Exports"),
        456'789,
        987'654,
        QStringLiteral("/tmp/flappedear-export.cancel"),
        false};
    const QString formatted = formatStageAFailureDiagnostics(diagnostics);
    QVERIFY(formatted.contains(QStringLiteral("Stage A exited unexpectedly")));
    QVERIFY(formatted.contains(QStringLiteral("Submitted frames: 819 / 8992")));
    QVERIFY(formatted.contains(QStringLiteral("FFmpeg exit: code=1 status=NormalExit")));
    QVERIFY(formatted.contains(QStringLiteral("QProcess error: WriteError (Broken pipe)")));
    QVERIFY(formatted.contains(QStringLiteral("Temporary overlay before cleanup: /tmp/flappedear-overlay-test.mkv (59391756 bytes)")));
    QVERIFY(formatted.contains(QStringLiteral("Cancellation marker: /tmp/flappedear-export.cancel exists=no")));
    QVERIFY(formatted.contains(QStringLiteral("FFmpeg stderr tail:\nNo space left on device")));

    const QVariantMap details = stageAFailureDiagnosticDetails(diagnostics);
    QCOMPARE(details.value(QStringLiteral("submittedFrames")).toLongLong(), 819);
    QCOMPARE(details.value(QStringLiteral("temporaryOverlayBytes")).toLongLong(), 59'391'756);
    QCOMPARE(details.value(QStringLiteral("cancellationFileExists")).toBool(), false);
    QCOMPARE(details.value(QStringLiteral("stderrTail")).toString(), QStringLiteral("No space left on device"));

    StageAFailureDiagnostics crash = diagnostics;
    crash.exitCode = 9;
    crash.exitStatus = QStringLiteral("CrashExit (Unix signal unavailable from QProcess)");
    crash.processError = QStringLiteral("Crashed");
    crash.processErrorString = QStringLiteral("Process crashed");
    crash.cancellationFileExists = true;
    crash.stderrTail.clear();
    const QString crashFormatted = formatStageAFailureDiagnostics(crash);
    QVERIFY(crashFormatted.contains(QStringLiteral("status=CrashExit (Unix signal unavailable from QProcess)")));
    QVERIFY(crashFormatted.contains(QStringLiteral("QProcess error: Crashed (Process crashed)")));
    QVERIFY(crashFormatted.contains(QStringLiteral("exists=yes")));
}

void ExportTests::throttlesDiagnosticHeartbeats()
{
    DiagnosticHeartbeat heartbeat(500);
    QVERIFY(!heartbeat.shouldEmit(0));
    QVERIFY(!heartbeat.shouldEmit(499));
    QVERIFY(heartbeat.shouldEmit(500));
    QVERIFY(!heartbeat.shouldEmit(999));
    QVERIFY(heartbeat.shouldEmit(1'000));
}

void ExportTests::tracksValidationSubstepStages()
{
    ExportStageTimer timer;
    timer.start(0, QStringLiteral("preparing"));
    timer.transition(10, QStringLiteral("renderingOverlay"));
    timer.transition(20, QStringLiteral("validatingOverlay"));
    QCOMPARE(timer.stage(), QStringLiteral("validatingOverlay"));
    QCOMPARE(timer.stageElapsedMilliseconds(25), 5);
    timer.transition(30, QStringLiteral("encodingVideo"));
    timer.transition(40, QStringLiteral("validatingOutput"));
    timer.transition(50, QStringLiteral("cleaningUp"));
    timer.transition(60, QStringLiteral("complete"));
    const auto durations = timer.completedStageDurations();
    for (const QString &stage : {QStringLiteral("preparing"), QStringLiteral("renderingOverlay"),
                                 QStringLiteral("validatingOverlay"), QStringLiteral("encodingVideo"),
                                 QStringLiteral("validatingOutput"), QStringLiteral("cleaningUp")}) {
        QCOMPARE(durations.value(stage), 10);
    }
}

void ExportTests::detectsHevcEncoders()
{
    const QString output = " V....D hevc_videotoolbox Apple VideoToolbox\n V....D libx265 x265\n";
    const QList<EncoderCapability> encoders = EncoderDetector::parseEncoders(output);
    QCOMPARE(encoders.size(), 2);
    QCOMPARE(encoders[0].id, QString("hevc_videotoolbox"));
    QVERIFY(encoders[0].hardware);
    QCOMPARE(EncoderDetector::preferredHevcEncoder(encoders), QString("hevc_videotoolbox"));
}

void ExportTests::cancelsEncoderDiscovery()
{
#ifdef Q_OS_UNIX
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString helper = directory.filePath("slow-ffmpeg.sh");
    QFile script(helper);
    QVERIFY(script.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QCOMPARE(script.write("#!/bin/sh\nsleep 10\n"), qint64(19));
    script.close();
    QVERIFY(script.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));
    QString error;
    try {
        static_cast<void>(EncoderDetector::discover(helper, [] { return true; }));
    } catch (const std::exception &exception) {
        error = QString::fromUtf8(exception.what());
    }
    QCOMPARE(error, QStringLiteral("Encoder discovery cancelled."));
#else
    QSKIP("Cancellable helper script requires a POSIX shell.");
#endif
}

void ExportTests::calculatesTimestampDrivenExportFrames()
{
    const MediaRational ntscRate{60'000, 1001};
    QCOMPARE(ExportEngine::sourceVideoTime(30.0, 0, ntscRate), 30.0);
    QVERIFY(qAbs(ExportEngine::sourceVideoTime(30.0, 1, ntscRate) - (30.0 + 1001.0 / 60'000.0)) < 0.000001);
    QVERIFY(ExportEngine::sourceVideoTime(30.0, 7'192, ntscRate) < 150.0);
    QVERIFY(ExportEngine::sourceVideoTime(30.0, 7'192, ntscRate) > 149.9);
    QCOMPARE(ExportEngine::exportRelativeTime(0, ntscRate), 0.0);

    MediaInfo source;
    source.audioStartTime = 0.0;
    source.audioDuration = 7.317333;
    QVERIFY(qAbs(ExportEngine::audioDurationForRange(source, 0.0, 7.374033)
                 - source.audioDuration) < 0.000001);
    QVERIFY(qAbs(ExportEngine::audioDurationForRange(source, 1.0, 8.0) - 6.317333) < 0.000001);
    QCOMPARE(ExportEngine::audioDurationForRange(source, 8.0, 9.0), 0.0);
}

void ExportTests::checksCompositionFiltersBeforeRendering()
{
    for (const int bits : {8, 10}) {
        ExportMediaProfile profile;
        profile.outputBitDepth = bits;
        profile.outputPixelFormat = bits == 10 ? "yuv420p10le" : "yuv420p";
        const QString graph = ExportEngine::stageBVideoFilterGraph(
            {"0", "0", "0.1"}, QSize(64, 64), QSize(64, 64), {30, 1}, 3, profile);
        const QString error = ExportEngine::verifyCompositionFilters(FfmpegTools::ffmpegPath(), graph);
        QVERIFY2(error.isEmpty(), qPrintable(error));
    }
    const QString error = ExportEngine::verifyCompositionFilters(
        FfmpegTools::ffmpegPath(), "[0:v]this_filter_does_not_exist[video]");
    QVERIFY(error.contains("required overlay filters"));
    QVERIFY(error.contains("this_filter_does_not_exist"));
    QVERIFY_THROWS_EXCEPTION(OperationCancelled, static_cast<void>(ExportEngine::verifyCompositionFilters(
        FfmpegTools::ffmpegPath(), {}, [] { return true; })));
}

void ExportTests::preservesFramesWithPositiveSourcePts_data()
{
    QTest::addColumn<int>("first"); QTest::addColumn<int>("last");
    QTest::newRow("full") << 0 << 299;
    QTest::newRow("early-range") << 90 << 179;
    QTest::newRow("seek-range") << 210 << 299;
}

void ExportTests::preservesFramesWithPositiveSourcePts()
{
    QFETCH(int, first); QFETCH(int, last);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto raw = directory.filePath("source.rgb");
    const auto source = directory.filePath("source.mp4");
    const auto overlayRaw = directory.filePath("overlay.rgba");
    const auto overlay = directory.filePath("overlay.mkv");
    const auto output = directory.filePath("output.rgba");
    constexpr int width = 64, height = 16;
    QByteArray pixels(300 * width * height * 3, '\0');
    for (int frame = 0; frame < 300; ++frame) for (int y = 0; y < height; ++y)
        for (int bit = 0; bit < 9; ++bit) for (int x = bit * 6; x < bit * 6 + 6; ++x)
            for (int c = 0; c < 3; ++c) pixels[((frame * height + y) * width + x) * 3 + c] = (frame & (1 << bit)) ? '\xFF' : '\0';
    QVERIFY(writeBytes(raw, pixels));
    const int count = last - first + 1;
    QVERIFY(writeBytes(overlayRaw, QByteArray(count * width * height * 4, '\0')));
    const auto run = [&](const QStringList &args) {
        QProcess process; process.start(FfmpegTools::ffmpegPath(), args);
        if (!process.waitForFinished(30'000) || process.exitCode() != 0)
            qWarning().noquote() << process.readAllStandardError();
        return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
    };
    QVERIFY(run({"-v", "error", "-y", "-f", "rawvideo", "-pixel_format", "rgb24", "-video_size", "64x16", "-framerate", "30", "-i", raw,
        "-c:v", "libx264", "-crf", "0", "-pix_fmt", "yuv420p", "-output_ts_offset", "2", source}));
    QVERIFY(run({"-v", "error", "-y", "-f", "rawvideo", "-pixel_format", "rgba", "-video_size", "64x16", "-framerate", "30", "-i", overlayRaw,
        "-c:v", "ffv1", "-pix_fmt", "bgra", overlay}));
    const auto info = MediaProbe::probe(source);
    QVERIFY(std::abs(info.videoStartTime - 2.0) < .001);
    const auto access = ExportEngine::stageBSourceAccess(info, {first,last}, {30,1});
    QVERIFY(access.has_value());
    const auto profile = ExportMediaProfile::derive(info, {width,height}, {30,1}, 1'000'000, "libx265");
    QStringList args{"-v", "error", "-y"};
    args += ExportEngine::stageBInputArguments(*access, source);
    args += QStringList{"-i", overlay, "-filter_complex", ExportEngine::stageBVideoFilterGraph(*access,
        {width,height}, {width,height}, {30,1}, count, profile), "-map", "[video]", "-fps_mode", "cfr",
        "-f", "rawvideo", "-pix_fmt", "rgba", output};
    QVERIFY(run(args));
    const auto decoded = readBytes(output);
    QCOMPARE(decoded.size(), qsizetype(count * width * height * 4));
    for (int frame = 0; frame < count; ++frame) {
        int identity = 0;
        for (int bit = 0; bit < 9; ++bit)
            if (static_cast<unsigned char>(decoded[(frame * width * height + width * 8 + bit * 6 + 3) * 4]) > 127) identity |= 1 << bit;
        QCOMPARE(identity, first + frame);
    }
}

void ExportTests::placesAdditionalVideosByLayout()
{
    using VideoComposition::layout;
    const QSize hd(1920, 1080);
    // Picture in picture: the main video fills the frame; each additional
    // video fits a box of 28 % down the right edge, 3 % margins.
    const auto pip = layout(VideoLayoutMode::PictureInPicture, hd, {hd, hd, QSize(1080, 1920)});
    QCOMPARE(pip.size(), 3);
    QCOMPARE(pip[0], QRect(0, 0, 1920, 1080));
    QCOMPARE(pip[1], QRect(1352, 32, 536, 300));
    QCOMPARE(pip[2], QRect(1720, 366, 168, 302));
    // Side by side: the main video in the left half, the others stacked in
    // the right half, each keeping its aspect ratio and centred.
    const auto sideBySide = layout(VideoLayoutMode::SideBySide, hd, {hd, QSize(1080, 1920)});
    QCOMPARE(sideBySide.size(), 2);
    QCOMPARE(sideBySide[0], QRect(0, 270, 960, 540));
    QCOMPARE(sideBySide[1], QRect(1136, 0, 606, 1080));
    const auto stacked = layout(VideoLayoutMode::SideBySide, hd, {hd, hd, hd});
    QCOMPARE(stacked[1], QRect(960, 0, 960, 540));
    QCOMPARE(stacked[2], QRect(960, 540, 960, 540));
    for (const auto &rects : {pip, sideBySide, stacked})
        for (const QRect &rect : rects)
            QVERIFY(rect.x() % 2 == 0 && rect.y() % 2 == 0 && rect.width() % 2 == 0 && rect.height() % 2 == 0);
    // Without additional videos side by side is the plain frame.
    QCOMPARE(layout(VideoLayoutMode::SideBySide, hd, {hd}), QVector<QRect>{QRect(0, 0, 1920, 1080)});
    QVERIFY(layout(VideoLayoutMode::PictureInPicture, hd, {hd, QSize()}).isEmpty());
    QVERIFY(layout(VideoLayoutMode::PictureInPicture, QSize(), {hd}).isEmpty());
}

void ExportTests::timesAdditionalVideosAgainstTheMainVideo()
{
    using VideoComposition::timing;
    // The helmet camera's video time is the main video's minus 2 s.
    const SyncTransform main{10.0, 1.0};
    auto result = timing(main, {12.0, 1.0}, 5.0, 10.0, 0.0, 20.0);
    QVERIFY(result.has_value());
    QCOMPARE(result->inputSeekSeconds, 2.0);
    QCOMPARE(result->timeFactor, 1.0);
    QCOMPARE(result->timeShift, -3.0); // its 3 s is the export's first frame
    // Its own start time is part of its timestamps.
    result = timing(main, {12.0, 1.0}, 5.0, 10.0, 1.5, 20.0);
    QVERIFY(result.has_value());
    QCOMPARE(result->inputSeekSeconds, 3.5);
    QCOMPARE(result->timeShift, -4.5);
    // A time scale: its clock runs at half the telemetry's rate.
    result = timing(main, {12.0, 0.5}, 5.0, 10.0, 0.0, 20.0);
    QVERIFY(result.has_value());
    QCOMPARE(result->timeFactor, 0.5);
    QCOMPARE(result->timeShift, -3.0); // its 6 s is the export's first frame
    QCOMPARE(result->inputSeekSeconds, 5.0);
    // Starting after the export starts: read from its beginning.
    result = timing(main, {20.0, 1.0}, 5.0, 10.0, 0.0, 20.0);
    QVERIFY(result.has_value());
    QCOMPARE(result->inputSeekSeconds, 0.0);
    QCOMPARE(result->timeShift, 5.0); // its first frame is the export's 5 s
    // No frame inside the export.
    QVERIFY(!timing(main, {12.0, 1.0}, 5.0, 10.0, 0.0, 3.0));
    QVERIFY(!timing(main, {-20.0, 1.0}, 5.0, 10.0, 0.0, 20.0));
    QVERIFY(!timing(main, {12.0, 0.0}, 5.0, 10.0, 0.0, 20.0));
    QVERIFY(!timing(main, {12.0, 1.0}, 5.0, 0.0, 0.0, 20.0));
}

void ExportTests::composesAnAdditionalVideoOnItsOwnTimeline()
{
    // A 128x32 main video and a 64x32 additional video at 25 fps whose
    // frames carry their number in white bars, side by side. Both files
    // start after 0 (2 s and 0.5 s), as camera files may.
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto run = [](const QStringList &args) {
        QProcess process; process.start(FfmpegTools::ffmpegPath(), args);
        if (!process.waitForFinished(30'000) || process.exitCode() != 0)
            qWarning().noquote() << process.readAllStandardError();
        return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
    };
    const auto numbered = [](int frames, int width, int height) {
        QByteArray pixels(frames * width * height * 3, '\0');
        for (int frame = 0; frame < frames; ++frame) for (int y = 0; y < height; ++y) {
            for (int bit = 0; bit < 9; ++bit) for (int x = bit * 6; x < bit * 6 + 6; ++x)
                for (int c = 0; c < 3; ++c) pixels[((frame * height + y) * width + x) * 3 + c] = (frame & (1 << bit)) ? '\xFF' : '\0';
            for (int x = 56; x < 62; ++x) for (int c = 0; c < 3; ++c) pixels[((frame * height + y) * width + x) * 3 + c] = '\xFF';
        }
        return pixels;
    };
    const auto mainRaw = directory.filePath("main.rgb"), main = directory.filePath("main.mp4");
    const auto extraRaw = directory.filePath("helmet.rgb"), extra = directory.filePath("helmet.mp4");
    const auto overlayRaw = directory.filePath("overlay.rgba"), overlay = directory.filePath("overlay.mkv");
    const auto output = directory.filePath("output.rgba");
    QVERIFY(writeBytes(mainRaw, numbered(180, 128, 32)));
    QVERIFY(writeBytes(extraRaw, numbered(75, 64, 32)));
    QVERIFY(run({"-v", "error", "-y", "-f", "rawvideo", "-pixel_format", "rgb24", "-video_size", "128x32", "-framerate", "30", "-i", mainRaw,
        "-c:v", "libx264", "-crf", "0", "-pix_fmt", "yuv420p", "-output_ts_offset", "2", main}));
    QVERIFY(run({"-v", "error", "-y", "-f", "rawvideo", "-pixel_format", "rgb24", "-video_size", "64x32", "-framerate", "25", "-i", extraRaw,
        "-c:v", "libx264", "-crf", "0", "-pix_fmt", "yuv420p", "-output_ts_offset", "0.5", extra}));
    constexpr int first = 60, count = 90;
    QVERIFY(writeBytes(overlayRaw, QByteArray(count * 128 * 32 * 4, '\0')));
    QVERIFY(run({"-v", "error", "-y", "-f", "rawvideo", "-pixel_format", "rgba", "-video_size", "128x32", "-framerate", "30", "-i", overlayRaw,
        "-c:v", "ffv1", "-pix_fmt", "bgra", overlay}));
    const auto mainInfo = MediaProbe::probe(main);
    const auto extraInfo = MediaProbe::probe(extra);
    QVERIFY(std::abs(extraInfo.videoStartTime - 0.5) < .001);
    // The helmet camera's video time is the main video's minus 1 s.
    const SyncTransform mainSync{0.0, 1.0};
    const auto timing = VideoComposition::timing(mainSync, {1.0, 1.0}, 2.0, 3.0, extraInfo.videoStartTime, extraInfo.videoDuration);
    QVERIFY(timing.has_value());
    const auto rects = VideoComposition::layout(VideoLayoutMode::SideBySide, {128, 32}, {mainInfo.videoSize, extraInfo.videoSize});
    QCOMPARE(rects[1], QRect(64, 0, 64, 32));
    StageBComposition composition{rects[0], {{rects[1], timing->timeFactor, timing->timeShift}}};
    const auto access = ExportEngine::stageBSourceAccess(mainInfo, {first, first + count - 1}, {30, 1});
    QVERIFY(access.has_value());
    const auto profile = ExportMediaProfile::derive(mainInfo, {128, 32}, {30, 1}, 1'000'000, "libx265");
    const QString graph = ExportEngine::stageBVideoFilterGraph(*access, mainInfo.videoSize, {128, 32}, {30, 1}, count, profile, composition);
    QStringList args{"-v", "error", "-y"};
    args += ExportEngine::stageBInputArguments(*access, main);
    args += QStringList{"-i", overlay};
    args += ExportEngine::stageBAdditionalInputArguments(*timing, extra);
    args += QStringList{"-filter_complex", graph, "-map", "[video]", "-fps_mode", "cfr", "-f", "rawvideo", "-pix_fmt", "rgba", output};
    QVERIFY(run(args));
    // The preflight runs the same graph shape with stand-in inputs.
    const QString preflight = ExportEngine::verifyCompositionFilters(FfmpegTools::ffmpegPath(),
        ExportEngine::stageBVideoFilterGraph({"0", "0", "0.1"}, {64, 64}, {64, 64}, {30, 1}, 3, profile,
            {VideoComposition::layout(VideoLayoutMode::SideBySide, {64, 64}, {mainInfo.videoSize, extraInfo.videoSize})[0],
             {{QRect(32, 16, 32, 16), 1.0, 0.0}}}), {}, 1);
    QVERIFY2(preflight.isEmpty(), qPrintable(preflight));
    const auto decoded = readBytes(output);
    QCOMPARE(decoded.size(), qsizetype(count * 128 * 32 * 4));
    const auto pixel = [&](int frame, int x, int y) {
        return static_cast<unsigned char>(decoded[((frame * 32 + y) * 128 + x) * 4]);
    };
    for (int frame = 0; frame < count; ++frame) {
        // Output frame k is main video time 2 + k/30 and helmet time
        // 1 + k/30, until the helmet video ends at its 3 s. Its frame j is at
        // output time (j - 25)/25; the overlay takes the last frame whose time,
        // rounded to the nearest output frame, is not after k.
        const bool shown = frame < 60;
        QCOMPARE(pixel(frame, 64 + 59, 16) > 127, shown);
        if (!shown) continue;
        int expected = 25;
        while (std::lround((expected + 1 - 25) * 30.0 / 25.0) <= frame) ++expected;
        int identity = 0;
        for (int bit = 0; bit < 9; ++bit) if (pixel(frame, 64 + bit * 6 + 3, 16) > 127) identity |= 1 << bit;
        QCOMPARE(identity, expected);
    }
    // The main video keeps its aspect ratio in the left half: 64x16 at y 8,
    // so its marker bar (x 56 to 61 of 128) is at x 28 to 30.
    QVERIFY(pixel(0, 29, 4) < 64);
    QVERIFY(pixel(0, 29, 16) > 127);
}

void ExportTests::plansBoundedStageBSourceAccess()
{
    MediaInfo source;
    source.timeBase = {1, 60'000};
    source.videoStartTicks = 120'000;
    const MediaRational rate{60'000, 1'001};
    const auto access = ExportEngine::stageBSourceAccess(source, {60, 359}, rate);
    QVERIFY(access.has_value());
    QCOMPARE(access->inputSeekTimestamp, QStringLiteral("0"));
    QCOMPARE(access->trimStartTimestamp, QStringLiteral("3.001"));
    QCOMPARE(access->trimEndTimestamp, QStringLiteral("8.006"));

    const auto late = ExportEngine::stageBSourceAccess(source, {14'388, 16'186}, rate);
    QVERIFY(late.has_value());
    QCOMPARE(late->inputSeekTimestamp, QStringLiteral("237.0398"));
    QCOMPARE(late->trimStartTimestamp, QStringLiteral("242.0398"));
    QCOMPARE(late->trimEndTimestamp, QStringLiteral("272.053116666666"));
}

void ExportTests::resolvesExplicitExportFormats()
{
    const QList<QSize> sizes = ExportFormat::resolutionOptions({3840, 2160});
    QCOMPARE(sizes.first(), QSize(3840, 2160));
    QVERIFY(sizes.contains(QSize(1920, 1080)));
    for (const QSize &size : sizes) {
        QVERIFY(size.width() <= 3840 && size.height() <= 2160);
        QCOMPARE(size.width() % 2, 0); QCOMPARE(size.height() % 2, 0);
    }
    const MediaRational ntsc{60'000, 1'001};
    const QList<MediaRational> rates = ExportFormat::frameRateOptions(ntsc);
    QCOMPARE(rates.size(), 2); QCOMPARE(rates.at(1).numerator, qint64(30'000));
    QCOMPARE(rates.at(1).denominator, qint64(1'001));
    const qint64 recommended = ExportFormat::recommendedVideoBitrate({1920, 1080}, {30, 1});
    QVERIFY(recommended >= 10'000'000 && recommended <= 14'000'000);
    QVERIFY(ExportFormat::recommendedVideoBitrate({1920, 1080}, {60'000, 1'001}) >= 15'000'000);
    QVERIFY(ExportFormat::recommendedVideoBitrate({1920, 1080}, {60'000, 1'001}) <= 20'000'000);
    QVERIFY(ExportFormat::recommendedVideoBitrate({2560, 1440}, {60, 1}) >= 26'000'000);
    QVERIFY(ExportFormat::recommendedVideoBitrate({2560, 1440}, {60, 1}) <= 34'000'000);
    QVERIFY(ExportFormat::recommendedVideoBitrate({3840, 2160}, {30, 1}) >= 30'000'000);
    QVERIFY(ExportFormat::recommendedVideoBitrate({3840, 2160}, {30, 1}) <= 40'000'000);
    const qint64 fourK60 = ExportFormat::recommendedVideoBitrate({3840, 2160}, {60'000, 1'001});
    QVERIFY(fourK60 >= 45'000'000 && fourK60 <= 60'000'000);
    QVERIFY(fourK60 < 120'000'000);
    QCOMPARE(ExportFormat::bitrateForQuality("smaller", {1920, 1080}, {30, 1}), qRound64(recommended * .7));
    const qint64 high = ExportFormat::bitrateForQuality("high", {3840, 2160}, {60'000, 1'001});
    QVERIFY(ExportFormat::bitrateForQuality("smaller", {3840, 2160}, {60'000, 1'001}) < fourK60);
    QVERIFY(fourK60 < high); QVERIFY(ExportFormat::validCustomBitrate(high));
    QVERIFY(!ExportFormat::validCustomBitrate(0));
    QVERIFY(ExportFormat::validCustomBitrate(121'000'000));
    QVERIFY(!ExportFormat::validCustomBitrate(501'000'000));
    QVERIFY(ExportFormat::validCustomBitrate(10'000'000));
    QVERIFY(ExportFormat::estimatedBytes(10'000'000, true, 60) > 75'000'000);
    // KAN-133: an estimate past qint64 saturates exactly; one just below it still rounds.
    QCOMPARE(ExportFormat::estimatedBytes(500'000'000, true, 1e15), std::numeric_limits<qint64>::max());
    QVERIFY(ExportFormat::estimatedBytes(500'000'000, true, 1e11) > 0);
    QCOMPARE(ExportFormat::estimatedBytes(10'000'000, true, std::numeric_limits<double>::infinity()), qint64(0));
    QCOMPARE(ExportFormat::formatEstimatedSize(qint64(850) * 1024 * 1024), QStringLiteral("~850 MiB"));
    QCOMPARE(ExportFormat::formatEstimatedSize(qint64(46) * 1024 * 1024 * 1024 / 10), QStringLiteral("~4.60 GiB"));
    QCOMPARE(ExportEngine::frameRangeFromInclusiveFrames(0, 299)->frameCount(), qint64(300));

    const QList<QSize> fiveK = ExportFormat::resolutionOptions({5312, 2988});
    QCOMPARE(fiveK.first(), QSize(5312, 2988));
    const QList<QSize> eightSeven = ExportFormat::resolutionOptions({5312, 4648});
    QCOMPARE(eightSeven.first(), QSize(5312, 4648));
    QVERIFY(eightSeven.contains(QSize(3840, 3360)));
    QVERIFY(eightSeven.contains(QSize(2560, 2240)));
    const QList<QSize> eightK = ExportFormat::resolutionOptions({7680, 4320});
    QCOMPARE(eightK.first(), QSize(7680, 4320));

    const QList<QPair<QSize, MediaRational>> bitrateCases{
        {{1280, 720}, {30, 1}}, {{1920, 1080}, {30, 1}},
        {{3840, 2160}, {30, 1}}, {{3840, 2160}, {60, 1}},
        {{5312, 2988}, {60, 1}}, {{5312, 4648}, {30, 1}},
        {{7680, 4320}, {30, 1}}, {{7680, 4320}, {60, 1}},
    };
    long double previousPixelRate = 0;
    qint64 previousBitrate = 0;
    for (const auto &[size, rate] : bitrateCases) {
        const long double pixelRate = static_cast<long double>(size.width()) * size.height()
            * rate.value();
        const qint64 bitrate = ExportFormat::recommendedVideoBitrate(size, rate);
        QVERIFY(bitrate > 0 && bitrate <= ExportFormat::maximumCustomVideoBitrate);
        if (pixelRate > previousPixelRate) QVERIFY(bitrate > previousBitrate);
        previousPixelRate = pixelRate;
        previousBitrate = bitrate;
    }
    QVERIFY(ExportFormat::recommendedVideoBitrate({5312, 2988}, {60, 1}) > fourK60);
    QVERIFY(ExportFormat::recommendedVideoBitrate({7680, 4320}, {60, 1})
            > ExportFormat::recommendedVideoBitrate({7680, 4320}, {30, 1}));
    QVERIFY(ExportFormat::recommendedVideoBitrate({3840, 2160}, {30, 1}, 10)
            > ExportFormat::recommendedVideoBitrate({3840, 2160}, {30, 1}, 8));
    QCOMPARE(ExportFormat::rgbaFrameBytes({3840, 2160}), std::optional<qint64>(33'177'600));
    QCOMPARE(ExportFormat::rgbaFrameBytes({7680, 4320}), std::optional<qint64>(132'710'400));
    QVERIFY(!ExportFormat::rgbaFrameBytes(
        {std::numeric_limits<int>::max(), std::numeric_limits<int>::max()}).has_value());
}

void ExportTests::derivesSourceDrivenExportProfiles()
{
    MediaInfo source;
    source.bitDepth = 8;
    source.pixelFormat = QStringLiteral("yuv420p");
    source.sourceColorClass = SourceColorClass::Sdr;
    source.colorRange = QStringLiteral("tv");
    source.colorSpace = QStringLiteral("bt709");
    source.colorTransfer = QStringLiteral("bt709");
    source.colorPrimaries = QStringLiteral("bt709");
    const ExportMediaProfile eightBit = ExportMediaProfile::derive(
        source, {5312, 2988}, {60'000, 1001}, 80'000'000, QStringLiteral("libx265"));
    QVERIFY(eightBit.supported);
    QCOMPARE(eightBit.outputPixelFormat, QStringLiteral("yuv420p"));
    QCOMPARE(eightBit.outputBitDepth, 8);
    QCOMPARE(eightBit.encoderProfile, QStringLiteral("main"));
    QCOMPARE(eightBit.outputSize, QSize(5312, 2988));
    QVERIFY(eightBit.acceptsOutputPixelFormat(QStringLiteral("yuv420p")));
    QVERIFY(!eightBit.acceptsOutputPixelFormat(QStringLiteral("yuvj420p")));
    MediaInfo fullRangeSource = source;
    fullRangeSource.colorRange = QStringLiteral("pc");
    const ExportMediaProfile fullRange = ExportMediaProfile::derive(
        fullRangeSource, {3840, 2160}, {60'000, 1001}, 50'000'000,
        QStringLiteral("hevc_videotoolbox"));
    QVERIFY(fullRange.acceptsOutputPixelFormat(QStringLiteral("yuvj420p")));

    source.bitDepth = 10;
    source.pixelFormat = QStringLiteral("yuv420p10le");
    const ExportMediaProfile tenBit = ExportMediaProfile::derive(
        source, {7680, 4320}, {30, 1}, 120'000'000, QStringLiteral("libx265"));
    QVERIFY(tenBit.supported);
    QCOMPARE(tenBit.outputBitDepth, 10);
    QCOMPARE(tenBit.outputPixelFormat, QStringLiteral("yuv420p10le"));
    QCOMPARE(tenBit.encoderProfile, QStringLiteral("main10"));
    QCOMPARE(tenBit.colorTransfer, QStringLiteral("bt709"));
    QVERIFY(tenBit.acceptsOutputPixelFormat(QStringLiteral("yuv420p10le")));
    const ExportMediaProfile videoToolbox = ExportMediaProfile::derive(
        source, {3840, 2160}, {30, 1}, 40'000'000,
        QStringLiteral("hevc_videotoolbox"));
    QCOMPARE(videoToolbox.outputPixelFormat, QStringLiteral("p010le"));

    source.sourceColorClass = SourceColorClass::HdrHlg;
    const ExportMediaProfile hdr = ExportMediaProfile::derive(
        source, {3840, 2160}, {30, 1}, 40'000'000, QStringLiteral("libx265"));
    QVERIFY(!hdr.supported);
    QVERIFY(hdr.error.contains(QStringLiteral("not yet supported")));
    source.sourceColorClass = SourceColorClass::Sdr;
    source.bitDepth.reset();
    const ExportMediaProfile unknown = ExportMediaProfile::derive(
        source, {3840, 2160}, {30, 1}, 40'000'000, QStringLiteral("libx265"));
    QVERIFY(!unknown.supported);
    QVERIFY(unknown.error.contains(QStringLiteral("unknown")));
}

void ExportTests::rejectsUnsupportedExportDisplayTransforms()
{
    MediaInfo source;
    source.bitDepth = 8;
    source.pixelFormat = QStringLiteral("yuv420p");
    source.sourceColorClass = SourceColorClass::Sdr;
    const auto derive = [&source] {
        return ExportMediaProfile::derive(
            source, {1920, 1080}, {30, 1}, 8'000'000, QStringLiteral("libx265"));
    };

    source.rotationDegrees = 90;
    QVERIFY(!derive().supported);
    source.rotationDegrees = -90;
    QVERIFY(!derive().supported);
    source.rotationDegrees.reset();
    source.sampleAspectRatio = {4, 3};
    QVERIFY(!derive().supported);
    source.sampleAspectRatio = {8, 9};
    QVERIFY(!derive().supported);
    source.sampleAspectRatio = {};
    QVERIFY(derive().supported);
    source.rotationDegrees = 0;
    source.sampleAspectRatio = {1, 1};
    QVERIFY(derive().supported);
}

void ExportTests::validatesHighResolutionCapabilitiesAndCache()
{
    const RendererCapabilityResult supported = TelemetryFrameRenderer::evaluateCapability(
        {7680, 4320}, 8192, QStringLiteral("Synthetic RHI"));
    QVERIFY(supported.supported);
    QCOMPARE(supported.frameBytes, qint64(132'710'400));
    QCOMPARE(supported.pixelCount, qint64(33'177'600));
    const RendererCapabilityResult rejected = TelemetryFrameRenderer::evaluateCapability(
        {7680, 4320}, 4096, QStringLiteral("Synthetic RHI"));
    QVERIFY(!rejected.supported);
    QVERIFY(rejected.error.contains(QStringLiteral("7680")));
    QVERIFY(rejected.error.contains(QStringLiteral("4096")));
    const RendererCapabilityResult overflow = TelemetryFrameRenderer::evaluateCapability(
        {std::numeric_limits<int>::max(), std::numeric_limits<int>::max()},
        std::numeric_limits<int>::max(), QStringLiteral("Synthetic RHI"));
    QVERIFY(!overflow.supported);

    EncoderCapabilityCache cache;
    EncoderProfileRequest request{
        QStringLiteral("/ffmpeg"), QStringLiteral("libx265"), {5312, 2988},
        {60'000, 1001}, QStringLiteral("yuv420p10le"), 10, QStringLiteral("main10")};
    int probes = 0;
    const auto probe = [&probes](const EncoderProfileRequest &) {
        ++probes;
        return EncoderProfileSupport{true, false, {}};
    };
    const EncoderProfileSupport first = cache.verify(request, probe);
    const EncoderProfileSupport second = cache.verify(request, probe);
    QVERIFY(first.supported && !first.cacheHit);
    QVERIFY(second.supported && second.cacheHit);
    QCOMPARE(probes, 1);
    request.size = {7680, 4320};
    static_cast<void>(cache.verify(request, probe));
    QCOMPARE(probes, 2);
    QCOMPARE(cache.size(), qsizetype(2));
}

void ExportTests::preservesTenBitSdrThroughComposition()
{
    const QString ffmpeg = FfmpegTools::ffmpegPath();
    if (ffmpeg.isEmpty()) QSKIP("FFmpeg is unavailable for the 10-bit SDR composition test.");
    QProcess encoderQuery;
    encoderQuery.start(ffmpeg, {QStringLiteral("-hide_banner"), QStringLiteral("-h"),
                                QStringLiteral("encoder=libx265")});
    if (!encoderQuery.waitForStarted() || !encoderQuery.waitForFinished(10'000)
        || encoderQuery.exitCode() != 0) {
        QSKIP("This FFmpeg build does not provide libx265 Main10.");
    }
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = directory.filePath(QStringLiteral("source-10bit.mp4"));
    const QString overlay = directory.filePath(QStringLiteral("overlay.mkv"));
    const QString output = directory.filePath(QStringLiteral("composed-10bit.mp4"));
    const auto runFfmpeg = [&ffmpeg](const QStringList &arguments) {
        QProcess process;
        process.start(ffmpeg, arguments);
        QVERIFY2(process.waitForStarted(), qPrintable(process.errorString()));
        QVERIFY2(process.waitForFinished(120'000), qPrintable(process.errorString()));
        QCOMPARE(process.exitStatus(), QProcess::NormalExit);
        QVERIFY2(process.exitCode() == 0, process.readAllStandardError().constData());
    };
    runFfmpeg({"-hide_banner", "-loglevel", "error", "-y", "-f", "lavfi", "-i",
               "color=c=blue:s=64x64:r=30:d=0.1", "-f", "lavfi", "-i",
               "sine=frequency=440:sample_rate=48000:duration=0.1", "-vf",
               "format=pix_fmts=yuv420p10le", "-frames:v", "3", "-c:v", "libx265",
               "-preset", "ultrafast", "-profile:v", "main10", "-pix_fmt", "yuv420p10le",
               "-color_range", "tv", "-colorspace", "bt709", "-color_trc", "bt709",
               "-color_primaries", "bt709", "-x265-params",
               "colorprim=bt709:transfer=bt709:colormatrix=bt709:range=limited",
               "-c:a", "aac", source});
    runFfmpeg({"-hide_banner", "-loglevel", "error", "-y", "-f", "lavfi", "-i",
               "color=c=red@0.25:s=64x64:r=30:d=0.1,format=rgba", "-frames:v", "3",
               "-c:v", "ffv1", "-pix_fmt", "bgra", overlay});
    runFfmpeg({"-hide_banner", "-loglevel", "error", "-y", "-i", source, "-i", overlay,
               "-filter_complex",
               "[0:v][1:v]overlay=0:0:shortest=1:repeatlast=0:eof_action=endall:format=yuv420p10[composited];[composited]format=pix_fmts=yuv420p10le[video]",
               "-map", "[video]", "-map", "0:a", "-c:v", "libx265", "-preset", "ultrafast",
               "-profile:v", "main10", "-pix_fmt", "yuv420p10le", "-color_range", "tv",
               "-colorspace", "bt709", "-color_trc", "bt709", "-color_primaries", "bt709",
               "-x265-params", "colorprim=bt709:transfer=bt709:colormatrix=bt709:range=limited",
               "-c:a", "copy", output});
    const MediaInfo info = MediaProbe::probe(output, {}, true, -1, {}, {}, true);
    QCOMPARE(info.videoCodec, QStringLiteral("hevc"));
    QCOMPARE(info.videoSize, QSize(64, 64));
    QCOMPARE(info.bitDepth, std::optional<int>(10));
    QCOMPARE(info.pixelFormat, QStringLiteral("yuv420p10le"));
    QVERIFY(info.videoCodecProfile.contains(QStringLiteral("10")));
    QCOMPARE(info.colorRange, QStringLiteral("tv"));
    QCOMPARE(info.colorSpace, QStringLiteral("bt709"));
    QCOMPARE(info.colorTransfer, QStringLiteral("bt709"));
    QCOMPARE(info.colorPrimaries, QStringLiteral("bt709"));
    QCOMPARE(info.sourceColorClass, SourceColorClass::Sdr);
    QVERIFY(info.averageFrameRate.isEquivalentTo({30, 1}));
    QVERIFY(!info.audioCodecs.isEmpty());
}

void ExportTests::preservesTenBitFullRangeColorThroughVideoToolboxExport()
{
    if (qEnvironmentVariableIntValue("FLAPPEDEAR_SKIP_HARDWARE_TESTS") == 1) {
        QSKIP("Hardware encoder validation is explicitly excluded from this cloud/synthetic run; validate VideoToolbox locally.");
    }
    const QString ffmpeg = FfmpegTools::ffmpegPath();
    if (ffmpeg.isEmpty()) QSKIP("FFmpeg is unavailable for the Main10 color-fidelity test.");
    QProcess encoderQuery;
    encoderQuery.setProcessChannelMode(QProcess::MergedChannels);
    encoderQuery.start(ffmpeg, {QStringLiteral("-hide_banner"), QStringLiteral("-h"),
                                QStringLiteral("encoder=hevc_videotoolbox")});
    // KAN-176: FFmpeg exits 0 for an unknown encoder and only prints
    // "Codec '...' is not recognized by FFmpeg.", so the exit code alone does not skip.
    if (!encoderQuery.waitForStarted() || !encoderQuery.waitForFinished(10'000)
        || encoderQuery.exitCode() != 0
        || encoderQuery.readAll().contains("is not recognized")) {
        QSKIP("This FFmpeg build does not provide VideoToolbox HEVC encoding.");
    }

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    constexpr int width = 320;
    constexpr int height = 192;
    constexpr int frameCount = 3;
    const QString size = QStringLiteral("%1x%2").arg(width).arg(height);
    const QString sourceRaw = directory.filePath(QStringLiteral("source.rgb"));
    const QString overlayRaw = directory.filePath(QStringLiteral("overlay.rgba"));
    const QString source = directory.filePath(QStringLiteral("source-main10.mp4"));
    const QString overlay = directory.filePath(QStringLiteral("overlay.mkv"));
    const QString output = directory.filePath(QStringLiteral("output-main10.mp4"));
    const QString referenceRaw = directory.filePath(QStringLiteral("reference.rgb"));
    const QString decodedRaw = directory.filePath(QStringLiteral("decoded.rgb"));
    constexpr std::array<std::array<uchar, 3>, 8> colors{{
        {128, 128, 128}, {220, 32, 32}, {32, 200, 64}, {32, 64, 220},
        {198, 134, 105}, {16, 20, 24}, {240, 240, 220}, {32, 200, 200},
    }};
    QByteArray sourcePixels(width * height * 3 * frameCount, '\0');
    for (int frame = 0; frame < frameCount; ++frame) {
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const int tile = (y >= height / 2 ? 4 : 0) + qMin(3, x * 4 / width);
                const qsizetype offset = (qsizetype(frame) * width * height
                                          + qsizetype(y) * width + x) * 3;
                for (int component = 0; component < 3; ++component) {
                    sourcePixels[offset + component] = static_cast<char>(colors[tile][component]);
                }
            }
        }
    }
    QVERIFY(writeBytes(sourceRaw, sourcePixels));
    QVERIFY(writeBytes(overlayRaw, QByteArray(width * height * 4 * frameCount, '\0')));

    const auto runFfmpeg = [&ffmpeg](const QStringList &arguments) {
        QProcess process;
        process.start(ffmpeg, arguments);
        QVERIFY2(process.waitForStarted(), qPrintable(process.errorString()));
        QVERIFY2(process.waitForFinished(120'000), qPrintable(process.errorString()));
        QCOMPARE(process.exitStatus(), QProcess::NormalExit);
        QVERIFY2(process.exitCode() == 0, process.readAllStandardError().constData());
    };
    runFfmpeg({"-hide_banner", "-loglevel", "error", "-y", "-f", "rawvideo",
               "-pixel_format", "rgb24", "-video_size", size, "-framerate", "30", "-i",
               sourceRaw, "-frames:v", QString::number(frameCount), "-vf",
               "format=pix_fmts=yuv420p10le", "-c:v", "libx265", "-preset", "ultrafast",
               "-profile:v", "main10", "-pix_fmt", "yuv420p10le", "-color_range", "pc",
               "-colorspace", "bt709", "-color_trc", "bt709", "-color_primaries", "bt709",
               "-x265-params",
               "lossless=1:colorprim=bt709:transfer=bt709:colormatrix=bt709:range=full", source});
    runFfmpeg({"-hide_banner", "-loglevel", "error", "-y", "-f", "rawvideo",
               "-pixel_format", "rgba", "-video_size", size, "-framerate", "30", "-i",
               overlayRaw, "-frames:v", QString::number(frameCount), "-an", "-c:v", "ffv1",
               "-pix_fmt", "bgra", "-f", "matroska", overlay});
    const MediaInfo sourceInfo = MediaProbe::probe(source);
    const ExportMediaProfile profile = ExportMediaProfile::derive(
        sourceInfo, {width, height}, {30, 1}, 5'000'000,
        QStringLiteral("hevc_videotoolbox"));
    QVERIFY2(profile.supported, qPrintable(profile.error));
    const auto stageBAccess = ExportEngine::stageBSourceAccess(sourceInfo, {0, frameCount - 1}, {30, 1});
    QVERIFY(stageBAccess.has_value());
    const QString stageBFilter = ExportEngine::stageBVideoFilterGraph(
        *stageBAccess, sourceInfo.videoSize, {width, height}, {30, 1}, frameCount, profile);
    runFfmpeg({"-hide_banner", "-loglevel", "error", "-y", "-i", source, "-i", overlay,
               "-filter_complex", stageBFilter,
               "-map", "[video]", "-fps_mode:v", "cfr", "-c:v", "hevc_videotoolbox",
               "-b:v", "5000000", "-tag:v", "hvc1", "-profile:v", "main10", "-pix_fmt",
               "p010le", "-color_range", "pc", "-colorspace", "bt709", "-color_trc",
               "bt709", "-color_primaries", "bt709", output});
    runFfmpeg({"-hide_banner", "-loglevel", "error", "-y", "-hwaccel", "none", "-i",
               source, "-frames:v", "1", "-f", "rawvideo", "-pix_fmt", "rgb24", referenceRaw});
    runFfmpeg({"-hide_banner", "-loglevel", "error", "-y", "-hwaccel", "none", "-i",
               output, "-frames:v", "1", "-f", "rawvideo", "-pix_fmt", "rgb24", decodedRaw});

    QFile referenceFile(referenceRaw);
    QFile decodedFile(decodedRaw);
    QVERIFY(referenceFile.open(QIODevice::ReadOnly));
    QVERIFY(decodedFile.open(QIODevice::ReadOnly));
    const QByteArray reference = referenceFile.readAll();
    const QByteArray decoded = decodedFile.readAll();
    QCOMPARE(reference.size(), width * height * 3);
    QCOMPARE(decoded.size(), reference.size());
    std::array<qint64, 3> absoluteError{};
    for (qsizetype offset = 0; offset < reference.size(); offset += 3) {
        for (int component = 0; component < 3; ++component) {
            absoluteError[component] += std::abs(
                int(static_cast<uchar>(reference[offset + component]))
                - int(static_cast<uchar>(decoded[offset + component])));
        }
    }
    const double pixelCount = width * height;
    std::array<double, 3> meanAbsoluteErrors{};
    for (int component = 0; component < 3; ++component) {
        meanAbsoluteErrors[component] = absoluteError[component] / pixelCount;
        QVERIFY2(meanAbsoluteErrors[component] <= 12.0,
                 qPrintable(QStringLiteral("RGB component %1 mean absolute error %2 exceeds 12")
                                .arg(component).arg(meanAbsoluteErrors[component], 0, 'f', 3)));
    }
    qInfo().noquote() << QStringLiteral(
        "Main10 VideoToolbox RGB MAE: R=%1 G=%2 B=%3 (limit 12.0)")
                             .arg(meanAbsoluteErrors[0], 0, 'f', 3)
                             .arg(meanAbsoluteErrors[1], 0, 'f', 3)
                             .arg(meanAbsoluteErrors[2], 0, 'f', 3);

    const MediaInfo info = MediaProbe::probe(output);
    QCOMPARE(info.videoCodec, QStringLiteral("hevc"));
    QCOMPARE(info.bitDepth, std::optional<int>(10));
    QCOMPARE(info.pixelFormat, QStringLiteral("yuv420p10le"));
    QCOMPARE(info.colorRange, QStringLiteral("pc"));
    QCOMPARE(info.colorSpace, QStringLiteral("bt709"));
    QCOMPARE(info.colorTransfer, QStringLiteral("bt709"));
    QCOMPARE(info.colorPrimaries, QStringLiteral("bt709"));
    QVERIFY(info.averageFrameRate.isEquivalentTo({30, 1}));
}

void ExportTests::preservesExactExportRateRationals()
{
    QVERIFY((MediaRational{24'000, 1'001}.isEquivalentTo({24'000, 1'001})));
    QVERIFY((MediaRational{30'000, 1'001}.isEquivalentTo({60'000, 2'002})));
    QVERIFY((MediaRational{60'000, 1'001}.isEquivalentTo({60'000, 1'001})));
    QVERIFY((!MediaRational{30'000, 1'001}.isEquivalentTo({30, 1})));
    QVERIFY(qAbs(ExportEngine::outputDuration(600, {30'000, 1'001}) - 20.02) < 0.0000001);
    QVERIFY(qAbs(ExportEngine::outputDuration(60, {60'000, 1'001}) - 1.001) < 0.0000001);
}

void ExportTests::floorsConvertedFrameCounts_data()
{
    QTest::addColumn<qint64>("ticks");
    QTest::addColumn<qint64>("timeBaseNumerator");
    QTest::addColumn<qint64>("timeBaseDenominator");
    QTest::addColumn<qint64>("rateNumerator");
    QTest::addColumn<qint64>("rateDenominator");
    QTest::addColumn<qint64>("expectedCount"); // Zero means no schedulable range.
    QTest::newRow("residual-denominator") << qint64(1001) << qint64(1) << qint64(30000)
        << qint64(30) << qint64(1) << qint64(1);
    QTest::newRow("odd-half-rate") << qint64(101) << qint64(1) << qint64(60)
        << qint64(30) << qint64(1) << qint64(50);
    QTest::newRow("both-residual-denominators") << qint64(101) << qint64(1) << qint64(60)
        << qint64(30000) << qint64(1001) << qint64(50);
    QTest::newRow("one-frame") << qint64(1) << qint64(1) << qint64(30)
        << qint64(30) << qint64(1) << qint64(1);
    QTest::newRow("sub-frame") << qint64(1) << qint64(1) << qint64(60)
        << qint64(30) << qint64(1) << qint64(0);
    QTest::newRow("zero-duration") << qint64(0) << qint64(1) << qint64(60)
        << qint64(30) << qint64(1) << qint64(0);
    QTest::newRow("negative-duration") << qint64(-1) << qint64(1) << qint64(60)
        << qint64(30) << qint64(1) << qint64(0);
    QTest::newRow("invalid-time-base") << qint64(101) << qint64(1) << qint64(0)
        << qint64(30) << qint64(1) << qint64(0);
    QTest::newRow("invalid-rate") << qint64(101) << qint64(1) << qint64(60)
        << qint64(30) << qint64(0) << qint64(0);
    QTest::newRow("overflow") << std::numeric_limits<qint64>::max() << qint64(1) << qint64(1)
        << qint64(2) << qint64(1) << qint64(0);
    QTest::newRow("cancel-before-multiplication") << std::numeric_limits<qint64>::max()
        << qint64(2) << qint64(2) << qint64(1) << qint64(1)
        << std::numeric_limits<qint64>::max();
}

void ExportTests::floorsConvertedFrameCounts()
{
    QFETCH(qint64, ticks);
    QFETCH(qint64, timeBaseNumerator);
    QFETCH(qint64, timeBaseDenominator);
    QFETCH(qint64, rateNumerator);
    QFETCH(qint64, rateDenominator);
    QFETCH(qint64, expectedCount);
    MediaInfo source;
    source.timeBase = {timeBaseNumerator, timeBaseDenominator};
    source.videoDurationTicks = ticks;
    source.averageFrameRate = {60, 1};
    for (const qsizetype metadataCount : {qsizetype(0), qsizetype(101)}) {
        source.videoFrameCount = metadataCount;
        const auto range = ExportEngine::fullVideoFrameRange(source, {rateNumerator, rateDenominator});
        QCOMPARE(range.has_value(), expectedCount > 0);
        if (range) {
            QCOMPARE(range->firstFrame, qint64(0));
            QCOMPARE(range->frameCount(), expectedCount);
        }
    }
}

void ExportTests::schedulesFrameAddressedExportRangesExactly()
{
    const MediaRational ntsc{60'000, 1'001};
    MediaInfo source;
    source.frameRate = ntsc;
    source.averageFrameRate = ntsc;
    source.timeBase = {1, 60'000};
    source.videoDurationTicks = 78'350'272;
    source.videoFrameCount = 78'272;

    const auto fullRange = ExportEngine::fullVideoFrameRange(source, ntsc);
    QVERIFY(fullRange.has_value());
    QCOMPARE(fullRange->firstFrame, qint64(0));
    QCOMPARE(fullRange->lastFrame, qint64(78'271));
    QCOMPARE(fullRange->frameCount(), qint64(78'272));
    MediaInfo fallbackSource = source;
    fallbackSource.videoFrameCount = 0;
    const auto fallbackRange = ExportEngine::fullVideoFrameRange(fallbackSource, ntsc);
    QVERIFY(fallbackRange.has_value());
    QCOMPARE(fallbackRange->frameCount(), qint64(78'272));
    const auto halfRateRange = ExportEngine::fullVideoFrameRange(source, {30'000, 1'001});
    QVERIFY(halfRateRange.has_value());
    QCOMPARE(halfRateRange->frameCount(), qint64(39'136));

    const auto regressionRange = ExportEngine::frameRangeForSourceTimecode(
        source, ntsc, QStringLiteral("00:00:00:00"), QStringLiteral("00:21:44:31"));
    QVERIFY(regressionRange.has_value());
    QCOMPARE(regressionRange->frameCount(), qint64(78'272));
    QCOMPARE(ExportEngine::formatSmpteTimecode(regressionRange->lastFrame, ntsc),
             QStringLiteral("00:21:44:31"));

    const auto inclusiveRange = ExportEngine::frameRangeFromInclusiveFrames(100, 199);
    QVERIFY(inclusiveRange.has_value());
    QCOMPARE(inclusiveRange->frameCount(), qint64(100));
    const auto singleFrame = ExportEngine::frameRangeFromInclusiveFrames(100, 100);
    QVERIFY(singleFrame.has_value());
    QCOMPARE(singleFrame->frameCount(), qint64(1));

    for (const MediaRational &rate : {MediaRational{24, 1}, MediaRational{25, 1},
                                      MediaRational{30, 1}, MediaRational{30'000, 1'001},
                                      MediaRational{50, 1}, MediaRational{60'000, 1'001}}) {
        const qint64 frame = rate.numerator == 60'000 ? 78'271 : 12'345;
        const QString timecode = ExportEngine::formatSmpteTimecode(frame, rate);
        const auto parsed = ExportEngine::parseSmpteTimecode(timecode, rate);
        QVERIFY2(parsed.has_value(), qPrintable(timecode));
        QCOMPARE(*parsed, frame);
    }
    QVERIFY(!ExportEngine::parseSmpteTimecode(QStringLiteral("00:00:00:60"), ntsc));
    QVERIFY(!ExportEngine::parseSmpteTimecode(QStringLiteral("not-a-timecode"), ntsc));
}

void ExportTests::boundsExportValidationAndWorkerDiagnostics()
{
    // KAN-148, 1: final validation reads every packet, so its timeout grows
    // with the output. A 13 GB export at 100 MB/s needs about 130 s.
    QCOMPARE(ExportEngine::finalValidationTimeoutMilliseconds(0), 30'000);
    QCOMPARE(ExportEngine::finalValidationTimeoutMilliseconds(-5), 30'000);
    QVERIFY(ExportEngine::finalValidationTimeoutMilliseconds(13'000'000'000LL) > 130'000 * 3);
    QCOMPARE(ExportEngine::finalValidationTimeoutMilliseconds(std::numeric_limits<qint64>::max()), 2 * 3600 * 1000);

    // 2: an FFmpeg tail as large as the GUI's whole message limit is cut to
    // its end, where the real error is, so the worker's message stays valid.
    QString tail;
    while (tail.toUtf8().size() < ProcessOutputLimits::ffmpegDiagnosticTailBytes)
        tail += QStringLiteral("frame=  1200 fps= 60 q=28.0 size=   10240KiB time=00:00:20.00 bitrate=4194.3kbits/s ż\n");
    tail += QStringLiteral("Error while encoding: No space left on device");
    const QString bounded = utf8Tail(tail, ProcessOutputLimits::workerMessageFieldBytes);
    QVERIFY(bounded.toUtf8().size() <= ProcessOutputLimits::workerMessageFieldBytes);
    QVERIFY(bounded.endsWith(QStringLiteral("No space left on device")));
    QVERIFY(bounded.startsWith(QStringLiteral("[… earlier output omitted]")));
    QVERIFY(!bounded.contains(QChar::ReplacementCharacter)); // cut on a character boundary
    QCOMPARE(utf8Tail(QStringLiteral("short"), 64), QStringLiteral("short"));

    // 3: start_pts is signed; absent is unknown, not zero.
    const auto streamJson = [](const QByteArray &start) {
        return QByteArray(R"({"format":{"duration":"10.0"},"streams":[{"codec_type":"video","codec_name":"hevc","width":32,"height":32,"r_frame_rate":"30/1","avg_frame_rate":"30/1","time_base":"1/30")")
            + start + "}]}";
    };
    const auto negative = MediaProbe::parseJson(streamJson(R"(,"start_pts":-2)"));
    QVERIFY(negative.videoStartKnown);
    QCOMPARE(negative.videoStartTicks, qint64(-2));
    const auto zero = MediaProbe::parseJson(streamJson(R"(,"start_pts":0)"));
    QVERIFY(zero.videoStartKnown);
    QCOMPARE(zero.videoStartTicks, qint64(0));
    const auto missing = MediaProbe::parseJson(streamJson(""));
    QVERIFY(!missing.videoStartKnown);
    const auto text = MediaProbe::parseJson(streamJson(R"(,"start_pts":"-1001")"));
    QVERIFY(text.videoStartKnown);
    QCOMPARE(text.videoStartTicks, qint64(-1001));
}

void ExportTests::enforcesStrictTerminalFrameDeficitEvidence()
{
    const auto evidence = [](const qint64 actualFrames) {
        FinalOutputEvidence value;
        value.expectedFrames = 100;
        value.stageAGeneratedFrames = 100;
        value.stageASubmittedFrames = 100;
        value.temporaryOverlayFrames = 100;
        value.stageBProgressFrames = actualFrames;
        value.finalFrameCount = actualFrames;
        value.frameRate = {60'000, 1'001};
        value.finalMedia.timeBase = {1, 60'000};
        value.finalMedia.videoStartTicks = 0;
        value.finalMedia.videoStartKnown = true;
        value.finalMedia.videoDurationTicks = actualFrames * 1'001;
        value.otherValidationPassed = true;
        value.outputTransactionSafe = true;
        return value;
    };

    QCOMPARE(FinalOutputValidation::evaluate(evidence(100)).classification,
             FinalOutputClassification::Success);
    QCOMPARE(FinalOutputValidation::evaluate(evidence(99)).classification,
             FinalOutputClassification::SuccessWithWarning);
    QCOMPARE(FinalOutputValidation::evaluate(evidence(90)).classification,
             FinalOutputClassification::SuccessWithWarning);
    QCOMPARE(FinalOutputValidation::evaluate(evidence(89)).classification,
             FinalOutputClassification::Failure);
    QCOMPARE(FinalOutputValidation::evaluate(evidence(101)).classification,
             FinalOutputClassification::Failure);
    // KAN-148: the output must be shown to start at zero.
    auto unknownStart = evidence(100);
    unknownStart.finalMedia.videoStartKnown = false;
    QVERIFY(!FinalOutputValidation::evaluate(unknownStart).startsAtOrigin);
    QCOMPARE(FinalOutputValidation::evaluate(unknownStart).classification, FinalOutputClassification::Failure);
    auto negativeStart = evidence(100);
    negativeStart.finalMedia.videoStartTicks = -1'001;
    QCOMPARE(FinalOutputValidation::evaluate(negativeStart).classification, FinalOutputClassification::Failure);

    auto stageAGeneratedShort = evidence(99);
    stageAGeneratedShort.stageAGeneratedFrames = 99;
    QCOMPARE(FinalOutputValidation::evaluate(stageAGeneratedShort).classification,
             FinalOutputClassification::Failure);
    auto stageASubmittedShort = evidence(99);
    stageASubmittedShort.stageASubmittedFrames = 99;
    QCOMPARE(FinalOutputValidation::evaluate(stageASubmittedShort).classification,
             FinalOutputClassification::Failure);
    auto temporaryOverlayShort = evidence(99);
    temporaryOverlayShort.temporaryOverlayFrames = 99;
    QCOMPARE(FinalOutputValidation::evaluate(temporaryOverlayShort).classification,
             FinalOutputClassification::Failure);
    auto progressMismatch = evidence(98);
    progressMismatch.stageBProgressFrames = 99;
    QCOMPARE(FinalOutputValidation::evaluate(progressMismatch).classification,
             FinalOutputClassification::Failure);
    auto discontinuousTiming = evidence(99);
    discontinuousTiming.finalMedia.videoDurationTicks = 100 * 1'001;
    QCOMPARE(FinalOutputValidation::evaluate(discontinuousTiming).classification,
             FinalOutputClassification::Failure);
    auto otherFailure = evidence(99);
    otherFailure.otherValidationPassed = false;
    QCOMPARE(FinalOutputValidation::evaluate(otherFailure).classification,
             FinalOutputClassification::Failure);
}

void ExportTests::preservesCfrCadenceForCommonRates()
{
    const QString ffmpeg = FfmpegTools::ffmpegPath();
    if (ffmpeg.isEmpty()) QSKIP("FFmpeg is unavailable for the CFR cadence integration test.");
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto runFfmpeg = [&ffmpeg](const QStringList &arguments) {
        QProcess process;
        process.start(ffmpeg, arguments);
        QVERIFY2(process.waitForStarted(), qPrintable(process.errorString()));
        QVERIFY2(process.waitForFinished(30'000), qPrintable(process.errorString()));
        QCOMPARE(process.exitStatus(), QProcess::NormalExit);
        QVERIFY2(process.exitCode() == 0, process.readAllStandardError().constData());
    };
    for (const MediaRational &rate : {MediaRational{30, 1}, MediaRational{30'000, 1'001},
                                      MediaRational{60'000, 1'001}}) {
        const QString rateText = QStringLiteral("%1/%2").arg(rate.numerator).arg(rate.denominator);
        const qsizetype expectedFrames = rate.numerator == 60'000 ? 60 : 30;
        const QString source = directory.filePath(QStringLiteral("source-%1.mkv").arg(rate.numerator));
        const QString output = directory.filePath(QStringLiteral("output-%1.mp4").arg(rate.numerator));
        runFfmpeg({"-hide_banner", "-loglevel", "error", "-y", "-f", "lavfi", "-i",
                   QStringLiteral("color=c=black:s=64x16:r=%1:d=1").arg(rateText), "-frames:v",
                   QString::number(expectedFrames), "-c:v", "ffv1", "-pix_fmt", "bgra", source});
        runFfmpeg({"-hide_banner", "-loglevel", "error", "-y", "-i", source, "-vf",
                   QStringLiteral("fps=fps=%1:start_time=0:round=near:eof_action=round,trim=end_frame=%2,setpts=PTS-STARTPTS")
                       .arg(rateText).arg(expectedFrames),
                   "-fps_mode:v", "cfr", "-c:v", "mpeg4", "-q:v", "2", output});
        const MediaInfo info = MediaProbe::probe(output, {}, true, -1, {}, {}, true);
        QCOMPARE(info.videoFrameCount, expectedFrames);
        QCOMPARE(info.videoPacketCount, expectedFrames);
        QVERIFY(info.frameRate.isEquivalentTo(rate));
        QVERIFY(info.averageFrameRate.isEquivalentTo(rate));
        QVERIFY(qAbs(info.videoStartTime) <= info.timeBase.value());
        QVERIFY(qAbs(info.videoDuration - ExportEngine::outputDuration(expectedFrames, rate))
                <= 1.0 / rate.value());
    }
}

void ExportTests::validatesQuantizedTemporaryOverlayCadence()
{
    const MediaRational scheduledRate{60'000, 1'001};
    constexpr qsizetype expectedFrames = 7'193;
    const double scheduledDuration = ExportEngine::outputDuration(expectedFrames, scheduledRate);
    MediaInfo quantized;
    quantized.videoCodec = QStringLiteral("ffv1");
    quantized.videoSize = {3840, 2160};
    quantized.frameRate = {19'001, 317};
    quantized.averageFrameRate = {19'001, 317};
    quantized.timeBase = {1, 1'000};
    quantized.duration = 120.004;
    quantized.videoStartTime = 0.0;
    const TemporaryOverlayValidationResult quantizedResult = TemporaryOverlayValidation::validate(
        quantized, {3840, 2160}, scheduledRate, expectedFrames, scheduledDuration);
    QVERIFY(!quantized.frameRate.isEquivalentTo(scheduledRate));
    QVERIFY(!quantized.averageFrameRate.isEquivalentTo(scheduledRate));
    QVERIFY(!quantizedResult.nominalRateExact);
    QVERIFY(quantizedResult.nominalRateOk);
    QVERIFY(quantizedResult.averageRateOk);
    QVERIFY(quantizedResult.durationOk);
    QVERIFY(quantizedResult.passed());

    MediaInfo wrongCadence = quantized;
    wrongCadence.frameRate = {30, 1};
    wrongCadence.averageFrameRate = {30, 1};
    const TemporaryOverlayValidationResult wrongCadenceResult = TemporaryOverlayValidation::validate(
        wrongCadence, {3840, 2160}, scheduledRate, expectedFrames, scheduledDuration);
    QVERIFY(!wrongCadenceResult.nominalRateOk);
    QVERIFY(!wrongCadenceResult.averageRateOk);
    QVERIFY(!wrongCadenceResult.passed());

    const QString ffmpeg = FfmpegTools::ffmpegPath();
    if (ffmpeg.isEmpty()) QSKIP("FFmpeg is unavailable for the staged-overlay validation integration test.");
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString overlay = directory.filePath(QStringLiteral("quantized-overlay.mkv"));
    QProcess process;
    process.start(ffmpeg, {"-hide_banner", "-loglevel", "error", "-y", "-f", "lavfi", "-i",
                           "color=c=black:s=16x16:r=60000/1001", "-frames:v",
                           QString::number(expectedFrames), "-an", "-c:v", "ffv1", "-pix_fmt", "bgra",
                           "-f", "matroska", overlay});
    QVERIFY2(process.waitForStarted(), qPrintable(process.errorString()));
    QVERIFY2(process.waitForFinished(30'000), qPrintable(process.errorString()));
    QVERIFY2(process.exitCode() == 0, process.readAllStandardError().constData());

    const MediaInfo metadata = MediaProbe::probeSummary(overlay);
    const TemporaryOverlayValidationResult metadataResult = TemporaryOverlayValidation::validate(
        metadata, {16, 16}, scheduledRate, expectedFrames, scheduledDuration);
    QVERIFY(metadataResult.passed());
    const MediaInfo packetCount = MediaProbe::probe(overlay, {}, false, -1, {}, {}, true);
    QCOMPARE(packetCount.videoPacketCount, expectedFrames);
    const MediaInfo deepCount = MediaProbe::probe(overlay, {}, true);
    QCOMPARE(deepCount.videoFrameCount, expectedFrames);
}

void ExportTests::preservesAbsoluteExportTimestamps()
{
    const MediaRational rate{30'000, 1001};
    QCOMPARE(ExportEngine::framePresentationTime(120.0, 0, rate), 120.0);
    QVERIFY(qAbs(ExportEngine::framePresentationTime(120.0, 300, rate) - 130.01) < 0.000001);
    const SyncTransform sync{90.203, 1.0};
    TelemetrySession session;
    session.channels.insert(
        "speed", TelemetryChannel{"speed", {}, {210.203, 215.203}, {73.4F, 101.2F}});
    session.channels.insert(
        "rpm", TelemetryChannel{"rpm", {}, {210.203, 215.203}, {3842.0F, 5270.0F}});
    TelemetryRenderContext context;
    context.setSession(&session);
    context.setSyncTransform(sync);
    context.setTime(ExportEngine::framePresentationTime(120.0, 0, rate));
    QVERIFY(qAbs(context.telemetryTime().toDouble() - 210.203) < 0.000001);
    QVERIFY(qAbs(context.telemetryValue("speed").toDouble() - 73.4) < 0.001);
    context.setTime(125.0);
    QVERIFY(qAbs(context.telemetryTime().toDouble() - 215.203) < 0.000001);
    QVERIFY(qAbs(context.telemetryValue("rpm").toDouble() - 5270.0) < 0.001);
}

void ExportTests::composesNonZeroExportRangeWithZeroBasedOutput()
{
    const QString ffmpeg = FfmpegTools::ffmpegPath();
    if (ffmpeg.isEmpty()) QSKIP("FFmpeg is unavailable for the non-zero-range integration test.");
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    constexpr int width = 64;
    constexpr int height = 16;
    constexpr int sourceFrames = 300;
    constexpr int rangeStartFrame = 90;
    constexpr int exportFrames = 150;
    const QString primaryRaw = directory.filePath("source.rgba");
    const QString overlayRaw = directory.filePath("telemetry.rgba");
    const QString primary = directory.filePath("source.mkv");
    const QString overlay = directory.filePath("overlay.mkv");
    const QString composed = directory.filePath("composed.mkv");
    const QString decoded = directory.filePath("decoded.rgba");
    const auto writeIdentityFrames = [](const QString &path, const int count, const int pixelOffset) {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly)) return false;
        for (int frame = 0; frame < count; ++frame) {
            QByteArray pixels(width * height * 4, '\0');
            for (int bit = 0; bit < 8; ++bit) {
                const int offset = (pixelOffset + bit) * 4;
                const char value = (frame & (1 << bit)) ? static_cast<char>(255) : 0;
                pixels[offset] = value;
                pixels[offset + 1] = value;
                pixels[offset + 2] = value;
                pixels[offset + 3] = static_cast<char>(255);
            }
            if (file.write(pixels) != pixels.size()) return false;
        }
        return true;
    };
    QVERIFY(writeIdentityFrames(primaryRaw, sourceFrames, 0));
    QVERIFY(writeIdentityFrames(overlayRaw, exportFrames, 8));
    const auto runFfmpeg = [&ffmpeg](const QStringList &arguments) {
        QProcess process;
        process.start(ffmpeg, arguments);
        QVERIFY2(process.waitForStarted(), qPrintable(process.errorString()));
        QVERIFY2(process.waitForFinished(30'000), qPrintable(process.errorString()));
        QCOMPARE(process.exitStatus(), QProcess::NormalExit);
        QVERIFY2(process.exitCode() == 0, process.readAllStandardError().constData());
    };
    const QString size = QStringLiteral("%1x%2").arg(width).arg(height);
    runFfmpeg({"-hide_banner", "-loglevel", "error", "-y", "-f", "rawvideo", "-pixel_format", "rgba",
               "-video_size", size, "-framerate", "30", "-i", primaryRaw, "-f", "lavfi", "-i",
               "sine=frequency=440:sample_rate=48000:duration=10", "-frames:v", QString::number(sourceFrames),
               "-c:v", "ffv1", "-pix_fmt", "bgra", "-c:a", "pcm_s16le", primary});
    runFfmpeg({"-hide_banner", "-loglevel", "error", "-y", "-f", "rawvideo", "-pixel_format", "rgba",
               "-video_size", size, "-framerate", "30", "-i", overlayRaw, "-an", "-c:v", "ffv1",
               "-pix_fmt", "bgra", overlay});
    runFfmpeg({"-hide_banner", "-loglevel", "error", "-y", "-i", primary, "-i", overlay,
               "-filter_complex", "[0:v]trim=start=3:end=8,setpts=PTS-STARTPTS,fps=fps=30/1:start_time=0:round=near:eof_action=round,trim=end_frame=150,setpts=PTS-STARTPTS[source];[1:v]setpts=PTS-STARTPTS[telemetry];[source][telemetry]overlay=0:0:shortest=1:repeatlast=0:eof_action=endall:format=rgb[video];[0:a]atrim=start=3:end=8,asetpts=PTS-STARTPTS[audio]",
               "-map", "[video]", "-fps_mode:v", "cfr", "-map", "[audio]", "-c:v", "ffv1", "-pix_fmt", "bgra", "-c:a",
               "pcm_s16le", composed});
    const MediaInfo outputInfo = MediaProbe::probe(composed, {}, true, -1, {}, {}, true);
    QCOMPARE(outputInfo.videoFrameCount, qsizetype(exportFrames));
    QCOMPARE(outputInfo.videoPacketCount, qsizetype(exportFrames));
    QVERIFY(outputInfo.frameRate.isEquivalentTo({30, 1}));
    QVERIFY(outputInfo.averageFrameRate.isEquivalentTo({30, 1}));
    QVERIFY(qAbs(outputInfo.videoStartTime) <= outputInfo.timeBase.value());
    QVERIFY(qAbs(outputInfo.videoDuration - 5.0) <= 1.0 / 30.0);
    QVERIFY(qAbs(outputInfo.audioStartTime - outputInfo.videoStartTime) <= 1.0 / 48000.0);
    QVERIFY(qAbs(outputInfo.audioDuration - 5.0) <= 1.0 / 48000.0);
    QVERIFY(qAbs(outputInfo.duration - 5.0) < 0.05);
    QVERIFY(!outputInfo.audioCodecs.isEmpty());
    const MediaInfo summaryInfo = MediaProbe::probeSummary(composed);
    QCOMPARE(summaryInfo.videoCodec, QStringLiteral("ffv1"));
    QCOMPARE(summaryInfo.videoSize, QSize(width, height));
    QVERIFY(qAbs(summaryInfo.duration - 5.0) < 0.05);
    QVERIFY(!summaryInfo.audioCodecs.isEmpty());
    runFfmpeg({"-hide_banner", "-loglevel", "error", "-y", "-i", composed, "-f", "rawvideo",
               "-pix_fmt", "rgba", decoded});
    QFile decodedFile(decoded);
    QVERIFY(decodedFile.open(QIODevice::ReadOnly));
    const QByteArray output = decodedFile.readAll();
    const qsizetype bytesPerFrame = width * height * 4;
    QCOMPARE(output.size(), exportFrames * bytesPerFrame);
    const auto decodeIdentity = [](const char *pixels, const int pixelOffset) {
        int identity = 0;
        for (int bit = 0; bit < 8; ++bit) {
            if (static_cast<uchar>(pixels[(pixelOffset + bit) * 4]) > 127) identity |= 1 << bit;
        }
        return identity;
    };
    for (int frame = 0; frame < exportFrames; ++frame) {
        const char *pixels = output.constData() + frame * bytesPerFrame;
        QCOMPARE(decodeIdentity(pixels, 0), rangeStartFrame + frame);
        QCOMPARE(decodeIdentity(pixels, 8), frame);
    }
}

void ExportTests::composes5994SixtySecondNonZeroRange()
{
    const QString ffmpeg = FfmpegTools::ffmpegPath();
    if (ffmpeg.isEmpty()) QSKIP("FFmpeg is unavailable for the 59.94 range regression test.");
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const MediaRational rate{60'000, 1'001};
    const qsizetype expectedFrames = 3'597;
    QCOMPARE(expectedFrames, qsizetype(3'597));
    const QString source = directory.filePath(QStringLiteral("source.mp4"));
    const QString overlay = directory.filePath(QStringLiteral("overlay.mkv"));
    const QString output = directory.filePath(QStringLiteral("output.mp4"));
    const auto runFfmpeg = [&ffmpeg](const QStringList &arguments) {
        QProcess process;
        process.start(ffmpeg, arguments);
        QVERIFY2(process.waitForStarted(), qPrintable(process.errorString()));
        QVERIFY2(process.waitForFinished(30'000), qPrintable(process.errorString()));
        QCOMPARE(process.exitStatus(), QProcess::NormalExit);
        QVERIFY2(process.exitCode() == 0, process.readAllStandardError().constData());
    };
    runFfmpeg({"-hide_banner", "-loglevel", "error", "-y", "-f", "lavfi", "-i",
               "color=c=black:s=64x16:r=60000/1001:d=91", "-f", "lavfi", "-i",
               "sine=frequency=440:sample_rate=48000:duration=91", "-map", "0:v", "-map", "1:a",
               "-c:v", "mpeg4", "-q:v", "2", "-c:a", "aac", source});
    runFfmpeg({"-hide_banner", "-loglevel", "error", "-y", "-f", "lavfi", "-i",
               "color=c=black:s=64x16:r=60000/1001", "-frames:v", QString::number(expectedFrames),
               "-an", "-c:v", "ffv1", "-pix_fmt", "bgra", overlay});
    runFfmpeg({"-hide_banner", "-loglevel", "error", "-y", "-i", source, "-i", overlay,
               "-filter_complex",
               QStringLiteral("[0:v]trim=start=30,setpts=PTS-STARTPTS,fps=fps=60000/1001:start_time=0:round=near:eof_action=round,trim=end_frame=%1,setpts=PTS-STARTPTS[source];[1:v]setpts=PTS-STARTPTS[telemetry];[source][telemetry]overlay=0:0:shortest=1:repeatlast=0:eof_action=endall[video];[0:a]atrim=start=30:end=90,asetpts=PTS-STARTPTS[audio]")
                   .arg(expectedFrames),
               "-map", "[video]", "-fps_mode:v", "cfr", "-map", "[audio]", "-c:v", "mpeg4",
               "-q:v", "2", "-c:a", "aac", output});
    const MediaInfo info = MediaProbe::probe(output, {}, true, -1, {}, {}, true);
    QCOMPARE(info.videoFrameCount, expectedFrames);
    QCOMPARE(info.videoPacketCount, expectedFrames);
    QVERIFY(info.frameRate.isEquivalentTo(rate));
    QVERIFY(info.averageFrameRate.isEquivalentTo(rate));
    QVERIFY(qAbs(info.videoDuration - ExportEngine::outputDuration(expectedFrames, rate))
            <= 1.0 / rate.value());
    QVERIFY(!info.audioCodecs.isEmpty());
    QVERIFY(qAbs(info.audioStartTime - info.videoStartTime) <= 1024.0 / 48'000.0);
    QVERIFY(qAbs(info.audioDuration - 60.0) <= 1024.0 / 48'000.0);
}

void ExportTests::normalizesNonZeroStreamPtsForVideoAndAudio()
{
    const QString ffmpeg = FfmpegTools::ffmpegPath();
    if (ffmpeg.isEmpty()) QSKIP("FFmpeg is unavailable for the stream-PTS integration test.");
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    constexpr int width = 64;
    constexpr int height = 16;
    const QString source = directory.filePath("offset-source.mkv");
    const QString overlay = directory.filePath("overlay.mkv");
    const QString output = directory.filePath("output.mkv");
    const auto runFfmpeg = [&ffmpeg](const QStringList &arguments) {
        QProcess process;
        process.start(ffmpeg, arguments);
        QVERIFY2(process.waitForStarted(), qPrintable(process.errorString()));
        QVERIFY2(process.waitForFinished(30'000), qPrintable(process.errorString()));
        QCOMPARE(process.exitStatus(), QProcess::NormalExit);
        QVERIFY2(process.exitCode() == 0, process.readAllStandardError().constData());
    };
    const QString size = QStringLiteral("%1x%2").arg(width).arg(height);
    runFfmpeg({"-hide_banner", "-loglevel", "error", "-y", "-f", "lavfi", "-i",
               QStringLiteral("color=c=black:s=%1:r=30:d=10").arg(size), "-f", "lavfi", "-i",
               "sine=frequency=440:sample_rate=48000:duration=10", "-output_ts_offset", "2",
               "-map", "0:v", "-map", "1:a", "-c:v", "ffv1", "-pix_fmt", "bgra", "-c:a",
               "pcm_s16le", source});
    const MediaInfo sourceInfo = MediaProbe::probe(source);
    QVERIFY(qAbs(sourceInfo.videoStartTime - 2.0) <= sourceInfo.timeBase.value());
    QVERIFY(qAbs(sourceInfo.audioStartTime - 2.0) <= sourceInfo.audioTimeBase.value());
    runFfmpeg({"-hide_banner", "-loglevel", "error", "-y", "-f", "lavfi", "-i",
               QStringLiteral("color=c=black:s=%1:r=30:d=5").arg(size), "-frames:v", "150",
               "-an", "-c:v", "ffv1", "-pix_fmt", "bgra", overlay});
    runFfmpeg({"-hide_banner", "-loglevel", "error", "-y", "-ss", "2", "-i", source, "-i", overlay,
               "-filter_complex", "[0:v]trim=start=1:end=6,setpts=PTS-STARTPTS,fps=fps=30/1:start_time=0:round=near:eof_action=round,trim=end_frame=150,setpts=PTS-STARTPTS[source];[1:v]setpts=PTS-STARTPTS[telemetry];[source][telemetry]overlay=0:0:shortest=1:repeatlast=0:eof_action=endall[video];[0:a]atrim=start=1:end=6,asetpts=PTS-STARTPTS[audio]",
               "-map", "[video]", "-fps_mode:v", "cfr", "-map", "[audio]", "-c:v", "ffv1",
               "-pix_fmt", "bgra", "-c:a", "pcm_s16le", output});
    const MediaInfo outputInfo = MediaProbe::probe(output, {}, true, -1, {}, {}, true);
    QCOMPARE(outputInfo.videoFrameCount, qsizetype(150));
    QCOMPARE(outputInfo.videoPacketCount, qsizetype(150));
    QVERIFY(outputInfo.frameRate.isEquivalentTo({30, 1}));
    QVERIFY(outputInfo.averageFrameRate.isEquivalentTo({30, 1}));
    QVERIFY(qAbs(outputInfo.videoStartTime) <= outputInfo.timeBase.value());
    QVERIFY(qAbs(outputInfo.audioStartTime) <= outputInfo.audioTimeBase.value());
    QVERIFY(qAbs(outputInfo.audioStartTime - outputInfo.videoStartTime) <= 1.0 / 48000.0);
    QVERIFY(qAbs(outputInfo.videoDuration - 5.0) <= 1.0 / 30.0);
    QVERIFY(qAbs(outputInfo.audioDuration - 5.0) <= 1.0 / 48000.0);
}

void ExportTests::convertsVfrInputToCfrWithFrameCorrectOverlay()
{
    const QString ffmpeg = FfmpegTools::ffmpegPath();
    if (ffmpeg.isEmpty()) QSKIP("FFmpeg is unavailable for the VFR-to-CFR integration test.");
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    constexpr int width = 64;
    constexpr int height = 16;
    const QString source = directory.filePath("vfr-source.mp4");
    const QString rawOverlay = directory.filePath("overlay.rgba");
    const QString overlay = directory.filePath("overlay.mkv");
    const QString output = directory.filePath("output.mkv");
    const QString decoded = directory.filePath("decoded.rgba");
    const auto runFfmpeg = [&ffmpeg](const QStringList &arguments) {
        QProcess process;
        process.start(ffmpeg, arguments);
        QVERIFY2(process.waitForStarted(), qPrintable(process.errorString()));
        QVERIFY2(process.waitForFinished(30'000), qPrintable(process.errorString()));
        QCOMPARE(process.exitStatus(), QProcess::NormalExit);
        QVERIFY2(process.exitCode() == 0, process.readAllStandardError().constData());
    };
    const QString size = QStringLiteral("%1x%2").arg(width).arg(height);
    runFfmpeg({"-hide_banner", "-loglevel", "error", "-y", "-f", "lavfi", "-i",
               QStringLiteral("testsrc2=size=%1:rate=30:duration=2").arg(size), "-vf",
               "select='eq(mod(n\\,5)\\,0)+eq(mod(n\\,5)\\,1)'", "-fps_mode:v", "vfr",
               "-c:v", "mpeg4", "-q:v", "2", source});
    const MediaInfo sourceInfo = MediaProbe::probe(source, {}, false, -1, {}, {}, true);
    QVERIFY(sourceInfo.likelyVariableFrameRate);
    const MediaRational exportRate = sourceInfo.averageFrameRate;
    QVERIFY(exportRate.isValid());
    const auto sourceRange = ExportEngine::fullVideoFrameRange(sourceInfo, exportRate);
    QVERIFY(sourceRange.has_value());
    const qsizetype expectedFrames = static_cast<qsizetype>(sourceRange->frameCount());
    QCOMPARE(expectedFrames, qsizetype(24));
    QFile rawFile(rawOverlay);
    QVERIFY(rawFile.open(QIODevice::WriteOnly));
    for (qsizetype frame = 0; frame < expectedFrames; ++frame) {
        QByteArray pixels(width * height * 4, '\0');
        for (int bit = 0; bit < 8; ++bit) {
            const char value = (frame & (qsizetype(1) << bit)) ? static_cast<char>(255) : 0;
            pixels[bit * 4] = value;
            pixels[bit * 4 + 1] = value;
            pixels[bit * 4 + 2] = value;
            pixels[bit * 4 + 3] = static_cast<char>(255);
        }
        QCOMPARE(rawFile.write(pixels), qint64(pixels.size()));
    }
    rawFile.close();
    const QString rate = QStringLiteral("%1/%2").arg(exportRate.numerator).arg(exportRate.denominator);
    runFfmpeg({"-hide_banner", "-loglevel", "error", "-y", "-f", "rawvideo", "-pixel_format",
               "rgba", "-video_size", size, "-framerate", rate, "-i", rawOverlay, "-frames:v",
               QString::number(expectedFrames), "-an", "-c:v", "ffv1", "-pix_fmt", "bgra", overlay});
    runFfmpeg({"-hide_banner", "-loglevel", "error", "-y", "-i", source, "-i", overlay,
               "-filter_complex", QStringLiteral("[0:v]trim=start=0:end=%1,setpts=PTS-STARTPTS,fps=fps=%2:start_time=0:round=near:eof_action=round,trim=end_frame=%3,setpts=PTS-STARTPTS[source];[1:v]setpts=PTS-STARTPTS[telemetry];[source][telemetry]overlay=0:0:shortest=1:repeatlast=0:eof_action=endall:format=rgb[video]")
                                      .arg(sourceInfo.duration, 0, 'f', 9).arg(rate).arg(expectedFrames),
               "-map", "[video]", "-fps_mode:v", "cfr", "-c:v", "ffv1", "-pix_fmt", "bgra", output});
    const MediaInfo outputInfo = MediaProbe::probe(output, {}, true, -1, {}, {}, true);
    QCOMPARE(outputInfo.videoFrameCount, expectedFrames);
    QCOMPARE(outputInfo.videoPacketCount, expectedFrames);
    QVERIFY(outputInfo.frameRate.isEquivalentTo(exportRate));
    QVERIFY(outputInfo.averageFrameRate.isEquivalentTo(exportRate));
    QVERIFY(qAbs(outputInfo.videoStartTime) <= outputInfo.timeBase.value());
    QVERIFY(qAbs(outputInfo.videoDuration - ExportEngine::outputDuration(expectedFrames, exportRate))
            <= 1.0 / exportRate.value());
    runFfmpeg({"-hide_banner", "-loglevel", "error", "-y", "-i", output, "-f", "rawvideo",
               "-pixel_format", "rgba", decoded});
    QFile decodedFile(decoded);
    QVERIFY(decodedFile.open(QIODevice::ReadOnly));
    const QByteArray frames = decodedFile.readAll();
    const qsizetype bytesPerFrame = width * height * 4;
    QCOMPARE(frames.size(), expectedFrames * bytesPerFrame);
    for (qsizetype frame = 0; frame < expectedFrames; ++frame) {
        const char *pixels = frames.constData() + frame * bytesPerFrame;
        int identity = 0;
        for (int bit = 0; bit < 8; ++bit) {
            if (static_cast<uchar>(pixels[bit * 4]) > 127) identity |= 1 << bit;
        }
        QCOMPARE(identity, static_cast<int>(frame));
    }
}

void ExportTests::estimatesExportProgress()
{
    ExportProgressEstimator estimator;
    const MediaRational rate{60, 1};
    const auto early = estimator.update(1, 600, 100, rate);
    QVERIFY(!early.etaAvailable);
    const auto steady = estimator.update(100, 600, 1'100, rate);
    QVERIFY(steady.etaAvailable);
    QVERIFY(qAbs(steady.throughputFps - 99.0) < 0.1);
    QVERIFY(qAbs(steady.realtimeFactor - 1.65) < 0.01);
    QVERIFY(steady.etaSeconds > 5.0 && steady.etaSeconds < 6.0);
    QCOMPARE(ExportProgressEstimator::stageProgress("rendering", 1.0), 95.0);
    QCOMPARE(ExportProgressEstimator::stageProgress("finalizing", 0.0), 97.0);
    QCOMPARE(ExportProgressEstimator::stageProgress("validating", 1.0), 99.0);
    QCOMPARE(ExportProgressEstimator::stageProgress("complete", 0.0), 100.0);
    QCOMPARE(ExportProgressEstimator::stageProgress("cancelled", 1.0), 0.0);
}

void ExportTests::parsesStructuredFfmpegProgress()
{
    FfmpegProgressParser parser;
    const QList<FfmpegProgress> first = parser.append(
        "frame=42\nfps=27.5\nout_time_us=700700\nspeed=0.46x\nprogress=continue\n");
    QCOMPARE(first.size(), 1);
    QCOMPARE(first.front().encodedFrames, qsizetype(42));
    QCOMPARE(first.front().outputMicroseconds, qint64(700700));
    QVERIFY(qAbs(first.front().encoderFps - 27.5) < 0.001);
    QVERIFY(qAbs(first.front().realtimeFactor - 0.46) < 0.001);
    QVERIFY(!first.front().complete);

    const QList<FfmpegProgress> split = parser.append("frame=60\nout_time_ms=1001000\nprogress=");
    QVERIFY(split.isEmpty());
    const QList<FfmpegProgress> last = parser.append("end\n");
    QCOMPARE(last.size(), 1);
    QCOMPARE(last.front().encodedFrames, qsizetype(60));
    QCOMPARE(last.front().outputMicroseconds, qint64(1001000));
    QVERIFY(last.front().complete);
}

void ExportTests::calculatesEncodedOutputProgress()
{
    QCOMPARE(FfmpegProgressParser::overallPercent(0.0, 10.0), 0.0);
    QVERIFY(FfmpegProgressParser::overallPercent(2.5, 10.0)
            < FfmpegProgressParser::overallPercent(5.0, 10.0));
    QCOMPARE(FfmpegProgressParser::overallPercent(5.0, 10.0), 47.5);
    QCOMPARE(FfmpegProgressParser::overallPercent(12.0, 10.0), 95.0);
    QCOMPARE(FfmpegProgressParser::overallPercent(-1.0, 10.0), 0.0);
}

void ExportTests::preservesFrameIdentityThroughCompletedOverlayComposition()
{
    const QString ffmpeg = FfmpegTools::ffmpegPath();
    if (ffmpeg.isEmpty()) QSKIP("FFmpeg is unavailable for the frame-identity integration test.");
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    constexpr int width = 64;
    constexpr int height = 16;
    constexpr int frameCount = 150;
    const QString primary = directory.filePath("primary.mkv");
    const QString rawOverlay = directory.filePath("identity.rgba");
    const QString overlay = directory.filePath("overlay.mkv");
    const QString composed = directory.filePath("composed.mkv");
    const QString decoded = directory.filePath("decoded.rgba");
    QFile rawFile(rawOverlay);
    QVERIFY(rawFile.open(QIODevice::WriteOnly));
    for (int frame = 0; frame < frameCount; ++frame) {
        QByteArray pixels(width * height * 4, '\0');
        for (int bit = 0; bit < 8; ++bit) {
            const int offset = bit * 4;
            const char value = (frame & (1 << bit)) ? static_cast<char>(255) : 0;
            pixels[offset] = value;
            pixels[offset + 1] = value;
            pixels[offset + 2] = value;
            pixels[offset + 3] = static_cast<char>(255);
        }
        QCOMPARE(rawFile.write(pixels), qint64(pixels.size()));
    }
    rawFile.close();
    const auto runFfmpeg = [&ffmpeg](const QStringList &arguments) {
        QProcess process;
        process.start(ffmpeg, arguments);
        QVERIFY2(process.waitForStarted(), qPrintable(process.errorString()));
        QVERIFY2(process.waitForFinished(30'000), qPrintable(process.errorString()));
        QCOMPARE(process.exitStatus(), QProcess::NormalExit);
        QVERIFY2(process.exitCode() == 0, process.readAllStandardError().constData());
    };
    runFfmpeg({"-hide_banner", "-loglevel", "error", "-y", "-f", "lavfi", "-i",
               QStringLiteral("color=c=black:s=%1x%2:r=30").arg(width).arg(height),
               "-frames:v", QString::number(frameCount), "-c:v", "ffv1", "-pix_fmt", "bgra", primary});
    runFfmpeg({"-hide_banner", "-loglevel", "error", "-y", "-f", "rawvideo", "-pixel_format",
               "rgba", "-video_size", QStringLiteral("%1x%2").arg(width).arg(height), "-framerate", "30",
               "-i", rawOverlay, "-frames:v", QString::number(frameCount), "-c:v", "ffv1", "-pix_fmt",
               "bgra", overlay});
    runFfmpeg({"-hide_banner", "-loglevel", "error", "-y", "-i", primary, "-i", overlay,
               "-filter_complex", "[0:v][1:v]overlay=0:0:shortest=1:repeatlast=0:eof_action=endall:format=rgb[v]",
               "-map", "[v]", "-c:v", "ffv1", "-pix_fmt", "bgra", composed});
    runFfmpeg({"-hide_banner", "-loglevel", "error", "-y", "-i", composed, "-f", "rawvideo",
               "-pixel_format", "rgba", decoded});
    QFile decodedFile(decoded);
    QVERIFY(decodedFile.open(QIODevice::ReadOnly));
    const QByteArray output = decodedFile.readAll();
    const qsizetype bytesPerFrame = width * height * 4;
    QCOMPARE(output.size(), frameCount * bytesPerFrame);
    for (int frame = 0; frame < frameCount; ++frame) {
        const char *pixels = output.constData() + frame * bytesPerFrame;
        int identity = 0;
        for (int bit = 0; bit < 8; ++bit) {
            if (static_cast<uchar>(pixels[bit * 4]) > 127) identity |= 1 << bit;
        }
        QCOMPARE(identity, frame);
    }
}

void ExportTests::preservesPremultipliedAlphaThroughOverlayComposition()
{
    const QString ffmpeg = FfmpegTools::ffmpegPath();
    if (ffmpeg.isEmpty()) QSKIP("FFmpeg is unavailable for the alpha-composition integration test.");
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    constexpr int width = 8;
    constexpr int height = 8;
    const QString primaryRaw = directory.filePath("primary.rgba");
    const QString overlayRaw = directory.filePath("overlay.rgba");
    const QString primary = directory.filePath("primary.mkv");
    const QString overlay = directory.filePath("overlay.mkv");
    const QString composed = directory.filePath("composed.mkv");
    const QString decodedPrimary = directory.filePath("decoded-primary.rgba");
    const QString decodedOverlay = directory.filePath("decoded-overlay.rgba");
    const QString decoded = directory.filePath("decoded.rgba");
    const std::array<uchar, 4> background{20, 40, 80, 255};
    QByteArray primaryPixels(width * height * 4, '\0');
    QByteArray overlayPixels(width * height * 4, '\0');
    const auto setPixel = [&primaryPixels, &overlayPixels](const int x, const int y,
                                                                   const std::array<uchar, 4> rgba,
                                                                   const bool overlayPixel) {
        QByteArray &pixels = overlayPixel ? overlayPixels : primaryPixels;
        const qsizetype offset = (y * width + x) * 4;
        for (int component = 0; component < 4; ++component) {
            pixels[offset + component] = static_cast<char>(rgba[component]);
        }
    };
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) setPixel(x, y, background, false);
    }
    // Premultiplied 50% red fill, translucent green border, an alpha-ramped
    // edge representative of antialiasing, and an opaque reference swatch.
    setPixel(2, 2, {128, 0, 0, 128}, true);
    setPixel(1, 2, {0, 96, 0, 96}, true);
    setPixel(2, 1, {64, 0, 0, 64}, true);
    setPixel(4, 4, {0, 0, 255, 255}, true);
    QVERIFY(writeBytes(primaryRaw, primaryPixels));
    QVERIFY(writeBytes(overlayRaw, overlayPixels));
    const auto runFfmpeg = [&ffmpeg](const QStringList &arguments) {
        QProcess process;
        process.start(ffmpeg, arguments);
        QVERIFY2(process.waitForStarted(), qPrintable(process.errorString()));
        QVERIFY2(process.waitForFinished(30'000), qPrintable(process.errorString()));
        QCOMPARE(process.exitStatus(), QProcess::NormalExit);
        QVERIFY2(process.exitCode() == 0, process.readAllStandardError().constData());
    };
    const QString size = QStringLiteral("%1x%2").arg(width).arg(height);
    runFfmpeg({"-hide_banner", "-loglevel", "error", "-y", "-f", "rawvideo", "-pixel_format",
               "rgba", "-video_size", size, "-framerate", "30", "-i", primaryRaw, "-frames:v", "1",
               "-c:v", "ffv1", "-pix_fmt", "bgra", primary});
    runFfmpeg({"-hide_banner", "-loglevel", "error", "-y", "-f", "rawvideo", "-pixel_format",
               "rgba", "-video_size", size, "-framerate", "30", "-i", overlayRaw, "-frames:v", "1",
               "-an", "-c:v", "ffv1", "-pix_fmt", "bgra", overlay});
    const auto decodeRgba = [&runFfmpeg](const QString &input, const QString &output) {
        runFfmpeg({"-hide_banner", "-loglevel", "error", "-y", "-i", input, "-f", "rawvideo",
                   "-pix_fmt", "rgba", output});
        QFile decodedFile(output);
        if (!decodedFile.open(QIODevice::ReadOnly)) return QByteArray{};
        return decodedFile.readAll();
    };
    // Stage A changes transport to FFV1/BGRA only. It must not alter the
    // premultiplied QRhi-readback bytes before Stage B interprets alpha.
    QCOMPARE(decodeRgba(primary, decodedPrimary), primaryPixels);
    QCOMPARE(decodeRgba(overlay, decodedOverlay), overlayPixels);
    runFfmpeg({"-hide_banner", "-loglevel", "error", "-y", "-i", primary, "-i", overlay,
               "-filter_complex", "[1:v]setparams=alpha_mode=premultiplied[temporaryOverlay];[0:v][temporaryOverlay]overlay=0:0:shortest=1:repeatlast=0:eof_action=endall:alpha=premultiplied:format=auto[v]",
               "-map", "[v]", "-c:v", "ffv1", "-pix_fmt", "bgra", composed});
    runFfmpeg({"-hide_banner", "-loglevel", "error", "-y", "-i", composed, "-f", "rawvideo",
               "-pix_fmt", "rgba", decoded});
    QFile decodedFile(decoded);
    QVERIFY(decodedFile.open(QIODevice::ReadOnly));
    const QByteArray output = decodedFile.readAll();
    QCOMPARE(output.size(), primaryPixels.size());
    const auto expected = [&background](const std::array<uchar, 4> foreground) {
        std::array<uchar, 4> value{};
        for (int component = 0; component < 3; ++component) {
            value[component] = static_cast<uchar>(foreground[component]
                + (background[component] * (255 - foreground[3]) + 127) / 255);
        }
        value[3] = 255;
        return value;
    };
    const auto verifyPixel = [&output](const int x, const int y,
                                               const std::array<uchar, 4> expectedPixel) {
        const qsizetype offset = (y * width + x) * 4;
        for (int component = 0; component < 4; ++component) {
            const int actual = static_cast<uchar>(output[offset + component]);
            QVERIFY2(std::abs(actual - expectedPixel[component]) <= 1,
                     qPrintable(QStringLiteral("pixel (%1,%2), component %3: expected %4, actual %5")
                                    .arg(x).arg(y).arg(component).arg(expectedPixel[component]).arg(actual)));
        }
    };
    verifyPixel(0, 0, background);
    verifyPixel(2, 2, expected({128, 0, 0, 128}));
    verifyPixel(1, 2, expected({0, 96, 0, 96}));
    verifyPixel(2, 1, expected({64, 0, 0, 64}));
    verifyPixel(4, 4, expected({0, 0, 255, 255}));
    QVERIFY(TelemetryFrameRenderer::readbackRequiresVerticalFlip(true));
    QVERIFY(!TelemetryFrameRenderer::readbackRequiresVerticalFlip(false));
}

void ExportTests::cancelsExportWorkerDuringTelemetryPreparation()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString vboPath = directory.filePath("large.vbo");
    QFile vbo(vboPath);
    QVERIFY(vbo.open(QIODevice::WriteOnly | QIODevice::Text));
    QVERIFY(vbo.write("[header]\ncoordinate units = arc-minutes\n[column names]\ntime speed latitude longitude\n[data]\n") > 0);
    for (int row = 0; row < 300'000; ++row) {
        QVERIFY(vbo.write(QStringLiteral("%1 %2 3120 -1260\n").arg(row).arg(row % 200).toUtf8()) > 0);
    }
    vbo.close();
    const QString cancelPath = directory.filePath("cancel");
    const QString configPath = directory.filePath("export.json");
    QVERIFY(writeBytes(configPath, QJsonDocument(QJsonObject{{"vboPath", vboPath}, {"cancelPath", cancelPath}}).toJson()));
    QProcess worker;
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
    worker.setProcessEnvironment(environment);
    worker.start(QStringLiteral(FLAPPEDEAR_NATIVE_PATH), {"--export-worker", configPath});
    QVERIFY2(worker.waitForStarted(), qPrintable(worker.errorString()));
    QElapsedTimer elapsed;
    elapsed.start();
    QByteArray events;
    while (!events.contains("parseTelemetry") && elapsed.elapsed() < 15'000) {
        worker.waitForReadyRead(100);
        events += worker.readAllStandardOutput();
    }
    QVERIFY2(events.contains("parseTelemetry"),
             qPrintable(QString::fromUtf8(events + worker.readAllStandardError())));
    QVERIFY(writeBytes(cancelPath, "cancel"));
    QVERIFY2(worker.waitForFinished(3'000), qPrintable(worker.errorString()));
    events += worker.readAllStandardOutput();
    QCOMPARE(worker.exitStatus(), QProcess::NormalExit);
    QCOMPARE(worker.exitCode(), 0);
    QVERIFY(events.contains("\"state\":\"cancelled\""));
    QVERIFY(!events.contains("encodeTemporaryOverlay"));
}

#define main nativeTestMain
QTEST_MAIN(ExportTests)
#undef main

int main(int argc, char *argv[])
{
    return runWithExportWorker(argc, argv, nativeTestMain);
}
#include "NativeExportTests.moc"
