#include "gopro/GoProTelemetrySource.h"
#include "app/AnalysisController.h"
#include "app/AppController.h"
#include "app/ApplicationIdentity.h"
#include "app/BundledFonts.h"
#include "app/GuiSessionLock.h"
#include "app/PreviewPlayback.h"
#include "app/TelemetryController.h"
#include "export/EncoderDetector.h"
#include "export/BoundedProcessOutput.h"
#include "export/ExportEngine.h"
#include "export/FinalOutputValidation.h"
#include "export/ExportFormat.h"
#include "export/ExportMediaProfile.h"
#include "export/ExportDiagnostics.h"
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
#include "telemetry/TelemetrySyncEngine.h"
#include "telemetry/TelemetrySession.h"
#include "telemetry/GgPairs.h"
#include "telemetry/DayReport.h"
#include "telemetry/FocusAreas.h"
#include "export/VideoFingerprint.h"
#include "telemetry/TelemetrySource.h"
#include "telemetry/LapTiming.h"
#include "telemetry/TelemetryRenderContext.h"
#include "telemetry/TrackGeometry.h"
#include "telemetry/TelemetryGeometry.h"
#include "telemetry/TyreData.h"
#include "UserGuideCapture.h"
#include "telemetry/VboParser.h"
#include "RczFixture.h"
#include "EventProjectFixture.h"
#include "telemetry/TrackSegmentReview.h"
#include "telemetry/SectorTiming.h"
#include "telemetry/CornerSpeeds.h"
#include "telemetry/BrakingMetrics.h"
#include "telemetry/ExitMetrics.h"
#include "project/EventProjectCodec.h"
#include "widgets/WidgetModel.h"
#include "project/ProjectWriter.h"
#include "project/ProjectDocumentState.h"
#include "project/ProjectRecoveryStore.h"
#include "project/ProjectSourceReference.h"
#include "project/BoundedJsonLoader.h"
#include "project/ProjectLimits.h"
#include "telemetry/TrackInference.h"

#include <QColor>
#include <QDateTime>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QMediaPlayer>
#include <QProcess>
#include <QPromise>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QQuickItem>
#include <QFontDatabase>
#include <QSettings>
#include <QScopeGuard>
#include <QSemaphore>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QTemporaryDir>
#include <QThread>
#include <QUuid>
#include <QVideoSink>
#include <QVideoFrame>
#include <QtEndian>
#include <QtTest>
#include <qpa/qwindowsysteminterface.h>
#include <cmath>
#include <atomic>
#include <array>
#include <bit>
#include <limits>
#include <numbers>
#include <thread>
#ifdef Q_OS_UNIX
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#endif

using namespace FlappedEar;

class TelemetryTests final : public QObject {
    Q_OBJECT

    static QStringList unreachableControls(QQuickWindow *window);
private slots:
    void initTestCase();
    void usesOverlaysIdentityWithItsOwnStorage();
    void preservesSignedSamplesWithBrakingUpPresentation();
    void reopensPreferencesProjectAndRecoveryAfterDisplayRename();
    void cleanupTestCase();
    void persistsEventSelectionAndRunLocalSync();
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
    void automaticallyGroupsPrivateTrackDay();
    void lapExclusionPolicySharesRankingAndRenderInputs();
    void lapExclusionsSurviveSaveRecoveryAndInvalidateSafely();
    void keepsAnalysisStateThroughOverlayEdits();
    void recordsVboUtcChronology();
    void formatsElapsedTimes();
    void presentsBrakingUpInGForceWidgets();
    void rejectsBatchLinksWithDifferentPersistedFormats();
    void savesReopensAndRelinksRcz();
    void exportsSyntheticRczThroughWorker_data();
    void exportsSyntheticRczThroughWorker();
    void exportsEditListSourceThroughWorker();
    void preservesMissingTelemetryGaps();
    void filtersOverlayPresentationValues();
    void samplesTelemetryRanges();
    void acceptsVboMicrosecondConversionBoundary();
    void publishesCurrentLapAfterFirstAcceptedPass();
    void publishesAndClearsLapStateWithController();
    void derivesNavigableLapFragmentsAndHotlapExportRange();
    void showsOneHotlapAndExportsItByDefault();
    void showsTyreTemperatureAndPressurePerCorner();
    void capturesUserGuideScreens();
    void appliesTelemetryDesignLanguage();
    void protectsEveryDaySourceFromExport();
    void keepsOutputSafeWhenTheDestinationFills_data();
    void keepsOutputSafeWhenTheDestinationFills();
    void describesOutOfSpaceExportFailures();
    void keepsRecoveryWhenQuittingAtTheRecoveryPrompt();
    void offersRecoveryOfAnEventCreatedByImport();
    void reviewsGoProChapterGroups();
    void keepsVideoChaptersAsOneTimeline();
    void playsVideoChaptersAcrossBoundaries();
    void keepsAPausedSeekWhenLoadedMediaRepeats();
    void routesNewDocumentSaveAsThroughPendingQuit();
    void mapsLapStartTelemetryTimesBackToVideoBounds();
    void rendersLapTimeTileInProductionScene();
    void rendersTyresInExportScene();
    void normalizesDesignedWidgetElements();
    void persistsWidgetLibrary();
    void rejectsMalformedWidgetLibraries();
    void rendersDesignedWidgetInExportScene();
    void editsDesignedWidgetInWidgetEditor();
    void rendersPedalsWithoutLayoutLoops();
    void decodesOptionalRealVideoFrameWithNativeSink();
    void benchmarksCachedOptionalRealVboPresentationLookups();
    void persistsWidgetScenes();
    void normalizesWidgetSemanticsAcrossMutationAndImport();
    void rejectsNonFiniteWidgetGeometryAndDuplicateIds();
    void loadsVisualTemplates();
    void dropsRetiredWidgetTypes();
    void providesCustomizableArchetypes();
    void persistsAndSharesCustomTemplates();
    void updatesCustomTemplatesInPlace();
    void rejectsTemplateStoreCountGrowth();
    void rejectsTemplateStoreByteGrowth();
    void preservesRejectedTemplateStores();
    void boundsLiveWidgetAndCueMutations();
    void preservesTemplatePickerSelectionById();
    void preservesOptionalFontSettings();
    void preservesGForcePresentationSettings();
    void providesGForceVariants();
    void persistsWidgetAnimationCues();
    void groupsAndMovesWidgets();
    void constrainsWidgetGeometry();
    void projectsWestPositiveTracksWithoutMirroring_data();
    void projectsWestPositiveTracksWithoutMirroring();
    void persistsAndInvalidatesRunTrackConfiguration();
    void cachesStaticTrackGeometry();
    void decodesGps9Gpmf();
    void rejectsMalformedGpmf();
    void cancelsSlowGoProProbePromptly();
    void boundsGoProProbeOutput();
    void rejectsOutOfFileGpmfPackets();
    void boundsGpmfDepthAndRecordCount();
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
    void rejectsAutomaticSyncWithShortOverlap();
    void synchronizesWhenTheRecordingsOnlyPartlyOverlap();
    void neverAutoAppliesAnotherLapOfPeriodicLaps();
    void gatesWeakSyncCandidates();
    void preservesTimingEditsDuringAutoSync_data();
    void preservesTimingEditsDuringAutoSync();
    void rendersTelemetryAtExplicitTime();
    void preservesPartialOverlapInAnalysisSeries();
    void probesMediaInfoJson();
    void parsesMediaSummaryJson();
    void modelsExtendedMediaCharacteristics();
    void rejectsInvalidMediaProbeJson();
    void classifiesMediaProbeProcessFailures();
    void reportsMediaProbeLifecycleHeartbeat();
    void cancelsMediaProbeWithoutLeavingItRunning();
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
    void rendersCanvasWidgetsInFirstOffscreenFrames_data();
    void rendersCanvasWidgetsInFirstOffscreenFrames();
    void preservesTenBitSdrThroughComposition();
    void preservesTenBitFullRangeColorThroughVideoToolboxExport();
    void preservesExactExportRateRationals();
    void schedulesFrameAddressedExportRangesExactly();
    void floorsConvertedFrameCounts_data();
    void floorsConvertedFrameCounts();
    void enforcesStrictTerminalFrameDeficitEvidence();
    void boundsExportValidationAndWorkerDiagnostics();
    void derivesStablePreviewViewportAndLastFrameAdapter();
    void exposesReactivePreviewMetadataToQml();
    void plansBoundedStageBSourceAccess();
    void checksCompositionFiltersBeforeRendering();
    void preservesFramesWithPositiveSourcePts_data();
    void preservesFramesWithPositiveSourcePts();
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
    void persistsExportDiagnosticsAndRetainsKnownLogs();
    void formatsStageAFailureDiagnostics();
    void throttlesDiagnosticHeartbeats();
    void tracksValidationSubstepStages();
    void parsesStructuredFfmpegProgress();
    void boundsWidgetAndTemplateCardinality();
    void boundsProcessOutputAndProgressLines();
    void surfacesAndRetriesRecoveryPersistenceFailure();
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
    void savesProjectsAtomically();
    void writesRecoverySnapshotsAtomically();
    void guardsGuiRecoveryAcrossProcesses_data();
    void guardsGuiRecoveryAcrossProcesses();
    void failsClosedWhenGuiDataDirectoryIsUnavailable();
    void detectsPartialAndCommitWriteFailures();
    void gatesDirtyDestructiveActions_data();
    void gatesDirtyDestructiveActions();
    void resolvesDirtyDecisionsSafely();
    void retainsTelemetryAfterFailedAsyncLoad();
    void replacesInFlightSourceLoad();
    void shutsDownWithInFlightSourceLoad();
    void opensProjectsTransactionally();
    void serializesPortableProjectSourcesAndMovesFolder();
    void opensProjectsWithMissingSources();
    void fingerprintsSourcesDeterministically();
    void relinksTelemetryWithMismatchPolicy();
    void preservesInterleavedSourceRequests_data();
    void preservesInterleavedSourceRequests();
    void rejectsStaleRelinkResults();
    void restoresSavedProjectsAndPreservesUnknownFields();
    void recoversAndDiscardsSavedChanges();
    void recoversAndDiscardsUnsavedDocuments();
    void discardsUnsavedStateForQuitNewAndOpen();
    void continuesDiscardedQuitWhenRecoveryDeletionFails();
    void continuesDiscardedNewAndOpenWhenRecoveryDeletionFails();
    void leavesRecoveryUntouchedWhenDiscardIsCancelled();
    void preservesNewerAndDifferentRecoveryAfterDiscard();
    void cancelsDiscardWhenTombstoneAndDeletionFail();
    void doesNotApplyDiscardTombstonesToLegacyRecovery();
    void preservesRecoveryAcrossFailedSave();
    void doesNotOfferStaleRecoveryAfterSuccessfulSaveCleanupFailure();
    void offersRecoveryWhenAnotherAppSavedTheSameRevision();
    void classifiesVersionedRecoveryAgainstSavedAuthority();
    void keepsSaveAsRecoveryIdentityWithNewAndExistingProjects();
    void rejectsInvalidVersionedRecoveryMetadata();
    void rejectsMismatchedVersionedRecoveryPayloadIdentity();
    void doesNotTrustMalformedProjectAsRecoveryAuthority();
    void recoversLegacyRecoverySnapshotConservatively();
    void preservesEditsAfterDocumentFirstProjectOpen();
    void syncsOptionalRealRecording();
};

namespace {
// KAN-78: a control is reachable when its centre lies inside the window and
// inside every clipping ancestor, where a scrolling ancestor (Flickable)
// counts as reachable if the control lies within its scrollable content.
bool isInteractiveControl(const QQuickItem *item)
{
    for (const char *type : {"QQuickAbstractButton", "QQuickComboBox", "QQuickTextInput", "QQuickTextEdit",
                             "QQuickSlider", "QQuickRangeSlider", "QQuickSpinBox"})
        if (item->inherits(type)) return true;
    return false;
}

QString describeItem(const QQuickItem *item)
{
    QString text = item->property("text").toString();
    if (text.isEmpty()) text = item->property("placeholderText").toString();
    const QString name = item->objectName().isEmpty() ? QString::fromLatin1(item->metaObject()->className()) : item->objectName();
    const QRectF scene = item->mapRectToScene(QRectF(0, 0, item->width(), item->height()));
    return QStringLiteral("%1 \"%2\" at %3,%4 %5x%6").arg(name, text.left(40)).arg(scene.x(), 0, 'f', 0)
        .arg(scene.y(), 0, 'f', 0).arg(scene.width(), 0, 'f', 0).arg(scene.height(), 0, 'f', 0);
}

// The content item of a Flickable clips nothing on its own.
bool isFlickableContent(const QQuickItem *item)
{
    const auto *parent = item->parentItem();
    return parent && parent->inherits("QQuickFlickable")
        && parent->property("contentItem").value<QQuickItem *>() == item;
}

bool reachableControl(QQuickItem *item, const QRectF &window)
{
    QQuickItem *current = item;
    QPointF centre = item->mapToScene(QPointF(item->width() / 2, item->height() / 2));
    for (QQuickItem *parent = item->parentItem(); parent; parent = parent->parentItem()) {
        if (parent->inherits("QQuickFlickable")) {
            // Scrolling brings anything within the content into view.
            auto *content = parent->property("contentItem").value<QQuickItem *>();
            if (!content) return false;
            const QPointF inContent = current->mapToItem(content, QPointF(current->width() / 2, current->height() / 2));
            // Content starts at the view's origin, which a ListView may move below zero.
            const double originX = parent->property("originX").toDouble(), originY = parent->property("originY").toDouble();
            const double width = std::max(parent->property("contentWidth").toDouble(), parent->width());
            const double height = std::max(parent->property("contentHeight").toDouble(), parent->height());
            if (inContent.x() < originX - 1 || inContent.y() < originY - 1
                || inContent.x() > originX + width + 1 || inContent.y() > originY + height + 1) return false;
            if (parent->width() < 8 || parent->height() < 8) return false;
            current = parent;
            centre = parent->mapToScene(QPointF(parent->width() / 2, parent->height() / 2));
            continue;
        }
        if (parent->clip() && !isFlickableContent(parent)) {
            const QRectF clipRect = parent->mapRectToScene(QRectF(0, 0, parent->width(), parent->height()));
            if (!clipRect.adjusted(-1, -1, 1, 1).contains(centre)) return false;
        }
    }
    return window.adjusted(-1, -1, 1, 1).contains(centre);
}
} // namespace

void TelemetryTests::initTestCase()
{
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    // Default QSettings needs an application identity on every native backend,
    // particularly the Windows registry. Keep tests outside the user's app data
    // without changing the backend exercised by AppController.
    QCoreApplication::setOrganizationName(QStringLiteral("FlappedEarTests"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("tests.flappedear.invalid"));
    QCoreApplication::setApplicationName(QStringLiteral("TelemetryTests-%1").arg(
        QUuid::createUuid().toString(QUuid::WithoutBraces)));
    QStandardPaths::setTestModeEnabled(true);

    const QString key = QStringLiteral("testHarness/roundTrip");
    const QString value = QStringLiteral("native-settings-ready");
    {
        QSettings settings;
        QCOMPARE(settings.status(), QSettings::NoError);
        QVERIFY(settings.isWritable());
        settings.setValue(key, value);
        settings.sync();
        QCOMPARE(settings.status(), QSettings::NoError);
    }
    QSettings restored;
    QCOMPARE(restored.value(key).toString(), value);
    restored.clear();
    restored.sync();
    QCOMPARE(restored.status(), QSettings::NoError);
}

void TelemetryTests::cleanupTestCase()
{
    QSettings settings;
    settings.clear();
    settings.sync();
    QCOMPARE(settings.status(), QSettings::NoError);
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

TelemetrySession speedSession(const double start, const double end, const double valueOffset)
{
    TelemetrySession session;
    TelemetryChannel speed;
    speed.name = "speed";
    speed.unit = "km/h";
    for (double time = start; time <= end; time += 0.2) {
        speed.timestamps.append(time);
        const double sourceTime = time - valueOffset;
        speed.values.append(static_cast<float>(
            50.0 + 18.0 * std::sin(sourceTime * 0.21)
            + 7.0 * std::sin(sourceTime * 0.73) + sourceTime * 0.08));
    }
    session.channels.insert("speed", speed);
    session.aliases.insert("speed", "speed");
    session.duration = end - start;
    session.sampleCount = speed.values.size();
    return session;
}

// A file in the application's QML source directory; components built from
// test strings use one as their base URL so sibling types resolve.
QString qmlSourcePath(const QString &fileName)
{
    return QDir(QStringLiteral(QML_SOURCE_DIR)).filePath(fileName);
}

bool writeBytes(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate)
        && file.write(bytes) == bytes.size();
}

QByteArray readBytes(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return file.readAll();
}

QJsonObject testProject(const double offset, const QJsonObject &extra = {})
{
    WidgetModel widgets;
    widgets.resetDefaults();
    QJsonObject project = extra;
    project.insert(QStringLiteral("version"), 2);
    project.insert(QStringLiteral("scene"), QJsonObject{{QStringLiteral("widgets"), widgets.toJson()}});
    project.insert(QStringLiteral("sync"), QJsonObject{{QStringLiteral("offset"), offset},
                                                        {QStringLiteral("timeScale"), 1.0}});
    project.insert(QStringLiteral("analysis"), QJsonObject{{QStringLiteral("channels"), QJsonArray{}},
                                                            {QStringLiteral("visible"), true}});
    return project;
}

class InjectedProjectWriteDevice final : public ProjectWriteDevice {
public:
    InjectedProjectWriteDevice(
        const bool opens, const qint64 bytesWritten, const bool commits, QString error)
        : m_opens(opens)
        , m_bytesWritten(bytesWritten)
        , m_commits(commits)
        , m_error(std::move(error))
    {
    }

    bool open() override { return m_opens; }
    qint64 write(const QByteArray &) override { return m_bytesWritten; }
    bool commit() override { return m_commits; }
    QString errorString() const override { return m_error; }

private:
    bool m_opens;
    qint64 m_bytesWritten;
    bool m_commits;
    QString m_error;
};

} // namespace

void TelemetryTests::preservesSignedSamplesWithBrakingUpPresentation()
{
    const auto session = VboParser::parse(
        u"[column names]\ntime longacc-calc latacc-calc velocity\n[data]\n0 -0.8 0.2 90\n1 0.4 -0.3 92\n");
    const auto longitudinal = AnalysisController::sessionSeries(session, "longacc-calc", 0, 1, 100);
    QVERIFY(longitudinal.value("brakingUp").toBool());
    QCOMPARE(longitudinal.value("minimum").toDouble(), double(float(-0.8)));
    QCOMPARE(longitudinal.value("maximum").toDouble(), double(float(0.4)));
    QVERIFY(AnalysisController::sessionSeries(session, "longitudinalAcceleration", 0, 1, 100).value("brakingUp").toBool());
    QVERIFY(!AnalysisController::sessionSeries(session, "latacc-calc", 0, 1, 100).value("brakingUp").toBool());
    QVERIFY(!AnalysisController::sessionSeries(session, "velocity", 0, 1, 100).value("brakingUp").toBool());
    QCOMPARE(session.valueAt("longitudinalAcceleration", 0).value(), double(float(-0.8)));
}

void TelemetryTests::usesOverlaysIdentityWithItsOwnStorage()
{
    const QString oldOrganization = QCoreApplication::organizationName();
    const QString oldDomain = QCoreApplication::organizationDomain();
    const QString oldApplication = QCoreApplication::applicationName();
    const QString oldDisplay = QGuiApplication::applicationDisplayName();
    const auto restore = qScopeGuard([&] {
        QCoreApplication::setOrganizationName(oldOrganization);
        QCoreApplication::setOrganizationDomain(oldDomain);
        QCoreApplication::setApplicationName(oldApplication);
        QGuiApplication::setApplicationDisplayName(oldDisplay);
    });
    // Compare production paths without reading or writing production preferences.
    // KAN-125: FlappedEar Overlays owns its own storage; the previous identity's
    // tree is reached only by LegacyStorageMigration (StorageMigrationTests).
    QCoreApplication::setOrganizationName("FlappedEar");
    QCoreApplication::setOrganizationDomain("flappedear.com");
    QCoreApplication::setApplicationName(ApplicationIdentity::legacyStorageName);
    QGuiApplication::setApplicationDisplayName("FlappedEar Telemetry");
    const QString legacySettingsPath = QSettings().fileName();
    const QString legacyDataPath = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    ApplicationIdentity::initialize();
    QCOMPARE(QGuiApplication::applicationDisplayName(), QString("FlappedEar Overlays"));
    QCOMPARE(QCoreApplication::applicationName(), QString("FlappedEar Overlays"));
    QCOMPARE(QCoreApplication::organizationName(), QString("FlappedEar"));
    QCOMPARE(QCoreApplication::organizationDomain(), QString("flappedear.com"));
    QVERIFY(QSettings().fileName() != legacySettingsPath);
    const QString dataPath = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QVERIFY(dataPath != legacyDataPath);
    QVERIFY(dataPath.endsWith(QStringLiteral("FlappedEar Overlays")));
    QCOMPARE(QFileInfo(ProjectRecoveryStore().path()).absolutePath(), QFileInfo(dataPath + "/x").absolutePath());
}

void TelemetryTests::reopensPreferencesProjectAndRecoveryAfterDisplayRename()
{
    const QString oldDisplay = QGuiApplication::applicationDisplayName();
    const auto restore = qScopeGuard([&] { QGuiApplication::setApplicationDisplayName(oldDisplay); });
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings; settings.clear();
    const QString projectPath = directory.filePath("existing.fetproject");
    const QString recoveryPath = directory.filePath("recovery.json");
    const QByteArray projectBytes = QJsonDocument(testProject(1.25)).toJson();
    QVERIFY(writeBytes(projectPath, projectBytes));
    settings.setValue("project/path", projectPath);
    settings.setValue("analysis/windowWidth", 777);
    settings.sync();
    QGuiApplication::setApplicationDisplayName("FlappedEar Telemetry");
    {
        AppController controller(nullptr, recoveryPath);
        QTRY_VERIFY(!controller.projectLoading());
        QCOMPARE(controller.syncOffset(), 1.25);
        controller.setSyncOffset(7.0);
        controller.m_document.writeRecoverySnapshot();
        QVERIFY(QFileInfo(recoveryPath).isFile());
    }
    QGuiApplication::setApplicationDisplayName(ApplicationIdentity::displayName);
    {
        AppController controller(nullptr, recoveryPath);
        QVERIFY(controller.recoveryPending());
        controller.resolveStartupRecovery("recover");
        QTRY_VERIFY(!controller.projectLoading());
        QCOMPARE(controller.syncOffset(), 7.0);
        QCOMPARE(controller.m_settings.value("analysis/windowWidth").toInt(), 777);
        QCOMPARE(controller.projectPath().toLocalFile(), QFileInfo(projectPath).canonicalFilePath());
        QVERIFY(controller.dirty());
        QCOMPARE(readBytes(projectPath), projectBytes);
        QVERIFY(controller.saveCurrentProject());
    }
    AppController reopened(nullptr, recoveryPath);
    QTRY_VERIFY(!reopened.projectLoading());
    QCOMPARE(reopened.syncOffset(), 7.0);
    QVERIFY(!reopened.dirty());
}

void TelemetryTests::persistsEventSelectionAndRunLocalSync()
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
    controller.m_activeTemplateId = QStringLiteral("test-template");
    controller.setSyncOffset(9.0);
    controller.setTimeScale(1.002);
    QVERIFY(controller.selectEventRun("run-b"));
    QTRY_COMPARE(controller.vboLoadState(), QStringLiteral("ready"));
    QCOMPARE(controller.sampleCount(), 3);
    QVERIFY(controller.lapSummaries().isEmpty());
    QCOMPARE(controller.syncOffset(), -1.5);
    QCOMPARE(controller.timeScale(), 1.0);
    QCOMPARE(controller.videoLoadState(), QStringLiteral("idle"));
    QVERIFY(controller.videoSource().isEmpty());
    QCOMPARE(controller.activeTemplateId(), QStringLiteral("test-template"));
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
    QCOMPARE(controller.syncOffset(), 9.0);
    QCOMPARE(controller.timeScale(), 1.002);
    QCOMPARE(controller.lapSummaries().size(), 3);
    QCOMPARE(controller.videoLoadState(), QStringLiteral("missing"));
    QCOMPARE(controller.lastSavedRevision(), revision);
}

void TelemetryTests::recoversEventAndRelinksOnlyActiveSource()
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
        controller.setSyncOffset(8.0);
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

void TelemetryTests::rejectsInvalidEventWithoutReplacingDocument()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings; settings.clear(); settings.sync();
    AppController controller(nullptr, directory.filePath("recovery.json"));
    QVERIFY(controller.m_document.beginProjectLoad(directory.filePath("event.fetproject"), EventProjectFixture::project()));
    controller.setSyncOffset(7.0);
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

void TelemetryTests::rejectsLateSourceResultsAfterRunSelection()
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
    QCOMPARE(controller.syncOffset(), -1.5);
}

void TelemetryTests::selectsEventRunThroughEditorQml()
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
}

void TelemetryTests::importsSixRunsAndAppendsWithoutDuplicates()
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
    controller.setSyncOffset(3.0); // Appending must retain unsaved active-run edits.
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
    QCOMPARE(controller.syncOffset(), 3.0);
    const auto appended = EventProjectFixture::runs(controller.currentProjectObject());
    for (int i = 0; i < 6; ++i) QCOMPARE(appended[i], savedRuns[i]);
    QVERIFY(controller.saveCurrentProject());
    QVERIFY(controller.m_document.beginProjectLoad(projectPath, QJsonDocument::fromJson(readBytes(projectPath)).object()));
    QTRY_COMPARE(controller.vboLoadState(), QStringLiteral("ready"));
    QCOMPARE(controller.eventRuns().size(), 7);
    QCOMPARE(controller.activeRunId(), active);
    QVERIFY(!controller.dirty());
}

void TelemetryTests::confirmsExplicitSourceGroups()
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

void TelemetryTests::cancelsAndRejectsChangedBatchSources()
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

void TelemetryTests::invalidatesBatchReviewAfterDocumentChanges()
{
    QTemporaryDir directory; QVERIFY(directory.isValid());
    QSettings settings; settings.clear(); settings.sync();
    AppController controller(nullptr, directory.filePath("recovery.json"));
    const QList<QUrl> urls{QUrl::fromLocalFile(QStringLiteral(TEST_FIXTURE_PATH))};
    QVERIFY(controller.m_document.beginBatchImport(urls));
    QTRY_COMPARE(controller.m_document.batchImportState(), QStringLiteral("review"));
    controller.setSyncOffset(8);
    QCOMPARE(controller.m_document.batchImportState(), QStringLiteral("error"));
    QCOMPARE(controller.syncOffset(), 8.0);
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

void TelemetryTests::importsAnalysisRunsAutomatically()
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
    controller.setSyncOffset(4);
    const auto laps = directory.filePath("third.vbo");
    QVERIFY(writeBytes(laps, EventProjectFixture::lapsVbo()));
    QVERIFY(controller.m_document.importAnalysisRuns({}, {vbo, QUrl::fromLocalFile(laps)}));
    QTRY_COMPARE(committed.size(), 2);
    QCOMPARE(controller.eventRuns().size(), 3);
    QCOMPARE(controller.activeRunId(), active);
    QCOMPARE(controller.syncOffset(), 4.0);
    QCOMPARE(controller.m_document.analysisImportMessages().size(), 1);
    const auto path = directory.filePath("outing.fetproject");
    QVERIFY(controller.saveProject(QUrl::fromLocalFile(path)));
    QVERIFY(controller.m_document.beginProjectLoad(path, QJsonDocument::fromJson(readBytes(path)).object()));
    QTRY_COMPARE(controller.vboLoadState(), QStringLiteral("ready"));
    QCOMPARE(controller.eventRuns().size(), 3);
    QCOMPARE(controller.eventName(), QStringLiteral("Track Saturday"));
}

void TelemetryTests::guardsAutomaticAnalysisImport()
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
    controller.setSyncOffset(2);
    QTRY_VERIFY(!controller.m_document.m_batchPending);
    QVERIFY(controller.eventRuns().isEmpty());
    QCOMPARE(controller.syncOffset(), 2.0);
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

void TelemetryTests::findsTheDayBestLapWithoutTheAnalysis()
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

void TelemetryTests::restoresDayDecisionsAfterMoveMissingRelinkAndRecovery()
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

void TelemetryTests::automaticallyGroupsPrivateTrackDay()
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

void TelemetryTests::lapExclusionPolicySharesRankingAndRenderInputs()
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

void TelemetryTests::lapExclusionsSurviveSaveRecoveryAndInvalidateSafely()
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

void TelemetryTests::keepsAnalysisStateThroughOverlayEdits()
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

void TelemetryTests::recordsVboUtcChronology()
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

void TelemetryTests::formatsElapsedTimes()
{
    QCOMPARE(AppController::formatElapsedTime(100.838), QString("1:40.838"));
    QCOMPARE(AppController::formatElapsedTime(109.8976), QString("1:49.898"));
    QCOMPARE(AppController::formatElapsedTime(59.9996), QString("1:00.000")); // rounds before choosing the form
    QCOMPARE(AppController::formatElapsedTime(28.662), QString("28.662 s"));
    QCOMPARE(AppController::formatElapsedTime(0.05), QString("0.050 s"));
    QCOMPARE(AppController::formatElapsedTime(-2.5), QString("-2.500 s"));
    QCOMPARE(AppController::formatElapsedTime(3661.0), QString("61:01.000"));
    QCOMPARE(AppController::formatElapsedTime(std::numeric_limits<double>::quiet_NaN()), QString("—"));
}

void TelemetryTests::presentsBrakingUpInGForceWidgets()
{
    QQmlEngine engine;
    QSignalSpy warnings(&engine, &QQmlEngine::warnings);
    QQmlComponent component(&engine);
    component.setData(R"QML(
import QtQuick
import "widgets"
Item {
    id: root
    width: 240; height: 240
    property real acceleration: -0.5
    property bool inverted: false
    QtObject {
        id: frameData
        property var widgetSettings: ({invertLongitudinal: root.inverted})
        property real sceneScale: 1
        property real labelScale: 1
        property string family: "Helvetica Neue"
        property color primary: "white"
        property color accent: "orange"
        function raw(source, alias) { return alias === "longitudinalAcceleration" ? root.acceleration : 0; }
    }
    F1GForceRadarWidget { frame: frameData }
}
)QML", QUrl::fromLocalFile(qmlSourcePath("Main.qml")));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    std::unique_ptr<QObject> root(component.create());
    QVERIFY2(root, qPrintable(component.errorString()));
    const auto dots = root->findChildren<QQuickItem *>(QStringLiteral("gForceDot"));
    QCOMPARE(dots.size(), 1);
    for (auto *dot : dots) {
        QVERIFY(dot->isVisible());
        QVERIFY(dot->y() + dot->height() / 2 < 120);
    }
    root->setProperty("acceleration", 0.5);
    for (auto *dot : dots) QVERIFY(dot->y() + dot->height() / 2 > 120);
    root->setProperty("inverted", true);
    for (auto *dot : dots) QVERIFY(dot->y() + dot->height() / 2 < 120);
    QCOMPARE(warnings.size(), 0);
}

void TelemetryTests::rejectsBatchLinksWithDifferentPersistedFormats()
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

void TelemetryTests::rejectsUnsafeExportPaths()
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

void TelemetryTests::capturesExportTargetIdentity()
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

void TelemetryTests::rejectsChangedExportTargets()
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

void TelemetryTests::rejectsSymlinkExportTargets()
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

void TelemetryTests::preservesExistingExportTargetOnFailures_data()
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

void TelemetryTests::preservesExistingExportTargetOnFailures()
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

void TelemetryTests::commitsNewAndReplacementExports()
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

void TelemetryTests::guardsGuiRecoveryAcrossProcesses_data()
{
    QTest::addColumn<bool>("crash");
    QTest::newRow("clean-exit") << false;
    QTest::newRow("crashed-owner") << true;
}

void TelemetryTests::guardsGuiRecoveryAcrossProcesses()
{
    QFETCH(bool, crash);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString recoveryPath = directory.filePath("project-recovery.json");
    const QByteArray snapshot("unsaved recovery must survive both contenders");
    QVERIFY(writeBytes(recoveryPath, snapshot));
    QProcess owner;
    owner.start(QStringLiteral(GUI_SESSION_LOCK_HELPER_PATH), {directory.path()});
    const auto stopOwner = qScopeGuard([&owner] {
        if (owner.state() != QProcess::NotRunning) {
            owner.kill();
            owner.waitForFinished(5'000);
        }
    });
    QVERIFY2(owner.waitForStarted(5'000), qPrintable(owner.errorString()));
    QByteArray output;
    // stdio can translate the helper's newline to CRLF on Windows.
    QTRY_VERIFY2_WITH_TIMEOUT((output += owner.readAllStandardOutput()).trimmed() == "locked",
        qPrintable(QStringLiteral("Helper output: %1; stderr: %2; state: %3; exit: %4")
            .arg(QString::fromUtf8(output), QString::fromUtf8(owner.readAllStandardError()))
            .arg(static_cast<int>(owner.state())).arg(owner.exitCode())), 5'000);
    const QString lockPath = directory.filePath("gui-session.lock");
    const QByteArray originalLock = readBytes(lockPath);
    QVERIFY(!originalLock.isEmpty());
    {
        GuiSessionLock contender(directory.path());
        QString error;
        QVERIFY(!contender.tryAcquire(&error));
        QVERIFY(error.contains("already running"));
        QCOMPARE(readBytes(recoveryPath), snapshot);
    }
    // Destroying an unsuccessful contender must not unlock the live owner.
    QCOMPARE(readBytes(lockPath), originalLock);
    GuiSessionLock next(directory.path());
    QVERIFY(!next.tryAcquire());
    if (crash) owner.kill();
    else QCOMPARE(owner.write("\n"), qint64(1));
    QVERIFY(owner.waitForFinished(5'000));
    if (!crash) QCOMPARE(owner.exitCode(), 0);
    QString error;
    QVERIFY2(next.tryAcquire(&error), qPrintable(error));
    QVERIFY(next.tryAcquire()); // Same guard is idempotent.
    QCOMPARE(readBytes(recoveryPath), snapshot);
    GuiSessionLock third(directory.path());
    QVERIFY(!third.tryAcquire());
}

void TelemetryTests::failsClosedWhenGuiDataDirectoryIsUnavailable()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString file = directory.filePath("not-a-directory");
    QVERIFY(writeBytes(file, "keep"));
    GuiSessionLock guard(file);
    QString error;
    QVERIFY(!guard.tryAcquire(&error));
    QVERIFY(!error.isEmpty());
    QCOMPARE(readBytes(file), QByteArray("keep"));
}

void TelemetryTests::savesProjectsAtomically()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath("project.fetproject");
    const QByteArray original = R"({"version":2,"scene":{"widgets":[]}})";
    const QByteArray replacement = R"({"version":2,"scene":{"widgets":[{"type":"speed"}]}})";
    QVERIFY(writeBytes(path, original));

    const ProjectWriter writer;
    const ProjectWriter::Result success = writer.write(path, replacement);
    QVERIFY2(success.success, qPrintable(success.error));
    QFile saved(path);
    QVERIFY(saved.open(QIODevice::ReadOnly));
    QCOMPARE(saved.readAll(), replacement);
    saved.close();

#ifndef Q_OS_WIN
    const QFile::Permissions originalPermissions = QFileInfo(directory.path()).permissions();
    QVERIFY(QFile::setPermissions(
        directory.path(), QFileDevice::ReadOwner | QFileDevice::ExeOwner));
    const auto restorePermissions = qScopeGuard([&] {
        QFile::setPermissions(directory.path(), originalPermissions);
    });
    const ProjectWriter::Result failure = writer.write(path, QByteArray("corrupting replacement"));
    QVERIFY(!failure.success);
    QFile unchanged(path);
    QVERIFY(unchanged.open(QIODevice::ReadOnly));
    QCOMPARE(unchanged.readAll(), replacement);
#endif
}

void TelemetryTests::writesRecoverySnapshotsAtomically()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("recovery.json"));
    const ProjectRecoveryStore store(path);
    QJsonObject firstProject = testProject(1.0);
    firstProject.insert(QStringLiteral("documentState"), QJsonObject{
        {QStringLiteral("id"), QStringLiteral("document-identity")},
        {QStringLiteral("savedRevision"), QStringLiteral("1")},
    });
    const ProjectRecoverySnapshot first{
        QStringLiteral("saved.fetproject"), QStringLiteral("document-identity"), 2, 1,
        QStringLiteral("2026-08-22T12:00:00.000Z"), firstProject, true};
    QString error;
    QVERIFY2(store.write(first, &error), qPrintable(error));
    ProjectRecoverySnapshot loaded;
    QVERIFY2(store.load(&loaded, &error), qPrintable(error));
    QCOMPARE(loaded.originalProjectPath, first.originalProjectPath);
    QCOMPARE(loaded.revision, first.revision);
    QCOMPARE(loaded.lastSavedRevision, first.lastSavedRevision);
    QCOMPARE(loaded.project, first.project);

#ifndef Q_OS_WIN
    const QFile::Permissions originalPermissions = QFileInfo(directory.path()).permissions();
    QVERIFY(QFile::setPermissions(
        directory.path(), QFileDevice::ReadOwner | QFileDevice::ExeOwner));
    const auto restorePermissions = qScopeGuard([&] {
        QFile::setPermissions(directory.path(), originalPermissions);
    });
    ProjectRecoverySnapshot replacement = first;
    replacement.revision = 3;
    QJsonObject replacementSync = replacement.project.value(QStringLiteral("sync")).toObject();
    replacementSync.insert(QStringLiteral("offset"), 9.0);
    replacement.project.insert(QStringLiteral("sync"), replacementSync);
    QVERIFY(!store.write(replacement, &error));
    QVERIFY2(store.load(&loaded, &error), qPrintable(error));
    QCOMPARE(loaded.revision, first.revision);
    QCOMPARE(loaded.project, first.project);
#endif
}

void TelemetryTests::detectsPartialAndCommitWriteFailures()
{
    const QByteArray payload("complete serialized project");
    const ProjectWriter partialWriter([&](const QString &) {
        return std::make_unique<InjectedProjectWriteDevice>(
            true, payload.size() - 1, true, QStringLiteral("injected partial write"));
    });
    const ProjectWriter::Result partial = partialWriter.write("project.fetproject", payload);
    QVERIFY(!partial.success);
    QVERIFY(partial.error.contains(QStringLiteral("incomplete")));

    const ProjectWriter commitWriter([&](const QString &) {
        return std::make_unique<InjectedProjectWriteDevice>(
            true, payload.size(), false, QStringLiteral("injected commit failure"));
    });
    const ProjectWriter::Result commit = commitWriter.write("project.fetproject", payload);
    QVERIFY(!commit.success);
    QVERIFY(commit.error.contains(QStringLiteral("commit")));
}

void TelemetryTests::gatesDirtyDestructiveActions_data()
{
    QTest::addColumn<int>("actionValue");
    QTest::newRow("New dirty")
        << static_cast<int>(ProjectDocumentState::DestructiveAction::NewProject);
    QTest::newRow("Open dirty")
        << static_cast<int>(ProjectDocumentState::DestructiveAction::OpenProject);
    QTest::newRow("Quit dirty")
        << static_cast<int>(ProjectDocumentState::DestructiveAction::Quit);
}

void TelemetryTests::gatesDirtyDestructiveActions()
{
    QFETCH(int, actionValue);
    const auto action = static_cast<ProjectDocumentState::DestructiveAction>(actionValue);
    ProjectDocumentState document;
    document.reset("current.fetproject");
    QCOMPARE(document.request(action), ProjectDocumentState::RequestResult::ContinueImmediately);
    QCOMPARE(document.takePendingAction(), action);

    document.markChanged();
    QVERIFY(document.dirty());
    QCOMPARE(document.request(action), ProjectDocumentState::RequestResult::DecisionRequired);
    QCOMPARE(document.pendingAction(), action);
}

void TelemetryTests::resolvesDirtyDecisionsSafely()
{
    ProjectDocumentState document;
    document.reset("current.fetproject");
    document.markChanged();
    const quint64 changedRevision = document.revision();
    QCOMPARE(document.request(ProjectDocumentState::DestructiveAction::OpenProject),
             ProjectDocumentState::RequestResult::DecisionRequired);

    // A failed save leaves both dirty state and the pending destructive action intact.
    QVERIFY(document.dirty());
    QCOMPARE(document.lastSavedRevision(), quint64(0));
    QCOMPARE(document.pendingAction(), ProjectDocumentState::DestructiveAction::OpenProject);

    // A successful save clears dirty, after which the requested action can continue.
    document.markSaved("current.fetproject");
    QVERIFY(!document.dirty());
    QCOMPARE(document.lastSavedRevision(), changedRevision);
    QCOMPARE(document.takePendingAction(), ProjectDocumentState::DestructiveAction::OpenProject);

    // Don't Save continues; Cancel retains the current dirty document.
    document.markChanged();
    QCOMPARE(document.request(ProjectDocumentState::DestructiveAction::NewProject),
             ProjectDocumentState::RequestResult::DecisionRequired);
    QCOMPARE(document.takePendingAction(), ProjectDocumentState::DestructiveAction::NewProject);
    QVERIFY(document.dirty());
    QCOMPARE(document.request(ProjectDocumentState::DestructiveAction::Quit),
             ProjectDocumentState::RequestResult::DecisionRequired);
    document.cancelPendingAction();
    QCOMPARE(document.pendingAction(), ProjectDocumentState::DestructiveAction::None);
    QVERIFY(document.dirty());
    QCOMPARE(document.projectPath(), QString("current.fetproject"));
}

void TelemetryTests::preservesMissingTelemetryGaps()
{
    TelemetrySession session;
    TelemetryChannel speed;
    speed.name = QStringLiteral("speed");
    speed.timestamps = {0.0, 0.1, 0.2};
    speed.values = {10.0F, std::numeric_limits<float>::quiet_NaN(), 30.0F};
    session.channels.insert(speed.name, speed);
    session.aliases.insert(QStringLiteral("speed"), speed.name);

    QVERIFY(!session.valueAt("speed", 0.05));
    QVERIFY(!session.valueAt("speed", 0.1));
    QVERIFY(!session.valueAt("speed", 0.15));
    QVERIFY(!session.valueAt("speed", 0.15, InterpolationMode::Previous));
    QVERIFY(!session.valueAt("speed", 0.11, InterpolationMode::Nearest));
    const QVector<QVector<QPointF>> gapSegments = session.sampledSegments("speed", 0.0, 0.2, 5);
    QCOMPARE(gapSegments.size(), 2);
    QCOMPARE(gapSegments.front(), QVector<QPointF>({QPointF(0.0, 10.0)}));
    QCOMPARE(gapSegments.back(), QVector<QPointF>({QPointF(0.2, 30.0)}));
    QVERIFY(!session.valueAt("speed", std::numeric_limits<double>::quiet_NaN()));
    QVERIFY(!session.valueAt("speed", std::numeric_limits<double>::infinity()));
    QVERIFY(!session.valueAt("speed", -std::numeric_limits<double>::infinity()));

    TelemetryChannel edgeValues;
    edgeValues.name = QStringLiteral("edge");
    edgeValues.timestamps = {0.0, 1.0, 2.0, 3.0};
    edgeValues.values = {std::numeric_limits<float>::quiet_NaN(), 10.0F, 20.0F,
                         std::numeric_limits<float>::quiet_NaN()};
    session.channels.insert(edgeValues.name, edgeValues);
    QVERIFY(!session.valueAt("edge", 0.0));
    QVERIFY(!session.valueAt("edge", 0.5));
    QCOMPARE(session.valueAt("edge", 1.0).value(), 10.0);
    QCOMPARE(session.valueAt("edge", 1.5).value(), 15.0);
    QVERIFY(!session.valueAt("edge", 2.5));
    QVERIFY(!session.valueAt("edge", 3.0));
    QVERIFY(!session.valueAt("edge", -0.001));
    QVERIFY(!session.valueAt("edge", 3.001));

    TelemetryChannel malformed;
    malformed.name = QStringLiteral("malformed");
    malformed.timestamps = {0.0, 1.0};
    malformed.values = {10.0F};
    session.channels.insert(malformed.name, malformed);
    QVERIFY(!session.valueAt("malformed", 0.0));

    TelemetryRenderContext context;
    context.setSession(&session);
    context.setTime(0.1);
    QCOMPARE(context.telemetryValue("speed").toDouble(), 10.0);
    QCOMPARE(context.valueText("speed"), QStringLiteral("10.00"));

    TelemetrySession positionSession;
    TelemetryChannel latitude;
    latitude.name = QStringLiteral("latitude");
    latitude.timestamps = {0.0, 1.0, 2.0};
    latitude.values = {52.0F, std::numeric_limits<float>::quiet_NaN(), 52.001F};
    TelemetryChannel longitude;
    longitude.name = QStringLiteral("longitude");
    longitude.timestamps = latitude.timestamps;
    longitude.values = {21.0F, std::numeric_limits<float>::quiet_NaN(), 21.001F};
    positionSession.channels.insert(latitude.name, latitude);
    positionSession.channels.insert(longitude.name, longitude);
    positionSession.aliases.insert(QStringLiteral("latitude"), latitude.name);
    positionSession.aliases.insert(QStringLiteral("longitude"), longitude.name);
    const TrackGeometry geometry = buildTrackGeometry(positionSession);
    QVERIFY(geometry.valid);
    QVERIFY(!currentTrackPoint(positionSession, -0.1, geometry));
    QVERIFY(!currentTrackPoint(positionSession, 1.0, geometry));
    QVERIFY(!currentTrackPoint(positionSession, 2.1, geometry));
}

void TelemetryTests::filtersOverlayPresentationValues()
{
    TelemetrySession session;
    const auto addChannel = [&session](const QString &name, QVector<float> values) {
        TelemetryChannel channel;
        channel.name = name;
        channel.timestamps = {0.0, 0.1, 0.2, 0.3};
        channel.values = std::move(values);
        session.channels.insert(name, channel);
        session.aliases.insert(name, name);
    };
    addChannel(QStringLiteral("speed"), {0.0F, 10.0F, std::numeric_limits<float>::quiet_NaN(), 30.0F});
    addChannel(QStringLiteral("rpm"), {0.0F, 1000.0F, std::numeric_limits<float>::quiet_NaN(), 3000.0F});
    addChannel(QStringLiteral("throttle"), {0.0F, 50.0F, std::numeric_limits<float>::quiet_NaN(), 100.0F});
    addChannel(QStringLiteral("brake"), {0.0F, 25.0F, std::numeric_limits<float>::quiet_NaN(), 0.0F});
    addChannel(QStringLiteral("lateralAcceleration"), {0.0F, 0.5F, std::numeric_limits<float>::quiet_NaN(), 1.0F});
    addChannel(QStringLiteral("longitudinalAcceleration"), {0.0F, -0.5F, std::numeric_limits<float>::quiet_NaN(), -1.0F});

    TelemetryRenderContext context;
    context.setSession(&session);
    context.setTime(0.0);
    QCOMPARE(context.telemetryValue("speed").toDouble(), 0.0);
    QCOMPARE(context.telemetryValue("rpm").toDouble(), 0.0);
    QCOMPARE(context.telemetryValue("throttle").toDouble(), 0.0);
    QCOMPARE(context.telemetryValue("brake").toDouble(), 0.0);
    QCOMPARE(context.telemetryValue("lateralAcceleration").toDouble(), 0.0);
    QCOMPARE(context.telemetryValue("longitudinalAcceleration").toDouble(), 0.0);
    QVERIFY(!context.telemetryValue("missing").isValid());

    context.setTime(0.2);
    QVERIFY(context.telemetryValue("speed").isValid());
    QVERIFY(context.telemetryValue("rpm").isValid());
    QVERIFY(context.telemetryValue("throttle").isValid());
    QVERIFY(context.telemetryValue("brake").isValid());
    QVERIFY(context.telemetryValue("lateralAcceleration").isValid());
    QVERIFY(context.telemetryValue("longitudinalAcceleration").isValid());

    context.setTime(1.1);
    QVERIFY(!context.telemetryValue("speed").isValid());
    QCOMPARE(context.valueText("speed"), QStringLiteral("—"));
}

void TelemetryTests::samplesTelemetryRanges()
{
    const auto session = VboParser::parse(
        u"[column names]\ntime speed\n[data]\n0 0\n1 10\n2 20\n3 30\n4 40");
    const QVector<QVector<QPointF>> segments = session.sampledSegments("speed", 1.0, 3.0, 5);
    QCOMPARE(segments.size(), 1);
    QCOMPARE(segments.front(), QVector<QPointF>({QPointF(1.0, 10.0), QPointF(2.0, 20.0), QPointF(3.0, 30.0)}));
    QVERIFY(session.sampledSegments("missing", 0.0, 1.0, 10).isEmpty());
    QVERIFY(session.sampledSegments("speed", 0.0, 1.0, 1).isEmpty());

    // A real range/channel problem must be distinguishable from a genuinely
    // empty overlap, so the UI can tell "no data here" from "something is wrong".
    SampledSegmentsStatus status = SampledSegmentsStatus::Ok;
    QVERIFY(session.sampledSegments("speed", 10.0, 20.0, 5, &status).isEmpty());
    QCOMPARE(status, SampledSegmentsStatus::Ok);
    QVERIFY(session.sampledSegments("missing", 0.0, 1.0, 10, &status).isEmpty());
    QCOMPARE(status, SampledSegmentsStatus::ChannelMissing);
    QVERIFY(session.sampledSegments("speed", 0.0, 1.0, 1, &status).isEmpty());
    QCOMPARE(status, SampledSegmentsStatus::InvalidRange);
    QVERIFY(session.sampledSegments("speed", std::numeric_limits<double>::quiet_NaN(), 1.0, 5, &status).isEmpty());
    QCOMPARE(status, SampledSegmentsStatus::InvalidRange);
    TelemetrySession malformed;
    TelemetryChannel malformedChannel;
    malformedChannel.name = QStringLiteral("bad");
    malformedChannel.timestamps = {0.0, 1.0};
    malformed.channels.insert(QStringLiteral("bad"), malformedChannel);
    QVERIFY(malformed.sampledSegments("bad", 0.0, 1.0, 5, &status).isEmpty());
    QCOMPARE(status, SampledSegmentsStatus::ChannelMalformed);

    TelemetrySession extrema;
    TelemetryChannel signal;
    signal.name = QStringLiteral("rpm");
    for (int index = 0; index < 1000; ++index) {
        signal.timestamps.append(index / 100.0);
        signal.values.append(index == 513 ? 9000.0F : (index % 2 == 0 ? 1000.0F : 1001.0F));
    }
    extrema.channels.insert(signal.name, signal);
    const QVector<QVector<QPointF>> reduced = extrema.sampledSegments("rpm", 0.0, 10.0, 20);
    QCOMPARE(reduced.size(), 1);
    qsizetype pointCount = 0;
    bool retainedPeak = false;
    for (const QPointF &point : reduced.front()) {
        ++pointCount;
        retainedPeak = retainedPeak || point.y() == 9000.0;
    }
    QVERIFY(pointCount <= 40);
    QVERIFY(retainedPeak);

    TelemetrySession flat;
    TelemetryChannel flatSignal;
    flatSignal.name = QStringLiteral("flat");
    for (int index = 0; index < 100; ++index) {
        flatSignal.timestamps.append(index / 10.0);
        flatSignal.values.append(42.0F);
    }
    flat.channels.insert(flatSignal.name, flatSignal);
    const QVector<QVector<QPointF>> flatReduced = flat.sampledSegments("flat", 0.0, 10.0, 10);
    QCOMPARE(flatReduced.size(), 1);
    QVERIFY(flatReduced.front().size() <= 20);
    QVERIFY(std::all_of(flatReduced.front().cbegin(), flatReduced.front().cend(), [](const QPointF &point) {
        return point.y() == 42.0;
    }));

    TelemetrySession timestampGap;
    TelemetryChannel gapped;
    gapped.name = QStringLiteral("brake");
    gapped.timestamps = {0.0, 0.1, 0.2, 2.0, 2.1, 2.2};
    gapped.values = {0.0F, 10.0F, 20.0F, 80.0F, 90.0F, 100.0F};
    timestampGap.channels.insert(gapped.name, gapped);
    timestampGap.aliases.insert(gapped.name, gapped.name);
    const QVector<QVector<QPointF>> separated = timestampGap.sampledSegments("brake", 0.0, 2.2, 20);
    QCOMPARE(separated.size(), 2);
    QCOMPARE(separated.front().back(), QPointF(0.2, 20.0));
    QCOMPARE(separated.back().front(), QPointF(2.0, 80.0));

    TelemetryRenderContext presentation;
    presentation.setSession(&timestampGap);
    presentation.setTime(0.5);
    QVERIFY(presentation.telemetryValue("brake").isValid());
    presentation.setTime(1.0);
    QVERIFY(!presentation.telemetryValue("brake").isValid());

    const QVector<QVector<QPointF>> shortRange = extrema.sampledSegments("rpm", 5.13, 5.13, 2);
    QCOMPARE(shortRange.size(), 1);
    QCOMPARE(shortRange.front(), QVector<QPointF>({QPointF(5.13, 9000.0)}));
}

void TelemetryTests::acceptsVboMicrosecondConversionBoundary()
{
    const double limit = 0x1p63;
    const double safeSeconds = std::nextafter(limit / 1'000'000.0, 0.0);
    QVERIFY(safeSeconds * 1'000'000.0 < limit);
    const auto session = VboParser::parse(
        QStringLiteral("[column names]\ntime speed\n[data]\n0 1\n%1 2")
            .arg(safeSeconds, 0, 'g', 17));
    QCOMPARE(session.sampleCount, 2);
    QCOMPARE(session.duration, safeSeconds);
    const auto fingerprint = ProjectSourceReferenceCodec::telemetryFingerprint(
        QStringLiteral(TEST_FIXTURE_PATH), session);
    const qint64 durationUs = fingerprint.value("durationUs").toInteger();
    QVERIFY(durationUs > 0);
    QCOMPARE(durationUs, static_cast<qint64>(std::llround(safeSeconds * 1'000'000.0)));
    QCOMPARE(session.channels.value("speed").timestamps, QVector<double>({0, safeSeconds}));

    const auto negativeOrigin = VboParser::parse(
        u"[column names]\ntime speed\n[data]\n-1 1\n0 2\n0.000001 3");
    QCOMPARE(negativeOrigin.startTime, -1.0);
    QCOMPARE(negativeOrigin.channels.value("speed").timestamps, QVector<double>({0, 1, 1.000001}));
}

void TelemetryTests::publishesCurrentLapAfterFirstAcceptedPass()
{
    TelemetrySession session;
    TelemetryChannel speed;
    speed.name = QStringLiteral("speed");
    speed.timestamps = {10.0, 12.0, 20.0};
    speed.values = {72.0F, 90.0F, 108.0F};
    session.channels.insert(speed.name, speed);
    session.aliases.insert(QStringLiteral("speed"), speed.name);
    session.duration = 20.0;

    LapSession laps;
    laps.status = LapSessionStatus::InsufficientPasses;
    laps.acceptedPasses.append({10.0, 0.0, 1, 0.5, 20.0, 20.0});

    TelemetryRenderContext context;
    context.setSession(&session);
    context.setLapSession(laps);
    context.setTime(12.0);
    const QVariantMap timing = context.lapTiming();

    QVERIFY(timing.value(QStringLiteral("available")).toBool());
    QCOMPARE(timing.value(QStringLiteral("state")).toString(), QStringLiteral("running"));
    QCOMPARE(timing.value(QStringLiteral("currentLapNumber")).toInt(), 1);
    QCOMPARE(timing.value(QStringLiteral("currentElapsedSeconds")).toDouble(), 2.0);
    QCOMPARE(timing.value(QStringLiteral("currentSpeedKmh")).toDouble(), 90.0);
    QVERIFY(!timing.contains(QStringLiteral("bestLapSeconds")));
    QVERIFY(!timing.contains(QStringLiteral("liveDeltaSeconds")));
}

void TelemetryTests::publishesAndClearsLapStateWithController()
{
    QSettings settings;
    settings.clear();
    settings.sync();
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = directory.filePath(QStringLiteral("laps.vbo"));
    const QByteArray vbo =
        "[header]\ncoordinate units = degrees\n[laptiming]\n"
        "Start 21.0000 52.0000 21.0000 52.0002 start\n"
        "[column names]\n"
        "time latitude longitude\n"
        "[data]\n"
        "0 52.0001 21.0002\n1 52.0001 21.0002\n2 52.0001 20.9998\n"
        "3 52.0008 20.9998\n4 52.0008 21.0002\n5 52.0001 21.0002\n"
        "6 52.0001 20.9998\n7 52.0008 20.9998\n8 52.0008 21.0002\n"
        "10 52.0001 21.0002\n11 52.0001 20.9998\n12 52.0008 20.9998\n"
        "13 52.0008 21.0002\n14 52.0001 21.0002\n15 52.0001 20.9998\n";
    QVERIFY(writeBytes(source, vbo));

    AppController controller;
    controller.loadVbo(QUrl::fromLocalFile(source));
    QTRY_COMPARE(controller.vboLoadState(), QStringLiteral("ready"));
    QCOMPARE(controller.lapTimingStatus(), QStringLiteral("3 complete laps"));
    const QVariantList published = controller.lapSummaries();
    QCOMPARE(published.size(), 3);
    QCOMPARE(published[0].toMap().value(QStringLiteral("number")).toInt(), 1);
    QVERIFY(published[0].toMap().value(QStringLiteral("isBest")).toBool());

    controller.requestNewProject();
    QCOMPARE(controller.pendingDestructiveAction(), QStringLiteral("new"));
    controller.resolveDestructiveAction(QStringLiteral("discard"));
    QCOMPARE(controller.lapTimingStatus(), QStringLiteral("Open telemetry for lap timing"));
    QVERIFY(controller.lapSummaries().isEmpty());
    QCOMPARE(controller.renderContext()->lapTiming().value(QStringLiteral("state")).toString(),
             QStringLiteral("unavailable"));
}

void TelemetryTests::showsOneHotlapAndExportsItByDefault()
{
    // The Current lap tile's hotlap option: one chosen lap only -- 0:00 until
    // its start/finish crossing, running during the lap, the final time held
    // after it -- chosen from a list or from the lap at the playhead; the
    // export dialog then opens on that lap.
    const QString ffmpeg = FfmpegTools::ffmpegPath();
    if (ffmpeg.isEmpty()) QSKIP("FFmpeg is unavailable for the hotlap test.");
    QSettings settings; settings.clear(); settings.sync();
    QTemporaryDir directory; QVERIFY(directory.isValid());
    const QString videoPath = directory.filePath(QStringLiteral("laps.mp4"));
    QProcess encoder;
    encoder.start(ffmpeg, {"-hide_banner", "-loglevel", "error", "-y", "-f", "lavfi", "-i",
                           "color=c=black:s=32x32:r=30:d=25", "-c:v", "mpeg4", "-q:v", "3", videoPath});
    QVERIFY(encoder.waitForFinished(30'000) && encoder.exitCode() == 0);
    const QString vboPath = directory.filePath(QStringLiteral("laps.vbo"));
    QVERIFY(writeBytes(vboPath,
        "[header]\ncoordinate units = degrees\n[laptiming]\n"
        "Start 21.0000 52.0000 21.0000 52.0002 start\n"
        "[column names]\ntime latitude longitude\n[data]\n"
        "0 52.0001 21.0002\n1 52.0001 21.0002\n2 52.0001 20.9998\n"
        "3 52.0008 20.9998\n4 52.0008 21.0002\n5 52.0001 21.0002\n"
        "6 52.0001 20.9998\n7 52.0008 20.9998\n8 52.0008 21.0002\n"
        "10 52.0001 21.0002\n11 52.0001 20.9998\n12 52.0008 20.9998\n"
        "13 52.0008 21.0002\n14 52.0001 21.0002\n15 52.0001 20.9998\n"));
    AppController controller(nullptr, directory.filePath("recovery.json"));
    controller.loadVideo(QUrl::fromLocalFile(videoPath));
    QTRY_COMPARE(controller.videoLoadState(), QStringLiteral("ready"));
    controller.loadVbo(QUrl::fromLocalFile(vboPath));
    QTRY_COMPARE(controller.vboLoadState(), QStringLiteral("ready"));
    const auto laps = controller.lapSummaries();
    QVERIFY(laps.size() >= 2);
    const auto second = laps[1].toMap();
    const int lapNumber = second.value("number").toInt();
    const double start = second.value("startTelemetryTime").toDouble();
    const double duration = second.value("durationSeconds").toDouble();

    auto *model = controller.widgetModel();
    const int tile = model->addWidget("lapCurrent");
    QVERIFY(tile >= 0);
    model->setSetting(tile, "hotlapMode", true);
    // The lap at the playhead, then that lap's timing through the preview.
    controller.setPlaybackTime(start + 0.5);
    QCOMPARE(controller.lapNumberAtPlayback(), lapNumber);
    model->setSetting(tile, "hotlapLap", controller.lapNumberAtPlayback());
    QCOMPARE(model->widget(tile).value("settings").toMap().value("hotlapLap").toInt(), lapNumber);
    model->setSetting(tile, "hotlapLap", -5.0); // malformed input is bounded, never negative
    QCOMPARE(model->widget(tile).value("settings").toMap().value("hotlapLap").toInt(), 0);
    model->setSetting(tile, "hotlapLap", lapNumber);

    QQmlEngine engine; engine.rootContext()->setContextProperty("appController", &controller);
    QSignalSpy warnings(&engine, &QQmlEngine::warnings);
    QQmlComponent component(&engine, QUrl::fromLocalFile(
        qmlSourcePath("Main.qml")));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    std::unique_ptr<QObject> object(component.create());
    auto *window = qobject_cast<QQuickWindow *>(object.get()); QVERIFY(window);
    window->show(); QVERIFY(QTest::qWaitForWindowExposed(window));
    const auto findVisual = [](auto &&self, QQuickItem *item, const std::function<bool(QQuickItem *)> &match) -> QQuickItem * {
        if (match(item)) return item;
        for (auto *child : item->childItems()) if (auto *found = self(self, child, match)) return found;
        return nullptr;
    };
    QQuickItem *hotlapTile = nullptr;
    QTRY_VERIFY((hotlapTile = findVisual(findVisual, window->contentItem(),
        [](QQuickItem *item) { return item->property("hotlap").toBool(); })));
    const auto tileValue = [&] { return hotlapTile->property("metricValue").toDouble(); };
    QVariant formatted;
    const auto tileState = [&] { return hotlapTile->property("hotlapTiming").toMap().value("state").toString(); };
    // Before the lap: 0:00; during: counting; after, even through the next lap: the final time.
    controller.setPlaybackTime(start - 0.5);
    QTRY_COMPARE(tileState(), QString("before"));
    QCOMPARE(tileValue(), 0.0);
    controller.setPlaybackTime(start + 1.0);
    QTRY_COMPARE(tileState(), QString("running"));
    QVERIFY(std::abs(tileValue() - 1.0) < 1e-6);
    controller.setPlaybackTime(start + duration + 3.0);
    QTRY_COMPARE(tileState(), QString("finished"));
    QVERIFY(std::abs(tileValue() - duration) < 1e-6);
    QVERIFY(hotlapTile->property("hotlapFinished").toBool());
    // Lap times show hundredths by default; the inspector offers tenths to thousandths.
    QCOMPARE(model->widget(tile).value("settings").toMap().value("timingDecimals").toInt(), 2);
    QCOMPARE(hotlapTile->property("decimals").toInt(), 2);
    QVERIFY(QMetaObject::invokeMethod(hotlapTile, "formatTime", Q_RETURN_ARG(QVariant, formatted), Q_ARG(QVariant, 100.234)));
    QCOMPARE(formatted.toString(), QString("1:40.23"));
    model->setSetting(tile, "timingDecimals", 3);
    QTRY_COMPARE(hotlapTile->property("decimals").toInt(), 3);
    QVERIFY(QMetaObject::invokeMethod(hotlapTile, "formatTime", Q_RETURN_ARG(QVariant, formatted), Q_ARG(QVariant, 100.234)));
    QCOMPARE(formatted.toString(), QString("1:40.234"));
    // KAN-149: the tile (also what an export renders) rounds before minutes.
    QVERIFY(QMetaObject::invokeMethod(hotlapTile, "formatTime", Q_RETURN_ARG(QVariant, formatted), Q_ARG(QVariant, 119.9996)));
    QCOMPARE(formatted.toString(), QString("2:00.000"));
    model->setSetting(tile, "timingDecimals", 2);

    // The inspector for the tile.
    window->setProperty("selectedWidgetIndex", tile);
    QQuickItem *check = nullptr;
    QTRY_VERIFY((check = findVisual(findVisual, window->contentItem(),
        [](QQuickItem *item) { return item->objectName() == "hotlapModeCheck" && item->isVisible(); })));
    QVERIFY(check->property("checked").toBool());
    auto *picker = findVisual(findVisual, window->contentItem(),
        [](QQuickItem *item) { return item->objectName() == "hotlapLapPicker"; });
    QVERIFY(picker && picker->isVisible());
    QCOMPARE(picker->property("currentIndex").toInt(), 2); // "best", then lap 1, then lap 2
    auto *decimalsPicker = findVisual(findVisual, window->contentItem(),
        [](QQuickItem *item) { return item->objectName() == "lapTimeDecimals"; });
    QVERIFY(decimalsPicker && decimalsPicker->isVisible());
    QCOMPARE(decimalsPicker->property("currentIndex").toInt(), 1); // hundredths
    QVERIFY(QMetaObject::invokeMethod(decimalsPicker, "activated", Q_ARG(int, 2)));
    QTRY_COMPARE(model->widget(tile).value("settings").toMap().value("timingDecimals").toInt(), 3);
    model->setSetting(tile, "timingDecimals", 2);
    if (const QString shots = qEnvironmentVariable("FLAPPEDEAR_LAYOUT_REVIEW_DIR"); !shots.isEmpty()) {
        QTest::qWait(500); // the inspector lays out the newly selected widget first
        for (auto *item = check->parentItem(); item; item = item->parentItem())
            if (item->inherits("QQuickFlickable")) {
                auto *content = item->property("contentItem").value<QQuickItem *>();
                item->setProperty("contentY", std::max(0.0, check->mapToItem(content, QPointF()).y() - 120.0));
                break;
            }
        QTest::qWait(300);
        static_cast<void>(window->grabWindow().save(QDir(shots).filePath("hotlap-inspector.png")));
    }
    controller.setPlaybackTime(laps[0].toMap().value("startTelemetryTime").toDouble() + 0.5);
    auto *usePlayhead = findVisual(findVisual, window->contentItem(),
        [](QQuickItem *item) { return item->objectName() == "hotlapUsePlayhead"; });
    QTRY_VERIFY(usePlayhead && usePlayhead->isEnabled());
    QVERIFY(QMetaObject::invokeMethod(usePlayhead, "clicked"));
    QTRY_COMPARE(model->widget(tile).value("settings").toMap().value("hotlapLap").toInt(), laps[0].toMap().value("number").toInt());
    QTRY_COMPARE(picker->property("currentIndex").toInt(), 1);

    // Export opens on Single lap with the hotlap tile's lap.
    auto *dialog = window->findChild<QObject *>("exportDialog"); QVERIFY(dialog);
    QVERIFY(QMetaObject::invokeMethod(dialog, "open"));
    QTRY_VERIFY(dialog->property("opened").toBool());
    // KAN-185: opening the dialog asks for the day's best lap.
    QTRY_VERIFY(controller.dayBestLap().value("state").toString() != QString("idle"));
    QCOMPARE(window->findChild<QObject *>("exportRangeMode")->property("currentIndex").toInt(), 2);
    QCOMPARE(window->findChild<QObject *>("exportLapPicker")->property("currentIndex").toInt(), 0);
    QVERIFY(dialog->property("singleLapRange").toMap().value("valid").toBool());
    const QString review = qEnvironmentVariable("FLAPPEDEAR_LAYOUT_REVIEW_DIR");
    if (!review.isEmpty()) { QTest::qWait(300); static_cast<void>(window->grabWindow().save(QDir(review).filePath("hotlap-export.png"))); }
    for (const auto &arguments : warnings)
        for (const auto &error : arguments.first().value<QList<QQmlError>>())
            QVERIFY2(error.toString().contains("Cannot open: qrc:"), qPrintable(error.toString()));
}

void TelemetryTests::showsTyreTemperatureAndPressurePerCorner()
{
    // KAN-132: the tyres widget shows each corner's temperature (°C) and
    // pressure (recorded in kPa, shown in bar or psi). A corner without a
    // channel, and a sensor's 0 placeholders before its first reading, show
    // a dash -- never a substituted value.
    const QString ffmpeg = FfmpegTools::ffmpegPath();
    if (ffmpeg.isEmpty()) QSKIP("FFmpeg is unavailable for the tyres widget test.");
    QSettings settings; settings.clear(); settings.sync();
    QTemporaryDir directory; QVERIFY(directory.isValid());
    const QString videoPath = directory.filePath(QStringLiteral("tyres.mp4"));
    QProcess encoder;
    encoder.start(ffmpeg, {"-hide_banner", "-loglevel", "error", "-y", "-f", "lavfi", "-i",
                           "color=c=black:s=32x32:r=30:d=12", "-c:v", "mpeg4", "-q:v", "3", videoPath});
    QVERIFY(encoder.waitForFinished(30'000) && encoder.exitCode() == 0);
    QByteArray vbo = "[header]\ncoordinate units = degrees\n[column names]\ntime latitude longitude "
                     "tyre_temp_rr-canbus tyre_pressure_rr-canbus tyre_temp_fl-canbus tyre_pressure_fl-canbus\n[data]\n";
    for (int second = 0; second <= 10; ++second) {
        const bool reported = second >= 3; // FL's sensor reports from 3 s
        vbo += QByteArray::number(second) + " 52.0001 21.0002 +045.000 +230.000 "
            + (reported ? QByteArray("+040.000 +214.000") : QByteArray("+000.000 +000.000")) + "\n";
    }
    const QString vboPath = directory.filePath(QStringLiteral("tyres.vbo"));
    QVERIFY(writeBytes(vboPath, vbo));
    AppController controller(nullptr, directory.filePath("recovery.json"));
    controller.loadVideo(QUrl::fromLocalFile(videoPath));
    QTRY_COMPARE(controller.videoLoadState(), QStringLiteral("ready"));
    controller.loadVbo(QUrl::fromLocalFile(vboPath));
    QTRY_COMPARE(controller.vboLoadState(), QStringLiteral("ready"));

    controller.setPlaybackTime(1.0);
    QVariantMap values = controller.renderContext()->tyreValues();
    QVERIFY(values.value("available").toBool());
    QVariantList corners = values.value("corners").toList();
    QCOMPARE(corners.size(), 4);
    QCOMPARE(corners[0].toMap().value("corner").toString(), QString("FL"));
    QVERIFY(!corners[0].toMap().value("hasTemperature").toBool()); // placeholder
    QVERIFY(!corners[1].toMap().value("hasPressure").toBool());    // FR not recorded
    QCOMPARE(corners[3].toMap().value("temperature").toDouble(), 45.0);
    QCOMPARE(corners[3].toMap().value("pressure").toDouble(), 2.30);
    QCOMPARE(corners[3].toMap().value("pressureSourceUnit").toString(), QString("kPa"));

    auto *model = controller.widgetModel();
    const int tyres = model->addWidget("tyres");
    QVERIFY(tyres >= 0);
    QQmlEngine engine; engine.rootContext()->setContextProperty("appController", &controller);
    QSignalSpy warnings(&engine, &QQmlEngine::warnings);
    QQmlComponent component(&engine, QUrl::fromLocalFile(
        qmlSourcePath("Main.qml")));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    std::unique_ptr<QObject> object(component.create());
    auto *window = qobject_cast<QQuickWindow *>(object.get()); QVERIFY(window);
    window->show(); QVERIFY(QTest::qWaitForWindowExposed(window));
    const auto findVisual = [](auto &&self, QQuickItem *item, const std::function<bool(QQuickItem *)> &match) -> QQuickItem * {
        if (match(item)) return item;
        for (auto *child : item->childItems()) if (auto *found = self(self, child, match)) return found;
        return nullptr;
    };
    const auto cornerText = [&](const QString &corner, const QString &kind) {
        auto *cell = findVisual(findVisual, window->contentItem(),
            [&](QQuickItem *item) { return item->objectName() == "tyreCorner" + corner; });
        auto *label = cell ? findVisual(findVisual, cell, [&](QQuickItem *item) { return item->objectName() == kind; }) : nullptr;
        return label ? label->property("text").toString() : QString();
    };
    // The preview player primes itself at its first frame and then owns the
    // playhead: FL's sensor has not reported yet, FR is not recorded.
    if (!QTest::qWaitFor([&] { return controller.playbackTime() > 0.0; }, 15000))
        QSKIP("Media playback is unavailable here.");
    QTRY_VERIFY_WITH_TIMEOUT(!window->property("previewPrimeFramePending").toBool(), 15000);
    QVERIFY(controller.playbackTime() < 3.0);
    QTRY_COMPARE(cornerText("RR", "tyreTemperature"), QString("45°C"));
    QCOMPARE(cornerText("RR", "tyrePressure"), QString("2.30 bar"));
    QCOMPARE(cornerText("FL", "tyreTemperature"), QString("—"));
    QCOMPARE(cornerText("FL", "tyrePressure"), QString("—"));
    QCOMPARE(cornerText("FR", "tyrePressure"), QString("—"));

    // The inspector switches the pressure to psi.
    window->setProperty("selectedWidgetIndex", tyres);
    QQuickItem *unit = nullptr;
    QTRY_VERIFY((unit = findVisual(findVisual, window->contentItem(),
        [](QQuickItem *item) { return item->objectName() == "tyrePressureUnit" && item->isVisible(); })));
    QVERIFY(QMetaObject::invokeMethod(unit, "activated", Q_ARG(int, 1)));
    QTRY_COMPARE(model->widget(tyres).value("settings").toMap().value("pressureUnit").toString(), QString("psi"));
    QTRY_COMPARE(cornerText("RR", "tyrePressure"), QString("33.4 psi"));
    for (const auto &arguments : warnings)
        for (const auto &error : arguments.first().value<QList<QQmlError>>())
            QVERIFY2(error.toString().contains("Cannot open: qrc:"), qPrintable(error.toString()));
}

void TelemetryTests::capturesUserGuideScreens()
{
    // Opt-in (KAN-138): the user guide's screenshots from the application's
    // own controller and QML with a real onboard video and its recording.
    // FLAPPEDEAR_GUIDE_CAPTURE_DIR receives the PNGs. FLAPPEDEAR_GUIDE_VIDEO
    // and FLAPPEDEAR_GUIDE_VBO are a matching GoPro clip and VBO;
    // FLAPPEDEAR_GUIDE_DAY (optional) is the folder of that day's VBOs and
    // FLAPPEDEAR_GUIDE_CHAPTERS (optional) a comma-separated chaptered clip.
    // Private media is read in place and never copied.
    UserGuideCaptureOptions options;
    options.outputDirectory = qEnvironmentVariable("FLAPPEDEAR_GUIDE_CAPTURE_DIR");
    options.video = qEnvironmentVariable("FLAPPEDEAR_GUIDE_VIDEO");
    options.recording = qEnvironmentVariable("FLAPPEDEAR_GUIDE_VBO");
    if (options.outputDirectory.isEmpty() || options.video.isEmpty() || options.recording.isEmpty())
        QSKIP("FLAPPEDEAR_GUIDE_CAPTURE_DIR, FLAPPEDEAR_GUIDE_VIDEO and FLAPPEDEAR_GUIDE_VBO are not set");
    if (const QString day = qEnvironmentVariable("FLAPPEDEAR_GUIDE_DAY"); !day.isEmpty())
        for (const auto &file : QDir(day).entryInfoList({"*.vbo", "*.VBO"}, QDir::Files, QDir::Name))
            options.day << file.absoluteFilePath();
    const QString chapters = qEnvironmentVariable("FLAPPEDEAR_GUIDE_CHAPTERS");
    if (!chapters.isEmpty()) options.chapters = chapters.split(',', Qt::SkipEmptyParts);
    QTemporaryDir scratch; QVERIFY(scratch.isValid());
    options.scratchDirectory = scratch.path();
    QVERIFY(FlappedEar::registerBundledFonts());
    captureUserGuide(options);
}

void TelemetryTests::appliesTelemetryDesignLanguage()
{
    // KAN-187: the editor's shared controls follow FlappedEar Telemetry's
    // theme (qml/Theme.js) and its bundled fonts, also when loaded by path.
    QVERIFY(FlappedEar::registerBundledFonts());
    const QStringList families = QFontDatabase::families();
    QVERIFY(families.contains(QStringLiteral("Sora")));
    QVERIFY(families.contains(QStringLiteral("JetBrains Mono")));

    QQmlEngine engine;
    QSignalSpy warnings(&engine, &QQmlEngine::warnings);
    QQmlComponent component(&engine);
    component.setData(
        "import QtQuick\nimport \"Theme.js\" as Theme\n"
        "Item { property color amber: Theme.primary; property string sans: Theme.sans\n"
        "  FeButton { objectName: 'accent'; accent: true; text: 'Export' }\n"
        "  FeButton { objectName: 'plain'; text: 'Open' }\n"
        "  FeTextField { objectName: 'field'; text: '1:23.456' } }",
        QUrl::fromLocalFile(qmlSourcePath(QStringLiteral("ThemeProbe.qml"))));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    std::unique_ptr<QObject> root(component.create());
    QVERIFY2(root, qPrintable(component.errorString()));
    QCOMPARE(root->property("amber").value<QColor>(), QColor(QStringLiteral("#fcb203")));
    QCOMPARE(root->property("sans").toString(), QStringLiteral("Sora"));

    const auto background = [&](const char *name) {
        auto *control = root->findChild<QObject *>(QString::fromLatin1(name));
        return control ? control->property("background").value<QObject *>() : nullptr;
    };
    QObject *accent = background("accent");
    QObject *plain = background("plain");
    QVERIFY(accent); QVERIFY(plain);
    QCOMPARE(accent->property("color").value<QColor>(), QColor(QStringLiteral("#fcb203")));
    QCOMPARE(plain->property("color").value<QColor>(), QColor(QStringLiteral("#24262a")));
    QCOMPARE(accent->property("radius").toReal(), 3.0);
    auto *field = root->findChild<QObject *>(QStringLiteral("field"));
    QVERIFY(field);
    QCOMPARE(field->property("font").value<QFont>().family(), QStringLiteral("Sora"));
    for (const auto &arguments : warnings)
        for (const auto &error : arguments.first().value<QList<QQmlError>>())
            QFAIL(qPrintable(error.toString()));
}

void TelemetryTests::derivesNavigableLapFragmentsAndHotlapExportRange()
{
    const QString ffmpeg = FfmpegTools::ffmpegPath();
    if (ffmpeg.isEmpty()) QSKIP("FFmpeg is unavailable for the lap-navigation controller test.");
    QSettings settings;
    settings.clear();
    settings.sync();
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString videoPath = directory.filePath(QStringLiteral("laps.mp4"));
    QProcess encoder;
    encoder.start(ffmpeg, {"-hide_banner", "-loglevel", "error", "-y", "-f", "lavfi", "-i",
                           "color=c=black:s=32x32:r=30:d=25", "-c:v", "mpeg4", "-q:v", "3", videoPath});
    QVERIFY2(encoder.waitForStarted(), qPrintable(encoder.errorString()));
    QVERIFY2(encoder.waitForFinished(30'000), qPrintable(encoder.errorString()));
    QVERIFY2(encoder.exitCode() == 0, encoder.readAllStandardError().constData());

    const QString vboPath = directory.filePath(QStringLiteral("laps.vbo"));
    const QByteArray vbo =
        "[header]\ncoordinate units = degrees\n[laptiming]\n"
        "Start 21.0000 52.0000 21.0000 52.0002 start\n"
        "[column names]\n"
        "time latitude longitude\n"
        "[data]\n"
        "0 52.0001 21.0002\n1 52.0001 21.0002\n2 52.0001 20.9998\n"
        "3 52.0008 20.9998\n4 52.0008 21.0002\n5 52.0001 21.0002\n"
        "6 52.0001 20.9998\n7 52.0008 20.9998\n8 52.0008 21.0002\n"
        "10 52.0001 21.0002\n11 52.0001 20.9998\n12 52.0008 20.9998\n"
        "13 52.0008 21.0002\n14 52.0001 21.0002\n15 52.0001 20.9998\n";
    QVERIFY(writeBytes(vboPath, vbo));

    AppController controller;
    controller.loadVideo(QUrl::fromLocalFile(videoPath));
    QTRY_COMPARE(controller.videoLoadState(), QStringLiteral("ready"));
    controller.loadVbo(QUrl::fromLocalFile(vboPath));
    QTRY_COMPARE(controller.vboLoadState(), QStringLiteral("ready"));

    const QVariantList segments = controller.lapNavigationSegments();
    QCOMPARE(segments.size(), 5);
    QCOMPARE(segments[0].toMap().value(QStringLiteral("kind")).toString(), QStringLiteral("outlap"));
    QCOMPARE(segments[1].toMap().value(QStringLiteral("kind")).toString(), QStringLiteral("lap"));
    QCOMPARE(segments[4].toMap().value(QStringLiteral("kind")).toString(), QStringLiteral("inlap"));
    QCOMPARE(segments[0].toMap().value(QStringLiteral("startMilliseconds")).toLongLong(), qint64(0));
    QVERIFY(segments[4].toMap().value(QStringLiteral("endMilliseconds")).toLongLong()
            == controller.previewEndPositionMilliseconds());

    const QVariantMap hotlap = controller.lapExportRange(2, 30, 1, 5);
    QVERIFY(hotlap.value(QStringLiteral("valid")).toBool());
    const auto range = ExportEngine::frameRangeForSourceTimecode(
        MediaProbe::probe(videoPath), {30, 1}, hotlap.value(QStringLiteral("inTimecode")).toString(),
        hotlap.value(QStringLiteral("outTimecode")).toString());
    QVERIFY(range.has_value());
    QCOMPARE(range->firstFrame, hotlap.value(QStringLiteral("firstFrame")).toLongLong());
    QCOMPARE(range->lastFrame, hotlap.value(QStringLiteral("lastFrame")).toLongLong());
    QCOMPARE(controller.exportRangeDurationSeconds(30, 1,
                                                   hotlap.value(QStringLiteral("inTimecode")).toString(),
                                                   hotlap.value(QStringLiteral("outTimecode")).toString()),
             hotlap.value(QStringLiteral("durationSeconds")).toDouble());
}

void TelemetryTests::protectsEveryDaySourceFromExport()
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
        if (exportTo(target)) { controller.cancelExport(); return QString("export started onto ") + target; }
        if (controller.exportState() != "failed") return QString("state ") + controller.exportState() + " for " + target;
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
        QVERIFY(controller.exportError().contains("hard link"));
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
    QCOMPARE(controller.exportState(), QString("overwriteConfirmationRequired"));
    QCOMPARE(readBytes(unrelated), QByteArray("earlier export"));
}

void TelemetryTests::keepsOutputSafeWhenTheDestinationFills_data()
{
    QTest::addColumn<QString>("stage");
    QTest::newRow("full before export (preflight)") << QStringLiteral("preflight");
    QTest::newRow("fills during Stage A (overlay)") << QStringLiteral("renderingOverlay");
    QTest::newRow("fills during Stage B (final encode)") << QStringLiteral("encodingVideo");
    QTest::newRow("cancelled during Stage B") << QStringLiteral("cancel");
    QTest::newRow("full at final publication") << QStringLiteral("publication");
}

void TelemetryTests::keepsOutputSafeWhenTheDestinationFills()
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

void TelemetryTests::describesOutOfSpaceExportFailures()
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

QStringList TelemetryTests::unreachableControls(QQuickWindow *window)
{
    QStringList problems;
    const QRectF bounds(0, 0, window->width(), window->height());
    std::function<void(QQuickItem *)> visit = [&](QQuickItem *item) {
        if (!item->isVisible() || item->opacity() <= 0.01 || item->width() <= 0 || item->height() <= 0) return;
        if (item->inherits("QQuickPopupItem")) {
            const QRectF popup = item->mapRectToScene(QRectF(0, 0, item->width(), item->height()));
            if (!bounds.adjusted(-1, -1, 1, 1).contains(popup))
                problems << QStringLiteral("dialog larger than the window: %1").arg(describeItem(item));
        }
        if (isInteractiveControl(item) && item->isEnabled() && !reachableControl(item, bounds)) problems << describeItem(item);
        if (isInteractiveControl(item) && !item->inherits("QQuickComboBox") && !item->inherits("QQuickSpinBox")) return;
        for (auto *child : item->childItems()) visit(child);
    };
    visit(window->contentItem());
    return problems;
}

void TelemetryTests::keepsRecoveryWhenQuittingAtTheRecoveryPrompt()
{
    // KAN-145: while "Recover unsaved changes?" is open, quitting must not
    // discard the snapshot (the driver chose neither Recover nor Discard),
    // and New, Open and Save are refused until they choose.
    QTemporaryDir directory; QVERIFY(directory.isValid());
    QSettings settings; settings.clear(); settings.sync();
    const QString recovery = directory.filePath("recovery.json");
    {
        AppController controller(nullptr, recovery);
        controller.loadVbo(QUrl::fromLocalFile(QStringLiteral(TEST_FIXTURE_PATH)));
        QTRY_COMPARE(controller.vboLoadState(), QStringLiteral("ready"));
        controller.setSyncOffset(1.25);
        QVERIFY(controller.dirty());
        controller.m_document.writeRecoverySnapshot();
        QVERIFY(QFileInfo::exists(recovery));
        // The controller ends without saving, as after a crash.
    }
    {
        AppController controller(nullptr, recovery);
        QVERIFY(controller.recoveryPending());
        QSignalSpy quit(&controller, &AppController::quitApproved);
        controller.requestNewProject();
        QVERIFY(controller.recoveryPending());
        QVERIFY(controller.pendingDestructiveAction().isEmpty());
        controller.requestOpenProject(QUrl::fromLocalFile(directory.filePath("other.fetproject")));
        QVERIFY(controller.recoveryPending());
        QVERIFY(!controller.saveProject(QUrl::fromLocalFile(directory.filePath("saved.fetproject"))));
        QVERIFY(!QFileInfo::exists(directory.filePath("saved.fetproject")));
        controller.requestQuit();
        QCOMPARE(quit.size(), 1);
        QVERIFY2(QFileInfo::exists(recovery), "quitting at the recovery prompt deleted the snapshot");
    }
    {
        AppController controller(nullptr, recovery);
        QVERIFY(controller.recoveryPending()); // offered again at the next start
        controller.resolveStartupRecovery("recover");
        QTRY_COMPARE(controller.vboLoadState(), QStringLiteral("ready"));
        QCOMPARE(controller.syncOffset(), 1.25);
    }
}

void TelemetryTests::offersRecoveryOfAnEventCreatedByImport()
{
    // KAN-145: a new event created by import is an untitled document. The
    // previously remembered project is not its authority, so after a crash
    // its recovery is offered rather than rejected as another document's.
    QTemporaryDir directory; QVERIFY(directory.isValid());
    QSettings settings; settings.clear(); settings.sync();
    const QString recovery = directory.filePath("recovery.json");
    const QString projectA = directory.filePath("a.fetproject");
    const auto first = directory.filePath("first.vbo"), second = directory.filePath("second.vbo");
    QVERIFY(writeBytes(first, EventProjectFixture::routeVbo()));
    QVERIFY(writeBytes(second, EventProjectFixture::routeVbo(130, -2, 2)));
    qsizetype widgets = 0;
    {
        AppController controller(nullptr, recovery);
        controller.loadVbo(QUrl::fromLocalFile(QStringLiteral(TEST_FIXTURE_PATH)));
        QTRY_COMPARE(controller.vboLoadState(), QStringLiteral("ready"));
        QVERIFY(controller.saveProject(QUrl::fromLocalFile(projectA)));
        QCOMPARE(QSettings().value("project/path").toString(), projectA);
        QSignalSpy committed(&controller.m_document, &DocumentController::batchImportCommitted);
        QVERIFY(controller.m_document.importAnalysisRuns("Imported day", {QUrl::fromLocalFile(first), QUrl::fromLocalFile(second)}));
        QTRY_COMPARE_WITH_TIMEOUT(committed.size(), 1, 30000);
        QTRY_COMPARE_WITH_TIMEOUT(controller.vboLoadState(), QString("ready"), 30000);
        // The untitled event no longer points at project A.
        QVERIFY(QSettings().value("project/path").toString().isEmpty());
        widgets = controller.widgetModel()->addWidget("lapCurrent") >= 0
            ? controller.currentProjectObject().value("scene").toObject().value("widgets").toArray().size() : -1;
        QVERIFY(widgets > 0);
        QVERIFY(controller.dirty());
        controller.m_document.writeRecoverySnapshot();
        // The controller ends without saving, as after a crash.
    }
    // Even with project A still remembered (written by an older version).
    QSettings().setValue("project/path", projectA);
    {
        AppController controller(nullptr, recovery);
        QVERIFY2(controller.recoveryPending(), "the imported event's recovery was not offered");
        controller.resolveStartupRecovery("recover");
        QTRY_VERIFY_WITH_TIMEOUT(controller.eventRuns().size() == 2 && controller.vboLoadState() == "ready", 30000);
        QCOMPARE(controller.eventName(), QStringLiteral("Imported day"));
        QCOMPARE(controller.currentProjectObject().value("scene").toObject().value("widgets").toArray().size(), widgets);
    }
}

void TelemetryTests::reviewsGoProChapterGroups()
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

namespace {
// Two or more short H.264 chapters for the KAN-105 tests.
bool encodeChapter(const QString &ffmpeg, const QString &path, const int seconds)
{
    QProcess encoder;
    encoder.start(ffmpeg, {"-hide_banner", "-loglevel", "error", "-y", "-f", "lavfi", "-i",
        QString("testsrc2=s=320x180:r=30:d=%1").arg(seconds), "-c:v", "libx264", "-pix_fmt", "yuv420p", path});
    return encoder.waitForFinished(30'000) && encoder.exitCode() == 0;
}
} // namespace

void TelemetryTests::keepsVideoChaptersAsOneTimeline()
{
    // KAN-105: chapters form one continuous timeline, are saved with their
    // durations, reopen as chapters, and a missing chapter keeps its time as
    // a gap. Export of a chaptered video is refused, never truncated.
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
        // Export is refused rather than cut to the first chapter.
        controller.loadVbo(QUrl::fromLocalFile(QFINDTESTDATA("fixtures/basic.vbo")));
        QTRY_COMPARE_WITH_TIMEOUT(controller.vboLoadState(), QString("ready"), 30000);
        QVERIFY(!controller.startExport(QUrl::fromLocalFile(directory.filePath("out.mp4")), 320, 180, 30, 1, 1'000'000, false, false, {}, {}, false));
        QVERIFY(controller.exportError().contains("several chapters"));
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

void TelemetryTests::playsVideoChaptersAcrossBoundaries()
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

void TelemetryTests::keepsAPausedSeekWhenLoadedMediaRepeats()
{
    // KAN-172: Qt's Windows backend reports LoadedMedia again after a paused seek.
    // Once the preview is primed for a source, a repeat must not prime it again,
    // which jumped the preview back to the start.
    const QString ffmpeg = FfmpegTools::ffmpegPath();
    if (ffmpeg.isEmpty()) QSKIP("FFmpeg is unavailable for the paused-seek test.");
    QTemporaryDir directory; QVERIFY(directory.isValid());
    QSettings settings; settings.clear(); settings.sync();
    QVERIFY(encodeChapter(ffmpeg, directory.filePath("paused-seek.mp4"), 3));
    AppController controller(nullptr, directory.filePath("recovery.json"));
    controller.loadVideo(QUrl::fromLocalFile(directory.filePath("paused-seek.mp4")));
    QTRY_COMPARE_WITH_TIMEOUT(controller.videoLoadState(), QString("ready"), 30000);
    QQmlEngine engine; engine.rootContext()->setContextProperty("appController", &controller);
    QQmlComponent component(&engine, QUrl::fromLocalFile(
        qmlSourcePath("Main.qml")));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    std::unique_ptr<QObject> object(component.create());
    QVERIFY2(object, qPrintable(component.errorString()));
    auto *window = qobject_cast<QQuickWindow *>(object.get()); QVERIFY(window);
    window->show(); QVERIFY(QTest::qWaitForWindowExposed(window));
    if (!QTest::qWaitFor([&] { return controller.playbackTime() > 0.0; }, 15000))
        QSKIP("Media playback is unavailable here.");
    QTRY_VERIFY_WITH_TIMEOUT(!window->property("previewPrimeFramePending").toBool(), 15000);
    QCOMPARE(window->property("previewPrimedSource").toString(), controller.videoChapterSource().toString());

    // A repeated LoadedMedia for the primed source is ignored: no new prime, no reset.
    QVariant primed;
    QVERIFY(QMetaObject::invokeMethod(window, "previewMediaLoaded", Q_RETURN_ARG(QVariant, primed)));
    QCOMPARE(primed.toBool(), false);
    QVERIFY(!window->property("previewPrimeFramePending").toBool());

    // A source that has not been primed yet is primed as before.
    window->setProperty("previewPrimedSource", QString());
    QVERIFY(QMetaObject::invokeMethod(window, "previewMediaLoaded", Q_RETURN_ARG(QVariant, primed)));
    QCOMPARE(primed.toBool(), true);
    QTRY_VERIFY_WITH_TIMEOUT(!window->property("previewPrimeFramePending").toBool(), 15000);
}

void TelemetryTests::routesNewDocumentSaveAsThroughPendingQuit()
{
    QSettings settings;
    settings.clear();
    settings.sync();
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    AppController controller(nullptr, directory.filePath(QStringLiteral("recovery.json")));
    controller.setSyncOffset(1.0); // A dirty new document has no project path.

    QSignalSpy saveAsSpy(&controller, &AppController::saveAsRequested);
    QSignalSpy quitSpy(&controller, &AppController::quitApproved);
    QVERIFY(!controller.saveCurrentProject());
    QCOMPARE(saveAsSpy.count(), 1);

    controller.requestQuit();
    QCOMPARE(controller.pendingDestructiveAction(), QStringLiteral("quit"));
    controller.resolveDestructiveAction(QStringLiteral("save"));
    QCOMPARE(saveAsSpy.count(), 2);
    QCOMPARE(controller.pendingDestructiveAction(), QStringLiteral("quit"));

    const QString path = directory.filePath(QStringLiteral("saved-from-quit.fetproject"));
    QVERIFY(controller.saveProject(QUrl::fromLocalFile(path)));
    QCOMPARE(quitSpy.count(), 1);
    QVERIFY(QFileInfo::exists(path));
    QVERIFY(!controller.dirty());
}

void TelemetryTests::mapsLapStartTelemetryTimesBackToVideoBounds()
{
    const SyncTransform transform{90.217, 1.001};
    const auto videoStart = telemetryToVideoTime(120.247, transform);
    QVERIFY(videoStart.has_value());
    QVERIFY(qAbs(*videoStart - 30.0) < 0.000001);
    QVERIFY(!telemetryToVideoTime(std::numeric_limits<double>::quiet_NaN(), transform));
    QVERIFY(!telemetryToVideoTime(120.0, {90.0, 0.0}));
    QVERIFY(!telemetryToVideoTime(120.0, {std::numeric_limits<double>::infinity(), 1.0}));
}

void TelemetryTests::decodesOptionalRealVideoFrameWithNativeSink()
{
    const QString path = qEnvironmentVariable("FLAPPEDEAR_REAL_GOPRO");
    if (path.isEmpty()) QSKIP("FLAPPEDEAR_REAL_GOPRO is not set");
    QVERIFY2(QFileInfo::exists(path), qPrintable(path));

    QMediaPlayer player;
    QVideoSink sink;
    QSignalSpy frameSpy(&sink, &QVideoSink::videoFrameChanged);
    player.setVideoSink(&sink);
    player.setSource(QUrl::fromLocalFile(path));

    QTRY_VERIFY_WITH_TIMEOUT(player.mediaStatus() == QMediaPlayer::LoadedMedia
                                 || player.mediaStatus() == QMediaPlayer::BufferedMedia,
                             15'000);
    player.setPosition(17);
    QTest::qWait(250);
    const qsizetype pausedSeekFrameCount = frameSpy.count();

    player.play();
    QTRY_VERIFY_WITH_TIMEOUT(frameSpy.count() > 0, 15'000);
    player.pause();
    QVERIFY(sink.videoFrame().isValid());
    qInfo().noquote() << QStringLiteral("real video sink: %1 frames after paused seek, first playback frame at %2 ms")
                             .arg(pausedSeekFrameCount)
                             .arg(player.position());
}

void TelemetryTests::benchmarksCachedOptionalRealVboPresentationLookups()
{
    const QString path = qEnvironmentVariable("FLAPPEDEAR_REAL_VBO");
    if (path.isEmpty()) QSKIP("FLAPPEDEAR_REAL_VBO is not set");
    const TelemetrySession session = VboParser::parseFile(path);
    const QString channelName = session.aliases.value(QStringLiteral("speed"), QStringLiteral("speed"));
    const auto channel = session.channels.constFind(channelName);
    QVERIFY(channel != session.channels.cend());
    QVERIFY(channel->timestamps.size() >= 2);
    TelemetryRenderContext context;
    context.setSession(&session);
    context.setTime((channel->timestamps.front() + channel->timestamps[1]) / 2.0);
    QVERIFY(context.telemetryValue(QStringLiteral("speed")).isValid());
    QCOMPARE(channel->cadenceStatisticComputationCount, qsizetype(1));
    constexpr int lookups = 10'000;
    QElapsedTimer elapsed;
    elapsed.start();
    for (int lookup = 0; lookup < lookups; ++lookup) {
        const double progress = static_cast<double>(lookup) / static_cast<double>(lookups - 1);
        context.setTime(channel->timestamps.front()
                        + (channel->timestamps.back() - channel->timestamps.front()) * progress);
        QVERIFY(context.telemetryValue(QStringLiteral("speed")).isValid());
    }
    qInfo().noquote() << QStringLiteral(
        "real VBO cached presentation benchmark: %1 lookups in %2 ms, cadence computations=%3")
                             .arg(lookups).arg(elapsed.elapsed())
                             .arg(channel->cadenceStatisticComputationCount);
    QCOMPARE(channel->cadenceStatisticComputationCount, qsizetype(1));
}

void TelemetryTests::persistsWidgetScenes()
{
    WidgetModel source;
    source.resetDefaults();
    const int custom = source.addWidget("retroCustomValue");
    source.setSetting(custom, "source", "oiltemp");
    source.setSetting(custom, "label", "Oil temperature");
    source.setWidgetProperty(custom, "rotation", 12.0);

    WidgetModel restored;
    QVERIFY(restored.fromJson(source.toJson()));
    QCOMPARE(restored.count(), source.count());
    const QVariantMap widget = restored.widget(custom);
    QCOMPARE(widget.value("type").toString(), QString("retroCustomValue"));
    QCOMPARE(widget.value("rotation").toDouble(), 12.0);
    QCOMPARE(widget.value("settings").toMap().value("source").toString(), QString("oiltemp"));
}

void TelemetryTests::normalizesWidgetSemanticsAcrossMutationAndImport()
{
    WidgetModel edited;
    const int editedIndex = edited.addWidget(QStringLiteral("retroTachometer"));
    QVERIFY(editedIndex >= 0);
    edited.setSetting(editedIndex, QStringLiteral("decimals"), 999999);
    edited.setSetting(editedIndex, QStringLiteral("backgroundColor"), QStringLiteral("not-a-color"));
    QCOMPARE(edited.widget(editedIndex).value(QStringLiteral("settings")).toMap()
                 .value(QStringLiteral("decimals")).toInt(), 6);
    QCOMPARE(edited.widget(editedIndex).value(QStringLiteral("settings")).toMap()
                 .value(QStringLiteral("backgroundColor")).toString(), QStringLiteral("#16232d"));

    WidgetModel imported;
    const QJsonArray invalid{
        QJsonObject{{QStringLiteral("id"), QStringLiteral("rpm-widget")},
                    {QStringLiteral("type"), QStringLiteral("retroTachometer")},
                    {QStringLiteral("settings"), QJsonObject{{QStringLiteral("decimals"), 999999},
                                                               {QStringLiteral("fontSize"), 999999},
                                                               {QStringLiteral("backgroundColor"), QStringLiteral("invalid")},
                                                               {QStringLiteral("minValue"), 100},
                                                               {QStringLiteral("maxValue"), 10}}},
                    {QStringLiteral("cues"), QJsonArray{QJsonObject{
                        {QStringLiteral("start"), -1.0},
                        {QStringLiteral("duration"), -2.0},
                        {QStringLiteral("effect"), QStringLiteral("invalid")},
                    }}}},
    };
    QVERIFY(imported.fromJson(invalid));
    const QVariantMap importedWidget = imported.widget(0);
    QCOMPARE(importedWidget.value(QStringLiteral("settings")).toMap()
                 .value(QStringLiteral("decimals")).toInt(), 6);
    QCOMPARE(importedWidget.value(QStringLiteral("settings")).toMap()
                 .value(QStringLiteral("fontSize")).toDouble(), 200.0);
    QCOMPARE(importedWidget.value(QStringLiteral("settings")).toMap()
                 .value(QStringLiteral("backgroundColor")).toString(), QStringLiteral("#16232d"));
    QCOMPARE(importedWidget.value(QStringLiteral("settings")).toMap()
                 .value(QStringLiteral("minValue")).toDouble(), 0.0);
    QCOMPARE(importedWidget.value(QStringLiteral("settings")).toMap()
                 .value(QStringLiteral("maxValue")).toDouble(), 9000.0);
    const QVariantMap cue = importedWidget.value(QStringLiteral("cues")).toList().front().toMap();
    QCOMPARE(cue.value(QStringLiteral("start")).toDouble(), 0.0);
    QCOMPARE(cue.value(QStringLiteral("duration")).toDouble(), 0.1);
    QCOMPARE(cue.value(QStringLiteral("effect")).toString(), QStringLiteral("fade"));

    QTemporaryDir templateDirectory;
    QVERIFY(templateDirectory.isValid());
    const bool hadOverride = qEnvironmentVariableIsSet("FLAPPEDEAR_TEMPLATE_STORE");
    const QByteArray previousOverride = qgetenv("FLAPPEDEAR_TEMPLATE_STORE");
    qputenv("FLAPPEDEAR_TEMPLATE_STORE", templateDirectory.filePath("templates.json").toUtf8());
    const auto restoreEnvironment = qScopeGuard([hadOverride, previousOverride] {
        if (hadOverride) qputenv("FLAPPEDEAR_TEMPLATE_STORE", previousOverride);
        else qunsetenv("FLAPPEDEAR_TEMPLATE_STORE");
    });
    const QString importedTemplatePath = templateDirectory.filePath("unsafe.fettemplate");
    const QJsonObject templateWidget{
        {QStringLiteral("id"), QStringLiteral("template-rpm")},
        {QStringLiteral("type"), QStringLiteral("retroTachometer")},
        {QStringLiteral("settings"), QJsonObject{{QStringLiteral("decimals"), 999999}}},
    };
    const QJsonObject unsafeTemplate{
        {QStringLiteral("name"), QStringLiteral("unsafe")},
        {QStringLiteral("widgets"), QJsonArray{templateWidget}},
    };
    QVERIFY(writeBytes(importedTemplatePath,
                       QJsonDocument(QJsonObject{{QStringLiteral("template"), unsafeTemplate}}).toJson()));
    WidgetModel templateModel;
    const QString templateId = templateModel.importTemplate(QUrl::fromLocalFile(importedTemplatePath));
    QVERIFY(!templateId.isEmpty());
    QVERIFY(templateModel.applyTemplate(templateId));
    QCOMPARE(templateModel.widget(0).value(QStringLiteral("settings")).toMap()
                 .value(QStringLiteral("decimals")).toInt(), 6);
}

void TelemetryTests::rejectsNonFiniteWidgetGeometryAndDuplicateIds()
{
    WidgetModel model;
    const int index = model.addWidget(QStringLiteral("speed"));
    QVERIFY(index >= 0);
    model.setWidgetProperty(index, QStringLiteral("scale"), std::numeric_limits<double>::quiet_NaN());
    QVERIFY(std::isfinite(model.widget(index).value(QStringLiteral("scale")).toDouble()));

    const QJsonArray duplicates{
        QJsonObject{{QStringLiteral("id"), QStringLiteral("duplicate")},
                    {QStringLiteral("type"), QStringLiteral("speed")}},
        QJsonObject{{QStringLiteral("id"), QStringLiteral("duplicate")},
                    {QStringLiteral("type"), QStringLiteral("heartRate")}},
    };
    QVERIFY(!model.fromJson(duplicates));
}

void TelemetryTests::loadsVisualTemplates()
{
    WidgetModel model;
    QCOMPARE(model.templates().size(), 1);
    QVERIFY(model.applyTemplate("motorsport-broadcast-smoke"));
    QCOMPARE(model.count(), 9);
    QCOMPARE(model.widget(0).value("type").toString(), QString("retroTachometer"));
    QCOMPARE(model.widget(6).value("type").toString(), QString("heartRate"));
    QCOMPARE(model.widget(7).value("type").toString(), QString("f1GForceRadar"));
    QCOMPARE(model.widget(8).value("type").toString(), QString("gForceMagnitudeBar"));
    QCOMPARE(model.widget(3).value("settings").toMap().value("stackPosition").toString(), QString("top"));
    QCOMPARE(model.widget(4).value("settings").toMap().value("stackPosition").toString(), QString("middle"));
    QCOMPARE(model.widget(5).value("settings").toMap().value("stackPosition").toString(), QString("bottom"));
    // KAN-192: ATF reads RaceChrono's gearbox temperature channel.
    QCOMPARE(model.widget(4).value("settings").toMap().value("source").toString(), QString("gearbox_temp-obd"));
    QVERIFY(!model.applyTemplate("missing-template"));
    // Retired built-ins are gone.
    for (const QString id : {"track-day", "minimal", "performance", "2000s-grand-prix"})
        QVERIFY2(!model.applyTemplate(id), qPrintable(id));
}

void TelemetryTests::dropsRetiredWidgetTypes()
{
    // KAN-192: a project or custom template saved with a removed type still
    // opens; only those widgets are left out.
    WidgetModel model;
    for (const QString retired : {"rpm", "gForce", "track", "customValue", "arcGauge", "dialGauge",
             "telemetryOverlay", "lapBest", "lapDelta", "speedBest", "speedCurrent", "speedDelta",
             "retroGrandPrix", "retroGear", "retroPedal", "retroSpeedArc", "retroNameplate", "brandLogo"})
        QCOMPARE(model.addWidget(retired), -1);
    const QJsonArray scene{
        QJsonObject{{QStringLiteral("id"), QStringLiteral("kept")}, {QStringLiteral("type"), QStringLiteral("speed")}},
        QJsonObject{{QStringLiteral("id"), QStringLiteral("old-rpm")}, {QStringLiteral("type"), QStringLiteral("rpm")}},
        QJsonObject{{QStringLiteral("id"), QStringLiteral("old-track")}, {QStringLiteral("type"), QStringLiteral("track")}},
        QJsonObject{{QStringLiteral("id"), QStringLiteral("lap")}, {QStringLiteral("type"), QStringLiteral("lapCurrent")}},
    };
    QVERIFY(model.fromJson(scene));
    QCOMPARE(model.count(), 2);
    QCOMPARE(model.retiredWidgetsDropped(), 2);
    QCOMPARE(model.widget(0).value("type").toString(), QString("speed"));
    QCOMPARE(model.widget(1).value("type").toString(), QString("lapCurrent"));
    QVERIFY(model.fromJson(QJsonArray{scene.at(0)}));
    QCOMPARE(model.retiredWidgetsDropped(), 0);
    // A type that never existed still makes the scene invalid.
    QVERIFY(!model.fromJson(QJsonArray{QJsonObject{{QStringLiteral("id"), QStringLiteral("x")},
                                                   {QStringLiteral("type"), QStringLiteral("unknownType")}}}));

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const bool hadOverride = qEnvironmentVariableIsSet("FLAPPEDEAR_TEMPLATE_STORE");
    const QByteArray previousOverride = qgetenv("FLAPPEDEAR_TEMPLATE_STORE");
    qputenv("FLAPPEDEAR_TEMPLATE_STORE", directory.filePath("templates.json").toUtf8());
    const auto restoreEnvironment = qScopeGuard([hadOverride, previousOverride] {
        if (hadOverride) qputenv("FLAPPEDEAR_TEMPLATE_STORE", previousOverride);
        else qunsetenv("FLAPPEDEAR_TEMPLATE_STORE");
    });
    const QString path = directory.filePath("old.fettemplate");
    const QJsonObject oldTemplate{
        {QStringLiteral("name"), QStringLiteral("Old layout")},
        {QStringLiteral("widgets"), QJsonArray{
             QJsonObject{{QStringLiteral("id"), QStringLiteral("dial")}, {QStringLiteral("type"), QStringLiteral("dialGauge")}},
             QJsonObject{{QStringLiteral("id"), QStringLiteral("hr")}, {QStringLiteral("type"), QStringLiteral("heartRate")}}}},
    };
    QVERIFY(writeBytes(path, QJsonDocument(QJsonObject{{QStringLiteral("template"), oldTemplate}}).toJson()));
    WidgetModel templates;
    const QString id = templates.importTemplate(QUrl::fromLocalFile(path));
    QVERIFY(!id.isEmpty());
    QVERIFY(templates.applyTemplate(id));
    QCOMPARE(templates.count(), 1);
    QCOMPARE(templates.widget(0).value("type").toString(), QString("heartRate"));
    WidgetModel reloaded;
    QVERIFY(reloaded.lastError().isEmpty());
    QVERIFY(reloaded.applyTemplate(id));
    QCOMPARE(reloaded.count(), 1);
}

void TelemetryTests::providesCustomizableArchetypes()
{
    WidgetModel model;
    const QHash<QString, QStringList> specialized = {
        {"speed", {"showGauge", "unit", "maxValue"}},
        {"heartRate", {"showIcon", "unit", "accentColor"}},
        {"pedals", {"acceleratorSource", "brakeSource", "acceleratorColor", "brakeColor"}},
        {"f1GForceRadar", {"lateralSource", "longitudinalSource", "invertLateral", "invertLongitudinal", "maxG", "ringStepG", "showCrosshair", "showCenterBox", "showRingLabels", "radarBackgroundColor", "dotColor", "gridColor"}},
        {"gForceMagnitudeBar", {"lateralSource", "longitudinalSource", "invertLateral", "invertLongitudinal", "maxG", "labelText", "showLabel", "showValue", "barColor", "barBackgroundColor", "barRadius"}},
        {"retroCustomValue", {"source", "label", "fallbackText", "panelColor", "valueColor", "labelColor", "icon", "stackPosition", "showSeparator"}},
        {"retroTachometer", {"source", "minValue", "maxValue", "needleColor"}},
        {"tyres", {"label", "showTemperature", "showPressure", "pressureUnit", "coldBelow", "hotAbove"}},
        {"lapCurrent", {"label", "timingDecimals"}},
    };
    for (auto iterator = specialized.cbegin(); iterator != specialized.cend(); ++iterator) {
        const int index = model.addWidget(iterator.key());
        QVERIFY(index >= 0);
        const QVariantMap settings = model.widget(index).value("settings").toMap();
        for (const QString common : {
                 "backgroundColor", "backgroundOpacity", "borderColor", "cornerRadius",
                 "textColor", "secondaryTextColor", "accentColor", "fontFamily",
                 "fontWeight", "fontSize", "valueFontScale", "labelFontScale", "padding"}) {
            QVERIFY2(settings.contains(common), qPrintable(iterator.key() + ": " + common));
        }
        for (const QString &key : iterator.value()) {
            QVERIFY2(settings.contains(key), qPrintable(iterator.key() + ": " + key));
        }
    }
}

void TelemetryTests::updatesCustomTemplatesInPlace()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const bool hadOverride = qEnvironmentVariableIsSet("FLAPPEDEAR_TEMPLATE_STORE");
    const QByteArray previousOverride = qgetenv("FLAPPEDEAR_TEMPLATE_STORE");
    const QString storePath = directory.filePath("layout-templates.json");
    qputenv("FLAPPEDEAR_TEMPLATE_STORE", storePath.toUtf8());
    const auto restoreEnvironment = qScopeGuard([hadOverride, previousOverride] {
        if (hadOverride) {
            qputenv("FLAPPEDEAR_TEMPLATE_STORE", previousOverride);
        } else {
            qunsetenv("FLAPPEDEAR_TEMPLATE_STORE");
        }
    });

    WidgetModel source;
    QVERIFY(source.addWidget("speed") == 0 && source.addWidget("heartRate") == 1);
    const QString templateId = source.saveCurrentAsTemplate("My layout", "Keep this description");
    QVERIFY(!templateId.isEmpty());
    const int templateCount = source.templates().size();
    source.setSetting(0, "fontSize", 48);
    source.setSetting(0, "futureCompatibleSetting", "preserve me");
    QCOMPARE(source.addWidget("retroCustomValue"), 2);
    QVERIFY(source.updateTemplate(templateId));
    QCOMPARE(source.templates().size(), templateCount);
    QVERIFY(!source.updateTemplate("motorsport-broadcast-smoke"));

    WidgetModel restored;
    const QVariantList restoredTemplates = restored.templates();
    QVariantMap saved;
    for (const QVariant &candidate : restoredTemplates) {
        if (candidate.toMap().value("id").toString() == templateId) {
            saved = candidate.toMap();
            break;
        }
    }
    QCOMPARE(saved.value("name").toString(), QString("My layout"));
    QCOMPARE(saved.value("description").toString(), QString("Keep this description"));
    QVERIFY(restored.applyTemplate(templateId));
    QCOMPARE(restored.count(), 3);
    QCOMPARE(restored.widget(0).value("settings").toMap().value("fontSize").toDouble(), 48.0);
    QCOMPARE(restored.widget(0).value("settings").toMap().value("futureCompatibleSetting").toString(), QString("preserve me"));
    QCOMPARE(restored.widget(2).value("type").toString(), QString("retroCustomValue"));

    QFile stored(storePath);
    QVERIFY(stored.open(QIODevice::ReadOnly));
    QJsonObject root = QJsonDocument::fromJson(stored.readAll()).object();
    QJsonArray templates = root.value("templates").toArray();
    QJsonObject savedTemplate = templates.first().toObject();
    savedTemplate.insert("futureTemplateField", "retain this");
    templates[0] = savedTemplate;
    root.insert("templates", templates);
    stored.close();
    const QByteArray rewrittenStore = QJsonDocument(root).toJson();
    QVERIFY(stored.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QCOMPARE(stored.write(rewrittenStore), qint64(rewrittenStore.size()));
    stored.close();

    WidgetModel compatibleReload;
    QVERIFY(compatibleReload.applyTemplate(templateId));
    compatibleReload.setSetting(0, "fontSize", 60);
    QVERIFY(compatibleReload.updateTemplate(templateId));
    QVERIFY(stored.open(QIODevice::ReadOnly));
    const QJsonObject updatedRoot = QJsonDocument::fromJson(stored.readAll()).object();
    QCOMPARE(updatedRoot.value("templates").toArray().first().toObject()
                 .value("futureTemplateField").toString(), QString("retain this"));
    stored.close();

    const QString blockedStore = directory.path();
    qputenv("FLAPPEDEAR_TEMPLATE_STORE", blockedStore.toUtf8());
    compatibleReload.setSetting(0, "fontSize", 72);
    QVERIFY(!compatibleReload.updateTemplate(templateId));
    qputenv("FLAPPEDEAR_TEMPLATE_STORE", storePath.toUtf8());
    QVERIFY(compatibleReload.applyTemplate(templateId));
    QCOMPARE(compatibleReload.widget(0).value("settings").toMap().value("fontSize").toDouble(), 60.0);
}

void TelemetryTests::rejectsTemplateStoreCountGrowth()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QByteArray previous = qgetenv("FLAPPEDEAR_TEMPLATE_STORE");
    const auto restore = qScopeGuard([previous] {
        if (previous.isNull()) qunsetenv("FLAPPEDEAR_TEMPLATE_STORE");
        else qputenv("FLAPPEDEAR_TEMPLATE_STORE", previous);
    });
    const QString path = directory.filePath("templates.json");
    qputenv("FLAPPEDEAR_TEMPLATE_STORE", path.toUtf8());
    WidgetModel model;
    QVERIFY(model.addWidget("speed") == 0 && model.addWidget("heartRate") == 1);
    const qsizetype builtInCount = model.templates().size();
    QJsonArray templates;
    for (qsizetype index = 0; index < ProjectLimits::maximumTemplateCount; ++index) {
        templates.append(QJsonObject{{"id", QString("user-%1").arg(index)}, {"name", "Saved"},
                                     {"widgets", model.toJson()}});
    }
    const QByteArray original = QJsonDocument(QJsonObject{
        {"schemaVersion", 1}, {"templates", templates}}).toJson();
    QVERIFY(writeBytes(path, original));
    model.reloadTemplates();
    QVERIFY(model.lastError().isEmpty());
    QCOMPARE(model.templates().size(), builtInCount + ProjectLimits::maximumTemplateCount);
    QVERIFY(model.saveCurrentAsTemplate("One too many", "").isEmpty());
    QVERIFY(!model.lastError().isEmpty());
    QCOMPARE(readBytes(path), original);
    const QString importPath = directory.filePath("import.fettemplate");
    QVERIFY(writeBytes(importPath, QJsonDocument(templates.first().toObject()).toJson()));
    QVERIFY(model.importTemplate(QUrl::fromLocalFile(importPath)).isEmpty());
    QCOMPARE(readBytes(path), original);
    WidgetModel restarted;
    QCOMPARE(restarted.templates().size(), builtInCount + ProjectLimits::maximumTemplateCount);
    QVERIFY(restarted.applyTemplate("user-0"));
    QVERIFY(restarted.updateTemplate("user-0"));
    QVERIFY(restarted.deleteTemplate("user-1"));
    const QString replacement = restarted.saveCurrentAsTemplate("Replacement", "");
    QVERIFY(!replacement.isEmpty());
    WidgetModel afterReplacement;
    QVERIFY(afterReplacement.applyTemplate(replacement));
    QCOMPARE(afterReplacement.templates().size(), builtInCount + ProjectLimits::maximumTemplateCount);
}

void TelemetryTests::rejectsTemplateStoreByteGrowth()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QByteArray previous = qgetenv("FLAPPEDEAR_TEMPLATE_STORE");
    const auto restore = qScopeGuard([previous] {
        if (previous.isNull()) qunsetenv("FLAPPEDEAR_TEMPLATE_STORE");
        else qputenv("FLAPPEDEAR_TEMPLATE_STORE", previous);
    });
    const QString path = directory.filePath("templates.json");
    qputenv("FLAPPEDEAR_TEMPLATE_STORE", path.toUtf8());
    WidgetModel source;
    QVERIFY(source.addWidget("speed") == 0 && source.addWidget("heartRate") == 1);
    QJsonArray futureData;
    for (int index = 0; index < 2044; ++index) futureData.append(QString(4096, QLatin1Char('x')));
    const QJsonObject root{{"schemaVersion", 1}, {"templates", QJsonArray{QJsonObject{
        {"id", "user-large"}, {"name", "Large compatible template"},
        {"widgets", source.toJson()}, {"futureData", futureData}}}}};
    const QByteArray original = QJsonDocument(root).toJson(QJsonDocument::Compact);
    QVERIFY(original.size() < ProjectLimits::templateStoreBytes);
    QVERIFY(QJsonDocument(root).toJson(QJsonDocument::Indented).size() > ProjectLimits::templateStoreBytes);
    QVERIFY(writeBytes(path, original));
    WidgetModel model;
    QVERIFY(model.lastError().isEmpty());
    QVERIFY(model.applyTemplate("user-large"));
    const QVariantList before = model.templates();
    QVERIFY(!model.updateTemplate("user-large"));
    QVERIFY(!model.lastError().isEmpty());
    QCOMPARE(readBytes(path), original);
    QCOMPARE(model.templates(), before);
    QVERIFY(model.saveCurrentAsTemplate("Extra", "").isEmpty());
    QCOMPARE(readBytes(path), original);
    WidgetModel restarted;
    QVERIFY(restarted.applyTemplate("user-large"));
    QVERIFY(restarted.deleteTemplate("user-large"));
    QVERIFY(!restarted.saveCurrentAsTemplate("Small", "").isEmpty());
}

void TelemetryTests::preservesRejectedTemplateStores()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QByteArray previous = qgetenv("FLAPPEDEAR_TEMPLATE_STORE");
    const auto restore = qScopeGuard([previous] {
        if (previous.isNull()) qunsetenv("FLAPPEDEAR_TEMPLATE_STORE");
        else qputenv("FLAPPEDEAR_TEMPLATE_STORE", previous);
    });
    const QString path = directory.filePath("templates.json");
    qputenv("FLAPPEDEAR_TEMPLATE_STORE", path.toUtf8());
    WidgetModel model;
    QVERIFY(model.addWidget("speed") == 0 && model.addWidget("heartRate") == 1);
    const QString id = model.saveCurrentAsTemplate("Preserved", "");
    QVERIFY(!id.isEmpty());
    const QByteArray good = readBytes(path);
    const QVariantList templates = model.templates();
    QJsonObject tooMany = QJsonDocument::fromJson(good).object();
    QJsonArray entries;
    for (qsizetype index = 0; index <= ProjectLimits::maximumTemplateCount; ++index)
        entries.append(tooMany.value("templates").toArray().first());
    tooMany.insert("templates", entries);
    const QList<QByteArray> rejected{
        QByteArray("{invalid"), QJsonDocument(tooMany).toJson(),
        QByteArray(ProjectLimits::templateStoreBytes + 1, ' ')};
    for (const QByteArray &bytes : rejected) {
        QVERIFY(writeBytes(path, bytes));
        model.reloadTemplates();
        QVERIFY(!model.lastError().isEmpty());
        QCOMPARE(model.templates(), templates); // Retain the last good in-memory collection.
        QVERIFY(model.saveCurrentAsTemplate("Would overwrite", "").isEmpty());
        QVERIFY(!model.deleteTemplate(id));
        QCOMPARE(readBytes(path), bytes);
        WidgetModel restarted;
        QVERIFY(!restarted.lastError().isEmpty());
        QVERIFY(restarted.applyTemplate("motorsport-broadcast-smoke")); // Built-ins remain usable.
        QVERIFY(restarted.saveCurrentAsTemplate("Would overwrite after restart", "").isEmpty());
        QCOMPARE(readBytes(path), bytes);
    }
    QVERIFY(writeBytes(path, good));
    model.reloadTemplates();
    QVERIFY(model.lastError().isEmpty());
    QVERIFY(model.updateTemplate(id));
}

void TelemetryTests::boundsLiveWidgetAndCueMutations()
{
    WidgetModel model;
    for (qsizetype index = 0; index < ProjectLimits::maximumWidgets; ++index)
        QVERIFY(model.addWidget("speed") >= 0);
    const int revision = model.revision();
    QCOMPARE(model.addWidget("speed"), -1);
    QCOMPARE(model.duplicateWidget(0), -1);
    QCOMPARE(model.revision(), revision);
    QVERIFY(!model.lastError().isEmpty());
    for (qsizetype index = 0; index < ProjectLimits::maximumCuesPerWidget; ++index)
        QVERIFY(model.addCue(0, 0, 1, "fade") >= 0);
    const int cueRevision = model.revision();
    QCOMPARE(model.addCue(0, 0, 1, "fade"), -1);
    QCOMPARE(model.revision(), cueRevision);
    for (qsizetype widget = 1; widget < ProjectLimits::maximumTotalCues / ProjectLimits::maximumCuesPerWidget; ++widget)
        for (qsizetype cue = 0; cue < ProjectLimits::maximumCuesPerWidget; ++cue)
            QVERIFY(model.addCue(static_cast<int>(widget), 0, 1, "fade") >= 0);
    model.removeWidget(model.count() - 1); // Leave room for a widget but not more cues.
    const int totalRevision = model.revision();
    QCOMPARE(model.addCue(model.count() - 1, 0, 1, "fade"), -1);
    QCOMPARE(model.duplicateWidget(0), -1);
    QCOMPARE(model.revision(), totalRevision);
    const QJsonObject document{{"version", 2}, {"scene", QJsonObject{{"widgets", model.toJson()}}}};
    QVERIFY(ProjectLimits::validateProject(document));
    model.removeCue(0, 0);
    QVERIFY(model.addCue(model.count() - 1, 0, 1, "fade") >= 0);
}

void TelemetryTests::preservesTemplatePickerSelectionById()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings;
    settings.clear();
    settings.sync();
    const bool hadOverride = qEnvironmentVariableIsSet("FLAPPEDEAR_TEMPLATE_STORE");
    const QByteArray previousOverride = qgetenv("FLAPPEDEAR_TEMPLATE_STORE");
    const QString storePath = directory.filePath("layout-templates.json");
    qputenv("FLAPPEDEAR_TEMPLATE_STORE", storePath.toUtf8());
    const auto restoreEnvironment = qScopeGuard([hadOverride, previousOverride] {
        if (hadOverride) {
            qputenv("FLAPPEDEAR_TEMPLATE_STORE", previousOverride);
        } else {
            qunsetenv("FLAPPEDEAR_TEMPLATE_STORE");
        }
    });

    QString templateA;
    QString templateB;
    QString templateC;
    {
        AppController controller(nullptr, directory.filePath("first-recovery.json"));
        WidgetModel *model = controller.widgetModel();
        templateA = model->saveCurrentAsTemplate("A", "unrelated");
        templateB = model->saveCurrentAsTemplate("B", "selected and applied");
        QVERIFY(!templateA.isEmpty());
        QVERIFY(!templateB.isEmpty());

        controller.selectTemplate(templateB);
        QCOMPARE(controller.selectedTemplateId(), templateB);
        model->reloadTemplates();
        QCOMPARE(controller.selectedTemplateId(), templateB);

        QVERIFY(controller.applyTemplate(templateB));
        QCOMPARE(controller.selectedTemplateId(), templateB);
        QCOMPARE(controller.activeTemplateId(), templateB);
        model->setSetting(0, "fontSize", 47);
        QCOMPARE(controller.selectedTemplateId(), templateB);
        QCOMPARE(controller.activeTemplateId(), templateB);
        QVERIFY(controller.saveActiveTemplate());
        QCOMPARE(controller.selectedTemplateId(), templateB);
        QCOMPARE(controller.activeTemplateId(), templateB);

        templateC = model->saveCurrentAsTemplate("C", "saved as new");
        QVERIFY(!templateC.isEmpty());
        controller.selectTemplate(templateC);
        controller.markTemplateActive(templateC);
        QCOMPARE(controller.selectedTemplateId(), templateC);
        QCOMPARE(controller.activeTemplateId(), templateC);
    }

    AppController restored(nullptr, directory.filePath("second-recovery.json"));
    QCOMPARE(restored.selectedTemplateId(), templateC);
    QVERIFY(restored.activeTemplateId().isEmpty());
    QVERIFY(!restored.dirty());

    restored.markTemplateActive(templateC);
    const QString currentProject = directory.filePath("current.fetproject");
    QVERIFY(restored.saveProject(QUrl::fromLocalFile(currentProject)));
    const QString arbitraryProject = directory.filePath("arbitrary.fetproject");
    QVERIFY(writeBytes(arbitraryProject, QJsonDocument(testProject(1.25)).toJson()));
    restored.requestOpenProject(QUrl::fromLocalFile(arbitraryProject));
    QTRY_VERIFY(!restored.projectLoading());
    QVERIFY(restored.activeTemplateId().isEmpty());
    QCOMPARE(restored.selectedTemplateId(), templateC);
    QVERIFY(!restored.dirty());

    restored.selectTemplate(templateB);
    QCOMPARE(restored.selectedTemplateId(), templateB);
    QVERIFY(!restored.dirty());
    restored.selectTemplate(templateC);
    QVERIFY(!restored.dirty());

    QVERIFY(restored.widgetModel()->deleteTemplate(templateA));
    QCOMPARE(restored.selectedTemplateId(), templateC);
    QVERIFY(restored.widgetModel()->deleteTemplate(templateC));
    const QVariantList remaining = restored.widgetModel()->templates();
    QVERIFY(!remaining.isEmpty());
    QCOMPARE(restored.selectedTemplateId(), remaining.constFirst().toMap().value("id").toString());

    settings.clear();
    settings.sync();
}

void TelemetryTests::preservesOptionalFontSettings()
{
    WidgetModel source;
    const int gear = source.addWidget("retroCustomValue");
    QVERIFY(gear >= 0);
    QCOMPARE(source.widget(gear).value("settings").toMap().value("fontSize").toDouble(), 0.0);
    source.setSetting(gear, "fontSize", 0);
    QCOMPARE(source.widget(gear).value("settings").toMap().value("fontSize").toDouble(), 0.0);
    source.setSetting(gear, "fontSize", 44);
    QCOMPARE(source.widget(gear).value("settings").toMap().value("fontSize").toDouble(), 44.0);
    const int duplicate = source.duplicateWidget(gear);
    QCOMPARE(source.widget(duplicate).value("settings").toMap().value("fontSize").toDouble(), 44.0);
    source.setSetting(gear, "fontSize", std::numeric_limits<double>::infinity());
    QCOMPARE(source.widget(gear).value("settings").toMap().value("fontSize").toDouble(), 0.0);

    WidgetModel restored;
    QVERIFY(restored.fromJson(source.toJson()));
    QCOMPARE(restored.widget(duplicate).value("settings").toMap().value("fontSize").toDouble(), 44.0);
    const int retroCustom = restored.addWidget("retroCustomValue");
    QCOMPARE(restored.widget(retroCustom).value("settings").toMap().value("fallbackText").toString(), QString("—"));
}

void TelemetryTests::preservesGForcePresentationSettings()
{
    WidgetModel source;
    const int gForce = source.addWidget("f1GForceRadar");
    const QVariantMap defaults = source.widget(gForce).value("settings").toMap();
    QVERIFY(!defaults.value("invertLateral").toBool());
    QVERIFY(!defaults.value("invertLongitudinal").toBool());
    source.setSetting(gForce, "invertLateral", true);
    source.setSetting(gForce, "invertLongitudinal", true);

    WidgetModel restored;
    QVERIFY(restored.fromJson(source.toJson()));
    const QVariantMap settings = restored.widget(0).value("settings").toMap();
    QVERIFY(settings.value("invertLateral").toBool());
    QVERIFY(settings.value("invertLongitudinal").toBool());
}

void TelemetryTests::providesGForceVariants()
{
    WidgetModel source;
    const int radar = source.addWidget("f1GForceRadar");
    const int bar = source.addWidget("gForceMagnitudeBar");
    QVERIFY(radar >= 0);
    QVERIFY(bar >= 0);
    const QVariantMap radarDefaults = source.widget(radar).value("settings").toMap();
    QCOMPARE(radarDefaults.value("maxG").toDouble(), 1.5);
    QCOMPARE(radarDefaults.value("ringStepG").toDouble(), 0.25);
    QVERIFY(radarDefaults.value("showRingLabels").toBool());
    QVERIFY(!radarDefaults.value("showBackground").toBool());
    QVERIFY(!radarDefaults.value("showBorder").toBool());
    QCOMPARE(radarDefaults.value("radarBackgroundColor").toString(), QString("#16232d"));
    QCOMPARE(radarDefaults.value("backgroundOpacity").toDouble(), 0.78);
    QCOMPARE(radarDefaults.value("dotColor").toString(), QString("#f5a623"));
    QCOMPARE(radarDefaults.value("gridColor").toString(), QString("#96a8b8"));
    QCOMPARE(static_cast<int>(std::floor(radarDefaults.value("maxG").toDouble()
                                         / radarDefaults.value("ringStepG").toDouble())), 6);
    source.setSetting(radar, "maxG", std::numeric_limits<double>::infinity());
    source.setSetting(radar, "ringStepG", -3.0);
    QCOMPARE(source.widget(radar).value("settings").toMap().value("maxG").toDouble(), 1.5);
    QCOMPARE(source.widget(radar).value("settings").toMap().value("ringStepG").toDouble(), 0.01);

    const QVariantMap barDefaults = source.widget(bar).value("settings").toMap();
    QCOMPARE(barDefaults.value("maxG").toDouble(), 1.5);
    QCOMPARE(barDefaults.value("labelText").toString(), QString("G-Force"));
    QVERIFY(barDefaults.value("showLabel").toBool());
    QVERIFY(barDefaults.value("showValue").toBool());
    QVERIFY(barDefaults.value("showBackground").toBool());
    QCOMPARE(barDefaults.value("barColor").toString(), QString("#f5a623"));

    source.setSetting(bar, "invertLateral", true);
    source.setSetting(bar, "fontSize", 40);
    WidgetModel restored;
    QVERIFY(restored.fromJson(source.toJson()));
    const QVariantMap restoredBar = restored.widget(bar).value("settings").toMap();
    QVERIFY(restoredBar.value("invertLateral").toBool());
    QCOMPARE(restoredBar.value("fontSize").toDouble(), 40.0);
}

void TelemetryTests::persistsAndSharesCustomTemplates()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const bool hadOverride = qEnvironmentVariableIsSet("FLAPPEDEAR_TEMPLATE_STORE");
    const QByteArray previousOverride = qgetenv("FLAPPEDEAR_TEMPLATE_STORE");
    qputenv(
        "FLAPPEDEAR_TEMPLATE_STORE",
        directory.filePath("layout-templates.json").toUtf8());
    const auto restoreEnvironment = qScopeGuard([hadOverride, previousOverride] {
        if (hadOverride) {
            qputenv("FLAPPEDEAR_TEMPLATE_STORE", previousOverride);
        } else {
            qunsetenv("FLAPPEDEAR_TEMPLATE_STORE");
        }
    });

    WidgetModel source;
    QVERIFY(source.applyTemplate("motorsport-broadcast-smoke"));
    const QString templateId =
        source.saveCurrentAsTemplate("My broadcast", "Reusable race insert");
    QVERIFY(!templateId.isEmpty());

    WidgetModel restored;
    QVERIFY(restored.applyTemplate(templateId));
    QCOMPARE(restored.count(), source.count());
    const QUrl exported = QUrl::fromLocalFile(directory.filePath("shared.fettemplate"));
    QVERIFY(restored.exportTemplate(templateId, exported));
    QVERIFY(QFileInfo::exists(exported.toLocalFile()));
    QVERIFY(restored.deleteTemplate(templateId));
    QVERIFY(!restored.applyTemplate(templateId));

    const QString importedId = restored.importTemplate(exported);
    QVERIFY(!importedId.isEmpty());
    QVERIFY(restored.applyTemplate(importedId));
    QCOMPARE(restored.widget(0).value("type").toString(), QString("retroTachometer"));
}

void TelemetryTests::persistsWidgetAnimationCues()
{
    WidgetModel source;
    const int widget = source.addWidget("lapCurrent");
    QCOMPARE(source.addCue(widget, 12.5, 4.0, "slideUp"), 0);
    source.setCueProperty(widget, 0, "fadeIn", 0.6);
    source.setCueProperty(widget, 0, "fadeOut", 0.8);

    WidgetModel restored;
    QVERIFY(restored.fromJson(source.toJson()));
    const QVariantList cues = restored.widget(0).value("cues").toList();
    QCOMPARE(cues.size(), 1);
    const QVariantMap cue = cues.front().toMap();
    QCOMPARE(cue.value("start").toDouble(), 12.5);
    QCOMPARE(cue.value("duration").toDouble(), 4.0);
    QCOMPARE(cue.value("fadeIn").toDouble(), 0.6);
    QCOMPARE(cue.value("fadeOut").toDouble(), 0.8);
    QCOMPARE(cue.value("effect").toString(), QString("slideUp"));
    restored.removeCue(0, 0);
    QVERIFY(restored.widget(0).value("cues").toList().isEmpty());
}

void TelemetryTests::groupsAndMovesWidgets()
{
    WidgetModel model;
    const int first = model.addWidget("heartRate");
    const int second = model.addWidget("retroCustomValue");
    const double firstX = model.widget(first).value("x").toDouble();
    const double secondX = model.widget(second).value("x").toDouble();
    const QString groupId = model.groupWidgets({first, second});
    QVERIFY(!groupId.isEmpty());
    QCOMPARE(model.groupMembers(first), QVariantList({first, second}));

    model.moveWidget(first, firstX + 0.1, model.widget(first).value("y").toDouble());
    QCOMPARE(model.widget(first).value("x").toDouble(), firstX + 0.1);
    QCOMPARE(model.widget(second).value("x").toDouble(), secondX + 0.1);

    WidgetModel restored;
    QVERIFY(restored.fromJson(model.toJson()));
    QCOMPARE(restored.groupMembers(second), QVariantList({first, second}));
    restored.ungroupWidget(first);
    QCOMPARE(restored.groupMembers(first), QVariantList{QVariant(first)});
    restored.removeWidgets({first, second});
    QCOMPARE(restored.count(), 0);
}

void TelemetryTests::constrainsWidgetGeometry()
{
    WidgetModel model;
    const int index = model.addWidget("speed");
    model.resizeWidget(index, 0.4, 0.3);
    model.moveWidget(index, 0.9, -1.0);
    const QVariantMap widget = model.widget(index);
    QCOMPARE(widget.value("x").toDouble(), 0.6);
    QCOMPARE(widget.value("y").toDouble(), 0.0);
    model.setWidgetProperty(index, "opacity", 4.0);
    QCOMPARE(model.widget(index).value("opacity").toDouble(), 1.0);
}

void TelemetryTests::projectsWestPositiveTracksWithoutMirroring_data()
{
    QTest::addColumn<double>("latitude");
    QTest::addColumn<double>("longitude");
    QTest::newRow("north-east") << 52.0 << 21.0;
    QTest::newRow("north-west") << 52.0 << -21.0;
    QTest::newRow("south-east") << -52.0 << 21.0;
    QTest::newRow("south-west") << -52.0 << -21.0;
    QTest::newRow("cross-zero-axes") << -0.015625 << -0.015625;
}

void TelemetryTests::projectsWestPositiveTracksWithoutMirroring()
{
    QFETCH(double, latitude); QFETCH(double, longitude);
    QString east = "[header]\ncoordinate units = degrees\n[column names]\ntime latitude longitude\n[data]\n";
    QString west = "[comments]\nGenerated by RaceChrono Pro v10.2.4\n[column names]\ntime latitude longitude\n[data]\n";
    // Asymmetric path: east, north, then southwest. Binary-exact coordinates
    // make the two source encodings identical after parsing to float samples.
    const QList<QPointF> offsets{{0, 0}, {.0625, 0}, {.0625, .03125}, {.015625, .015625}};
    for (int index = 0; index < offsets.size(); ++index) {
        const auto lat = latitude + offsets[index].y();
        const auto lon = longitude + offsets[index].x();
        east += QString("%1 %2 %3\n").arg(index).arg(lat, 0, 'f', 8).arg(lon, 0, 'f', 8);
        west += QString("%1 %2 %3\n").arg(index).arg(lat * 60, 0, 'f', 8).arg(-lon * 60, 0, 'f', 8);
    }
    const auto eastSession = VboParser::parse(east);
    const auto westSession = VboParser::parse(west);
    QCOMPARE(westSession.metadata.value("gpsLongitudeConvention"), QString("west-positive"));
    const auto eastMap = buildTrackGeometry(eastSession);
    const auto westMap = buildTrackGeometry(westSession);
    QVERIFY(eastMap.valid && westMap.valid);
    QVERIFY(!eastMap.longitudeIsWestPositive);
    QVERIFY(westMap.longitudeIsWestPositive);
    QCOMPARE(westMap.points, eastMap.points);
    QVERIFY(westMap.points[1].x() > westMap.points[0].x()); // East is right.
    QVERIFY(westMap.points[2].y() < westMap.points[1].y()); // North is up.
    for (const double time : {0.0, .5, 1.0, 1.5, 2.0, 3.0}) {
        const auto eastPoint = currentTrackPoint(eastSession, time, eastMap);
        const auto westPoint = currentTrackPoint(westSession, time, westMap);
        QVERIFY(eastPoint && westPoint);
        QCOMPARE(*westPoint, *eastPoint);
    }
    QCOMPARE(*currentTrackPoint(westSession, 1, westMap), westMap.points[1]);
    // Projection must never rewrite the source or fabricate missing marker data.
    QCOMPARE(westSession.valueAt("longitude", 0).value(), -longitude);
    auto missing = westSession;
    missing.channels["longitude"].values[1] = std::numeric_limits<float>::quiet_NaN();
    QVERIFY(!currentTrackPoint(missing, 1, westMap));
    TelemetryRenderContext context;
    context.setSession(&westSession);
    context.setTrackGeometry(&westMap);
    context.setTime(1);
    const auto marker = context.currentTrackPoint();
    QCOMPARE(marker.value("x").toDouble(), westMap.points[1].x());
    QCOMPARE(marker.value("y").toDouble(), westMap.points[1].y());
}

void TelemetryTests::persistsAndInvalidatesRunTrackConfiguration()
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

void TelemetryTests::cachesStaticTrackGeometry()
{
    constexpr qsizetype pointCount = 30'000;
    TrackGeometry geometry;
    geometry.valid = true;
    geometry.originLatitude = 52.0;
    geometry.originLongitude = 21.0;
    geometry.localCenter = {0.0, 0.0};
    geometry.normalizationScale = 1.0;
    geometry.points.reserve(pointCount);

    TelemetrySession session;
    TelemetryChannel latitude;
    latitude.name = QStringLiteral("latitude");
    latitude.timestamps.reserve(pointCount);
    latitude.values.reserve(pointCount);
    TelemetryChannel longitude;
    longitude.name = QStringLiteral("longitude");
    longitude.timestamps.reserve(pointCount);
    longitude.values.reserve(pointCount);
    for (qsizetype index = 0; index < pointCount; ++index) {
        const double progress = static_cast<double>(index) / static_cast<double>(pointCount - 1);
        geometry.points.append(
            {progress, 0.5 + 0.4 * std::sin(progress * 8.0 * std::numbers::pi)});
        const double timestamp = static_cast<double>(index) / 10.0;
        latitude.timestamps.append(timestamp);
        longitude.timestamps.append(timestamp);
        latitude.values.append(static_cast<float>(52.0 + progress * 0.001));
        longitude.values.append(static_cast<float>(21.0 + progress * 0.001));
    }
    session.channels.insert(latitude.name, latitude);
    session.channels.insert(longitude.name, longitude);
    session.aliases.insert(QStringLiteral("latitude"), latitude.name);
    session.aliases.insert(QStringLiteral("longitude"), longitude.name);

    TelemetryRenderContext context;
    context.setSession(&session);
    QSignalSpy geometryChanges(&context, &TelemetryRenderContext::trackGeometryChanged);
    QElapsedTimer constructionTimer;
    constructionTimer.start();
    context.setTrackGeometry(&geometry);
    const qint64 constructionNanoseconds = constructionTimer.nsecsElapsed();
    QCOMPARE(context.trackPoints().size(), pointCount);
    QCOMPARE(context.trackRevision(), quint64(1));
    QCOMPARE(context.trackConversionCount(), quint64(1));
    QCOMPARE(geometryChanges.count(), 1);

    const QVariantMap startPoint = context.currentTrackPoint();
    QElapsedTimer updatesTimer;
    updatesTimer.start();
    for (int update = 1; update <= 10'000; ++update) {
        context.setTime(static_cast<double>(update % pointCount) / 10.0);
        QVERIFY(context.currentTrackPoint().contains(QStringLiteral("x")));
        QCOMPARE(context.trackPoints().size(), pointCount);
    }
    const qint64 updateNanoseconds = updatesTimer.nsecsElapsed();
    const QVariantMap laterPoint = context.currentTrackPoint();
    QVERIFY(startPoint != laterPoint);
    QCOMPARE(context.trackConversionCount(), quint64(1));
    QCOMPARE(context.trackRevision(), quint64(1));
    QCOMPARE(geometryChanges.count(), 1);
    const quint64 additionalConversions = context.trackConversionCount() - 1;

    geometry.points = {{0.1, 0.2}, {0.8, 0.9}};
    context.setTrackGeometry(&geometry); // The owner intentionally reuses the same storage address.
    QCOMPARE(context.trackPoints().size(), 2);
    QCOMPARE(context.trackPoints().front().toPointF(), QPointF(0.1, 0.2));
    QCOMPARE(context.trackConversionCount(), quint64(2));
    QCOMPARE(context.trackRevision(), quint64(2));
    QCOMPARE(geometryChanges.count(), 2);

    context.setTrackGeometry(nullptr);
    QVERIFY(context.trackPoints().isEmpty());
    QVERIFY(context.currentTrackPoint().isEmpty());
    QCOMPARE(context.trackConversionCount(), quint64(2));
    QCOMPARE(context.trackRevision(), quint64(3));
    QCOMPARE(geometryChanges.count(), 3);

    qInfo().nospace() << "track-cache-benchmark points=" << pointCount
                      << " initial-cache-ms=" << constructionNanoseconds / 1'000'000.0
                      << " time-marker-updates=10000 updates-ms="
                      << updateNanoseconds / 1'000'000.0
                      << " additional-conversions=" << additionalConversions;
}

void TelemetryTests::preservesTimingEditsDuringAutoSync_data()
{
    QTest::addColumn<int>("edit");
    QTest::newRow("offset") << 1;
    QTest::newRow("scale") << 2;
    QTest::newRow("edit-and-restore") << 3;
    QTest::newRow("unedited-result-applies") << 0;
}

void TelemetryTests::preservesTimingEditsDuringAutoSync()
{
    QFETCH(int, edit);
    QSettings settings;
    settings.clear();
    settings.sync();
    QTemporaryDir directory;
    AppController controller(nullptr, directory.filePath("recovery.json"));
    controller.m_syncCancellation = std::make_shared<std::atomic_bool>(false);
    AppController::AutoSyncResult result;
    result.generation = controller.m_document.m_sourceGeneration;
    result.syncRevision = controller.m_syncRevision;
    result.success = true;
    result.candidate.offset = 12.5;
    result.candidate.timeScale = 1.002;
    result.candidate.confidence = 1.0;
    QPromise<AppController::AutoSyncResult> promise;
    promise.start();
    controller.m_syncWatcher.setFuture(promise.future());
    QSignalSpy finished(&controller, &AppController::syncingChanged);
    if (edit == 1) controller.setSyncOffset(7.0);
    if (edit == 2) controller.setTimeScale(1.01);
    if (edit == 3) { controller.setSyncOffset(7.0); controller.setSyncOffset(0.0); }
    const bool cancelled = controller.m_syncCancellation->load();
    promise.addResult(result); // A completed worker can still deliver an already-queued result.
    promise.finish();
    QTRY_VERIFY(!finished.isEmpty());
    QCOMPARE(cancelled, edit != 0);
    QCOMPARE(controller.syncOffset(), edit == 0 ? 12.5 : edit == 1 ? 7.0 : 0.0);
    QCOMPARE(controller.timeScale(), edit == 0 ? 1.002 : edit == 2 ? 1.01 : 1.0);
    if (edit != 0) QVERIFY(controller.syncCandidate().isEmpty());
    else {
        QCOMPARE(controller.syncCandidate().value("timeScale").toDouble(), 1.002);
        controller.applySyncCandidate();
        QCOMPARE(controller.timeScale(), 1.002);
        controller.setSyncOffset(8.0);
        QVERIFY(controller.syncCandidate().isEmpty());
        controller.applySyncCandidate();
        QCOMPARE(controller.syncOffset(), 8.0);
    }
}

void TelemetryTests::gatesWeakSyncCandidates()
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
    latitude.timestamps = {0.0, 1.0, 2.0, 3.0};
    latitude.values = {0.0F, 0.0F, 0.001F, 0.001F};
    TelemetryChannel longitude;
    longitude.name = "longitude";
    longitude.timestamps = latitude.timestamps;
    longitude.values = {0.0F, 0.004F, 0.004F, 0.0F};
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

void TelemetryTests::rendersTelemetryAtExplicitTime()
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

void TelemetryTests::probesMediaInfoJson()
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

void TelemetryTests::parsesMediaSummaryJson()
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

void TelemetryTests::modelsExtendedMediaCharacteristics()
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

void TelemetryTests::rejectsInvalidMediaProbeJson()
{
    try {
        static_cast<void>(MediaProbe::parseJson("not-json", "/invalid.mp4"));
        QFAIL("Invalid ffprobe JSON should throw.");
    } catch (const std::runtime_error &error) {
        QCOMPARE(QString::fromUtf8(error.what()), QStringLiteral("ffprobe returned invalid JSON."));
    }
}

void TelemetryTests::classifiesMediaProbeProcessFailures()
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

void TelemetryTests::reportsMediaProbeLifecycleHeartbeat()
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

void TelemetryTests::cancelsMediaProbeWithoutLeavingItRunning()
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

void TelemetryTests::evaluatesIndependentExportStorageVolumes()
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

void TelemetryTests::resolvesExportFilesystemsForFutureArtifacts()
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

void TelemetryTests::estimatesTemporaryStorageFromRepresentativeSample()
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

void TelemetryTests::streamsRawFramesToSlowConsumer()
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

void TelemetryTests::failsRawFrameTransportWhenConsumerExits()
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

void TelemetryTests::timesOutStalledRawFrameTransport()
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

void TelemetryTests::cancelsBlockedRawFrameTransportPromptly()
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

void TelemetryTests::cleansOnlyManifestOwnedArtifacts()
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

void TelemetryTests::preservesLiveManifestForStartupRecovery()
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

void TelemetryTests::recoversManifestsFromDataAndLegacyFolders()
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

void TelemetryTests::keepsStaleExportArtifactsFromCommandLineRuns()
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

void TelemetryTests::noticesWhenTheParentProcessExits()
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

void TelemetryTests::supervisesUnixExportProcessTree()
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

void TelemetryTests::stopsUnixWritersAcrossLeaderExit_data()
{
    QTest::addColumn<QString>("action");
    QTest::newRow("leader-already-exited") << QStringLiteral("stop");
    QTest::newRow("leader-exits-during-grace") << QStringLiteral("grace");
    QTest::newRow("destructor-after-leader-exit") << QStringLiteral("destructor");
    QTest::newRow("failed-marker-after-leader-exit") << QStringLiteral("marker");
}

void TelemetryTests::stopsUnixWritersAcrossLeaderExit()
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

void TelemetryTests::stopsUnixWritersBeforeControllerCleanup_data()
{
    QTest::addColumn<bool>("cancelled");
    QTest::newRow("cancelled") << true;
    QTest::newRow("reported-success-with-live-writer") << false;
}

void TelemetryTests::stopsUnixWritersBeforeControllerCleanup()
{
#ifdef Q_OS_UNIX
    QFETCH(bool, cancelled);
    QSettings settings;
    settings.clear();
    settings.sync();
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    AppController controller;
    controller.m_exportOutputTransaction = std::make_unique<ExportOutputTransaction>();
    const QString target = directory.filePath("result.mp4");
    QVERIFY(writeBytes(target, "existing user target"));
    const auto prepared = controller.m_exportOutputTransaction->prepare(target, {}, {}, true);
    QCOMPARE(prepared.status, ExportOutputTransaction::PreparationStatus::Ready);
    const QString staging = controller.m_exportOutputTransaction->stagingPath();
    const QString id = controller.m_exportOutputTransaction->transactionId();
    const QString overlay = QDir::temp().filePath(QStringLiteral("flappedear-overlay-%1.mkv").arg(id));
    QVERIFY(writeBytes(overlay, "owned overlay"));
    controller.m_exportCancelPath = directory.filePath("cancel");
    if (cancelled) QVERIFY(writeBytes(controller.m_exportCancelPath, {}));
    controller.m_exportState = cancelled ? QStringLiteral("cancelling") : QStringLiteral("complete");
    controller.m_exportProcess = std::make_unique<QProcess>();
    controller.m_exportSupervisor = std::make_unique<ExportProcessSupervisor>(*controller.m_exportProcess);
    const QString ready = directory.filePath("writer.ready");
    controller.m_exportSupervisor->start(QStringLiteral(RAW_TRANSPORT_CONSUMER_PATH),
        {QStringLiteral("tree-leader-exits"), staging, ready});
    QVERIFY(controller.m_exportSupervisor->waitForStarted());
    const auto leaderPid = static_cast<pid_t>(controller.m_exportProcess->processId());
    const auto emergencyStop = qScopeGuard([leaderPid] { if (leaderPid > 1) ::kill(-leaderPid, SIGKILL); });
    QTRY_VERIFY_WITH_TIMEOUT(!readBytes(ready).isEmpty(), 2'000);
    const qint64 writerPid = readBytes(ready).toLongLong();
    QVERIFY(writerPid > 1);
    const ExportArtifactManifestData manifest{id, QDateTime::currentMSecsSinceEpoch(), overlay,
        staging, target, leaderPid, QStringLiteral("stageB")};
    QString error;
    QVERIFY2(ExportArtifactManifest::create(manifest, &error), qPrintable(error));
    const QString manifestPath = ExportArtifactManifest::manifestPathFor(id);
    controller.m_exportManifestPath = manifestPath;
    QCOMPARE(controller.m_exportProcess->write("R", 1), qint64(1));
    QVERIFY(controller.m_exportProcess->waitForBytesWritten(2'000));
    QVERIFY(controller.m_exportProcess->state() == QProcess::NotRunning
            || controller.m_exportProcess->waitForFinished(2'000));

    QVERIFY(!ExportArtifactManifest::recoverStale().contains(manifestPath));
    QVERIFY(QFileInfo::exists(staging));
    QVERIFY(QFileInfo::exists(overlay));
    QVERIFY(controller.exporting());
    controller.finishExport(0, QProcess::NormalExit);
    QVERIFY(!controller.exporting());
    QCOMPARE(controller.exportState(), cancelled ? QStringLiteral("cancelled") : QStringLiteral("failed"));
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

void TelemetryTests::boundsImmediateProcessTreeStop()
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

void TelemetryTests::stopsExportWorkerWhenCancellationMarkerCannotBeCreated()
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

void TelemetryTests::preservesPartialOverlapInAnalysisSeries()
{
    QSettings settings;
    settings.clear();
    settings.sync();
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("analysis.vbo"));
    QVERIFY(writeBytes(path,
                       "[column names]\ntime ramp flat gapped\n[data]\n"
                       "0 0 42 0\n1 10 42 10\n2 20 42 20\n3 30 42 30\n10 100 42 100\n11 110 42 110\n"));

    AppController controller(nullptr, directory.filePath(QStringLiteral("recovery.json")));
    controller.loadVbo(QUrl::fromLocalFile(path));
    QTRY_COMPARE(controller.vboLoadState(), QStringLiteral("ready"));

    // A negative offset maps the beginning of the video before telemetry starts.
    // The overlapping half must remain a plotted segment, not disappear wholesale.
    controller.setSyncOffset(-2.0);
    const QVariantMap ramp = controller.telemetrySeries(QStringLiteral("ramp"), 0.0, 6.0, 100);
    const QVariantList rampSegments = ramp.value(QStringLiteral("segments")).toList();
    QVERIFY(!rampSegments.isEmpty());
    const QVariantList firstSegment = rampSegments.front().toList();
    qsizetype totalRampPoints = 0;
    for (const QVariant &segment : rampSegments) {
        totalRampPoints += segment.toList().size();
    }
    QVERIFY(totalRampPoints >= 2);
    const double firstX = firstSegment.front().toMap().value(QStringLiteral("x")).toDouble();
    QVERIFY2(firstX >= 0.32 && firstX <= 0.34, qPrintable(QString::number(firstX)));

    const QVariantMap constant = controller.telemetrySeries(QStringLiteral("flat"), 0.0, 6.0, 100);
    QVERIFY(!constant.value(QStringLiteral("segments")).toList().isEmpty());
    QCOMPARE(constant.value(QStringLiteral("minimum")).toDouble(), 42.0);
    QCOMPARE(constant.value(QStringLiteral("maximum")).toDouble(), 42.0);

    const QVariantMap gapped = controller.telemetrySeries(QStringLiteral("gapped"), 0.0, 14.0, 100);
    QVERIFY(gapped.value(QStringLiteral("segments")).toList().size() >= 2);
}

void TelemetryTests::retainsTelemetryAfterFailedAsyncLoad()
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

void TelemetryTests::replacesInFlightSourceLoad()
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

void TelemetryTests::shutsDownWithInFlightSourceLoad()
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

void TelemetryTests::opensProjectsTransactionally()
{
    QSettings settings;
    settings.clear();
    settings.sync();
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    // Its own recovery file: a snapshot an earlier test left in the shared
    // default would hold Open behind "Recover unsaved changes?".
    AppController controller(nullptr, directory.filePath(QStringLiteral("recovery.json")));
    QVERIFY(!controller.recoveryPending());
    controller.loadVbo(QUrl::fromLocalFile(QStringLiteral(TEST_FIXTURE_PATH)));
    QTRY_COMPARE(controller.vboLoadState(), QStringLiteral("ready"));
    QVERIFY(controller.dirty());

    const QJsonObject scene{{"widgets", controller.widgetModel()->toJson()}};
    const QJsonObject failedProject{{"version", 2},
                                    {"scene", scene},
                                    {"vboPath", directory.filePath("missing.vbo")},
                                    {"sync", QJsonObject{{"offset", 4.0}, {"timeScale", 1.0}}}};
    const QString failedPath = directory.filePath("missing-source.fetproject");
    QVERIFY(writeBytes(failedPath, QJsonDocument(failedProject).toJson()));
    controller.requestOpenProject(QUrl::fromLocalFile(failedPath));
    controller.resolveDestructiveAction("discard");
    QTRY_VERIFY(!controller.projectLoading());
    QVERIFY(controller.projectLoadError().isEmpty());
    QCOMPARE(controller.vboLoadState(), QStringLiteral("missing"));
    QCOMPARE(controller.telemetryName(), QStringLiteral("missing.vbo"));
    QVERIFY(controller.channelNames().isEmpty());
    QCOMPARE(controller.widgetModel()->toJson(), scene.value(QStringLiteral("widgets")).toArray());
    QCOMPARE(controller.syncOffset(), 4.0);
    QCOMPARE(controller.projectPath().toLocalFile(), QFileInfo(failedPath).canonicalFilePath());
    QVERIFY(!controller.dirty());

    const QJsonObject successProject{{"version", 2},
                                     {"scene", scene},
                                     {"vboPath", QStringLiteral(TEST_FIXTURE_PATH)},
                                     {"sync", QJsonObject{{"offset", 2.5}, {"timeScale", 1.0}}},
                                     {"analysis", QJsonObject{{"channels", QJsonArray{}}, {"visible", true}}}};
    const QString successPath = directory.filePath("valid.fetproject");
    QVERIFY(writeBytes(successPath, QJsonDocument(successProject).toJson()));
    controller.requestOpenProject(QUrl::fromLocalFile(successPath));
    controller.resolveDestructiveAction("discard");
    QTRY_COMPARE(controller.vboLoadState(), QStringLiteral("ready"));
    QCOMPARE(controller.projectPath().toLocalFile(), QFileInfo(successPath).canonicalFilePath());
    QCOMPARE(controller.telemetryName(), QStringLiteral("basic.vbo"));
    QCOMPARE(controller.syncOffset(), 2.5);
    QVERIFY(!controller.dirty());
    QVERIFY(controller.saveCurrentProject());
    const QJsonObject migrated = QJsonDocument::fromJson(readBytes(successPath)).object();
    QVERIFY(!migrated.contains(QStringLiteral("vboPath")));
    QVERIFY(!migrated.value(QStringLiteral("sources")).toObject()
                 .value(QStringLiteral("telemetry")).toObject()
                 .value(QStringLiteral("fingerprint")).toObject().isEmpty());
}

void TelemetryTests::boundsWidgetAndTemplateCardinality()
{
    QJsonArray widgets;
    for (qsizetype index = 0; index <= ProjectLimits::maximumWidgets; ++index) {
        widgets.append(QJsonObject{{QStringLiteral("id"), QStringLiteral("w%1").arg(index)},
                                   {QStringLiteral("type"), QStringLiteral("speed")},
                                   {QStringLiteral("settings"), QJsonObject{}},
                                   {QStringLiteral("cues"), QJsonArray{}}});
    }
    QJsonObject project = testProject(0.0);
    project.insert(QStringLiteral("scene"), QJsonObject{{QStringLiteral("widgets"), widgets}});
    QString error;
    QVERIFY(!ProjectLimits::validateProject(project, &error));
    QVERIFY(error.contains(QStringLiteral("Widget count")));

    QJsonObject settings;
    for (qsizetype index = 0; index <= ProjectLimits::maximumSettingsEntries; ++index) {
        settings.insert(QStringLiteral("s%1").arg(index), 1);
    }
    project = testProject(0.0);
    QJsonObject widget = project.value(QStringLiteral("scene")).toObject()
                             .value(QStringLiteral("widgets")).toArray().first().toObject();
    widget.insert(QStringLiteral("settings"), settings);
    project.insert(QStringLiteral("scene"), QJsonObject{{QStringLiteral("widgets"), QJsonArray{widget}}});
    QVERIFY(!ProjectLimits::validateProject(project, &error));
    QVERIFY(error.contains(QStringLiteral("settings")));

    QJsonArray templates;
    const QJsonObject validTemplate{{QStringLiteral("id"), QStringLiteral("one")},
                                    {QStringLiteral("name"), QStringLiteral("One")},
                                    {QStringLiteral("description"), QStringLiteral("Normal")},
                                    {QStringLiteral("widgets"), QJsonArray{project.value(QStringLiteral("scene")).toObject().value(QStringLiteral("widgets")).toArray().first()}}};
    for (qsizetype index = 0; index <= ProjectLimits::maximumTemplateCount; ++index) templates.append(validTemplate);
    QVERIFY(!ProjectLimits::validateTemplateStore(
        QJsonObject{{QStringLiteral("schemaVersion"), 1}, {QStringLiteral("templates"), templates}}, &error));
    QVERIFY(error.contains(QStringLiteral("Template store")));
}

void TelemetryTests::boundsProcessOutputAndProgressLines()
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

    FfmpegProgressParser parser;
    static_cast<void>(parser.append(QByteArray(ProcessOutputLimits::ffmpegProgressLineBytes + 1, 'x')));
    QVERIFY(parser.overflowed());
    const QList<FfmpegProgress> progress = parser.append(QByteArrayLiteral("frame=12\nprogress=end\n"));
    QCOMPARE(progress.size(), 1);
    QCOMPARE(progress.first().encodedFrames, qsizetype(12));
}

void TelemetryTests::surfacesAndRetriesRecoveryPersistenceFailure()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString recoveryPath = directory.filePath(QStringLiteral("recovery.json"));
    QVERIFY(QDir().mkpath(recoveryPath)); // A directory cannot be atomically replaced as a snapshot file.
    QSettings settings;
    settings.clear();
    settings.sync();
    AppController controller(nullptr, recoveryPath);
    controller.setSyncOffset(1.0);
    QTRY_VERIFY(controller.recoveryDegraded());
    QVERIFY(!controller.recoveryError().isEmpty());
    QVERIFY(controller.dirty()); // Editing remains available while recovery is unavailable.
    QVERIFY(controller.saveProject(QUrl::fromLocalFile(directory.filePath(QStringLiteral("manual.fetproject")))));
    QVERIFY(!controller.dirty()); // Authoritative manual save is independent of recovery failure.

    QVERIFY(QDir().rmdir(recoveryPath));
    controller.setSyncOffset(2.0);
    QTRY_VERIFY(!controller.recoveryDegraded());
    QVERIFY(QFileInfo(recoveryPath).isFile());
}

void TelemetryTests::exportsSyntheticRczThroughWorker_data()
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

void TelemetryTests::exportsSyntheticRczThroughWorker()
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
void TelemetryTests::exportsEditListSourceThroughWorker()
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

void TelemetryTests::savesReopensAndRelinksRcz()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings;
    settings.clear();
    settings.sync();
    const auto source = directory.filePath(QStringLiteral("synthetic.rcz"));
    const auto project = directory.filePath(QStringLiteral("native.fetproject"));
    QVERIFY(writeBytes(source, RczFixture::zip(RczFixture::members())));
    AppController writer(nullptr, directory.filePath(QStringLiteral("writer.json")));
    writer.loadVbo(QUrl::fromLocalFile(source));
    QTRY_COMPARE(writer.vboLoadState(), QStringLiteral("ready"));
    QCOMPARE(writer.telemetryName(), QStringLiteral("synthetic.rcz"));
    QVERIFY(writer.saveProject(QUrl::fromLocalFile(project)));
    settings.clear();
    settings.sync();
    AppController reader(nullptr, directory.filePath(QStringLiteral("reader.json")));
    reader.requestOpenProject(QUrl::fromLocalFile(project));
    QTRY_COMPARE(reader.vboLoadState(), QStringLiteral("ready"));
    QCOMPARE(reader.telemetryName(), QStringLiteral("synthetic.rcz"));
    QVERIFY(!reader.dirty());
    const auto replacement = directory.filePath(QStringLiteral("relinked.RCZ"));
    QVERIFY(QFile::rename(source, replacement));
    reader.relinkVbo(QUrl::fromLocalFile(replacement));
    QTRY_COMPARE(reader.telemetryName(), QStringLiteral("relinked.RCZ"));
    QCOMPARE(reader.vboLoadState(), QStringLiteral("ready"));
    QVERIFY(reader.saveCurrentProject());
    const auto telemetry = QJsonDocument::fromJson(readBytes(project)).object()
        .value(QStringLiteral("sources")).toObject().value(QStringLiteral("telemetry")).toObject();
    QCOMPARE(telemetry.value(QStringLiteral("relativePath")).toString(), QStringLiteral("relinked.RCZ"));
}

void TelemetryTests::serializesPortableProjectSourcesAndMovesFolder()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString original = directory.filePath(QStringLiteral("TrackDay"));
    QVERIFY(QDir().mkpath(QDir(original).filePath(QStringLiteral("media"))));
    const QString videoPath = QDir(original).filePath(QStringLiteral("media/camera.mp4"));
    QVERIFY(writeBytes(videoPath, QByteArrayLiteral("path-resolution fixture")));
    const QString vboPath = QDir(original).filePath(QStringLiteral("media/session.vbo"));
    QVERIFY(QFile::copy(QStringLiteral(TEST_FIXTURE_PATH), vboPath));
    const QString projectPath = QDir(original).filePath(QStringLiteral("Project.fetproject"));
    const ProjectSourceReference videoReference =
        ProjectSourceReferenceCodec::forLoadedSource(
            videoPath, QJsonObject{{QStringLiteral("kind"), QStringLiteral("video-v1")}});
    const QJsonObject serializedVideo = ProjectSourceReferenceCodec::toJson(
        videoReference, projectPath);
    QCOMPARE(serializedVideo.value(QStringLiteral("relativePath")).toString(),
             QStringLiteral("media/camera.mp4"));

    QSettings settings;
    settings.clear();
    settings.sync();
    AppController writer(nullptr, directory.filePath(QStringLiteral("recovery-a.json")));
    writer.loadVbo(QUrl::fromLocalFile(vboPath));
    QTRY_COMPARE(writer.vboLoadState(), QStringLiteral("ready"));
    QVERIFY(writer.saveProject(QUrl::fromLocalFile(projectPath)));

    const QJsonObject saved = QJsonDocument::fromJson(readBytes(projectPath)).object();
    QVERIFY(!saved.contains(QStringLiteral("vboPath")));
    const QJsonObject telemetry = saved.value(QStringLiteral("sources")).toObject()
                                      .value(QStringLiteral("telemetry")).toObject();
    QCOMPARE(telemetry.value(QStringLiteral("relativePath")).toString(),
             QStringLiteral("media/session.vbo"));
    QVERIFY(!telemetry.value(QStringLiteral("fingerprint")).toObject().isEmpty());

    const QString moved = directory.filePath(QStringLiteral("MovedTrackDay"));
    QVERIFY(QDir().rename(original, moved));
    const QString movedProjectPath = QDir(moved).filePath(QStringLiteral("Project.fetproject"));
    const QJsonObject portableVideoProject{{QStringLiteral("sources"), QJsonObject{
        {QStringLiteral("video"), serializedVideo}}}};
    const ProjectSourceReference movedVideo = ProjectSourceReferenceCodec::fromProject(
        portableVideoProject, QStringLiteral("video"), QStringLiteral("videoPath"));
    QCOMPARE(ProjectSourceReferenceCodec::resolve(movedVideo, movedProjectPath),
             QFileInfo(QDir(moved).filePath(QStringLiteral("media/camera.mp4"))).canonicalFilePath());
    settings.clear();
    settings.sync();
    AppController reader(nullptr, directory.filePath(QStringLiteral("recovery-b.json")));
    reader.requestOpenProject(QUrl::fromLocalFile(
        movedProjectPath));
    QTRY_COMPARE(reader.vboLoadState(), QStringLiteral("ready"));
    QCOMPARE(reader.telemetryName(), QStringLiteral("session.vbo"));
    QVERIFY(!reader.dirty());
}

void TelemetryTests::opensProjectsWithMissingSources()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings;
    settings.clear();
    settings.sync();
    QJsonObject project = testProject(3.25, {{QStringLiteral("future"), 42}});
    project.insert(QStringLiteral("sources"), QJsonObject{
        {QStringLiteral("video"), QJsonObject{{QStringLiteral("relativePath"), QStringLiteral("media/missing.mp4")},
                                                {QStringLiteral("futureSourceField"), 17}}},
        {QStringLiteral("telemetry"), QJsonObject{{QStringLiteral("relativePath"), QStringLiteral("media/missing.vbo")}}},
    });
    const QString path = directory.filePath(QStringLiteral("missing.fetproject"));
    QVERIFY(writeBytes(path, QJsonDocument(project).toJson()));

    AppController controller(nullptr, directory.filePath(QStringLiteral("recovery.json")));
    controller.requestOpenProject(QUrl::fromLocalFile(path));
    QTRY_VERIFY(!controller.projectLoading());
    QCOMPARE(controller.videoLoadState(), QStringLiteral("missing"));
    QCOMPARE(controller.vboLoadState(), QStringLiteral("missing"));
    QCOMPARE(controller.syncOffset(), 3.25);
    QVERIFY(!controller.dirty());
    controller.setSyncOffset(4.0);
    QTRY_VERIFY(QFileInfo(directory.filePath(QStringLiteral("recovery.json"))).isFile());
    ProjectRecoveryStore recovery(directory.filePath(QStringLiteral("recovery.json")));
    ProjectRecoverySnapshot snapshot;
    QString recoveryError;
    QVERIFY2(recovery.load(&snapshot, &recoveryError), qPrintable(recoveryError));
    QCOMPARE(snapshot.project.value(QStringLiteral("sources")).toObject()
                 .value(QStringLiteral("telemetry")).toObject()
                 .value(QStringLiteral("relativePath")).toString(),
             QStringLiteral("media/missing.vbo"));
    QVERIFY(controller.saveCurrentProject());
    const QJsonObject reloaded = QJsonDocument::fromJson(readBytes(path)).object();
    QCOMPARE(reloaded.value(QStringLiteral("future")).toInt(), 42);
    QCOMPARE(reloaded.value(QStringLiteral("sources")).toObject()
                 .value(QStringLiteral("video")).toObject()
                 .value(QStringLiteral("relativePath")).toString(),
             QStringLiteral("media/missing.mp4"));
    QCOMPARE(reloaded.value(QStringLiteral("sources")).toObject()
                 .value(QStringLiteral("video")).toObject()
                 .value(QStringLiteral("futureSourceField")).toInt(), 17);
}

void TelemetryTests::fingerprintsSourcesDeterministically()
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
    speed.values = {1.0F, 2.0F};
    sessionA.channels.insert(speed.name, speed);
    TelemetrySession sessionB = sessionA;
    TelemetryChannel rpm;
    rpm.name = QStringLiteral("rpm");
    rpm.unit = QStringLiteral("rpm");
    rpm.values = {1000.0F, 2000.0F};
    sessionB.channels.insert(rpm.name, rpm);
    const QJsonObject telemetryA = ProjectSourceReferenceCodec::telemetryFingerprint(first, sessionA);
    QCOMPARE(ProjectSourceReferenceCodec::compareFingerprints(
                 telemetryA, ProjectSourceReferenceCodec::telemetryFingerprint(first, sessionB)),
             SourceFingerprintMatch::Mismatch);
}

void TelemetryTests::preservesInterleavedSourceRequests_data()
{
    QTest::addColumn<bool>("videoFirst");
    QTest::addColumn<bool>("secondRelink");
    QTest::addColumn<bool>("mismatch");
    for (bool video : {false, true}) for (bool relink : {false, true}) for (bool mismatch : {false, true})
        QTest::newRow(qPrintable(QStringLiteral("video%1-relink%2-mismatch%3").arg(video).arg(relink).arg(mismatch)))
            << video << relink << mismatch;
}

void TelemetryTests::preservesInterleavedSourceRequests()
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

void TelemetryTests::relinksTelemetryWithMismatchPolicy()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings;
    settings.clear();
    settings.sync();
    QJsonObject project = testProject(0.0);
    project.insert(QStringLiteral("sources"), QJsonObject{{QStringLiteral("telemetry"),
        QJsonObject{{QStringLiteral("relativePath"), QStringLiteral("missing.vbo")},
                    {QStringLiteral("fingerprint"), QJsonObject{{QStringLiteral("kind"), QStringLiteral("telemetry-v1")},
                                                                  {QStringLiteral("size"), 1}}}}}});
    const QString projectPath = directory.filePath(QStringLiteral("relink.fetproject"));
    QVERIFY(writeBytes(projectPath, QJsonDocument(project).toJson()));
    AppController controller(nullptr, directory.filePath(QStringLiteral("recovery.json")));
    controller.requestOpenProject(QUrl::fromLocalFile(projectPath));
    QTRY_COMPARE(controller.vboLoadState(), QStringLiteral("missing"));
    controller.relinkVbo(QUrl::fromLocalFile(QStringLiteral(TEST_FIXTURE_PATH)));
    QTRY_COMPARE(controller.vboLoadState(), QStringLiteral("mismatch"));
    QCOMPARE(controller.sourceMismatchType(), QStringLiteral("telemetry"));
    QVERIFY(controller.telemetryDuration() == 0.0);
    controller.resolveSourceMismatch(true);
    QCOMPARE(controller.vboLoadState(), QStringLiteral("ready"));
    QVERIFY(controller.dirty());

    const QString invalid = directory.filePath(QStringLiteral("invalid.vbo"));
    QVERIFY(writeBytes(invalid, QByteArrayLiteral("invalid")));
    controller.relinkVbo(QUrl::fromLocalFile(invalid));
    QTRY_COMPARE(controller.vboLoadState(), QStringLiteral("ready"));
    QCOMPARE(controller.telemetryName(), QStringLiteral("basic.vbo"));
}

void TelemetryTests::rejectsStaleRelinkResults()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings;
    settings.clear();
    settings.sync();
    QString large = QStringLiteral("[column names]\ntime speed\n[data]\n");
    for (int row = 0; row < 200'000; ++row) {
        large += QStringLiteral("%1 %2\n").arg(row).arg(row % 200);
    }
    const QString slow = directory.filePath(QStringLiteral("slow.vbo"));
    QVERIFY(writeBytes(slow, large.toUtf8()));
    AppController controller(nullptr, directory.filePath(QStringLiteral("recovery.json")));
    controller.relinkVbo(QUrl::fromLocalFile(slow));
    controller.relinkVbo(QUrl::fromLocalFile(QStringLiteral(TEST_FIXTURE_PATH)));
    QTRY_COMPARE_WITH_TIMEOUT(controller.vboLoadState(), QStringLiteral("ready"), 10'000);
    QCOMPARE(controller.telemetryName(), QStringLiteral("basic.vbo"));
    QCOMPARE(controller.sampleCount(), 3);
}

void TelemetryTests::restoresSavedProjectsAndPreservesUnknownFields()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings;
    settings.clear();
    const QString projectPath = directory.filePath(QStringLiteral("authoritative.fetproject"));
    const QString recoveryPath = directory.filePath(QStringLiteral("recovery.json"));
    const QJsonObject future{{QStringLiteral("something"), 123}};
    QJsonObject project = testProject(1.25, {{QStringLiteral("futureField"), future}});
    QJsonObject scene = project.value(QStringLiteral("scene")).toObject();
    scene.insert(QStringLiteral("futureSceneField"), QStringLiteral("preserve me"));
    project.insert(QStringLiteral("scene"), scene);
    QVERIFY(writeBytes(projectPath, QJsonDocument(project).toJson()));
    settings.setValue(QStringLiteral("project/path"), projectPath);
    settings.setValue(QStringLiteral("sync/offset"), 99.0);
    settings.setValue(QStringLiteral("editor/widgets"), QByteArray("legacy"));
    settings.sync();

    {
        AppController controller(nullptr, recoveryPath);
        QTRY_VERIFY(!controller.projectLoading());
        QCOMPARE(controller.syncOffset(), 1.25);
        QVERIFY(!controller.dirty());
        controller.setSyncOffset(2.5);
        QVERIFY(controller.saveCurrentProject());
        QVERIFY(!controller.dirty());
    }

    QFile saved(projectPath);
    QVERIFY(saved.open(QIODevice::ReadOnly));
    const QJsonObject reloaded = QJsonDocument::fromJson(saved.readAll()).object();
    QCOMPARE(reloaded.value(QStringLiteral("futureField")).toObject(), future);
    QCOMPARE(reloaded.value(QStringLiteral("scene")).toObject()
                 .value(QStringLiteral("futureSceneField")).toString(),
             QStringLiteral("preserve me"));
    QCOMPARE(reloaded.value(QStringLiteral("sync")).toObject()
                 .value(QStringLiteral("offset")).toDouble(), 2.5);
    QVERIFY(!reloaded.value(QStringLiteral("analysis")).toObject()
                 .contains(QStringLiteral("visible")));
    QVERIFY(!settings.contains(QStringLiteral("sync/offset")));
    QVERIFY(!settings.contains(QStringLiteral("editor/widgets")));
}

void TelemetryTests::recoversAndDiscardsSavedChanges()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings;
    settings.clear();
    const QString projectPath = directory.filePath(QStringLiteral("saved.fetproject"));
    const QString recoveryPath = directory.filePath(QStringLiteral("recovery.json"));
    QVERIFY(writeBytes(projectPath, QJsonDocument(testProject(1.0)).toJson()));
    settings.setValue(QStringLiteral("project/path"), projectPath);
    settings.sync();

    {
        AppController controller(nullptr, recoveryPath);
        QTRY_VERIFY(!controller.projectLoading());
        controller.setSyncOffset(7.0);
        QTRY_VERIFY(QFileInfo(recoveryPath).isFile());
    }
    {
        AppController controller(nullptr, recoveryPath);
        QVERIFY(controller.recoveryPending());
        controller.resolveStartupRecovery(QStringLiteral("recover"));
        QTRY_VERIFY(!controller.projectLoading());
        QCOMPARE(controller.syncOffset(), 7.0);
        QCOMPARE(controller.projectPath().toLocalFile(), QFileInfo(projectPath).canonicalFilePath());
        QVERIFY(controller.dirty());
    }
    {
        AppController controller(nullptr, recoveryPath);
        QVERIFY(controller.recoveryPending());
        controller.resolveStartupRecovery(QStringLiteral("discard"));
        QTRY_VERIFY(!controller.projectLoading());
        QCOMPARE(controller.syncOffset(), 1.0);
        QVERIFY(!controller.dirty());
        QVERIFY(!QFileInfo(recoveryPath).exists());
    }
}

void TelemetryTests::recoversAndDiscardsUnsavedDocuments()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings;
    settings.clear();
    settings.sync();
    const QString recoveryPath = directory.filePath(QStringLiteral("recovery.json"));
    int recoveredWidgetCount = 0;
    {
        AppController controller(nullptr, recoveryPath);
        controller.widgetModel()->addWidget(QStringLiteral("retroCustomValue"));
        controller.setSyncOffset(4.0);
        recoveredWidgetCount = controller.widgetModel()->count();
        QTRY_VERIFY(QFileInfo(recoveryPath).isFile());
    }
    {
        AppController controller(nullptr, recoveryPath);
        QVERIFY(controller.recoveryPending());
        controller.resolveStartupRecovery(QStringLiteral("recover"));
        QTRY_VERIFY(!controller.projectLoading());
        QCOMPARE(controller.widgetModel()->count(), recoveredWidgetCount);
        QCOMPARE(controller.syncOffset(), 4.0);
        QVERIFY(controller.projectPath().isEmpty());
        QVERIFY(controller.dirty());
    }
    {
        AppController controller(nullptr, recoveryPath);
        QVERIFY(controller.recoveryPending());
        controller.resolveStartupRecovery(QStringLiteral("discard"));
        QVERIFY(!controller.projectLoading());
        QCOMPARE(controller.syncOffset(), 0.0);
        QVERIFY(controller.projectPath().isEmpty());
        QVERIFY(!controller.dirty());
        QVERIFY(!QFileInfo(recoveryPath).exists());
    }
}

void TelemetryTests::discardsUnsavedStateForQuitNewAndOpen()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings;
    settings.clear();
    const QString projectA = directory.filePath(QStringLiteral("a.fetproject"));
    const QString projectB = directory.filePath(QStringLiteral("b.fetproject"));
    const QString recoveryPath = directory.filePath(QStringLiteral("recovery.json"));
    QVERIFY(writeBytes(projectA, QJsonDocument(testProject(1.0)).toJson()));
    QVERIFY(writeBytes(projectB, QJsonDocument(testProject(3.0)).toJson()));
    settings.setValue(QStringLiteral("project/path"), projectA);
    settings.sync();

    {
        AppController controller(nullptr, recoveryPath);
        QTRY_VERIFY(!controller.projectLoading());
        controller.widgetModel()->addWidget(QStringLiteral("retroCustomValue"));
        controller.setSyncOffset(8.0);
        controller.loadVbo(QUrl::fromLocalFile(QStringLiteral(TEST_FIXTURE_PATH)));
        QTRY_COMPARE(controller.vboLoadState(), QStringLiteral("ready"));
        QTRY_VERIFY(QFileInfo(recoveryPath).isFile());
        QSignalSpy quitSpy(&controller, &AppController::quitApproved);
        controller.requestQuit();
        controller.resolveDestructiveAction(QStringLiteral("discard"));
        QCOMPARE(quitSpy.count(), 1);
        QVERIFY(!QFileInfo(recoveryPath).exists());
    }
    {
        AppController controller(nullptr, recoveryPath);
        QTRY_VERIFY(!controller.projectLoading());
        QCOMPARE(controller.syncOffset(), 1.0);
        QVERIFY(controller.telemetryName().isEmpty());
        QVERIFY(!controller.dirty());

        controller.setSyncOffset(8.0);
        QTRY_VERIFY(QFileInfo(recoveryPath).isFile());
        controller.requestNewProject();
        controller.resolveDestructiveAction(QStringLiteral("discard"));
        QCOMPARE(controller.syncOffset(), 0.0);
        QVERIFY(controller.projectPath().isEmpty());
        QVERIFY(!controller.dirty());
        QVERIFY(!QFileInfo(recoveryPath).exists());

        controller.setSyncOffset(9.0);
        QTRY_VERIFY(QFileInfo(recoveryPath).isFile());
        controller.requestOpenProject(QUrl::fromLocalFile(projectB));
        controller.resolveDestructiveAction(QStringLiteral("discard"));
        QTRY_VERIFY(!controller.projectLoading());
        QCOMPARE(controller.syncOffset(), 3.0);
        QCOMPARE(controller.projectPath().toLocalFile(), QFileInfo(projectB).canonicalFilePath());
        QVERIFY(!controller.dirty());
        QVERIFY(!QFileInfo(recoveryPath).exists());
    }
}

void TelemetryTests::continuesDiscardedQuitWhenRecoveryDeletionFails()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings;
    settings.clear();
    settings.sync();
    const QString recoveryPath = directory.filePath(QStringLiteral("recovery.json"));
    ProjectRecoveryStore::Operations operations;
    operations.clearSnapshot = [](QString *error) {
        if (error) *error = QStringLiteral("injected recovery deletion failure");
        return false;
    };

    {
        AppController controller(nullptr, recoveryPath, operations);
        controller.setSyncOffset(8.0);
        QTRY_VERIFY(QFileInfo(recoveryPath).isFile());
        QSignalSpy quitSpy(&controller, &AppController::quitApproved);
        controller.requestQuit();
        controller.resolveDestructiveAction(QStringLiteral("discard"));

        QCOMPARE(quitSpy.count(), 1);
        QVERIFY(!controller.recoveryDegraded());
        QVERIFY(QFileInfo(recoveryPath).isFile());
        ProjectRecoveryStore store(recoveryPath);
        ProjectRecoveryDiscardTombstone tombstone;
        QString error;
        QVERIFY2(store.loadDiscardTombstone(&tombstone, &error), qPrintable(error));
        QVERIFY(!tombstone.documentId.isEmpty());
        QVERIFY(tombstone.discardedThroughRevision > 0);
    }
    {
        AppController restarted(nullptr, recoveryPath);
        QVERIFY(!restarted.recoveryPending());
    }
    QVERIFY(!QFileInfo(recoveryPath).exists());
    QVERIFY(!QFileInfo(recoveryPath + QStringLiteral(".discard")).exists());
}

void TelemetryTests::continuesDiscardedNewAndOpenWhenRecoveryDeletionFails()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings;
    settings.clear();
    settings.sync();
    const QString recoveryPath = directory.filePath(QStringLiteral("recovery.json"));
    const QString projectPath = directory.filePath(QStringLiteral("open.fetproject"));
    QVERIFY(writeBytes(projectPath, QJsonDocument(testProject(3.0)).toJson()));
    ProjectRecoveryStore::Operations operations;
    operations.clearSnapshot = [](QString *error) {
        if (error) *error = QStringLiteral("injected recovery deletion failure");
        return false;
    };

    {
        AppController controller(nullptr, recoveryPath, operations);
        controller.setSyncOffset(8.0);
        QTRY_VERIFY(QFileInfo(recoveryPath).isFile());
        controller.requestNewProject();
        controller.resolveDestructiveAction(QStringLiteral("discard"));
        QCOMPARE(controller.syncOffset(), 0.0);
        QVERIFY(controller.projectPath().isEmpty());
        QVERIFY(QFileInfo(recoveryPath).isFile());
    }
    {
        AppController restarted(nullptr, recoveryPath);
        QVERIFY(!restarted.recoveryPending());
    }

    {
        AppController controller(nullptr, recoveryPath, operations);
        controller.setSyncOffset(9.0);
        QTRY_VERIFY(QFileInfo(recoveryPath).isFile());
        controller.requestOpenProject(QUrl::fromLocalFile(projectPath));
        controller.resolveDestructiveAction(QStringLiteral("discard"));
        QTRY_VERIFY(!controller.projectLoading());
        QCOMPARE(controller.syncOffset(), 3.0);
        QCOMPARE(controller.projectPath().toLocalFile(), QFileInfo(projectPath).canonicalFilePath());
        QVERIFY(QFileInfo(recoveryPath).isFile());
    }
    {
        AppController restarted(nullptr, recoveryPath);
        QVERIFY(!restarted.recoveryPending());
        QTRY_VERIFY(!restarted.projectLoading());
        QCOMPARE(restarted.projectPath().toLocalFile(), QFileInfo(projectPath).canonicalFilePath());
    }
}

void TelemetryTests::leavesRecoveryUntouchedWhenDiscardIsCancelled()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings;
    settings.clear();
    settings.sync();
    const QString recoveryPath = directory.filePath(QStringLiteral("recovery.json"));
    AppController controller(nullptr, recoveryPath);
    controller.setSyncOffset(8.0);
    QTRY_VERIFY(QFileInfo(recoveryPath).isFile());
    QSignalSpy quitSpy(&controller, &AppController::quitApproved);
    controller.requestQuit();
    controller.resolveDestructiveAction(QStringLiteral("cancel"));

    QCOMPARE(quitSpy.count(), 0);
    QVERIFY(QFileInfo(recoveryPath).isFile());
    QVERIFY(!QFileInfo(recoveryPath + QStringLiteral(".discard")).exists());
}

void TelemetryTests::preservesNewerAndDifferentRecoveryAfterDiscard()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings;
    settings.clear();
    settings.sync();
    const QString recoveryPath = directory.filePath(QStringLiteral("recovery.json"));
    ProjectRecoveryStore store(recoveryPath);
    QString error;
    QVERIFY2(store.writeDiscardTombstone({QStringLiteral("document-a"), 3}, &error), qPrintable(error));
    const auto projectFor = [](const QString &id) {
        QJsonObject project = testProject(8.0);
        project.insert(QStringLiteral("documentState"), QJsonObject{
            {QStringLiteral("id"), id},
            {QStringLiteral("savedRevision"), QStringLiteral("1")},
        });
        return project;
    };
    QVERIFY2(store.write({{}, QStringLiteral("document-a"), 4, 1,
                          QStringLiteral("2026-08-25T12:00:00.000Z"),
                          projectFor(QStringLiteral("document-a")), true}, &error), qPrintable(error));
    {
        AppController restarted(nullptr, recoveryPath);
        QVERIFY(restarted.recoveryPending());
    }
    QVERIFY2(store.write({{}, QStringLiteral("document-b"), 2, 1,
                          QStringLiteral("2026-08-25T12:00:01.000Z"),
                          projectFor(QStringLiteral("document-b")), true}, &error), qPrintable(error));
    {
        AppController restarted(nullptr, recoveryPath);
        QVERIFY(restarted.recoveryPending());
    }
}

void TelemetryTests::cancelsDiscardWhenTombstoneAndDeletionFail()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings;
    settings.clear();
    settings.sync();
    const QString recoveryPath = directory.filePath(QStringLiteral("recovery.json"));
    ProjectRecoveryStore::Operations operations;
    operations.clearSnapshot = [](QString *error) {
        if (error) *error = QStringLiteral("injected recovery deletion failure");
        return false;
    };
    operations.writeDiscardTombstone = [](QString *error) {
        if (error) *error = QStringLiteral("injected tombstone persistence failure");
        return false;
    };
    AppController controller(nullptr, recoveryPath, operations);
    controller.setSyncOffset(8.0);
    QTRY_VERIFY(QFileInfo(recoveryPath).isFile());
    QSignalSpy quitSpy(&controller, &AppController::quitApproved);
    controller.requestQuit();
    controller.resolveDestructiveAction(QStringLiteral("discard"));

    QCOMPARE(quitSpy.count(), 0);
    QCOMPARE(controller.statusText(), QStringLiteral("Could not discard recovery data; action cancelled."));
    QVERIFY(QFileInfo(recoveryPath).isFile());
    QVERIFY(!QFileInfo(recoveryPath + QStringLiteral(".discard")).exists());
    QVERIFY(!controller.recoveryDegraded());
}

void TelemetryTests::doesNotApplyDiscardTombstonesToLegacyRecovery()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings;
    settings.clear();
    settings.sync();
    const QString recoveryPath = directory.filePath(QStringLiteral("recovery.json"));
    ProjectRecoveryStore store(recoveryPath);
    QString error;
    QVERIFY2(store.writeDiscardTombstone({QStringLiteral("document-a"), 99}, &error), qPrintable(error));
    const QJsonObject legacy{
        {QStringLiteral("recoveryVersion"), 1},
        {QStringLiteral("dirty"), true},
        {QStringLiteral("revision"), QStringLiteral("2")},
        {QStringLiteral("lastSavedRevision"), QStringLiteral("1")},
        {QStringLiteral("project"), testProject(8.0)},
    };
    QVERIFY(writeBytes(recoveryPath, QJsonDocument(legacy).toJson()));

    AppController restarted(nullptr, recoveryPath);
    QVERIFY(restarted.recoveryPending());
}

void TelemetryTests::preservesRecoveryAcrossFailedSave()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings;
    settings.clear();
    settings.sync();
    const QString projectPath = directory.filePath(QStringLiteral("saved.fetproject"));
    const QString recoveryPath = directory.filePath(QStringLiteral("recovery.json"));
    const QByteArray original = QJsonDocument(testProject(1.0)).toJson();
    QVERIFY(writeBytes(projectPath, original));

    AppController controller(nullptr, recoveryPath);
    controller.requestOpenProject(QUrl::fromLocalFile(projectPath));
    QTRY_VERIFY(!controller.projectLoading());
    controller.setSyncOffset(8.0);
    QTRY_VERIFY(QFileInfo(recoveryPath).isFile());
    QVERIFY(!controller.saveProject(
        QUrl::fromLocalFile(directory.filePath(QStringLiteral("missing/project.fetproject")))));
    QVERIFY(controller.dirty());
    QVERIFY(QFileInfo(recoveryPath).isFile());
    QFile unchanged(projectPath);
    QVERIFY(unchanged.open(QIODevice::ReadOnly));
    QCOMPARE(unchanged.readAll(), original);
    unchanged.close(); // Do not hold the target open across Windows atomic replacement.
    QVERIFY(controller.saveCurrentProject());
    QVERIFY(!controller.dirty());
    QVERIFY(!QFileInfo(recoveryPath).exists());
}

void TelemetryTests::offersRecoveryWhenAnotherAppSavedTheSameRevision()
{
    // KAN-183: FlappedEar Telemetry saves the same document with its own
    // revision count. A save of ours at the same revision makes the snapshot
    // stale; theirs must not, or our unsaved edits would be dropped.
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings;
    settings.clear();
    settings.sync();
    const QString projectPath = directory.filePath(QStringLiteral("saved.fetproject"));
    const QString recoveryPath = directory.filePath(QStringLiteral("recovery.json"));
    QVERIFY(writeBytes(projectPath, QJsonDocument(testProject(1.0)).toJson()));

    quint64 snapshotRevision = 0;
    {
        AppController controller(nullptr, recoveryPath);
        controller.requestOpenProject(QUrl::fromLocalFile(projectPath));
        QTRY_VERIFY(!controller.projectLoading());
        controller.setSyncOffset(3.0);
        QVERIFY(controller.saveCurrentProject());
        const auto saved = QJsonDocument::fromJson(readBytes(projectPath)).object();
        QVERIFY(!saved.value("documentState").toObject().value("saveId").toString().isEmpty());
        controller.setSyncOffset(8.0); // unsaved edit, kept only in recovery
        QTRY_VERIFY(QFileInfo(recoveryPath).isFile());
        snapshotRevision = QJsonDocument::fromJson(readBytes(recoveryPath)).object().value("revision").toVariant().toULongLong();
        QVERIFY(snapshotRevision > 0);
    }

    // Another application saves different content at the snapshot's revision.
    auto foreign = QJsonDocument::fromJson(readBytes(projectPath)).object();
    auto state = foreign.value("documentState").toObject();
    state.insert("savedRevision", QString::number(snapshotRevision));
    state.insert("saveId", QStringLiteral("telemetry-save"));
    foreign.insert("documentState", state);
    QVERIFY(writeBytes(projectPath, QJsonDocument(foreign).toJson()));
    {
        AppController restarted(nullptr, recoveryPath);
        QVERIFY(restarted.recoveryPending());
        QVERIFY(QFileInfo(recoveryPath).isFile());
    }

    // A stale classification deletes the snapshot, so each check below starts
    // from this copy.
    const QByteArray snapshot = readBytes(recoveryPath);

    // A legacy copy without a saveId keeps the old rule: the snapshot is stale.
    state.remove("saveId");
    foreign.insert("documentState", state);
    QVERIFY(writeBytes(projectPath, QJsonDocument(foreign).toJson()));
    {
        AppController legacy(nullptr, recoveryPath);
        QVERIFY(!legacy.recoveryPending());
    }

    // The same copy carrying our own saveId is our save: the snapshot is stale.
    QVERIFY(writeBytes(recoveryPath, snapshot));
    state.insert("saveId", settings.value("project/ownSaveId").toString());
    foreign.insert("documentState", state);
    QVERIFY(writeBytes(projectPath, QJsonDocument(foreign).toJson()));
    AppController ours(nullptr, recoveryPath);
    QVERIFY(!ours.recoveryPending());
}

void TelemetryTests::doesNotOfferStaleRecoveryAfterSuccessfulSaveCleanupFailure()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings;
    settings.clear();
    settings.sync();
    const QString projectPath = directory.filePath(QStringLiteral("saved.fetproject"));
    const QString recoveryDirectory = directory.filePath(QStringLiteral("recovery-state"));
    const QString recoveryPath = QDir(recoveryDirectory).filePath(QStringLiteral("recovery.json"));
    QVERIFY(QDir().mkpath(recoveryDirectory));
    QVERIFY(writeBytes(projectPath, QJsonDocument(testProject(1.0)).toJson()));
    ProjectRecoveryStore::Operations operations;
    operations.clearSnapshot = [](QString *error) {
        if (error) *error = QStringLiteral("injected recovery deletion failure");
        return false;
    };

    {
        AppController controller(nullptr, recoveryPath, operations);
        controller.requestOpenProject(QUrl::fromLocalFile(projectPath));
        QTRY_VERIFY(!controller.projectLoading());
        controller.setSyncOffset(8.0);
        QTRY_VERIFY(QFileInfo(recoveryPath).isFile());

        QVERIFY(controller.saveCurrentProject());
        QVERIFY(!controller.dirty());
        QVERIFY(!controller.recoveryDegraded());
        QVERIFY(QFileInfo(recoveryPath).isFile());
    }

    AppController restarted(nullptr, recoveryPath);
    QVERIFY(!restarted.recoveryPending());
    QTRY_VERIFY(!restarted.projectLoading());
    QCOMPARE(restarted.syncOffset(), 8.0);
    QVERIFY(!restarted.dirty());
    QVERIFY(!QFileInfo(recoveryPath).exists()); // Startup retry completed stale cleanup.
}

void TelemetryTests::classifiesVersionedRecoveryAgainstSavedAuthority()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings;
    settings.clear();
    settings.sync();
    const QString projectPath = directory.filePath(QStringLiteral("saved.fetproject"));
    const QString recoveryPath = directory.filePath(QStringLiteral("recovery.json"));

    {
        AppController controller(nullptr, recoveryPath);
        controller.setSyncOffset(1.0);
        QVERIFY(controller.saveProject(QUrl::fromLocalFile(projectPath)));
    }
    const QJsonObject saved = QJsonDocument::fromJson(readBytes(projectPath)).object();
    const QJsonObject state = saved.value(QStringLiteral("documentState")).toObject();
    const QString documentId = state.value(QStringLiteral("id")).toString();
    QVERIFY(!documentId.isEmpty());
    ProjectRecoveryStore store(recoveryPath);
    QString error;

    for (const quint64 revision : {quint64{1}, quint64{0}}) {
        const ProjectRecoverySnapshot stale{
            projectPath, documentId, revision, 1,
            QStringLiteral("2026-08-25T12:00:00.000Z"), saved, true};
        QVERIFY2(store.write(stale, &error), qPrintable(error));
        AppController restarted(nullptr, recoveryPath);
        QVERIFY(!restarted.recoveryPending());
        QTRY_VERIFY(!restarted.projectLoading());
        QCOMPARE(restarted.syncOffset(), 1.0);
    }

    QJsonObject newer = saved;
    QJsonObject sync = newer.value(QStringLiteral("sync")).toObject();
    sync.insert(QStringLiteral("offset"), 7.0);
    newer.insert(QStringLiteral("sync"), sync);
    const ProjectRecoverySnapshot valid{
        projectPath, documentId, 2, 1,
        QStringLiteral("2026-08-25T12:00:01.000Z"), newer, true};
    QVERIFY2(store.write(valid, &error), qPrintable(error));
    AppController restarted(nullptr, recoveryPath);
    QVERIFY(restarted.recoveryPending());
    restarted.resolveStartupRecovery(QStringLiteral("recover"));
    QTRY_VERIFY(!restarted.projectLoading());
    QVERIFY(restarted.dirty());
    QVERIFY(restarted.saveCurrentProject());
    const QJsonObject recoveredSaved = QJsonDocument::fromJson(readBytes(projectPath)).object();
    QVERIFY(!recoveredSaved.value(QStringLiteral("documentState")).toObject()
                 .value(QStringLiteral("id")).toString().isEmpty());
}

void TelemetryTests::keepsSaveAsRecoveryIdentityWithNewAndExistingProjects()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings;
    settings.clear();
    settings.sync();
    const QString projectA = directory.filePath(QStringLiteral("a.fetproject"));
    const QString projectB = directory.filePath(QStringLiteral("b.fetproject"));
    const QString recoveryDirectory = directory.filePath(QStringLiteral("recovery-state"));
    const QString recoveryPath = QDir(recoveryDirectory).filePath(QStringLiteral("recovery.json"));
    QVERIFY(QDir().mkpath(recoveryDirectory));
    QVERIFY(writeBytes(projectA, QJsonDocument(testProject(1.0)).toJson()));
    ProjectRecoveryStore::Operations operations;
    operations.clearSnapshot = [](QString *error) {
        if (error) *error = QStringLiteral("injected recovery deletion failure");
        return false;
    };

    {
        AppController controller(nullptr, recoveryPath, operations);
        controller.setSyncOffset(2.0); // New document -> Save As.
        QTRY_VERIFY(QFileInfo(recoveryPath).isFile());
        QVERIFY(controller.saveProject(QUrl::fromLocalFile(projectB)));
        QVERIFY(QFileInfo(recoveryPath).isFile());
    }
    {
        AppController restarted(nullptr, recoveryPath);
        QVERIFY(!restarted.recoveryPending());
        QTRY_VERIFY(!restarted.projectLoading());
        QCOMPARE(restarted.projectPath().toLocalFile(), QFileInfo(projectB).canonicalFilePath());
    }

    {
        AppController controller(nullptr, recoveryPath, operations);
        controller.requestOpenProject(QUrl::fromLocalFile(projectA));
        QTRY_VERIFY(!controller.projectLoading());
        controller.setSyncOffset(3.0); // Existing A -> Save As B.
        QTRY_VERIFY(QFileInfo(recoveryPath).isFile());
        QVERIFY(controller.saveProject(QUrl::fromLocalFile(projectB)));
        QVERIFY(QFileInfo(recoveryPath).isFile());
    }
    AppController restarted(nullptr, recoveryPath);
    QVERIFY(!restarted.recoveryPending());
    QTRY_VERIFY(!restarted.projectLoading());
    QCOMPARE(restarted.projectPath().toLocalFile(), QFileInfo(projectB).canonicalFilePath());
}

void TelemetryTests::rejectsInvalidVersionedRecoveryMetadata()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings;
    settings.clear();
    settings.sync();
    const QString projectPath = directory.filePath(QStringLiteral("saved.fetproject"));
    const QString recoveryPath = directory.filePath(QStringLiteral("recovery.json"));
    QVERIFY(writeBytes(projectPath, QJsonDocument(testProject(1.0)).toJson()));
    settings.setValue(QStringLiteral("project/path"), projectPath);
    settings.sync();
    QJsonObject invalid{
        {QStringLiteral("recoveryVersion"), 2},
        {QStringLiteral("dirty"), true},
        {QStringLiteral("revision"), QStringLiteral("not-a-revision")},
        {QStringLiteral("lastSavedRevision"), QStringLiteral("0")},
        {QStringLiteral("documentId"), QStringLiteral("identity")},
        {QStringLiteral("project"), testProject(9.0)},
    };
    QVERIFY(writeBytes(recoveryPath, QJsonDocument(invalid).toJson()));

    AppController restarted(nullptr, recoveryPath);
    QVERIFY(!restarted.recoveryPending());
    QTRY_VERIFY(!restarted.projectLoading());
    QCOMPARE(restarted.syncOffset(), 1.0);
}

void TelemetryTests::rejectsMismatchedVersionedRecoveryPayloadIdentity()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings;
    settings.clear();
    settings.sync();
    const QString projectPath = directory.filePath(QStringLiteral("saved.fetproject"));
    const QString recoveryPath = directory.filePath(QStringLiteral("recovery.json"));
    QJsonObject authority = testProject(1.0);
    authority.insert(QStringLiteral("documentState"), QJsonObject{
        {QStringLiteral("id"), QStringLiteral("document-a")},
        {QStringLiteral("savedRevision"), QStringLiteral("1")},
    });
    QVERIFY(writeBytes(projectPath, QJsonDocument(authority).toJson()));
    settings.setValue(QStringLiteral("project/path"), projectPath);
    settings.sync();
    QJsonObject mixedPayload = testProject(9.0);
    mixedPayload.insert(QStringLiteral("documentState"), QJsonObject{
        {QStringLiteral("id"), QStringLiteral("document-b")},
        {QStringLiteral("savedRevision"), QStringLiteral("1")},
    });
    ProjectRecoveryStore store(recoveryPath);
    QString error;
    const ProjectRecoverySnapshot mixed{
        projectPath, QStringLiteral("document-a"), 2, 1,
        QStringLiteral("2026-08-25T12:00:00.000Z"), mixedPayload, true};
    QVERIFY(!store.write(mixed, &error));
    QVERIFY(error.contains(QStringLiteral("metadata")));
    QVERIFY(!QFileInfo(recoveryPath).exists());

    AppController restarted(nullptr, recoveryPath);
    QVERIFY(!restarted.recoveryPending());
    QTRY_VERIFY(!restarted.projectLoading());
    QCOMPARE(restarted.syncOffset(), 1.0);
}

void TelemetryTests::doesNotTrustMalformedProjectAsRecoveryAuthority()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings;
    settings.clear();
    settings.sync();
    const QString projectPath = directory.filePath(QStringLiteral("saved.fetproject"));
    const QString recoveryPath = directory.filePath(QStringLiteral("recovery.json"));
    const QJsonObject malformedAuthority{
        {QStringLiteral("documentState"), QJsonObject{
            {QStringLiteral("id"), QStringLiteral("document-a")},
            {QStringLiteral("savedRevision"), QStringLiteral("2")},
        }},
    };
    QVERIFY(writeBytes(projectPath, QJsonDocument(malformedAuthority).toJson()));
    settings.setValue(QStringLiteral("project/path"), projectPath);
    settings.sync();
    QJsonObject recoveryProject = testProject(9.0);
    recoveryProject.insert(QStringLiteral("documentState"), QJsonObject{
        {QStringLiteral("id"), QStringLiteral("document-a")},
        {QStringLiteral("savedRevision"), QStringLiteral("1")},
    });
    ProjectRecoveryStore store(recoveryPath);
    QString error;
    const ProjectRecoverySnapshot recovery{
        projectPath, QStringLiteral("document-a"), 2, 1,
        QStringLiteral("2026-08-25T12:00:00.000Z"), recoveryProject, true};
    QVERIFY2(store.write(recovery, &error), qPrintable(error));

    AppController restarted(nullptr, recoveryPath);
    QVERIFY(restarted.recoveryPending());
}

void TelemetryTests::recoversLegacyRecoverySnapshotConservatively()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings;
    settings.clear();
    settings.sync();
    const QString projectPath = directory.filePath(QStringLiteral("saved.fetproject"));
    const QString recoveryPath = directory.filePath(QStringLiteral("recovery.json"));
    QVERIFY(writeBytes(projectPath, QJsonDocument(testProject(1.0)).toJson()));
    settings.setValue(QStringLiteral("project/path"), projectPath);
    settings.sync();
    QJsonObject recovered = testProject(9.0);
    const QJsonObject legacy{
        {QStringLiteral("recoveryVersion"), 1},
        {QStringLiteral("dirty"), true},
        {QStringLiteral("originalProjectPath"), projectPath},
        {QStringLiteral("revision"), QStringLiteral("2")},
        {QStringLiteral("lastSavedRevision"), QStringLiteral("1")},
        {QStringLiteral("project"), recovered},
    };
    QVERIFY(writeBytes(recoveryPath, QJsonDocument(legacy).toJson()));

    AppController restarted(nullptr, recoveryPath);
    QVERIFY(restarted.recoveryPending());
    restarted.resolveStartupRecovery(QStringLiteral("recover"));
    QTRY_VERIFY(!restarted.projectLoading());
    QVERIFY(restarted.dirty());
    QVERIFY(restarted.saveCurrentProject());
    const QJsonObject saved = QJsonDocument::fromJson(readBytes(projectPath)).object();
    QVERIFY(!saved.value(QStringLiteral("documentState")).toObject()
                 .value(QStringLiteral("id")).toString().isEmpty());
}

void TelemetryTests::preservesEditsAfterDocumentFirstProjectOpen()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings;
    settings.clear();
    settings.sync();
    const QString projectPath = directory.filePath(QStringLiteral("delayed.fetproject"));
    const QString recoveryPath = directory.filePath(QStringLiteral("recovery.json"));
    QVERIFY(writeBytes(projectPath, QJsonDocument(testProject(2.0)).toJson()));

    AppController controller(nullptr, recoveryPath);
    controller.requestOpenProject(QUrl::fromLocalFile(projectPath));
    QTRY_VERIFY(!controller.projectLoading());
    QCOMPARE(controller.syncOffset(), 2.0);
    QCOMPARE(controller.projectPath().toLocalFile(), QFileInfo(projectPath).canonicalFilePath());
    controller.setSyncOffset(9.0);
    QCOMPARE(controller.syncOffset(), 9.0);
    QVERIFY(controller.dirty());
    QVERIFY(controller.projectLoadError().isEmpty());
}

void TelemetryTests::tracksExportStageElapsedTime()
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

void TelemetryTests::boundsVerboseDiagnosticStorage()
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
}

void TelemetryTests::persistsExportDiagnosticsAndRetainsKnownLogs()
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

void TelemetryTests::formatsStageAFailureDiagnostics()
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

void TelemetryTests::throttlesDiagnosticHeartbeats()
{
    DiagnosticHeartbeat heartbeat(500);
    QVERIFY(!heartbeat.shouldEmit(0));
    QVERIFY(!heartbeat.shouldEmit(499));
    QVERIFY(heartbeat.shouldEmit(500));
    QVERIFY(!heartbeat.shouldEmit(999));
    QVERIFY(heartbeat.shouldEmit(1'000));
}

void TelemetryTests::tracksValidationSubstepStages()
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

void TelemetryTests::detectsHevcEncoders()
{
    const QString output = " V....D hevc_videotoolbox Apple VideoToolbox\n V....D libx265 x265\n";
    const QList<EncoderCapability> encoders = EncoderDetector::parseEncoders(output);
    QCOMPARE(encoders.size(), 2);
    QCOMPARE(encoders[0].id, QString("hevc_videotoolbox"));
    QVERIFY(encoders[0].hardware);
    QCOMPARE(EncoderDetector::preferredHevcEncoder(encoders), QString("hevc_videotoolbox"));
}

void TelemetryTests::cancelsEncoderDiscovery()
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

void TelemetryTests::calculatesTimestampDrivenExportFrames()
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

void TelemetryTests::checksCompositionFiltersBeforeRendering()
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

void TelemetryTests::preservesFramesWithPositiveSourcePts_data()
{
    QTest::addColumn<int>("first"); QTest::addColumn<int>("last");
    QTest::newRow("full") << 0 << 299;
    QTest::newRow("early-range") << 90 << 179;
    QTest::newRow("seek-range") << 210 << 299;
}

void TelemetryTests::preservesFramesWithPositiveSourcePts()
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

void TelemetryTests::plansBoundedStageBSourceAccess()
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

void TelemetryTests::resolvesExplicitExportFormats()
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

void TelemetryTests::derivesSourceDrivenExportProfiles()
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

void TelemetryTests::rejectsUnsupportedExportDisplayTransforms()
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

void TelemetryTests::validatesHighResolutionCapabilitiesAndCache()
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

void TelemetryTests::rendersCanvasWidgetsInFirstOffscreenFrames_data()
{
    QTest::addColumn<QString>("widgetType");
    QTest::newRow("retroTachometer") << QStringLiteral("retroTachometer");
}

void TelemetryTests::rendersCanvasWidgetsInFirstOffscreenFrames()
{
    QFETCH(QString, widgetType);

    TelemetrySession session = speedSession(0.0, 2.0, 0.0);
    session.aliases.insert(QStringLiteral("rpm"), QStringLiteral("speed"));
    WidgetModel widgets;
    const int index = widgets.addWidget(widgetType);
    QVERIFY(index >= 0);
    widgets.moveWidget(index, 0.1, 0.1);
    widgets.resizeWidget(index, 0.8, 0.8);
    widgets.setSetting(index, QStringLiteral("showBackground"), false);
    widgets.setSetting(index, QStringLiteral("showBorder"), false);
    widgets.setSetting(index, QStringLiteral("padding"), 0);
    widgets.setSetting(index, QStringLiteral("source"), QStringLiteral("speed"));
    widgets.setSetting(index, QStringLiteral("rpmSource"), QStringLiteral("speed"));
    widgets.setSetting(index, QStringLiteral("speedSource"), QStringLiteral("speed"));
    widgets.setSetting(index, QStringLiteral("gearSource"), QStringLiteral("speed"));
    widgets.setSetting(index, QStringLiteral("throttleSource"), QStringLiteral("speed"));
    widgets.setSetting(index, QStringLiteral("brakeSource"), QStringLiteral("speed"));
    widgets.setSetting(index, QStringLiteral("textColor"), QStringLiteral("#010101"));
    widgets.setSetting(index, QStringLiteral("secondaryTextColor"), QStringLiteral("#010101"));
    for (const QString &setting : {
             QStringLiteral("accentColor"), QStringLiteral("trackColor"),
             QStringLiteral("tickColor"), QStringLiteral("needleColor"),
             QStringLiteral("panelColor"), QStringLiteral("dialColor"),
             QStringLiteral("warningColor"), QStringLiteral("rimColor"),
             QStringLiteral("lowColor"), QStringLiteral("midColor"),
             QStringLiteral("highColor"), QStringLiteral("emptyColor"),
             QStringLiteral("throttleColor"), QStringLiteral("brakeColor"),
             QStringLiteral("brakeActiveColor")}) {
        widgets.setSetting(index, setting, QStringLiteral("#ff00ff"));
    }
    widgets.setSetting(index, QStringLiteral("panelOpacity"), 1.0);

    TelemetryFrameRenderer renderer;
    QVERIFY2(renderer.initialize(
                 &widgets, &session, nullptr, SyncTransform{}, QSize(640, 480)),
             qPrintable(renderer.errorString()));
    const auto signaturePixelCount = [](const QImage &image) {
        qsizetype count = 0;
        for (int y = 0; y < image.height(); ++y) {
            for (int x = 0; x < image.width(); ++x) {
                const QColor pixel = image.pixelColor(x, y);
                if (pixel.alpha() >= 40 && pixel.red() >= 120 && pixel.blue() >= 120
                    && pixel.green() <= 40) {
                    ++count;
                }
            }
        }
        return count;
    };
    QList<QImage> frames;
    for (const auto &[label, time] : {
             std::pair{QStringLiteral("first non-zero-range"), 1.0},
             std::pair{QStringLiteral("same-time reference"), 1.0},
             std::pair{QStringLiteral("later"), 1.8}}) {
        const QImage image = renderer.renderFrame(time);
        QVERIFY2(!image.isNull(), qPrintable(renderer.errorString()));
        const qsizetype pixels = signaturePixelCount(image);
        QVERIFY2(pixels >= 25,
                 qPrintable(QStringLiteral(
                     "%1 %2 offscreen frame contains only %3 Canvas signature pixels")
                                .arg(widgetType, label).arg(pixels)));
        frames.append(image);
    }
    QCOMPARE(frames[0], frames[1]);
}

namespace {
QStringList *capturedLayoutWarnings = nullptr;
void captureLayoutWarning(QtMsgType, const QMessageLogContext &, const QString &message)
{
    if (capturedLayoutWarnings && message.contains(QStringLiteral("recursive rearrange"))) capturedLayoutWarnings->append(message);
}
} // namespace

void TelemetryTests::rendersPedalsWithoutLayoutLoops()
{
    // KAN-140: the pedals widget sized its rows from their own layout, and
    // Qt Quick Layouts aborted a recursive rearrange on every template apply.
    TelemetrySession session = speedSession(0.0, 2.0, 0.0);
    WidgetModel widgets;
    const int pedals = widgets.addWidget(QStringLiteral("pedals"));
    QVERIFY(pedals >= 0);
    QStringList warnings;
    capturedLayoutWarnings = &warnings;
    const auto previous = qInstallMessageHandler(captureLayoutWarning);
    {
        TelemetryFrameRenderer renderer;
        QVERIFY2(renderer.initialize(&widgets, &session, nullptr, SyncTransform{}, QSize(1280, 720)), qPrintable(renderer.errorString()));
        for (const double size : {0.25, 0.4, 0.15}) {
            widgets.resizeWidget(pedals, size, size / 2);
            QVERIFY(!renderer.renderFrame(1.0).isNull());
        }
        for (const QString id : {"motorsport-broadcast-smoke"}) {
            QVERIFY(widgets.applyTemplate(id));
            QVERIFY(!renderer.renderFrame(1.0).isNull());
        }
    }
    qInstallMessageHandler(previous);
    capturedLayoutWarnings = nullptr;
    QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
}

void TelemetryTests::normalizesDesignedWidgetElements()
{
    // KAN-191: a designed widget's elements are bounded and normalised on every path in.
    WidgetModel model;
    const int index = model.addWidget(QStringLiteral("designed"));
    QVERIFY(index >= 0);
    QVariantList defaults = model.widget(index).value("settings").toMap().value("elements").toList();
    QCOMPARE(defaults.size(), 4);
    QCOMPARE(defaults[1].toMap().value("kind").toString(), QStringLiteral("value"));
    QCOMPARE(defaults[1].toMap().value("source").toString(), QStringLiteral("speed"));
    QVERIFY(defaults[1].toMap().contains("fallbackText"));

    const QVariantList raw{
        QVariantMap{{"kind", "text"}, {"id", "same"}, {"x", 0.9}, {"w", 0.5}, {"h", -3},
                    {"color", "not a colour"}, {"fontScale", 99}, {"align", "justify"}, {"unknown", 1}},
        QVariantMap{{"kind", "rocket"}, {"id", "dropped"}},
        QVariantMap{{"kind", "bar"}, {"id", "same"}, {"minValue", 10}, {"maxValue", 5},
                    {"orientation", "vertical"}, {"decimals", 42}, {"multiplier", qQNaN()}},
        QVariantMap{{"kind", "lap"}, {"lapField", "lastDelta"}, {"text", QString(500, QLatin1Char('x'))}},
        QStringLiteral("not an element"),
    };
    model.setElements(index, raw);
    const QVariantList elements = model.widget(index).value("settings").toMap().value("elements").toList();
    QCOMPARE(elements.size(), 3);
    const QVariantMap text = elements[0].toMap();
    QCOMPARE(text.value("id").toString(), QStringLiteral("same"));
    QCOMPARE(text.value("w").toDouble(), 0.5);
    QCOMPARE(text.value("h").toDouble(), 0.01);
    QCOMPARE(text.value("x").toDouble(), 0.5);
    QCOMPARE(text.value("color").toString(), QStringLiteral("#f2f5f7"));
    QCOMPARE(text.value("fontScale").toDouble(), 2.0);
    QCOMPARE(text.value("align").toString(), QStringLiteral("center"));
    QVERIFY(!text.contains("unknown"));
    const QVariantMap bar = elements[1].toMap();
    QVERIFY(bar.value("id").toString() != QStringLiteral("same"));
    QCOMPARE(bar.value("minValue").toDouble(), 0.0);
    QCOMPARE(bar.value("maxValue").toDouble(), 100.0);
    QCOMPARE(bar.value("orientation").toString(), QStringLiteral("vertical"));
    QCOMPARE(bar.value("decimals").toInt(), 6);
    QCOMPARE(bar.value("multiplier").toDouble(), 1.0);
    QCOMPARE(elements[2].toMap().value("lapField").toString(), QStringLiteral("lastDelta"));
    QCOMPARE(elements[2].toMap().value("text").toString().size(), 200);

    // The same rules apply through setSetting, and the elements survive a project round trip.
    model.setSetting(index, QStringLiteral("elements"), raw);
    QCOMPARE(model.widget(index).value("settings").toMap().value("elements").toList(), elements);
    WidgetModel restored;
    QVERIFY(restored.fromJson(model.toJson()));
    QCOMPARE(restored.widget(index).value("settings").toMap().value("elements").toList(), elements);

    // Live edits keep at most 64 elements; a document with more is rejected, not truncated.
    QVariantList many;
    for (int element = 0; element < 70; ++element)
        many.append(QVariantMap{{"kind", "shape"}});
    model.setElements(index, many);
    QCOMPARE(model.widget(index).value("settings").toMap().value("elements").toList().size(), 64);
    QJsonArray document = model.toJson();
    QJsonObject widget = document[index].toObject();
    QJsonObject settings = widget.value("settings").toObject();
    settings.insert("elements", QJsonArray::fromVariantList(many));
    widget.insert("settings", settings);
    document[index] = widget;
    QVERIFY(!restored.fromJson(document));
}

void TelemetryTests::persistsWidgetLibrary()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString libraryPath = directory.filePath("widget-library.json");
    const bool hadOverride = qEnvironmentVariableIsSet("FLAPPEDEAR_WIDGET_LIBRARY");
    const QByteArray previousOverride = qgetenv("FLAPPEDEAR_WIDGET_LIBRARY");
    qputenv("FLAPPEDEAR_WIDGET_LIBRARY", libraryPath.toUtf8());
    const auto restoreEnvironment = qScopeGuard([hadOverride, previousOverride] {
        if (hadOverride) qputenv("FLAPPEDEAR_WIDGET_LIBRARY", previousOverride);
        else qunsetenv("FLAPPEDEAR_WIDGET_LIBRARY");
    });

    WidgetModel source;
    QVERIFY(source.libraryWidgets().isEmpty());
    const int designed = source.addWidget(QStringLiteral("designed"));
    source.resizeWidget(designed, 0.3, 0.2);
    source.setElements(designed, QVariantList{QVariantMap{{"kind", "lap"}, {"id", "lap"}, {"lapField", "best"}}});
    source.moveWidget(designed, 0.5, 0.5);
    QSignalSpy changed(&source, &WidgetModel::libraryWidgetsChanged);
    const QString designedId = source.saveWidgetToLibrary(designed, QStringLiteral("  Best lap card  "));
    QVERIFY(!designedId.isEmpty());
    QCOMPARE(changed.count(), 1);
    QCOMPARE(source.widget(designed).value("settings").toMap().value("libraryId").toString(), designedId);
    QVERIFY(source.saveWidgetToLibrary(designed, QStringLiteral("   ")).isEmpty());
    const int speed = source.addWidget(QStringLiteral("speed"));
    source.setSetting(speed, QStringLiteral("textColor"), QStringLiteral("#123456"));
    const QString speedId = source.saveWidgetToLibrary(speed, QStringLiteral("Blue speed"));
    QVERIFY(!speedId.isEmpty());

    // A new model (a later app session) reads the library back.
    WidgetModel restored;
    const QVariantList library = restored.libraryWidgets();
    QCOMPARE(library.size(), 2);
    QCOMPARE(library[0].toMap().value("name").toString(), QStringLiteral("Best lap card"));
    QCOMPARE(library[0].toMap().value("type").toString(), QStringLiteral("designed"));
    QCOMPARE(library[0].toMap().value("elementCount").toInt(), 1);
    const int added = restored.addLibraryWidget(designedId);
    QCOMPARE(added, 0);
    const QVariantMap addedWidget = restored.widget(added);
    QCOMPARE(addedWidget.value("width").toDouble(), 0.3);
    QCOMPARE(addedWidget.value("height").toDouble(), 0.2);
    QCOMPARE(addedWidget.value("settings").toMap().value("name").toString(), QStringLiteral("Best lap card"));
    QCOMPARE(addedWidget.value("settings").toMap().value("elements").toList().constFirst().toMap()
                 .value("lapField").toString(), QStringLiteral("best"));
    const int addedSpeed = restored.addLibraryWidget(speedId);
    QCOMPARE(restored.widget(addedSpeed).value("settings").toMap().value("textColor").toString(),
             QStringLiteral("#123456"));
    QCOMPARE(restored.addLibraryWidget(QStringLiteral("missing")), -1);

    // Update in place, rename, export, import, delete.
    restored.setElements(added, QVariantList{QVariantMap{{"kind", "shape"}}, QVariantMap{{"kind", "text"}}});
    QVERIFY(restored.updateLibraryWidget(designedId, added));
    QCOMPARE(restored.libraryWidgets().size(), 2);
    QCOMPARE(restored.libraryWidgets()[0].toMap().value("elementCount").toInt(), 2);
    QVERIFY(restored.renameLibraryWidget(designedId, QStringLiteral("Card")));
    QVERIFY(!restored.renameLibraryWidget(designedId, QStringLiteral(" ")));
    const QUrl exported = QUrl::fromLocalFile(directory.filePath("card"));
    QVERIFY(restored.exportLibraryWidget(designedId, exported));
    QVERIFY(QFileInfo::exists(directory.filePath("card.fetwidget")));
    const QString importedId = restored.importLibraryWidget(QUrl::fromLocalFile(directory.filePath("card.fetwidget")));
    QVERIFY(!importedId.isEmpty());
    QVERIFY(importedId != designedId);
    QCOMPARE(restored.libraryWidgets().size(), 3);
    QCOMPARE(restored.libraryWidgets()[2].toMap().value("name").toString(), QStringLiteral("Card"));
    QVERIFY(restored.deleteLibraryWidget(designedId));
    QVERIFY(!restored.deleteLibraryWidget(designedId));
    WidgetModel reread;
    QCOMPARE(reread.libraryWidgets().size(), 2);
    QCOMPARE(reread.libraryWidgets()[1].toMap().value("id").toString(), importedId);
}

void TelemetryTests::rejectsMalformedWidgetLibraries()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString libraryPath = directory.filePath("widget-library.json");
    const bool hadOverride = qEnvironmentVariableIsSet("FLAPPEDEAR_WIDGET_LIBRARY");
    const QByteArray previousOverride = qgetenv("FLAPPEDEAR_WIDGET_LIBRARY");
    qputenv("FLAPPEDEAR_WIDGET_LIBRARY", libraryPath.toUtf8());
    const auto restoreEnvironment = qScopeGuard([hadOverride, previousOverride] {
        if (hadOverride) qputenv("FLAPPEDEAR_WIDGET_LIBRARY", previousOverride);
        else qunsetenv("FLAPPEDEAR_WIDGET_LIBRARY");
    });
    const auto write = [](const QString &path, const QByteArray &bytes) {
        QFile file(path);
        return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(bytes) == bytes.size();
    };

    // An unreadable library is reported and preserved: nothing overwrites it.
    const QByteArray corrupt = "{\"schemaVersion\": 1, \"widgets\": [{\"id\": \"a\", \"name\": \"A\", \"type\": \"warpDrive\", \"settings\": {}}]}";
    QVERIFY(write(libraryPath, corrupt));
    WidgetModel model;
    QVERIFY(model.libraryWidgets().isEmpty());
    QVERIFY(!model.libraryError().isEmpty());
    const int widget = model.addWidget(QStringLiteral("designed"));
    QVERIFY(model.saveWidgetToLibrary(widget, QStringLiteral("Mine")).isEmpty());
    QFile preserved(libraryPath);
    QVERIFY(preserved.open(QIODevice::ReadOnly));
    QCOMPARE(preserved.readAll(), corrupt);
    preserved.close();

    QVERIFY(write(libraryPath, "{\"schemaVersion\": 2, \"widgets\": []}"));
    model.reloadLibrary();
    QVERIFY(!model.libraryError().isEmpty());

    QVERIFY(QFile::remove(libraryPath));
    model.reloadLibrary();
    QVERIFY(model.libraryError().isEmpty());

    // Imports: wrong version, unknown type, too many elements, oversize file.
    const QString package = directory.filePath("bad.fetwidget");
    QVERIFY(write(package, "{\"flappedEarWidgetVersion\": 9, \"widget\": {\"name\": \"A\", \"type\": \"designed\", \"settings\": {}}}"));
    QVERIFY(model.importLibraryWidget(QUrl::fromLocalFile(package)).isEmpty());
    QVERIFY(write(package, "{\"flappedEarWidgetVersion\": 1, \"widget\": {\"name\": \"A\", \"type\": \"warpDrive\", \"settings\": {}}}"));
    QVERIFY(model.importLibraryWidget(QUrl::fromLocalFile(package)).isEmpty());
    QJsonArray tooMany;
    for (int element = 0; element < 65; ++element) tooMany.append(QJsonObject{{"kind", "shape"}});
    QJsonObject entry{{"name", "A"}, {"type", "designed"}, {"settings", QJsonObject{{"elements", tooMany}}}};
    QVERIFY(write(package, QJsonDocument(QJsonObject{{"flappedEarWidgetVersion", 1}, {"widget", entry}}).toJson()));
    QVERIFY(model.importLibraryWidget(QUrl::fromLocalFile(package)).isEmpty());
    QVERIFY(write(package, QByteArray(static_cast<qsizetype>(ProjectLimits::libraryWidgetBytes) + 1, ' ')));
    QVERIFY(model.importLibraryWidget(QUrl::fromLocalFile(package)).isEmpty());
    QVERIFY(model.libraryWidgets().isEmpty());
    QVERIFY(!QFileInfo::exists(libraryPath));
}

void TelemetryTests::rendersDesignedWidgetInExportScene()
{
    // KAN-191: the export renderer draws a designed widget's elements, with live data.
    TelemetrySession session = speedSession(0.0, 2.0, 0.0);
    WidgetModel widgets;
    const int index = widgets.addWidget(QStringLiteral("designed"));
    QVERIFY(index >= 0);
    widgets.moveWidget(index, 0.0, 0.0);
    widgets.resizeWidget(index, 1.0, 1.0);
    widgets.setSetting(index, QStringLiteral("showBackground"), false);
    widgets.setSetting(index, QStringLiteral("showBorder"), false);
    widgets.setElements(index, QVariantList{
        QVariantMap{{"kind", "bar"}, {"source", "speed"}, {"minValue", 40}, {"maxValue", 80},
                    {"x", 0.0}, {"y", 0.0}, {"w", 1.0}, {"h", 0.4},
                    {"fillColor", "#ff00ff"}, {"trackColor", "#000000"}, {"opacity", 1.0}},
        QVariantMap{{"kind", "shape"}, {"x", 0.0}, {"y", 0.5}, {"w", 0.25}, {"h", 0.25},
                    {"fillColor", "#00ff00"}},
        QVariantMap{{"kind", "value"}, {"source", "speed"}, {"x", 0.5}, {"y", 0.5}, {"w", 0.5},
                    {"h", 0.5}, {"color", "#ffffff"}},
        QVariantMap{{"kind", "shape"}, {"visible", false}, {"x", 0.25}, {"y", 0.5}, {"w", 0.25},
                    {"h", 0.25}, {"fillColor", "#00ff00"}},
    });
    TelemetryFrameRenderer renderer;
    QVERIFY2(renderer.initialize(&widgets, &session, nullptr, SyncTransform{}, QSize(400, 300)),
             qPrintable(renderer.errorString()));
    const auto count = [](const QImage &image, const QRect &area, const auto &match) {
        qsizetype pixels = 0;
        for (int y = area.top(); y <= area.bottom(); ++y)
            for (int x = area.left(); x <= area.right(); ++x)
                if (match(image.pixelColor(x, y))) ++pixels;
        return pixels;
    };
    const auto magenta = [](const QColor &pixel) {
        return pixel.alpha() > 200 && pixel.red() > 200 && pixel.blue() > 200 && pixel.green() < 60;
    };
    const auto green = [](const QColor &pixel) {
        return pixel.alpha() > 200 && pixel.green() > 200 && pixel.red() < 60 && pixel.blue() < 60;
    };
    const auto white = [](const QColor &pixel) { return pixel.alpha() > 200 && pixel.lightness() > 200; };
    const QImage early = renderer.renderFrame(0.2);
    const QImage late = renderer.renderFrame(1.8);
    QVERIFY2(!early.isNull() && !late.isNull(), qPrintable(renderer.errorString()));
    const QRect barArea(0, 0, 400, 120);
    const qsizetype earlyFill = count(early, barArea, magenta);
    const qsizetype lateFill = count(late, barArea, magenta);
    QVERIFY2(earlyFill > 4000, qPrintable(QString::number(earlyFill)));
    QVERIFY2(lateFill > earlyFill + 4000, qPrintable(QStringLiteral("%1 -> %2").arg(earlyFill).arg(lateFill)));
    QVERIFY(count(late, QRect(10, 160, 80, 50), green) > 3000);
    QCOMPARE(count(late, QRect(110, 160, 80, 50), green), qsizetype(0));
    QVERIFY(count(late, QRect(200, 150, 200, 150), white) > 100);
}

void TelemetryTests::editsDesignedWidgetInWidgetEditor()
{
    // KAN-191: the widget editor edits a designed widget live; Undo and Cancel restore it,
    // and the editor owns the Delete key while it is open.
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const bool hadOverride = qEnvironmentVariableIsSet("FLAPPEDEAR_WIDGET_LIBRARY");
    const QByteArray previousOverride = qgetenv("FLAPPEDEAR_WIDGET_LIBRARY");
    qputenv("FLAPPEDEAR_WIDGET_LIBRARY", directory.filePath("widget-library.json").toUtf8());
    const auto restoreEnvironment = qScopeGuard([hadOverride, previousOverride] {
        if (hadOverride) qputenv("FLAPPEDEAR_WIDGET_LIBRARY", previousOverride);
        else qunsetenv("FLAPPEDEAR_WIDGET_LIBRARY");
    });
    AppController controller(nullptr, directory.filePath("recovery.json"));
    auto *model = controller.widgetModel();
    const int widgetCount = model->count();
    const int designed = model->addWidget(QStringLiteral("designed"));
    QVERIFY(designed >= 0);
    const QVariantList original = model->widget(designed).value("settings").toMap().value("elements").toList();

    QQmlEngine engine; engine.rootContext()->setContextProperty("appController", &controller);
    QSignalSpy warnings(&engine, &QQmlEngine::warnings);
    QQmlComponent component(&engine, QUrl::fromLocalFile(qmlSourcePath("Main.qml")));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    std::unique_ptr<QObject> object(component.create());
    auto *window = qobject_cast<QQuickWindow *>(object.get()); QVERIFY(window);
    window->show(); QVERIFY(QTest::qWaitForWindowExposed(window));
    QObject *editor = object->findChild<QObject *>(QStringLiteral("widgetEditor"));
    QVERIFY(editor);
    const auto elements = [&] { return model->widget(designed).value("settings").toMap().value("elements").toList(); };

    QVERIFY(QMetaObject::invokeMethod(editor, "openFor", Q_ARG(QVariant, designed)));
    QTRY_VERIFY(editor->property("visible").toBool());
    QCOMPARE(editor->property("selectedElement").toInt(), 0);
    QVERIFY(QMetaObject::invokeMethod(editor, "addElement", Q_ARG(QVariant, QStringLiteral("lap"))));
    QCOMPARE(elements().size(), original.size() + 1);
    QCOMPARE(elements().constLast().toMap().value("kind").toString(), QStringLiteral("lap"));
    QCOMPARE(editor->property("selectedElement").toInt(), original.size());
    QVERIFY(QMetaObject::invokeMethod(editor, "setField", Q_ARG(QVariant, QStringLiteral("lapField")),
                                      Q_ARG(QVariant, QStringLiteral("best"))));
    QCOMPARE(elements().constLast().toMap().value("lapField").toString(), QStringLiteral("best"));
    QVERIFY(QMetaObject::invokeMethod(editor, "nudge", Q_ARG(QVariant, 0.05), Q_ARG(QVariant, 0.0)));
    QVERIFY(std::abs(elements().constLast().toMap().value("x").toDouble() - 0.15) < 1e-9);
    QVERIFY(QMetaObject::invokeMethod(editor, "undo"));
    QVERIFY(std::abs(elements().constLast().toMap().value("x").toDouble() - 0.1) < 1e-9);
    QVERIFY(QMetaObject::invokeMethod(editor, "redo"));
    QVERIFY(std::abs(elements().constLast().toMap().value("x").toDouble() - 0.15) < 1e-9);

    // Delete removes the selected element, never the widget behind the editor.
    window->setProperty("selectedWidgetIndex", designed);
    window->requestActivate();
    QVERIFY(QTest::qWaitForWindowActive(window));
    QTest::keyClick(window, Qt::Key_Delete);
    QTRY_COMPARE(elements().size(), original.size());
    QCOMPARE(model->count(), widgetCount + 1);

    const QString review = qEnvironmentVariable("FLAPPEDEAR_LAYOUT_REVIEW_DIR");
    if (!review.isEmpty()) {
        QTest::qWait(300);
        static_cast<void>(window->grabWindow().save(QDir(review).filePath("widget-editor.png")));
    }

    QVERIFY(QMetaObject::invokeMethod(editor, "cancelEditing"));
    QTRY_VERIFY(!editor->property("visible").toBool());
    QCOMPARE(elements(), original);

    // Widget › New Widget… adds a designed widget and opens it in the editor.
    QVERIFY(QMetaObject::invokeMethod(object.get(), "newDesignedWidget"));
    QCOMPARE(model->count(), widgetCount + 2);
    QCOMPARE(model->widget(widgetCount + 1).value("type").toString(), QStringLiteral("designed"));
    QTRY_VERIFY(editor->property("visible").toBool());
    QCOMPARE(editor->property("widgetIndex").toInt(), widgetCount + 1);
    QVERIFY(QMetaObject::invokeMethod(editor, "close"));
    for (const auto &arguments : warnings)
        for (const auto &error : arguments.first().value<QList<QQmlError>>())
            QVERIFY2(error.toString().contains("Cannot open: qrc:"), qPrintable(error.toString()));
}

void TelemetryTests::rendersTyresInExportScene()
{
    // KAN-132: the export renderer shares the tyres widget with the preview.
    // FL's sensor reports from 3 s: before that its cell shows dashes only.
    QString vbo = QStringLiteral("[header]\ncoordinate units = degrees\n[column names]\ntime latitude longitude "
        "tyre_temp_fl-canbus tyre_pressure_fl-canbus tyre_temp_rr-canbus tyre_pressure_rr-canbus\n[data]\n");
    for (int second = 0; second <= 10; ++second)
        vbo += QStringLiteral("%1 52.0001 21.0002 %2 45 230\n").arg(second).arg(second >= 3 ? "40 214" : "0 0");
    const TelemetrySession session = VboParser::parse(vbo);
    WidgetModel widgets;
    const int tyres = widgets.addWidget(QStringLiteral("tyres"));
    QVERIFY(tyres >= 0);
    widgets.moveWidget(tyres, 0.05, 0.05);
    widgets.resizeWidget(tyres, 0.40, 0.60);
    TelemetryFrameRenderer renderer;
    QVERIFY2(renderer.initialize(&widgets, &session, nullptr, SyncTransform{}, QSize(640, 480)),
             qPrintable(renderer.errorString()));
    // Bright text pixels in the widget's front-left quarter.
    const auto frontLeftInk = [](const QImage &image) {
        qsizetype count = 0;
        for (int y = static_cast<int>(0.05 * image.height()); y < static_cast<int>(0.35 * image.height()); ++y)
            for (int x = static_cast<int>(0.05 * image.width()); x < static_cast<int>(0.22 * image.width()); ++x) {
                const QColor pixel = image.pixelColor(x, y);
                if (pixel.alpha() > 200 && pixel.lightness() > 180) ++count;
            }
        return count;
    };
    const QImage before = renderer.renderFrame(1.0);
    const QImage after = renderer.renderFrame(5.0);
    QVERIFY2(!before.isNull() && !after.isNull(), qPrintable(renderer.errorString()));
    QVERIFY2(frontLeftInk(after) > frontLeftInk(before) + 50,
             qPrintable(QStringLiteral("front-left ink %1 -> %2").arg(frontLeftInk(before)).arg(frontLeftInk(after))));
    const QString shots = qEnvironmentVariable("FLAPPEDEAR_LAYOUT_REVIEW_DIR");
    if (!shots.isEmpty()) static_cast<void>(after.save(QDir(shots).filePath("tyres-export.png")));

    // Opt-in: a real day's first recording with tyre channels, mid-session.
    const QString day = qEnvironmentVariable("FLAPPEDEAR_REAL_DAY");
    if (day.isEmpty()) return;
    for (const auto &file : QDir(day).entryInfoList({"*.vbo"}, QDir::Files, QDir::Name)) {
        const TelemetrySession real = TelemetrySource::load(file.absoluteFilePath());
        const TyreChannelMap map = mapTyreChannels(real);
        if (!map.hasAny()) continue;
        const auto &timestamps = real.channels.value(map.pressure[0]).timestamps;
        const double time = timestamps[timestamps.size() / 2];
        TelemetryFrameRenderer realRenderer;
        QVERIFY2(realRenderer.initialize(&widgets, &real, nullptr, SyncTransform{}, QSize(1280, 720)),
                 qPrintable(realRenderer.errorString()));
        const QImage frame = realRenderer.renderFrame(time);
        QVERIFY(!frame.isNull());
        for (int corner = 0; corner < tyreCornerCount; ++corner) {
            const auto reading = tyreReadingAt(real, map, static_cast<TyreCorner>(corner), time);
            qInfo().noquote() << file.baseName().left(24) << tyreCornerCode(static_cast<TyreCorner>(corner))
                              << reading.temperatureCelsius.value_or(-1) << "°C" << reading.pressureBar.value_or(-1) << "bar";
            QVERIFY(reading.temperatureCelsius && reading.pressureBar);
        }
        if (!shots.isEmpty()) static_cast<void>(frame.save(QDir(shots).filePath("tyres-export-real.png")));
        break;
    }
}

void TelemetryTests::rendersLapTimeTileInProductionScene()
{
    TelemetrySession session = speedSession(0.0, 2.0, 0.0);
    WidgetModel widgets;
    const QStringList types{QStringLiteral("lapCurrent")};
    for (qsizetype index = 0; index < types.size(); ++index) {
        const int widget = widgets.addWidget(types[index]);
        QVERIFY2(widget >= 0, qPrintable(types[index]));
        const double x = 0.04 + 0.32 * static_cast<double>(index % 3);
        const double y = 0.14 + 0.42 * static_cast<double>(index / 3);
        widgets.moveWidget(widget, x, y);
        widgets.resizeWidget(widget, 0.28, 0.28);
    }

    TelemetryFrameRenderer renderer;
    QVERIFY2(renderer.initialize(
                 &widgets, &session, nullptr, SyncTransform{}, QSize(640, 480)),
             qPrintable(renderer.errorString()));
    const QImage image = renderer.renderFrame(1.0);
    QVERIFY2(!image.isNull(), qPrintable(renderer.errorString()));
    for (qsizetype index = 0; index < types.size(); ++index) {
        const int x = static_cast<int>((0.04 + 0.32 * static_cast<double>(index % 3) + 0.14) * image.width());
        const int y = static_cast<int>((0.14 + 0.42 * static_cast<double>(index / 3) + 0.14) * image.height());
        const QColor pixel = image.pixelColor(x, y);
        QVERIFY2(pixel.alpha() > 80,
                 qPrintable(QStringLiteral("%1 did not create an opaque lap time tile at %2,%3")
                                .arg(types[index]).arg(x).arg(y)));
    }
}

void TelemetryTests::preservesTenBitSdrThroughComposition()
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

void TelemetryTests::preservesTenBitFullRangeColorThroughVideoToolboxExport()
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

void TelemetryTests::preservesExactExportRateRationals()
{
    QVERIFY((MediaRational{24'000, 1'001}.isEquivalentTo({24'000, 1'001})));
    QVERIFY((MediaRational{30'000, 1'001}.isEquivalentTo({60'000, 2'002})));
    QVERIFY((MediaRational{60'000, 1'001}.isEquivalentTo({60'000, 1'001})));
    QVERIFY((!MediaRational{30'000, 1'001}.isEquivalentTo({30, 1})));
    QVERIFY(qAbs(ExportEngine::outputDuration(600, {30'000, 1'001}) - 20.02) < 0.0000001);
    QVERIFY(qAbs(ExportEngine::outputDuration(60, {60'000, 1'001}) - 1.001) < 0.0000001);
}

void TelemetryTests::floorsConvertedFrameCounts_data()
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

void TelemetryTests::floorsConvertedFrameCounts()
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

void TelemetryTests::schedulesFrameAddressedExportRangesExactly()
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

void TelemetryTests::boundsExportValidationAndWorkerDiagnostics()
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

void TelemetryTests::enforcesStrictTerminalFrameDeficitEvidence()
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

void TelemetryTests::derivesStablePreviewViewportAndLastFrameAdapter()
{
    QCOMPARE(PreviewPlayback::aspectFitViewport({1600, 900}, {3840, 2160}), QRect(0, 0, 1600, 900));
    QCOMPARE(PreviewPlayback::aspectFitViewport({1600, 1000}, {3840, 2160}), QRect(0, 50, 1600, 900));
    QCOMPARE(PreviewPlayback::aspectFitViewport({1000, 900}, {3840, 2160}), QRect(0, 169, 1000, 562));
    QCOMPARE(PreviewPlayback::lastFrame(1), std::optional<qint64>(0));
    QCOMPARE(PreviewPlayback::lastFrame(78'272), std::optional<qint64>(78'271));
    QCOMPARE(PreviewPlayback::framePositionMilliseconds(59, {60, 1}),
             std::optional<qint64>(983));
    QCOMPARE(PreviewPlayback::framePositionMilliseconds(59'940, {60'000, 1'001}),
             std::optional<qint64>(999'999));
    QCOMPARE(PreviewPlayback::firstTimelineFramePositionMilliseconds({60, 1}),
             std::optional<qint64>(17));
    QCOMPARE(PreviewPlayback::firstTimelineFramePositionMilliseconds({60'000, 1'001}),
             std::optional<qint64>(17));
    QCOMPARE(PreviewPlayback::clampPositionMilliseconds(2'000, 59, {60, 1}),
             std::optional<qint64>(983));
}

void TelemetryTests::exposesReactivePreviewMetadataToQml()
{
    const QMetaObject &metaObject = AppController::staticMetaObject;
    for (const char *propertyName : {"previewEndPositionMilliseconds", "previewEndTimecode"}) {
        const QMetaProperty property = metaObject.property(metaObject.indexOfProperty(propertyName));
        QVERIFY2(property.isValid(), propertyName);
        QVERIFY2(property.hasNotifySignal(), propertyName);
        QCOMPARE(property.notifySignal().name(), QByteArrayLiteral("previewMetadataChanged"));
    }
}

void TelemetryTests::preservesCfrCadenceForCommonRates()
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

void TelemetryTests::validatesQuantizedTemporaryOverlayCadence()
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

void TelemetryTests::preservesAbsoluteExportTimestamps()
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

void TelemetryTests::composesNonZeroExportRangeWithZeroBasedOutput()
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

void TelemetryTests::composes5994SixtySecondNonZeroRange()
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

void TelemetryTests::normalizesNonZeroStreamPtsForVideoAndAudio()
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

void TelemetryTests::convertsVfrInputToCfrWithFrameCorrectOverlay()
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

void TelemetryTests::estimatesExportProgress()
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

void TelemetryTests::parsesStructuredFfmpegProgress()
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

void TelemetryTests::calculatesEncodedOutputProgress()
{
    QCOMPARE(FfmpegProgressParser::overallPercent(0.0, 10.0), 0.0);
    QVERIFY(FfmpegProgressParser::overallPercent(2.5, 10.0)
            < FfmpegProgressParser::overallPercent(5.0, 10.0));
    QCOMPARE(FfmpegProgressParser::overallPercent(5.0, 10.0), 47.5);
    QCOMPARE(FfmpegProgressParser::overallPercent(12.0, 10.0), 95.0);
    QCOMPARE(FfmpegProgressParser::overallPercent(-1.0, 10.0), 0.0);
}

void TelemetryTests::preservesFrameIdentityThroughCompletedOverlayComposition()
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

void TelemetryTests::preservesPremultipliedAlphaThroughOverlayComposition()
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

void TelemetryTests::cancelsExportWorkerDuringTelemetryPreparation()
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

void TelemetryTests::decodesGps9Gpmf()
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
    QCOMPARE(speed.timestamps, QVector<double>({10.0, 10.5}));
    QVERIFY(qAbs(speed.values[0] - 4.5F) < 0.001F);
    QVERIFY(qAbs(speed.values[1] - 5.4F) < 0.001F);
}

void TelemetryTests::rejectsMalformedGpmf()
{
    QVERIFY_THROWS_EXCEPTION(
        std::runtime_error,
        (void) GoProTelemetrySource::decodeGpsPackets({{{"broken"}, 0.0, 1.0}}, 1.0));
}

void TelemetryTests::cancelsSlowGoProProbePromptly()
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

void TelemetryTests::boundsGoProProbeOutput()
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

void TelemetryTests::rejectsOutOfFileGpmfPackets()
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

void TelemetryTests::boundsGpmfDepthAndRecordCount()
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

void TelemetryTests::normalizesGpmfTimestamps()
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
    QCOMPARE(speed.timestamps, QVector<double>({1.0, 2.0}));
    QVERIFY(speed.timestamps[1] > speed.timestamps[0]);
    QVERIFY(qAbs(speed.values[0] - 7.2F) < 0.001F);
}

void TelemetryTests::boundsTimeTransforms_data()
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

void TelemetryTests::boundsTimeTransforms()
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

void TelemetryTests::exposesNoDataForOverflowingTransforms()
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
    controller.setTimeScale(std::numeric_limits<double>::max());
    controller.m_playbackTime = 2;
    QVERIFY(!controller.telemetryValue("speed").isValid());
    QCOMPARE(controller.valueText("speed"), QString("—"));
    QVERIFY(controller.telemetrySeries("speed", 2, 3, 100).isEmpty());
    controller.setTimeScale(1);
    QCOMPARE(controller.telemetryValue("speed").toDouble(), 20.0);
    QVERIFY(!controller.telemetrySeries("speed", 2, 3, 100).isEmpty());
}

void TelemetryTests::rejectsUnsafeSynchronizationInputs_data()
{
    QTest::addColumn<int>("fault");
    const char *names[] = {"empty", "mismatched", "nan-time", "infinite-time", "duplicate-time",
        "backward-time", "stalled-grid", "overflowing-difference", "sample-grid-budget",
        "offset-grid-budget", "pair-work-budget", "source-sample-budget"};
    for (int i = 0; i < 12; ++i) QTest::newRow(names[i]) << i;
}

void TelemetryTests::rejectsUnsafeSynchronizationInputs()
{
    QFETCH(int, fault);
    auto video = speedSession(0, 30, 0);
    auto telemetry = video;
    auto &a = video.channels["speed"];
    auto &b = telemetry.channels["speed"];
    if (fault == 0) { a.timestamps.clear(); a.values.clear(); }
    if (fault == 1) a.values.removeLast();
    if (fault == 2) a.timestamps[1] = std::numeric_limits<double>::quiet_NaN();
    if (fault == 3) a.timestamps[1] = std::numeric_limits<double>::infinity();
    if (fault == 4) a.timestamps[1] = a.timestamps[0];
    if (fault == 5) a.timestamps[1] = -1;
    if (fault >= 6) {
        a.timestamps.resize(20); a.values.resize(20);
        b.timestamps.resize(20); b.values.resize(20);
        double negative = -std::numeric_limits<double>::max();
        double positive = std::numeric_limits<double>::max() * .9;
        for (int i = 0; i < 20; ++i) {
            a.timestamps[i] = b.timestamps[i] = i;
            if (fault == 6) a.timestamps[i] = b.timestamps[i] = 1e16 + i * 2.0;
            if (fault == 7) {
                a.timestamps[i] = negative; b.timestamps[i] = positive;
                negative = std::nextafter(negative, 0.0);
                positive = std::nextafter(positive, std::numeric_limits<double>::infinity());
            }
            if (fault == 8) a.timestamps[i] = b.timestamps[i] = i * 100000.0;
            if (fault == 9) b.timestamps[i] = i * 100000.0;
            if (fault == 10) { a.timestamps[i] = i * 1000.0; b.timestamps[i] = i * 1500.0; }
        }
        if (fault == 11) {
            a.timestamps.resize(kMaximumSyncSignalSamples + 1);
            a.values.resize(kMaximumSyncSignalSamples + 1);
        }
    }
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

void TelemetryTests::preservesConfirmedTransformForAmbiguousResult()
{
    QTemporaryDir directory;
    AppController controller(nullptr, directory.filePath("recovery.json"));
    controller.setSyncOffset(7.25);
    controller.setTimeScale(1.003);
    auto video = speedSession(0, 30, 0);
    auto telemetry = speedSession(0, 35, 0);
    std::fill(video.channels["speed"].values.begin(), video.channels["speed"].values.end(), 42.0F);
    std::fill(telemetry.channels["speed"].values.begin(), telemetry.channels["speed"].values.end(), 42.0F);
    AppController::AutoSyncResult result;
    result.success = true;
    result.generation = controller.m_document.m_sourceGeneration;
    result.syncRevision = controller.m_syncRevision;
    result.candidate = TelemetrySyncEngine::synchronize(video, telemetry);
    QVERIFY(!shouldAutoApplySyncCandidate(result.candidate));
    QPromise<AppController::AutoSyncResult> promise;
    promise.start();
    controller.m_syncWatcher.setFuture(promise.future());
    QSignalSpy finished(&controller, &AppController::syncingChanged);
    promise.addResult(result); promise.finish();
    QTRY_VERIFY(!finished.isEmpty());
    QCOMPARE(controller.syncOffset(), 7.25);
    QCOMPARE(controller.timeScale(), 1.003);
    QVERIFY(!controller.syncCandidate().isEmpty());
}

void TelemetryTests::rejectsInvalidAutomaticCandidates()
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

void TelemetryTests::keepsExtremeFiniteSyncSignalsBounded()
{
    auto session = speedSession(0, 30, 0);
    auto &values = session.channels["speed"].values;
    for (qsizetype i = 0; i < values.size(); ++i)
        values[i] = (i % 2 ? 1.0F : -1.0F) * std::numeric_limits<float>::max();
    const auto candidate = TelemetrySyncEngine::synchronize(session, session);
    QVERIFY(std::isfinite(candidate.offset));
    QVERIFY(std::isfinite(candidate.confidence));
    QVERIFY(candidate.confidence >= 0 && candidate.confidence <= 1);
    QVERIFY(candidate.diagnostics.correlation > .99);
}

void TelemetryTests::synchronizesGpsSpeed()
{
    const TelemetrySession video = speedSession(0.0, 60.0, 0.0);
    const TelemetrySession telemetry = speedSession(0.0, 70.0, 3.2);
    const SyncCandidate candidate = TelemetrySyncEngine::synchronize(video, telemetry);
    QVERIFY2(qAbs(candidate.offset - 3.2) <= 0.11, qPrintable(QString::number(candidate.offset)));
    QVERIFY(candidate.diagnostics.correlation > 0.99);
    QVERIFY(shouldAutoApplySyncCandidate(candidate));
}

void TelemetryTests::cancelsSynchronizationDeterministically()
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

void TelemetryTests::reportsAmbiguousGpsSpeed()
{
    TelemetrySession video = speedSession(0.0, 30.0, 0.0);
    TelemetrySession telemetry = speedSession(0.0, 35.0, 0.0);
    std::fill(video.channels["speed"].values.begin(), video.channels["speed"].values.end(), 42.0F);
    std::fill(
        telemetry.channels["speed"].values.begin(),
        telemetry.channels["speed"].values.end(),
        42.0F);
    const SyncCandidate candidate = TelemetrySyncEngine::synchronize(video, telemetry);
    QCOMPARE(candidate.diagnostics.correlation, -1.0);
    QCOMPARE(candidate.confidence, 0.0);
}

void TelemetryTests::retainsGlobalSyncAmbiguity()
{
    const auto periodicSession = [](const int seconds) {
        TelemetrySession session;
        TelemetryChannel speed;
        speed.name = QStringLiteral("speed");
        for (int index = 0; index <= seconds * 10; ++index) {
            const double time = index / 10.0;
            speed.timestamps.append(time);
            speed.values.append(static_cast<float>(70.0
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

void TelemetryTests::rejectsAutomaticSyncWithShortOverlap()
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

void TelemetryTests::synchronizesWhenTheRecordingsOnlyPartlyOverlap()
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
            speed.timestamps.append(time);
            speed.values.append(static_cast<float>(knot(base) + (knot(base + 1.0) - knot(base)) * fraction));
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

void TelemetryTests::neverAutoAppliesAnotherLapOfPeriodicLaps()
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
            speed.timestamps.append(time);
            speed.values.append(static_cast<float>(120.0 + 40.0 * std::sin(phase) + 15.0 * std::sin(3.0 * phase + 0.4)));
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

void TelemetryTests::syncsOptionalRealRecording()
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

// AppController starts its export worker as this executable with
// --export-worker. That is handed to the application's own worker, so a
// controller export in a test (the user-guide capture) runs for real.
#define main telemetryTestsMain
QTEST_MAIN(TelemetryTests)
#undef main

int main(int argc, char *argv[])
{
    if (argc == 3 && QByteArray(argv[1]) == "--export-worker") {
        QCoreApplication app(argc, argv);
        return QProcess::execute(QStringLiteral(FLAPPEDEAR_NATIVE_PATH),
            {QStringLiteral("--export-worker"), QString::fromLocal8Bit(argv[2])});
    }
    return telemetryTestsMain(argc, argv);
}
#include "TelemetryTests.moc"
