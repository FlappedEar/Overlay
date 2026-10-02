#include "gopro/GoProTelemetrySource.h"
#include "app/AppController.h"
#include "app/ApplicationIdentity.h"
#include "app/GuiSessionLock.h"
#include "app/PreviewPlayback.h"
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
#include <fcntl.h>
#endif

using namespace FlappedEar;

class TelemetryTests final : public QObject {
    Q_OBJECT

    static QJsonArray approveAllSegmentsOnRun(FlappedEar::AppController &controller, const QString &runName);
    static QStringList unreachableControls(QQuickWindow *window);
private slots:
    void initTestCase();
    void usesOverlaysIdentityWithItsOwnStorage();
    void preservesSignedSamplesWithBrakingUpPresentation();
    void persistsEditableLapChannels();
    void reopensPreferencesProjectAndRecoveryAfterDisplayRename();
    void cleanupTestCase();
    void persistsEventSelectionAndRunLocalSync();
    void recoversEventAndRelinksOnlyActiveSource();
    void rejectsInvalidEventWithoutReplacingDocument();
    void rejectsLateSourceResultsAfterRunSelection();
    void selectsEventRunThroughAnalysisQml();
    void importsSixRunsAndAppendsWithoutDuplicates();
    void confirmsExplicitSourceGroups();
    void cancelsAndRejectsChangedBatchSources();
    void invalidatesBatchReviewAfterDocumentChanges();
    void reviewsBatchThroughProductionQml();
    void displaysTimedLapsWithoutVideo();
    void importsAnalysisRunsAutomatically();
    void guardsAutomaticAnalysisImport();
    void editsRunMetadataWithoutChangingAnalysis();
    void editsRunMetadataThroughQml();
    void showsRunProgressionWithLiveContext();
    void startsOutingThroughAnalysisQml();
    void opensOutingLapWithoutChangingEditor();
    void linksOutingLapVideoToActiveRunOnly();
    void followsOutingLapVideoPositionWithinLapBounds();
    void opensRankedLapsAndRecomputesAfterExclusion();
    void groupsOutingLapsAfterExplicitConfiguration();
    void persistsDayDecisionsAndKeepsIndependentDetail();
    void restoresDayDecisionsAfterMoveMissingRelinkAndRecovery();
    void rejectsStaleDayDetailCompletion();
    void automaticallyGroupsRunsThroughProductionQml();
    void separatesAutomaticGroupsAfterSourceReplacement();
    void automaticallyGroupsPrivateTrackDay();
    void confirmsTrackConfigurationThroughQml();
    void lapExclusionPolicySharesRankingAndRenderInputs();
    void excludesAndRestoresLapThroughQml();
    void lapExclusionsSurviveSaveRecoveryAndInvalidateSafely();
    void lapReferencesSurviveReopenAndReordering();
    void lapReferencesRejectSourceAndGateChanges();
    void lapReferencesDetectUnsampledContentChanges();
    void recordsVboUtcChronology();
    void ordersWholeOutingAndReopensSources();
    void presentsDayResultStatesWithoutVideo();
    void selectsIndependentComparisonLapsThroughQml();
    void restoresComparisonSelectionAfterReopen();
    void restoresComparisonRangeAndChannelsAfterReopen();
    void preservesComparisonSlotAcrossFailuresAndReplacement();
    void comparesTwoLapsFromTheSameRun();
    void overlaysComparisonLapsOnASharedProgressAxis();
    void showsCornerAnalyzerSegmentMetricsForBothLaps();
    void selectsCornerAnalyzerSegmentThroughQml();
    void calculatesOutingTheoreticalBestAcrossPopulation();
    void opensTheoreticalBestDonorFromAnotherRun();
    void opensTheoreticalBestSectorThroughQml();
    void acceptsM3SegmentationCornerAndTheoreticalBestWorkflow();
    void derivesTimeLossObservationsForComparisonPair();
    void ranksTimeLossesAndRecalculatesOnExclusion();
    void navigatesFromRankedLossToCornerEvidence();
    void reportsLapAndSectorConsistency();
    void reportsCornerVariabilityWithGpsLimits();
    void showsSectionProgressionBetweenSessions();
    void showsAbGgScatterWithPeaks();
    void summarizesRecordedTemperaturesPerRunAndSection();
    void showsRecordedTemperaturesThroughTheDayInQml();
    void showsHeartRateByRunAndSegmentInQml();
    void buildsDayReportWithProvenance();
    void presentsDayReportWithEvidenceNavigation();
    void selectsFocusAreasFromComputedObservations();
    void acceptsM4ReportLossCornerEvidenceWorkflow();
    void summarizesHeartRatePerRunSectionAndInterval();
    void formatsElapsedTimes();
    void analyzesPrivateTrackDayCorners();
    void createsSegmentsAutomaticallyFromTheBestLap();
    void discardsDayResultsFromThePreviousDay();
    void keepsAutomaticSegmentsWhenTheRunChangesDuringCreation();
    void reviewsSegmentProposalsForTheOpenLap();
    void editsApprovedSegmentsWithUndo();
    void persistsSegmentationAcrossSaveRecoveryAndReopen();
    void timesApprovedSectorsForTheOpenLap();
    void showsComparisonSlotCompatibilityAndCoverageContext();
    void sharesComparisonCacheAndRevalidatesSources();
    void rejectsComparisonBeyondSharedBudget();
    void cancelsSupersededComparisonWaitingForCache();
    void excludesChannelMissingFromOneComparisonSlot();
    void comparesKnownDeltaThroughFullComparisonPipeline();
    void keepsComparisonAndOutingLapVideoIndependent();
    void presentsBrakingUpInGForceWidgets();
    void rejectsBatchLinksWithDifferentPersistedFormats();
    void savesReopensAndRelinksRcz();
    void exportsSyntheticRczThroughWorker_data();
    void exportsSyntheticRczThroughWorker();
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
    void protectsEveryDaySourceFromExport();
    void keepsOutputSafeWhenTheDestinationFills_data();
    void keepsOutputSafeWhenTheDestinationFills();
    void describesOutOfSpaceExportFailures();
    void keepsAnalysisControlsReachableAtMinimumSize_data();
    void keepsAnalysisControlsReachableAtMinimumSize();
    void keepsACompleteDayThroughMoveRelinkAndRecovery();
    void keepsRecoveryWhenQuittingAtTheRecoveryPrompt();
    void offersRecoveryOfAnEventCreatedByImport();
    void importsDroppedFilesAndFolders();
    void switchesTheActiveRunPrimaryWithoutStaleEditorState();
    void reviewsSourceFusionInRunDetails();
    void reviewsGoProChapterGroups();
    void keepsVideoChaptersAsOneTimeline();
    void playsVideoChaptersAcrossBoundaries();
    void showsSideBySideLapVideo();
    void showsCoastingOnTheOpenLap();
    void showsTrailBrakingInTheCornerAnalyzer();
    void coloursTheComparisonMapByAChannel();
    void associatesTemperaturesWithLapPerformance();
    void routesNewDocumentSaveAsThroughPendingQuit();
    void mapsLapStartTelemetryTimesBackToVideoBounds();
    void rendersAllComparisonTilesInProductionScene();
    void rendersTyresInExportScene();
    void decodesOptionalRealVideoFrameWithNativeSink();
    void benchmarksCachedOptionalRealVboPresentationLookups();
    void persistsWidgetScenes();
    void normalizesWidgetSemanticsAcrossMutationAndImport();
    void rejectsNonFiniteWidgetGeometryAndDuplicateIds();
    void loadsVisualTemplates();
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
    void preservesLongitudeConventionInLapDetail();
    void persistsAndInvalidatesRunTrackConfiguration();
    void cachesStaticTrackGeometry();
    void keepsStaticTrackIndependentFromTime();
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

// KAN-42: EventProjectFixture::routeVbo() never has more than time/latitude/
// longitude columns, so a "missing sensor" comparison fixture (one recording
// has a channel the other lacks) needs a real extra column appended to its
// text, not just a differently-shaped session -- the actual VBO parser must
// see and name the column the same way a real logger's extra channel would.
QByteArray withSyntheticSpeedChannel(const QByteArray &vbo)
{
    QString text = QString::fromUtf8(vbo);
    text.replace(QStringLiteral("[column names]\ntime latitude longitude\n[data]\n"),
        QStringLiteral("[column names]\ntime latitude longitude speed\n[data]\n"));
    const QStringList lines = text.split('\n');
    QStringList result;
    result.reserve(lines.size());
    bool inData = false;
    int index = 0;
    for (const QString &line : lines) {
        if (line == QStringLiteral("[data]")) { inData = true; result.append(line); continue; }
        if (inData && !line.isEmpty())
            result.append(line + QString(" %1").arg(40.0 + 5.0 * std::sin(index++ * 0.3), 0, 'f', 3));
        else
            result.append(line);
    }
    return result.join('\n').toUtf8();
}

// KAN-42: a known, deterministic pace difference between two SEPARATE
// imported runs of the identical physical path (same coordinates, only time
// uniformly rescaled) -- the full-pipeline equivalent of
// TrackProgressTests::knownDelayHasCorrectSignAndFinishLineMagnitude, which
// exercises the same guarantee directly against buildProgressAxis/
// computeDeltaSeries without import, comparison-slot selection or the shared
// progress axis built through AppController.
QByteArray routeVboWithTimeScale(const double scale, const QByteArray &vbo)
{
    const QStringList lines = QString::fromUtf8(vbo).split('\n');
    QStringList result;
    result.reserve(lines.size());
    bool inData = false;
    for (const QString &line : lines) {
        if (line == QStringLiteral("[data]")) { inData = true; result.append(line); continue; }
        if (inData && !line.isEmpty()) {
            const QStringList parts = line.split(' ');
            if (parts.size() >= 3) {
                result.append(QString("%1 %2 %3").arg(parts[0].toDouble() * scale, 0, 'f', 6).arg(parts[1]).arg(parts[2]));
                continue;
            }
        }
        result.append(line);
    }
    return result.join('\n').toUtf8();
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

void TelemetryTests::persistsEditableLapChannels()
{
    QTemporaryDir directory; QVERIFY(directory.isValid());
    QSettings settings; settings.clear(); settings.sync();
    const auto project = directory.filePath("channels.fetproject");
    const auto recovery = directory.filePath("recovery.json");
    {
        AppController controller(nullptr, recovery);
        QVERIFY(controller.importAnalysisRuns("Channels", {QUrl::fromLocalFile(QStringLiteral(TEST_FIXTURE_PATH))}));
        QTRY_COMPARE(controller.outingLaps().size(), 1);
        QTRY_COMPARE(controller.vboLoadState(), QStringLiteral("ready"));
        QVERIFY(controller.saveProject(QUrl::fromLocalFile(project)));
        QTRY_VERIFY(controller.selectOutingLap(0));
        QTRY_COMPARE(controller.outingLapDetailState(), QStringLiteral("ready"));
        const auto document = controller.currentProjectObject();
        const auto geometry = controller.outingLapTrack();
        const auto activeRun = controller.activeRunId();
        const auto cursor = controller.outingLapCursor();
        QVERIFY(controller.outingLapAvailableChannels().contains("rpm"));
        controller.setOutingLapChannels({"rpm", "rpm", "missing", "heart_rate", "brake", "velocity", "mystery"});
        QCOMPARE(controller.outingLapChannels(), QStringList({"rpm", "heart_rate", "brake", "velocity"}));
        controller.closeOutingLap();
        QVERIFY(controller.outingLapAvailableChannels().isEmpty());
        controller.setOutingLapChannels({"mystery"}); // No active detail cannot change preferences.
        QVERIFY(controller.selectOutingLap(0));
        QTRY_COMPARE(controller.outingLapDetailState(), QStringLiteral("ready"));
        QCOMPARE(controller.outingLapChannels(), QStringList({"rpm", "heart_rate", "brake", "velocity"}));
        QCOMPARE(controller.currentProjectObject(), document);
        QCOMPARE(controller.outingLapTrack(), geometry);
        QCOMPARE(controller.activeRunId(), activeRun);
        QCOMPARE(controller.outingLapCursor(), cursor);
        QVERIFY(!controller.dirty());
    }
    {
        AppController reopened(nullptr, recovery);
        QTRY_VERIFY(!reopened.projectLoading());
        QTRY_COMPARE(reopened.outingLaps().size(), 1);
        QVERIFY(reopened.selectOutingLap(0));
        QTRY_COMPARE(reopened.outingLapDetailState(), QStringLiteral("ready"));
        QCOMPARE(reopened.outingLapChannels(), QStringList({"rpm", "heart_rate", "brake", "velocity"}));
        reopened.setOutingLapChannels({});
        reopened.closeOutingLap();
        QVERIFY(reopened.selectOutingLap(0));
        QTRY_COMPARE(reopened.outingLapDetailState(), QStringLiteral("ready"));
        QVERIFY(reopened.outingLapChannels().isEmpty());
    }
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
    // KAN-125: Flapped Ear Overlays owns its own storage; the previous identity's
    // tree is reached only by LegacyStorageMigration (StorageMigrationTests).
    QCoreApplication::setOrganizationName("FlappedEar");
    QCoreApplication::setOrganizationDomain("flappedear.com");
    QCoreApplication::setApplicationName(ApplicationIdentity::legacyStorageName);
    QGuiApplication::setApplicationDisplayName("Flapped Ear Telemetry");
    const QString legacySettingsPath = QSettings().fileName();
    const QString legacyDataPath = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    ApplicationIdentity::initialize();
    QCOMPARE(QGuiApplication::applicationDisplayName(), QString("Flapped Ear Overlays"));
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
        QCOMPARE(controller.analysisWindowWidth(), 777);
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
    controller.setAnalysisVisible(true);
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
    QVERIFY(controller.analysisVisible());
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

void TelemetryTests::selectsEventRunThroughAnalysisQml()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings; settings.clear(); settings.sync();
    AppController controller(nullptr, directory.filePath("recovery.json"));
    QVERIFY(controller.m_document.beginProjectLoad(directory.filePath("event.fetproject"), EventProjectFixture::project()));
    QQmlEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("appController"), &controller);
    QSignalSpy warnings(&engine, &QQmlEngine::warnings);
    QQmlComponent component(&engine, QUrl::fromLocalFile(QStringLiteral(ANALYSIS_PANEL_QML_PATH)));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    std::unique_ptr<QObject> panel(component.createWithInitialProperties({{"mediaDuration", 0}, {"width", 900}, {"height", 600}}));
    QVERIFY2(panel, qPrintable(component.errorString()));
    auto *picker = panel->findChild<QObject *>(QStringLiteral("eventRunPicker"));
    QVERIFY(picker);
    QCOMPARE(picker->property("count").toInt(), 2);
    QCOMPARE(picker->property("currentIndex").toInt(), 0);
    QCOMPARE(picker->property("currentText").toString(), QStringLiteral("run-a"));
    QVERIFY(QMetaObject::invokeMethod(picker, "activated", Q_ARG(int, 1)));
    QCOMPARE(controller.activeRunId(), QStringLiteral("run-b"));
    QCOMPARE(picker->property("currentIndex").toInt(), 1);
    QCOMPARE(picker->property("currentText").toString(), QStringLiteral("run-b"));
    controller.requestNewProject();
    QVERIFY(!picker->property("enabled").toBool());
    QVERIFY(QMetaObject::invokeMethod(picker, "activated", Q_ARG(int, 0)));
    QCOMPARE(controller.activeRunId(), QStringLiteral("run-b"));
    QCOMPARE(picker->property("currentIndex").toInt(), 1);
    QCOMPARE(warnings.size(), 0);
    controller.cancelPendingDestructiveAction();
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
    QVERIFY(controller.beginBatchImport(urls));
    QTRY_COMPARE(controller.batchImportState(), QStringLiteral("review"));
    QCOMPARE(controller.batchImportRows().size(), 8);
    QCOMPARE(controller.batchImportProcessed(), 8);
    QCOMPARE(controller.currentProjectObject(), original);
    QCOMPARE(controller.batchImportRows()[6].toMap().value("status").toString(), QStringLiteral("duplicate"));
    QCOMPARE(controller.batchImportRows()[7].toMap().value("status").toString(), QStringLiteral("error"));
    QVERIFY(controller.confirmBatchImport("Track day", false, independentBatchChoices(controller.batchImportRows())));
    QTRY_COMPARE(controller.batchImportState(), QStringLiteral("idle"));
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
    QVERIFY(controller.beginBatchImport({urls[0], QUrl::fromLocalFile(extra)}));
    QTRY_COMPARE(controller.batchImportState(), QStringLiteral("review"));
    QVERIFY(controller.batchImportRows()[0].toMap().value("existing").toBool());
    QVERIFY(!controller.confirmBatchImport({}, true, independentBatchChoices(controller.batchImportRows())));
    QVERIFY(controller.confirmBatchImport({}, true, independentBatchChoices(controller.batchImportRows(), true)));
    QTRY_COMPARE(controller.batchImportState(), QStringLiteral("idle"));
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
    QVERIFY(controller.beginBatchImport({vbo, QUrl::fromLocalFile(rcz)}));
    QTRY_COMPARE(controller.batchImportState(), QStringLiteral("review"));
    auto choices = independentBatchChoices(controller.batchImportRows());
    QCOMPARE(choices.size(), 2);
    const auto first = choices[0].toMap().value("proposalId");
    const auto second = choices[1].toMap().value("proposalId");
    // Cycles and duplicate choices must not silently lose sources.
    QVERIFY(!controller.confirmBatchImport("Day", false, {QVariantMap{{"proposalId", first}, {"groupId", second}},
        QVariantMap{{"proposalId", second}, {"groupId", first}}}));
    QVERIFY(!controller.confirmBatchImport("Day", false, {choices[0], choices[0]}));
    choices[1] = QVariantMap{{"proposalId", second}, {"groupId", first}};
    QVERIFY(controller.confirmBatchImport("Day", false, choices));
    QTRY_COMPARE(controller.batchImportState(), QStringLiteral("idle"));
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
    QVERIFY(controller.beginBatchImport({QUrl::fromLocalFile(source)}));
    controller.cancelBatchImport();
    QTRY_VERIFY(!controller.m_document.m_batchPending);
    QCOMPARE(controller.batchImportState(), QStringLiteral("idle"));
    QCOMPARE(controller.currentProjectObject(), before);
    QVERIFY(controller.beginBatchImport({QUrl::fromLocalFile(source)}));
    QTRY_COMPARE(controller.batchImportState(), QStringLiteral("review"));
    const auto choices = independentBatchChoices(controller.batchImportRows());
    QVERIFY(writeBytes(source, "changed"));
    QVERIFY(controller.confirmBatchImport("Day", false, choices));
    QTRY_COMPARE(controller.batchImportState(), QStringLiteral("review"));
    QVERIFY(!controller.batchImportError().isEmpty());
    QCOMPARE(controller.currentProjectObject(), before);
    QVERIFY(writeBytes(source, readBytes(QStringLiteral(TEST_FIXTURE_PATH))));
    QVERIFY(controller.confirmBatchImport("Day", false, choices));
    controller.cancelBatchImport();
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
    QVERIFY(controller.beginBatchImport(urls));
    QTRY_COMPARE(controller.batchImportState(), QStringLiteral("review"));
    controller.setSyncOffset(8);
    QCOMPARE(controller.batchImportState(), QStringLiteral("error"));
    QCOMPARE(controller.syncOffset(), 8.0);
    QVERIFY(controller.beginBatchImport(urls));
    QTRY_COMPARE(controller.batchImportState(), QStringLiteral("review"));
    QVERIFY(!controller.confirmBatchImport("Day", false, independentBatchChoices(controller.batchImportRows())));
    QVERIFY(controller.batchImportError().contains("Save"));
    controller.cancelBatchImport();
    QVERIFY(controller.saveProject(QUrl::fromLocalFile(directory.filePath("old.fetproject"))));
    QVERIFY(controller.beginBatchImport(urls));
    QTRY_COMPARE(controller.batchImportState(), QStringLiteral("review"));
    QVERIFY(controller.saveProject(QUrl::fromLocalFile(directory.filePath("new.fetproject"))));
    QCOMPARE(controller.batchImportState(), QStringLiteral("error"));
    // Source generation invalidates preparation even if its file finishes later.
    QVERIFY(controller.beginBatchImport(urls));
    controller.loadVbo(urls[0]);
    QTRY_VERIFY(!controller.m_document.m_batchPending);
    QCOMPARE(controller.batchImportState(), QStringLiteral("error"));
}

void TelemetryTests::reviewsBatchThroughProductionQml()
{
    QTemporaryDir directory; QVERIFY(directory.isValid());
    QSettings settings; settings.clear(); settings.sync();
    AppController controller(nullptr, directory.filePath("recovery.json"));
    QVERIFY(controller.beginBatchImport({QUrl::fromLocalFile(QStringLiteral(TEST_FIXTURE_PATH))}));
    QTRY_COMPARE(controller.batchImportState(), QStringLiteral("review"));
    QQmlEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("appController"), &controller);
    QSignalSpy warnings(&engine, &QQmlEngine::warnings);
    QQuickWindow window;
    window.resize(1180, 720);
    QQmlComponent component(&engine, QUrl::fromLocalFile(QStringLiteral(BATCH_IMPORT_QML_PATH)));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    std::unique_ptr<QObject> dialog(component.createWithInitialProperties({{"parent", QVariant::fromValue(window.contentItem())}}));
    QVERIFY2(dialog, qPrintable(component.errorString()));
    QVERIFY(QMetaObject::invokeMethod(dialog.get(), "open"));
    QCoreApplication::processEvents();
    auto *name = dialog->findChild<QObject *>(QStringLiteral("batchEventName"));
    QVERIFY(name); name->setProperty("text", "QML event");
    QVERIFY(QMetaObject::invokeMethod(dialog.get(), "submit"));
    QTRY_COMPARE(controller.eventName(), QStringLiteral("QML event"));
    QCOMPARE(controller.eventRuns().size(), 1);
    QCOMPARE(warnings.size(), 0);
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
    QSignalSpy committed(&controller, &AppController::batchImportCommitted);
    QVERIFY(controller.importAnalysisRuns("  Track Saturday  ", {vbo, QUrl::fromLocalFile(rcz), vbo, QUrl::fromLocalFile(bad)}));
    QTRY_COMPARE(committed.size(), 1);
    QTRY_COMPARE(controller.vboLoadState(), QStringLiteral("ready"));
    QCOMPARE(controller.eventName(), QStringLiteral("Track Saturday"));
    QCOMPARE(controller.eventRuns().size(), 2);
    QCOMPARE(controller.analysisImportMessages().size(), 2);
    QVERIFY(controller.batchImportError().isEmpty());
    QVERIFY(controller.videoSource().isEmpty());
    QVERIFY(controller.analysisVisible());
    QVERIFY(controller.dirty());
    const auto active = controller.activeRunId();
    controller.setSyncOffset(4);
    const auto laps = directory.filePath("third.vbo");
    QVERIFY(writeBytes(laps, EventProjectFixture::lapsVbo()));
    QVERIFY(controller.importAnalysisRuns({}, {vbo, QUrl::fromLocalFile(laps)}));
    QTRY_COMPARE(committed.size(), 2);
    QCOMPARE(controller.eventRuns().size(), 3);
    QCOMPARE(controller.activeRunId(), active);
    QCOMPARE(controller.syncOffset(), 4.0);
    QCOMPARE(controller.analysisImportMessages().size(), 1);
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
    QVERIFY(!controller.importAnalysisRuns(" ", urls));
    QVERIFY(controller.eventRuns().isEmpty());
    QVERIFY(controller.importAnalysisRuns("Cancelled", urls));
    controller.cancelBatchImport();
    QTRY_VERIFY(!controller.m_document.m_batchPending);
    QVERIFY(controller.eventRuns().isEmpty());
    QVERIFY(!controller.dirty());
    QVERIFY(controller.importAnalysisRuns("Stale", urls));
    controller.setSyncOffset(2);
    QTRY_VERIFY(!controller.m_document.m_batchPending);
    QVERIFY(controller.eventRuns().isEmpty());
    QCOMPARE(controller.syncOffset(), 2.0);
    QVERIFY(!controller.importAnalysisRuns("Dirty", urls));
    QVERIFY(controller.batchImportError().contains("Save"));
    QVERIFY(controller.saveProject(QUrl::fromLocalFile(directory.filePath("old.fetproject"))));
    const auto bad = directory.filePath("bad.rcz"); QVERIFY(writeBytes(bad, "broken"));
    const auto before = controller.currentProjectObject();
    QVERIFY(controller.importAnalysisRuns("Broken", {QUrl::fromLocalFile(bad)}));
    QTRY_VERIFY(!controller.m_document.m_batchPending);
    QVERIFY(!controller.batchImportError().isEmpty());
    QCOMPARE(controller.analysisImportMessages().size(), 1);
    QCOMPARE(controller.currentProjectObject(), before);
    QVERIFY(controller.eventRuns().isEmpty());
}

void TelemetryTests::editsRunMetadataWithoutChangingAnalysis()
{
    QTemporaryDir directory; QVERIFY(directory.isValid());
    QSettings settings; settings.clear(); settings.sync();
    const auto path = directory.filePath("20260901-rain.vbo");
    const auto secondPath = directory.filePath("second.vbo");
    QVERIFY(writeBytes(path, EventProjectFixture::lapsVbo()));
    QVERIFY(writeBytes(secondPath, EventProjectFixture::lapsVbo().replace("15 52.0001", "16 52.0001")));
    const auto savedPath = directory.filePath("day.fetproject");
    const auto recoveryPath = directory.filePath("recovery.json");
    QString runId;
    {
        AppController controller(nullptr, recoveryPath);
        QVERIFY(controller.importAnalysisRuns("Metadata", {QUrl::fromLocalFile(path), QUrl::fromLocalFile(secondPath)}));
        QTRY_COMPARE(controller.vboLoadState(), QString("ready")); QTRY_COMPARE(controller.outingLaps().size(), 10);
        runId = controller.activeRunId();
        QVERIFY(controller.setRunTrackConfiguration(runId, "Full", "clockwise"));
        QVERIFY(controller.saveProject(QUrl::fromLocalFile(savedPath)));
        QTRY_VERIFY(!controller.outingLapsLoading() && controller.m_analysis.m_outingLapRequestedKey == controller.m_analysis.outingLapKey());
        QString group;
        for (const auto &value : controller.outingCompatibilityGroups())
            if (value.toMap().value("resolved").toBool()) group = value.toMap().value("id").toString();
        QVERIFY(controller.selectOutingComparisonGroup(group));
        QVERIFY(controller.saveCurrentProject());
        const auto winner = controller.outingRanking().value("bestOfDay").toMap().value("reference").toMap();
        QVERIFY(controller.selectOutingLapReference(winner));
        QTRY_COMPARE(controller.outingLapDetailState(), QString("ready")); controller.setOutingLapCursor(1);
        const auto cursor = controller.outingLapCursor(); const auto track = controller.outingLapTrack();
        const auto detail = controller.m_analysis.m_outingLapDetailSession; const auto *session = controller.m_session.get();
        const auto generation = controller.m_document.m_sourceGeneration; const auto key = controller.m_analysis.outingLapKey();
        const auto config = controller.runTrackConfiguration(runId);
        const auto before = controller.currentProjectObject(); const auto revision = controller.m_document.m_documentState.revision();
        const auto metadata = controller.runMetadata(runId); const auto token = metadata.value("editToken").toString();
        QVERIFY(metadata.value("conditions").isNull()); QVERIFY(metadata.value("setupChanges").isNull());
        QVERIFY(controller.runMetadata("missing").isEmpty()); QVERIFY(!controller.dirty());
        QVERIFY(controller.updateRunMetadata(runId, token, metadata.value("name").toString(), "", "", ""));
        QCOMPARE(controller.currentProjectObject(), before); QCOMPARE(controller.m_document.m_documentState.revision(), revision);
        for (const auto &badName : {QString(" "), QString(161, 'x'), QString("x") + QChar::Null})
            QVERIFY(!controller.updateRunMetadata(runId, token, badName, "", "", ""));
        for (const auto &bad : {QString(4097, 'x'), QString("x") + QChar::Null}) {
            QVERIFY(!controller.updateRunMetadata(runId, token, "Renamed", bad, "", ""));
            QVERIFY(!controller.updateRunMetadata(runId, token, "Renamed", "", bad, ""));
            QVERIFY(!controller.updateRunMetadata(runId, token, "Renamed", "", "", bad));
        }
        QCOMPARE(controller.currentProjectObject(), before); QVERIFY(!controller.dirty());
        QVERIFY(controller.updateRunMetadata(runId, token, " Renamed run ", "Driver notes\nSecond line", "Damp, 18 °C", "Front pressure +0.1 bar"));
        QVERIFY(controller.dirty()); QCOMPARE(controller.m_document.m_documentState.revision(), revision + 1);
        QVERIFY(!controller.updateRunMetadata(runId, token, "Stale overwrite", "", "", ""));
        QCOMPARE(controller.runMetadata(runId).value("name").toString(), QString("Renamed run"));
        QCOMPARE(controller.m_document.m_sourceGeneration, generation); QCOMPARE(controller.m_session.get(), session);
        QCOMPARE(controller.m_analysis.outingLapKey(), key); QCOMPARE(controller.runTrackConfiguration(runId), config);
        QCOMPARE(controller.m_analysis.m_outingLapDetailSession, detail); QCOMPARE(controller.outingLapTrack(), track);
        QCOMPARE(controller.outingLapCursor(), cursor); QCOMPARE(controller.outingLapDetailState(), QString("ready"));
        QCOMPARE(controller.selectedOutingLap().value("runName").toString(), QString("Renamed run"));
        QCOMPARE(controller.selectedOutingLap().value("reference").toMap(), winner);
        QCOMPARE(controller.outingRanking().value("bestOfDay").toMap().value("reference").toMap(), winner);
        QCOMPARE(controller.outingRanking().value("bestOfDay").toMap().value("runName").toString(), QString("Renamed run"));
        QVERIFY(controller.outingLapMessages().join('\n').contains("Renamed run: recording date/time unavailable"));
        QVERIFY(!controller.outingLapMessages().join('\n').contains(metadata.value("name").toString() + ":"));
        for (const auto &value : controller.outingLaps()) if (value.toMap().value("runId").toString() == runId) {
            QCOMPARE(value.toMap().value("runName").toString(), QString("Renamed run"));
            QVERIFY(!value.toMap().value("chronologyKnown").toBool());
        }
        const auto updated = controller.currentProjectObject();
        const auto runsBefore = EventProjectFixture::runs(before); const auto runsAfter = EventProjectFixture::runs(updated);
        for (qsizetype i = 0; i < runsBefore.size(); ++i) {
            auto a = runsBefore[i].toObject(); auto b = runsAfter[i].toObject();
            for (const auto *field : {"name", "notes", "conditions", "setupChanges"}) { a.remove(field); b.remove(field); }
            QCOMPARE(a, b);
        }
        QString inactive;
        for (const auto &value : controller.eventRuns()) if (value.toMap().value("id").toString() != runId) inactive = value.toMap().value("id").toString();
        const auto inactiveMetadata = controller.runMetadata(inactive);
        QVERIFY(controller.updateRunMetadata(inactive, inactiveMetadata.value("editToken").toString(), "Second run", "", "", "Rear damping -1"));
        QCOMPARE(controller.activeRunId(), runId); QCOMPARE(controller.m_session.get(), session);
        QVERIFY(controller.saveProject(QUrl::fromLocalFile(savedPath))); QVERIFY(!controller.dirty());
    }
    {
        AppController controller(nullptr, recoveryPath);
        QTRY_COMPARE(controller.vboLoadState(), QString("ready")); QTRY_COMPARE(controller.outingLaps().size(), 10);
        const auto metadata = controller.runMetadata(runId);
        QCOMPARE(metadata.value("notes").toString(), QString("Driver notes\nSecond line"));
        QCOMPARE(metadata.value("conditions").toString(), QString("Damp, 18 °C"));
        QCOMPARE(metadata.value("setupChanges").toString(), QString("Front pressure +0.1 bar"));
        QVERIFY(controller.updateRunMetadata(runId, metadata.value("editToken").toString(), "Recovered run", "Unsaved notes", "", ""));
        QVERIFY(controller.runMetadata(runId).value("conditions").isNull());
        controller.m_document.writeRecoverySnapshot(); QVERIFY(QFileInfo::exists(recoveryPath));
    }
    {
        AppController controller(nullptr, recoveryPath); QVERIFY(controller.recoveryPending());
        controller.resolveStartupRecovery("recover"); QTRY_COMPARE(controller.vboLoadState(), QString("ready"));
        QTRY_COMPARE(controller.outingLaps().size(), 10); QVERIFY(controller.dirty());
        const auto metadata = controller.runMetadata(runId);
        QCOMPARE(metadata.value("name").toString(), QString("Recovered run"));
        QCOMPARE(metadata.value("notes").toString(), QString("Unsaved notes"));
        QVERIFY(metadata.value("conditions").isNull()); QVERIFY(metadata.value("setupChanges").isNull());
    }
}

void TelemetryTests::showsRunProgressionWithLiveContext()
{
    QTemporaryDir directory; QVERIFY(directory.isValid());
    QSettings settings; settings.clear(); settings.sync();
    const auto first = directory.filePath("first.vbo"), second = directory.filePath("second.vbo");
    QVERIFY(writeBytes(first, EventProjectFixture::lapsVbo()));
    QVERIFY(writeBytes(second, EventProjectFixture::lapsVbo().replace("15 52.0001", "16 52.0001")));
    AppController controller(nullptr, directory.filePath("recovery.json"));
    QVERIFY(controller.importAnalysisRuns("Progression", {QUrl::fromLocalFile(first), QUrl::fromLocalFile(second)}));
    QTRY_COMPARE(controller.vboLoadState(), QString("ready")); QTRY_COMPARE(controller.outingLaps().size(), 10);
    QCOMPARE(controller.outingProgression().value("state").toString(), QString("selection-required"));
    QStringList ids;
    for (const auto &value : controller.eventRuns()) ids.append(value.toMap().value("id").toString());
    QCOMPARE(ids.size(), 2);
    for (const auto &id : ids) QVERIFY(controller.setRunTrackConfiguration(id, "Full", "clockwise"));
    QTRY_VERIFY(!controller.outingLapsLoading() && controller.m_analysis.m_outingLapRequestedKey == controller.m_analysis.outingLapKey());
    const auto group = controller.outingCompatibilityGroups().first().toMap().value("id").toString();
    QVERIFY(controller.selectOutingComparisonGroup(group));
    const auto metadata = controller.runMetadata(ids[0]);
    QVERIFY(controller.updateRunMetadata(ids[0], metadata.value("editToken").toString(), "Morning", "Traffic observed",
        "Damp", "Front pressure +0.1 bar"));
    auto progression = controller.outingProgression();
    QCOMPARE(progression.value("runs").toList().size(), 2);
    QCOMPARE(progression.value("eligibleLapCount").toInt(), 6);
    auto run = progression.value("runs").toList().first().toMap();
    QCOMPARE(run.value("runId").toString(), ids[0]);
    QCOMPARE(run.value("conditions").toString(), QString("Damp"));
    QVERIFY(!run.value("chronologyKnown").toBool());
    const auto excluded = run.value("bestLap").toMap().value("reference").toMap();
    QVERIFY(controller.setOutingLapExcluded(excluded, true, "Traffic"));
    run = controller.outingProgression().value("runs").toList().first().toMap();
    QCOMPARE(run.value("eligibleLapCount").toInt(), 2);
    QCOMPARE(run.value("excludedLaps").toList().first().toMap().value("userReason").toString(), QString("Traffic"));
    const auto generation = controller.m_document.m_sourceGeneration;
    const auto *session = controller.m_session.get();
    QQmlEngine engine; engine.rootContext()->setContextProperty("appController", &controller);
    QSignalSpy warnings(&engine, &QQmlEngine::warnings); QQmlComponent component(&engine);
    component.setData("import QtQuick\nWindow { width: 760; height: 480; OutingLapPanel { anchors.fill: parent } }",
        QUrl::fromLocalFile(QStringLiteral(ANALYSIS_PANEL_QML_PATH)));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    std::unique_ptr<QObject> object(component.create()); QVERIFY2(object, qPrintable(component.errorString()));
    auto *window = qobject_cast<QQuickWindow *>(object.get()); QVERIFY(window);
    window->show(); QVERIFY(QTest::qWaitForWindowExposed(window));
    const auto findVisual = [](auto &&self, QQuickItem *item, const QString &name) -> QQuickItem * {
        if (item->objectName() == name) return item;
        for (auto *child : item->childItems()) if (auto *found = self(self, child, name)) return found;
        return nullptr;
    };
    auto *open = window->findChild<QQuickItem *>("openOutingProgression"); QVERIFY(open); QVERIFY(open->isEnabled());
    QTRY_VERIFY(window->contentItem()->contains(open->mapToScene(QPointF(open->width()/2, open->height()/2))));
    open->forceActiveFocus(); QTest::keyClick(window, Qt::Key_Space);
    auto *dialog = window->findChild<QObject *>("outingProgressionDialog"); QVERIFY(dialog);
    QTRY_VERIFY(dialog->property("opened").toBool());
    auto *list = window->findChild<QQuickItem *>("outingProgressionRuns"); QVERIFY(list);
    QTRY_COMPARE(list->property("count").toInt(), 2); QTRY_VERIFY(list->height() >= 80);
    QQuickItem *plot = nullptr, *context = nullptr, *best = nullptr;
    QTRY_VERIFY((plot = findVisual(findVisual, list, "progressionDistribution0")));
    QTRY_VERIFY((context = findVisual(findVisual, list, "progressionContext0")));
    QTRY_VERIFY((best = findVisual(findVisual, list, "openProgressionBest0")));
    QVERIFY(plot->isVisible()); QVERIFY(plot->width() > 100);
    const auto text = context->property("text").toString();
    QVERIFY(text.contains("Traffic")); QVERIFY(text.contains("Damp")); QVERIFY(text.contains("Front pressure +0.1 bar"));
    // Live metadata edits update visible context without reloading analysis.
    const auto current = controller.runMetadata(ids[0]);
    QVERIFY(controller.updateRunMetadata(ids[0], current.value("editToken").toString(), "Morning", "Traffic observed", "Drying", "Front pressure +0.1 bar"));
    QTRY_VERIFY((context = findVisual(findVisual, list, "progressionContext0")) && context->property("text").toString().contains("Drying"));
    QCOMPARE(controller.m_document.m_sourceGeneration, generation); QCOMPARE(controller.m_session.get(), session);
    QTRY_VERIFY((best = findVisual(findVisual, list, "openProgressionBest0")) && best->isEnabled());
    const auto bestReference = controller.outingProgression().value("runs").toList().first().toMap().value("bestLap").toMap().value("reference").toMap();
    best->forceActiveFocus(); QTest::keyClick(window, Qt::Key_Space);
    QTRY_VERIFY(!dialog->property("visible").toBool());
    QTRY_COMPARE(controller.outingLapDetailState(), QString("ready"));
    QCOMPARE(controller.selectedOutingLap().value("reference").toMap(), bestReference);
    controller.closeOutingLap();
    QCOMPARE(controller.activeRunId(), ids[0]);
    // A stale recording cannot contribute statistics. Restoring the policy recomputes the sample count.
    controller.m_analysis.m_outingStaleRunIds.insert(ids[0]); controller.m_analysis.refreshOutingCompatibility();
    auto stale = controller.outingProgression().value("runs").toList().first().toMap();
    QCOMPARE(stale.value("eligibleLapCount").toInt(), 0); QVERIFY(stale.value("distribution").isNull());
    controller.m_analysis.m_outingStaleRunIds.clear(); controller.m_analysis.refreshOutingCompatibility();
    QVERIFY(controller.setOutingLapExcluded(excluded, false, ""));
    QCOMPARE(controller.outingProgression().value("eligibleLapCount").toInt(), 6);
    QVERIFY(controller.setRunTrackConfiguration(ids[0], "Short", "clockwise"));
    QCOMPARE(controller.outingProgression().value("state").toString(), QString("loading"));
    QVERIFY(controller.outingProgression().value("runs").toList().isEmpty());
    QTRY_VERIFY(!controller.outingLapsLoading() && controller.m_analysis.m_outingLapRequestedKey == controller.m_analysis.outingLapKey());
    QCOMPARE(controller.outingProgression().value("runs").toList().size(), 1);
    QCOMPARE(controller.outingProgression().value("runs").toList().first().toMap().value("runId").toString(), ids[1]);
    QVERIFY(controller.selectOutingComparisonGroup(""));
    QCOMPARE(controller.outingComparisonSelectionState(), QString("automatic"));
    QVERIFY(!controller.outingProgression().value("runs").toList().isEmpty());
    QCOMPARE(warnings.size(), 0);
}

void TelemetryTests::editsRunMetadataThroughQml()
{
    QTemporaryDir directory; QVERIFY(directory.isValid());
    QSettings settings; settings.clear(); settings.sync();
    const auto path = directory.filePath("run.vbo"); QVERIFY(writeBytes(path, EventProjectFixture::lapsVbo()));
    AppController controller(nullptr, directory.filePath("recovery.json"));
    QVERIFY(controller.importAnalysisRuns("Details", {QUrl::fromLocalFile(path)}));
    QTRY_COMPARE(controller.vboLoadState(), QString("ready")); QTRY_COMPARE(controller.outingLaps().size(), 5);
    QVERIFY(controller.saveProject(QUrl::fromLocalFile(directory.filePath("day.fetproject"))));
    QTRY_VERIFY(!controller.outingLapsLoading() && controller.m_analysis.m_outingLapRequestedKey == controller.m_analysis.outingLapKey());
    const auto before = controller.currentProjectObject();
    QQmlEngine engine; engine.rootContext()->setContextProperty("appController", &controller);
    QSignalSpy warnings(&engine, &QQmlEngine::warnings); QQmlComponent component(&engine);
    component.setData("import QtQuick\nWindow { width: 760; height: 480; OutingLapPanel { anchors.fill: parent } }",
        QUrl::fromLocalFile(QStringLiteral(ANALYSIS_PANEL_QML_PATH)));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    std::unique_ptr<QObject> object(component.create()); QVERIFY2(object, qPrintable(component.errorString()));
    auto *window = qobject_cast<QQuickWindow *>(object.get()); QVERIFY(window);
    window->show(); QVERIFY(QTest::qWaitForWindowExposed(window));
    auto *open = window->findChild<QQuickItem *>("openRunDetails"); QVERIFY(open);
    open->forceActiveFocus(); QTest::keyClick(window, Qt::Key_Space);
    auto *dialog = window->findChild<QObject *>("runDetailsDialog"); QVERIFY(dialog);
    QTRY_VERIFY(dialog->property("opened").toBool());
    auto *name = window->findChild<QQuickItem *>("runDetailsName");
    auto *notes = window->findChild<QQuickItem *>("runDetailsNotes");
    auto *conditions = window->findChild<QQuickItem *>("runDetailsConditions");
    auto *setup = window->findChild<QQuickItem *>("runDetailsSetup");
    auto *save = window->findChild<QQuickItem *>("saveRunDetails");
    auto *cancel = window->findChild<QQuickItem *>("cancelRunDetails");
    auto *picker = window->findChild<QQuickItem *>("runDetailsPicker");
    auto *scroll = window->findChild<QQuickItem *>("runDetailsScroll");
    QVERIFY(name && notes && conditions && setup && save && cancel && picker && scroll);
    QVERIFY(conditions->property("text").toString().isEmpty());
    name->setProperty("text", "Draft"); QVERIFY(!picker->isEnabled());
    notes->setProperty("text", QString(4097, 'x')); QVERIFY(!save->isEnabled());
    QCOMPARE(notes->property("text").toString().size(), 4097); // Never silently truncate notes.
    notes->setProperty("text", "Notes"); conditions->setProperty("text", "Dry"); setup->setProperty("text", "Tyres changed");
    QVERIFY(save->isEnabled());
    QTRY_VERIFY2(scroll->height() > 50, qPrintable(QString("Scroll height %1; dialog %2x%3; window %4x%5")
        .arg(scroll->height()).arg(dialog->property("width").toDouble()).arg(dialog->property("height").toDouble())
        .arg(window->width()).arg(window->height())));
    QTRY_VERIFY(save->mapRectToScene(save->boundingRect()).top() >= 0
        && save->mapRectToScene(save->boundingRect()).bottom() <= window->height());
    cancel->forceActiveFocus(); QTest::keyClick(window, Qt::Key_Space);
    QTRY_VERIFY(!dialog->property("visible").toBool()); QCOMPARE(controller.currentProjectObject(), before); QVERIFY(!controller.dirty());
    open->forceActiveFocus(); QTest::keyClick(window, Qt::Key_Space); QTRY_VERIFY(dialog->property("opened").toBool());
    QVERIFY(notes->property("text").toString().isEmpty());
    name->setProperty("text", "Morning run"); notes->setProperty("text", "Driver notes");
    conditions->setProperty("text", "Dry"); setup->setProperty("text", "Tyres changed");
    save->forceActiveFocus(); QTest::keyClick(window, Qt::Key_Space); QTRY_VERIFY(!dialog->property("visible").toBool());
    QCOMPARE(controller.runMetadata(controller.activeRunId()).value("name").toString(), QString("Morning run"));
    QVERIFY(controller.dirty());
    open->forceActiveFocus(); QTest::keyClick(window, Qt::Key_Space); QTRY_VERIFY(dialog->property("opened").toBool());
    QCOMPARE(setup->property("text").toString(), QString("Tyres changed"));
    const auto current = controller.runMetadata(controller.activeRunId());
    QVERIFY(controller.updateRunMetadata(controller.activeRunId(), current.value("editToken").toString(), "Newer edit", "", "", ""));
    name->setProperty("text", "Stale draft"); save->forceActiveFocus(); QTest::keyClick(window, Qt::Key_Space);
    QVERIFY(dialog->property("visible").toBool());
    auto *error = window->findChild<QObject *>("runDetailsError"); QVERIFY(error); QVERIFY(!error->property("text").toString().isEmpty());
    QCOMPARE(controller.runMetadata(controller.activeRunId()).value("name").toString(), QString("Newer edit"));
    QTest::keyClick(window, Qt::Key_Escape); QTRY_VERIFY(!dialog->property("visible").toBool());
    QCOMPARE(warnings.size(), 0);
}

void TelemetryTests::startsOutingThroughAnalysisQml()
{
    QTemporaryDir directory; QVERIFY(directory.isValid());
    QSettings settings; settings.clear(); settings.sync();
    AppController controller(nullptr, directory.filePath("recovery.json"));
    QQmlEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("appController"), &controller);
    QSignalSpy warnings(&engine, &QQmlEngine::warnings);
    const auto path = QFileInfo(QStringLiteral(ANALYSIS_PANEL_QML_PATH)).dir().filePath("AnalysisWindow.qml");
    QQmlComponent component(&engine, QUrl::fromLocalFile(path));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    std::unique_ptr<QObject> window(component.createWithInitialProperties({{"videoSource", QUrl{}},
        {"playbackPosition", 0}, {"playbackRunning", false}, {"mediaDuration", 0}}));
    QVERIFY2(window, qPrintable(component.errorString()));
    auto *name = window->findChild<QObject *>("analysisOutingName");
    auto *choose = window->findChild<QObject *>("analysisChooseFiles");
    auto *runs = window->findChild<QObject *>("outingLapList");
    QVERIFY(name); QVERIFY(choose); QVERIFY(runs);
    QVERIFY(!window->property("hasWorkspace").toBool());
    QVERIFY(!choose->property("enabled").toBool());
    // KAN-87: a folder is offered beside the files, with an explicit
    // subfolder choice that starts off.
    auto *chooseFolder = window->findChild<QObject *>("analysisChooseFolder");
    auto *subfolders = window->findChild<QObject *>("analysisIncludeSubfolders");
    QVERIFY(chooseFolder); QVERIFY(subfolders);
    QVERIFY(!chooseFolder->property("enabled").toBool());
    QVERIFY(!subfolders->property("checked").toBool());
    name->setProperty("text", "QML outing");
    QVERIFY(choose->property("enabled").toBool());
    QVERIFY(chooseFolder->property("enabled").toBool());
    const QVariant files = QVariant::fromValue(QList<QUrl>{QUrl::fromLocalFile(QStringLiteral(TEST_FIXTURE_PATH))});
    QSignalSpy committed(&controller, &AppController::batchImportCommitted);
    QVERIFY(QMetaObject::invokeMethod(window.get(), "importFiles", Q_ARG(QVariant, files)));
    QTRY_COMPARE(committed.size(), 1);
    QTRY_COMPARE(controller.vboLoadState(), QStringLiteral("ready"));
    QCOMPARE(controller.eventName(), QStringLiteral("QML outing"));
    QVERIFY(window->property("hasWorkspace").toBool());
    QTRY_COMPARE(runs->property("count").toInt(), 1);
    auto *quickWindow = qobject_cast<QQuickWindow *>(window.get());
    QVERIFY(quickWindow);
    quickWindow->show();
    QVERIFY(QTest::qWaitForWindowExposed(quickWindow));
    // A shell-launched test cannot always take foreground focus on macOS.
    // Synthesize window activation as well as keyboard input so the production
    // WindowShortcut receives Escape, without calling its handler directly.
    QWindowSystemInterface::handleFocusWindowChanged(quickWindow);
    QTRY_COMPARE(QGuiApplication::focusWindow(), quickWindow);
    QQuickItem *row = nullptr;
    QTRY_VERIFY(QMetaObject::invokeMethod(runs, "itemAtIndex", Q_RETURN_ARG(QQuickItem *, row), Q_ARG(int, 0)) && row);
    QTest::mouseClick(quickWindow, Qt::LeftButton, Qt::NoModifier,
        row->mapToScene(QPointF(row->width() / 2, row->height() / 2)).toPoint());
    QTRY_COMPARE(controller.outingLapDetailState(), QStringLiteral("ready"));
    QVERIFY(window->property("showingLap").toBool());
    auto *charts = quickWindow->findChild<QQuickItem *>("outingLapCharts");
    QVERIFY(charts);
    auto *picker = charts->findChild<QQuickItem *>("analysisChannelPicker");
    auto *add = charts->findChild<QQuickItem *>("analysisAddChannel");
    QVERIFY(picker); QVERIFY(add); QVERIFY(add->isVisible()); QVERIFY(add->isEnabled());
    const auto geometry = controller.outingLapTrack();
    const auto document = controller.currentProjectObject();
    const auto added = picker->property("currentText").toString();
    QTest::mouseClick(quickWindow, Qt::LeftButton, Qt::NoModifier,
        add->mapToScene(QPointF(add->width() / 2, add->height() / 2)).toPoint());
    QTRY_VERIFY(controller.outingLapChannels().contains(added));
    QCOMPARE(controller.outingLapChannels().size(), 4);
    QVERIFY(!add->isEnabled());
    // Repeater/SplitView delegates follow the visual tree, which need not match
    // QObject ownership. Wait for their visual creation before interacting.
    const auto findVisual = [](auto &&self, QQuickItem *item, const QString &name) -> QQuickItem * {
        if (item->objectName() == name) return item;
        for (auto *child : item->childItems())
            if (auto *found = self(self, child, name)) return found;
        return nullptr;
    };
    QQuickItem *replace = nullptr;
    QTRY_VERIFY((replace = findVisual(findVisual, charts, "analysisReplaceChannel-" + added)) && replace->isVisible());
    // Let SplitView finish laying out the newly created row before input.
    QSignalSpy presented(quickWindow, &QQuickWindow::frameSwapped);
    quickWindow->requestUpdate();
    QTRY_VERIFY(!presented.isEmpty());
    // End activates the last choice on a closed ComboBox. Do not send Return
    // to a delegate that replacement may already have destroyed.
    replace->forceActiveFocus();
    QTest::keyClick(quickWindow, Qt::Key_End);
    QTRY_VERIFY(!controller.outingLapChannels().contains(added));
    const auto replacement = controller.outingLapChannels().last();
    QQuickItem *remove = nullptr;
    QTRY_VERIFY((remove = findVisual(findVisual, charts, "analysisRemoveChannel-" + replacement)) && remove->isVisible());
    presented.clear();
    quickWindow->requestUpdate();
    QTRY_VERIFY(!presented.isEmpty());
    QVERIFY(remove->width() > 0 && remove->height() > 0);
    QVERIFY(quickWindow->contentItem()->contains(remove->mapToScene(QPointF(remove->width() / 2, remove->height() / 2))));
    QTest::mouseClick(quickWindow, Qt::LeftButton, Qt::NoModifier,
        remove->mapToScene(QPointF(remove->width() / 2, remove->height() / 2)).toPoint());
    QTRY_COMPARE(controller.outingLapChannels().size(), 3);
    QVERIFY(add->isEnabled());
    QCOMPARE(controller.outingLapTrack(), geometry);
    QCOMPARE(controller.currentProjectObject(), document);
    QVariant brakingY, accelerationY, lateralY;
    QVERIFY(QMetaObject::invokeMethod(charts, "graphY", Q_RETURN_ARG(QVariant, brakingY),
        Q_ARG(QVariant, -1.0), Q_ARG(QVariant, -1.0), Q_ARG(QVariant, 1.0), Q_ARG(QVariant, 100.0), Q_ARG(QVariant, true)));
    QVERIFY(QMetaObject::invokeMethod(charts, "graphY", Q_RETURN_ARG(QVariant, accelerationY),
        Q_ARG(QVariant, 1.0), Q_ARG(QVariant, -1.0), Q_ARG(QVariant, 1.0), Q_ARG(QVariant, 100.0), Q_ARG(QVariant, true)));
    QVERIFY(QMetaObject::invokeMethod(charts, "graphY", Q_RETURN_ARG(QVariant, lateralY),
        Q_ARG(QVariant, -1.0), Q_ARG(QVariant, -1.0), Q_ARG(QVariant, 1.0), Q_ARG(QVariant, 100.0), Q_ARG(QVariant, false)));
    QVERIFY(brakingY.toDouble() < accelerationY.toDouble());
    QCOMPARE(lateralY.toDouble(), accelerationY.toDouble());
    auto *back = quickWindow->findChild<QQuickItem *>("backToOutingLaps");
    QVERIFY(back); QVERIFY(back->isVisible());
    QTest::mouseClick(quickWindow, Qt::LeftButton, Qt::NoModifier,
        back->mapToScene(QPointF(back->width() / 2, back->height() / 2)).toPoint());
    QTRY_VERIFY(!window->property("showingLap").toBool());
    QVERIFY(runs->property("visible").toBool());
    row->forceActiveFocus();
    QTest::keyClick(quickWindow, Qt::Key_Return);
    QTRY_COMPARE(controller.outingLapDetailState(), QStringLiteral("ready"));
    QTest::keyClick(quickWindow, Qt::Key_Escape);
    QTRY_VERIFY(controller.selectedOutingLap().isEmpty());
    QCOMPARE(warnings.size(), 0);
}

void TelemetryTests::opensOutingLapWithoutChangingEditor()
{
    QTemporaryDir directory; QVERIFY(directory.isValid());
    QSettings settings; settings.clear(); settings.sync();
    const auto recording = [](int base) {
        auto text = EventProjectFixture::lapsVbo();
        text.replace("time latitude longitude", "time latitude longitude velocity latacc-calc longacc-calc");
        const auto at = text.indexOf("[data]\n") + 7;
        auto lines = text.mid(at).split('\n');
        QByteArray data;
        for (const auto &line : lines) {
            if (line.isEmpty()) continue;
            const auto t = line.first(line.indexOf(' ')).toInt();
            data += line + ' ' + QByteArray::number(base + t) + " 0.25 -0.5\n";
        }
        return text.first(at) + data;
    };
    const auto first = directory.filePath("first.vbo"), second = directory.filePath("second.vbo");
    QVERIFY(writeBytes(first, recording(100))); QVERIFY(writeBytes(second, recording(200)));
    AppController controller(nullptr, directory.filePath("recovery.json"));
    QVERIFY(controller.importAnalysisRuns("Clickable day", {QUrl::fromLocalFile(first), QUrl::fromLocalFile(second)}));
    QTRY_COMPARE(controller.outingLaps().size(), 10);
    QTRY_COMPARE(controller.vboLoadState(), QStringLiteral("ready"));
    controller.setSyncOffset(19); controller.setTimeScale(1.3); controller.setPlaybackTime(7);
    const auto before = controller.currentProjectObject();
    const auto active = controller.activeRunId();
    const auto selected = controller.outingLaps()[6].toMap();
    QVERIFY(controller.selectOutingLap(6));
    QTRY_COMPARE(controller.outingLapDetailState(), QStringLiteral("ready"));
    QCOMPARE(controller.selectedOutingLap(), selected);
    QCOMPARE(controller.outingLapChannels(), QStringList({"velocity", "latacc-calc", "longacc-calc"}));
    QVERIFY(!controller.outingLapTrack().isEmpty());
    const auto start = selected.value("startTime").toDouble();
    const auto end = selected.value("endTime").toDouble();
    controller.setOutingLapCursor((start + end) / 2);
    QCOMPARE(controller.outingLapValueText("velocity"), QString::number(200 + (start + end) / 2, 'f', 2));
    QCOMPARE(controller.outingLapValueText("longacc-calc"), QStringLiteral("-0.50"));
    QVERIFY(!controller.outingLapTrackPoint().isEmpty());
    const auto series = controller.outingLapSeries("velocity", 200);
    QVERIFY(series.value("minimum").toDouble() >= 200 + start);
    QVERIFY(series.value("maximum").toDouble() <= 200 + end);
    // Zoom window: re-fetches at the same point budget over less time, so it
    // must reflect only the narrower range, not the full lap.
    const auto zoomStart = start + (end - start) * 0.25, zoomEnd = start + (end - start) * 0.75;
    const auto zoomed = controller.outingLapSeries("velocity", zoomStart, zoomEnd, 200);
    QVERIFY(zoomed.value("minimum").toDouble() >= 200 + zoomStart);
    QVERIFY(zoomed.value("maximum").toDouble() <= 200 + zoomEnd);
    QVERIFY(zoomed.value("minimum").toDouble() > series.value("minimum").toDouble());
    QVERIFY(zoomed.value("maximum").toDouble() < series.value("maximum").toDouble());
    {
        // The lap chart plots the zoom window, so the pointer must map across
        // that window, not the whole lap.
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("appController"), &controller);
        QQmlComponent component(&engine, QUrl::fromLocalFile(QStringLiteral(ANALYSIS_PANEL_QML_PATH)));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        std::unique_ptr<QObject> panel(component.createWithInitialProperties(
            {{"lapDetail", true}, {"mediaDuration", 0}, {"width", 900}, {"height", 600}}));
        QVERIFY2(panel, qPrintable(component.errorString()));
        const auto seekAt = [&panel](const double ratio) {
            return QMetaObject::invokeMethod(panel.get(), "seekAt", Q_ARG(QVariant, ratio));
        };
        QVERIFY(seekAt(0.5));
        QCOMPARE(controller.outingLapCursor(), (start + end) / 2);
        QVERIFY(panel->setProperty("zoomStart", zoomStart));
        QVERIFY(panel->setProperty("zoomEnd", zoomEnd));
        QVERIFY(panel->property("zoomed").toBool());
        QVERIFY(seekAt(0.0));
        QCOMPARE(controller.outingLapCursor(), zoomStart);
        QVERIFY(seekAt(0.5));
        QCOMPARE(controller.outingLapCursor(), (zoomStart + zoomEnd) / 2);
        QVERIFY(seekAt(1.0));
        QCOMPARE(controller.outingLapCursor(), zoomEnd);
        QVERIFY(QMetaObject::invokeMethod(panel.get(), "resetZoom"));
        QVERIFY(seekAt(1.0));
        QCOMPARE(controller.outingLapCursor(), end);
    }
    const auto track = controller.outingLapTrack();
    controller.setOutingLapCursor(-100); QCOMPARE(controller.outingLapCursor(), start);
    controller.setOutingLapCursor(1000); QCOMPARE(controller.outingLapCursor(), end);
    QCOMPARE(controller.outingLapTrack(), track);
    QCOMPARE(controller.outingLapSeries("velocity", 200), series);
    QCOMPARE(controller.currentProjectObject(), before);
    QCOMPARE(controller.activeRunId(), active);
    QCOMPARE(controller.playbackTime(), 7.0);
    QVERIFY(!controller.selectOutingLap(-1));
    QCOMPARE(controller.selectedOutingLap(), selected);
    // Delay an old completion, choose a new row, then release the stale result.
    AnalysisController::OutingLapDetailResult stale;
    stale.request = controller.m_analysis.m_outingLapDetailRequest;
    stale.session = controller.m_analysis.m_outingLapDetailSession;
    QPromise<AnalysisController::OutingLapDetailResult> promise; promise.start();
    controller.m_analysis.m_outingLapDetailPending = true;
    controller.m_analysis.m_outingLapDetailWatcher.setFuture(promise.future());
    QVERIFY(controller.selectOutingLap(0));
    QVERIFY(controller.selectOutingLap(1));
    promise.addResult(stale); promise.finish();
    QTRY_COMPARE(controller.outingLapDetailState(), QStringLiteral("ready"));
    QCOMPARE(controller.selectedOutingLap(), controller.outingLaps()[1].toMap());
    QVERIFY(controller.outingLapSeries("velocity", 200).value("maximum").toDouble() < 200);
    controller.closeOutingLap();
    QVERIFY(controller.outingLapSeries("velocity", 200).isEmpty());
    QVERIFY(QFile::remove(second));
    QVERIFY(controller.selectOutingLap(6));
    QTRY_COMPARE(controller.outingLapDetailState(), QStringLiteral("error"));
    QVERIFY(controller.outingLapDetailError().contains("missing"));
    QVERIFY(controller.outingLapSeries("velocity", 200).isEmpty());
    QVERIFY(writeBytes(second, recording(300)));
    QVERIFY(controller.selectOutingLap(6));
    QTRY_COMPARE(controller.outingLapDetailState(), QStringLiteral("error"));
    QVERIFY(controller.outingLapDetailError().contains("changed"));
    QVERIFY(controller.selectOutingLap(1));
    controller.closeOutingLap();
    QTRY_VERIFY(!controller.m_analysis.m_outingLapDetailWatcher.isRunning());
    QVERIFY(controller.selectedOutingLap().isEmpty());
    QCOMPARE(controller.currentProjectObject(), before);
}

void TelemetryTests::linksOutingLapVideoToActiveRunOnly()
{
    // KAN-39: video linkage is gated to the open lap's run being the
    // currently active/loaded one. Uses direct member access (this class is
    // a declared friend) to give the controller a deterministic, known video
    // duration without needing a real decodable file -- outingLapVideoAvailable/
    // outingLapVideoPositionMilliseconds only need m_videoSource non-empty and
    // m_exportSourceInfo populated, the same fields the existing preview-bound
    // invokables already read.
    QTemporaryDir directory; QVERIFY(directory.isValid());
    QSettings settings; settings.clear(); settings.sync();
    const auto first = directory.filePath("first.vbo"), second = directory.filePath("second.vbo");
    QVERIFY(writeBytes(first, EventProjectFixture::routeVbo()));
    QVERIFY(writeBytes(second, EventProjectFixture::routeVbo(130, -2, 2)));
    AppController controller(nullptr, directory.filePath("recovery.json"));
    QVERIFY(controller.importAnalysisRuns("Video linkage", {QUrl::fromLocalFile(first), QUrl::fromLocalFile(second)}));
    QTRY_COMPARE(controller.vboLoadState(), QStringLiteral("ready"));
    QTRY_VERIFY(!controller.outingLapsLoading());
    const auto active = controller.activeRunId();
    QVariantMap sameRunLap, otherRunLap;
    for (const auto &value : controller.outingLaps()) {
        const auto row = value.toMap();
        if (row.value("type") != "LAP") continue;
        if (sameRunLap.isEmpty() && row.value("runId").toString() == active) sameRunLap = row;
        else if (otherRunLap.isEmpty() && row.value("runId").toString() != active) otherRunLap = row;
    }
    QVERIFY(!sameRunLap.isEmpty() && !otherRunLap.isEmpty());

    // No video loaded at all: unavailable regardless of which lap is open.
    QVERIFY(controller.selectOutingLapReference(sameRunLap.value("reference").toMap()));
    QTRY_COMPARE(controller.outingLapDetailState(), QStringLiteral("ready"));
    QVERIFY(!controller.outingLapVideoAvailable());

    // A synthetic video at 30fps, without decoding anything real, long enough
    // to cover mid-lap but deliberately shorter than the lap's own end --
    // the "past the last real frame" case below needs that gap to exist.
    const auto start = sameRunLap.value("startTime").toDouble(), end = sameRunLap.value("endTime").toDouble();
    const auto midpoint = (start + end) / 2.0;
    controller.m_videoSource = QUrl::fromLocalFile(QStringLiteral("/synthetic/video.mp4"));
    MediaInfo info;
    info.frameRate = {30, 1};
    info.averageFrameRate = {30, 1};
    info.videoFrameCount = qRound64((midpoint + 1.0) * 30.0);
    info.timeBase = {1, 30};
    info.videoDurationTicks = info.videoFrameCount;
    controller.m_exportSourceInfo = info;
    controller.m_sync = {0.0, 1.0}; // offset 0, 1:1 scale -- telemetry time == video time here.

    // Lap belongs to the active run: available, and mid-lap maps to a real,
    // in-range video position.
    controller.setOutingLapCursor(midpoint);
    QVERIFY(controller.outingLapVideoAvailable());
    QCOMPARE(controller.outingLapVideoPositionMilliseconds(), qRound64((start + end) / 2 * 1000.0));

    // Past the video's last real frame: unavailable, not clamped into a
    // misleadingly-nearby frame. The synthetic video was deliberately sized
    // to end before the lap does, so this gap is guaranteed to exist.
    controller.setOutingLapCursor(end);
    QVERIFY(end * 1000.0 > controller.previewEndPositionMilliseconds());
    QVERIFY(!controller.outingLapVideoAvailable());

    // A lap from the non-active run: unavailable even with video loaded and
    // sync configured, and following a video position must not move its cursor.
    controller.closeOutingLap();
    QVERIFY(controller.selectOutingLapReference(otherRunLap.value("reference").toMap()));
    QTRY_COMPARE(controller.outingLapDetailState(), QStringLiteral("ready"));
    QVERIFY(!controller.outingLapVideoAvailable());
    const auto otherCursorBefore = controller.outingLapCursor();
    QVERIFY(!controller.followOutingLapVideoPosition(1000));
    QCOMPARE(controller.outingLapCursor(), otherCursorBefore);
}

void TelemetryTests::followsOutingLapVideoPositionWithinLapBounds()
{
    QTemporaryDir directory; QVERIFY(directory.isValid());
    QSettings settings; settings.clear(); settings.sync();
    const auto path = directory.filePath("first.vbo");
    QVERIFY(writeBytes(path, EventProjectFixture::lapsVbo()));
    AppController controller(nullptr, directory.filePath("recovery.json"));
    QVERIFY(controller.importAnalysisRuns("Follow video", {QUrl::fromLocalFile(path)}));
    QTRY_COMPARE(controller.vboLoadState(), QStringLiteral("ready"));
    QTRY_VERIFY(!controller.outingLapsLoading());
    QVariantMap lap;
    for (const auto &value : controller.outingLaps()) {
        const auto row = value.toMap();
        if (row.value("type") == "LAP") { lap = row; break; }
    }
    QVERIFY(!lap.isEmpty());
    QVERIFY(controller.selectOutingLapReference(lap.value("reference").toMap()));
    QTRY_COMPARE(controller.outingLapDetailState(), QStringLiteral("ready"));
    controller.m_videoSource = QUrl::fromLocalFile(QStringLiteral("/synthetic/video.mp4"));
    MediaInfo info;
    info.frameRate = {30, 1};
    info.averageFrameRate = {30, 1};
    info.videoFrameCount = 6000; // 200 seconds, comfortably covering the lap
    info.timeBase = {1, 30};
    info.videoDurationTicks = 6000;
    controller.m_exportSourceInfo = info;
    controller.m_sync = {10.0, 1.0}; // telemetry = video + 10s

    const auto start = lap.value("startTime").toDouble(), end = lap.value("endTime").toDouble();
    // A video position mapping inside the lap moves the cursor there exactly.
    // sync offset is 10 (telemetry = video + 10), so video position (start - 10 + 0.5)s
    // maps to telemetry time (start + 0.5)s.
    QVERIFY(controller.followOutingLapVideoPosition(qRound64((start - 10.0 + 0.5) * 1000.0)));
    QCOMPARE(controller.outingLapCursor(), start + 0.5);

    // A video position mapping before/after the lap clamps to the lap's own
    // bounds (setOutingLapCursor's existing clamp), never runs the cursor
    // outside the lap it belongs to.
    QVERIFY(controller.followOutingLapVideoPosition(qRound64((start - 10.0 - 5.0) * 1000.0)));
    QCOMPARE(controller.outingLapCursor(), start);
    QVERIFY(controller.followOutingLapVideoPosition(qRound64((end - 10.0 + 5.0) * 1000.0)));
    QCOMPARE(controller.outingLapCursor(), end);

    // KAN-124: analysis without a video link (Flapped Ear Telemetry) has no
    // lap video and never follows one; the cursor stays where it is.
    auto *link = controller.m_analysis.m_videoLink;
    controller.m_analysis.m_videoLink = nullptr;
    QVERIFY(!controller.outingLapVideoAvailable());
    QCOMPARE(controller.outingLapVideoPositionMilliseconds(), qint64(0));
    QVERIFY(!controller.followOutingLapVideoPosition(qRound64((start - 10.0 + 0.5) * 1000.0)));
    QCOMPARE(controller.outingLapCursor(), end);
    controller.m_analysis.m_videoLink = link;
    QVERIFY(controller.followOutingLapVideoPosition(qRound64((start - 10.0 + 0.5) * 1000.0)));
    QCOMPARE(controller.outingLapCursor(), start + 0.5);
}

void TelemetryTests::opensRankedLapsAndRecomputesAfterExclusion()
{
    QTemporaryDir directory; QVERIFY(directory.isValid());
    QSettings settings; settings.clear(); settings.sync();
    const auto path = directory.filePath("run.vbo"); QVERIFY(writeBytes(path, EventProjectFixture::lapsVbo()));
    AppController controller(nullptr, directory.filePath("recovery.json"));
    QVERIFY(controller.importAnalysisRuns("Rankings", {QUrl::fromLocalFile(path)}));
    QTRY_COMPARE(controller.vboLoadState(), QString("ready")); QTRY_COMPARE(controller.outingLaps().size(), 5);
    QCOMPARE(controller.outingRanking().value("state").toString(), QString("selection-required"));
    QVERIFY(controller.setRunTrackConfiguration(controller.activeRunId(), "Full", "clockwise"));
    QTRY_VERIFY(!controller.outingLapsLoading() && controller.m_analysis.m_outingLapRequestedKey == controller.m_analysis.outingLapKey());
    const auto group = controller.outingCompatibilityGroups()[0].toMap().value("id").toString();
    QVERIFY(controller.selectOutingComparisonGroup(group));
    QCOMPARE(controller.outingRanking().value("state").toString(), QString("available"));
    const auto original = controller.outingRanking().value("bestOfDay").toMap();
    const auto originalRef = original.value("reference").toMap();
    QCOMPARE(original.value("groupId").toString(), group);
    QCOMPARE(original.value("runId").toString(), controller.activeRunId());
    int badges = 0;
    for (const auto &row : controller.outingLaps()) if (row.toMap().value("bestOfDay").toBool()) ++badges;
    QCOMPARE(badges, 1);
    QQmlEngine engine; engine.rootContext()->setContextProperty("appController", &controller);
    QSignalSpy warnings(&engine, &QQmlEngine::warnings);
    QQmlComponent component(&engine);
    component.setData("import QtQuick\nWindow { width: 760; height: 480; OutingLapPanel { anchors.fill: parent } }",
        QUrl::fromLocalFile(QStringLiteral(ANALYSIS_PANEL_QML_PATH)));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    std::unique_ptr<QObject> object(component.create()); QVERIFY2(object, qPrintable(component.errorString()));
    auto *window = qobject_cast<QQuickWindow *>(object.get()); QVERIFY(window);
    window->show(); QVERIFY(QTest::qWaitForWindowExposed(window));
    auto *day = window->findChild<QQuickItem *>("openBestDayLap");
    auto *details = window->findChild<QQuickItem *>("openOutingRankingDetails");
    auto *list = window->findChild<QQuickItem *>("outingLapList");
    QVERIFY(day); QVERIFY(details); QVERIFY(list);
    QTRY_VERIFY(list->height() >= 66);
    QVERIFY(day->isEnabled()); day->forceActiveFocus(); QTest::keyClick(window, Qt::Key_Space);
    QTRY_COMPARE(controller.outingLapDetailState(), QString("ready"));
    QCOMPARE(controller.selectedOutingLap().value("reference").toMap(), originalRef);
    controller.closeOutingLap();
    details->forceActiveFocus(); QTest::keyClick(window, Qt::Key_Space);
    auto *dialog = window->findChild<QObject *>("outingRankingDialog"); QVERIFY(dialog);
    QTRY_VERIFY(dialog->property("opened").toBool());
    auto *runs = window->findChild<QObject *>("outingBestRuns"); QVERIFY(runs);
    QQuickItem *runButton = nullptr;
    QTRY_VERIFY(QMetaObject::invokeMethod(runs, "itemAtIndex", Q_RETURN_ARG(QQuickItem *, runButton), Q_ARG(int, 0)) && runButton);
    runButton->forceActiveFocus(); QTest::keyClick(window, Qt::Key_Space);
    QTRY_COMPARE(controller.outingLapDetailState(), QString("ready"));
    QCOMPARE(controller.selectedOutingLap().value("reference").toMap(), originalRef);
    QVERIFY(controller.setOutingLapExcluded(originalRef, true, "Traffic"));
    QVERIFY(controller.outingRanking().value("bestOfDay").toMap().value("reference").toMap() != originalRef);
    QCOMPARE(controller.outingRanking().value("excludedLaps").toList().first().toMap().value("userReason").toString(), QString("Traffic"));
    // Every excluded lap remains visible, but an all-excluded group has no winner.
    const auto rows = controller.outingLaps();
    for (const auto &value : rows) if (value.toMap().value("type") == "LAP")
        QVERIFY(controller.setOutingLapExcluded(value.toMap().value("reference").toMap(), true, "Cooldown"));
    QCOMPARE(controller.outingRanking().value("state").toString(), QString("no-eligible-laps"));
    QVERIFY(controller.outingRanking().value("bestOfDay").toMap().isEmpty());
    QCOMPARE(controller.outingRanking().value("excludedLaps").toList().size(), 3);
    QCOMPARE(controller.outingLaps().size(), 5);
    QTRY_VERIFY(!day->isEnabled());
    QVERIFY(day->property("text").toString().contains("No eligible lap"));
    QVERIFY(controller.setOutingLapExcluded(originalRef, false));
    QCOMPARE(controller.outingRanking().value("bestOfDay").toMap().value("reference").toMap(), originalRef);
    controller.m_analysis.m_outingStaleRunIds.insert(controller.activeRunId());
    controller.m_analysis.refreshOutingCompatibility();
    QCOMPARE(controller.outingRanking().value("state").toString(), QString("no-eligible-laps"));
    for (const auto &row : controller.outingLaps()) {
        QVERIFY(!row.toMap().value("bestOfRun").toBool());
        QVERIFY(!row.toMap().value("bestOfDay").toBool());
    }
    controller.m_analysis.m_outingStaleRunIds.clear(); controller.m_analysis.refreshOutingCompatibility();
    QCOMPARE(controller.outingRanking().value("bestOfDay").toMap().value("reference").toMap(), originalRef);
    // Stale generations cannot expose a former winning result during async refresh.
    QVERIFY(controller.setRunTrackConfiguration(controller.activeRunId(), "Changed", "clockwise"));
    QCOMPARE(controller.outingRanking().value("state").toString(), QString("loading"));
    QTRY_VERIFY(!controller.outingLapsLoading() && controller.m_analysis.m_outingLapRequestedKey == controller.m_analysis.outingLapKey());
    QCOMPARE(controller.outingRanking().value("state").toString(), QString("selection-required"));
    const auto savedPath = directory.filePath("day.fetproject");
    QVERIFY(controller.saveProject(QUrl::fromLocalFile(savedPath)));
    QTRY_VERIFY(!controller.outingLapsLoading() && controller.m_analysis.m_outingLapRequestedKey == controller.m_analysis.outingLapKey());
    controller.requestNewProject();
    QTRY_VERIFY(controller.eventRuns().isEmpty());
    QTRY_COMPARE(controller.outingRanking().value("state").toString(), QString("selection-required"));
    QVERIFY(controller.outingRanking().value("bestOfDay").toMap().isEmpty());
    QCOMPARE(warnings.size(), 0);
}

void TelemetryTests::groupsOutingLapsAfterExplicitConfiguration()
{
    QTemporaryDir directory; QVERIFY(directory.isValid());
    QSettings settings; settings.clear(); settings.sync();
    const auto first = directory.filePath("first.vbo"), second = directory.filePath("second.vbo");
    QVERIFY(writeBytes(first, EventProjectFixture::lapsVbo()));
    auto other = EventProjectFixture::lapsVbo(); other.replace("52.0008", "52.0007");
    QVERIFY(writeBytes(second, other));
    AppController controller(nullptr, directory.filePath("recovery.json"));
    QVERIFY(controller.importAnalysisRuns("Compatibility", {QUrl::fromLocalFile(first), QUrl::fromLocalFile(second)}));
    QTRY_COMPARE(controller.vboLoadState(), QString("ready"));
    QTRY_COMPARE(controller.outingLaps().size(), 10);
    QCOMPARE(controller.outingCompatibilityGroups().size(), 2);
    for (const auto &value : controller.outingCompatibilityGroups()) {
        QVERIFY(!value.toMap().value("resolved").toBool());
        QVERIFY(value.toMap().value("eligibleMembers").toList().isEmpty());
        QVERIFY(!controller.selectOutingComparisonGroup(value.toMap().value("id").toString()));
    }
    const auto run1 = controller.eventRuns()[0].toMap().value("id").toString();
    const auto run2 = controller.eventRuns()[1].toMap().value("id").toString();
    const auto confirm = [&](const QString &run, const QString &layout, const QString &direction) {
        return controller.confirmRunTrackConfiguration(run, controller.runTrackConfiguration(run).value("derivationKey").toString(), layout, direction);
    };
    QVERIFY(!controller.confirmRunTrackConfiguration(run1, "stale", "Circuit", "clockwise"));
    const auto previous = controller.runTrackConfiguration(run1);
    QVERIFY(confirm(run1, "Circuit", "clockwise"));
    QVERIFY(!controller.confirmRunTrackConfiguration(run1, previous.value("derivationKey").toString(), "Other", "clockwise"));
    QVERIFY(confirm(run2, "Circuit", "clockwise"));
    QTRY_VERIFY(!controller.outingLapsLoading() && controller.m_analysis.m_outingLapRequestedKey == controller.m_analysis.outingLapKey());
    QCOMPARE(controller.outingCompatibilityGroups().size(), 1);
    const auto group = controller.outingCompatibilityGroups()[0].toMap();
    const auto groupId = group.value("id").toString();
    QCOMPARE(group.value("lapCount").toInt(), 6);
    QCOMPARE(group.value("eligibleLapCount").toInt(), 6);
    QCOMPARE(controller.outingComparisonGroupId(), groupId);
    QVERIFY(controller.selectOutingComparisonGroup(groupId));
    QVariantMap excludedReference;
    for (const auto &value : controller.outingLaps()) {
        const auto row = value.toMap();
        if (row.value("type") == "LAP") {
            QVERIFY(row.value("comparisonEligible").toBool());
            excludedReference = row.value("reference").toMap();
        } else QVERIFY(!row.value("comparisonEligible").toBool());
    }
    QVERIFY(controller.setOutingLapExcluded(excludedReference, true, "Traffic"));
    QCOMPARE(controller.outingCompatibilityGroups()[0].toMap().value("eligibleLapCount").toInt(), 5);
    QCOMPARE(controller.outingCompatibilityGroups()[0].toMap().value("lapCount").toInt(), 6);
    QVERIFY(controller.selectOutingLapReference(excludedReference));
    QTRY_COMPARE(controller.outingLapDetailState(), QString("ready"));
    QVERIFY(controller.selectedOutingLap().value("compatibilityReasons").toStringList().contains("user-exclusion"));
    // GPS and user restrictions coexist rather than replacing one another.
    for (auto &row : controller.m_analysis.m_outingRawLapRows)
        if (row.reference.toVariantMap() == excludedReference) { row.referenceIssue = LapReferenceIssue::GpsGap; row.referenceEligible = false; }
    controller.m_analysis.refreshLapExclusionPolicy();
    const auto reasons = controller.selectedOutingLap().value("compatibilityReasons").toStringList();
    QVERIFY(reasons.contains("incomplete-gps")); QVERIFY(reasons.contains("user-exclusion"));
    // Opposite direction creates a distinct group, even with identical gates/layout.
    QVERIFY(confirm(run2, "Circuit", "counterclockwise"));
    QTRY_VERIFY(!controller.outingLapsLoading() && controller.m_analysis.m_outingLapRequestedKey == controller.m_analysis.outingLapKey());
    QCOMPARE(controller.outingCompatibilityGroups().size(), 2);
    QVERIFY(controller.selectOutingComparisonGroup(groupId));
    for (const auto &value : controller.outingLaps()) {
        const auto row = value.toMap();
        if (row.value("runId") == run2) {
            QVERIFY(row.value("compatibilityReasons").toStringList().contains("opposite-direction"));
            QVERIFY(!row.value("comparisonEligible").toBool());
        }
    }
    // Save As/reopen retains the explicit group decision as well as run configuration.
    const auto savedPath = directory.filePath("day.fetproject");
    QVERIFY(controller.saveProject(QUrl::fromLocalFile(savedPath)));
    QTRY_VERIFY(!controller.outingLapsLoading() && controller.m_analysis.m_outingLapRequestedKey == controller.m_analysis.outingLapKey());
    const auto groups = controller.outingCompatibilityGroups();
    AppController reopened(nullptr, directory.filePath("reopened.json"));
    QTRY_COMPARE(reopened.outingCompatibilityGroups(), groups);
    QCOMPARE(reopened.outingComparisonGroupId(), groupId);
    // New derivation/generation cannot serve stale groups during the timer gap.
    QVERIFY(confirm(run1, "Changed circuit", "clockwise"));
    QVERIFY(controller.outingCompatibilityGroups().isEmpty());
    QVERIFY(!controller.selectOutingComparisonGroup(groupId));
    QTRY_VERIFY(!controller.outingLapsLoading() && controller.m_analysis.m_outingLapRequestedKey == controller.m_analysis.outingLapKey());
    QVERIFY(controller.outingComparisonGroupId().isEmpty());
}

void TelemetryTests::persistsDayDecisionsAndKeepsIndependentDetail()
{
    QTemporaryDir directory; QVERIFY(directory.isValid());
    QSettings settings; settings.clear(); settings.sync();
    const auto first = directory.filePath("first.vbo"), second = directory.filePath("second.vbo");
    QVERIFY(writeBytes(first, EventProjectFixture::lapsVbo()));
    auto other = EventProjectFixture::lapsVbo(); other.replace("52.0008", "52.0007");
    QVERIFY(writeBytes(second, other));
    AppController controller(nullptr, directory.filePath("recovery.json"));
    QVERIFY(controller.importAnalysisRuns("Decisions", {QUrl::fromLocalFile(first), QUrl::fromLocalFile(second)}));
    QTRY_COMPARE(controller.vboLoadState(), QString("ready")); QTRY_COMPARE(controller.outingLaps().size(), 10);
    const auto runA = controller.eventRuns()[0].toMap().value("id").toString();
    const auto runB = controller.eventRuns()[1].toMap().value("id").toString();
    QVERIFY(controller.setRunTrackConfiguration(runA, "Circuit", "clockwise"));
    QVERIFY(controller.setRunTrackConfiguration(runB, "Circuit", "clockwise"));
    QTRY_VERIFY(!controller.outingLapsLoading() && controller.m_analysis.m_outingLapRequestedKey == controller.m_analysis.outingLapKey());
    const auto group = controller.outingCompatibilityGroups().first().toMap().value("id").toString();
    const auto savedPath = directory.filePath("day.fetproject");
    QVERIFY(controller.saveProject(QUrl::fromLocalFile(savedPath)));
    QTRY_VERIFY(!controller.outingLapsLoading() && controller.m_analysis.m_outingLapRequestedKey == controller.m_analysis.outingLapKey());
    QVERIFY(controller.selectOutingComparisonGroup(group));
    QVERIFY(controller.dirty());
    const auto revision = controller.m_document.m_documentState.revision();
    QVERIFY(controller.selectOutingComparisonGroup(group));
    QCOMPARE(controller.m_document.m_documentState.revision(), revision);
    QVariantMap referenceB;
    for (const auto &item : controller.outingLaps()) {
        const auto row = item.toMap();
        if (row.value("runId") == runB && row.value("type") == "LAP") referenceB = row.value("reference").toMap();
    }
    QVERIFY(controller.selectOutingLapReference(referenceB));
    QTRY_COMPARE(controller.outingLapDetailState(), QString("ready"));
    QCOMPARE(controller.m_document.m_documentState.revision(), revision); // Inspection is not a comparison decision.
    controller.setOutingLapCursor(controller.selectedOutingLap().value("startTime").toDouble() + .1);
    const auto detail = controller.m_analysis.m_outingLapDetailSession;
    const auto track = controller.outingLapTrack(); const auto cursor = controller.outingLapCursor();
    const auto serialB = controller.m_analysis.m_outingRunCache.value(runB).derivationSerial;
    QVERIFY(controller.setRunTrackConfiguration(runA, "Other", "counterclockwise"));
    QCOMPARE(controller.m_analysis.m_outingLapDetailSession, detail);
    QCOMPARE(controller.outingLapTrack(), track); QCOMPARE(controller.outingLapCursor(), cursor);
    QTRY_VERIFY(!controller.outingLapsLoading() && controller.m_analysis.m_outingLapRequestedKey == controller.m_analysis.outingLapKey());
    QCOMPARE(controller.m_analysis.m_outingLapDetailSession, detail);
    QCOMPARE(controller.m_analysis.m_outingRunCache.value(runB).derivationSerial, serialB);
    QCOMPARE(controller.outingRanking().value("runs").toList().size(), 1);
    const auto metadata = controller.runMetadata(runA);
    const auto serialA = controller.m_analysis.m_outingRunCache.value(runA).derivationSerial;
    QVERIFY(controller.updateRunMetadata(runA, metadata.value("editToken").toString(), "Morning", "Traffic", "Dry", "Tyres"));
    QCoreApplication::processEvents();
    QCOMPARE(controller.m_analysis.m_outingRunCache.value(runA).derivationSerial, serialA);
    QCOMPARE(controller.m_analysis.m_outingRunCache.value(runB).derivationSerial, serialB);
    QCOMPARE(controller.m_analysis.m_outingLapDetailSession, detail);
    // Replacing A cancels only its dependent detail; B's session/cursor survive.
    const auto replacement = directory.filePath("replacement.vbo");
    QVERIFY(writeBytes(replacement, EventProjectFixture::lapsVbo().replace("52.0008", "52.0006")));
    controller.loadVbo(QUrl::fromLocalFile(replacement));
    QCOMPARE(controller.m_analysis.m_outingLapDetailSession, detail);
    QTRY_COMPARE(controller.vboLoadState(), QString("ready"));
    QTRY_VERIFY(!controller.outingLapsLoading() && controller.m_analysis.m_outingLapRequestedKey == controller.m_analysis.outingLapKey());
    QCOMPARE(controller.m_analysis.m_outingLapDetailSession, detail);
    QCOMPARE(controller.outingLapTrack(), track); QCOMPARE(controller.outingLapCursor(), cursor);
    QCOMPARE(controller.m_analysis.m_outingRunCache.value(runB).derivationSerial, serialB);
    QVERIFY(controller.runTrackConfiguration(runA).value("layoutId").isNull());
    QVERIFY(controller.saveProject(QUrl::fromLocalFile(savedPath)));
    AppController reopened(nullptr, directory.filePath("reopened.json"));
    QTRY_COMPARE(reopened.outingComparisonGroupId(), group);
    QVERIFY(!reopened.dirty()); QVERIFY(reopened.selectedOutingLap().isEmpty());
}

void TelemetryTests::restoresDayDecisionsAfterMoveMissingRelinkAndRecovery()
{
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
    QVariantMap excludedA, excludedB;
    {
        AppController controller(nullptr, recovery);
        QVERIFY(controller.importAnalysisRuns("Portable decisions", {QUrl::fromLocalFile(first), QUrl::fromLocalFile(second)}));
        QTRY_COMPARE(controller.vboLoadState(), QString("ready")); QTRY_COMPARE(controller.outingLaps().size(), 10);
        runA = controller.activeRunId(); runB = controller.eventRuns()[1].toMap().value("id").toString();
        QVERIFY(controller.setRunTrackConfiguration(runA, "Circuit A", "clockwise"));
        QVERIFY(controller.setRunTrackConfiguration(runB, "Circuit B", "counterclockwise"));
        QTRY_VERIFY(!controller.outingLapsLoading() && controller.m_analysis.m_outingLapRequestedKey == controller.m_analysis.outingLapKey());
        for (const auto &value : controller.outingLaps()) {
            const auto row = value.toMap(); if (row.value("type") != "LAP") continue;
            if (row.value("runId") == runA) { excludedA = row.value("reference").toMap(); group = row.value("compatibilityGroupId").toString(); }
            else excludedB = row.value("reference").toMap();
        }
        QVERIFY(controller.setOutingLapExcluded(excludedA, true, "Traffic A"));
        QVERIFY(controller.setOutingLapExcluded(excludedB, true, "Cooldown B"));
        const auto revision = controller.m_document.m_documentState.revision();
        QVERIFY(controller.setOutingLapExcluded(excludedA, true, "Traffic A"));
        QCOMPARE(controller.m_document.m_documentState.revision(), revision); // Entry order is not an edit.
        for (const auto &id : {runA, runB}) {
            auto metadata = controller.runMetadata(id);
            QVERIFY(controller.updateRunMetadata(id, metadata.value("editToken").toString(), metadata.value("name").toString(),
                id == runA ? "Morning notes" : "Afternoon notes", "Dry", "No changes"));
        }
        QVERIFY(controller.selectOutingComparisonGroup(group));
        QVERIFY(controller.saveProject(QUrl::fromLocalFile(savedPath)));
        savedEvent = controller.currentProjectObject().value("event").toObject();
        const auto saveAs = QDir(original).filePath("save-as/day.fetproject");
        QVERIFY(controller.saveProject(QUrl::fromLocalFile(saveAs)));
        QTRY_COMPARE(controller.outingComparisonSelectionState(), QString("applied"));
        const auto saved = QJsonDocument::fromJson(readBytes(saveAs)).object();
        const auto event = saved.value("event").toObject();
        QCOMPARE(event.value("analysisDecisions"), savedEvent.value("analysisDecisions"));
        QCOMPARE(event.value("lapExclusions"), savedEvent.value("lapExclusions"));
        const auto runs = event.value("runs").toArray();
        for (qsizetype i = 0; i < runs.size(); ++i) {
            QCOMPARE(runs[i].toObject().value("id"), savedEvent.value("runs").toArray()[i].toObject().value("id"));
            QCOMPARE(runs[i].toObject().value("trackConfiguration"), savedEvent.value("runs").toArray()[i].toObject().value("trackConfiguration"));
            QCOMPARE(EventProjectFixture::reference(runs[i].toObject()).value("relativePath").toString(),
                i == 0 ? QString("../first.vbo") : QString("../second.vbo"));
        }
    }
    QVERIFY(QDir().rename(original, moved));
    const auto movedProject = QDir(moved).filePath("save-as/day.fetproject");
    settings.setValue("project/path", movedProject); settings.sync();
    {
        AppController controller(nullptr, recovery);
        QTRY_COMPARE(controller.outingComparisonGroupId(), group);
        QTRY_COMPARE(controller.vboLoadState(), QString("ready")); QVERIFY(!controller.dirty());
        QCOMPARE(controller.resolveOutingLapReference(excludedA).value("state").toString(), QString("resolved"));
        QCOMPARE(controller.resolveOutingLapReference(excludedB).value("state").toString(), QString("resolved"));
        QCOMPARE(controller.currentProjectObject().value("event").toObject().value("lapExclusions"), savedEvent.value("lapExclusions"));
        QCOMPARE(controller.runMetadata(runB).value("notes").toString(), QString("Afternoon notes"));
    }
    const auto missing = QDir(moved).filePath("first.vbo"), relocated = QDir(moved).filePath("relocated.vbo");
    QVERIFY(QFile::rename(missing, relocated));
    {
        AppController controller(nullptr, recovery);
        QTRY_COMPARE(controller.vboLoadState(), QString("missing"));
        QTRY_COMPARE(controller.outingComparisonSelectionState(), QString("unavailable"));
        QVERIFY(!controller.dirty()); QVERIFY(controller.outingComparisonGroupId().isEmpty());
        QCOMPARE(controller.currentProjectObject().value("event").toObject().value("analysisDecisions"), savedEvent.value("analysisDecisions"));
        QCOMPARE(controller.resolveOutingLapReference(excludedA).value("state").toString(), QString("unavailable"));
        QCOMPARE(controller.resolveOutingLapReference(excludedB).value("state").toString(), QString("resolved"));
        QQmlEngine engine; engine.rootContext()->setContextProperty("appController", &controller);
        QSignalSpy warnings(&engine, &QQmlEngine::warnings); QQmlComponent component(&engine);
        component.setData("import QtQuick\nWindow { width: 760; height: 480; OutingLapPanel { anchors.fill: parent } }",
            QUrl::fromLocalFile(QStringLiteral(ANALYSIS_PANEL_QML_PATH)));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        std::unique_ptr<QObject> object(component.create()); QVERIFY2(object, qPrintable(component.errorString()));
        auto *window = qobject_cast<QQuickWindow *>(object.get()); QVERIFY(window);
        window->show(); QVERIFY(QTest::qWaitForWindowExposed(window));
        auto *picker = window->findChild<QObject *>("outingComparisonGroupPicker"); QVERIFY(picker);
        QTRY_COMPARE(picker->property("currentIndex").toInt(), -1);
        QVERIFY(picker->property("displayText").toString().contains("unavailable"));
        controller.relinkVbo(QUrl::fromLocalFile(relocated));
        QTRY_COMPARE(controller.vboLoadState(), QString("ready"));
        QTRY_COMPARE(controller.outingComparisonGroupId(), group);
        QTRY_COMPARE(picker->property("currentValue").toString(), group);
        QCOMPARE(controller.resolveOutingLapReference(excludedA).value("state").toString(), QString("resolved"));
        QVERIFY(controller.saveCurrentProject()); QVERIFY(!controller.dirty());
        const auto revision = controller.m_document.m_documentState.revision();
        QVERIFY(controller.selectOutingComparisonGroup(group));
        QCOMPARE(controller.m_document.m_documentState.revision(), revision); QVERIFY(!controller.dirty());
        auto *clear = window->findChild<QQuickItem *>("clearOutingComparisonGroup"); QVERIFY(clear);
        clear->forceActiveFocus(); QTest::keyClick(window, Qt::Key_Space);
        QTRY_COMPARE(controller.outingComparisonSelectionState(), QString("automatic")); QVERIFY(controller.dirty());
        QCOMPARE(warnings.size(), 0);
        controller.m_document.writeRecoverySnapshot(); QVERIFY(QFileInfo::exists(recovery));
    }
    {
        AppController recovered(nullptr, recovery); QVERIFY(recovered.recoveryPending());
        recovered.resolveStartupRecovery("recover");
        QTRY_COMPARE(recovered.vboLoadState(), QString("ready")); QTRY_COMPARE(recovered.outingLaps().size(), 10);
        QVERIFY(recovered.dirty()); QCOMPARE(recovered.outingComparisonSelectionState(), QString("automatic"));
        QVERIFY(recovered.selectOutingComparisonGroup(group));
        recovered.m_document.writeRecoverySnapshot();
    }
    {
        AppController recovered(nullptr, recovery); QVERIFY(recovered.recoveryPending());
        recovered.resolveStartupRecovery("recover");
        QTRY_COMPARE(recovered.outingComparisonGroupId(), group); QVERIFY(recovered.dirty());
        QVERIFY(recovered.saveCurrentProject()); QVERIFY(!recovered.dirty());
    }
    AppController clean(nullptr, recovery);
    QVERIFY(!clean.recoveryPending()); QTRY_COMPARE(clean.outingComparisonGroupId(), group);
    QVERIFY(!clean.dirty()); QVERIFY(clean.selectedOutingLap().isEmpty());
}

void TelemetryTests::rejectsStaleDayDetailCompletion()
{
    QTemporaryDir directory; QVERIFY(directory.isValid());
    QSettings settings; settings.clear(); settings.sync();
    const auto path = directory.filePath("laps.vbo"); QVERIFY(writeBytes(path, EventProjectFixture::lapsVbo()));
    AppController controller(nullptr, directory.filePath("recovery.json"));
    QVERIFY(controller.importAnalysisRuns("Stale detail", {QUrl::fromLocalFile(path)}));
    QTRY_COMPARE(controller.vboLoadState(), QString("ready")); QTRY_COMPARE(controller.outingLaps().size(), 5);
    QVERIFY(controller.selectOutingLap(1));
    controller.m_analysis.m_outingLapDetailTimer.stop();
    AnalysisController::OutingLapDetailResult late; late.request = controller.m_analysis.m_outingLapDetailRequest;
    late.session = std::make_shared<TelemetrySession>(TelemetrySource::load(path));
    QPromise<AnalysisController::OutingLapDetailResult> pending; pending.start();
    controller.m_analysis.m_outingLapDetailPending = true;
    controller.m_analysis.m_outingLapDetailCancellation = std::make_shared<std::atomic_bool>(false);
    const auto cancellation = controller.m_analysis.m_outingLapDetailCancellation;
    controller.m_analysis.m_outingLapDetailWatcher.setFuture(pending.future());
    QVERIFY(controller.setRunTrackConfiguration(controller.activeRunId(), "Changed", "clockwise"));
    QVERIFY(cancellation->load()); QCOMPARE(controller.outingLapDetailState(), QString("idle"));
    pending.addResult(late); pending.finish();
    QTRY_VERIFY(!controller.m_analysis.m_outingLapDetailPending);
    QVERIFY(!controller.m_analysis.m_outingLapDetailSession); QVERIFY(controller.selectedOutingLap().isEmpty());
}

void TelemetryTests::automaticallyGroupsRunsThroughProductionQml()
{
    QTemporaryDir directory; QVERIFY(directory.isValid());
    QSettings settings; settings.clear(); settings.sync();
    const auto first = directory.filePath("first.vbo"), second = directory.filePath("unrelated-name.vbo");
    QVERIFY(writeBytes(first, EventProjectFixture::routeVbo()));
    QVERIFY(writeBytes(second, EventProjectFixture::routeVbo(130, -2, 2)));
    AppController controller(nullptr, directory.filePath("recovery.json"));
    QVERIFY(controller.importAnalysisRuns("Automatic day", {QUrl::fromLocalFile(first), QUrl::fromLocalFile(second)}));
    QTRY_COMPARE(controller.vboLoadState(), QString("ready"));
    QTRY_COMPARE(controller.outingRanking().value("state").toString(), QString("available"));
    QCOMPARE(controller.outingCompatibilityGroups().size(), 1);
    QCOMPARE(controller.outingRanking().value("runs").toList().size(), 2);
    QCOMPARE(controller.outingProgression().value("runs").toList().size(), 2);
    QCOMPARE(controller.outingComparisonSelectionState(), QString("automatic"));
    const auto group = controller.outingComparisonGroupId(); QVERIFY(!group.isEmpty());
    const auto reference = controller.outingRanking().value("bestOfDay").toMap().value("reference").toMap();
    for (const auto &value : controller.eventRuns()) {
        const auto id = value.toMap().value("id").toString();
        QVERIFY(controller.runTrackConfiguration(id).value("layoutId").isNull()); // No fabricated manual override.
    }
    QQmlEngine engine; engine.rootContext()->setContextProperty("appController", &controller);
    QSignalSpy warnings(&engine, &QQmlEngine::warnings); QQmlComponent component(&engine);
    component.setData("import QtQuick\nWindow { width: 760; height: 480; OutingLapPanel { anchors.fill: parent } }",
        QUrl::fromLocalFile(QStringLiteral(ANALYSIS_PANEL_QML_PATH)));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    std::unique_ptr<QObject> object(component.create()); QVERIFY2(object, qPrintable(component.errorString()));
    auto *window = qobject_cast<QQuickWindow *>(object.get()); QVERIFY(window);
    window->show(); QVERIFY(QTest::qWaitForWindowExposed(window));
    auto *best = window->findChild<QQuickItem *>("openBestDayLap"); QVERIFY(best); QVERIFY(best->isEnabled());
    best->forceActiveFocus(); QTest::keyClick(window, Qt::Key_Space);
    QTRY_COMPARE(controller.outingLapDetailState(), QString("ready"));
    QCOMPARE(controller.selectedOutingLap().value("reference").toMap(), reference);
    const auto saved = directory.filePath("day.fetproject"); QVERIFY(controller.saveProject(QUrl::fromLocalFile(saved)));
    const auto event = QJsonDocument::fromJson(readBytes(saved)).object().value("event").toObject();
    for (const auto &value : event.value("runs").toArray()) {
        const auto inference = value.toObject().value("trackInference").toObject();
        QCOMPARE(inference.value("algorithm").toString(), QString(trackInferenceVersion));
        QCOMPARE(inference.value("sourceRevision").toString().size(), 64);
    }
    const auto revision = controller.m_document.m_documentState.revision();
    controller.m_analysis.m_outingLapRequestedKey.clear(); controller.m_analysis.refreshOutingLaps();
    QTRY_COMPARE(controller.outingRanking().value("state").toString(), QString("available"));
    QVERIFY(!controller.dirty()); QCOMPARE(controller.m_document.m_documentState.revision(), revision);
    AppController reopened(nullptr, directory.filePath("reopened.json"));
    QTRY_COMPARE(reopened.outingComparisonGroupId(), group); QVERIFY(!reopened.dirty());
    const auto reversed = directory.filePath("reversed.vbo"), alternative = directory.filePath("alternative.vbo");
    QVERIFY(writeBytes(reversed, EventProjectFixture::routeVbo(240, 1, 0, true)));
    QVERIFY(writeBytes(alternative, EventProjectFixture::routeVbo(240, -1, 0, false, 450)));
    QVERIFY(controller.importAnalysisRuns("", {QUrl::fromLocalFile(reversed), QUrl::fromLocalFile(alternative)}));
    QTRY_COMPARE(controller.eventRuns().size(), 4);
    QTRY_COMPARE(controller.outingCompatibilityGroups().size(), 3);
    for (const auto &value : controller.outingCompatibilityGroups()) {
        const auto groupResult = value.toMap(); QVERIFY(groupResult.value("resolved").toBool());
        QCOMPARE(groupResult.value("ranking").toMap().value("state").toString(), QString("available"));
        QVERIFY(!groupResult.value("progression").toMap().value("runs").toList().isEmpty());
    }
    auto *summaries = window->findChild<QObject *>("automaticGroupResults"); QVERIFY(summaries);
    QTRY_COMPARE(summaries->property("count").toInt(), 3);
    const auto runA = controller.eventRuns()[0].toMap().value("id").toString();
    const auto runB = controller.eventRuns()[1].toMap().value("id").toString();
    const auto beforeCorrection = controller.m_document.m_documentState.revision();
    QVERIFY(controller.confirmRunTrackConfiguration(runA, controller.runTrackConfiguration(runA).value("derivationKey").toString(),
        "Owner corrected layout", "counterclockwise", true));
    QCOMPARE(controller.m_document.m_documentState.revision(), beforeCorrection + 1); // One atomic matching-run correction.
    QTRY_VERIFY(!controller.outingLapsLoading() && controller.m_analysis.m_outingLapRequestedKey == controller.m_analysis.outingLapKey());
    QCOMPARE(controller.runTrackConfiguration(runB).value("layoutId").toString(), QString("Owner corrected layout"));
    QVERIFY(controller.runTrackConfiguration(controller.eventRuns()[2].toMap().value("id").toString()).value("layoutId").isNull());
    QVariantMap referenceB, referenceA; QString correctedGroup;
    for (const auto &value : controller.outingLaps()) {
        const auto row = value.toMap(); if (row.value("type") != "LAP") continue;
        if (row.value("runId") == runB) { referenceB = row.value("reference").toMap(); correctedGroup = row.value("compatibilityGroupId").toString(); }
        if (row.value("runId") == runA) referenceA = row.value("reference").toMap();
    }
    QVERIFY(controller.selectOutingComparisonGroup(correctedGroup));
    QVERIFY(controller.setOutingLapExcluded(referenceA, true, "Traffic"));
    QVERIFY(controller.selectOutingLapReference(referenceB));
    QTRY_COMPARE(controller.outingLapDetailState(), QString("ready"));
    controller.setOutingLapCursor(controller.selectedOutingLap().value("startTime").toDouble() + 1);
    const auto detailB = controller.m_analysis.m_outingLapDetailSession; const auto trackB = controller.outingLapTrack();
    const auto cursorB = controller.outingLapCursor(); const auto serialB = controller.m_analysis.m_outingRunCache.value(runB).derivationSerial;
    const auto replacement = directory.filePath("changed-a.vbo");
    QVERIFY(writeBytes(replacement, EventProjectFixture::routeVbo(200, -1.2, 1, false, 450)));
    controller.loadVbo(QUrl::fromLocalFile(replacement));
    QTRY_COMPARE(controller.vboLoadState(), QString("ready"));
    QTRY_VERIFY(!controller.outingLapsLoading() && controller.m_analysis.m_outingLapRequestedKey == controller.m_analysis.outingLapKey());
    QCOMPARE(controller.m_analysis.m_outingLapDetailSession, detailB); QCOMPARE(controller.outingLapTrack(), trackB);
    QCOMPARE(controller.outingLapCursor(), cursorB); QCOMPARE(controller.m_analysis.m_outingRunCache.value(runB).derivationSerial, serialB);
    QCOMPARE(controller.outingComparisonGroupId(), correctedGroup);
    QCOMPARE(controller.outingRanking().value("runs").toList().size(), 1);
    QCOMPARE(controller.resolveOutingLapReference(referenceA).value("state").toString(), QString("stale"));
    QVERIFY(controller.saveCurrentProject());
    AppController corrected(nullptr, directory.filePath("corrected.json"));
    QTRY_COMPARE(corrected.outingComparisonGroupId(), correctedGroup); QVERIFY(!corrected.dirty());
    QCOMPARE(corrected.runTrackConfiguration(runB).value("layoutId").toString(), QString("Owner corrected layout"));
    QCOMPARE(corrected.resolveOutingLapReference(referenceB).value("state").toString(), QString("resolved"));
    QCOMPARE(corrected.currentProjectObject().value("event").toObject().value("lapExclusions").toArray().size(), 1);
    QCOMPARE(warnings.size(), 0);
}

void TelemetryTests::separatesAutomaticGroupsAfterSourceReplacement()
{
    QTemporaryDir directory; QVERIFY(directory.isValid());
    QSettings settings; settings.clear(); settings.sync();
    const auto first = directory.filePath("first.vbo"), second = directory.filePath("second.vbo");
    QVERIFY(writeBytes(first, EventProjectFixture::routeVbo()));
    QVERIFY(writeBytes(second, EventProjectFixture::routeVbo(240, -1, 0, false, 300, 4, 1)));
    AppController controller(nullptr, directory.filePath("recovery.json"));
    QVERIFY(controller.importAnalysisRuns("Replacement day", {QUrl::fromLocalFile(first), QUrl::fromLocalFile(second)}));
    QTRY_COMPARE(controller.outingRanking().value("state").toString(), QString("available"));
    QCOMPARE(controller.outingRanking().value("eligibleLapCount").toInt(), 5);
    QCOMPARE(controller.outingRanking().value("lapCount").toInt(), 6);
    QCOMPARE(controller.outingCompatibilityGroups().first().toMap().value("eligibleLapCount").toInt(), 5);
    auto ids = controller.m_analysis.m_outingRunCache.keys(); std::sort(ids.begin(), ids.end());
    QCOMPARE(ids.size(), 2);
    const auto oldGroup = controller.outingComparisonGroupId();
    QVERIFY(controller.selectOutingComparisonGroup(oldGroup));
    QVERIFY(controller.selectEventRun(ids.first()));
    QTRY_COMPARE(controller.vboLoadState(), QString("ready"));
    QTRY_VERIFY(!controller.outingLapsLoading());
    const auto otherSerial = controller.m_analysis.m_outingRunCache.value(ids.last()).derivationSerial;
    const auto changed = directory.filePath("different-route.vbo");
    QVERIFY(writeBytes(changed, EventProjectFixture::routeVbo(240, -1, 0, false, 450)));
    controller.loadVbo(QUrl::fromLocalFile(changed));
    QTRY_COMPARE(controller.vboLoadState(), QString("ready"));
    QTRY_VERIFY(!controller.outingLapsLoading());
    QCOMPARE(controller.outingCompatibilityGroups().size(), 2);
    QCOMPARE(controller.outingComparisonGroupId(), oldGroup);
    QCOMPARE(controller.outingRanking().value("runs").toList().size(), 1);
    QCOMPARE(controller.outingRanking().value("runs").toList().first().toMap().value("runId").toString(), ids.last());
    QCOMPARE(controller.m_analysis.m_outingRunCache.value(ids.last()).derivationSerial, otherSerial);
    for (const auto &value : controller.outingCompatibilityGroups())
        QCOMPARE(value.toMap().value("ranking").toMap().value("runs").toList().size(), 1);
    QVERIFY(controller.saveProject(QUrl::fromLocalFile(directory.filePath("day.fetproject"))));
    AppController reopened(nullptr, directory.filePath("reopened.json"));
    QTRY_COMPARE(reopened.outingComparisonGroupId(), oldGroup);
    QCOMPARE(reopened.outingCompatibilityGroups().size(), 2); QVERIFY(!reopened.dirty());
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
    AppController controller(nullptr, directory.filePath("recovery.json"));
    QVERIFY(controller.importAnalysisRuns("Local track day", recordings));
    QTRY_COMPARE_WITH_TIMEOUT(controller.eventRuns().size(), recordings.size(), 120000);
    QTRY_VERIFY_WITH_TIMEOUT(!controller.outingLapsLoading() && controller.m_analysis.m_outingLapRequestedKey == controller.m_analysis.outingLapKey(), 120000);
    for (auto it = controller.m_analysis.m_outingRunCache.cbegin(); it != controller.m_analysis.m_outingRunCache.cend(); ++it)
        qInfo() << "Run route:" << it->inference.route.lengthMeters << it->inference.route.direction
            << "supported laps:" << it->inference.matchingLaps.size() << it->inference.reason;
    qInfo() << "Groups:" << controller.outingCompatibilityGroups().size() << "Ranking:" << controller.outingRanking().value("state")
        << "Eligible:" << controller.outingRanking().value("eligibleLapCount") << controller.outingLapMessages();
    QCOMPARE(controller.outingCompatibilityGroups().size(), 1);
    QCOMPARE(controller.outingRanking().value("state").toString(), QString("available"));
    QCOMPARE(controller.outingRanking().value("runs").toList().size(), recordings.size());
    QCOMPARE(controller.outingProgression().value("runs").toList().size(), recordings.size());
    QCOMPARE(controller.outingCompatibilityGroups().first().toMap().value("eligibleLapCount"),
        controller.outingRanking().value("eligibleLapCount"));
    const auto output = qEnvironmentVariable("FLAPPEDEAR_DAY_REVIEW_PROJECT");
    if (!output.isEmpty()) QVERIFY(controller.saveProject(QUrl::fromLocalFile(output)));
    // Save As rebases source paths and queues verified cache reuse. Capture the
    // published results after that refresh, not its legitimate loading state.
    QTRY_COMPARE_WITH_TIMEOUT(controller.outingRanking().value("state").toString(), QString("available"), 120000);
    const auto screenshot = qEnvironmentVariable("FLAPPEDEAR_DAY_REVIEW_IMAGE");
    if (!screenshot.isEmpty()) {
        QQmlEngine engine; engine.rootContext()->setContextProperty("appController", &controller);
        QSignalSpy warnings(&engine, &QQmlEngine::warnings); QQmlComponent component(&engine);
        component.loadUrl(QUrl::fromLocalFile(QFileInfo(QStringLiteral(ANALYSIS_PANEL_QML_PATH)).dir().filePath("AnalysisWindow.qml")));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        std::unique_ptr<QObject> object(component.createWithInitialProperties({{"videoSource", QUrl{}},
            {"playbackPosition", 0}, {"playbackRunning", false}, {"mediaDuration", 0}}));
        QVERIFY2(object, qPrintable(component.errorString()));
        auto *window = qobject_cast<QQuickWindow *>(object.get()); QVERIFY(window);
        window->resize(1180, 720);
        window->show(); QVERIFY(QTest::qWaitForWindowExposed(window));
        QSignalSpy frame(window, &QQuickWindow::frameSwapped); window->requestUpdate();
        QTRY_VERIFY(frame.size() > 0);
        QVERIFY(window->grabWindow().save(screenshot));
        auto *progression = window->findChild<QQuickItem *>("openOutingProgression"); QVERIFY(progression); QVERIFY(progression->isEnabled());
        progression->forceActiveFocus(); QTest::keyClick(window, Qt::Key_Space);
        auto *dialog = window->findChild<QObject *>("outingProgressionDialog"); QVERIFY(dialog);
        QTRY_VERIFY(dialog->property("opened").toBool());
        frame.clear(); window->requestUpdate(); QTRY_VERIFY(frame.size() > 0);
        QVERIFY(window->grabWindow().save(screenshot + ".progression.png"));
        QVERIFY(QMetaObject::invokeMethod(dialog, "close")); QTRY_VERIFY(!dialog->property("visible").toBool());
        auto *correct = window->findChild<QQuickItem *>("openTrackConfiguration"); QVERIFY(correct);
        correct->forceActiveFocus(); QTest::keyClick(window, Qt::Key_Space);
        auto *configuration = window->findChild<QObject *>("outingTrackConfigurationDialog"); QVERIFY(configuration);
        QTRY_VERIFY(configuration->property("opened").toBool());
        frame.clear(); window->requestUpdate(); QTRY_VERIFY(frame.size() > 0);
        QVERIFY(window->grabWindow().save(screenshot + ".correction.png"));
        auto *inspect = window->findChild<QQuickItem *>("inspectGroupingGpsTrace"); QVERIFY(inspect); QVERIFY(inspect->isEnabled());
        inspect->forceActiveFocus(); QTest::keyClick(window, Qt::Key_Space);
        QTRY_COMPARE_WITH_TIMEOUT(controller.outingLapDetailState(), QString("ready"), 120000);
        QVERIFY(window->property("showingLap").toBool());
        frame.clear(); window->requestUpdate(); QTRY_VERIFY(frame.size() > 0);
        QVERIFY(window->grabWindow().save(screenshot + ".lap.png"));
        const auto excluded = controller.outingRanking().value("excludedLaps").toList();
        for (qsizetype i = 0; i < std::min<qsizetype>(2, excluded.size()); ++i) {
            const auto row = excluded[i].toMap(); qInfo() << "Excluded lap:" << row.value("lapNumber") << row.value("reasonLabels");
            QVERIFY(controller.selectOutingLapReference(row.value("reference").toMap()));
            QTRY_COMPARE_WITH_TIMEOUT(controller.outingLapDetailState(), QString("ready"), 120000);
            frame.clear(); window->requestUpdate(); QTRY_VERIFY(frame.size() > 0);
            QVERIFY(window->grabWindow().save(screenshot + QString(".excluded-%1.png").arg(i)));
        }
        QCOMPARE(warnings.size(), 0);
    }
}

void TelemetryTests::confirmsTrackConfigurationThroughQml()
{
    QTemporaryDir directory; QVERIFY(directory.isValid());
    QSettings settings; settings.clear(); settings.sync();
    const auto path = directory.filePath("run.vbo"); QVERIFY(writeBytes(path, EventProjectFixture::lapsVbo()));
    AppController controller(nullptr, directory.filePath("recovery.json"));
    QVERIFY(controller.importAnalysisRuns("Configuration", {QUrl::fromLocalFile(path)}));
    QTRY_COMPARE(controller.vboLoadState(), QString("ready")); QTRY_COMPARE(controller.outingLaps().size(), 5);
    QQmlEngine engine; engine.rootContext()->setContextProperty("appController", &controller);
    QSignalSpy warnings(&engine, &QQmlEngine::warnings);
    QQmlComponent component(&engine);
    component.setData("import QtQuick\nWindow { width: 1180; height: 720; OutingLapPanel { anchors.fill: parent } }",
        QUrl::fromLocalFile(QStringLiteral(ANALYSIS_PANEL_QML_PATH)));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    std::unique_ptr<QObject> object(component.create()); QVERIFY2(object, qPrintable(component.errorString()));
    auto *window = qobject_cast<QQuickWindow *>(object.get()); QVERIFY(window);
    window->show(); QVERIFY(QTest::qWaitForWindowExposed(window));
    auto *open = window->findChild<QQuickItem *>("openTrackConfiguration"); QVERIFY(open);
    open->forceActiveFocus(); QTest::keyClick(window, Qt::Key_Space);
    auto *dialog = window->findChild<QObject *>("outingTrackConfigurationDialog"); QVERIFY(dialog);
    QTRY_VERIFY(dialog->property("opened").toBool());
    auto *layout = window->findChild<QQuickItem *>("compatibilityLayoutName");
    auto *direction = window->findChild<QQuickItem *>("compatibilityDirectionPicker");
    auto *confirm = window->findChild<QQuickItem *>("confirmTrackConfiguration");
    QVERIFY(layout); QVERIFY(direction); QVERIFY(confirm); QVERIFY(!confirm->isEnabled());
    layout->setProperty("text", "Jastrzab full circuit");
    direction->forceActiveFocus(); QTest::keyClick(window, Qt::Key_End);
    QTRY_COMPARE(direction->property("currentIndex").toInt(), 2);
    QVERIFY(confirm->isEnabled());
    confirm->forceActiveFocus(); QTest::keyClick(window, Qt::Key_Space);
    QTRY_VERIFY(!dialog->property("visible").toBool());
    QTRY_VERIFY(!controller.outingLapsLoading() && controller.m_analysis.m_outingLapRequestedKey == controller.m_analysis.outingLapKey());
    const auto config = controller.runTrackConfiguration(controller.activeRunId());
    QCOMPARE(config.value("layoutId").toString(), QString("Jastrzab full circuit"));
    QCOMPARE(config.value("direction").toString(), QString("counterclockwise"));
    QVERIFY(controller.outingCompatibilityGroups()[0].toMap().value("resolved").toBool());
    auto *picker = window->findChild<QQuickItem *>("outingComparisonGroupPicker"); QVERIFY(picker);
    picker->forceActiveFocus(); QTest::keyClick(window, Qt::Key_End);
    QTRY_VERIFY(!controller.outingComparisonGroupId().isEmpty());
    QVERIFY(controller.dirty());
    QCOMPARE(warnings.size(), 0);
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

void TelemetryTests::excludesAndRestoresLapThroughQml()
{
    QTemporaryDir directory; QVERIFY(directory.isValid());
    QSettings settings; settings.clear(); settings.sync();
    const auto path = directory.filePath("run.vbo"); QVERIFY(writeBytes(path, EventProjectFixture::lapsVbo()));
    AppController controller(nullptr, directory.filePath("recovery.json"));
    QVERIFY(controller.importAnalysisRuns("Exclusions", {QUrl::fromLocalFile(path)}));
    QTRY_COMPARE(controller.vboLoadState(), QString("ready"));
    QTRY_COMPARE(controller.outingLaps().size(), 5);
    QVariantMap best;
    for (const auto &item : controller.outingLaps()) if (item.toMap().value("bestOfRun").toBool()) best = item.toMap();
    QVERIFY(!best.isEmpty()); const auto reference = best.value("reference").toMap();
    QVERIFY(controller.selectOutingLapReference(reference));
    QTRY_COMPARE(controller.outingLapDetailState(), QString("ready"));
    const auto track = controller.outingLapTrack(); const auto cursor = controller.outingLapCursor();
    const auto detailSession = controller.m_analysis.m_outingLapDetailSession;
    const auto before = controller.currentProjectObject();
    QVERIFY(!controller.setOutingLapExcluded(reference, true, " "));
    QVERIFY(!controller.setOutingLapExcluded(controller.outingLaps()[0].toMap().value("reference").toMap(), true, "Traffic"));
    QCOMPARE(controller.currentProjectObject(), before);
    QQmlEngine engine; engine.rootContext()->setContextProperty("appController", &controller);
    QSignalSpy warnings(&engine, &QQmlEngine::warnings);
    QQmlComponent component(&engine);
    component.setData("import QtQuick\nWindow { width: 1180; height: 720; OutingLapDetailPanel { anchors.fill: parent } }",
        QUrl::fromLocalFile(QStringLiteral(ANALYSIS_PANEL_QML_PATH)));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    std::unique_ptr<QObject> object(component.create()); QVERIFY2(object, qPrintable(component.errorString()));
    auto *window = qobject_cast<QQuickWindow *>(object.get()); QVERIFY(window);
    window->show(); QVERIFY(QTest::qWaitForWindowExposed(window));
    auto *reason = window->findChild<QQuickItem *>("lapExclusionReason");
    auto *toggle = window->findChild<QQuickItem *>("toggleLapExclusion");
    QVERIFY(reason); QVERIFY(toggle); QVERIFY(!toggle->isEnabled());
    reason->setProperty("text", "Traffic"); QVERIFY(toggle->isEnabled());
    toggle->forceActiveFocus(); QTest::keyClick(window, Qt::Key_Space);
    QTRY_VERIFY(controller.selectedOutingLap().value("excluded").toBool());
    QCOMPARE(controller.selectedOutingLap().value("exclusionReason").toString(), QString("Traffic"));
    QVERIFY(!controller.selectedOutingLap().value("referenceEligible").toBool());
    QVERIFY(!controller.selectedOutingLap().value("bestOfRun").toBool());
    QCOMPARE(controller.outingLapDetailState(), QString("ready"));
    QCOMPARE(controller.outingLapTrack(), track); QCOMPARE(controller.outingLapCursor(), cursor);
    QCOMPARE(controller.m_analysis.m_outingLapDetailSession, detailSession);
    QCOMPARE(controller.outingLaps().size(), 5);
    const auto bestIndex = best.value("lapNumber").toInt() - 1;
    QVERIFY(!controller.m_lapSession.timedLaps[bestIndex].referenceEligible());
    QVERIFY(controller.m_lapSession.fastestLapIndex != std::optional<qsizetype>(bestIndex));
    QTest::keyClick(window, Qt::Key_Space);
    QTRY_VERIFY(!controller.selectedOutingLap().value("excluded").toBool());
    QVERIFY(controller.selectedOutingLap().value("bestOfRun").toBool());
    QCOMPARE(controller.m_lapSession.fastestLapIndex, std::optional<qsizetype>(bestIndex));
    QCOMPARE(warnings.size(), 0);
}

void TelemetryTests::lapExclusionsSurviveSaveRecoveryAndInvalidateSafely()
{
    QTemporaryDir directory; QVERIFY(directory.isValid());
    QSettings settings; settings.clear(); settings.sync();
    const auto path = directory.filePath("run.vbo"); QVERIFY(writeBytes(path, EventProjectFixture::lapsVbo()));
    const auto savedPath = directory.filePath("day.fetproject"); const auto recoveryPath = directory.filePath("recovery.json");
    QVariantMap reference;
    {
        AppController controller(nullptr, recoveryPath);
        QVERIFY(controller.importAnalysisRuns("Exclusions", {QUrl::fromLocalFile(path)}));
        QTRY_COMPARE(controller.vboLoadState(), QString("ready")); QTRY_COMPARE(controller.outingLaps().size(), 5);
        reference = controller.outingLaps()[1].toMap().value("reference").toMap();
        QVERIFY(controller.setOutingLapExcluded(reference, true, "Traffic"));
        QVERIFY(controller.saveProject(QUrl::fromLocalFile(savedPath)));
        QTRY_COMPARE(controller.resolveOutingLapReference(reference).value("state").toString(), QString("resolved"));
    }
    {
        AppController controller(nullptr, recoveryPath);
        QTRY_COMPARE(controller.vboLoadState(), QString("ready")); QTRY_COMPARE(controller.outingLaps().size(), 5);
        QCOMPARE(controller.outingLaps()[1].toMap().value("exclusionReason").toString(), QString("Traffic"));
        QVERIFY(!controller.m_lapSession.timedLaps[0].referenceEligible());
        QVERIFY(controller.setOutingLapExcluded(reference, true, "Cooldown"));
        controller.m_document.writeRecoverySnapshot(); QVERIFY(QFileInfo::exists(recoveryPath));
    }
    {
        AppController controller(nullptr, recoveryPath); QVERIFY(controller.recoveryPending());
        controller.resolveStartupRecovery("recover");
        QTRY_COMPARE(controller.vboLoadState(), QString("ready")); QTRY_COMPARE(controller.outingLaps().size(), 5);
        QVERIFY(controller.dirty());
        QCOMPARE(controller.outingLaps()[1].toMap().value("exclusionReason").toString(), QString("Cooldown"));
        QVERIFY(!controller.m_lapSession.timedLaps[0].referenceEligible());
        QVERIFY(controller.setRunTrackConfiguration(controller.activeRunId(), "other-layout", "unknown"));
        QTRY_COMPARE(controller.resolveOutingLapReference(reference).value("state").toString(), QString("stale"));
        QTRY_VERIFY(controller.m_analysis.m_outingLapRequestedKey == controller.m_analysis.outingLapKey()
            && !controller.outingLapsLoading() && controller.outingLaps().size() == 5);
        QVERIFY(!controller.outingLaps()[1].toMap().value("excluded").toBool());
        QVERIFY(controller.m_lapSession.timedLaps[0].referenceEligible());
        QVERIFY(controller.outingLapMessages().join(' ').contains("could not be matched"));
        QCOMPARE(controller.currentProjectObject().value("event").toObject().value("lapExclusions").toArray().size(), 1);
        QVERIFY(controller.setOutingLapExcluded(reference, false));
        QVERIFY(controller.currentProjectObject().value("event").toObject().value("lapExclusions").toArray().isEmpty());
        QVERIFY(controller.outingLapMessages().join(' ').contains("could not be matched") == false);
    }
}

void TelemetryTests::lapReferencesSurviveReopenAndReordering()
{
    QTemporaryDir directory; QVERIFY(directory.isValid());
    QSettings settings; settings.clear(); settings.sync();
    const auto path = directory.filePath("run.vbo");
    QVERIFY(writeBytes(path, EventProjectFixture::lapsVbo()));
    AppController controller(nullptr, directory.filePath("recovery.json"));
    QVERIFY(controller.importAnalysisRuns("References", {QUrl::fromLocalFile(path)}));
    QTRY_COMPARE(controller.vboLoadState(), QString("ready"));
    QTRY_COMPARE(controller.outingLaps().size(), 5);
    const auto reference = controller.outingLaps()[1].toMap().value("reference").toMap();
    const auto portable = QJsonDocument::fromJson(QJsonDocument(QJsonObject::fromVariantMap(reference)).toJson()).object().toVariantMap();
    QCOMPARE(portable, reference);
    QVERIFY(!reference.contains("lapNumber"));
    QCOMPARE(controller.resolveOutingLapReference(reference).value("state").toString(), QString("resolved"));
    QVERIFY(controller.selectOutingLapReference(reference));
    QTRY_COMPARE(controller.outingLapDetailState(), QString("ready"));
    const auto before = controller.currentProjectObject();
    auto invalid = reference; invalid.remove("version");
    QCOMPARE(controller.resolveOutingLapReference(invalid).value("state").toString(), QString("invalid"));
    QVERIFY(!controller.selectOutingLapReference(invalid));
    QCOMPARE(controller.currentProjectObject(), before);
    QVERIFY(QDir().mkpath(directory.filePath("moved-project")));
    const auto savedPath = directory.filePath("moved-project/day.fetproject");
    QVERIFY(controller.saveProject(QUrl::fromLocalFile(savedPath)));
    const auto saved = QJsonDocument::fromJson(readBytes(savedPath)).object();
    AppController reopened(nullptr, directory.filePath("reopened-recovery.json"));
    QVERIFY(reopened.m_document.beginProjectLoad(savedPath, saved));
    QCOMPARE(reopened.resolveOutingLapReference(portable).value("state").toString(), QString("loading"));
    QTRY_COMPARE(reopened.outingLaps().size(), 5);
    QCOMPARE(reopened.outingLaps()[1].toMap().value("reference").toMap(), portable);
    // The reference remains valid after ordering and display numbering changes.
    std::reverse(reopened.m_analysis.m_outingLapRows.begin(), reopened.m_analysis.m_outingLapRows.end());
    auto row = reopened.m_analysis.m_outingLapRows[3].toMap(); row.insert("lapNumber", 99); reopened.m_analysis.m_outingLapRows[3] = row;
    QCOMPARE(reopened.resolveOutingLapReference(portable).value("index").toInt(), 3);
    QVERIFY(reopened.selectOutingLapReference(portable));
    QTRY_COMPARE(reopened.outingLapDetailState(), QString("ready"));
    QCOMPARE(reopened.selectedOutingLap().value("reference").toMap(), portable);
    QCOMPARE(reopened.selectedOutingLap().value("lapNumber").toInt(), 99);
    // Failed lookups never choose the nearest time, same number, or another run.
    for (const auto *field : {"eventId", "runId", "sourceId", "algorithm", "derivationKey", "sourceRevision", "startTime", "type"}) {
        auto stale = portable;
        stale.insert(field, QString(field) == "startTime" ? QVariant(portable.value(field).toDouble() + .001)
            : QString(field) == "type" ? QVariant("IN")
            : QString(field).endsWith("Key") || QString(field) == "sourceRevision" ? QVariant(QString(64, '0')) : QVariant("changed"));
        QCOMPARE(reopened.resolveOutingLapReference(stale).value("state").toString(), QString("stale"));
        QVERIFY(!reopened.selectOutingLapReference(stale));
        QCOMPARE(reopened.selectedOutingLap().value("reference").toMap(), portable);
    }
    // Two identical candidates are ambiguous, never selected arbitrarily.
    reopened.m_analysis.m_outingLapRows.append(reopened.m_analysis.m_outingLapRows[3]);
    QCOMPARE(reopened.resolveOutingLapReference(portable).value("state").toString(), QString("stale"));
    QVERIFY(!reopened.selectOutingLapReference(portable));
}

void TelemetryTests::lapReferencesRejectSourceAndGateChanges()
{
    QTemporaryDir directory; QVERIFY(directory.isValid());
    QSettings settings; settings.clear(); settings.sync();
    const auto path = directory.filePath("run.vbo");
    QVERIFY(writeBytes(path, EventProjectFixture::lapsVbo()));
    AppController controller(nullptr, directory.filePath("recovery.json"));
    QVERIFY(controller.importAnalysisRuns("Stale references", {QUrl::fromLocalFile(path)}));
    QTRY_COMPARE(controller.vboLoadState(), QString("ready")); QTRY_COMPARE(controller.outingLaps().size(), 5);
    const auto reference = controller.outingLaps()[1].toMap().value("reference").toMap();
    const auto savedPath = directory.filePath("day.fetproject"); QVERIFY(controller.saveProject(QUrl::fromLocalFile(savedPath)));
    const auto saved = QJsonDocument::fromJson(readBytes(savedPath)).object();
    QVERIFY(controller.setRunTrackConfiguration(controller.activeRunId(), "layout", "clockwise"));
    // Immediate rejection before the queued refresh clears the previous rows.
    QCOMPARE(controller.resolveOutingLapReference(reference).value("state").toString(), QString("stale"));
    QVERIFY(!controller.selectOutingLapReference(reference)); QVERIFY(!controller.selectOutingLap(1));
    QTRY_VERIFY(!controller.outingLapsLoading() && !controller.outingLaps().isEmpty()
        && controller.m_analysis.m_outingLapRequestedKey == controller.m_analysis.outingLapKey());
    auto project = controller.currentProjectObject(); auto runs = EventProjectFixture::runs(project); auto run = runs[0].toObject();
    const auto configuredReference = controller.outingLaps()[1].toMap().value("reference").toMap();
    auto config = EventProjectCodec::trackConfiguration(run); config.insert("gateRevision", "gates-v1:" + QString(64, '0'));
    run.insert("trackConfiguration", config); runs[0] = run; EventProjectFixture::setRuns(project, runs);
    controller.m_document.m_projectTemplate = project; controller.markPersistentChange();
    QCOMPARE(controller.resolveOutingLapReference(configuredReference).value("state").toString(), QString("stale"));
    QVERIFY(!controller.selectOutingLapReference(configuredReference));
    // Ordinary reopening of the original derivation still resolves its original reference.
    QVERIFY(controller.m_document.beginProjectLoad(savedPath, saved));
    QTRY_COMPARE(controller.resolveOutingLapReference(reference).value("state").toString(), QString("resolved"));
    QVERIFY(controller.selectOutingLapReference(reference)); QTRY_COMPARE(controller.outingLapDetailState(), QString("ready"));
    auto changedGates = EventProjectFixture::lapsVbo(); changedGates.replace("Start 21.0000", "Start 21.0001");
    QVERIFY(writeBytes(path, changedGates));
    QVERIFY(controller.selectOutingLapReference(reference)); QTRY_COMPARE(controller.outingLapDetailState(), QString("error"));
    QVERIFY(controller.outingLapDetailError().contains("stale"));
    QVERIFY(controller.outingLapSeries("latitude", 200).isEmpty());
    QCOMPARE(controller.resolveOutingLapReference(reference).value("state").toString(), QString("stale"));
    controller.relinkVbo(QUrl::fromLocalFile(path));
    QTRY_COMPARE(controller.vboLoadState(), QString("mismatch"));
    controller.resolveSourceMismatch(true);
    QTRY_COMPARE(controller.vboLoadState(), QString("ready"));
    QCOMPARE(controller.resolveOutingLapReference(reference).value("state").toString(), QString("stale"));
    QVERIFY(!controller.selectOutingLapReference(reference));
    // Missing source data is unavailable, not reassigned to another section.
    QVERIFY(QFile::remove(path)); QVERIFY(controller.m_document.beginProjectLoad(savedPath, saved));
    QTRY_COMPARE(controller.resolveOutingLapReference(reference).value("state").toString(), QString("unavailable"));
    QVERIFY(!controller.selectOutingLapReference(reference));
}

void TelemetryTests::lapReferencesDetectUnsampledContentChanges()
{
    QTemporaryDir directory; QVERIFY(directory.isValid());
    QSettings settings; settings.clear(); settings.sync();
    auto bytes = EventProjectFixture::lapsVbo() + "[comments]\n";
    for (int i = 0; i < 512; ++i) bytes += QByteArray(1023, 'a') + '\n';
    const auto path = directory.filePath("large.vbo"); QVERIFY(writeBytes(path, bytes));
    const auto sampled = ProjectSourceReferenceCodec::telemetryFingerprint(path, TelemetrySource::load(path));
    const auto full = TelemetrySource::contentSha256(path, bytes.size());
    QVERIFY_THROWS_EXCEPTION(OperationCancelled, static_cast<void>(TelemetrySource::contentSha256(path, bytes.size(), [] { return true; })));
    QVERIFY_THROWS_EXCEPTION(ResourceLimitError, static_cast<void>(TelemetrySource::contentSha256(path, 128LL * 1024 * 1024 + 1)));
    QVERIFY_THROWS_EXCEPTION(std::runtime_error, static_cast<void>(TelemetrySource::contentSha256(path, bytes.size() - 1)));
    AppController controller(nullptr, directory.filePath("recovery.json"));
    QVERIFY(controller.importAnalysisRuns("Content identity", {QUrl::fromLocalFile(path)}));
    QTRY_COMPARE(controller.vboLoadState(), QString("ready")); QTRY_COMPARE(controller.outingLaps().size(), 5);
    QVERIFY(controller.setRunTrackConfiguration(controller.activeRunId(), "Circuit", "clockwise"));
    QTRY_VERIFY(!controller.outingLapsLoading() && controller.m_analysis.m_outingLapRequestedKey == controller.m_analysis.outingLapKey());
    const auto group = controller.outingCompatibilityGroups().first().toMap().value("id").toString();
    QVERIFY(controller.selectOutingComparisonGroup(group));
    const auto reference = controller.outingLaps()[1].toMap().value("reference").toMap();
    QVERIFY(controller.setOutingLapExcluded(reference, true, "Traffic"));
    const auto savedPath = directory.filePath("content.fetproject");
    QVERIFY(controller.saveProject(QUrl::fromLocalFile(savedPath)));
    QTRY_VERIFY(!controller.outingLapsLoading() && controller.m_analysis.m_outingLapRequestedKey == controller.m_analysis.outingLapKey());
    const auto saved = QJsonDocument::fromJson(readBytes(savedPath)).object();
    QCOMPARE(reference.value("sourceRevision").toString().toLatin1(), full.toHex());
    QVERIFY(controller.selectOutingLapReference(reference));
    QTRY_COMPARE(controller.outingLapDetailState(), QString("ready"));
    QVERIFY(controller.m_analysis.m_analysisSourceCache->usedBytes() > 0); // Prime the verified cache before mutation.
    bytes[100000] = 'b'; QVERIFY(writeBytes(path, bytes));
    QCOMPARE(ProjectSourceReferenceCodec::telemetryFingerprint(path, TelemetrySource::load(path)), sampled);
    QVERIFY(TelemetrySource::contentSha256(path, bytes.size()) != full);
    QVERIFY(controller.selectOutingLapReference(reference));
    QTRY_COMPARE(controller.outingLapDetailState(), QString("error"));
    QVERIFY(controller.outingLapDetailError().contains("stale"));
    QVERIFY(controller.outingLapTrack().isEmpty());
    QCOMPARE(controller.resolveOutingLapReference(reference).value("state").toString(), QString("stale"));
    QVERIFY(!controller.selectOutingLapReference(reference));
    // An unchanged sampled fingerprint cannot authorize changed complete content.
    controller.m_analysis.m_outingLapRequestedKey.clear(); controller.m_analysis.refreshOutingLaps();
    QTRY_VERIFY(!controller.outingLapsLoading());
    QVERIFY(controller.outingLaps().isEmpty());
    QCOMPARE(controller.resolveOutingLapReference(reference).value("state").toString(), QString("unavailable"));
    QVERIFY(controller.outingLapMessages().join(' ').contains("complete recording content differs"));
    QVERIFY(!controller.selectOutingLapReference(reference));
    QCOMPARE(controller.outingComparisonSelectionState(), QString("unavailable"));
    QVERIFY(controller.m_document.beginProjectLoad(savedPath, saved));
    QTRY_COMPARE(controller.vboLoadState(), QString("mismatch"));
    QTRY_COMPARE(controller.outingComparisonSelectionState(), QString("unavailable"));
    QVERIFY(!controller.dirty()); QVERIFY(controller.channelNames().isEmpty());
    QCOMPARE(controller.currentProjectObject().value("event"), saved.value("event"));
    controller.relinkVbo(QUrl::fromLocalFile(path));
    QTRY_COMPARE(controller.vboLoadState(), QString("mismatch"));
    controller.resolveSourceMismatch(true); // Explicitly accept changed content as a replacement.
    QTRY_COMPARE(controller.vboLoadState(), QString("ready"));
    QTRY_COMPARE(controller.outingLaps().size(), 5);
    QCOMPARE(controller.resolveOutingLapReference(reference).value("state").toString(), QString("stale"));
    QVERIFY(controller.runTrackConfiguration(controller.activeRunId()).value("layoutId").isNull());
    QVERIFY(!controller.outingLaps()[1].toMap().value("excluded").toBool());
    QCOMPARE(controller.currentProjectObject().value("event").toObject().value("lapExclusions"),
        saved.value("event").toObject().value("lapExclusions"));
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

void TelemetryTests::ordersWholeOutingAndReopensSources()
{
    QTemporaryDir directory; QVERIFY(directory.isValid());
    QSettings settings; settings.clear(); settings.sync();
    const auto recording = [](int hour) {
        auto text = QString::fromUtf8(EventProjectFixture::lapsVbo());
        text.replace("Start 21.0000 52.0000 21.0000 52.0002 start", "Start 1260.0000 3120.006 1260.0195 3120.006 start");
        text.replace("coordinate units = degrees", "coordinate units = arc-minutes");
        const auto data = text.indexOf("[data]\n") + 7;
        auto lines = text.mid(data).split('\n', Qt::SkipEmptyParts);
        for (auto &line : lines) {
            auto cells = line.split(' ');
            cells[0] = QTime(hour, 0).addMSecs(qRound(cells[0].toDouble() * 1000)).toString("HHmmss.zzz");
            cells[1] = QString::number(cells[1].toDouble() * 60.0, 'f', 9);
            cells[2] = QString::number(cells[2].toDouble() * 60.0, 'f', 9);
            line = cells.join(' ');
        }
        return (QString("File created on 29/08/2026 at %1:00:00\n[comments]\nGenerated by RaceChrono Pro v10.2.4\n").arg(hour, 2, 10, QChar('0'))
            + text.first(data) + lines.join('\n') + '\n').toUtf8();
    };
    const auto late = directory.filePath("late.vbo"); const auto early = directory.filePath("early.vbo");
    QVERIFY(writeBytes(late, recording(15))); QVERIFY(writeBytes(early, recording(9)));
    AppController controller(nullptr, directory.filePath("recovery.json"));
    QVERIFY(controller.importAnalysisRuns("Whole day", {QUrl::fromLocalFile(late), QUrl::fromLocalFile(early)}));
    QTRY_COMPARE(controller.outingLaps().size(), 10);
    QVERIFY(!controller.outingLapsLoading());
    QCOMPARE(controller.outingLapMessages().size(), 2);
    for (const auto &message : controller.outingLapMessages()) QVERIFY(message.contains("repeated, complete GPS laps"));
    const auto rows = controller.outingLaps();
    // KAN-119: named by recording time, not import order or filename.
    QCOMPARE(rows[0].toMap().value("runName").toString(), QStringLiteral("Session 1"));
    QCOMPARE(rows[0].toMap().value("type").toString(), QStringLiteral("OUT"));
    QCOMPARE(rows[4].toMap().value("type").toString(), QStringLiteral("IN"));
    QCOMPARE(rows[5].toMap().value("runName").toString(), QStringLiteral("Session 2"));
    const auto path = directory.filePath("day.fetproject");
    QTRY_COMPARE(controller.vboLoadState(), QStringLiteral("ready"));
    QVERIFY(controller.saveProject(QUrl::fromLocalFile(path)));
    QVERIFY(controller.m_document.beginProjectLoad(path, QJsonDocument::fromJson(readBytes(path)).object()));
    QTRY_COMPARE(controller.outingLaps(), rows);
    QVERIFY(QFile::remove(early));
    QVERIFY(controller.m_document.beginProjectLoad(path, QJsonDocument::fromJson(readBytes(path)).object()));
    QTRY_COMPARE(controller.outingLaps().size(), 5);
    QCOMPARE(controller.outingLapMessages().size(), 2);
    QVERIFY(controller.outingLapMessages().join(' ').contains("missing"));
    QVERIFY(controller.outingLapMessages().join(' ').contains("repeated, complete GPS laps"));
    QVERIFY(writeBytes(late, recording(16)));
    QVERIFY(controller.m_document.beginProjectLoad(path, QJsonDocument::fromJson(readBytes(path)).object()));
    QTRY_VERIFY(!controller.outingLapsLoading() && controller.outingLapMessages().size() == 2);
    QVERIFY(controller.outingLaps().isEmpty());
    QVERIFY(controller.outingLapMessages().join(' ').contains("identity"));
    // A completed old worker result must not repopulate a newly cleared document.
    AnalysisController::OutingLapResult stale;
    stale.key = controller.m_analysis.outingLapKey(); stale.generation = controller.m_document.m_sourceGeneration;
    stale.rows = {{"stale", "Stale", LapSectionType::Lap, 1, 0, 10, {}, 0}};
    QPromise<AnalysisController::OutingLapResult> promise; promise.start();
    controller.m_analysis.m_outingLapWatcher.setFuture(promise.future());
    controller.requestNewProject();
    promise.addResult(stale); promise.finish();
    QTRY_VERIFY(controller.outingLaps().isEmpty());
    QTRY_VERIFY(!controller.m_analysis.m_outingLapWatcher.isRunning());
    QVERIFY(controller.eventRuns().isEmpty());
}

void TelemetryTests::selectsIndependentComparisonLapsThroughQml()
{
    QTemporaryDir directory; QVERIFY(directory.isValid());
    QSettings settings; settings.clear(); settings.sync();
    const auto first = directory.filePath("morning.vbo"), second = directory.filePath("afternoon.vbo");
    const auto reverse = directory.filePath("reverse.vbo");
    QVERIFY(writeBytes(first, EventProjectFixture::routeVbo()));
    QVERIFY(writeBytes(second, EventProjectFixture::routeVbo(130, -2, 2)));
    QVERIFY(writeBytes(reverse, EventProjectFixture::routeVbo(240, 1, 0, true)));
    AppController controller(nullptr, directory.filePath("recovery.json"));
    QVERIFY(controller.importAnalysisRuns("Comparison", {QUrl::fromLocalFile(first), QUrl::fromLocalFile(second), QUrl::fromLocalFile(reverse)}));
    QTRY_COMPARE(controller.vboLoadState(), QString("ready"));
    QTRY_VERIFY(!controller.outingLapsLoading());
    const auto candidates = controller.comparisonLaps(); QVERIFY(candidates.size() >= 3);
    QVariantMap a, b, incompatible;
    for (const auto &value : candidates) {
        const auto row = value.toMap();
        if (a.isEmpty()) a = row;
        else if (row.value("compatibilityGroupId") != a.value("compatibilityGroupId")) incompatible = row;
        else if (row.value("runId") != a.value("runId")) b = row;
    }
    QVERIFY(!a.isEmpty() && !b.isEmpty() && !incompatible.isEmpty());
    controller.setSyncOffset(19); controller.setTimeScale(1.3); controller.setPlaybackTime(7);
    QVERIFY(controller.saveProject(QUrl::fromLocalFile(directory.filePath("day.fetproject"))));
    QTRY_VERIFY(!controller.outingLapsLoading());
    const auto before = controller.currentProjectObject(); const auto active = controller.activeRunId();
    const auto revision = controller.m_document.m_documentState.revision();
    QQmlEngine engine; engine.rootContext()->setContextProperty("appController", &controller);
    QSignalSpy warnings(&engine, &QQmlEngine::warnings); QQmlComponent component(&engine);
    component.setData("import QtQuick\nWindow { width: 760; height: 480; OutingLapPanel { anchors.fill: parent } }",
        QUrl::fromLocalFile(QStringLiteral(ANALYSIS_PANEL_QML_PATH)));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    std::unique_ptr<QObject> object(component.create()); QVERIFY2(object, qPrintable(component.errorString()));
    auto *window = qobject_cast<QQuickWindow *>(object.get()); QVERIFY(window);
    window->show(); QVERIFY(QTest::qWaitForWindowExposed(window));
    auto *open = window->findChild<QQuickItem *>("openComparisonLaps"); QVERIFY(open);
    open->forceActiveFocus(); QTest::keyClick(window, Qt::Key_Space);
    auto *dialog = window->findChild<QObject *>("comparisonLapDialog"); QVERIFY(dialog);
    QTRY_VERIFY(dialog->property("opened").toBool());
    auto *entries = window->findChild<QObject *>("comparisonSlotEntries"); QVERIFY(entries);
    QQuickItem *entryA = nullptr, *entryB = nullptr;
    QTRY_VERIFY(QMetaObject::invokeMethod(entries, "itemAt", Q_RETURN_ARG(QQuickItem *, entryA), Q_ARG(int, 0)) && entryA);
    QTRY_VERIFY(QMetaObject::invokeMethod(entries, "itemAt", Q_RETURN_ARG(QQuickItem *, entryB), Q_ARG(int, 1)) && entryB);
    auto *pickerA = entryA->findChild<QObject *>("comparisonLapPicker0");
    auto *pickerB = entryB->findChild<QObject *>("comparisonLapPicker1"); QVERIFY(pickerA && pickerB);
    // Exercise the production selectors, including a different compatible run.
    QVERIFY(QMetaObject::invokeMethod(pickerA, "activated", Q_ARG(int, 0)));
    QTRY_COMPARE(controller.comparisonSlots()[0].toMap().value("state").toString(), QString("ready"));
    QCOMPARE(controller.comparisonSlots()[0].toMap().value("lap").toMap().value("reference"), a.value("reference"));
    const auto model = pickerB->property("model");
    const auto labels = model.metaType() == QMetaType::fromType<QJSValue>()
        ? model.value<QJSValue>().toVariant().toList() : model.toList();
    int bIndex = -1;
    for (qsizetype i = 0; i < labels.size(); ++i) if (labels[i].toString() == b.value("label").toString()) bIndex = static_cast<int>(i);
    QVERIFY(bIndex >= 0);
    QVERIFY(QMetaObject::invokeMethod(pickerB, "activated", Q_ARG(int, bIndex)));
    QTRY_VERIFY(controller.comparisonPairReady());
    const auto sessionA = controller.m_analysis.m_comparisonSlots[0].session;
    const auto sessionB = controller.m_analysis.m_comparisonSlots[1].session;
    QVERIFY(sessionA && sessionB && sessionA != sessionB);
    QVERIFY2(!controller.comparisonLapTrack(0).isEmpty(), "comparison slot 0 track should not be empty");
    QVERIFY2(!controller.comparisonLapTrack(1).isEmpty(), "comparison slot 1 track should not be empty");
    {
        QQmlComponent mapComponent(&engine);
        mapComponent.setData("import QtQuick\nTrackMapPanel { comparisonSlot: 0; width: 200; height: 200 }",
            QUrl::fromLocalFile(QStringLiteral(ANALYSIS_PANEL_QML_PATH)));
        QVERIFY2(mapComponent.isReady(), qPrintable(mapComponent.errorString()));
        std::unique_ptr<QObject> mapObject(mapComponent.create());
        QVERIFY2(mapObject, qPrintable(mapComponent.errorString()));
        QVERIFY2(!mapObject->property("pathSegments").toList().isEmpty(),
            "TrackMapPanel.pathSegments should not be empty for a ready comparison slot");
    }
    QVERIFY(!controller.selectComparisonLap(1, incompatible.value("reference").toMap()));
    QCOMPARE(controller.m_analysis.m_comparisonSlots[1].session, sessionB);
    auto *swap = window->findChild<QQuickItem *>("swapComparisonLaps"); QVERIFY(swap);
    swap->forceActiveFocus(); QTest::keyClick(window, Qt::Key_Space);
    QCOMPARE(controller.m_analysis.m_comparisonSlots[0].session, sessionB);
    QCOMPARE(controller.m_analysis.m_comparisonSlots[1].session, sessionA);
    auto *bestRun = window->findChild<QQuickItem *>("bestRunAsComparisonB"); QVERIFY(bestRun);
    bestRun->forceActiveFocus(); QTest::keyClick(window, Qt::Key_Space);
    QTRY_VERIFY(controller.comparisonPairReady());
    QCOMPARE(controller.m_analysis.m_comparisonSlots[1].row.value("runId"), b.value("runId"));
    for (const auto &groupValue : controller.outingCompatibilityGroups()) {
        const auto group = groupValue.toMap();
        if (group.value("id") != b.value("compatibilityGroupId")) continue;
        for (const auto &runValue : group.value("ranking").toMap().value("runs").toList()) {
            const auto run = runValue.toMap();
            if (run.value("runId") == b.value("runId"))
                QCOMPARE(controller.m_analysis.m_comparisonSlots[1].row.value("reference"), run.value("bestLap").toMap().value("reference"));
        }
    }
    auto *bestDay = window->findChild<QQuickItem *>("bestDayAsComparisonB"); QVERIFY(bestDay);
    bestDay->forceActiveFocus(); QTest::keyClick(window, Qt::Key_Space);
    QTRY_VERIFY(controller.comparisonPairReady());
    for (const auto &groupValue : controller.outingCompatibilityGroups()) {
        const auto group = groupValue.toMap();
        if (group.value("id") == b.value("compatibilityGroupId"))
            QCOMPARE(controller.m_analysis.m_comparisonSlots[1].row.value("reference"), group.value("ranking").toMap().value("bestOfDay").toMap().value("reference"));
    }
    auto *inspect = entryA->findChild<QQuickItem *>("inspectComparison0"); QVERIFY(inspect);
    inspect->forceActiveFocus(); QTest::keyClick(window, Qt::Key_Space);
    QTRY_COMPARE(controller.outingLapDetailState(), QString("ready"));
    QCOMPARE(controller.selectedOutingLap().value("reference"), b.value("reference"));
    QCOMPARE(controller.m_analysis.m_comparisonSlots[0].session, sessionB);
    QCOMPARE(controller.activeRunId(), active); QCOMPARE(controller.syncOffset(), 19.0); QCOMPARE(controller.timeScale(), 1.3);
    // Selecting/swapping/inspecting comparison laps must not touch sync, active
    // run or editor state, but the A/B pick itself is now a persisted analysis
    // decision (KAN-41), so the document differs from `before` by exactly that
    // and is dirty, unlike sync/playback edits which stay ephemeral.
    auto afterEvent = before.value("event").toObject();
    auto afterDecisions = afterEvent.value("analysisDecisions").toObject();
    afterDecisions.insert("comparisonSlots", QJsonArray{
        QJsonObject::fromVariantMap(controller.m_analysis.m_comparisonSlots[0].row).value("reference"),
        QJsonObject::fromVariantMap(controller.m_analysis.m_comparisonSlots[1].row).value("reference")});
    afterEvent.insert("analysisDecisions", afterDecisions);
    auto after = before; after.insert("event", afterEvent);
    QCOMPARE(controller.currentProjectObject(), after);
    QVERIFY(controller.m_document.m_documentState.revision() != revision);
    QVERIFY(controller.dirty());
    controller.closeOutingLap();
    QVERIFY(controller.setOutingLapExcluded(b.value("reference").toMap(), true, "Traffic"));
    QCOMPARE(controller.m_analysis.m_comparisonSlots[0].state, QString("error"));
    QVERIFY(!controller.comparisonPairReady());
    QCOMPARE(warnings.size(), 0);
}

void TelemetryTests::restoresComparisonSelectionAfterReopen()
{
    QTemporaryDir directory; QVERIFY(directory.isValid());
    QSettings settings; settings.clear(); settings.sync();
    const auto first = directory.filePath("morning.vbo"), second = directory.filePath("afternoon.vbo");
    QVERIFY(writeBytes(first, EventProjectFixture::routeVbo()));
    QVERIFY(writeBytes(second, EventProjectFixture::routeVbo(130, -2, 2)));
    AppController controller(nullptr, directory.filePath("recovery.json"));
    QVERIFY(controller.importAnalysisRuns("RoundTrip", {QUrl::fromLocalFile(first), QUrl::fromLocalFile(second)}));
    QTRY_COMPARE(controller.vboLoadState(), QString("ready"));
    QTRY_VERIFY(!controller.outingLapsLoading());
    const auto candidates = controller.comparisonLaps(); QVERIFY(candidates.size() >= 2);
    QVariantMap a, b;
    for (const auto &value : candidates) {
        const auto row = value.toMap();
        if (a.isEmpty()) a = row;
        else if (b.isEmpty() && row.value("runId") != a.value("runId")
            && row.value("compatibilityGroupId") == a.value("compatibilityGroupId")) b = row;
    }
    QVERIFY(!a.isEmpty() && !b.isEmpty());
    QVERIFY(controller.selectComparisonLap(0, a.value("reference").toMap()));
    QVERIFY(controller.selectComparisonLap(1, b.value("reference").toMap()));
    QTRY_VERIFY(controller.comparisonPairReady());
    const auto referenceA = a.value("reference").toMap(), referenceB = b.value("reference").toMap();
    QVERIFY(controller.saveProject(QUrl::fromLocalFile(directory.filePath("day.fetproject"))));

    AppController reopened(nullptr, directory.filePath("reopened-recovery.json"));
    QTRY_VERIFY(!reopened.projectLoading());
    QTRY_VERIFY(!reopened.outingLapsLoading());
    QTRY_COMPARE(reopened.comparisonSlots()[0].toMap().value("state").toString(), QString("ready"));
    QTRY_COMPARE(reopened.comparisonSlots()[1].toMap().value("state").toString(), QString("ready"));
    QCOMPARE(reopened.comparisonSlots()[0].toMap().value("lap").toMap().value("reference").toMap(), referenceA);
    QCOMPARE(reopened.comparisonSlots()[1].toMap().value("lap").toMap().value("reference").toMap(), referenceB);
    QVERIFY(!reopened.dirty());

    // A cleared slot's persisted reference must also survive reopen as empty,
    // not silently resurrect the previous pick.
    reopened.clearComparisonLap(0);
    QVERIFY(reopened.saveCurrentProject());
    AppController reopenedAgain(nullptr, directory.filePath("reopened-again-recovery.json"));
    QTRY_VERIFY(!reopenedAgain.projectLoading());
    QTRY_VERIFY(!reopenedAgain.outingLapsLoading());
    QTRY_COMPARE(reopenedAgain.comparisonSlots()[1].toMap().value("state").toString(), QString("ready"));
    QCOMPARE(reopenedAgain.comparisonSlots()[0].toMap().value("state").toString(), QString("empty"));
}

void TelemetryTests::restoresComparisonRangeAndChannelsAfterReopen()
{
    QTemporaryDir directory; QVERIFY(directory.isValid());
    QSettings settings; settings.clear(); settings.sync();
    const auto first = directory.filePath("morning.vbo"), second = directory.filePath("afternoon.vbo");
    QVERIFY(writeBytes(first, EventProjectFixture::routeVbo()));
    QVERIFY(writeBytes(second, EventProjectFixture::routeVbo(130, -2, 2)));
    AppController controller(nullptr, directory.filePath("recovery.json"));
    QVERIFY(controller.importAnalysisRuns("RoundTrip", {QUrl::fromLocalFile(first), QUrl::fromLocalFile(second)}));
    QTRY_COMPARE(controller.vboLoadState(), QString("ready"));
    QTRY_VERIFY(!controller.outingLapsLoading());
    const auto candidates = controller.comparisonLaps(); QVERIFY(candidates.size() >= 2);
    QVariantMap a, b;
    for (const auto &value : candidates) {
        const auto row = value.toMap();
        if (a.isEmpty()) a = row;
        else if (b.isEmpty() && row.value("runId") != a.value("runId")
            && row.value("compatibilityGroupId") == a.value("compatibilityGroupId")) b = row;
    }
    QVERIFY(!a.isEmpty() && !b.isEmpty());
    QVERIFY(controller.selectComparisonLap(0, a.value("reference").toMap()));
    QVERIFY(controller.selectComparisonLap(1, b.value("reference").toMap()));
    QTRY_VERIFY(controller.comparisonPairReady());

    // Nothing persisted yet: reading back gives an empty/default result, not a
    // fabricated range or channel list.
    QVERIFY(controller.comparisonPersistedRangeMeters().isEmpty());
    QVERIFY(controller.comparisonPersistedChannels().isEmpty());

    controller.persistComparisonRange(12.5, 87.25);
    controller.persistComparisonChannels({"speed", "throttle"});
    QVERIFY(controller.saveProject(QUrl::fromLocalFile(directory.filePath("day.fetproject"))));

    AppController reopened(nullptr, directory.filePath("reopened-recovery.json"));
    QTRY_VERIFY(!reopened.projectLoading());
    QTRY_VERIFY(!reopened.outingLapsLoading());
    const auto restoredRange = reopened.comparisonPersistedRangeMeters();
    QCOMPARE(restoredRange.value("startMeters").toDouble(), 12.5);
    QCOMPARE(restoredRange.value("endMeters").toDouble(), 87.25);
    QCOMPARE(reopened.comparisonPersistedChannels(), QStringList({"speed", "throttle"}));
    QVERIFY(!reopened.dirty());

    // Malformed writes must not corrupt the document: an inverted/non-finite
    // range or an over-budget channel list is rejected rather than silently
    // clamped or truncated into something that looks plausible.
    reopened.persistComparisonRange(50.0, 10.0);
    QCOMPARE(reopened.comparisonPersistedRangeMeters().value("startMeters").toDouble(), 12.5);
    reopened.persistComparisonRange(std::numeric_limits<double>::infinity(), 10.0);
    QCOMPARE(reopened.comparisonPersistedRangeMeters().value("startMeters").toDouble(), 12.5);
    reopened.persistComparisonChannels({"a", "b", "c", "d", "e"});
    QCOMPARE(reopened.comparisonPersistedChannels(), QStringList({"speed", "throttle"}));
}

void TelemetryTests::preservesComparisonSlotAcrossFailuresAndReplacement()
{
    QTemporaryDir directory; QVERIFY(directory.isValid());
    QSettings settings; settings.clear(); settings.sync();
    const auto first = directory.filePath("first.vbo"), second = directory.filePath("second.vbo");
    const auto secondBytes = EventProjectFixture::routeVbo(130, -2, 2);
    QVERIFY(writeBytes(first, EventProjectFixture::routeVbo())); QVERIFY(writeBytes(second, secondBytes));
    AppController controller(nullptr, directory.filePath("recovery.json"));
    QVERIFY(controller.importAnalysisRuns("Independent slots", {QUrl::fromLocalFile(first), QUrl::fromLocalFile(second)}));
    QTRY_COMPARE(controller.vboLoadState(), QString("ready")); QTRY_VERIFY(!controller.outingLapsLoading());
    const auto runA = controller.eventRuns()[0].toMap().value("id").toString();
    QVariantMap a, b;
    for (const auto &value : controller.comparisonLaps()) {
        const auto row = value.toMap();
        if (row.value("runId") == runA) a = row; else b = row;
    }
    QVERIFY(!a.isEmpty() && !b.isEmpty());
    QVERIFY(controller.selectComparisonLap(0, a.value("reference").toMap()));
    QTRY_COMPARE(controller.m_analysis.m_comparisonSlots[0].state, QString("ready"));
    const auto sessionA = controller.m_analysis.m_comparisonSlots[0].session;
    const auto trackA = controller.m_analysis.m_comparisonSlots[0].track;
    // Missing B fails through the shared verified-source loader without disturbing A.
    QVERIFY(QFile::remove(second));
    QVERIFY(controller.selectComparisonLap(1, b.value("reference").toMap()));
    QTRY_COMPARE(controller.m_analysis.m_comparisonSlots[1].state, QString("error"));
    QVERIFY(!controller.m_analysis.m_comparisonSlots[1].error.isEmpty());
    QCOMPARE(controller.m_analysis.m_comparisonSlots[0].session, sessionA); QCOMPARE(controller.m_analysis.m_comparisonSlots[0].track, trackA);
    QVERIFY(writeBytes(second, secondBytes));
    QVERIFY(controller.selectComparisonLap(1, b.value("reference").toMap()));
    QTRY_VERIFY(controller.comparisonPairReady());
    const auto sessionB = controller.m_analysis.m_comparisonSlots[1].session;
    // Hold one completion so swap/replacement races are deterministic.
    QTRY_VERIFY(!controller.m_analysis.m_comparisonPending);
    AnalysisController::OutingLapDetailResult stale;
    stale.request = controller.m_analysis.m_comparisonSlots[1].request;
    stale.session = sessionB; stale.error = "Obsolete worker result";
    controller.m_analysis.m_comparisonSlots[1].state = "loading";
    controller.m_analysis.m_comparisonPending = true; controller.m_analysis.m_comparisonLoadingSlot = 1;
    QPromise<AnalysisController::OutingLapDetailResult> promise; promise.start();
    controller.m_analysis.m_comparisonWatcher.setFuture(promise.future());
    QVERIFY(controller.swapComparisonLaps());
    QCOMPARE(controller.m_analysis.m_comparisonSlots[1].session, sessionA);
    promise.addResult(stale); promise.finish();
    QTRY_VERIFY(controller.comparisonPairReady());
    QCOMPARE(controller.m_analysis.m_comparisonSlots[1].session, sessionA);
    QVERIFY(controller.m_analysis.m_comparisonSlots[0].error.isEmpty());
    QCOMPARE(controller.m_analysis.m_comparisonSlots[0].row.value("reference"), b.value("reference"));
    // Replacing editor run A invalidates only its B slot, retaining the other run.
    const auto retained = controller.m_analysis.m_comparisonSlots[0].session;
    const auto replacement = directory.filePath("replacement.vbo");
    QVERIFY(writeBytes(replacement, EventProjectFixture::routeVbo(200, -1.2, 1, false, 450)));
    QCOMPARE(controller.activeRunId(), runA);
    controller.loadVbo(QUrl::fromLocalFile(replacement));
    QTRY_COMPARE(controller.vboLoadState(), QString("ready")); QTRY_VERIFY(!controller.outingLapsLoading());
    QCOMPARE(controller.m_analysis.m_comparisonSlots[1].state, QString("error"));
    QCOMPARE(controller.m_analysis.m_comparisonSlots[0].state, QString("ready"));
    QCOMPARE(controller.m_analysis.m_comparisonSlots[0].session, retained);
    // Starting another document clears both and cannot accept a late pair result.
    QTRY_VERIFY(!controller.m_analysis.m_comparisonPending);
    stale.request = controller.m_analysis.m_comparisonSlots[0].request;
    controller.m_analysis.m_comparisonSlots[0].state = "loading";
    controller.m_analysis.m_comparisonPending = true; controller.m_analysis.m_comparisonLoadingSlot = 0;
    QPromise<AnalysisController::OutingLapDetailResult> late; late.start();
    controller.m_analysis.m_comparisonWatcher.setFuture(late.future());
    QVERIFY(controller.saveProject(QUrl::fromLocalFile(directory.filePath("day.fetproject"))));
    controller.requestNewProject();
    late.addResult(stale); late.finish();
    QTRY_VERIFY(!controller.m_analysis.m_comparisonPending);
    QCOMPARE(controller.m_analysis.m_comparisonSlots[0].state, QString("empty"));
    QCOMPARE(controller.m_analysis.m_comparisonSlots[1].state, QString("empty"));
    QVERIFY(!controller.m_analysis.m_comparisonSlots[0].session && !controller.m_analysis.m_comparisonSlots[1].session);
}

void TelemetryTests::comparesTwoLapsFromTheSameRun()
{
    // The owner's real-world scenario: A and B are two different laps from the
    // SAME recording, not two different files. selectsIndependentComparisonLapsThroughQml
    // only exercises two different runs; this isolates the same-run case.
    QTemporaryDir directory; QVERIFY(directory.isValid());
    QSettings settings; settings.clear(); settings.sync();
    const auto path = directory.filePath("session.vbo");
    QVERIFY(writeBytes(path, EventProjectFixture::routeVbo()));
    AppController controller(nullptr, directory.filePath("recovery.json"));
    QVERIFY(controller.importAnalysisRuns("SameRun", {QUrl::fromLocalFile(path)}));
    QTRY_COMPARE(controller.vboLoadState(), QString("ready"));
    QTRY_VERIFY(!controller.outingLapsLoading());
    const auto candidates = controller.comparisonLaps(); QVERIFY(candidates.size() >= 2);
    QVariantMap a, b;
    for (const auto &value : candidates) {
        const auto row = value.toMap();
        if (a.isEmpty()) a = row;
        else if (b.isEmpty() && row.value("runId") == a.value("runId")
            && row.value("lapNumber") != a.value("lapNumber")) b = row;
    }
    QVERIFY2(!a.isEmpty() && !b.isEmpty(), "fixture must produce at least two laps in one run");
    QCOMPARE(a.value("runId").toString(), b.value("runId").toString());

    // Production instantiates ComparisonDetailPanel's TrackMapPanel once, directly
    // in AnalysisWindow's tree (not behind a Loader), when the window is first
    // created -- long before any lap is selected. Reproduce that ordering: create
    // the panel while both slots are still empty, then select laps afterward, to
    // test the live-update reactivity path, not just a fresh evaluation with data
    // already present.
    QQmlEngine engine; engine.rootContext()->setContextProperty("appController", &controller);
    QQmlComponent mapComponentA(&engine), mapComponentB(&engine);
    mapComponentA.setData("import QtQuick\nTrackMapPanel { comparisonSlot: 0; width: 200; height: 200 }",
        QUrl::fromLocalFile(QStringLiteral(ANALYSIS_PANEL_QML_PATH)));
    mapComponentB.setData("import QtQuick\nTrackMapPanel { comparisonSlot: 1; width: 200; height: 200 }",
        QUrl::fromLocalFile(QStringLiteral(ANALYSIS_PANEL_QML_PATH)));
    QVERIFY2(mapComponentA.isReady(), qPrintable(mapComponentA.errorString()));
    QVERIFY2(mapComponentB.isReady(), qPrintable(mapComponentB.errorString()));
    std::unique_ptr<QObject> mapObjectA(mapComponentA.create()), mapObjectB(mapComponentB.create());
    QVERIFY2(mapObjectA, qPrintable(mapComponentA.errorString()));
    QVERIFY2(mapObjectB, qPrintable(mapComponentB.errorString()));
    QVERIFY(mapObjectA->property("pathSegments").toList().isEmpty());
    QVERIFY(mapObjectB->property("pathSegments").toList().isEmpty());

    QVERIFY(controller.selectComparisonLap(0, a.value("reference").toMap()));
    QVERIFY(controller.selectComparisonLap(1, b.value("reference").toMap()));
    QTRY_VERIFY(controller.comparisonPairReady());
    QVERIFY2(!controller.comparisonLapTrack(0).isEmpty(), "comparison slot 0 track should not be empty");
    QVERIFY2(!controller.comparisonLapTrack(1).isEmpty(), "comparison slot 1 track should not be empty");
    QTRY_VERIFY2(!mapObjectA->property("pathSegments").toList().isEmpty(),
        "TrackMapPanel comparisonSlot 0 should update once its lap becomes ready");
    QTRY_VERIFY2(!mapObjectB->property("pathSegments").toList().isEmpty(),
        "TrackMapPanel comparisonSlot 1 should update once its lap becomes ready");
    QVERIFY(!controller.comparisonLapSeries(0, "latitude", a.value("startTime").toDouble(),
        a.value("endTime").toDouble(), 200).isEmpty());
    QVERIFY(!controller.comparisonLapSeries(1, "latitude", b.value("startTime").toDouble(),
        b.value("endTime").toDouble(), 200).isEmpty());
}

void TelemetryTests::reviewsSegmentProposalsForTheOpenLap()
{
    // KAN-48: proposals for the open lap are review input only; approval writes
    // the run's persisted segments, whose revision identifies what results use.
    QTemporaryDir directory; QVERIFY(directory.isValid());
    QSettings settings; settings.clear(); settings.sync();
    const auto path = directory.filePath("session.vbo");
    QVERIFY(writeBytes(path, EventProjectFixture::routeVbo()));
    AppController controller(nullptr, directory.filePath("recovery.json"));
    QVERIFY(controller.importAnalysisRuns("Segments", {QUrl::fromLocalFile(path)}));
    QTRY_COMPARE(controller.vboLoadState(), QString("ready"));
    QTRY_VERIFY(!controller.outingLapsLoading());
    int lapIndex = -1, otherIndex = -1;
    const auto rows = controller.outingLaps();
    for (int i = 0; i < rows.size(); ++i) {
        const auto row = rows[i].toMap();
        if (row.value("type") == "LAP" && lapIndex < 0 && !row.value("compatibilityGroupId").toString().isEmpty()) lapIndex = i;
        if (row.value("type") != "LAP" && otherIndex < 0) otherIndex = i;
    }
    QVERIFY(lapIndex >= 0);

    // A section that is not a complete timed lap cannot anchor proposals.
    if (otherIndex >= 0) {
        QVERIFY(controller.selectOutingLap(otherIndex));
        QTRY_COMPARE(controller.outingLapDetailState(), QString("ready"));
        controller.requestSegmentReview();
        QCOMPARE(controller.segmentReviewState(), QString("unavailable"));
        QVERIFY(!controller.segmentReviewMessage().isEmpty());
        QVERIFY(controller.segmentReviewItems().isEmpty());
    }

    // A result arriving after the lap closed must not mutate review state.
    QVERIFY(controller.selectOutingLap(lapIndex));
    QTRY_COMPARE(controller.outingLapDetailState(), QString("ready"));
    controller.requestSegmentReview();
    QCOMPARE(controller.segmentReviewState(), QString("loading"));
    controller.closeOutingLap();
    QCOMPARE(controller.segmentReviewState(), QString("idle"));
    QTRY_VERIFY(!controller.m_analysis.m_segmentReviewWatcher.isRunning());
    QCoreApplication::processEvents();
    QCOMPARE(controller.segmentReviewState(), QString("idle"));
    QVERIFY(controller.segmentReviewItems().isEmpty());

    QVERIFY(controller.selectOutingLap(lapIndex));
    QTRY_COMPARE(controller.outingLapDetailState(), QString("ready"));
    const auto runId = controller.selectedOutingLap().value("runId").toString();
    const auto configuration = controller.selectedOutingLap().value("compatibilityGroupId").toString();
    controller.requestSegmentReview();
    QTRY_VERIFY(controller.segmentReviewState() != "loading");
    QVERIFY2(controller.segmentReviewState() == "ready", qPrintable(controller.segmentReviewMessage()));
    QVERIFY(controller.segmentReviewAxisLength() > 0.0);
    auto items = controller.segmentReviewItems();
    QVERIFY2(items.size() >= 2, "the elliptical route splits into corners and straights");
    for (const auto &value : items) QCOMPARE(value.toMap().value("state").toString(), QString("proposed"));
    QCOMPARE(controller.segmentReviewApproved().value("count").toInt(), 0);
    QVERIFY(controller.segmentReviewApproved().value("revision").toString().isEmpty());

    // Proposals and uncertainty windows are drawn from the lap's own GPS trace.
    const auto layers = controller.segmentReviewMapLayers();
    QVERIFY(!layers.isEmpty());
    for (const auto &layer : layers) {
        for (const auto &polyline : layer.toMap().value("polylines").toList()) {
            for (const auto &point : polyline.toList()) {
                QVERIFY(std::isfinite(point.toMap().value("x").toDouble()));
                QVERIFY(std::isfinite(point.toMap().value("y").toDouble()));
            }
        }
    }

    {
        // The review panel and the map's static segment layer load against the live controller.
        QQmlEngine engine; engine.rootContext()->setContextProperty("appController", &controller);
        QQmlComponent component(&engine);
        component.setData("import QtQuick\nItem { width: 900; height: 700\n"
            "SegmentReviewPanel { objectName: \"panel\"; width: 560; height: 700 }\n"
            "TrackMapPanel { x: 580; lapDetail: true; segmentReview: true; width: 300; height: 300 } }",
            QUrl::fromLocalFile(QStringLiteral(ANALYSIS_PANEL_QML_PATH)));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        std::unique_ptr<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));
        auto *list = root->findChild<QObject *>("segmentProposalList");
        QVERIFY(list);
        QTRY_COMPARE(list->property("count").toInt(), static_cast<int>(items.size()));
        auto *mapLayer = root->findChild<QObject *>("segmentReviewMapLayer");
        QVERIFY(mapLayer);
        QCOMPARE(mapLayer->property("layers").toList().size(), static_cast<int>(layers.size()));
        QCOMPARE(controller.segmentReviewState(), QString("ready")); // opening the panel keeps the review
    }

    const auto storedSegments = [&] {
        for (const auto &run : controller.currentProjectObject().value("event").toObject().value("runs").toArray())
            if (run.toObject().value("id").toString() == runId) return run.toObject().value("trackSegments").toArray();
        return QJsonArray{};
    };
    QVERIFY(!controller.dirty() || controller.saveProject(QUrl::fromLocalFile(directory.filePath("before.fetproject"))));
    QVERIFY(!controller.dirty());
    QCOMPARE(controller.approveSegmentProposal(0), QString());
    QVERIFY(controller.dirty());
    QCOMPARE(storedSegments().size(), 1);
    QCOMPARE(storedSegments().first().toObject().value("trackConfigurationReference").toString(), configuration);
    const auto firstRevision = controller.segmentReviewApproved().value("revision").toString();
    QVERIFY(firstRevision.startsWith("track-segments-v1:"));
    QCOMPARE(firstRevision, approvedSegmentation(storedSegments(), configuration).revision);
    items = controller.segmentReviewItems();
    QCOMPARE(items[0].toMap().value("state").toString(), QString("approved"));
    QCOMPARE(items[0].toMap().value("approvedSegmentId").toString(),
        storedSegments().first().toObject().value("id").toString());
    QVERIFY(controller.approveSegmentProposal(0).isEmpty()); // idempotent
    QCOMPARE(storedSegments().size(), 1);

    // Rejection is review-session state only; it never touches the document.
    QVERIFY(controller.setSegmentProposalRejected(1, true));
    QCOMPARE(controller.segmentReviewItems()[1].toMap().value("state").toString(), QString("rejected"));
    QCOMPARE(storedSegments().size(), 1);
    QVERIFY(controller.setSegmentProposalRejected(1, false));
    QVERIFY(!controller.setSegmentProposalRejected(0, true)); // approved rows are revoked, not rejected
    QVERIFY(!controller.setSegmentProposalRejected(99, true));

    // Edits are validated, and an edit into approved territory cannot be approved.
    const auto second = controller.segmentReviewItems()[1].toMap();
    const double length = controller.segmentReviewAxisLength();
    QVERIFY(!controller.editSegmentProposal(1, "Back", "straight", std::nan(""), 10.0).isEmpty());
    QVERIFY(!controller.editSegmentProposal(1, "Back", "chicane", 10.0, 20.0).isEmpty());
    QVERIFY(!controller.editSegmentProposal(1, " ", "straight", 10.0, 20.0).isEmpty());
    QVERIFY(!controller.editSegmentProposal(1, "Back", "straight", 10.0, length + 1.0).isEmpty());
    QVERIFY(!controller.editSegmentProposal(0, "Renamed", "corner", 10.0, 20.0).isEmpty());
    const double first0 = items[0].toMap().value("startMeters").toDouble();
    const double first1 = items[0].toMap().value("endMeters").toDouble();
    const auto secondType = second.value("type").toString();
    QCOMPARE(controller.editSegmentProposal(1, "Back section", secondType,
        std::fmod(first0 + (first1 > first0 ? first1 - first0 : first1 + length - first0) / 2.0, length),
        second.value("endMeters").toDouble()), QString());
    auto edited = controller.segmentReviewItems()[1].toMap();
    QVERIFY(edited.value("edited").toBool());
    QCOMPARE(edited.value("name").toString(), QString("Back section"));
    QCOMPARE(edited.value("state").toString(), QString("superseded"));
    QVERIFY(controller.approveSegmentProposal(1).contains("Overlaps"));
    QCOMPARE(storedSegments().size(), 1);
    // Moving the start back onto the approved boundary makes it approvable again.
    QCOMPARE(controller.editSegmentProposal(1, "Back section", secondType, first1, second.value("endMeters").toDouble()), QString());
    QCOMPARE(controller.segmentReviewItems()[1].toMap().value("state").toString(), QString("proposed"));
    QCOMPARE(controller.approveSegmentProposal(1), QString());
    QCOMPARE(storedSegments().size(), 2);
    const auto secondRevision = controller.segmentReviewApproved().value("revision").toString();
    QVERIFY(secondRevision != firstRevision);
    QVERIFY(!segmentationResultCurrent({configuration, firstRevision, {}}, approvedSegmentation(storedSegments(), configuration)));

    // Revoking restores the proposal and yields yet another revision.
    QVERIFY(controller.revokeApprovedSegment(storedSegments().first().toObject().value("id").toString()));
    QCOMPARE(storedSegments().size(), 1);
    QCOMPARE(controller.segmentReviewItems()[0].toMap().value("state").toString(), QString("proposed"));
    QVERIFY(controller.segmentReviewApproved().value("revision").toString() != secondRevision);
    QVERIFY(!controller.revokeApprovedSegment("missing"));

    // Save and reopen: the approved segment is recognized against fresh proposals.
    const auto projectPath = directory.filePath("segments.fetproject");
    QVERIFY(controller.saveProject(QUrl::fromLocalFile(projectPath)));
    QVERIFY(!controller.dirty());
    AppController reopened(nullptr, directory.filePath("recovery-reopened.json"));
    reopened.requestOpenProject(QUrl::fromLocalFile(projectPath));
    QTRY_COMPARE(reopened.vboLoadState(), QString("ready"));
    QTRY_VERIFY(!reopened.outingLapsLoading());
    QTRY_VERIFY(reopened.selectOutingLap(lapIndex));
    QTRY_COMPARE(reopened.outingLapDetailState(), QString("ready"));
    reopened.requestSegmentReview();
    QTRY_COMPARE(reopened.segmentReviewState(), QString("ready"));
    QCOMPARE(reopened.segmentReviewApproved().value("count").toInt(), 1);
    // Matched on exact bounds and type; the reviewer's name is kept on the approved segment.
    QCOMPARE(reopened.segmentReviewItems()[1].toMap().value("state").toString(), QString("approved"));
    QCOMPARE(reopened.segmentReviewApproved().value("segments").toList().first().toMap().value("name").toString(),
        QString("Back section"));
    QCOMPARE(reopened.segmentReviewItems()[0].toMap().value("state").toString(), QString("proposed"));
}

void TelemetryTests::editsApprovedSegmentsWithUndo()
{
    // KAN-49: approved segments are edited in place (stable IDs), split,
    // merged and renamed; overlap and empty segments are refused; every change
    // is dirty document state with bounded undo/redo, and changes the revision.
    QTemporaryDir directory; QVERIFY(directory.isValid());
    QSettings settings; settings.clear(); settings.sync();
    const auto path = directory.filePath("session.vbo");
    QVERIFY(writeBytes(path, EventProjectFixture::routeVbo()));
    AppController controller(nullptr, directory.filePath("recovery.json"));
    QVERIFY(controller.importAnalysisRuns("Editing", {QUrl::fromLocalFile(path)}));
    QTRY_COMPARE(controller.vboLoadState(), QString("ready"));
    QTRY_VERIFY(!controller.outingLapsLoading());
    int lapIndex = -1;
    const auto rows = controller.outingLaps();
    for (int i = 0; i < rows.size() && lapIndex < 0; ++i) {
        const auto row = rows[i].toMap();
        if (row.value("type") == "LAP" && !row.value("compatibilityGroupId").toString().isEmpty()) lapIndex = i;
    }
    QVERIFY(lapIndex >= 0);
    QVERIFY(controller.selectOutingLap(lapIndex));
    QTRY_COMPARE(controller.outingLapDetailState(), QString("ready"));
    const auto runId = controller.selectedOutingLap().value("runId").toString();
    const auto configuration = controller.selectedOutingLap().value("compatibilityGroupId").toString();
    QVERIFY(!controller.editApprovedSegment("any", "X", "corner", 1.0, 2.0, true).isEmpty()); // no review open
    controller.requestSegmentReview();
    QTRY_COMPARE(controller.segmentReviewState(), QString("ready"));
    const double length = controller.segmentReviewAxisLength();
    QVERIFY(controller.segmentReviewItems().size() >= 3);
    const auto stored = [&] { return controller.m_analysis.storedRunTrackSegments(runId).toArray(); };
    const auto find = [&](const QString &id) {
        for (const auto &value : stored())
            if (value.toObject().value("id").toString() == id) return value.toObject();
        return QJsonObject{};
    };

    QVERIFY(!controller.dirty() || controller.saveProject(QUrl::fromLocalFile(directory.filePath("before.fetproject"))));
    QCOMPARE(controller.approveSegmentProposal(0), QString());
    QCOMPARE(controller.approveSegmentProposal(1), QString());
    QVERIFY(controller.dirty());
    QVERIFY(controller.segmentReviewApproved().value("canUndo").toBool());
    const auto firstId = controller.segmentReviewItems()[0].toMap().value("approvedSegmentId").toString();
    const auto secondId = controller.segmentReviewItems()[1].toMap().value("approvedSegmentId").toString();
    QVERIFY(!firstId.isEmpty() && !secondId.isEmpty());
    const auto first = find(firstId);
    const double start0 = first.value("startProgressMeters").toDouble();
    const double end0 = first.value("endProgressMeters").toDouble();
    QVERIFY(end0 > start0 + 10.0);
    QCOMPARE(find(secondId).value("startProgressMeters").toDouble(), end0);

    // A point on the approved segment's map line resolves to progress within it.
    QVariantList polyline;
    for (const auto &layer : controller.segmentReviewMapLayers()) {
        const auto map = layer.toMap();
        if (map.value("kind") == "approved" && map.value("id") == firstId && !map.value("polylines").toList().isEmpty())
            polyline = map.value("polylines").toList().first().toList();
    }
    QVERIFY(polyline.size() >= 3);
    const auto middle = polyline[polyline.size() / 2].toMap();
    const auto picked = controller.segmentReviewProgressAt(middle.value("x").toDouble(), middle.value("y").toDouble());
    QVERIFY2(picked.contains("progressMeters"), qPrintable(picked.value("error").toString()));
    QVERIFY(picked.value("progressMeters").toDouble() >= start0 - 5.0);
    QVERIFY(picked.value("progressMeters").toDouble() <= end0 + 5.0);
    QVERIFY(controller.segmentReviewProgressAt(5.0, 5.0).contains("error"));

    // Rename keeps the ID and changes the revision; results stamped earlier are stale.
    const auto revisionBefore = controller.segmentReviewApproved().value("revision").toString();
    const auto stamp = segmentationResultStamp(approvedSegmentation(stored(), configuration));
    QCOMPARE(controller.editApprovedSegment(firstId, "Turn A", first.value("type").toString(), start0, end0, true), QString());
    QCOMPARE(find(firstId).value("name").toString(), QString("Turn A"));
    QVERIFY(controller.segmentReviewApproved().value("revision").toString() != revisionBefore);
    QVERIFY(!segmentationResultCurrent(stamp, approvedSegmentation(stored(), configuration)));

    // Moving the shared boundary moves the neighbour with it; without that, overlap is refused.
    const double moved = end0 - 5.0;
    QVERIFY(controller.editApprovedSegment(firstId, "Turn A", first.value("type").toString(), start0, end0 + 5.0, false)
        .contains("overlap"));
    QCOMPARE(controller.editApprovedSegment(firstId, "Turn A", first.value("type").toString(), start0, moved, true), QString());
    QCOMPARE(find(firstId).value("endProgressMeters").toDouble(), moved);
    QCOMPARE(find(secondId).value("startProgressMeters").toDouble(), moved);
    QVERIFY(!controller.editApprovedSegment(firstId, "Turn A", "corner", start0, start0, true).isEmpty()); // empty
    QVERIFY(!controller.editApprovedSegment(firstId, "Turn A", "corner", start0, length + 1.0, true).isEmpty());

    // Split keeps the first ID; merging the parts restores one segment with that ID.
    const auto second = find(secondId);
    const double s1 = second.value("startProgressMeters").toDouble();
    const double e1 = second.value("endProgressMeters").toDouble();
    const double span = e1 > s1 ? e1 - s1 : e1 + length - s1;
    QCOMPARE(controller.splitApprovedSegment(secondId, std::fmod(s1 + span / 2.0, length)), QString());
    QCOMPARE(stored().size(), 3);
    QString splitId;
    for (const auto &value : stored()) {
        const auto id = value.toObject().value("id").toString();
        if (id != firstId && id != secondId) splitId = id;
    }
    QVERIFY(!splitId.isEmpty());
    QVERIFY(find(splitId).value("name").toString().endsWith("(2)"));
    QVERIFY(!controller.splitApprovedSegment(secondId, s1).isEmpty()); // on its own boundary
    QCOMPARE(controller.mergeApprovedSegments(splitId, secondId), QString());
    QCOMPARE(stored().size(), 2);
    QCOMPARE(find(secondId).value("endProgressMeters").toDouble(), e1);
    QVERIFY(!controller.mergeApprovedSegments(firstId, firstId).isEmpty());

    {
        // The approved-segment editor lists every approved segment.
        QQmlEngine engine; engine.rootContext()->setContextProperty("appController", &controller);
        QQmlComponent component(&engine);
        component.setData("import QtQuick\nItem { width: 700; height: 900\n"
            "SegmentReviewPanel { objectName: \"panel\"; width: 640; height: 900 } }",
            QUrl::fromLocalFile(QStringLiteral(ANALYSIS_PANEL_QML_PATH)));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        std::unique_ptr<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));
        auto *editor = root->findChild<QObject *>("approvedSegmentList");
        QVERIFY(editor);
        QTRY_COMPARE(editor->property("count").toInt(), 2);
    }

    // Undo walks back every recorded change (approvals included); redo replays them.
    const auto finalSegments = stored();
    int undone = 0;
    while (controller.undoSegmentEdit().isEmpty()) ++undone;
    QCOMPARE(undone, 6); // approve, approve, rename, move, split, merge
    QVERIFY(stored().isEmpty());
    QVERIFY(!controller.segmentReviewApproved().value("canUndo").toBool());
    QVERIFY(controller.segmentReviewApproved().value("canRedo").toBool());
    for (int i = 0; i < undone; ++i) QCOMPARE(controller.redoSegmentEdit(), QString());
    QVERIFY(stored() == finalSegments);
    QVERIFY(!controller.redoSegmentEdit().isEmpty());

    // A change made outside the history is never overwritten by undo.
    QVERIFY(controller.m_analysis.replaceRunTrackSegments(runId, QJsonArray{}, false));
    QVERIFY(controller.undoSegmentEdit().contains("changed outside"));
    QVERIFY(stored().isEmpty());
    QVERIFY(!controller.segmentReviewApproved().value("canUndo").toBool());
}

void TelemetryTests::persistsSegmentationAcrossSaveRecoveryAndReopen()
{
    // KAN-50: save, recovery and reopen keep segment IDs, bounds and review
    // decisions; a result stamp stays current across them and goes stale on a
    // segment edit, a calculation change or a layout change.
    QTemporaryDir directory; QVERIFY(directory.isValid());
    QSettings settings; settings.clear(); settings.sync();
    const auto path = directory.filePath("session.vbo");
    QVERIFY(writeBytes(path, EventProjectFixture::routeVbo()));
    const auto projectPath = directory.filePath("segments.fetproject");
    const auto recoveryPath = directory.filePath("recovery-after-reopen.json");
    const QString calculation = "sector-times-test-v1";
    const auto openReview = [](AppController &controller, const int lapIndex) {
        QTRY_VERIFY(controller.selectOutingLap(lapIndex));
        QTRY_COMPARE(controller.outingLapDetailState(), QString("ready"));
        controller.requestSegmentReview();
        QTRY_COMPARE(controller.segmentReviewState(), QString("ready"));
    };
    int lapIndex = -1;
    QString runId, configuration, firstId, firstName;
    double firstStart = 0.0, firstEnd = 0.0;
    SegmentationResultStamp stamp;
    {
        AppController controller(nullptr, directory.filePath("recovery-first.json"));
        QVERIFY(controller.importAnalysisRuns("Persistence", {QUrl::fromLocalFile(path)}));
        QTRY_COMPARE(controller.vboLoadState(), QString("ready"));
        QTRY_VERIFY(!controller.outingLapsLoading());
        const auto rows = controller.outingLaps();
        for (int i = 0; i < rows.size() && lapIndex < 0; ++i) {
            const auto row = rows[i].toMap();
            if (row.value("type") == "LAP" && !row.value("compatibilityGroupId").toString().isEmpty()) lapIndex = i;
        }
        QVERIFY(lapIndex >= 0);
        openReview(controller, lapIndex);
        QVERIFY(controller.segmentReviewItems().size() >= 3);
        runId = controller.selectedOutingLap().value("runId").toString();
        configuration = controller.selectedOutingLap().value("compatibilityGroupId").toString();
        QCOMPARE(controller.approveSegmentProposal(0), QString());
        QVERIFY(controller.setSegmentProposalRejected(1, true));
        QVERIFY(validTrackSegmentReview(controller.m_analysis.storedRunValue(runId, "trackSegmentReview")));
        QVERIFY(controller.m_analysis.storedRunValue(runId, "trackSegmentReview").isObject());
        const auto first = controller.m_analysis.storedRunTrackSegments(runId).toArray().first().toObject();
        firstId = first.value("id").toString();
        firstStart = first.value("startProgressMeters").toDouble();
        firstEnd = first.value("endProgressMeters").toDouble();
        firstName = QStringLiteral("Turn A");
        QCOMPARE(controller.editApprovedSegment(firstId, firstName, first.value("type").toString(), firstStart, firstEnd, true),
            QString());
        stamp = segmentationResultStamp(controller.m_analysis.currentApprovedSegmentation(), calculation);
        QVERIFY(segmentationResultCurrent(stamp, controller.m_analysis.currentApprovedSegmentation(), calculation));
        // Restoring a rejection removes it from the document; rejecting again stores it.
        QVERIFY(controller.setSegmentProposalRejected(1, false));
        QVERIFY(controller.m_analysis.storedRunValue(runId, "trackSegmentReview").isUndefined());
        QVERIFY(controller.setSegmentProposalRejected(1, true));
        QVERIFY(controller.saveProject(QUrl::fromLocalFile(projectPath)));
        QVERIFY(!controller.dirty());
    }
    {
        AppController reopened(nullptr, recoveryPath);
        reopened.requestOpenProject(QUrl::fromLocalFile(projectPath));
        QTRY_COMPARE(reopened.vboLoadState(), QString("ready"));
        QTRY_VERIFY(!reopened.outingLapsLoading());
        openReview(reopened, lapIndex);
        auto items = reopened.segmentReviewItems();
        QCOMPARE(items[0].toMap().value("state").toString(), QString("approved"));
        QCOMPARE(items[0].toMap().value("approvedSegmentId").toString(), firstId);
        QCOMPARE(items[1].toMap().value("state").toString(), QString("rejected"));
        const auto reopenedFirst = reopened.m_analysis.storedRunTrackSegments(runId).toArray().first().toObject();
        QCOMPARE(reopenedFirst.value("name").toString(), firstName);
        QCOMPARE(reopenedFirst.value("startProgressMeters").toDouble(), firstStart);
        QCOMPARE(reopenedFirst.value("endProgressMeters").toDouble(), firstEnd);
        QVERIFY(segmentationResultCurrent(stamp, reopened.m_analysis.currentApprovedSegmentation(), calculation));
        QVERIFY(!segmentationResultCurrent(stamp, reopened.m_analysis.currentApprovedSegmentation(), "sector-times-test-v2"));

        // Unsaved changes survive through the recovery snapshot, not the saved file.
        QCOMPARE(reopened.approveSegmentProposal(2), QString());
        QVERIFY(reopened.dirty());
        reopened.m_document.writeRecoverySnapshot();
        QVERIFY(QFileInfo::exists(recoveryPath));
    }
    {
        AppController recovered(nullptr, recoveryPath);
        QVERIFY(recovered.recoveryPending());
        recovered.resolveStartupRecovery("recover");
        QTRY_COMPARE(recovered.vboLoadState(), QString("ready"));
        QTRY_VERIFY(!recovered.outingLapsLoading());
        QVERIFY(recovered.dirty());
        openReview(recovered, lapIndex);
        const auto items = recovered.segmentReviewItems();
        QCOMPARE(items[0].toMap().value("approvedSegmentId").toString(), firstId);
        QCOMPARE(items[1].toMap().value("state").toString(), QString("rejected"));
        QCOMPARE(items[2].toMap().value("state").toString(), QString("approved"));
        QCOMPARE(recovered.m_analysis.storedRunTrackSegments(runId).toArray().size(), 2);
        // The unsaved approval is a segment edit: the earlier stamp is stale.
        QVERIFY(!segmentationResultCurrent(stamp, recovered.m_analysis.currentApprovedSegmentation(), calculation));
        const auto recoveredStamp = segmentationResultStamp(recovered.m_analysis.currentApprovedSegmentation(), calculation);
        QVERIFY(segmentationResultCurrent(recoveredStamp, recovered.m_analysis.currentApprovedSegmentation(), calculation));

        // A layout change produces a different configuration: the stored segments are
        // kept (IDs intact) but no longer apply, so no result stays current.
        QVERIFY(recovered.setRunTrackConfiguration(runId, "Changed", "clockwise"));
        QTRY_VERIFY(!recovered.outingLapsLoading() && recovered.m_analysis.m_outingLapRequestedKey == recovered.m_analysis.outingLapKey());
        QString changedConfiguration;
        for (const auto &row : recovered.outingLaps()) {
            const auto map = row.toMap();
            if (map.value("runId").toString() == runId && map.value("type") == "LAP")
                changedConfiguration = map.value("compatibilityGroupId").toString();
        }
        QVERIFY(changedConfiguration != configuration);
        const auto stored = recovered.m_analysis.storedRunTrackSegments(runId);
        QCOMPARE(stored.toArray().size(), 2);
        QCOMPARE(stored.toArray().first().toObject().value("id").toString(), firstId);
        const auto afterLayout = approvedSegmentation(stored, changedConfiguration);
        QCOMPARE(afterLayout.otherConfigurationSegments, 2);
        QVERIFY(afterLayout.segments.isEmpty());
        QVERIFY(!segmentationResultCurrent(recoveredStamp, afterLayout, calculation));
    }
}

void TelemetryTests::timesApprovedSectorsForTheOpenLap()
{
    // KAN-51: approving every proposal and splitting the gate-crossing one at the
    // gate tiles the lap; sector times then sum to the lap time within tolerance.
    QTemporaryDir directory; QVERIFY(directory.isValid());
    QSettings settings; settings.clear(); settings.sync();
    const auto path = directory.filePath("session.vbo");
    QVERIFY(writeBytes(path, EventProjectFixture::routeVbo()));
    AppController controller(nullptr, directory.filePath("recovery.json"));
    QVERIFY(controller.importAnalysisRuns("Sectors", {QUrl::fromLocalFile(path)}));
    QTRY_COMPARE(controller.vboLoadState(), QString("ready"));
    QTRY_VERIFY(!controller.outingLapsLoading());
    int lapIndex = -1;
    const auto rows = controller.outingLaps();
    for (int i = 0; i < rows.size() && lapIndex < 0; ++i) {
        const auto row = rows[i].toMap();
        if (row.value("type") == "LAP" && !row.value("compatibilityGroupId").toString().isEmpty()) lapIndex = i;
    }
    QVERIFY(lapIndex >= 0);
    QVERIFY(!controller.outingLapSectorTimes().value("valid").toBool()); // no review yet
    QVERIFY(controller.selectOutingLap(lapIndex));
    QTRY_COMPARE(controller.outingLapDetailState(), QString("ready"));
    controller.requestSegmentReview();
    QTRY_COMPARE(controller.segmentReviewState(), QString("ready"));
    const auto runId = controller.selectedOutingLap().value("runId").toString();

    // Nothing approved: valid, no sectors, no revision.
    auto times = controller.outingLapSectorTimes();
    QVERIFY(times.value("valid").toBool());
    QVERIFY(times.value("sectors").toList().isEmpty());
    QVERIFY(times.value("revision").toString().isEmpty());

    const auto count = controller.segmentReviewItems().size();
    for (int i = 0; i < count; ++i) QCOMPARE(controller.approveSegmentProposal(i), QString());
    QString wrappingId;
    for (const auto &value : controller.m_analysis.storedRunTrackSegments(runId).toArray()) {
        const auto segment = value.toObject();
        if (segment.value("endProgressMeters").toDouble() < segment.value("startProgressMeters").toDouble())
            wrappingId = segment.value("id").toString();
    }
    if (!wrappingId.isEmpty()) {
        // KAN-120: unsplit, the gate-crossing segment is timed within the lap
        // and the approved set already tiles it; splitting keeps the same sum.
        times = controller.outingLapSectorTimes();
        QVERIFY(times.value("completePartition").toBool());
        QVERIFY(times.contains("sumSeconds"));
        QVERIFY(times.value("partitionErrorSeconds").toDouble() <= sectorSumToleranceSeconds);
        QCOMPARE(controller.splitApprovedSegment(wrappingId, controller.segmentReviewAxisLength()), QString());
    }

    times = controller.outingLapSectorTimes();
    QVERIFY(times.value("valid").toBool());
    QVERIFY(times.value("completePartition").toBool());
    QCOMPARE(times.value("revision").toString(), controller.segmentReviewApproved().value("revision").toString());
    QCOMPARE(times.value("calculationAlgorithm").toString(), QString(sectorTimingAlgorithm));
    const auto sectors = times.value("sectors").toList();
    QCOMPARE(sectors.size(), controller.m_analysis.storedRunTrackSegments(runId).toArray().size());
    for (const auto &value : sectors) {
        const auto sector = value.toMap();
        QVERIFY2(sector.contains("seconds"), qPrintable(sector.value("name").toString() + ": "
            + sector.value("unavailableReason").toString()));
        QVERIFY(sector.value("seconds").toDouble() > 0.0);
    }
    QVERIFY(times.contains("sumSeconds"));
    const double lapSeconds = controller.selectedOutingLap().value("endTime").toDouble()
        - controller.selectedOutingLap().value("startTime").toDouble();
    QVERIFY(std::abs(times.value("lapSeconds").toDouble() - lapSeconds) < 1e-9);
    QVERIFY(times.value("partitionErrorSeconds").toDouble() <= sectorSumToleranceSeconds);

    // KAN-52: the route has no recorded speed channel, so corner speeds are
    // explicitly unavailable rather than derived from GPS positions.
    const auto corners = controller.outingLapCornerSpeeds();
    QVERIFY(!corners.isEmpty());
    for (const auto &value : corners) {
        const auto corner = value.toMap();
        QCOMPARE(corner.value("provenance").toString(), QString("unavailable"));
        QCOMPARE(corner.value("calculationAlgorithm").toString(), QString(cornerSpeedsAlgorithm));
        for (const auto *phase : {"entry", "apex", "minimum", "exit"}) {
            QVERIFY(!corner.value(phase).toMap().contains("value"));
            QCOMPARE(corner.value(phase).toMap().value("unavailableReason").toString(), QString(cornerPhaseSpeedChannelMissing));
        }
    }

    // KAN-53: without brake or acceleration channels there is no braking point,
    // and no distance or deceleration is invented.
    const auto braking = controller.outingLapBrakingMetrics();
    QCOMPARE(braking.size(), corners.size());
    for (const auto &value : braking) {
        const auto metrics = value.toMap();
        QCOMPARE(metrics.value("unavailableReason").toString(), QString(brakingNoChannel));
        QVERIFY(!metrics.contains("brakingPointMeters"));
        QVERIFY(!metrics.contains("brakingDistanceMeters"));
        QVERIFY(!metrics.contains("peakDeceleration"));
        QCOMPARE(metrics.value("calculationAlgorithm").toString(), QString(brakingMetricsAlgorithm));
    }

    // KAN-54: no throttle or acceleration channel means no pickup; the following
    // interval is still timed from the projection, without any speed values.
    const auto exits = controller.outingLapExitMetrics();
    QCOMPARE(exits.size(), corners.size());
    bool timedInterval = false;
    for (const auto &value : exits) {
        const auto exit = value.toMap();
        QCOMPARE(exit.value("pickup").toMap().value("unavailableReason").toString(), QString(exitNoChannel));
        QVERIFY(!exit.value("pickup").toMap().contains("progressMeters"));
        QVERIFY(!exit.contains("exitSpeed"));
        QVERIFY(!exit.contains("intervalEndSpeed"));
        timedInterval |= exit.contains("elapsedSeconds") && exit.value("elapsedSeconds").toDouble() > 0.0;
    }
    QVERIFY(timedInterval);
}

void TelemetryTests::overlaysComparisonLapsOnASharedProgressAxis()
{
    // The A/B comparison screen overlays both laps on one chart/map, aligned
    // on the shared cross-lap track-progress axis (KAN-31/32/33) rather than
    // two independent, unaligned side-by-side panels or each lap's own
    // distance-into-lap. Cover the invokables that make that possible: a
    // shared-geometry track pair, a progress-parameterized channel series,
    // and the available-channel intersection.
    QTemporaryDir directory; QVERIFY(directory.isValid());
    QSettings settings; settings.clear(); settings.sync();
    const auto path = directory.filePath("session.vbo");
    QVERIFY(writeBytes(path, EventProjectFixture::routeVbo()));
    AppController controller(nullptr, directory.filePath("recovery.json"));
    QVERIFY(controller.importAnalysisRuns("Overlay", {QUrl::fromLocalFile(path)}));
    QTRY_COMPARE(controller.vboLoadState(), QString("ready"));
    QTRY_VERIFY(!controller.outingLapsLoading());
    const auto candidates = controller.comparisonLaps(); QVERIFY(candidates.size() >= 2);
    const auto a = candidates[0].toMap(), b = candidates[1].toMap();

    QVERIFY(controller.comparisonAvailableChannels().isEmpty());

    QQmlEngine engine; engine.rootContext()->setContextProperty("appController", &controller);
    QQmlComponent chartComponent(&engine), mapComponent(&engine);
    chartComponent.setData("import QtQuick\nComparisonOverlayChart { channel: \"latitude\"; width: 300; height: 200; "
        "totalMeters: Math.max(1, appController.comparisonProgressAxisLength); "
        "zoomEnd: totalMeters }",
        QUrl::fromLocalFile(QStringLiteral(ANALYSIS_PANEL_QML_PATH)));
    mapComponent.setData("import QtQuick\nComparisonOverlayMap { width: 200; height: 200 }",
        QUrl::fromLocalFile(QStringLiteral(ANALYSIS_PANEL_QML_PATH)));
    QVERIFY2(chartComponent.isReady(), qPrintable(chartComponent.errorString()));
    QVERIFY2(mapComponent.isReady(), qPrintable(mapComponent.errorString()));
    std::unique_ptr<QObject> chartObject(chartComponent.create());
    std::unique_ptr<QObject> mapObject(mapComponent.create());
    QVERIFY2(chartObject, qPrintable(chartComponent.errorString()));
    QVERIFY2(mapObject, qPrintable(mapComponent.errorString()));
    QVERIFY(!chartObject->property("hasData").toBool());

    QVERIFY(controller.selectComparisonLap(0, a.value("reference").toMap()));
    QVERIFY(controller.selectComparisonLap(1, b.value("reference").toMap()));
    QTRY_VERIFY(controller.comparisonPairReady());

    QCOMPARE(controller.comparisonAvailableChannels(), controller.m_analysis.m_comparisonSlots[0].session->channelNames());
    QVERIFY2(controller.comparisonProgressAxisLength() > 0.0, "the shared progress axis should build from slot 0's lap");

    QVERIFY2(!controller.comparisonOverlayTrack(0).isEmpty(), "overlay track 0 should not be empty");
    QVERIFY2(!controller.comparisonOverlayTrack(1).isEmpty(), "overlay track 1 should not be empty");
    for (const int slot : {0, 1}) {
        for (const auto &segmentValue : controller.comparisonOverlayTrack(slot)) {
            for (const auto &pointValue : segmentValue.toList()) {
                const auto point = pointValue.toMap();
                QVERIFY(std::isfinite(point.value("x").toDouble()));
                QVERIFY(std::isfinite(point.value("y").toDouble()));
            }
        }
    }

    const double axisLength = controller.comparisonProgressAxisLength();
    const auto seriesA = controller.comparisonChannelSeriesByProgress(0, "latitude", 0, axisLength, 100);
    const auto seriesB = controller.comparisonChannelSeriesByProgress(1, "latitude", 0, axisLength, 100);
    QVERIFY2(!seriesA.value("segments").toList().isEmpty(), "progress series 0 should not be empty");
    QVERIFY2(!seriesB.value("segments").toList().isEmpty(), "progress series 1 should not be empty");

    // A midpoint progress value should resolve to a real position on the
    // shared map for both laps -- this is what drives the hover markers.
    const auto midpointA = controller.comparisonPositionAtProgress(0, axisLength / 2);
    const auto midpointB = controller.comparisonPositionAtProgress(1, axisLength / 2);
    QVERIFY(midpointA.contains("x") && midpointA.contains("y"));
    QVERIFY(midpointB.contains("x") && midpointB.contains("y"));

    // The delta-time trace: cumulative time gap between the laps at the same
    // shared progress, not a per-sample channel-value difference.
    const auto deltaSeries = controller.comparisonDeltaSeriesByProgress(0, axisLength, 50);
    QVERIFY2(!deltaSeries.value("segments").toList().isEmpty(), "time delta series should not be empty");
    QCOMPARE(deltaSeries.value("unit").toString(), QString("s"));
    const auto firstDeltaSegment = deltaSeries.value("segments").toList().first().toList();
    QVERIFY(!firstDeltaSegment.isEmpty());
    // Both laps start their own elapsed-time reference at progress 0, so the
    // gap right at the start of the lap should be close to zero.
    QVERIFY(std::abs(firstDeltaSegment.first().toPointF().y()) < 1.0);
    QVERIFY(controller.comparisonDeltaSeriesByProgress(10, 5, 50).contains("reason"));

    QTRY_VERIFY2(chartObject->property("hasData").toBool(), "overlay chart should show data once both laps are ready");
    QTRY_VERIFY2(!mapObject->property("trackA").toList().isEmpty(), "overlay map track A should populate");
    QTRY_VERIFY2(!mapObject->property("trackB").toList().isEmpty(), "overlay map track B should populate");
}

void TelemetryTests::showsCornerAnalyzerSegmentMetricsForBothLaps()
{
    // KAN-55: two laps of the SAME run/file share one run id, so approving
    // segments once (via the ordinary single-lap review flow, on either lap)
    // gives both comparison slots the identical approved revision -- this is
    // the ordinary "compare two laps from one session" case, not the harder
    // cross-run case where each run keeps its own independently-approved
    // trackSegments and only a matching revision makes them comparable.
    QTemporaryDir directory; QVERIFY(directory.isValid());
    QSettings settings; settings.clear(); settings.sync();
    const auto path = directory.filePath("session.vbo");
    QVERIFY(writeBytes(path, EventProjectFixture::routeVbo()));
    AppController controller(nullptr, directory.filePath("recovery.json"));
    QVERIFY(controller.importAnalysisRuns("Corner Analyzer", {QUrl::fromLocalFile(path)}));
    QTRY_COMPARE(controller.vboLoadState(), QString("ready"));
    QTRY_VERIFY(!controller.outingLapsLoading());
    const auto candidates = controller.comparisonLaps(); QVERIFY(candidates.size() >= 2);
    const auto a = candidates[0].toMap(), b = candidates[1].toMap();

    QVERIFY(controller.selectComparisonLap(0, a.value("reference").toMap()));
    QVERIFY(controller.selectComparisonLap(1, b.value("reference").toMap()));
    QTRY_VERIFY(controller.comparisonPairReady());

    // No segments approved yet: nothing to show, not an empty-but-valid list.
    QVERIFY(controller.comparisonApprovedSegments().isEmpty());
    QVERIFY(controller.comparisonSegmentMetrics("anything").isEmpty());

    // Approve every proposal on lap A's underlying run (shared with lap B).
    int lapIndex = -1;
    const auto rows = controller.outingLaps();
    for (int i = 0; i < rows.size() && lapIndex < 0; ++i) {
        const auto row = rows[i].toMap();
        if (row.value("type") == "LAP" && !row.value("compatibilityGroupId").toString().isEmpty()) lapIndex = i;
    }
    QVERIFY(lapIndex >= 0);
    QVERIFY(controller.selectOutingLap(lapIndex));
    QTRY_COMPARE(controller.outingLapDetailState(), QString("ready"));
    controller.requestSegmentReview();
    QTRY_COMPARE(controller.segmentReviewState(), QString("ready"));
    const auto runId = controller.selectedOutingLap().value("runId").toString();
    const auto count = controller.segmentReviewItems().size();
    for (int i = 0; i < count; ++i) QCOMPARE(controller.approveSegmentProposal(i), QString());
    QString wrappingId;
    for (const auto &value : controller.m_analysis.storedRunTrackSegments(runId).toArray()) {
        const auto segment = value.toObject();
        if (segment.value("endProgressMeters").toDouble() < segment.value("startProgressMeters").toDouble())
            wrappingId = segment.value("id").toString();
    }
    if (!wrappingId.isEmpty())
        QCOMPARE(controller.splitApprovedSegment(wrappingId, controller.segmentReviewAxisLength()), QString());
    const auto approvedSegments = controller.m_analysis.storedRunTrackSegments(runId).toArray();
    QVERIFY(!approvedSegments.isEmpty());
    controller.closeOutingLap();

    // Both slots resolve the same run's approved segmentation: available now.
    const auto segments = controller.comparisonApprovedSegments();
    QCOMPARE(segments.size(), approvedSegments.size());
    QString cornerId, otherId;
    for (const auto &value : segments) {
        const auto segment = value.toMap();
        QVERIFY(!segment.value("id").toString().isEmpty());
        if (segment.value("type") == "corner" && cornerId.isEmpty()) cornerId = segment.value("id").toString();
        else if (otherId.isEmpty()) otherId = segment.value("id").toString();
    }

    // A non-corner (or any) segment always reports sector time -- both laps
    // took a real, positive amount of time through it, projected on the
    // shared comparison axis, not each lap's own distance-into-lap.
    const auto anyId = cornerId.isEmpty() ? otherId : cornerId;
    QVERIFY(!anyId.isEmpty());
    const auto metrics = controller.comparisonSegmentMetrics(anyId);
    QCOMPARE(metrics.value("segmentId").toString(), anyId);
    const auto sectorTime = metrics.value("sectorTime").toMap();
    QVERIFY(!sectorTime.isEmpty());
    for (const auto *side : {"a", "b"}) {
        const auto value = sectorTime.value(side).toMap();
        QVERIFY2(value.contains("value"), qPrintable(value.value("unavailableReason").toString()));
        QCOMPARE(value.value("provenance").toString(), QString("calculated"));
        QVERIFY(value.value("value").toDouble() > 0.0);
    }
    QVERIFY(sectorTime.value("delta").toMap().contains("value"));

    // A corner segment: the route has no recorded speed channel, so every
    // corner/braking/exit value is explicitly unavailable, never derived from
    // GPS positions -- the same invariant outingLapCornerSpeeds already proves
    // for the single-lap case, now also true through the A/B combination.
    if (!cornerId.isEmpty()) {
        const auto cornerMetrics = controller.comparisonSegmentMetrics(cornerId);
        const auto corner = cornerMetrics.value("corner").toMap();
        QVERIFY(!corner.isEmpty());
        for (const auto *phase : {"entry", "apex", "minimum", "exit"}) {
            const auto phaseMap = corner.value(phase).toMap();
            for (const auto *side : {"a", "b"}) {
                const auto value = phaseMap.value(side).toMap();
                QVERIFY(!value.contains("value"));
                QCOMPARE(value.value("unavailableReason").toString(), QString(cornerPhaseSpeedChannelMissing));
                QCOMPARE(value.value("provenance").toString(), QString("unavailable"));
            }
        }
        const auto braking = cornerMetrics.value("braking").toMap();
        QVERIFY(!braking.isEmpty());
        QCOMPARE(braking.value("point").toMap().value("a").toMap().value("unavailableReason").toString(), QString(brakingNoChannel));
        const auto exitEffects = cornerMetrics.value("exitEffects").toMap();
        QVERIFY(!exitEffects.isEmpty());
        QCOMPARE(exitEffects.value("pickup").toMap().value("a").toMap().value("unavailableReason").toString(), QString(exitNoChannel));
    }

    // An unknown segment id is never fabricated into a result.
    QVERIFY(controller.comparisonSegmentMetrics("not-a-real-id").isEmpty());
}

void TelemetryTests::calculatesOutingTheoreticalBestAcrossPopulation()
{
    // KAN-56: the fastest valid time per approved sector across every
    // eligible lap of the current comparison group's population (here, one
    // run's several laps -- routeVbo() has more than the two laps the
    // comparison feature uses), not just a sum of each lap's own best.
    QTemporaryDir directory; QVERIFY(directory.isValid());
    QSettings settings; settings.clear(); settings.sync();
    const auto path = directory.filePath("session.vbo");
    QVERIFY(writeBytes(path, EventProjectFixture::routeVbo()));
    AppController controller(nullptr, directory.filePath("recovery.json"));
    QVERIFY(controller.importAnalysisRuns("Theoretical Best", {QUrl::fromLocalFile(path)}));
    QTRY_COMPARE(controller.vboLoadState(), QString("ready"));
    QTRY_VERIFY(!controller.outingLapsLoading());
    QTRY_VERIFY(!controller.outingComparisonGroupId().isEmpty());

    // No run has approved segments yet: unavailable, not silently empty-but-ready.
    controller.requestOutingTheoreticalBest();
    QTRY_COMPARE(controller.outingTheoreticalBest().value("state").toString(), QString("unavailable"));
    QVERIFY(!controller.outingTheoreticalBest().value("message").toString().isEmpty());

    // Approve every proposal on one lap's underlying run.
    int lapIndex = -1;
    const auto rows = controller.outingLaps();
    for (int i = 0; i < rows.size() && lapIndex < 0; ++i) {
        const auto row = rows[i].toMap();
        if (row.value("type") == "LAP" && !row.value("compatibilityGroupId").toString().isEmpty()) lapIndex = i;
    }
    QVERIFY(lapIndex >= 0);
    QVERIFY(controller.selectOutingLap(lapIndex));
    QTRY_COMPARE(controller.outingLapDetailState(), QString("ready"));
    controller.requestSegmentReview();
    QTRY_COMPARE(controller.segmentReviewState(), QString("ready"));
    const auto runId = controller.selectedOutingLap().value("runId").toString();
    const auto count = controller.segmentReviewItems().size();
    for (int i = 0; i < count; ++i) QCOMPARE(controller.approveSegmentProposal(i), QString());
    QString wrappingId;
    for (const auto &value : controller.m_analysis.storedRunTrackSegments(runId).toArray()) {
        const auto segment = value.toObject();
        if (segment.value("endProgressMeters").toDouble() < segment.value("startProgressMeters").toDouble())
            wrappingId = segment.value("id").toString();
    }
    if (!wrappingId.isEmpty())
        QCOMPARE(controller.splitApprovedSegment(wrappingId, controller.segmentReviewAxisLength()), QString());
    const auto approvedSegments = controller.m_analysis.storedRunTrackSegments(runId).toArray();
    QVERIFY(!approvedSegments.isEmpty());
    controller.closeOutingLap();

    controller.requestOutingTheoreticalBest();
    QTRY_COMPARE(controller.outingTheoreticalBest().value("state").toString(), QString("ready"));
    const auto best = controller.outingTheoreticalBest();
    const auto sectors = best.value("sectors").toList();
    QCOMPARE(sectors.size(), approvedSegments.size());
    bool anyTimed = false;
    bool allTimed = true;
    for (const auto &value : sectors) {
        const auto sector = value.toMap();
        QVERIFY(!sector.value("segmentId").toString().isEmpty());
        if (sector.contains("seconds")) {
            anyTimed = true;
            QVERIFY(sector.value("seconds").toDouble() > 0.0);
            QVERIFY(!sector.value("sourceLapLabel").toString().isEmpty());
        } else {
            allTimed = false;
            QVERIFY(!sector.value("unavailableReason").toString().isEmpty());
        }
    }
    QVERIFY(anyTimed);
    QCOMPARE(best.contains("totalSeconds"), allTimed);
}

namespace {
// A routeVbo() recording with every timestamp scaled: the same route and
// gate (same compatibility group), uniformly faster when scale < 1.
QByteArray scaledRouteVbo(const double scale)
{
    const auto lines = QString::fromUtf8(EventProjectFixture::routeVbo()).split('\n');
    QStringList out;
    bool data = false;
    for (const auto &line : lines) {
        if (!data || line.trimmed().isEmpty()) {
            out << line;
            data = data || line == "[data]";
            continue;
        }
        auto fields = line.split(' ');
        fields[0] = QString::number(fields[0].toDouble() * scale, 'f', 6);
        out << fields.join(' ');
    }
    return out.join('\n').toUtf8();
}

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
// routeVbo() with calculated acceleration columns: lateral = scale·sin(angle),
// longitudinal = scale·0.5·cos(angle), so the peaks are known (scale lateral,
// scale·0.5 braking and accelerating).
QByteArray routeVboWithAccelerations(const double scale, const int samplesPerLap = 240)
{
    const auto lines = QString::fromUtf8(EventProjectFixture::routeVbo(samplesPerLap)).split('\n');
    QStringList out;
    bool data = false;
    int index = 0;
    for (const auto &line : lines) {
        if (line == "time latitude longitude") { out << line + " longacc-calc latacc-calc"; continue; }
        if (!data || line.trimmed().isEmpty()) { out << line; data = data || line == "[data]"; continue; }
        const double angle = -1.0 + 2 * std::numbers::pi * index / samplesPerLap;
        out << line + QString(" %1 %2").arg(scale * 0.5 * std::cos(angle), 0, 'f', 5).arg(scale * std::sin(angle), 0, 'f', 5);
        ++index;
    }
    return out.join('\n').toUtf8();
}
// routeVbo() with a coolant temperature column: three OBD placeholder zeros,
// then 80 °C rising by 0.01 °C per sample.
QByteArray routeVboWithCoolant(const int samplesPerLap = 240)
{
    const auto lines = QString::fromUtf8(EventProjectFixture::routeVbo(samplesPerLap)).split('\n');
    QStringList out;
    bool data = false;
    int index = 0;
    for (const auto &line : lines) {
        if (line == "time latitude longitude") { out << line + " coolant_temp-obd"; continue; }
        if (!data || line.trimmed().isEmpty()) { out << line; data = data || line == "[data]"; continue; }
        out << line + QString(" %1").arg(index < 3 ? 0.0 : 80.0 + 0.01 * index, 0, 'f', 3);
        ++index;
    }
    return out.join('\n').toUtf8();
}
// routeVbo() with a heart-rate column held for five rows at a time (a strap
// updates slower than the logger), at `level` bpm with one 255 bpm artifact.
QByteArray routeVboWithHeartRate(const double level, const int samplesPerLap = 240)
{
    const auto lines = QString::fromUtf8(EventProjectFixture::routeVbo(samplesPerLap)).split('\n');
    QStringList out;
    bool data = false;
    int index = 0;
    for (const auto &line : lines) {
        if (line == "time latitude longitude") { out << line + " heart_rate"; continue; }
        if (!data || line.trimmed().isEmpty()) { out << line; data = data || line == "[data]"; continue; }
        const double value = index == 100 ? 255.0 : level + ((index / 5) % 2 == 0 ? -2.0 : 2.0);
        out << line + QString(" %1").arg(value, 0, 'f', 1);
        ++index;
    }
    return out.join('\n').toUtf8();
}
// `base` (a routeVbo()-style recording) with extra data columns: `names`
// are appended to the column line, `values(row)` supplies each row's values.
QByteArray withColumns(const QByteArray &base, const QStringList &names, const std::function<QStringList(int)> &values)
{
    const auto lines = QString::fromUtf8(base).split('\n');
    QStringList out;
    bool data = false;
    int index = 0;
    for (const auto &line : lines) {
        if (line == "time latitude longitude") { out << line + " " + names.join(' '); continue; }
        if (!data || line.trimmed().isEmpty()) { out << line; data = data || line == "[data]"; continue; }
        out << line + " " + values(index).join(' ');
        ++index;
    }
    return out.join('\n').toUtf8();
}
// A complete M4 recording: warpedRouteVbo() with heart rate at `heartRate`
// bpm, coolant from `coolantStart` rising 0.01 per row, and accelerations
// with lateral peak `gScale` and braking/accelerating peaks gScale/2.
// `vbo` with a constant 100 km/h velocity column appended (the route
// fixtures have no speed channel).
// KAN-103: a session recording with a non-periodic speed trace, optionally
// an OBD coolant channel, and a speed bias for a disagreeing logger.
QByteArray withSessionSpeed(const QByteArray &vbo, const bool coolant, const double speedBias = 0.0)
{
    QStringList out;
    bool data = false;
    for (const auto &line : QString::fromUtf8(vbo).split('\n')) {
        if (line.startsWith("time latitude longitude")) { out << line + " velocity" + (coolant ? " coolant_temp-obd" : ""); continue; }
        if (!data || line.trimmed().isEmpty()) { out << line; data = data || line == "[data]"; continue; }
        const double t = line.split(' ').first().toDouble();
        const double speed = 90.0 + 25.0 * std::sin(0.11 * t) + 12.0 * std::sin(0.0007 * t * t) + speedBias;
        out << line + QString(" %1").arg(speed, 0, 'f', 3) + (coolant ? QString(" %1").arg(88.0 + 0.02 * t, 0, 'f', 3) : "");
    }
    return out.join('\n').toUtf8();
}
QByteArray withVelocity(const QByteArray &vbo)
{
    QStringList out;
    bool data = false;
    for (const auto &line : QString::fromUtf8(vbo).split('\n')) {
        if (line.startsWith("time latitude longitude")) { out << line + " velocity"; continue; }
        if (!data || line.trimmed().isEmpty()) { out << line; data = data || line == "[data]"; continue; }
        out << line + " 100.0";
    }
    return out.join('\n').toUtf8();
}
QByteArray fullM4Vbo(const bool fastFirstHalf, const double heartRate, const double coolantStart, const double gScale)
{
    return withColumns(warpedRouteVbo(fastFirstHalf), {"heart_rate", "coolant_temp-obd", "longacc-calc", "latacc-calc"},
        [=](const int index) {
            const double angle = -1.0 + 2 * std::numbers::pi * index / 240;
            return QStringList{QString::number(heartRate, 'f', 1), QString::number(coolantStart + 0.01 * index, 'f', 3),
                QString::number(gScale * 0.5 * std::cos(angle), 'f', 5), QString::number(gScale * std::sin(angle), 'f', 5)};
        });
}
} // namespace

// Approves every proposal on the first eligible lap of `runName`'s run and
// splits a gate-crossing segment, returning the stored segments.
QJsonArray TelemetryTests::approveAllSegmentsOnRun(AppController &controller, const QString &runName)
{
    int lapIndex = -1;
    const auto rows = controller.outingLaps();
    for (int i = 0; i < rows.size() && lapIndex < 0; ++i) {
        const auto row = rows[i].toMap();
        if (row.value("type") == "LAP" && row.value("runName").toString() == runName
            && !row.value("compatibilityGroupId").toString().isEmpty()) lapIndex = i;
    }
    if (lapIndex < 0 || !controller.selectOutingLap(lapIndex)) return {};
    if (!QTest::qWaitFor([&] { return controller.outingLapDetailState() == "ready"; }, 20000)) return {};
    controller.requestSegmentReview();
    if (!QTest::qWaitFor([&] { return controller.segmentReviewState() == "ready"; }, 20000)) return {};
    const auto runId = controller.selectedOutingLap().value("runId").toString();
    const auto count = controller.segmentReviewItems().size();
    for (int i = 0; i < count; ++i) if (!controller.approveSegmentProposal(i).isEmpty()) return {};
    for (const auto &value : controller.m_analysis.storedRunTrackSegments(runId).toArray()) {
        const auto segment = value.toObject();
        if (segment.value("endProgressMeters").toDouble() < segment.value("startProgressMeters").toDouble()
            && !controller.splitApprovedSegment(segment.value("id").toString(), controller.segmentReviewAxisLength()).isEmpty())
            return {};
    }
    const auto approved = controller.m_analysis.storedRunTrackSegments(runId).toArray();
    controller.closeOutingLap();
    return approved;
}

void TelemetryTests::opensTheoreticalBestDonorFromAnotherRun()
{
    // KAN-57: segments approved only on the slower run; the other run is
    // uniformly 10% faster, so every donor lap and the actual best come from
    // a run with no approved segments of its own. The theoretical best still
    // times them on the canonical axis, and opening a sector compares donor
    // against actual best using that same, labelled segmentation.
    QTemporaryDir directory; QVERIFY(directory.isValid());
    QSettings settings; settings.clear(); settings.sync();
    const auto slow = directory.filePath("slow.vbo"), fast = directory.filePath("fast.vbo");
    QVERIFY(writeBytes(slow, EventProjectFixture::routeVbo()));
    QVERIFY(writeBytes(fast, scaledRouteVbo(0.9)));
    AppController controller(nullptr, directory.filePath("recovery.json"));
    QVERIFY(controller.importAnalysisRuns("Donors", {QUrl::fromLocalFile(slow), QUrl::fromLocalFile(fast)}));
    QTRY_COMPARE(controller.vboLoadState(), QString("ready"));
    QTRY_VERIFY(!controller.outingLapsLoading());
    QTRY_VERIFY(!controller.outingComparisonGroupId().isEmpty());
    QString slowName, fastName;
    for (const auto &value : controller.outingLaps()) {
        const auto row = value.toMap();
        if (row.value("type") != "LAP") continue;
        const auto seconds = row.value("endTime").toDouble() - row.value("startTime").toDouble();
        (seconds > 45.0 ? slowName : fastName) = row.value("runName").toString();
    }
    QVERIFY(!slowName.isEmpty() && !fastName.isEmpty() && slowName != fastName);
    const auto approved = approveAllSegmentsOnRun(controller, slowName);
    QVERIFY(!approved.isEmpty());

    controller.requestOutingTheoreticalBest();
    QTRY_COMPARE_WITH_TIMEOUT(controller.outingTheoreticalBest().value("state").toString(), QString("ready"), 30000);
    const auto best = controller.outingTheoreticalBest();
    QCOMPARE(best.value("algorithm").toString(), QString("theoretical-best-v1"));
    const auto actual = best.value("actualBest").toMap();
    QVERIFY(actual.value("label").toString().startsWith(fastName));
    const auto sectors = best.value("sectors").toList();
    QCOMPARE(sectors.size(), approved.size());
    QString timedId;
    for (const auto &value : sectors) {
        const auto sector = value.toMap();
        if (!sector.contains("seconds")) continue;
        if (timedId.isEmpty()) timedId = sector.value("segmentId").toString();
        QVERIFY2(sector.value("sourceLapLabel").toString().startsWith(fastName),
            qPrintable(sector.value("sourceLapLabel").toString()));
        // The actual best is itself in the population: never faster than the theoretical sector.
        QVERIFY(sector.contains("actualSeconds"));
        QVERIFY(sector.value("lossSeconds").toDouble() >= -1e-9);
    }
    QVERIFY(!timedId.isEmpty());
    if (best.contains("totalSeconds")) QVERIFY(best.value("differenceSeconds").toDouble() >= -1e-9);

    QVERIFY(!controller.openTheoreticalBestSector("not-a-real-id"));
    QVERIFY(controller.openTheoreticalBestSector(timedId));
    QVERIFY(controller.comparisonViewOpen());
    QCOMPARE(controller.comparisonFocusSegmentId(), timedId);
    QTRY_VERIFY(controller.comparisonPairReady());
    // Neither lap's own run has approved segments: the canonical segmentation applies, with a note.
    const auto segments = controller.comparisonApprovedSegments();
    QCOMPARE(segments.size(), approved.size());
    QVERIFY(!controller.comparisonSegmentationNote().isEmpty());
    const auto metrics = controller.comparisonSegmentMetrics(timedId);
    QVERIFY(metrics.value("sectorTime").toMap().value("a").toMap().contains("value"));
    QVERIFY(metrics.value("sectorTime").toMap().value("b").toMap().contains("value"));

    // Closing the view drops the borrowed segmentation and the focus request.
    controller.setComparisonViewOpen(false);
    QVERIFY(controller.comparisonFocusSegmentId().isEmpty());
    QVERIFY(controller.comparisonApprovedSegments().isEmpty());
    QVERIFY(controller.comparisonSegmentationNote().isEmpty());
}

void TelemetryTests::opensTheoreticalBestSectorThroughQml()
{
    // KAN-57: the dialog requests the calculation, shows actual best,
    // theoretical total, their difference and the algorithm label, and a
    // sector row opens the Corner Analyzer on that segment.
    QTemporaryDir directory; QVERIFY(directory.isValid());
    QSettings settings; settings.clear(); settings.sync();
    const auto path = directory.filePath("session.vbo");
    QVERIFY(writeBytes(path, EventProjectFixture::routeVbo()));
    AppController controller(nullptr, directory.filePath("recovery.json"));
    QVERIFY(controller.importAnalysisRuns("Theoretical Best QML", {QUrl::fromLocalFile(path)}));
    QTRY_COMPARE(controller.vboLoadState(), QString("ready"));
    QTRY_VERIFY(!controller.outingLapsLoading());
    QTRY_VERIFY(!controller.outingComparisonGroupId().isEmpty());
    const auto runName = controller.outingLaps().first().toMap().value("runName").toString();
    QVERIFY(!approveAllSegmentsOnRun(controller, runName).isEmpty());

    QQmlEngine engine; engine.rootContext()->setContextProperty("appController", &controller);
    QSignalSpy warnings(&engine, &QQmlEngine::warnings);
    QQmlComponent component(&engine);
    component.setData("import QtQuick\nWindow { width: 1000; height: 700; visible: true; "
        "ComparisonDetailPanel { objectName: \"comparisonRoot\"; anchors.fill: parent } "
        "TheoreticalBestDialog { objectName: \"dialog\" } }",
        QUrl::fromLocalFile(QStringLiteral(ANALYSIS_PANEL_QML_PATH)));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    std::unique_ptr<QObject> object(component.create()); QVERIFY2(object, qPrintable(component.errorString()));
    auto *window = qobject_cast<QQuickWindow *>(object.get()); QVERIFY(window);
    window->show(); QVERIFY(QTest::qWaitForWindowExposed(window));

    auto *dialog = window->findChild<QObject *>("dialog"); QVERIFY(dialog);
    QVERIFY(QMetaObject::invokeMethod(dialog, "open"));
    QTRY_COMPARE_WITH_TIMEOUT(controller.outingTheoreticalBest().value("state").toString(), QString("ready"), 30000);
    auto *total = window->findChild<QObject *>("theoreticalBestTotal"); QVERIFY(total);
    auto *actual = window->findChild<QObject *>("theoreticalBestActual"); QVERIFY(actual);
    auto *explanation = window->findChild<QObject *>("theoreticalBestExplanation"); QVERIFY(explanation);
    QTRY_VERIFY(actual->property("text").toString().contains(runName));
    QVERIFY(explanation->property("text").toString().contains("theoretical-best-v1"));
    QVERIFY(explanation->property("text").toString().contains("does not show that the whole lap"));
    if (controller.outingTheoreticalBest().contains("totalSeconds"))
        QVERIFY(total->property("text").toString() != "—");

    // KAN-120: the list is ordered by gain, and the map draws every segment.
    QCOMPARE(controller.outingTheoreticalBest().value("gains").toList().size(),
        controller.outingTheoreticalBest().value("sectors").toList().size());
    QCOMPARE(controller.outingTheoreticalBest().value("map").toMap().value("segments").toList().size(),
        controller.outingTheoreticalBest().value("sectors").toList().size());
    QString timedId;
    int timedRow = -1;
    const auto sectors = controller.outingTheoreticalBest().value("gains").toList();
    for (int i = 0; i < sectors.size() && timedRow < 0; ++i)
        if (sectors[i].toMap().contains("seconds")) { timedRow = i; timedId = sectors[i].toMap().value("segmentId").toString(); }
    QVERIFY(timedRow >= 0);
    auto *list = window->findChild<QQuickItem *>("theoreticalBestSectors"); QVERIFY(list);
    // The analysis can still record its bookkeeping (track inference) after
    // the result is ready; the open dialog then recalculates and its rows are
    // recreated, so a key sent to an old row is lost (KAN-163, see KAN-150).
    // Each attempt looks the row up again and activates it from the keyboard.
    bool opened = false;
    for (int attempt = 0; attempt < 10 && !opened; ++attempt) {
        QTRY_COMPARE_WITH_TIMEOUT(controller.outingTheoreticalBest().value("state").toString(), QString("ready"), 30000);
        QQuickItem *row = nullptr;
        if (!QMetaObject::invokeMethod(list, "itemAtIndex", Q_RETURN_ARG(QQuickItem *, row), Q_ARG(int, timedRow)) || !row) {
            QTest::qWait(100);
            continue;
        }
        QVERIFY(row->isEnabled());
        row->forceActiveFocus();
        if (!QTest::qWaitFor([&] { return row->hasActiveFocus(); }, 1000)) continue;
        QTest::keyClick(window, Qt::Key_Space);
        opened = QTest::qWaitFor([&] { return !dialog->property("visible").toBool(); }, 1000);
    }
    QVERIFY2(opened, "activating the sector row did not open the comparison");
    QVERIFY(controller.comparisonViewOpen());
    auto *panel = window->findChild<QObject *>("comparisonSegmentPanel"); QVERIFY(panel);
    QTRY_VERIFY(panel->property("visible").toBool());
    QTRY_COMPARE_WITH_TIMEOUT(panel->property("selectedSegmentId").toString(), timedId, 20000);
    QTRY_VERIFY(controller.comparisonFocusSegmentId().isEmpty());
    QCOMPARE(warnings.size(), 0);
}

void TelemetryTests::acceptsM3SegmentationCornerAndTheoreticalBestWorkflow()
{
    // KAN-58 (M3 acceptance): proposal -> review -> correction -> save ->
    // reopen -> corner comparison -> donor-sector navigation, with a
    // known-time fixture, cache invalidation, a missing speed sensor and a
    // changed layout. Two runs lap in exactly 48 s each, one 10% quicker in
    // the first half of the revolution and the other in the second half, so
    // the sector theoretical best must be clearly quicker than either lap
    // and must take donors from both runs.
    QTemporaryDir directory; QVERIFY(directory.isValid());
    QSettings settings; settings.clear(); settings.sync();
    const auto first = directory.filePath("fastfirst.vbo"), second = directory.filePath("fastsecond.vbo");
    QVERIFY(writeBytes(first, warpedRouteVbo(true)));
    QVERIFY(writeBytes(second, warpedRouteVbo(false)));
    const auto projectPath = directory.filePath("m3.fetproject");
    const auto ready = [](AppController &controller) {
        controller.requestOutingTheoreticalBest();
        return QTest::qWaitFor([&] { return controller.outingTheoreticalBest().value("state") != "loading"; }, 30000)
            && controller.outingTheoreticalBest().value("state") == "ready";
    };
    QString revision;
    double total = 0.0;
    qsizetype sectorCount = 0;
    {
        AppController controller(nullptr, directory.filePath("recovery-first.json"));
        QVERIFY(controller.importAnalysisRuns("M3 acceptance", {QUrl::fromLocalFile(first), QUrl::fromLocalFile(second)}));
        QTRY_COMPARE(controller.vboLoadState(), QString("ready"));
        QTRY_VERIFY(!controller.outingLapsLoading());
        QTRY_VERIFY(!controller.outingComparisonGroupId().isEmpty());

        // Proposal and review: approve every proposal on one run only.
        const auto approved = approveAllSegmentsOnRun(controller, "Session 1");
        QVERIFY(approved.size() >= 3);

        // Known-time sums.
        QVERIFY2(ready(controller), qPrintable(controller.outingTheoreticalBest().value("message").toString()));
        auto best = controller.outingTheoreticalBest();
        const auto actual = best.value("actualBest").toMap();
        QVERIFY(actual.value("coversWholeLap").toBool());
        QVERIFY2(qAbs(actual.value("lapSeconds").toDouble() - 48.0) < 0.05, qPrintable(actual.value("lapSeconds").toString()));
        QVERIFY(qAbs(actual.value("sectorSumSeconds").toDouble() - actual.value("lapSeconds").toDouble()) < 0.01);
        QVERIFY(best.contains("totalSeconds"));
        total = best.value("totalSeconds").toDouble();
        QVERIFY2(total < 48.0 - 0.5 && total > 43.2 - 0.05, qPrintable(QString::number(total, 'f', 3)));
        QVERIFY(qAbs(best.value("differenceSeconds").toDouble()
            - (actual.value("sectorSumSeconds").toDouble() - total)) < 1e-6);
        QSet<QString> donorRuns;
        for (const auto &value : best.value("sectors").toList())
            donorRuns.insert(value.toMap().value("sourceLapLabel").toString().section(" · ", 0, 0));
        QVERIFY2(donorRuns.contains("Session 1") && donorRuns.contains("Session 2"),
            qPrintable(QStringList(donorRuns.values()).join(", ")));
        revision = best.value("revision").toString();
        QVERIFY(!revision.isEmpty());

        // Correction invalidates the cached result; a finer partition can only lower the sum.
        int lapIndex = -1;
        const auto rows = controller.outingLaps();
        for (int i = 0; i < rows.size() && lapIndex < 0; ++i)
            if (rows[i].toMap().value("type") == "LAP" && rows[i].toMap().value("runName") == "Session 1") lapIndex = i;
        QVERIFY(controller.selectOutingLap(lapIndex));
        QTRY_COMPARE(controller.outingLapDetailState(), QString("ready"));
        controller.requestSegmentReview();
        QTRY_COMPARE(controller.segmentReviewState(), QString("ready"));
        QCOMPARE(controller.outingTheoreticalBest().value("state").toString(), QString("ready"));
        const auto runId = controller.selectedOutingLap().value("runId").toString();
        QJsonObject splittable;
        for (const auto &value : controller.m_analysis.storedRunTrackSegments(runId).toArray())
            if (splittable.isEmpty() && value.toObject().value("endProgressMeters").toDouble()
                    > value.toObject().value("startProgressMeters").toDouble() + 40.0) splittable = value.toObject();
        QVERIFY(!splittable.isEmpty());
        QCOMPARE(controller.splitApprovedSegment(splittable.value("id").toString(),
            (splittable.value("startProgressMeters").toDouble() + splittable.value("endProgressMeters").toDouble()) / 2.0), QString());
        QTRY_COMPARE(controller.outingTheoreticalBest().value("state").toString(), QString("idle"));
        controller.closeOutingLap();
        QVERIFY(ready(controller));
        best = controller.outingTheoreticalBest();
        QVERIFY(best.value("revision").toString() != revision);
        QCOMPARE(best.value("sectors").toList().size(), approved.size() + 1);
        QVERIFY(best.value("totalSeconds").toDouble() <= total + 1e-6);
        revision = best.value("revision").toString();
        total = best.value("totalSeconds").toDouble();
        sectorCount = best.value("sectors").toList().size();

        QVERIFY(controller.saveProject(QUrl::fromLocalFile(projectPath)));
        QVERIFY(!controller.dirty());
    }
    {
        // Reopen: the same approved revision gives the same result.
        AppController reopened(nullptr, directory.filePath("recovery-reopen.json"));
        reopened.requestOpenProject(QUrl::fromLocalFile(projectPath));
        QTRY_COMPARE(reopened.vboLoadState(), QString("ready"));
        QTRY_VERIFY(!reopened.outingLapsLoading());
        QTRY_VERIFY(!reopened.outingComparisonGroupId().isEmpty());
        QVERIFY(ready(reopened));
        const auto best = reopened.outingTheoreticalBest();
        QCOMPARE(best.value("revision").toString(), revision);
        QCOMPARE(best.value("sectors").toList().size(), sectorCount);
        QVERIFY(qAbs(best.value("totalSeconds").toDouble() - total) < 1e-6);

        // Donor-sector navigation into the corner comparison; the route has
        // no speed channel, so corner speeds are unavailable, never derived.
        QString cornerId;
        for (const auto &value : best.value("sectors").toList())
            if (cornerId.isEmpty() && value.toMap().value("type") == "corner" && value.toMap().contains("seconds"))
                cornerId = value.toMap().value("segmentId").toString();
        QVERIFY(!cornerId.isEmpty());
        QVERIFY(reopened.openTheoreticalBestSector(cornerId));
        QTRY_VERIFY(reopened.comparisonPairReady());
        QCOMPARE(reopened.comparisonApprovedSegments().size(), sectorCount);
        const auto metrics = reopened.comparisonSegmentMetrics(cornerId);
        QVERIFY(metrics.value("sectorTime").toMap().value("a").toMap().contains("value"));
        QVERIFY(metrics.value("sectorTime").toMap().value("b").toMap().contains("value"));
        QCOMPARE(metrics.value("corner").toMap().value("entry").toMap().value("a").toMap().value("unavailableReason").toString(),
            QString(cornerPhaseSpeedChannelMissing));
        reopened.setComparisonViewOpen(false);

        // A layout change: the approved segments no longer apply to the run.
        QString runId;
        for (const auto &value : reopened.outingLaps())
            if (value.toMap().value("runName") == "Session 1") runId = value.toMap().value("runId").toString();
        QVERIFY(reopened.setRunTrackConfiguration(runId, "Changed", "clockwise"));
        QTRY_VERIFY(!reopened.outingLapsLoading() && reopened.m_analysis.m_outingLapRequestedKey == reopened.m_analysis.outingLapKey());
        QTRY_COMPARE(reopened.outingTheoreticalBest().value("state").toString(), QString("idle"));
        reopened.requestOutingTheoreticalBest();
        QTRY_VERIFY(reopened.outingTheoreticalBest().value("state") != "loading");
        QCOMPARE(reopened.outingTheoreticalBest().value("state").toString(), QString("unavailable"));
    }
}

void TelemetryTests::derivesTimeLossObservationsForComparisonPair()
{
    // KAN-59: one loss window per approved segment for the comparison pair.
    // Both runs lap in 48 s but are quick in opposite halves, so windows show
    // real losses and gains that net to the lap-time difference, and the
    // running delta is reported separately from each window's increment.
    QTemporaryDir directory; QVERIFY(directory.isValid());
    QSettings settings; settings.clear(); settings.sync();
    const auto first = directory.filePath("fastfirst.vbo"), second = directory.filePath("fastsecond.vbo");
    QVERIFY(writeBytes(first, warpedRouteVbo(true)));
    QVERIFY(writeBytes(second, warpedRouteVbo(false)));
    AppController controller(nullptr, directory.filePath("recovery.json"));
    QVERIFY(controller.importAnalysisRuns("Losses", {QUrl::fromLocalFile(first), QUrl::fromLocalFile(second)}));
    QTRY_COMPARE(controller.vboLoadState(), QString("ready"));
    QTRY_VERIFY(!controller.outingLapsLoading());
    QTRY_VERIFY(!controller.outingComparisonGroupId().isEmpty());
    QVERIFY(!approveAllSegmentsOnRun(controller, "Session 1").isEmpty());
    QVERIFY(!controller.comparisonTimeLossObservations().value("valid").toBool()); // no pair yet

    // A pair from different runs, measured against the canonical segments.
    controller.requestOutingTheoreticalBest();
    QTRY_COMPARE_WITH_TIMEOUT(controller.outingTheoreticalBest().value("state").toString(), QString("ready"), 30000);
    const auto best = controller.outingTheoreticalBest();
    const auto actualRun = best.value("actualBest").toMap().value("label").toString().section(" · ", 0, 0);
    QString crossRunSector;
    for (const auto &value : best.value("sectors").toList()) {
        const auto sector = value.toMap();
        if (crossRunSector.isEmpty() && sector.contains("seconds")
            && sector.value("sourceLapLabel").toString().section(" · ", 0, 0) != actualRun)
            crossRunSector = sector.value("segmentId").toString();
    }
    QVERIFY(!crossRunSector.isEmpty());
    QVERIFY(controller.openTheoreticalBestSector(crossRunSector));
    QTRY_VERIFY(controller.comparisonPairReady());

    const auto losses = controller.comparisonTimeLossObservations();
    QVERIFY(losses.value("valid").toBool());
    QCOMPARE(losses.value("algorithm").toString(), QString("time-loss-windows-v1"));
    const auto windows = losses.value("windows").toList();
    QCOMPARE(windows.size(), controller.comparisonApprovedSegments().size());
    QVERIFY(losses.value("allWindowsTimed").toBool());
    QVERIFY2(std::abs(losses.value("timedIncrementSumSeconds").toDouble() - losses.value("lapDeltaSeconds").toDouble()) < 0.01,
        qPrintable(QString("%1 vs %2").arg(losses.value("timedIncrementSumSeconds").toDouble())
            .arg(losses.value("lapDeltaSeconds").toDouble())));
    bool lost = false, gained = false;
    double previousEnd = 0.0, previousCumulative = 0.0;
    for (qsizetype i = 0; i < windows.size(); ++i) {
        const auto window = windows[i].toMap();
        const auto increment = window.value("incrementSeconds").toDouble();
        lost |= increment > 0.5;
        gained |= increment < -0.5;
        QVERIFY(std::abs(increment - (window.value("cumulativeAtEndSeconds").toDouble()
            - window.value("cumulativeAtStartSeconds").toDouble())) < 1e-6);
        // Windows are in track order and never overlap; the running delta carries over.
        if (i > 0) {
            QVERIFY(window.value("startMeters").toDouble() >= previousEnd - 1e-6);
            QVERIFY(std::abs(window.value("cumulativeAtStartSeconds").toDouble() - previousCumulative) < 0.01);
        }
        previousEnd = window.value("endMeters").toDouble();
        previousCumulative = window.value("cumulativeAtEndSeconds").toDouble();
        if (window.value("role") == "continuation")
            QVERIFY(!window.value("cornerSegmentId").toString().isEmpty());
    }
    QVERIFY(lost && gained);
}

void TelemetryTests::ranksTimeLossesAndRecalculatesOnExclusion()
{
    // KAN-60: every eligible lap against the group's best lap, one window per
    // approved segment, largest loss first. Excluding a lap recalculates the
    // ranking (and can change the reference), and the dialog says an observed
    // loss is not a guaranteed gain.
    QTemporaryDir directory; QVERIFY(directory.isValid());
    QSettings settings; settings.clear(); settings.sync();
    const auto first = directory.filePath("fastfirst.vbo"), second = directory.filePath("fastsecond.vbo");
    QVERIFY(writeBytes(first, warpedRouteVbo(true)));
    QVERIFY(writeBytes(second, warpedRouteVbo(false)));
    AppController controller(nullptr, directory.filePath("recovery.json"));
    QVERIFY(controller.importAnalysisRuns("Ranking", {QUrl::fromLocalFile(first), QUrl::fromLocalFile(second)}));
    QTRY_COMPARE(controller.vboLoadState(), QString("ready"));
    QTRY_VERIFY(!controller.outingLapsLoading());
    QTRY_VERIFY(!controller.outingComparisonGroupId().isEmpty());
    QVERIFY(!approveAllSegmentsOnRun(controller, "Session 1").isEmpty());

    QQmlEngine engine; engine.rootContext()->setContextProperty("appController", &controller);
    QSignalSpy warnings(&engine, &QQmlEngine::warnings);
    QQmlComponent component(&engine);
    component.setData("import QtQuick\nWindow { width: 1000; height: 700; visible: true; "
        "TimeLossDialog { objectName: \"dialog\" } }", QUrl::fromLocalFile(QStringLiteral(ANALYSIS_PANEL_QML_PATH)));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    std::unique_ptr<QObject> object(component.create()); QVERIFY2(object, qPrintable(component.errorString()));
    auto *window = qobject_cast<QQuickWindow *>(object.get()); QVERIFY(window);
    window->show(); QVERIFY(QTest::qWaitForWindowExposed(window));
    auto *dialog = window->findChild<QObject *>("dialog"); QVERIFY(dialog);
    QVERIFY(QMetaObject::invokeMethod(dialog, "open"));
    QTRY_COMPARE_WITH_TIMEOUT(controller.outingTimeLossRanking().value("state").toString(), QString("ready"), 30000);
    // By default only each run's best lap is compared (the other run's best here).
    QCOMPARE(controller.outingTimeLossRanking().value("scope").toString(), QString("runBests"));
    QCOMPARE(controller.outingTimeLossRanking().value("comparedLapCount").toInt(), 1);
    controller.setOutingTimeLossAllLaps(true);

    auto ranking = controller.outingTimeLossRanking();
    QCOMPARE(ranking.value("scope").toString(), QString("allLaps"));
    const auto reference = ranking.value("referenceLap").toMap();
    QCOMPARE(ranking.value("referenceLabel").toString(), controller.outingTheoreticalBest().value("actualBest").toMap().value("label").toString());
    QCOMPARE(ranking.value("algorithm").toString(), QString("time-loss-windows-v1"));
    auto losses = ranking.value("losses").toList();
    QVERIFY(!losses.isEmpty());
    QVERIFY(ranking.value("observationCount").toInt() >= losses.size());
    QVERIFY(ranking.value("comparedLapCount").toInt() >= 2);
    for (qsizetype i = 0; i < losses.size(); ++i) {
        const auto loss = losses[i].toMap();
        QVERIFY(loss.value("lossSeconds").toDouble() > 0.0);
        if (i > 0) QVERIFY(losses[i - 1].toMap().value("lossSeconds").toDouble() >= loss.value("lossSeconds").toDouble());
        QVERIFY(loss.value("lapReference").toMap() != reference);
        QVERIFY(!loss.value("lapLabel").toString().isEmpty());
        QVERIFY(!loss.value("name").toString().isEmpty());
        QVERIFY(loss.value("coverageLap").toDouble() > 0.9 && loss.value("coverageReference").toDouble() > 0.9);
        if (loss.value("role") == "continuation") QVERIFY(!loss.value("cornerName").toString().isEmpty());
    }
    auto *explanation = window->findChild<QObject *>("timeLossExplanation"); QVERIFY(explanation);
    QTRY_VERIFY(explanation->property("text").toString().contains("not a guaranteed or necessarily safe gain"));
    auto *list = window->findChild<QQuickItem *>("timeLossList"); QVERIFY(list);
    QTRY_COMPARE(list->property("count").toInt(), losses.size());

    // Excluding the top loss's lap recalculates the ranking while the dialog is open.
    const auto excluded = losses.first().toMap().value("lapReference").toMap();
    QVERIFY(controller.setOutingLapExcluded(excluded, true, "Traffic"));
    QTRY_VERIFY_WITH_TIMEOUT(controller.outingTimeLossRanking().value("state") == "ready"
        && controller.outingTimeLossRanking().value("revision").toString() == ranking.value("revision").toString()
        && [&] {
            for (const auto &value : controller.outingTimeLossRanking().value("losses").toList())
                if (value.toMap().value("lapReference").toMap() == excluded) return false;
            return true;
        }(), 30000);
    QVERIFY(controller.outingTimeLossRanking().value("comparedLapCount").toInt() < ranking.value("comparedLapCount").toInt()
        || controller.outingTimeLossRanking().value("referenceLap").toMap() != reference);
    QCOMPARE(warnings.size(), 0);
}

void TelemetryTests::navigatesFromRankedLossToCornerEvidence()
{
    // KAN-61: Day results -> Time losses -> a loss opens A (the loss's lap)
    // against B (the reference) in the Corner Analyzer, focused and zoomed on
    // the loss window. Lap A opens at the window with no video available
    // (nothing blocks), and returning reopens the ranking on the same row.
    QTemporaryDir directory; QVERIFY(directory.isValid());
    QSettings settings; settings.clear(); settings.sync();
    const auto first = directory.filePath("fastfirst.vbo"), second = directory.filePath("fastsecond.vbo");
    QVERIFY(writeBytes(first, warpedRouteVbo(true)));
    QVERIFY(writeBytes(second, warpedRouteVbo(false)));
    AppController controller(nullptr, directory.filePath("recovery.json"));
    QVERIFY(controller.importAnalysisRuns("Navigation", {QUrl::fromLocalFile(first), QUrl::fromLocalFile(second)}));
    QTRY_COMPARE(controller.vboLoadState(), QString("ready"));
    QTRY_VERIFY(!controller.outingLapsLoading());
    QTRY_VERIFY(!controller.outingComparisonGroupId().isEmpty());
    QVERIFY(!approveAllSegmentsOnRun(controller, "Session 1").isEmpty());

    QQmlEngine engine; engine.rootContext()->setContextProperty("appController", &controller);
    QSignalSpy warnings(&engine, &QQmlEngine::warnings);
    QQmlComponent component(&engine);
    component.setData("import QtQuick\nWindow { width: 1100; height: 760; visible: true; "
        "OutingLapPanel { objectName: \"outingRoot\"; anchors.fill: parent; visible: !appController.comparisonViewOpen } "
        "ComparisonDetailPanel { objectName: \"comparisonRoot\"; anchors.fill: parent; visible: appController.comparisonViewOpen } }",
        QUrl::fromLocalFile(QStringLiteral(ANALYSIS_PANEL_QML_PATH)));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    std::unique_ptr<QObject> object(component.create()); QVERIFY2(object, qPrintable(component.errorString()));
    auto *window = qobject_cast<QQuickWindow *>(object.get()); QVERIFY(window);
    window->show(); QVERIFY(QTest::qWaitForWindowExposed(window));
    const auto activate = [&](QQuickItem *item) { item->forceActiveFocus(); QTest::keyClick(window, Qt::Key_Space); };

    QQuickItem *open = nullptr;
    controller.setOutingTimeLossAllLaps(true);
    QTRY_VERIFY((open = window->findChild<QQuickItem *>("openTimeLosses")) && open->isEnabled());
    activate(open);
    auto *dialog = window->findChild<QObject *>("timeLossDialog"); QVERIFY(dialog);
    QTRY_VERIFY(dialog->property("visible").toBool());
    QTRY_COMPARE_WITH_TIMEOUT(controller.outingTimeLossRanking().value("state").toString(), QString("ready"), 30000);
    const auto ranking = controller.outingTimeLossRanking();
    const auto losses = ranking.value("losses").toList();
    QVERIFY(losses.size() >= 2);
    // Pick a window that does not wrap the gate so its zoom range is simple.
    int chosen = -1;
    for (int i = 0; i < losses.size() && chosen < 0; ++i)
        if (losses[i].toMap().value("endMeters").toDouble() > losses[i].toMap().value("startMeters").toDouble() + 1.0) chosen = i;
    QVERIFY(chosen >= 0);
    const auto loss = losses[chosen].toMap();
    auto *list = window->findChild<QQuickItem *>("timeLossList"); QVERIFY(list);
    QQuickItem *row = nullptr;
    QTRY_VERIFY(QMetaObject::invokeMethod(list, "itemAtIndex", Q_RETURN_ARG(QQuickItem *, row), Q_ARG(int, chosen)) && row);
    activate(row);

    QTRY_VERIFY(!dialog->property("visible").toBool());
    QVERIFY(controller.comparisonViewOpen());
    QTRY_VERIFY(controller.comparisonPairReady());
    const auto pair = controller.comparisonSlots();
    QCOMPARE(pair[0].toMap().value("lap").toMap().value("reference").toMap(), loss.value("lapReference").toMap());
    QCOMPARE(pair[1].toMap().value("lap").toMap().value("reference").toMap(), ranking.value("referenceLap").toMap());
    auto *segmentPanel = window->findChild<QObject *>("comparisonSegmentPanel"); QVERIFY(segmentPanel);
    auto *comparisonRoot = window->findChild<QObject *>("comparisonRoot"); QVERIFY(comparisonRoot);
    QTRY_VERIFY(segmentPanel->property("visible").toBool());
    QTRY_COMPARE_WITH_TIMEOUT(segmentPanel->property("selectedSegmentId").toString(), loss.value("segmentId").toString(), 20000);
    QTRY_VERIFY(std::abs(comparisonRoot->property("zoomStart").toDouble() - loss.value("startMeters").toDouble()) < 1e-6
        && std::abs(comparisonRoot->property("zoomEnd").toDouble() - loss.value("endMeters").toDouble()) < 1e-6);
    QTRY_VERIFY(controller.comparisonFocusSegmentId().isEmpty());

    // Lap A at the loss window; this fixture has no video, which must not block it.
    QQuickItem *openLapA = nullptr;
    QTRY_VERIFY((openLapA = window->findChild<QQuickItem *>("cornerAnalyzerOpenLapA")) && openLapA->isVisible());
    activate(openLapA);
    QTRY_COMPARE(controller.selectedOutingLap().value("reference").toMap(), loss.value("lapReference").toMap());
    QTRY_COMPARE(controller.outingLapDetailState(), QString("ready"));
    QVERIFY(!controller.outingLapVideoAvailable());
    const auto lapStart = controller.selectedOutingLap().value("startTime").toDouble();
    const auto lapEnd = controller.selectedOutingLap().value("endTime").toDouble();
    QVERIFY(controller.outingLapCursor() > lapStart + 0.5 && controller.outingLapCursor() < lapEnd);

    // Back to the comparison, then back to Day results: the ranking reopens on the same row.
    controller.closeOutingLap();
    QVERIFY(controller.comparisonViewOpen());
    controller.setComparisonViewOpen(false);
    QTRY_VERIFY(dialog->property("visible").toBool());
    QTRY_COMPARE(list->property("currentIndex").toInt(), chosen);
    QCOMPARE(warnings.size(), 0);
}

void TelemetryTests::reportsLapAndSectorConsistency()
{
    // KAN-62: every lap of both runs takes exactly 48 s, so the lap spread is
    // ~0, while sectors differ between the runs' quick halves. Excluding laps
    // below the minimum makes lap consistency unavailable, not a guess.
    QTemporaryDir directory; QVERIFY(directory.isValid());
    QSettings settings; settings.clear(); settings.sync();
    const auto first = directory.filePath("fastfirst.vbo"), second = directory.filePath("fastsecond.vbo");
    QVERIFY(writeBytes(first, warpedRouteVbo(true)));
    QVERIFY(writeBytes(second, warpedRouteVbo(false)));
    AppController controller(nullptr, directory.filePath("recovery.json"));
    QVERIFY(controller.importAnalysisRuns("Consistency", {QUrl::fromLocalFile(first), QUrl::fromLocalFile(second)}));
    QTRY_COMPARE(controller.vboLoadState(), QString("ready"));
    QTRY_VERIFY(!controller.outingLapsLoading());
    QTRY_VERIFY(!controller.outingComparisonGroupId().isEmpty());

    auto laps = controller.outingLapConsistency();
    QCOMPARE(laps.value("algorithm").toString(), QString("consistency-iqr-v1"));
    const auto day = laps.value("day").toMap();
    const auto eligible = controller.outingRanking().value("eligibleLapCount").toInt();
    QCOMPARE(day.value("count").toInt(), eligible); // the ranking's own population
    QVERIFY(day.value("available").toBool());
    QVERIFY(std::abs(day.value("median").toDouble() - 48.0) < 0.05);
    QVERIFY(day.value("interquartileRange").toDouble() < 0.05);
    QCOMPARE(laps.value("runs").toList().size(), 2);

    QVERIFY(!approveAllSegmentsOnRun(controller, "Session 1").isEmpty());
    controller.requestOutingTheoreticalBest();
    QTRY_COMPARE_WITH_TIMEOUT(controller.outingTheoreticalBest().value("state").toString(), QString("ready"), 30000);
    bool someSpread = false;
    for (const auto &value : controller.outingTheoreticalBest().value("sectors").toList()) {
        const auto consistency = value.toMap().value("consistency").toMap();
        QVERIFY2(consistency.value("available").toBool(), qPrintable(value.toMap().value("name").toString()));
        QCOMPARE(consistency.value("count").toInt(), eligible);
        QVERIFY(consistency.value("q1").toDouble() <= consistency.value("median").toDouble());
        QVERIFY(consistency.value("median").toDouble() <= consistency.value("q3").toDouble());
        someSpread |= consistency.value("interquartileRange").toDouble() > 0.1;
    }
    QVERIFY(someSpread); // the runs' opposite quick halves show up per sector

    // Exclude laps until fewer than the minimum remain.
    int remaining = eligible;
    for (const auto &value : controller.outingLaps()) {
        if (remaining <= 2) break;
        const auto row = value.toMap();
        if (row.value("type") != "LAP" || !row.value("referenceEligible").toBool()) continue;
        QVERIFY(controller.setOutingLapExcluded(row.value("reference").toMap(), true, "Test"));
        --remaining;
    }
    QTRY_VERIFY(!controller.outingLapsLoading());
    QTRY_COMPARE(controller.outingLapConsistency().value("day").toMap().value("count").toInt(), 2);
    laps = controller.outingLapConsistency();
    QVERIFY(!laps.value("day").toMap().value("available").toBool());
    QCOMPARE(laps.value("day").toMap().value("unavailableReason").toString(), QString("tooFewSamples"));
    QVERIFY(!laps.value("day").toMap().contains("median"));
}

void TelemetryTests::reportsCornerVariabilityWithGpsLimits()
{
    // KAN-63: both runs drive the identical path with no speed, brake,
    // throttle or GPS-accuracy channel. The line spread is ~0 but never
    // claimed resolvable (no stated accuracy); speed and braking metrics
    // have no samples rather than being derived from GPS.
    QTemporaryDir directory; QVERIFY(directory.isValid());
    QSettings settings; settings.clear(); settings.sync();
    const auto first = directory.filePath("fastfirst.vbo"), second = directory.filePath("fastsecond.vbo");
    QVERIFY(writeBytes(first, warpedRouteVbo(true)));
    QVERIFY(writeBytes(second, warpedRouteVbo(false)));
    AppController controller(nullptr, directory.filePath("recovery.json"));
    QVERIFY(controller.importAnalysisRuns("Variability", {QUrl::fromLocalFile(first), QUrl::fromLocalFile(second)}));
    QTRY_COMPARE(controller.vboLoadState(), QString("ready"));
    QTRY_VERIFY(!controller.outingLapsLoading());
    QTRY_VERIFY(!controller.outingComparisonGroupId().isEmpty());
    QVERIFY(!approveAllSegmentsOnRun(controller, "Session 1").isEmpty());
    controller.requestOutingTheoreticalBest();
    QTRY_COMPARE_WITH_TIMEOUT(controller.outingTheoreticalBest().value("state").toString(), QString("ready"), 30000);
    const auto best = controller.outingTheoreticalBest();
    QCOMPARE(best.value("variabilityAlgorithm").toString(), QString("driving-variability-v1"));
    const auto eligible = controller.outingRanking().value("eligibleLapCount").toInt();
    int corners = 0;
    for (const auto &value : best.value("sectors").toList()) {
        const auto sector = value.toMap();
        if (sector.value("type") != "corner") { QVERIFY(!sector.contains("variability")); continue; }
        ++corners;
        const auto variability = sector.value("variability").toMap();
        const auto line = variability.value("lineOffset").toMap();
        QVERIFY2(line.value("available").toBool(), qPrintable(sector.value("name").toString()));
        QCOMPARE(line.value("count").toInt(), eligible);
        QVERIFY(std::abs(line.value("median").toDouble()) < 1.0);
        QVERIFY(line.value("interquartileRange").toDouble() < 0.5);
        QVERIFY(!variability.contains("typicalGpsAccuracyMeters"));
        QVERIFY(!variability.value("lineSpreadResolvable").toBool());
        for (const auto *metric : {"apexSpeed", "minimumSpeed", "exitSpeed", "brakingPointMeasured", "brakingPointInferred"})
            QCOMPARE(variability.value(metric).toMap().value("count").toInt(), 0);
    }
    QVERIFY(corners > 0);
}

void TelemetryTests::showsSectionProgressionBetweenSessions()
{
    // KAN-64: sections x sessions, each cell the typical time and spread of
    // that session's laps in the section; a cell lists its laps, and choosing
    // one opens it. The two sessions are quick in opposite halves, so their
    // typical times differ per section.
    QTemporaryDir directory; QVERIFY(directory.isValid());
    QSettings settings; settings.clear(); settings.sync();
    const auto first = directory.filePath("fastfirst.vbo"), second = directory.filePath("fastsecond.vbo");
    QVERIFY(writeBytes(first, warpedRouteVbo(true)));
    QVERIFY(writeBytes(second, warpedRouteVbo(false)));
    AppController controller(nullptr, directory.filePath("recovery.json"));
    QVERIFY(controller.importAnalysisRuns("Progression", {QUrl::fromLocalFile(first), QUrl::fromLocalFile(second)}));
    QTRY_COMPARE(controller.vboLoadState(), QString("ready"));
    QTRY_VERIFY(!controller.outingLapsLoading());
    QTRY_VERIFY(!controller.outingComparisonGroupId().isEmpty());
    const auto approved = approveAllSegmentsOnRun(controller, "Session 1");
    QVERIFY(!approved.isEmpty());

    QQmlEngine engine; engine.rootContext()->setContextProperty("appController", &controller);
    QSignalSpy warnings(&engine, &QQmlEngine::warnings);
    QQmlComponent component(&engine);
    component.setData("import QtQuick\nWindow { width: 1200; height: 800; visible: true; "
        "OutingProgressionDialog { objectName: \"dialog\" } }", QUrl::fromLocalFile(QStringLiteral(ANALYSIS_PANEL_QML_PATH)));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    std::unique_ptr<QObject> object(component.create()); QVERIFY2(object, qPrintable(component.errorString()));
    auto *window = qobject_cast<QQuickWindow *>(object.get()); QVERIFY(window);
    window->show(); QVERIFY(QTest::qWaitForWindowExposed(window));
    auto *dialog = window->findChild<QObject *>("dialog"); QVERIFY(dialog);
    QVERIFY(QMetaObject::invokeMethod(dialog, "open"));
    auto *tabs = window->findChild<QObject *>("progressionTabs"); QVERIFY(tabs);
    QTRY_VERIFY(dialog->property("visible").toBool());
    tabs->setProperty("currentIndex", 1);
    QTRY_COMPARE_WITH_TIMEOUT(controller.outingSectorProgression().value("state").toString(), QString("ready"), 30000);

    const auto progression = controller.outingSectorProgression();
    const auto sessions = progression.value("sessions").toList();
    QCOMPARE(sessions.size(), 2);
    QCOMPARE(sessions[0].toMap().value("runName").toString(), QString("Session 1"));
    const auto segments = progression.value("segments").toList();
    QCOMPARE(segments.size(), approved.size());
    bool sessionsDiffer = false;
    for (const auto &value : segments) {
        const auto row = value.toMap();
        const auto cells = row.value("cells").toList();
        QCOMPARE(cells.size(), 2);
        for (const auto &cellValue : cells) {
            const auto cell = cellValue.toMap();
            QCOMPARE(cell.value("laps").toList().size(), cell.value("summary").toMap().value("count").toInt());
        }
        const auto a = cells[0].toMap().value("summary").toMap(), b = cells[1].toMap().value("summary").toMap();
        if (a.value("available").toBool() && b.value("available").toBool())
            sessionsDiffer |= std::abs(a.value("median").toDouble() - b.value("median").toDouble()) > 0.3;
    }
    QVERIFY(sessionsDiffer);

    // A cell lists its laps; choosing one opens it.
    const auto findVisual = [](auto &&self, QQuickItem *item, const QString &name) -> QQuickItem * {
        if (item->objectName() == name) return item;
        for (auto *child : item->childItems()) if (auto *found = self(self, child, name)) return found;
        return nullptr;
    };
    auto *grid = window->findChild<QQuickItem *>("sectionProgressionGrid"); QVERIFY(grid);
    QQuickItem *cell = nullptr;
    QTRY_VERIFY((cell = findVisual(findVisual, grid, "sectionCell-0-0")) && cell->isVisible());
    cell->forceActiveFocus(); QTest::keyClick(window, Qt::Key_Space);
    auto *popup = window->findChild<QObject *>("sectionCellLaps"); QVERIFY(popup);
    QTRY_VERIFY2(popup->property("visible").toBool(), "cell click did not open the lap list");
    auto *lapList = window->findChild<QQuickItem *>("sectionCellLapList"); QVERIFY(lapList);
    QTRY_VERIFY2(lapList->property("count").toInt() > 0, qPrintable(QString::number(lapList->property("count").toInt())));
    QQuickItem *lap = nullptr;
    QTRY_VERIFY(QMetaObject::invokeMethod(lapList, "itemAtIndex", Q_RETURN_ARG(QQuickItem *, lap), Q_ARG(int, 0)) && lap);
    const auto expected = segments[0].toMap().value("cells").toList()[0].toMap().value("laps").toList()[0].toMap().value("reference").toMap();
    lap->forceActiveFocus(); QTest::keyClick(window, Qt::Key_Space);
    QTRY_VERIFY(!dialog->property("visible").toBool());
    QTRY_COMPARE(controller.selectedOutingLap().value("reference").toMap(), expected);
    QCOMPARE(warnings.size(), 0);
}

void TelemetryTests::showsAbGgScatterWithPeaks()
{
    // KAN-66: session 2 is 90 % of session 1's accelerations, so every peak is
    // known. Peaks and counts come from all samples, the drawn points are
    // capped, and a narrower range has fewer samples.
    QTemporaryDir directory; QVERIFY(directory.isValid());
    QSettings settings; settings.clear(); settings.sync();
    const auto first = directory.filePath("full.vbo"), second = directory.filePath("ninety.vbo");
    QVERIFY(writeBytes(first, routeVboWithAccelerations(1.0)));
    QVERIFY(writeBytes(second, routeVboWithAccelerations(0.9)));
    AppController controller(nullptr, directory.filePath("recovery.json"));
    QVERIFY(controller.importAnalysisRuns("G-G", {QUrl::fromLocalFile(first), QUrl::fromLocalFile(second)}));
    QTRY_COMPARE(controller.vboLoadState(), QString("ready"));
    QTRY_VERIFY(!controller.outingLapsLoading());
    QVariantMap a, b;
    for (const auto &value : controller.comparisonLaps()) {
        const auto row = value.toMap();
        if (a.isEmpty() && row.value("runName") == "Session 1") a = row;
        if (b.isEmpty() && row.value("runName") == "Session 2") b = row;
    }
    QVERIFY(!a.isEmpty() && !b.isEmpty());
    QVERIFY(controller.selectComparisonLap(0, a.value("reference").toMap()));
    QVERIFY(controller.selectComparisonLap(1, b.value("reference").toMap()));
    QTRY_VERIFY(controller.comparisonPairReady());
    const double length = controller.comparisonProgressAxisLength();
    const auto scatter = controller.comparisonGgScatter(0.0, length, 100);
    QVERIFY(scatter.value("valid").toBool());
    const auto laps = scatter.value("laps").toList();
    QCOMPARE(laps.size(), 2);
    const auto lapA = laps[0].toMap(), lapB = laps[1].toMap();
    QVERIFY(lapA.value("valid").toBool() && lapB.value("valid").toBool());
    QCOMPARE(lapA.value("longitudinalChannel").toString(), QString("longacc-calc"));
    QVERIFY(lapA.value("sharedClock").toBool());
    QVERIFY(lapA.value("sampleCount").toInt() > 200);
    QVERIFY(lapA.value("points").toList().size() <= 104); // drawn points capped, peaks kept
    const auto peak = [](const QVariantMap &lap, const char *key) {
        return lap.value("peaks").toMap().value(key).toMap().value("value").toDouble();
    };
    QVERIFY2(std::abs(peak(lapA, "lateral") - 1.0) < 0.02, qPrintable(QString::number(peak(lapA, "lateral"))));
    QVERIFY(std::abs(peak(lapB, "lateral") - 0.9) < 0.02);
    QVERIFY(std::abs(peak(lapA, "braking") - 0.5) < 0.02);
    QVERIFY(std::abs(peak(lapB, "braking") - 0.45) < 0.02);
    QVERIFY(peak(lapA, "combined") >= peak(lapA, "lateral") - 1e-9);
    // Decimation never changes the peaks.
    const auto dense = controller.comparisonGgScatter(0.0, length, 100000).value("laps").toList()[0].toMap();
    QVERIFY(std::abs(peak(dense, "lateral") - peak(lapA, "lateral")) < 1e-12);
    QCOMPARE(dense.value("sampleCount").toInt(), lapA.value("sampleCount").toInt());
    // A narrower range has fewer samples.
    const auto part = controller.comparisonGgScatter(0.0, length / 4, 100).value("laps").toList()[0].toMap();
    QVERIFY(part.value("sampleCount").toInt() < lapA.value("sampleCount").toInt() / 2);

    // The panel shows the peaks.
    QQmlEngine engine; engine.rootContext()->setContextProperty("appController", &controller);
    QSignalSpy warnings(&engine, &QQmlEngine::warnings);
    QQmlComponent component(&engine);
    component.setData("import QtQuick\nWindow { width: 1300; height: 800; visible: true; "
        "ComparisonDetailPanel { objectName: \"comparisonRoot\"; anchors.fill: parent } }",
        QUrl::fromLocalFile(QStringLiteral(ANALYSIS_PANEL_QML_PATH)));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    std::unique_ptr<QObject> object(component.create()); QVERIFY2(object, qPrintable(component.errorString()));
    auto *window = qobject_cast<QQuickWindow *>(object.get()); QVERIFY(window);
    window->show(); QVERIFY(QTest::qWaitForWindowExposed(window));
    auto *toggle = window->findChild<QQuickItem *>("comparisonToggleGg"); QVERIFY(toggle);
    toggle->forceActiveFocus(); QTest::keyClick(window, Qt::Key_Space);
    auto *panel = window->findChild<QQuickItem *>("comparisonGgPanel"); QVERIFY(panel);
    QTRY_VERIFY(panel->isVisible());
    auto *lateralA = window->findChild<QObject *>("ggLateralA"); QVERIFY(lateralA);
    QTRY_COMPARE(lateralA->property("text").toString(), QString("1.00 g"));
    auto *brakingB = window->findChild<QObject *>("ggBrakingB"); QVERIFY(brakingB);
    QTRY_COMPARE(brakingB->property("text").toString(), QString("0.45 g"));
    QCOMPARE(warnings.size(), 0);
}

void TelemetryTests::summarizesRecordedTemperaturesPerRunAndSection()
{
    // KAN-67: only recorded temperature channels are summarized (the second
    // run has none); placeholder zeros are excluded and counted; every
    // recorded section has its own summary with coverage.
    QTemporaryDir directory; QVERIFY(directory.isValid());
    QSettings settings; settings.clear(); settings.sync();
    const auto first = directory.filePath("hot.vbo"), second = directory.filePath("plain.vbo");
    QVERIFY(writeBytes(first, routeVboWithCoolant()));
    QVERIFY(writeBytes(second, EventProjectFixture::routeVbo()));
    AppController controller(nullptr, directory.filePath("recovery.json"));
    QVERIFY(controller.importAnalysisRuns("Temperatures", {QUrl::fromLocalFile(first), QUrl::fromLocalFile(second)}));
    QTRY_COMPARE(controller.vboLoadState(), QString("ready"));
    QTRY_VERIFY(!controller.outingLapsLoading());
    controller.requestOutingChannelSummaries();
    QTRY_COMPARE_WITH_TIMEOUT(controller.outingChannelSummaries().value("state").toString(), QString("ready"), 30000);
    const auto summaries = controller.outingChannelSummaries();
    QCOMPARE(summaries.value("algorithm").toString(), QString("channel-summary-v1"));
    const auto runs = summaries.value("runs").toList();
    QCOMPARE(runs.size(), 2);
    QVariantMap hot, plain;
    for (const auto &value : runs) (value.toMap().value("channels").toList().isEmpty() ? plain : hot) = value.toMap();
    QVERIFY(!hot.isEmpty() && !plain.isEmpty()); // the run without a sensor lists none, invents none
    const auto coolant = hot.value("channels").toList().first().toMap();
    QCOMPARE(coolant.value("channel").toString(), QString("coolant_temp-obd"));
    const auto run = coolant.value("run").toMap();
    QVERIFY(run.value("valid").toBool());
    QCOMPARE(run.value("excludedArtifacts").toInt(), 3);
    QVERIFY(std::abs(run.value("minimum").toDouble() - 80.03) < 1e-3);
    QVERIFY(run.value("maximum").toDouble() > run.value("mean").toDouble());
    QVERIFY(run.value("coverage").toDouble() > 0.95);
    const auto sections = coolant.value("sections").toList();
    QVERIFY(sections.size() >= 3);
    double previousMean = 0.0;
    for (const auto &value : sections) {
        const auto section = value.toMap();
        QVERIFY(!section.value("type").toString().isEmpty());
        if (!section.value("valid").toBool()) continue;
        QVERIFY(section.value("coverage").toDouble() > 0.9);
        QVERIFY(section.value("mean").toDouble() > previousMean); // the temperature only rises
        previousMean = section.value("mean").toDouble();
    }
    // KAN-68: a bounded trend for the day view, in recording order, and no
    // cooling found in a temperature that only rises.
    const auto trace = coolant.value("trace").toList();
    QCOMPARE(trace.size(), 120);
    double previousTime = -1.0, previousValue = 0.0;
    int drawn = 0;
    for (const auto &value : trace) {
        if (!value.isValid()) continue;
        const auto point = value.toList();
        QVERIFY(point.at(0).toDouble() > previousTime && point.at(1).toDouble() >= previousValue);
        previousTime = point.at(0).toDouble(); previousValue = point.at(1).toDouble();
        ++drawn;
    }
    QVERIFY(drawn > 100);
    QVERIFY(coolant.value("cooling").toList().isEmpty());
}

void TelemetryTests::showsRecordedTemperaturesThroughTheDayInQml()
{
    // KAN-68: the Car & driver view requests the summaries itself, draws one
    // trend per recorded channel and lists every session, including the one
    // whose recording has no temperature sensor ("Not recorded").
    QTemporaryDir directory; QVERIFY(directory.isValid());
    QSettings settings; settings.clear(); settings.sync();
    const auto first = directory.filePath("hot.vbo"), second = directory.filePath("plain.vbo");
    QVERIFY(writeBytes(first, routeVboWithCoolant()));
    QVERIFY(writeBytes(second, EventProjectFixture::routeVbo()));
    AppController controller(nullptr, directory.filePath("recovery.json"));
    QVERIFY(controller.importAnalysisRuns("Car and driver", {QUrl::fromLocalFile(first), QUrl::fromLocalFile(second)}));
    QTRY_COMPARE(controller.vboLoadState(), QString("ready"));
    QTRY_VERIFY(!controller.outingLapsLoading());
    QQmlEngine engine; engine.rootContext()->setContextProperty("appController", &controller);
    QSignalSpy warnings(&engine, &QQmlEngine::warnings);
    QQmlComponent component(&engine);
    component.setData("import QtQuick\nWindow { width: 900; height: 600; visible: true; CarDriverView { anchors.fill: parent } }",
        QUrl::fromLocalFile(QStringLiteral(ANALYSIS_PANEL_QML_PATH)));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    std::unique_ptr<QObject> object(component.create()); QVERIFY2(object, qPrintable(component.errorString()));
    auto *window = qobject_cast<QQuickWindow *>(object.get()); QVERIFY(window);
    QVERIFY(QTest::qWaitForWindowExposed(window));
    QTRY_COMPARE_WITH_TIMEOUT(controller.outingChannelSummaries().value("state").toString(), QString("ready"), 30000);
    const auto findVisual = [](auto &&self, QQuickItem *item, const QString &name) -> QQuickItem * {
        if (item->objectName() == name) return item;
        for (auto *child : item->childItems()) if (auto *found = self(self, child, name)) return found;
        return nullptr;
    };
    QQuickItem *trend = nullptr;
    QTRY_VERIFY((trend = findVisual(findVisual, window->contentItem(), "carDriverTrend0")));
    QTRY_VERIFY(trend->width() > 400); // laid out after the first polish on slower machines
    QVERIFY(!findVisual(findVisual, window->contentItem(), "carDriverTrend1")); // one recorded channel, none invented
    int hotIndex = -1;
    const auto runs = controller.outingChannelSummaries().value("runs").toList();
    for (int index = 0; index < runs.size(); ++index)
        if (!runs[index].toMap().value("channels").toList().isEmpty()) hotIndex = index;
    QVERIFY(hotIndex >= 0);
    const auto cell = [&](const int run, const int column) {
        auto *label = findVisual(findVisual, window->contentItem(), QString("carDriverCell0-%1-%2").arg(run).arg(column));
        return label ? label->property("text").toString() : QString("<missing>");
    };
    QTRY_VERIFY(cell(hotIndex, 0).contains("Session"));
    const auto hot = runs[hotIndex].toMap().value("channels").toList().first().toMap().value("run").toMap();
    QCOMPARE(cell(hotIndex, 2), QString("%1 – %2").arg(std::round(hot.value("minimum").toDouble())).arg(std::round(hot.value("maximum").toDouble()))); // undeclared unit: no °C invented
    QCOMPARE(cell(hotIndex, 4), QString("none recorded"));
    QCOMPARE(cell(1 - hotIndex, 1), QString("Not recorded"));
    QCOMPARE(warnings.size(), 0);
}

void TelemetryTests::summarizesHeartRatePerRunSectionAndInterval()
{
    // KAN-69: heart rate from the recordings' own channel, per run, per
    // section and for a selected interval of the comparison pair. The 255 bpm
    // artifact is excluded and counted; a recording without heart rate has
    // no summary rather than an invented one.
    QTemporaryDir directory; QVERIFY(directory.isValid());
    QSettings settings; settings.clear(); settings.sync();
    const auto first = directory.filePath("calm.vbo"), second = directory.filePath("busy.vbo"), third = directory.filePath("none.vbo");
    QVERIFY(writeBytes(first, routeVboWithHeartRate(140)));
    QVERIFY(writeBytes(second, routeVboWithHeartRate(150)));
    QVERIFY(writeBytes(third, EventProjectFixture::routeVbo()));
    AppController controller(nullptr, directory.filePath("recovery.json"));
    QVERIFY(controller.importAnalysisRuns("Heart rate", {QUrl::fromLocalFile(first), QUrl::fromLocalFile(second), QUrl::fromLocalFile(third)}));
    QTRY_COMPARE(controller.vboLoadState(), QString("ready"));
    QTRY_VERIFY(!controller.outingLapsLoading());
    controller.requestOutingChannelSummaries();
    QTRY_COMPARE_WITH_TIMEOUT(controller.outingChannelSummaries().value("state").toString(), QString("ready"), 30000);
    int withHeartRate = 0;
    for (const auto &value : controller.outingChannelSummaries().value("runs").toList()) {
        const auto run = value.toMap();
        if (!run.contains("heartRate")) continue;
        ++withHeartRate;
        const auto heartRate = run.value("heartRate").toMap();
        QCOMPARE(heartRate.value("channel").toString(), QString("heart_rate"));
        const auto whole = heartRate.value("run").toMap();
        QVERIFY(whole.value("valid").toBool());
        QCOMPARE(whole.value("excludedArtifacts").toInt(), 1);
        QVERIFY(whole.value("maximum").toDouble() < 200.0);
        const double level = run.value("runName") == "Session 1" ? 140.0 : 150.0;
        QVERIFY2(std::abs(whole.value("mean").toDouble() - level) < 0.5, qPrintable(whole.value("mean").toString()));
        QVERIFY(whole.value("coverage").toDouble() > 0.95);
        QVERIFY(heartRate.value("sections").toList().size() >= 3);
    }
    QCOMPARE(withHeartRate, 2);

    // A selected interval of the comparison pair.
    QVariantMap a, b;
    for (const auto &value : controller.comparisonLaps()) {
        const auto row = value.toMap();
        if (a.isEmpty() && row.value("runName") == "Session 1") a = row;
        if (b.isEmpty() && row.value("runName") == "Session 2") b = row;
    }
    QVERIFY(controller.selectComparisonLap(0, a.value("reference").toMap()));
    QVERIFY(controller.selectComparisonLap(1, b.value("reference").toMap()));
    QTRY_VERIFY(controller.comparisonPairReady());
    const double length = controller.comparisonProgressAxisLength();
    const auto pair = controller.comparisonHeartRate(0.0, length / 2).value("laps").toList();
    QCOMPARE(pair.size(), 2);
    QVERIFY(std::abs(pair[0].toMap().value("mean").toDouble() - 140.0) < 2.5);
    QVERIFY(std::abs(pair[1].toMap().value("mean").toDouble() - 150.0) < 2.5);
    QVERIFY(pair[0].toMap().value("coverage").toDouble() > 0.9);
    // KAN-70: a range across start/finish combines the lap's end and start.
    const auto wrapped = controller.comparisonHeartRate(length * 0.75, length * 0.25);
    QVERIFY(wrapped.value("crossesStartFinish").toBool());
    const auto wrappedLaps = wrapped.value("laps").toList();
    QVERIFY(wrappedLaps[0].toMap().value("valid").toBool());
    QVERIFY(std::abs(wrappedLaps[0].toMap().value("mean").toDouble() - 140.0) < 2.5);
    QVERIFY(std::abs(wrappedLaps[1].toMap().value("mean").toDouble() - 150.0) < 2.5);
    QVERIFY(wrappedLaps[0].toMap().value("coverage").toDouble() > 0.9);
}

void TelemetryTests::showsHeartRateByRunAndSegmentInQml()
{
    // KAN-70: the Car & driver view lists each run's recorded heart rate with
    // samples and coverage ("Not recorded" for the run without it), and a lap
    // opens with the heart-rate channel first without overwriting the saved
    // chart preference. The Corner Analyzer shows A/B heart rate for the
    // selected segment and can put the channel into the comparison charts.
    QTemporaryDir directory; QVERIFY(directory.isValid());
    QSettings settings; settings.clear(); settings.sync();
    const auto first = directory.filePath("calm.vbo"), second = directory.filePath("busy.vbo"), third = directory.filePath("none.vbo");
    QVERIFY(writeBytes(first, routeVboWithHeartRate(140)));
    QVERIFY(writeBytes(second, routeVboWithHeartRate(150)));
    QVERIFY(writeBytes(third, EventProjectFixture::routeVbo()));
    AppController controller(nullptr, directory.filePath("recovery.json"));
    QVERIFY(controller.importAnalysisRuns("Heart rate QML", {QUrl::fromLocalFile(first), QUrl::fromLocalFile(second), QUrl::fromLocalFile(third)}));
    QTRY_COMPARE(controller.vboLoadState(), QString("ready"));
    QTRY_VERIFY(!controller.outingLapsLoading());
    const auto findVisual = [](auto &&self, QQuickItem *item, const QString &name) -> QQuickItem * {
        if (item->objectName() == name) return item;
        for (auto *child : item->childItems()) if (auto *found = self(self, child, name)) return found;
        return nullptr;
    };
    {
        QQmlEngine engine; engine.rootContext()->setContextProperty("appController", &controller);
        QSignalSpy warnings(&engine, &QQmlEngine::warnings);
        QQmlComponent component(&engine);
        component.setData("import QtQuick\nWindow { width: 900; height: 700; visible: true; CarDriverView { objectName: \"view\"; anchors.fill: parent } }",
            QUrl::fromLocalFile(QStringLiteral(ANALYSIS_PANEL_QML_PATH)));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        std::unique_ptr<QObject> object(component.create()); QVERIFY2(object, qPrintable(component.errorString()));
        auto *window = qobject_cast<QQuickWindow *>(object.get()); QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QTRY_COMPARE_WITH_TIMEOUT(controller.outingChannelSummaries().value("state").toString(), QString("ready"), 30000);
        auto *view = findVisual(findVisual, window->contentItem(), "view"); QVERIFY(view);
        QSignalSpy opened(view, SIGNAL(lapOpened()));
        QQuickItem *card = nullptr;
        QTRY_VERIFY((card = findVisual(findVisual, window->contentItem(), "carDriverHeartRate")) && card->isVisible());
        QStringList texts;
        for (int run = 0; run < 3; ++run) {
            QQuickItem *label = nullptr;
            QTRY_VERIFY((label = findVisual(findVisual, card, QString("carDriverHeartRateRun%1").arg(run))));
            texts << label->property("text").toString();
        }
        QCOMPARE(texts.filter("Not recorded").size(), 1);
        QCOMPARE(texts.filter("mean 140 · ").size(), 1);
        QCOMPARE(texts.filter("mean 150 · ").size(), 1);
        QVERIFY(texts.filter("samples · ").size() == 2 && texts.filter("% covered").size() == 2);
        QCOMPARE(texts.filter("1 implausible excluded").size(), 2);
        for (const auto &text : texts) QVERIFY(!text.contains("stress", Qt::CaseInsensitive));
        // Open the calm run's first timed lap on its heart-rate channel.
        const int calm = texts.indexOf(QRegularExpression(".*mean 140 · .*"));
        QQuickItem *chip = nullptr;
        for (int index = 0; index < 8 && !chip; ++index) {
            auto *candidate = findVisual(findVisual, card, QString("carDriverHeartRateLap%1-%2").arg(calm).arg(index));
            if (candidate && candidate->property("text").toString().startsWith("LAP ")) chip = candidate;
        }
        QVERIFY(chip); QVERIFY(chip->isEnabled());
        chip->forceActiveFocus(); QTest::keyClick(window, Qt::Key_Space);
        QTRY_COMPARE(opened.size(), 1);
        QTRY_COMPARE(controller.outingLapDetailState(), QString("ready"));
        QCOMPARE(controller.outingLapChannels().value(0), QString("heart_rate"));
        QVERIFY(controller.outingLapChannels().size() <= 4);
        QVERIFY(!QSettings().contains("analysis/lapChannels")); // the saved preference is not rewritten
        QCOMPARE(warnings.size(), 0);
        controller.closeOutingLap();
        // A lap opened normally afterwards does not inherit the heart-rate request.
        const auto firstSection = controller.outingChannelSummaries().value("runs").toList()[calm].toMap()
            .value("heartRate").toMap().value("sections").toList().first().toMap();
        QVERIFY(controller.selectOutingLapReference(firstSection.value("reference").toMap()));
        QTRY_COMPARE(controller.outingLapDetailState(), QString("ready"));
        QVERIFY(controller.outingLapChannels().value(0) != "heart_rate");
        controller.closeOutingLap();
    }

    // The Corner Analyzer: A/B heart rate over the selected segment.
    // Opened as evidence from the theoretical best, on its canonical segments.
    const auto approved = approveAllSegmentsOnRun(controller, "Session 1");
    QVERIFY(!approved.isEmpty());
    QString segmentId;
    for (const auto &value : approved) {
        const auto segment = value.toObject();
        if (segment.value("endProgressMeters").toDouble() > segment.value("startProgressMeters").toDouble() + 50) {
            segmentId = segment.value("id").toString(); break;
        }
    }
    QVERIFY(!segmentId.isEmpty());
    controller.requestOutingTheoreticalBest();
    QTRY_COMPARE_WITH_TIMEOUT(controller.outingTheoreticalBest().value("state").toString(), QString("ready"), 30000);
    QVariantMap a, b;
    for (const auto &value : controller.comparisonLaps()) {
        const auto row = value.toMap();
        if (a.isEmpty() && row.value("runName") == "Session 1") a = row;
        if (b.isEmpty() && row.value("runName") == "Session 2") b = row;
    }
    QVERIFY(controller.m_analysis.openComparisonEvidence(a.value("reference").toMap(), b.value("reference").toMap(), segmentId));
    QTRY_VERIFY(controller.comparisonPairReady());
    QVERIFY(!controller.comparisonApprovedSegments().isEmpty());
    QQmlEngine engine; engine.rootContext()->setContextProperty("appController", &controller);
    QSignalSpy warnings(&engine, &QQmlEngine::warnings);
    QQmlComponent component(&engine);
    component.setData("import QtQuick\nWindow { width: 1300; height: 800; visible: true; "
        "ComparisonDetailPanel { objectName: \"comparisonRoot\"; anchors.fill: parent } }",
        QUrl::fromLocalFile(QStringLiteral(ANALYSIS_PANEL_QML_PATH)));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    std::unique_ptr<QObject> object(component.create()); QVERIFY2(object, qPrintable(component.errorString()));
    auto *window = qobject_cast<QQuickWindow *>(object.get()); QVERIFY(window);
    QVERIFY(QTest::qWaitForWindowExposed(window));
    auto *toggle = window->findChild<QQuickItem *>("comparisonToggleCornerAnalyzer"); QVERIFY(toggle);
    toggle->forceActiveFocus(); QTest::keyClick(window, Qt::Key_Space);
    auto *heartA = window->findChild<QObject *>("cornerAnalyzerHeartRateA"); QVERIFY(heartA);
    auto *heartB = window->findChild<QObject *>("cornerAnalyzerHeartRateB"); QVERIFY(heartB);
    QTRY_VERIFY(heartA->property("text").toString().endsWith(" bpm"));
    const auto bpm = [](QObject *label) { return label->property("text").toString().section(' ', 0, 0).toDouble(); };
    QVERIFY2(std::abs(bpm(heartA) - 140) <= 3, qPrintable(heartA->property("text").toString()));
    QVERIFY(std::abs(bpm(heartB) - 150) <= 3);
    auto *note = window->findChild<QObject *>("cornerAnalyzerHeartRateNote"); QVERIFY(note);
    QVERIFY(note->property("text").toString().contains("samples"));
    auto *comparisonRoot = window->findChild<QObject *>("comparisonRoot"); QVERIFY(comparisonRoot);
    QTRY_VERIFY(!comparisonRoot->property("visibleChannels").toStringList().isEmpty());
    QVERIFY(!comparisonRoot->property("visibleChannels").toStringList().contains("heart_rate"));
    auto *show = window->findChild<QQuickItem *>("cornerAnalyzerHeartRateShow"); QVERIFY(show);
    show->forceActiveFocus(); QTest::keyClick(window, Qt::Key_Space);
    QTRY_VERIFY(comparisonRoot->property("visibleChannels").toStringList().contains("heart_rate"));
    QVERIFY(comparisonRoot->property("visibleChannels").toStringList().size() <= 4);
    QCOMPARE(warnings.size(), 0);
}

void TelemetryTests::buildsDayReportWithProvenance()
{
    // KAN-71: the day report presents computed results with algorithm, range,
    // status and evidence; results not yet computed say so; a changed
    // analysis decision (a lap exclusion) never leaves an old value showing.
    QTemporaryDir directory; QVERIFY(directory.isValid());
    QSettings settings; settings.clear(); settings.sync();
    const auto slow = directory.filePath("slow.vbo"), fast = directory.filePath("fast.vbo");
    QVERIFY(writeBytes(slow, routeVboWithHeartRate(140)));
    QVERIFY(writeBytes(fast, scaledRouteVbo(0.9)));
    AppController controller(nullptr, directory.filePath("recovery.json"));
    QVERIFY(controller.importAnalysisRuns("Day report", {QUrl::fromLocalFile(slow), QUrl::fromLocalFile(fast)}));
    QTRY_COMPARE(controller.vboLoadState(), QString("ready"));
    QTRY_VERIFY(!controller.outingLapsLoading());
    QTRY_VERIFY(!controller.outingComparisonGroupId().isEmpty());
    const auto resultOf = [&](const QString &id) {
        for (const auto &value : controller.outingDayReport().value("results").toList())
            if (value.toMap().value("id") == id) return value.toMap();
        return QVariantMap{};
    };
    QSignalSpy changed(&controller, &AppController::outingDayReportChanged);

    // Before any background result: ranking-based results are available,
    // the rest are "not computed" and carry no value.
    auto report = controller.outingDayReport();
    QCOMPARE(report.value("schema").toString(), QString(dayReportSchema));
    QCOMPARE(report.value("groupId").toString(), controller.outingComparisonGroupId());
    QCOMPARE(validateDayReport(QJsonObject::fromVariantMap(report)), QString());
    const auto best = resultOf("bestLap");
    QCOMPARE(best.value("status").toString(), QString("available"));
    QCOMPARE(best.value("algorithm").toString(), QString(lapRankingAlgorithm));
    const auto bestEvidence = best.value("evidence").toList();
    QCOMPARE(bestEvidence.size(), 1);
    QCOMPARE(bestEvidence.first().toMap().value("kind").toString(), QString("lap"));
    QCOMPARE(controller.resolveOutingLapReference(bestEvidence.first().toMap().value("reference").toMap()).value("state").toString(),
        QString("resolved"));
    QVERIFY(best.value("range").toMap().value("eligibleLapCount").toInt() >= 2);
    const auto consistency = resultOf("consistency");
    QCOMPARE(consistency.value("status").toString(), QString("available"));
    QCOMPARE(consistency.value("evidence").toList().size(), best.value("range").toMap().value("eligibleLapCount").toInt());
    QCOMPARE(resultOf("progression").value("status").toString(), QString("available"));
    for (const auto *id : {"theoreticalBest", "timeLosses", "sectionProgression", "temperatures", "heartRate"}) {
        QCOMPARE(resultOf(id).value("status").toString(), QString("notComputed"));
        QVERIFY(!resultOf(id).contains("value"));
    }

    // Computed: every result is available (or says why not) with evidence.
    QString slowName;
    for (const auto &value : controller.outingLaps()) {
        const auto row = value.toMap();
        if (row.value("type") == "LAP" && row.value("endTime").toDouble() - row.value("startTime").toDouble() > 45.0)
            slowName = row.value("runName").toString();
    }
    QVERIFY(!approveAllSegmentsOnRun(controller, slowName).isEmpty());
    controller.requestOutingDayReport();
    QTRY_COMPARE_WITH_TIMEOUT(resultOf("theoreticalBest").value("status").toString(), QString("available"), 30000);
    QTRY_COMPARE_WITH_TIMEOUT(resultOf("heartRate").value("status").toString(), QString("available"), 30000);
    QVERIFY(changed.size() > 0);
    report = controller.outingDayReport();
    QCOMPARE(validateDayReport(QJsonObject::fromVariantMap(report)), QString());
    const auto theoretical = resultOf("theoreticalBest");
    QCOMPARE(theoretical.value("algorithm").toString(), QString(theoreticalBestAlgorithm));
    QVERIFY(!theoretical.value("value").toMap().value("sectors").toList().isEmpty());
    bool segmentEvidence = false;
    for (const auto &value : theoretical.value("evidence").toList())
        segmentEvidence = segmentEvidence || (value.toMap().value("kind") == "segment" && !value.toMap().value("segmentId").toString().isEmpty());
    QVERIFY(segmentEvidence);
    const auto losses = resultOf("timeLosses");
    QVERIFY(losses.value("status") == "available" || losses.value("status") == "unavailable");
    if (losses.value("status") == "available") {
        QVERIFY(!losses.value("revision").toString().isEmpty());
        QCOMPARE(losses.value("evidence").toList().size(), losses.value("value").toMap().value("losses").toList().size());
    }
    QCOMPARE(resultOf("sectionProgression").value("status").toString(), QString("available"));
    const auto heart = resultOf("heartRate");
    QCOMPARE(heart.value("evidence").toList().size(), 1); // one run recorded heart rate
    QCOMPARE(heart.value("evidence").toList().first().toMap().value("channel").toString(), QString("heart_rate"));
    const auto temperatures = resultOf("temperatures");
    QCOMPARE(temperatures.value("status").toString(), QString("unavailable")); // none recorded, none invented
    QCOMPARE(temperatures.value("reason").toString(), QString("No temperature recorded."));

    // A changed decision: excluding the best lap changes the decisions key,
    // and the theoretical best no longer shows its old value.
    const auto decisions = report.value("decisionsKey").toString();
    QVERIFY(controller.setOutingLapExcluded(bestEvidence.first().toMap().value("reference").toMap(), true, "Traffic"));
    QTRY_VERIFY(controller.outingDayReport().value("decisionsKey").toString() != decisions);
    QVERIFY(resultOf("theoreticalBest").value("status") != "available");
    QVERIFY(!resultOf("theoreticalBest").contains("value"));
    QVERIFY(resultOf("bestLap").value("value").toMap().value("label") != best.value("value").toMap().value("label"));
}

void TelemetryTests::presentsDayReportWithEvidenceNavigation()
{
    // KAN-72: the day report opens from Day results, presents the computed
    // report, says exactly what is missing (no temperature sensor: no zero),
    // and each result leads to its evidence: a loss to the comparison (and
    // back to the report), a session to its best lap.
    QTemporaryDir directory; QVERIFY(directory.isValid());
    QSettings settings; settings.clear(); settings.sync();
    const auto slow = directory.filePath("slow.vbo"), fast = directory.filePath("fast.vbo");
    QVERIFY(writeBytes(slow, routeVboWithHeartRate(140)));
    QVERIFY(writeBytes(fast, scaledRouteVbo(0.9)));
    AppController controller(nullptr, directory.filePath("recovery.json"));
    QVERIFY(controller.importAnalysisRuns("Day report QML", {QUrl::fromLocalFile(slow), QUrl::fromLocalFile(fast)}));
    QTRY_COMPARE(controller.vboLoadState(), QString("ready"));
    QTRY_VERIFY(!controller.outingLapsLoading());
    QTRY_VERIFY(!controller.outingComparisonGroupId().isEmpty());
    QString slowName;
    for (const auto &value : controller.outingLaps()) {
        const auto row = value.toMap();
        if (row.value("type") == "LAP" && row.value("endTime").toDouble() - row.value("startTime").toDouble() > 45.0)
            slowName = row.value("runName").toString();
    }
    QVERIFY(!approveAllSegmentsOnRun(controller, slowName).isEmpty());

    QQmlEngine engine; engine.rootContext()->setContextProperty("appController", &controller);
    QSignalSpy warnings(&engine, &QQmlEngine::warnings); QQmlComponent component(&engine);
    component.setData("import QtQuick\nWindow { width: 1180; height: 720; OutingLapPanel { anchors.fill: parent } }",
        QUrl::fromLocalFile(QStringLiteral(ANALYSIS_PANEL_QML_PATH)));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    std::unique_ptr<QObject> object(component.create()); QVERIFY2(object, qPrintable(component.errorString()));
    auto *window = qobject_cast<QQuickWindow *>(object.get()); QVERIFY(window);
    window->show(); QVERIFY(QTest::qWaitForWindowExposed(window));
    const auto findVisual = [](auto &&self, QQuickItem *item, const QString &name) -> QQuickItem * {
        if (item->objectName() == name) return item;
        for (auto *child : item->childItems()) if (auto *found = self(self, child, name)) return found;
        return nullptr;
    };
    const auto find = [&](const QString &name) { return findVisual(findVisual, window->contentItem(), name); };
    auto *open = window->findChild<QQuickItem *>("openDayReport"); QVERIFY(open);
    QTRY_VERIFY(open->isEnabled());
    open->forceActiveFocus(); QTest::keyClick(window, Qt::Key_Space);
    auto *dialog = window->findChild<QObject *>("dayReportDialog"); QVERIFY(dialog);
    QTRY_VERIFY(dialog->property("opened").toBool());

    // Opening requests the background results; the report fills in.
    const auto best = controller.outingRanking().value("bestOfDay").toMap();
    QQuickItem *bestTime = nullptr;
    QTRY_VERIFY((bestTime = find("dayReportBestTime")));
    QTRY_COMPARE(bestTime->property("text").toString(), AppController::formatElapsedTime(best.value("durationSeconds").toDouble()));
    QTRY_VERIFY_WITH_TIMEOUT(find("dayReportTheoreticalTime") && find("dayReportTheoreticalTime")->property("text").toString() != "—", 30000);
    QQuickItem *carMissing = nullptr;
    QTRY_VERIFY_WITH_TIMEOUT((carMissing = find("dayReportCarMissing")) && carMissing->isVisible()
        && carMissing->property("text").toString() == "No temperature recorded.", 30000);
    QStringList heart;
    QTRY_VERIFY(find("dayReportHeartRate1"));
    for (int index = 0; index < 2; ++index) heart << find(QString("dayReportHeartRate%1").arg(index))->property("primary").toString();
    QCOMPARE(heart.filter("Not recorded").size(), 1);
    QCOMPARE(heart.filter("mean 140 bpm").size(), 1);
    QVERIFY(find("dayReportConsistencyDay")->property("text").toString().contains("laps"));

    // A loss opens the comparison at that corner; closing it returns here.
    const auto losses = controller.outingDayReport().value("results").toList();
    bool lossesAvailable = false;
    for (const auto &value : losses)
        if (value.toMap().value("id") == "timeLosses") lossesAvailable = value.toMap().value("status") == "available";
    QVERIFY(lossesAvailable);
    {
        QQuickItem *loss = nullptr;
        QTRY_VERIFY((loss = find("dayReportLoss0")));
        loss->forceActiveFocus(); QTest::keyClick(window, Qt::Key_Space);
        QTRY_VERIFY(controller.comparisonViewOpen());
        QTRY_VERIFY(!dialog->property("visible").toBool());
        QVERIFY(!controller.comparisonFocusSegmentId().isEmpty() || controller.comparisonPairReady());
        controller.setComparisonViewOpen(false);
        QTRY_VERIFY(dialog->property("opened").toBool());
    }

    // A session opens its best lap.
    QQuickItem *session = nullptr;
    int sessionIndex = -1;
    for (int index = 0; index < 2 && !session; ++index) {
        auto *candidate = find(QString("dayReportSession%1").arg(index));
        if (candidate && candidate->isEnabled()) { session = candidate; sessionIndex = index; }
    }
    QVERIFY(session);
    const auto progressionRuns = controller.outingDayReport().value("results").toList();
    QVariantMap expected;
    for (const auto &value : progressionRuns) {
        const auto result = value.toMap();
        if (result.value("id") != "progression") continue;
        const auto row = result.value("value").toMap().value("runs").toList()[sessionIndex].toMap();
        expected = result.value("evidence").toList()[row.value("evidenceIndex").toInt()].toMap().value("reference").toMap();
    }
    QVERIFY(!expected.isEmpty());
    session->forceActiveFocus(); QTest::keyClick(window, Qt::Key_Space);
    QTRY_VERIFY(!dialog->property("visible").toBool());
    QTRY_COMPARE(controller.outingLapDetailState(), QString("ready"));
    QCOMPARE(controller.selectedOutingLap().value("reference").toMap(), expected);
    controller.closeOutingLap();
    for (const auto &warning : warnings) qInfo() << warning;
    QCOMPARE(warnings.size(), 0);
}

void TelemetryTests::selectsFocusAreasFromComputedObservations()
{
    // KAN-73: two sessions with equal lap times, each quicker in a different
    // half. The best lap is slower than the fastest recorded time in its slow
    // half, so the report names that as an area to inspect, with the
    // observation apart from the hypothesis and the pair of laps to compare.
    QTemporaryDir directory; QVERIFY(directory.isValid());
    QSettings settings; settings.clear(); settings.sync();
    const auto first = directory.filePath("first.vbo"), second = directory.filePath("second.vbo");
    QVERIFY(writeBytes(first, warpedRouteVbo(true)));
    QVERIFY(writeBytes(second, warpedRouteVbo(false)));
    AppController controller(nullptr, directory.filePath("recovery.json"));
    QVERIFY(controller.importAnalysisRuns("Focus", {QUrl::fromLocalFile(first), QUrl::fromLocalFile(second)}));
    QTRY_COMPARE(controller.vboLoadState(), QString("ready"));
    QTRY_VERIFY(!controller.outingLapsLoading());
    QTRY_VERIFY(!controller.outingComparisonGroupId().isEmpty());
    QVERIFY(!approveAllSegmentsOnRun(controller, "Session 1").isEmpty());
    const auto resultOf = [&](const QString &id) {
        for (const auto &value : controller.outingDayReport().value("results").toList())
            if (value.toMap().value("id") == id) return value.toMap();
        return QVariantMap{};
    };
    QCOMPARE(resultOf("focusAreas").value("status").toString(), QString("notComputed"));
    controller.requestOutingDayReport();
    QTRY_VERIFY_WITH_TIMEOUT(resultOf("focusAreas").value("status") != "notComputed"
        && resultOf("focusAreas").value("status") != "computing", 30000);
    const auto focus = resultOf("focusAreas");
    QCOMPARE(focus.value("status").toString(), QString("available"));
    QCOMPARE(focus.value("algorithm").toString(), QString(focusAreasAlgorithm));
    const auto areas = focus.value("value").toMap().value("areas").toList();
    QVERIFY(!areas.isEmpty() && areas.size() <= 3);
    const auto evidence = focus.value("evidence").toList();
    QSet<QString> segments;
    for (const auto &value : areas) {
        const auto area = value.toMap();
        QVERIFY(!segments.contains(area.value("segmentId").toString())); // one area per segment
        segments.insert(area.value("segmentId").toString());
        QVERIFY(!area.value("observation").toString().isEmpty());
        QVERIFY(!area.value("hypothesis").toString().isEmpty());
        QVERIFY(area.value("observation") != area.value("hypothesis"));
        QVERIFY(!area.value("hypothesis").toString().contains("brake later", Qt::CaseInsensitive));
        const auto item = evidence[area.value("evidenceIndex").toInt()].toMap();
        QCOMPARE(item.value("segmentId").toString(), area.value("segmentId").toString());
        QVERIFY(item.value("reference") != item.value("against"));
        QCOMPARE(controller.resolveOutingLapReference(item.value("reference").toMap()).value("state").toString(), QString("resolved"));
        QCOMPARE(controller.resolveOutingLapReference(item.value("against").toMap()).value("state").toString(), QString("resolved"));
    }
    const auto top = areas.first().toMap();
    QCOMPARE(top.value("kind").toString(), QString("sectorGap"));
    QVERIFY2(top.value("value").toDouble() > 0.5, qPrintable(top.value("observation").toString()));
    // The evidence opens the comparison at that segment, best lap as A.
    const auto item = evidence[top.value("evidenceIndex").toInt()].toMap();
    QVERIFY(controller.openFocusArea(item));
    QTRY_VERIFY(controller.comparisonPairReady());
    QCOMPARE(controller.comparisonFocusSegmentId(), top.value("segmentId").toString());
    QCOMPARE(controller.comparisonSlots().toList()[0].toMap().value("lap").toMap().value("reference").toMap(),
        item.value("reference").toMap());
    QVERIFY(!controller.openFocusArea(QVariantMap{{"kind", "lap"}}));

    // The report shows the observation and the hypothesis as such, and the
    // compare row opens the same evidence.
    controller.setComparisonViewOpen(false);
    QQmlEngine engine; engine.rootContext()->setContextProperty("appController", &controller);
    QSignalSpy warnings(&engine, &QQmlEngine::warnings); QQmlComponent component(&engine);
    component.setData("import QtQuick\nWindow { width: 1180; height: 720; OutingLapPanel { anchors.fill: parent } }",
        QUrl::fromLocalFile(QStringLiteral(ANALYSIS_PANEL_QML_PATH)));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    std::unique_ptr<QObject> object(component.create()); QVERIFY2(object, qPrintable(component.errorString()));
    auto *window = qobject_cast<QQuickWindow *>(object.get()); QVERIFY(window);
    window->show(); QVERIFY(QTest::qWaitForWindowExposed(window));
    const auto findVisual = [](auto &&self, QQuickItem *node, const QString &name) -> QQuickItem * {
        if (node->objectName() == name) return node;
        for (auto *child : node->childItems()) if (auto *found = self(self, child, name)) return found;
        return nullptr;
    };
    auto *dialog = window->findChild<QObject *>("dayReportDialog"); QVERIFY(dialog);
    QVERIFY(QMetaObject::invokeMethod(dialog, "open"));
    QQuickItem *observation = nullptr, *hypothesis = nullptr, *compare = nullptr;
    QTRY_VERIFY((observation = findVisual(findVisual, window->contentItem(), "dayReportFocusObservation0")));
    hypothesis = findVisual(findVisual, window->contentItem(), "dayReportFocusHypothesis0"); QVERIFY(hypothesis);
    QCOMPARE(observation->property("text").toString(), "Observed: " + top.value("observation").toString());
    QCOMPARE(hypothesis->property("text").toString(), "Hypothesis: " + top.value("hypothesis").toString());
    compare = findVisual(findVisual, window->contentItem(), "dayReportFocusCompare0"); QVERIFY(compare);
    compare->forceActiveFocus(); QTest::keyClick(window, Qt::Key_Space);
    QTRY_VERIFY(controller.comparisonViewOpen());
    QTRY_VERIFY(!dialog->property("visible").toBool());
    QCOMPARE(warnings.size(), 0);
}

void TelemetryTests::acceptsM4ReportLossCornerEvidenceWorkflow()
{
    // KAN-74: the M4 workflow end to end on full and limited fixtures:
    // report -> loss -> corner -> evidence, values checked against the
    // generated source, absent heart rate / temperature / G on the limited
    // run, an exclusion, a changed segmentation, and save/reopen. No video.
    QTemporaryDir directory; QVERIFY(directory.isValid());
    QSettings settings; settings.clear(); settings.sync();
    const auto fullA = directory.filePath("full-a.vbo"), fullB = directory.filePath("full-b.vbo"),
               limited = directory.filePath("limited.vbo");
    QVERIFY(writeBytes(fullA, fullM4Vbo(true, 140, 80, 1.0)));
    QVERIFY(writeBytes(fullB, fullM4Vbo(false, 150, 85, 0.9)));
    QVERIFY(writeBytes(limited, warpedRouteVbo(true)));
    const int rows = QString::fromUtf8(warpedRouteVbo(true)).split('\n').size(); // upper bound of data rows
    const auto projectPath = directory.filePath("m4.fetproject");
    const auto resultIn = [](const QVariantMap &report, const QString &id) {
        for (const auto &value : report.value("results").toList())
            if (value.toMap().value("id") == id) return value.toMap();
        return QVariantMap{};
    };
    const auto allSettled = [&](AppController &controller) {
        for (const auto &value : controller.outingDayReport().value("results").toList()) {
            const auto status = value.toMap().value("status").toString();
            if (status == "notComputed" || status == "computing" || status == "stale") return false;
        }
        return true;
    };
    struct Snapshot { QString bestLabel; double bestSeconds = 0; double theoreticalTotal = 0; int sectors = 0;
        QStringList focus; int consistencyLaps = 0; QString decisions; };
    const auto snapshot = [&](AppController &controller) {
        const auto report = controller.outingDayReport();
        Snapshot result;
        result.bestLabel = resultIn(report, "bestLap").value("value").toMap().value("label").toString();
        result.bestSeconds = resultIn(report, "bestLap").value("value").toMap().value("seconds").toDouble();
        const auto theoretical = resultIn(report, "theoreticalBest").value("value").toMap();
        result.theoreticalTotal = theoretical.value("totalSeconds").toDouble();
        result.sectors = theoretical.value("sectors").toList().size();
        for (const auto &area : resultIn(report, "focusAreas").value("value").toMap().value("areas").toList())
            result.focus << area.toMap().value("kind").toString() + "/" + area.toMap().value("segmentId").toString();
        result.consistencyLaps = resultIn(report, "consistency").value("value").toMap().value("day").toMap().value("count").toInt();
        result.decisions = report.value("decisionsKey").toString();
        return result;
    };
    Snapshot saved;
    QVariantMap excludedReference;
    {
        AppController controller(nullptr, directory.filePath("recovery.json"));
        QVERIFY(controller.importAnalysisRuns("M4 acceptance",
            {QUrl::fromLocalFile(fullA), QUrl::fromLocalFile(fullB), QUrl::fromLocalFile(limited)}));
        QTRY_COMPARE(controller.vboLoadState(), QString("ready"));
        QTRY_VERIFY(!controller.outingLapsLoading());
        QTRY_VERIFY(!controller.outingComparisonGroupId().isEmpty());
        const auto approved = approveAllSegmentsOnRun(controller, "Session 1");
        QVERIFY(!approved.isEmpty());
        controller.requestOutingDayReport();
        QTRY_VERIFY_WITH_TIMEOUT(allSettled(controller), 60000);
        auto report = controller.outingDayReport();
        QCOMPARE(validateDayReport(QJsonObject::fromVariantMap(report)), QString());

        // Values agree with the source. Every warped lap takes 48 s.
        const auto best = resultIn(report, "bestLap");
        QVERIFY(std::abs(best.value("value").toMap().value("seconds").toDouble() - 48.0) < 0.01);
        const auto theoretical = resultIn(report, "theoreticalBest").value("value").toMap();
        QVERIFY(theoretical.value("totalSeconds").toDouble() < 47.5 && theoretical.value("totalSeconds").toDouble() > 43.2);
        QVERIFY(std::abs(theoretical.value("differenceSeconds").toDouble()
            - (48.0 - theoretical.value("totalSeconds").toDouble())) < 0.02);
        QCOMPARE(theoretical.value("sectors").toList().size(), approved.size());
        const auto heart = resultIn(report, "heartRate").value("value").toMap().value("runs").toList();
        const auto temperatures = resultIn(report, "temperatures").value("value").toMap().value("runs").toList();
        QCOMPARE(heart.size(), 3);
        int withHeart = 0, withTemperature = 0;
        for (const auto &value : heart) {
            const auto run = value.toMap();
            if (!run.contains("heartRate")) continue; // the limited run: absent, not zero
            ++withHeart;
            const auto mean = run.value("heartRate").toMap().value("run").toMap().value("mean").toDouble();
            QVERIFY2(std::abs(mean - 140) < 1e-3 || std::abs(mean - 150) < 1e-3, qPrintable(QString::number(mean)));
        }
        for (const auto &value : temperatures) {
            const auto channels = value.toMap().value("channels").toList();
            if (channels.isEmpty()) continue;
            ++withTemperature;
            const auto whole = channels.first().toMap().value("run").toMap();
            const double start = std::round(whole.value("minimum").toDouble());
            QVERIFY(start == 80.0 || start == 85.0);
            // Never beyond the generated source: start + 0.01 per row.
            QVERIFY(whole.value("maximum").toDouble() <= start + 0.01 * rows + 1e-6);
            QVERIFY(whole.value("maximum").toDouble() > start + 1.0);
        }
        QCOMPARE(withHeart, 2);
        QCOMPARE(withTemperature, 2);
        const auto focus = resultIn(report, "focusAreas");
        QCOMPARE(focus.value("status").toString(), QString("available"));

        // Loss -> corner -> evidence, no video: the first ranked loss opens
        // the comparison with the Corner Analyzer on that segment.
        const auto losses = resultIn(report, "timeLosses");
        QCOMPARE(losses.value("status").toString(), QString("available"));
        const auto lossEvidence = losses.value("evidence").toList().first().toMap();
        QVERIFY(controller.openTimeLoss({{"segmentId", lossEvidence.value("segmentId")}, {"lapReference", lossEvidence.value("reference")}}));
        QTRY_VERIFY(controller.comparisonPairReady());
        QCOMPARE(controller.comparisonFocusSegmentId(), lossEvidence.value("segmentId").toString());
        QVERIFY(!controller.comparisonSegmentMetrics(lossEvidence.value("segmentId").toString()).isEmpty());
        // G agrees with the generated peaks; the limited run (Session 3,
        // identical route, so it can tie with Session 1) has none.
        const auto gg = controller.comparisonGgScatter(0.0, controller.comparisonProgressAxisLength(), 500).value("laps").toList();
        const auto pairSlots = controller.comparisonSlots().toList();
        for (int slot = 0; slot < 2; ++slot) {
            const auto lap = gg[slot].toMap();
            if (pairSlots[slot].toMap().value("lap").toMap().value("runName") == "Session 3") {
                QVERIFY(!lap.value("valid").toBool());
                continue;
            }
            QVERIFY(lap.value("valid").toBool());
            const double lateral = lap.value("peaks").toMap().value("lateral").toMap().value("value").toDouble();
            QVERIFY2(std::abs(lateral - 1.0) < 0.02 || std::abs(lateral - 0.9) < 0.02, qPrintable(QString::number(lateral)));
        }
        controller.setComparisonViewOpen(false);
        // A limited lap against a full one: G and heart rate are absent for it, not zero.
        QVariantMap limitedLap, fullLap;
        for (const auto &value : controller.comparisonLaps()) {
            const auto row = value.toMap();
            if (limitedLap.isEmpty() && row.value("runName") == "Session 3") limitedLap = row;
            if (fullLap.isEmpty() && row.value("runName") == "Session 1") fullLap = row;
        }
        QVERIFY(!limitedLap.isEmpty() && !fullLap.isEmpty());
        QVERIFY(controller.selectComparisonLap(0, limitedLap.value("reference").toMap()));
        QVERIFY(controller.selectComparisonLap(1, fullLap.value("reference").toMap()));
        QTRY_VERIFY(controller.comparisonPairReady());
        const double pairLength = controller.comparisonProgressAxisLength();
        const auto limitedGg = controller.comparisonGgScatter(0.0, pairLength, 500).value("laps").toList();
        QVERIFY(!limitedGg[0].toMap().value("valid").toBool());
        QVERIFY(!limitedGg[0].toMap().value("unavailableReason").toString().isEmpty());
        QVERIFY(limitedGg[1].toMap().value("valid").toBool());
        const auto limitedHeart = controller.comparisonHeartRate(0.0, pairLength).value("laps").toList();
        QVERIFY(!limitedHeart[0].toMap().value("valid").toBool());
        QVERIFY(!limitedHeart[0].toMap().contains("mean"));
        QVERIFY(limitedHeart[1].toMap().value("valid").toBool());
        controller.setComparisonViewOpen(false);
        // The focus area's evidence opens its pair; the best lap opens without video.
        const auto area = focus.value("value").toMap().value("areas").toList().first().toMap();
        QVERIFY(controller.openFocusArea(focus.value("evidence").toList()[area.value("evidenceIndex").toInt()].toMap()));
        QTRY_VERIFY(controller.comparisonPairReady());
        controller.setComparisonViewOpen(false);
        QVERIFY(controller.selectOutingLapReference(best.value("evidence").toList().first().toMap().value("reference").toMap()));
        QTRY_COMPARE(controller.outingLapDetailState(), QString("ready"));
        controller.closeOutingLap();

        // An exclusion changes the decisions: the old values never show,
        // the best lap moves and the recomputed results leave the lap out.
        const auto before = snapshot(controller);
        excludedReference = best.value("evidence").toList().first().toMap().value("reference").toMap();
        QVERIFY(controller.setOutingLapExcluded(excludedReference, true, "Traffic"));
        QTRY_VERIFY(controller.outingDayReport().value("decisionsKey").toString() != before.decisions);
        QVERIFY(!resultIn(controller.outingDayReport(), "theoreticalBest").contains("value"));
        controller.requestOutingDayReport();
        QTRY_VERIFY_WITH_TIMEOUT(allSettled(controller), 60000);
        const auto excluded = snapshot(controller);
        QVERIFY(excluded.bestLabel != before.bestLabel);
        QCOMPARE(excluded.consistencyLaps, before.consistencyLaps - 1);
        for (const auto &value : resultIn(controller.outingDayReport(), "theoreticalBest").value("evidence").toList())
            QVERIFY(value.toMap().value("reference").toMap() != excludedReference);

        // A changed segmentation: splitting a segment invalidates, and the
        // recomputed report has one more sector.
        int lapIndex = -1;
        const auto lapRows = controller.outingLaps();
        for (int i = 0; i < lapRows.size() && lapIndex < 0; ++i)
            if (lapRows[i].toMap().value("type") == "LAP" && lapRows[i].toMap().value("runName") == "Session 1"
                && lapRows[i].toMap().value("reference").toMap() != excludedReference) lapIndex = i;
        QVERIFY(controller.selectOutingLap(lapIndex));
        QTRY_COMPARE(controller.outingLapDetailState(), QString("ready"));
        controller.requestSegmentReview();
        QTRY_COMPARE(controller.segmentReviewState(), QString("ready"));
        const auto runId = controller.selectedOutingLap().value("runId").toString();
        QJsonObject splittable;
        for (const auto &value : controller.m_analysis.storedRunTrackSegments(runId).toArray())
            if (splittable.isEmpty() && value.toObject().value("endProgressMeters").toDouble()
                    > value.toObject().value("startProgressMeters").toDouble() + 40.0) splittable = value.toObject();
        QVERIFY(!splittable.isEmpty());
        QCOMPARE(controller.splitApprovedSegment(splittable.value("id").toString(),
            (splittable.value("startProgressMeters").toDouble() + splittable.value("endProgressMeters").toDouble()) / 2.0), QString());
        controller.closeOutingLap();
        QTRY_VERIFY(!resultIn(controller.outingDayReport(), "theoreticalBest").contains("value"));
        controller.requestOutingDayReport();
        QTRY_VERIFY_WITH_TIMEOUT(allSettled(controller), 60000);
        saved = snapshot(controller);
        QCOMPARE(saved.sectors, excluded.sectors + 1);
        QVERIFY(saved.decisions != excluded.decisions);
        QVERIFY(controller.saveProject(QUrl::fromLocalFile(projectPath)));
        QVERIFY(!controller.dirty());
        // Saving to a new path re-derives the laps; unchanged recordings and
        // decisions keep every computed result instead of discarding it.
        QTest::qWait(50);
        QTRY_VERIFY(!controller.outingLapsLoading());
        QTest::qWait(50);
        QCOMPARE(controller.outingTheoreticalBest().value("state").toString(), QString("ready"));
        QCOMPARE(controller.outingChannelSummaries().value("state").toString(), QString("ready"));
        QVERIFY(allSettled(controller));
        QCOMPARE(snapshot(controller).decisions, saved.decisions);
    }
    {
        // Reopen: the same decisions give the same report.
        AppController reopened(nullptr, directory.filePath("recovery-reopen.json"));
        reopened.requestOpenProject(QUrl::fromLocalFile(projectPath));
        QTRY_COMPARE(reopened.vboLoadState(), QString("ready"));
        QTRY_VERIFY(!reopened.outingLapsLoading());
        QTRY_VERIFY(!reopened.outingComparisonGroupId().isEmpty());
        reopened.requestOutingDayReport();
        QTRY_VERIFY_WITH_TIMEOUT(allSettled(reopened), 60000);
        const auto again = snapshot(reopened);
        QCOMPARE(again.decisions, saved.decisions);
        QCOMPARE(again.bestLabel, saved.bestLabel);
        QVERIFY(std::abs(again.bestSeconds - saved.bestSeconds) < 1e-9);
        QVERIFY(std::abs(again.theoreticalTotal - saved.theoreticalTotal) < 1e-9);
        QCOMPARE(again.sectors, saved.sectors);
        QCOMPARE(again.focus, saved.focus);
        QCOMPARE(again.consistencyLaps, saved.consistencyLaps);
        // Navigation still works after reopening, without video.
        const auto evidence = resultIn(reopened.outingDayReport(), "timeLosses").value("evidence").toList().first().toMap();
        QVERIFY(reopened.openTimeLoss({{"segmentId", evidence.value("segmentId")}, {"lapReference", evidence.value("reference")}}));
        QTRY_VERIFY(reopened.comparisonPairReady());
        QCOMPARE(reopened.comparisonFocusSegmentId(), evidence.value("segmentId").toString());
    }
}

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

void TelemetryTests::keepsAutomaticSegmentsWhenTheRunChangesDuringCreation()
{
    // KAN-142: automatic segments are created in the background after an
    // import. A driver who switches the active run, or opens its video, at
    // once must still get them, rather than lose them for good.
    const QString ffmpeg = FfmpegTools::ffmpegPath();
    if (ffmpeg.isEmpty()) QSKIP("FFmpeg is unavailable for the automatic segments test.");
    for (const QString &action : {QStringLiteral("switch run"), QStringLiteral("open video")}) {
        QTemporaryDir directory; QVERIFY(directory.isValid());
        QSettings settings; settings.clear(); settings.sync();
        const auto fullA = directory.filePath("full-a.vbo"), fullB = directory.filePath("full-b.vbo");
        QVERIFY(writeBytes(fullA, withVelocity(fullM4Vbo(true, 140, 80, 1.0))));
        QVERIFY(writeBytes(fullB, withVelocity(fullM4Vbo(false, 150, 85, 0.9))));
        const QString videoPath = directory.filePath(QStringLiteral("run.mp4"));
        QProcess encoder;
        encoder.start(ffmpeg, {"-hide_banner", "-loglevel", "error", "-y", "-f", "lavfi", "-i",
                               "color=c=black:s=32x32:r=30:d=5", "-c:v", "mpeg4", "-q:v", "3", videoPath});
        QVERIFY(encoder.waitForFinished(30'000) && encoder.exitCode() == 0);
        AppController controller(nullptr, directory.filePath("recovery.json"));
        controller.setAutomaticSegments(true);
        QSignalSpy committed(&controller, &AppController::batchImportCommitted);
        QVERIFY(controller.importAnalysisRuns("At once", {QUrl::fromLocalFile(fullA), QUrl::fromLocalFile(fullB)}));
        QTRY_COMPARE_WITH_TIMEOUT(committed.size(), 1, 60000);
        QTRY_COMPARE_WITH_TIMEOUT(controller.vboLoadState(), QStringLiteral("ready"), 60000);
        if (action == "switch run") {
            const auto runs = controller.eventRuns();
            const auto other = runs[controller.activeRunId() == runs[0].toMap().value("id").toString() ? 1 : 0].toMap().value("id").toString();
            QVERIFY(controller.selectEventRun(other));
            QTRY_COMPARE_WITH_TIMEOUT(controller.activeRunId(), other, 60000);
        } else {
            controller.loadVideo(QUrl::fromLocalFile(videoPath));
            QTRY_COMPARE_WITH_TIMEOUT(controller.videoLoadState(), QStringLiteral("ready"), 60000);
        }
        QTRY_VERIFY_WITH_TIMEOUT(controller.vboLoadState() == "ready" && !controller.outingLapsLoading(), 60000);
        const auto bestRun = [&] { return controller.outingRanking().value("bestOfDay").toMap().value("runId").toString(); };
        QTRY_VERIFY_WITH_TIMEOUT(!bestRun().isEmpty(), 60000);
        const bool created = QTest::qWaitFor([&] {
            return !controller.m_analysis.storedRunTrackSegments(bestRun()).toArray().isEmpty(); }, 20000);
        qInfo().noquote() << action << (created ? "kept" : "LOST") << "·" << controller.statusText();
        QVERIFY2(created, qPrintable(action));
    }
}

void TelemetryTests::discardsDayResultsFromThePreviousDay()
{
    // KAN-151: a theoretical best or channel summary started on day A must
    // not be published for day B. Day B's project is opened from day A, and
    // day A's results complete (under test control) while B's laps still
    // load: the window in which invalidation used to wait.
    QTemporaryDir directory; QVERIFY(directory.isValid());
    QSettings settings; settings.clear(); settings.sync();
    const auto dayA = directory.filePath("day-a.vbo"), dayB = directory.filePath("day-b.vbo");
    QVERIFY(writeBytes(dayA, withVelocity(fullM4Vbo(true, 140, 80, 1.0))));
    QVERIFY(writeBytes(dayB, withVelocity(fullM4Vbo(false, 150, 85, 0.9))));
    const auto projectB = directory.filePath("day-b.fetproject"), projectA = directory.filePath("day-a.fetproject");
    {
        AppController other(nullptr, directory.filePath("recovery-b.json"));
        QSignalSpy committed(&other, &AppController::batchImportCommitted);
        QVERIFY(other.importAnalysisRuns("Day B", {QUrl::fromLocalFile(dayB)}));
        QTRY_COMPARE_WITH_TIMEOUT(committed.size(), 1, 30000);
        QTRY_VERIFY_WITH_TIMEOUT(other.vboLoadState() == "ready" && !other.outingLapsLoading(), 30000);
        QVERIFY(other.saveProject(QUrl::fromLocalFile(projectB)));
    }
    QSettings().remove("project/path"); // start on day A, not the remembered day B
    AppController controller(nullptr, directory.filePath("recovery.json"));
    controller.setAutomaticSegments(true);
    QSignalSpy committed(&controller, &AppController::batchImportCommitted);
    QVERIFY(controller.importAnalysisRuns("Day A", {QUrl::fromLocalFile(dayA)}));
    QTRY_COMPARE_WITH_TIMEOUT(committed.size(), 1, 30000);
    QTRY_VERIFY_WITH_TIMEOUT(controller.vboLoadState() == "ready" && !controller.outingLapsLoading()
        && controller.statusText().contains("created automatically"), 60000);
    QVERIFY(controller.saveProject(QUrl::fromLocalFile(projectA))); // clean, so Open does not ask

    controller.requestOutingTheoreticalBest();
    controller.requestOutingChannelSummaries();
    QCOMPARE(controller.outingTheoreticalBest().value("state").toString(), QString("loading"));
    // Day A's results, held back until day B is opening.
    auto &analysis = controller.m_analysis;
    const auto staleBest = analysis.m_theoreticalBestRequest;
    const auto staleSummaries = analysis.m_channelSummariesRequest;
    QPromise<AnalysisController::TheoreticalBestResult> best; best.start();
    QPromise<AnalysisController::ChannelSummariesResult> summaries; summaries.start();
    analysis.m_theoreticalBestWatcher.setFuture(best.future());
    analysis.m_channelSummariesWatcher.setFuture(summaries.future());

    controller.requestOpenProject(QUrl::fromLocalFile(projectB));
    QTRY_COMPARE_WITH_TIMEOUT(controller.eventName(), QString("Day B"), 30000);
    AnalysisController::TheoreticalBestResult staleBestResult;
    staleBestResult.request = staleBest;
    staleBestResult.error = QStringLiteral("day A");
    best.addResult(staleBestResult); best.finish();
    AnalysisController::ChannelSummariesResult staleSummaryResult;
    staleSummaryResult.request = staleSummaries;
    staleSummaryResult.runs = {QVariantMap{{"runName", "Day A run"}}};
    summaries.addResult(staleSummaryResult); summaries.finish();
    // While B's laps load, and once they have loaded, nothing of day A shows.
    const auto showsDayA = [&] {
        const auto runs = controller.outingChannelSummaries().value("runs").toList();
        return controller.outingTheoreticalBest().value("message").toString() == "day A"
            || (!runs.isEmpty() && runs.first().toMap().value("runName") == "Day A run");
    };
    for (int step = 0; step < 40; ++step) {
        QTest::qWait(25);
        QVERIFY2(!showsDayA(), qPrintable(QString("day A's results shown for day B (laps loading: %1)").arg(controller.outingLapsLoading())));
    }
    QTRY_VERIFY_WITH_TIMEOUT(controller.vboLoadState() == "ready" && !controller.outingLapsLoading(), 30000);
    QVERIFY(!showsDayA());
}

void TelemetryTests::createsSegmentsAutomaticallyFromTheBestLap()
{
    // KAN-136: with automatic segments on (the Overlays app turns it on), a
    // layout without approved segments gets the day's best lap's proposals
    // approved without any review, so the theoretical best works straight
    // after import; the segments stay editable. Set FLAPPEDEAR_REAL_DAY to a
    // folder of private recordings to run the same on a real day as well.
    QStringList days{QString()};
    if (const auto real = qEnvironmentVariable("FLAPPEDEAR_REAL_DAY"); !real.isEmpty()) days.append(real);
    for (const auto &day : days) {
        QTemporaryDir directory; QVERIFY(directory.isValid());
        QSettings settings; settings.clear(); settings.sync();
        QList<QUrl> recordings;
        if (day.isEmpty()) {
            const auto fullA = directory.filePath("full-a.vbo"), fullB = directory.filePath("full-b.vbo");
            QVERIFY(writeBytes(fullA, withVelocity(fullM4Vbo(true, 140, 80, 1.0))));
            QVERIFY(writeBytes(fullB, withVelocity(fullM4Vbo(false, 150, 85, 0.9))));
            recordings = {QUrl::fromLocalFile(fullA), QUrl::fromLocalFile(fullB)};
        } else {
            for (const auto &info : QDir(day).entryInfoList({"*.vbo", "*.VBO", "*.rcz", "*.RCZ"}, QDir::Files, QDir::Name))
                recordings.append(QUrl::fromLocalFile(info.absoluteFilePath()));
            QVERIFY(!recordings.isEmpty());
        }
        AppController controller(nullptr, directory.filePath("recovery.json"));
        controller.setAutomaticSegments(true);
        QVERIFY(controller.importAnalysisRuns("Automatic", recordings));
        QTRY_VERIFY_WITH_TIMEOUT(controller.eventRuns().size() == recordings.size() && !controller.outingLapsLoading(), 180000);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.outingComparisonGroupId().isEmpty(), 60000);
        // Every timed lap of the day belongs to an identified layout.
        const auto allResolved = [&] {
            for (const auto &value : controller.outingLaps()) {
                const auto row = value.toMap();
                if (row.value("type") == "LAP" && !row.value("compatibilityResolved").toBool()) return false;
            }
            return true;
        };
        if (!QTest::qWaitFor(allResolved, 60000)) {
            for (const auto &value : controller.outingLaps())
                if (value.toMap().value("type") == "LAP" && !value.toMap().value("compatibilityResolved").toBool())
                    qInfo() << value.toMap().value("runName") << value.toMap().value("compatibilityReasons");
            for (const auto &message : controller.outingLapMessages()) qInfo() << message;
            QFAIL("A timed lap stayed on an unidentified layout.");
        }
        // No review: the best lap's run gets approved segments on its own.
        const auto best = controller.outingRanking().value("bestOfDay").toMap();
        const auto runId = best.value("runId").toString();
        QTRY_VERIFY_WITH_TIMEOUT(!controller.m_analysis.storedRunTrackSegments(runId).toArray().isEmpty(), 60000);
        const auto segments = controller.m_analysis.storedRunTrackSegments(runId).toArray();
        QVERIFY(segments.size() >= 4);
        QTRY_VERIFY(controller.statusText().contains("created automatically"));
        controller.requestOutingTheoreticalBest();
        QTRY_COMPARE_WITH_TIMEOUT(controller.outingTheoreticalBest().value("state").toString(), QString("ready"), 120000);
        const auto theoretical = controller.outingTheoreticalBest();
        qInfo().noquote() << QString("  %1: %2 automatic segments from %3; theoretical %4 of best %5")
            .arg(day.isEmpty() ? "synthetic" : "real day").arg(segments.size())
            .arg(best.value("runName").toString() + " LAP " + best.value("lapNumber").toString())
            .arg(theoretical.value("totalSeconds").toDouble(), 0, 'f', 3).arg(best.value("durationSeconds").toDouble(), 0, 'f', 3);
        // Once per layout and best lap: revoking them does not bring them back.
        QVERIFY(controller.m_analysis.replaceRunTrackSegments(runId, {}));
        QTest::qWait(1500);
        QVERIFY(controller.m_analysis.storedRunTrackSegments(runId).toArray().isEmpty());
    }
}

void TelemetryTests::analyzesPrivateTrackDayCorners()
{
    // KAN-117: opt-in real-day check of segmentation, theoretical best, time
    // losses and the Corner Analyzer. FLAPPEDEAR_REAL_DAY is a directory of
    // private VBO recordings (never committed); FLAPPEDEAR_CORNER_REVIEW_DIR,
    // when set, receives screenshots of the real Analysis window.
    const auto path = qEnvironmentVariable("FLAPPEDEAR_REAL_DAY");
    if (path.isEmpty()) QSKIP("FLAPPEDEAR_REAL_DAY is not set");
    const auto reviewDirectory = qEnvironmentVariable("FLAPPEDEAR_CORNER_REVIEW_DIR");
    QTemporaryDir directory; QVERIFY(directory.isValid());
    QSettings settings; settings.clear(); settings.sync();
    QList<QUrl> recordings;
    for (const auto &name : QDir(path).entryList({"*.vbo"}, QDir::Files, QDir::Name))
        recordings.append(QUrl::fromLocalFile(QDir(path).filePath(name)));
    QVERIFY(!recordings.isEmpty());
    AppController controller(nullptr, directory.filePath("recovery.json"));
    QVERIFY(controller.importAnalysisRuns("Private corners", recordings));
    QTRY_COMPARE_WITH_TIMEOUT(controller.eventRuns().size(), recordings.size(), 120000);
    QTRY_VERIFY_WITH_TIMEOUT(!controller.outingLapsLoading() && controller.m_analysis.m_outingLapRequestedKey == controller.m_analysis.outingLapKey(), 120000);
    QTRY_VERIFY_WITH_TIMEOUT(!controller.outingComparisonGroupId().isEmpty(), 60000);

    // KAN-79: every derived section, for the owner to check against RaceChrono.
    int outCount = 0, lapCount = 0, inCount = 0;
    for (const auto &value : controller.outingLaps()) {
        const auto row = value.toMap();
        const auto type = row.value("type").toString();
        outCount += type == "OUT"; lapCount += type == "LAP"; inCount += type == "IN";
        qInfo().noquote() << QString("  section %1 %2 %3 %4%5").arg(row.value("runName").toString(), -10)
            .arg(type == "LAP" ? "LAP " + row.value("lapNumber").toString() : type, -7)
            .arg(AppController::formatElapsedTime(row.value("durationSeconds").toDouble()), 10)
            .arg(row.value("referenceEligible").toBool() ? "" : " not eligible")
            .arg(row.value("referenceEligible").toBool() || type != "LAP" ? QString()
                : " (" + row.value("compatibilityReasonLabels").toStringList().join(", ") + ")");
    }
    qInfo().noquote() << "Sections: OUT" << outCount << "LAP" << lapCount << "IN" << inCount;
    // Every session starts with its out lap and ends with its in lap.
    QString previousRun;
    QString previousType;
    for (const auto &value : controller.outingLaps()) {
        const auto row = value.toMap();
        if (row.value("runName").toString() != previousRun) {
            if (!previousRun.isEmpty()) QCOMPARE(previousType, QString("IN"));
            QCOMPARE(row.value("type").toString(), QString("OUT"));
            previousRun = row.value("runName").toString();
        }
        previousType = row.value("type").toString();
    }
    QCOMPARE(previousType, QString("IN"));
    // An exclusion moves the best of the day to the next-fastest eligible lap
    // and restoring it brings the original back exactly.
    {
        const auto best = controller.outingRanking().value("bestOfDay").toMap();
        QVERIFY(controller.setOutingLapExcluded(best.value("reference").toMap(), true, "Acceptance check"));
        QTRY_VERIFY_WITH_TIMEOUT(!controller.outingLapsLoading(), 60000);
        const auto next = controller.outingRanking().value("bestOfDay").toMap();
        QVERIFY(next.value("reference") != best.value("reference"));
        QVERIFY(next.value("durationSeconds").toDouble() >= best.value("durationSeconds").toDouble());
        qInfo().noquote() << "Excluding" << best.value("runName").toString() << "LAP" << best.value("lapNumber").toInt()
                          << "moves the best of the day to" << next.value("runName").toString() << "LAP" << next.value("lapNumber").toInt()
                          << AppController::formatElapsedTime(next.value("durationSeconds").toDouble());
        QVERIFY(controller.setOutingLapExcluded(best.value("reference").toMap(), false, {}));
        QTRY_VERIFY_WITH_TIMEOUT(!controller.outingLapsLoading(), 60000);
        QCOMPARE(controller.outingRanking().value("bestOfDay").toMap().value("reference"), best.value("reference"));
    }

    // Review the best lap's run so its proposals come from a clean lap.
    const auto bestOfDay = controller.outingRanking().value("bestOfDay").toMap();
    QVERIFY(!bestOfDay.isEmpty());
    QVERIFY(controller.selectOutingLapReference(bestOfDay.value("reference").toMap()));
    QTRY_COMPARE_WITH_TIMEOUT(controller.outingLapDetailState(), QString("ready"), 60000);
    controller.requestSegmentReview();
    QTRY_COMPARE_WITH_TIMEOUT(controller.segmentReviewState(), QString("ready"), 60000);
    const auto runId = controller.selectedOutingLap().value("runId").toString();
    qInfo().noquote() << "Axis length" << controller.segmentReviewAxisLength() << "m, best lap"
        << bestOfDay.value("runName").toString() << bestOfDay.value("lapNumber").toInt()
        << bestOfDay.value("durationSeconds").toDouble();
    const auto items = controller.segmentReviewItems();
    for (const auto &value : items) {
        const auto item = value.toMap();
        qInfo().noquote() << QString("  proposal %1 %2 %3-%4 m (%5 m)").arg(item.value("name").toString(), -14)
            .arg(item.value("type").toString(), -8).arg(item.value("startMeters").toDouble(), 7, 'f', 1)
            .arg(item.value("endMeters").toDouble(), 7, 'f', 1).arg(item.value("lengthMeters").toDouble(), 6, 'f', 1);
    }
    // As a driver would: approve every proposal, without splitting the one
    // that crosses the start/finish line (KAN-120 times it within the lap).
    for (int i = 0; i < items.size(); ++i) QCOMPARE(controller.approveSegmentProposal(i), QString());
    controller.closeOutingLap();

    controller.requestOutingTheoreticalBest();
    QTRY_VERIFY_WITH_TIMEOUT(controller.outingTheoreticalBest().value("state") != "loading", 180000);
    const auto best = controller.outingTheoreticalBest();
    QVERIFY2(best.value("state") == "ready", qPrintable(best.value("message").toString()));
    const auto actual = best.value("actualBest").toMap();
    qInfo().noquote() << "Theoretical" << best.value("totalSeconds").toDouble() << "actual" << actual.value("label").toString()
        << actual.value("lapSeconds").toDouble() << "whole lap" << actual.value("coversWholeLap").toBool()
        << "difference" << best.value("differenceSeconds").toDouble();
    QString sameLapSector;
    for (const auto &value : best.value("sectors").toList()) {
        const auto sector = value.toMap();
        qInfo().noquote() << QString("  %1 best %2 donor %3 actual %4 loss %5").arg(sector.value("name").toString(), -14)
            .arg(sector.value("seconds").toDouble(), 7, 'f', 3).arg(sector.value("sourceLapLabel").toString(), -48)
            .arg(sector.value("actualSeconds").toDouble(), 7, 'f', 3).arg(sector.value("lossSeconds").toDouble(), 6, 'f', 3);
        if (sameLapSector.isEmpty() && sector.value("sourceLapReference").toMap() == actual.value("reference").toMap())
            sameLapSector = sector.value("segmentId").toString();
    }
    const auto lapConsistency = controller.outingLapConsistency();
    const auto dayLaps = lapConsistency.value("day").toMap();
    qInfo().noquote() << "Lap consistency: n" << dayLaps.value("count").toInt() << "median"
        << AppController::formatElapsedTime(dayLaps.value("median").toDouble()) << "IQR" << dayLaps.value("interquartileRange").toDouble();
    for (const auto &value : lapConsistency.value("runs").toList()) {
        const auto run = value.toMap(); const auto summary = run.value("laps").toMap();
        qInfo().noquote() << "  " << run.value("runName").toString() << "n" << summary.value("count").toInt()
            << "median" << (summary.value("available").toBool() ? AppController::formatElapsedTime(summary.value("median").toDouble()) : QString("-"))
            << "IQR" << summary.value("interquartileRange").toDouble();
    }
    for (const auto &value : best.value("sectors").toList()) {
        const auto sector = value.toMap(); const auto v = sector.value("variability").toMap();
        if (v.isEmpty()) continue;
        const auto spread = [&v](const char *key) { const auto m = v.value(key).toMap();
            return m.value("available").toBool() ? QString::number(m.value("interquartileRange").toDouble(), 'f', 1) + " (n" + m.value("count").toString() + ")" : QString("-"); };
        qInfo().noquote() << QString("  %1 brake %2 min %3 exit %4 pickup %5 line %6 gps %7 resolvable %8").arg(sector.value("name").toString(), -14)
            .arg(spread("brakingPointMeasured"), spread("minimumSpeed"), spread("exitSpeed"), spread("pickupMeasured"), spread("lineOffset"))
            .arg(v.value("typicalGpsAccuracyMeters").toDouble(), 0, 'f', 2).arg(v.value("lineSpreadResolvable").toBool());
    }
    for (const auto &value : best.value("sectors").toList()) {
        const auto sector = value.toMap(); const auto consistency = sector.value("consistency").toMap();
        qInfo().noquote() << QString("  %1 typical %2 spread %3 n %4").arg(sector.value("name").toString(), -14)
            .arg(consistency.value("median").toDouble(), 7, 'f', 3).arg(consistency.value("interquartileRange").toDouble(), 6, 'f', 3)
            .arg(consistency.value("count").toInt());
    }
    controller.requestOutingChannelSummaries();
    QTRY_VERIFY_WITH_TIMEOUT(controller.outingChannelSummaries().value("state") != "loading", 180000);
    for (const auto &value : controller.outingChannelSummaries().value("runs").toList()) {
        const auto run = value.toMap();
        for (const auto &channelValue : run.value("channels").toList()) {
            const auto channel = channelValue.toMap(); const auto summary = channel.value("run").toMap();
            qInfo().noquote() << QString("  %1 %2 min %3 mean %4 max %5 coverage %6 artifacts %7").arg(run.value("runName").toString(), -10)
                .arg(channel.value("channel").toString(), -20).arg(summary.value("minimum").toDouble(), 6, 'f', 1)
                .arg(summary.value("mean").toDouble(), 6, 'f', 1).arg(summary.value("maximum").toDouble(), 6, 'f', 1)
                .arg(summary.value("coverage").toDouble(), 5, 'f', 3).arg(summary.value("excludedArtifacts").toInt());
            for (const auto &coolingValue : channel.value("cooling").toList()) {
                const auto cooling = coolingValue.toMap();
                qInfo().noquote() << QString("      cooling %1 -> %2 over %3 s from t=%4 (%5 %6)")
                    .arg(cooling.value("startValue").toDouble(), 0, 'f', 1).arg(cooling.value("endValue").toDouble(), 0, 'f', 1)
                    .arg(cooling.value("seconds").toDouble(), 0, 'f', 0).arg(cooling.value("startTime").toDouble(), 0, 'f', 0)
                    .arg(cooling.value("type").toString()).arg(cooling.value("lapNumber").toInt());
            }
        }
    }
    for (const auto &value : controller.outingChannelSummaries().value("runs").toList()) {
        const auto heartRate = value.toMap().value("heartRate").toMap().value("run").toMap();
        if (heartRate.isEmpty()) continue;
        qInfo().noquote() << QString("  %1 heart rate min %2 mean %3 max %4 coverage %5 artifacts %6").arg(value.toMap().value("runName").toString(), -10)
            .arg(heartRate.value("minimum").toDouble(), 0, 'f', 0).arg(heartRate.value("mean").toDouble(), 0, 'f', 1)
            .arg(heartRate.value("maximum").toDouble(), 0, 'f', 0).arg(heartRate.value("coverage").toDouble(), 0, 'f', 3)
            .arg(heartRate.value("excludedArtifacts").toInt());
    }
    // KAN-71: the day report over the same computed results.
    const auto dayReport = controller.outingDayReport();
    QCOMPARE(validateDayReport(QJsonObject::fromVariantMap(dayReport)), QString());
    for (const auto &value : dayReport.value("results").toList()) {
        const auto result = value.toMap();
        qInfo().noquote() << QString("  report %1 %2 %3 evidence %4 %5").arg(result.value("id").toString(), -18)
            .arg(result.value("status").toString(), -12).arg(result.value("algorithm").toString(), -22)
            .arg(result.value("evidence").toList().size(), 4).arg(result.value("reason").toString());
    }
    for (const auto &value : dayReport.value("results").toList()) {
        if (value.toMap().value("id") != "focusAreas") continue;
        for (const auto &area : value.toMap().value("value").toMap().value("areas").toList())
            qInfo().noquote() << "  focus" << area.toMap().value("kind").toString() << "|" << area.toMap().value("observation").toString()
                              << "|" << area.toMap().value("hypothesis").toString();
    }
    if (const auto dump = qEnvironmentVariable("FLAPPEDEAR_REPORT_DUMP"); !dump.isEmpty()) {
        QFile file(dump); // compare refactors byte for byte (keys are sorted)
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QJsonDocument(QJsonObject::fromVariantMap(dayReport)).toJson(QJsonDocument::Indented));
    }
    qInfo().noquote() << "  report size" << QJsonDocument(QJsonObject::fromVariantMap(dayReport)).toJson(QJsonDocument::Compact).size() << "bytes";
    // KAN-74: save and reopen the real day; the same decisions give the same report.
    {
        const auto keyValues = [](const QVariantMap &report) {
            QStringList values{report.value("decisionsKey").toString()};
            for (const auto &value : report.value("results").toList()) {
                const auto result = value.toMap();
                const auto content = result.value("value").toMap();
                values << result.value("id").toString() + ":" + result.value("status").toString() + ":"
                        + QString::number(result.value("evidence").toList().size());
                if (result.value("id") == "bestLap") values << content.value("label").toString() + QString::number(content.value("seconds").toDouble(), 'f', 6);
                if (result.value("id") == "theoreticalBest") values << QString::number(content.value("totalSeconds").toDouble(), 'f', 6);
                if (result.value("id") == "focusAreas")
                    for (const auto &area : content.value("areas").toList()) values << area.toMap().value("observation").toString();
            }
            return values;
        };
        const auto before = keyValues(controller.outingDayReport());
        const auto projectPath = directory.filePath("real-day.fetproject");
        QVERIFY(controller.saveProject(QUrl::fromLocalFile(projectPath)));
        // Saving re-derives the laps under the new path; the computed results stay.
        QTest::qWait(50);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.outingLapsLoading(), 120000);
        QCOMPARE(controller.outingTheoreticalBest().value("state").toString(), QString("ready"));
        QCOMPARE(controller.outingChannelSummaries().value("state").toString(), QString("ready"));
        AppController reopened(nullptr, directory.filePath("recovery-reopen.json"));
        reopened.requestOpenProject(QUrl::fromLocalFile(projectPath));
        QTRY_VERIFY_WITH_TIMEOUT(!reopened.outingComparisonGroupId().isEmpty() && !reopened.outingLapsLoading(), 180000);
        reopened.requestOutingDayReport();
        const auto settled = [&] {
            for (const auto &value : reopened.outingDayReport().value("results").toList())
                if (value.toMap().value("status") != "available") return false;
            return true;
        };
        QTRY_VERIFY_WITH_TIMEOUT(settled(), 240000);
        const auto after = keyValues(reopened.outingDayReport());
        QCOMPARE(after, before);
        qInfo().noquote() << "  reopened report matches:" << after.size() << "key values";
    }
    const auto ranking = controller.outingTimeLossRanking();
    qInfo().noquote() << "Losses:" << ranking.value("observationCount").toInt() << "observed over"
        << ranking.value("comparedLapCount").toInt() << "laps";
    const auto losses = ranking.value("losses").toList();
    for (qsizetype i = 0; i < std::min<qsizetype>(10, losses.size()); ++i) {
        const auto loss = losses[i].toMap();
        qInfo().noquote() << QString("  #%1 +%2 s %3 (%4) %5").arg(i + 1).arg(loss.value("lossSeconds").toDouble(), 0, 'f', 3)
            .arg(loss.value("name").toString(), loss.value("role").toString(), loss.value("lapLabel").toString());
    }

    // A sector whose donor is the actual best must never compare a lap with itself.
    if (!sameLapSector.isEmpty()) {
        QVERIFY(controller.openTheoreticalBestSector(sameLapSector));
        QTRY_VERIFY_WITH_TIMEOUT(controller.comparisonPairReady(), 60000);
        const auto pair = controller.comparisonSlots();
        QVERIFY2(pair[0].toMap().value("lap").toMap().value("reference") != pair[1].toMap().value("lap").toMap().value("reference"),
            "A theoretical-best sector opened a lap against itself");
        controller.setComparisonViewOpen(false);
    }

    // Corner Analyzer on the top loss, shown in the real Analysis window.
    QVERIFY(!losses.isEmpty());
    const auto top = losses.first().toMap();
    QVERIFY(controller.openTimeLoss(top));
    QTRY_VERIFY_WITH_TIMEOUT(controller.comparisonPairReady(), 60000);
    // KAN-65: G-G pairs for lap A of the pair on the real recording.
    {
        const auto &slot = controller.m_analysis.m_comparisonSlots[0];
        const auto pairs = buildGgPairs(*slot.session, slot.row.value("startTime").toDouble(), slot.row.value("endTime").toDouble());
        qInfo().noquote() << "G-G pairs:" << pairs.points.size() << "of" << pairs.candidateCount << "channels"
            << pairs.longitudinalChannel << pairs.lateralChannel << "shared clock" << pairs.sharedClock
            << "units declared" << pairs.unitsDeclared << "gaps" << pairs.skippedForGap << "outliers" << pairs.excludedOutliers;
        QVERIFY(pairs.valid && !pairs.points.isEmpty());
    }
    qInfo() << "Channels" << controller.comparisonAvailableChannels().mid(0, 6) << "preferred"
        << controller.comparisonPreferredChannels();
    for (const auto &value : controller.comparisonApprovedSegments()) {
        const auto segment = value.toMap();
        const auto metrics = controller.comparisonSegmentMetrics(segment.value("id").toString());
        const auto speeds = metrics.value("speeds").toMap();
        const auto pickup = metrics.value("exitEffects").toMap().value("pickup").toMap();
        if (!pickup.isEmpty())
            qInfo().noquote() << QString("  %1 throttle pickup A %2 B %3 (%4)").arg(segment.value("name").toString(), -14)
                .arg(pickup.value("a").toMap().value("value").toDouble(), 7, 'f', 1).arg(pickup.value("b").toMap().value("value").toDouble(), 7, 'f', 1)
                .arg(pickup.value("a").toMap().value("unavailableReason").toString());
        qInfo().noquote() << QString("  %1 sector A %2 B %3 | entry A %4 max A %5 min A %6 exit A %7").arg(segment.value("name").toString(), -14)
            .arg(metrics.value("sectorTime").toMap().value("a").toMap().value("value").toDouble(), 7, 'f', 3)
            .arg(metrics.value("sectorTime").toMap().value("b").toMap().value("value").toDouble(), 7, 'f', 3)
            .arg(speeds.value("entry").toMap().value("a").toMap().value("value").toDouble(), 6, 'f', 1)
            .arg(speeds.value("maximum").toMap().value("a").toMap().value("value").toDouble(), 6, 'f', 1)
            .arg(speeds.value("minimum").toMap().value("a").toMap().value("value").toDouble(), 6, 'f', 1)
            .arg(speeds.value("exit").toMap().value("a").toMap().value("value").toDouble(), 6, 'f', 1);
    }
    if (reviewDirectory.isEmpty()) return;
    QQmlEngine engine; engine.rootContext()->setContextProperty("appController", &controller);
    QSignalSpy warnings(&engine, &QQmlEngine::warnings); QQmlComponent component(&engine);
    component.loadUrl(QUrl::fromLocalFile(QFileInfo(QStringLiteral(ANALYSIS_PANEL_QML_PATH)).dir().filePath("AnalysisWindow.qml")));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    std::unique_ptr<QObject> object(component.createWithInitialProperties({{"videoSource", QUrl{}},
        {"playbackPosition", 0}, {"playbackRunning", false}, {"mediaDuration", 0}}));
    QVERIFY2(object, qPrintable(component.errorString()));
    auto *window = qobject_cast<QQuickWindow *>(object.get()); QVERIFY(window);
    // KAN-78: FLAPPEDEAR_REVIEW_SIZE (e.g. 760x480) reviews the real day at a
    // minimum size; every screenshot state is then checked for reachability.
    const auto sizeText = qEnvironmentVariable("FLAPPEDEAR_REVIEW_SIZE", "1600x900").split('x');
    const QSize reviewSize(sizeText.value(0).toInt(), sizeText.value(1).toInt());
    QVERIFY(reviewSize.isValid());
    window->resize(reviewSize);
    controller.setAnalysisVisible(true);
    window->show(); QVERIFY(QTest::qWaitForWindowExposed(window));
    QStringList unreachable;
    const auto reachable = [&](const QString &state) {
        for (const auto &problem : unreachableControls(window)) unreachable << state + ": " + problem;
    };
    // Reopen with the window present, as in the app where the panel always exists.
    controller.setComparisonViewOpen(false);
    QVERIFY(controller.openTimeLoss(top));
    QTRY_VERIFY_WITH_TIMEOUT(controller.comparisonPairReady(), 60000);
    QTRY_VERIFY(controller.comparisonFocusSegmentId().isEmpty());
    QTest::qWait(1500);
    for (const auto &value : controller.comparisonHeartRate(0.0, controller.comparisonProgressAxisLength()).value("laps").toList())
        qInfo().noquote() << QString("  pair heart rate mean %1 bpm, %2 samples, coverage %3").arg(value.toMap().value("mean").toDouble(), 0, 'f', 1)
            .arg(value.toMap().value("sampleCount").toInt()).arg(value.toMap().value("coverage").toDouble(), 0, 'f', 3);
    QVERIFY(window->grabWindow().save(QDir(reviewDirectory).filePath("corner-analyzer.png"))); reachable("corner-analyzer");
    if (auto *ggToggle = window->findChild<QQuickItem *>("comparisonToggleGg")) {
        ggToggle->forceActiveFocus(); QTest::keyClick(window, Qt::Key_Space); QTest::qWait(1000);
        QVERIFY(window->grabWindow().save(QDir(reviewDirectory).filePath("gg.png"))); reachable("gg");
        ggToggle->forceActiveFocus(); QTest::keyClick(window, Qt::Key_Space); QTest::qWait(300);
    }
    // The segment list is a ListView (delegates exist only when in view), so
    // a segment is selected through the panel, as a delegate click does.
    auto *segmentPanel = window->findChild<QQuickItem *>("comparisonSegmentPanel"); QVERIFY(segmentPanel);
    const auto selectSegment = [&](const QVariantMap &segment) {
        if (!segmentPanel->isVisible()) {
            auto *toggle = window->findChild<QQuickItem *>("comparisonToggleCornerAnalyzer"); QVERIFY(toggle);
            toggle->forceActiveFocus(); QTest::keyClick(window, Qt::Key_Space);
            QTRY_VERIFY(segmentPanel->isVisible());
        }
        segmentPanel->setProperty("selectedSegmentId", segment.value("id"));
        QVERIFY(QMetaObject::invokeMethod(segmentPanel, "selectMetric",
            Q_ARG(QVariant, segment.value("startMeters")), Q_ARG(QVariant, segment.value("endMeters"))));
        QTest::qWait(800);
    };
    for (const auto &value : controller.comparisonApprovedSegments()) {
        if (value.toMap().value("type") != "straight") continue;
        selectSegment(value.toMap());
        QVERIFY(window->grabWindow().save(QDir(reviewDirectory).filePath("straight.png")));
        break;
    }
    // KAN-93: trail braking in the longest corner of the pair.
    QVariantMap longestCorner;
    const auto segmentLength = [](const QVariantMap &segment) {
        return segment.value("endMeters").toDouble() - segment.value("startMeters").toDouble();
    };
    for (const auto &value : controller.comparisonApprovedSegments())
        if (value.toMap().value("type") == "corner" && segmentLength(value.toMap()) > segmentLength(longestCorner))
            longestCorner = value.toMap();
    if (!longestCorner.isEmpty()) {
        for (const auto &value : controller.comparisonTrailBraking(longestCorner.value("startMeters").toDouble(),
                 longestCorner.value("endMeters").toDouble()).value("laps").toList()) {
            const auto lap = value.toMap();
            qInfo().noquote() << QString("  %1 trail braking %2 s / %3 m (braking %4 s %5, cornering %6 s %7)")
                .arg(longestCorner.value("name").toString()).arg(lap.value("overlapSeconds").toDouble(), 0, 'f', 2)
                .arg(lap.value("overlapMeters").toDouble(), 0, 'f', 0).arg(lap.value("brakingSeconds").toDouble(), 0, 'f', 2)
                .arg(lap.value("brakingProvenance").toString()).arg(lap.value("corneringSeconds").toDouble(), 0, 'f', 2)
                .arg(lap.value("corneringProvenance").toString());
        }
        selectSegment(longestCorner);
        QTRY_VERIFY(window->findChild<QQuickItem *>("cornerAnalyzerTrailBrakingRow")->isVisible());
        // Scroll to the end once wrapped labels have settled the content height.
        QTest::qWait(500);
        if (auto *scroll = window->findChild<QQuickItem *>("cornerAnalyzerScroll"))
            scroll->setProperty("contentY", std::max(0.0, scroll->property("contentHeight").toDouble() - scroll->height()));
        QTest::qWait(300);
        QVERIFY(window->grabWindow().save(QDir(reviewDirectory).filePath("trail-braking.png"))); reachable("trail-braking");
    }
    // KAN-97: map layers on the pair, lap B (the faster lap of the loss).
    for (const auto &value : controller.comparisonMapLayerOptions()) {
        const auto option = value.toMap();
        const auto layer = controller.comparisonMapLayer(option.value("id").toString(), 1);
        qInfo().noquote() << QString("  map layer %1: %2 %3..%4 %5 (%6)").arg(option.value("id").toString())
            .arg(layer.value("valid").toBool() ? "valid" : "unavailable " + layer.value("reason").toString())
            .arg(layer.value("minimum").toDouble(), 0, 'f', 2).arg(layer.value("maximum").toDouble(), 0, 'f', 2)
            .arg(layer.value("unit").toString()).arg(layer.value("provenance").toString());
    }
    if (auto *map = window->findChild<QQuickItem *>("comparisonOverlayMap")) {
        segmentPanel->setProperty("selectedSegmentId", QString());
        for (const auto *id : {"speed", "brake"}) {
            map->setProperty("layerId", QString::fromLatin1(id));
            QTest::qWait(800);
            QVERIFY(window->grabWindow().save(QDir(reviewDirectory).filePath(QString("map-layer-%1.png").arg(id))));
            reachable(QString("map-layer-%1").arg(id));
        }
        map->setProperty("layerId", QString());
    }
    // The theoretical-best window on the same real day.
    controller.setComparisonViewOpen(false);
    QTest::qWait(500);
    auto *theoretical = window->findChild<QObject *>("theoreticalBestDialog");
    if (theoretical) {
        QVERIFY(QMetaObject::invokeMethod(theoretical, "open"));
        QTest::qWait(1000);
        for (const auto &value : controller.outingTheoreticalBest().value("gains").toList())
            if (value.toMap().value("type") == "corner") { theoretical->setProperty("selectedSegmentId", value.toMap().value("segmentId")); break; }
        QTest::qWait(800);
        QVERIFY(window->grabWindow().save(QDir(reviewDirectory).filePath("theoretical-best.png"))); reachable("theoretical-best");
        QVERIFY(QMetaObject::invokeMethod(theoretical, "close"));
    }
    auto *progressionDialog = window->findChild<QObject *>("outingProgressionDialog");
    auto *progressionTabs = window->findChild<QObject *>("progressionTabs");
    if (progressionDialog && progressionTabs) {
        QVERIFY(QMetaObject::invokeMethod(progressionDialog, "open"));
        progressionTabs->setProperty("currentIndex", 1);
        QTest::qWait(1200);
        QVERIFY(window->grabWindow().save(QDir(reviewDirectory).filePath("section-progression.png"))); reachable("section-progression");
        progressionTabs->setProperty("currentIndex", 2);
        QTRY_COMPARE_WITH_TIMEOUT(controller.outingChannelSummaries().value("state").toString(), QString("ready"), 180000);
        QTest::qWait(500);
        QVERIFY(window->grabWindow().save(QDir(reviewDirectory).filePath("car-driver.png"))); reachable("car-driver");
        // KAN-100: temperature associations over the eligible laps, and the
        // first card's association block on screen.
        const auto findVisual = [](auto &&self, QQuickItem *item, const QString &name) -> QQuickItem * {
            if (item->objectName() == name) return item;
            for (auto *child : item->childItems()) if (auto *found = self(self, child, name)) return found;
            return nullptr;
        };
        auto *carDriverScroll = findVisual(findVisual, window->contentItem(), "carDriverScroll");
        auto *association = findVisual(findVisual, window->contentItem(), "carDriverAssociation0");
        if (carDriverScroll && association) {
            auto *content = carDriverScroll->property("contentItem").value<QQuickItem *>();
            const double y = association->mapToItem(content, QPointF(0, 0)).y();
            carDriverScroll->setProperty("contentY", std::max(0.0, std::min(y - 40.0,
                carDriverScroll->property("contentHeight").toDouble() - carDriverScroll->height())));
            QTest::qWait(500);
            QVERIFY(window->grabWindow().save(QDir(reviewDirectory).filePath("temperature-association.png")));
            reachable("temperature-association");
        }
        const auto associations = controller.outingTemperatureAssociations();
        qInfo().noquote() << QString("  temperature associations over %1 eligible laps").arg(associations.value("eligibleLaps").toInt());
        const auto rho = [](const QVariantMap &correlation) {
            return correlation.value("available").toBool()
                ? QString("%1 (%2, n=%3)").arg(correlation.value("coefficient").toDouble(), 0, 'f', 2)
                      .arg(correlation.value("strength").toString()).arg(correlation.value("count").toInt())
                : QString("unavailable %1 (n=%2)").arg(correlation.value("unavailableReason").toString()).arg(correlation.value("count").toInt());
        };
        for (const auto &value : associations.value("channels").toList()) {
            const auto channel = value.toMap();
            qInfo().noquote() << QString("  %1: lap time %2; acceleration %3; order %4%5; low coverage %6, not recorded %7")
                .arg(channel.value("channel").toString(), rho(channel.value("lapTime").toMap()),
                     rho(channel.value("acceleration").toMap()), rho(channel.value("order").toMap()),
                     channel.value("confoundedByOrder").toBool() ? " CONFOUNDED" : "")
                .arg(channel.value("lowCoverageLaps").toInt()).arg(channel.value("notRecordedLaps").toInt());
        }
        QVERIFY(QMetaObject::invokeMethod(progressionDialog, "close"));
    }
    if (auto *reportDialog = window->findChild<QObject *>("dayReportDialog")) {
        QVERIFY(QMetaObject::invokeMethod(reportDialog, "open"));
        const auto reportReady = [&] {
            int available = 0;
            for (const auto &value : controller.outingDayReport().value("results").toList())
                available += value.toMap().value("status") == "available" ? 1 : 0;
            return available == controller.outingDayReport().value("results").toList().size();
        };
        QTRY_VERIFY_WITH_TIMEOUT(reportReady(), 180000);
        QTest::qWait(800);
        QVERIFY(window->grabWindow().save(QDir(reviewDirectory).filePath("day-report.png"))); reachable("day-report");
        if (auto *flickable = reportDialog->property("contentItem").value<QQuickItem *>()) {
            flickable->setProperty("contentY", flickable->property("contentHeight").toDouble() - flickable->height());
            QTest::qWait(500);
            QVERIFY(window->grabWindow().save(QDir(reviewDirectory).filePath("day-report-end.png")));
        }
    }
    // KAN-92: the best lap's coasting, with both pedals recorded.
    if (auto *reportDialog = window->findChild<QObject *>("dayReportDialog")) QMetaObject::invokeMethod(reportDialog, "close");
    QVERIFY(controller.selectOutingLapReference(controller.outingRanking().value("bestOfDay").toMap().value("reference").toMap()));
    QTRY_COMPARE_WITH_TIMEOUT(controller.outingLapDetailState(), QString("ready"), 60000);
    if (auto *coastingToggle = window->findChild<QQuickItem *>("toggleCoasting")) {
        coastingToggle->forceActiveFocus(); QTest::keyClick(window, Qt::Key_Space);
        QTRY_VERIFY_WITH_TIMEOUT(window->findChild<QQuickItem *>("coastingPanel")
            && window->findChild<QQuickItem *>("coastingPanel")->property("coasting").toMap().value("segmentsReady").toBool(), 60000);
        const auto coasting = window->findChild<QQuickItem *>("coastingPanel")->property("coasting").toMap();
        QCOMPARE(coasting.value("provenance").toString(), QString("measured"));
        qInfo().noquote() << QString("Coasting on the best lap: %1 s, %2 m, %3 episodes")
            .arg(coasting.value("coastingSeconds").toDouble(), 0, 'f', 1).arg(coasting.value("coastingMeters").toDouble(), 0, 'f', 0)
            .arg(coasting.value("episodes").toList().size());
        for (const auto &value : coasting.value("segments").toList()) {
            const auto segment = value.toMap();
            if (segment.value("seconds").toDouble() > 0.05)
                qInfo().noquote() << QString("  %1 %2 s %3 m").arg(segment.value("name").toString(), -14)
                    .arg(segment.value("seconds").toDouble(), 0, 'f', 1).arg(segment.value("meters").toDouble(), 0, 'f', 0);
        }
        QTest::qWait(500);
        QVERIFY(window->grabWindow().save(QDir(reviewDirectory).filePath("coasting.png"))); reachable("coasting");
    }
    for (const auto &warning : warnings) qInfo() << "QML warning" << warning;
    QVERIFY2(unreachable.isEmpty(), qPrintable(unreachable.join('\n')));
}

void TelemetryTests::selectsCornerAnalyzerSegmentThroughQml()
{
    // KAN-55: the Corner Analyzer panel inside ComparisonDetailPanel.qml
    // renders the approved-segment list and per-segment metrics, and
    // "jump to this segment" sets the shared zoom/hover state the way
    // ComparisonOverlayChart/ComparisonOverlayMap already consume it --
    // reusing the existing shared cursor, not a second one.
    QTemporaryDir directory; QVERIFY(directory.isValid());
    QSettings settings; settings.clear(); settings.sync();
    const auto path = directory.filePath("session.vbo");
    QVERIFY(writeBytes(path, EventProjectFixture::routeVbo()));
    AppController controller(nullptr, directory.filePath("recovery.json"));
    QVERIFY(controller.importAnalysisRuns("Corner Analyzer QML", {QUrl::fromLocalFile(path)}));
    QTRY_COMPARE(controller.vboLoadState(), QString("ready"));
    QTRY_VERIFY(!controller.outingLapsLoading());
    const auto candidates = controller.comparisonLaps(); QVERIFY(candidates.size() >= 2);
    const auto a = candidates[0].toMap(), b = candidates[1].toMap();
    QVERIFY(controller.selectComparisonLap(0, a.value("reference").toMap()));
    QVERIFY(controller.selectComparisonLap(1, b.value("reference").toMap()));
    QTRY_VERIFY(controller.comparisonPairReady());

    int lapIndex = -1;
    const auto rows = controller.outingLaps();
    for (int i = 0; i < rows.size() && lapIndex < 0; ++i) {
        const auto row = rows[i].toMap();
        if (row.value("type") == "LAP" && !row.value("compatibilityGroupId").toString().isEmpty()) lapIndex = i;
    }
    QVERIFY(lapIndex >= 0);
    QVERIFY(controller.selectOutingLap(lapIndex));
    QTRY_COMPARE(controller.outingLapDetailState(), QString("ready"));
    controller.requestSegmentReview();
    QTRY_COMPARE(controller.segmentReviewState(), QString("ready"));
    const auto count = controller.segmentReviewItems().size();
    for (int i = 0; i < count; ++i) QCOMPARE(controller.approveSegmentProposal(i), QString());
    QString wrappingId;
    const auto runId = controller.selectedOutingLap().value("runId").toString();
    for (const auto &value : controller.m_analysis.storedRunTrackSegments(runId).toArray()) {
        const auto segment = value.toObject();
        if (segment.value("endProgressMeters").toDouble() < segment.value("startProgressMeters").toDouble())
            wrappingId = segment.value("id").toString();
    }
    if (!wrappingId.isEmpty())
        QCOMPARE(controller.splitApprovedSegment(wrappingId, controller.segmentReviewAxisLength()), QString());
    controller.closeOutingLap();
    QVERIFY(!controller.comparisonApprovedSegments().isEmpty());

    QQmlEngine engine; engine.rootContext()->setContextProperty("appController", &controller);
    QSignalSpy warnings(&engine, &QQmlEngine::warnings);
    QQmlComponent component(&engine);
    component.setData("import QtQuick\nWindow { width: 900; height: 600; visible: true; "
        "ComparisonDetailPanel { objectName: \"comparisonRoot\"; anchors.fill: parent } }",
        QUrl::fromLocalFile(QStringLiteral(ANALYSIS_PANEL_QML_PATH)));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    std::unique_ptr<QObject> object(component.create()); QVERIFY2(object, qPrintable(component.errorString()));
    auto *window = qobject_cast<QQuickWindow *>(object.get()); QVERIFY(window);
    window->show(); QVERIFY(QTest::qWaitForWindowExposed(window));

    auto *toggle = window->findChild<QQuickItem *>("comparisonToggleCornerAnalyzer"); QVERIFY(toggle);
    toggle->forceActiveFocus(); QTest::keyClick(window, Qt::Key_Space);
    auto *panel = window->findChild<QObject *>("comparisonSegmentPanel"); QVERIFY(panel);
    QTRY_VERIFY(panel->property("visible").toBool());
    QTRY_COMPARE(panel->property("segments").toList().size(), controller.comparisonApprovedSegments().size());

    auto *sectorA = window->findChild<QObject *>("cornerAnalyzerSectorTimeA"); QVERIFY(sectorA);
    auto *sectorB = window->findChild<QObject *>("cornerAnalyzerSectorTimeB"); QVERIFY(sectorB);
    auto *sectorDelta = window->findChild<QObject *>("cornerAnalyzerSectorTimeDelta"); QVERIFY(sectorDelta);
    QTRY_VERIFY(sectorA->property("text").toString().contains(" s"));
    QTRY_VERIFY(sectorB->property("text").toString().contains(" s"));
    QTRY_VERIFY(sectorDelta->property("text").toString().contains(" s"));
    QVERIFY(!sectorA->property("text").toString().contains("—"));

    auto *comparisonRoot = window->findChild<QObject *>("comparisonRoot"); QVERIFY(comparisonRoot);
    const auto zoomStartBefore = comparisonRoot->property("zoomStart").toDouble();
    const auto zoomEndBefore = comparisonRoot->property("zoomEnd").toDouble();

    auto *jump = window->findChild<QQuickItem *>("cornerAnalyzerSectorTimeSelect"); QVERIFY(jump);
    jump->forceActiveFocus(); QTest::keyClick(window, Qt::Key_Space);
    QTRY_VERIFY(comparisonRoot->property("hoverDistanceMeters").toDouble() >= 0.0);
    // Selecting a segment set the shared range to that segment's own bounds,
    // not the full-lap default it started at.
    QVERIFY(comparisonRoot->property("zoomStart").toDouble() != zoomStartBefore
        || comparisonRoot->property("zoomEnd").toDouble() != zoomEndBefore);
    QCOMPARE(warnings.size(), 0);
}

void TelemetryTests::showsComparisonSlotCompatibilityAndCoverageContext()
{
    // KAN-40: the full-screen compare view must show compatibility group,
    // exclusions and the reason a slot is unavailable directly (not just the
    // lap-picker dialog), since a slot can go stale/error while this view is
    // already open. The text is always-visible plain Label content, not
    // behind a mouse-only hover/tooltip, so there is nothing here a keyboard
    // user could fail to reach.
    QTemporaryDir directory; QVERIFY(directory.isValid());
    QSettings settings; settings.clear(); settings.sync();
    const auto first = directory.filePath("morning.vbo"), second = directory.filePath("afternoon.vbo");
    QVERIFY(writeBytes(first, EventProjectFixture::routeVbo()));
    QVERIFY(writeBytes(second, EventProjectFixture::routeVbo(130, -2, 2)));
    AppController controller(nullptr, directory.filePath("recovery.json"));
    QVERIFY(controller.importAnalysisRuns("Coverage", {QUrl::fromLocalFile(first), QUrl::fromLocalFile(second)}));
    QTRY_COMPARE(controller.vboLoadState(), QString("ready"));
    QTRY_VERIFY(!controller.outingLapsLoading());
    const auto candidates = controller.comparisonLaps(); QVERIFY(candidates.size() >= 2);
    QVariantMap a, b;
    for (const auto &value : candidates) {
        const auto row = value.toMap();
        if (a.isEmpty()) a = row;
        else if (b.isEmpty() && row.value("runId") != a.value("runId")
            && row.value("compatibilityGroupId") == a.value("compatibilityGroupId")) b = row;
    }
    QVERIFY(!a.isEmpty() && !b.isEmpty());

    QQmlEngine engine; engine.rootContext()->setContextProperty("appController", &controller);
    QSignalSpy warnings(&engine, &QQmlEngine::warnings);
    QQmlComponent component(&engine);
    component.setData(
        "import QtQuick\nWindow { width: 900; height: 600; visible: true; ComparisonDetailPanel { anchors.fill: parent } }",
        QUrl::fromLocalFile(QStringLiteral(ANALYSIS_PANEL_QML_PATH)));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    std::unique_ptr<QObject> object(component.create()); QVERIFY2(object, qPrintable(component.errorString()));
    auto *window = qobject_cast<QQuickWindow *>(object.get()); QVERIFY(window);
    window->show(); QVERIFY(QTest::qWaitForWindowExposed(window));

    auto *statusRepeater = window->findChild<QObject *>("comparisonSlotStatusRepeater"); QVERIFY(statusRepeater);
    QQuickItem *statusA = nullptr, *statusB = nullptr;
    QVERIFY(QMetaObject::invokeMethod(statusRepeater, "itemAt", Q_RETURN_ARG(QQuickItem *, statusA), Q_ARG(int, 0)) && statusA);
    QVERIFY(QMetaObject::invokeMethod(statusRepeater, "itemAt", Q_RETURN_ARG(QQuickItem *, statusB), Q_ARG(int, 1)) && statusB);
    QCOMPARE(statusA->property("text").toString(), QString("Lap A: No lap selected."));
    QCOMPARE(statusB->property("text").toString(), QString("Lap B: No lap selected."));

    QVERIFY(controller.selectComparisonLap(0, a.value("reference").toMap()));
    QVERIFY(controller.selectComparisonLap(1, b.value("reference").toMap()));
    QTRY_VERIFY(controller.comparisonPairReady());
    const auto expectedGroupLabel = a.value("compatibilityGroupLabel").toString();
    QVERIFY(!expectedGroupLabel.isEmpty());
    QTRY_VERIFY(statusA->property("text").toString().contains(expectedGroupLabel));
    // Ready and mutually compatible: neutral color, not the warning one.
    QCOMPARE(qvariant_cast<QColor>(statusA->property("color")), QColor("#657386"));

    // Excluding the underlying lap while the view is open surfaces the
    // resulting error reason directly, not just a blank "not ready" screen.
    QVERIFY(controller.setOutingLapExcluded(a.value("reference").toMap(), true, "Traffic"));
    QTRY_COMPARE(controller.m_analysis.m_comparisonSlots[0].state, QString("error"));
    const auto reason = controller.m_analysis.m_comparisonSlots[0].error;
    QVERIFY(!reason.isEmpty());
    QTRY_VERIFY(statusA->property("text").toString().contains(reason));
    QCOMPARE(qvariant_cast<QColor>(statusA->property("color")), QColor("#ffb84d"));
    auto *pairStatus = window->findChild<QObject *>("comparisonPairStatus"); QVERIFY(pairStatus);
    QVERIFY(pairStatus->property("visible").toBool());
    QCOMPARE(warnings.size(), 0);
}

void TelemetryTests::sharesComparisonCacheAndRevalidatesSources()
{
    QTemporaryDir directory; QVERIFY(directory.isValid());
    QSettings settings; settings.clear(); settings.sync();
    const auto path = directory.filePath("shared.vbo");
    const auto bytes = EventProjectFixture::routeVbo(); QVERIFY(writeBytes(path, bytes));
    AppController controller(nullptr, directory.filePath("recovery.json"));
    QVERIFY(controller.importAnalysisRuns("Shared source", {QUrl::fromLocalFile(path)}));
    QTRY_COMPARE(controller.vboLoadState(), QString("ready")); QTRY_VERIFY(!controller.outingLapsLoading());
    const auto choices = controller.comparisonLaps(); QVERIFY(choices.size() >= 2);
    const auto a = choices[0].toMap(), b = choices[1].toMap();
    QVERIFY(controller.selectComparisonLap(0, a.value("reference").toMap()));
    QTRY_COMPARE(controller.m_analysis.m_comparisonSlots[0].state, QString("ready"));
    const auto session = controller.m_analysis.m_comparisonSlots[0].session;
    const auto cost = controller.m_analysis.m_analysisSourceCache->usedBytes();
    QVERIFY(controller.selectComparisonLap(1, b.value("reference").toMap()));
    QTRY_VERIFY(controller.comparisonPairReady());
    QCOMPARE(controller.m_analysis.m_comparisonSlots[1].session, session);
    QCOMPARE(controller.m_analysis.m_comparisonSlots[1].row.value("reference"), b.value("reference"));
    QVERIFY(controller.inspectComparisonLap(1));
    QTRY_COMPARE(controller.outingLapDetailState(), QString("ready"));
    QCOMPARE(controller.m_analysis.m_outingLapDetailSession, session);
    QCOMPARE(controller.m_analysis.m_analysisSourceCache->usedBytes(), cost);
    // The cache is source-wide; a different derivation cannot reuse its entry.
    const auto source = controller.m_analysis.m_comparisonSlots[0].source;
    auto revised = a; auto reference = a.value("reference").toMap();
    reference.insert("derivationKey", QString(64, 'b')); revised.insert("reference", reference);
    const auto token = std::make_shared<std::atomic_bool>(false);
    const auto changedRevision = FlappedEar::loadOutingLapDetail(source, {}, revised, 1, token, controller.m_analysis.m_analysisSourceCache);
    QVERIFY2(changedRevision.session != nullptr, qPrintable(changedRevision.error));
    QVERIFY(changedRevision.session != session);
    // Even a cached source must still exist and match its full content digest.
    QVERIFY(QFile::remove(path));
    const auto missing = FlappedEar::loadOutingLapDetail(source, {}, a, 2, token, controller.m_analysis.m_analysisSourceCache);
    QVERIFY(!missing.session); QVERIFY(missing.track.isEmpty()); QVERIFY(!missing.error.isEmpty());
    QVERIFY(writeBytes(path, bytes));
    const auto restored = FlappedEar::loadOutingLapDetail(source, {}, a, 3, token, controller.m_analysis.m_analysisSourceCache);
    QCOMPARE(restored.session, session);
    auto changed = bytes; changed.replace("coordinate units = degrees", "coordinate units = degreeS");
    QCOMPARE(changed.size(), bytes.size()); QVERIFY(changed != bytes); QVERIFY(writeBytes(path, changed));
    const auto stale = FlappedEar::loadOutingLapDetail(source, {}, a, 4, token, controller.m_analysis.m_analysisSourceCache);
    QVERIFY(!stale.session); QVERIFY(stale.track.isEmpty()); QVERIFY(stale.staleReference);
    QCOMPARE(controller.m_analysis.m_comparisonSlots[0].session, session); // Read-only worker cannot mutate GUI state.
}

void TelemetryTests::rejectsComparisonBeyondSharedBudget()
{
    QTemporaryDir directory; QVERIFY(directory.isValid());
    QSettings settings; settings.clear(); settings.sync();
    const auto first = directory.filePath("first.vbo"), second = directory.filePath("second.vbo");
    QVERIFY(writeBytes(first, EventProjectFixture::routeVbo(2400)));
    QVERIFY(writeBytes(second, EventProjectFixture::routeVbo(6800)));
    constexpr qint64 budget = 2 * 1024 * 1024;
    QVERIFY(TelemetrySource::load(second, {}, budget).sampleCount > 0); // B fits alone.
    AppController controller(nullptr, directory.filePath("recovery.json"));
    controller.m_analysis.m_analysisSourceCache = std::make_shared<TelemetrySessionCache>(budget);
    QVERIFY(controller.importAnalysisRuns("Shared budget", {QUrl::fromLocalFile(first), QUrl::fromLocalFile(second)}));
    QTRY_COMPARE(controller.vboLoadState(), QString("ready")); QTRY_VERIFY(!controller.outingLapsLoading());
    const auto runA = controller.eventRuns()[0].toMap().value("id");
    QVariantMap a, b;
    for (const auto &value : controller.comparisonLaps()) {
        const auto row = value.toMap();
        if (row.value("runId") == runA) a = row; else b = row;
    }
    QVERIFY(!a.isEmpty() && !b.isEmpty());
    QVERIFY(controller.selectComparisonLap(0, a.value("reference").toMap()));
    QTRY_COMPARE(controller.m_analysis.m_comparisonSlots[0].state, QString("ready"));
    const auto track = controller.m_analysis.m_comparisonSlots[0].track;
    const auto retained = controller.m_analysis.m_comparisonSlots[0].session.get();
    QVERIFY(controller.selectComparisonLap(1, b.value("reference").toMap()));
    QTRY_COMPARE(controller.m_analysis.m_comparisonSlots[1].state, QString("error"));
    QVERIFY(controller.m_analysis.m_comparisonSlots[1].error.contains("memory budget"));
    QCOMPARE(controller.m_analysis.m_comparisonSlots[0].session.get(), retained);
    QCOMPARE(controller.m_analysis.m_comparisonSlots[0].track, track);
    QVERIFY(controller.m_analysis.m_analysisSourceCache->usedBytes() <= budget);
    controller.clearComparisonLap(0);
    QVERIFY(controller.selectComparisonLap(1, b.value("reference").toMap()));
    QTRY_COMPARE(controller.m_analysis.m_comparisonSlots[1].state, QString("ready"));
    QVERIFY(controller.m_analysis.m_analysisSourceCache->usedBytes() <= budget);
}

void TelemetryTests::cancelsSupersededComparisonWaitingForCache()
{
    QTemporaryDir directory; QVERIFY(directory.isValid());
    QSettings settings; settings.clear(); settings.sync();
    const auto path = directory.filePath("source.vbo"); QVERIFY(writeBytes(path, EventProjectFixture::routeVbo()));
    AppController controller(nullptr, directory.filePath("recovery.json"));
    QVERIFY(controller.importAnalysisRuns("Rapid selections", {QUrl::fromLocalFile(path)}));
    QTRY_COMPARE(controller.vboLoadState(), QString("ready")); QTRY_VERIFY(!controller.outingLapsLoading());
    const auto choices = controller.comparisonLaps(); QVERIFY(choices.size() >= 2);
    const auto a = choices[0].toMap().value("reference").toMap();
    const auto b = choices[1].toMap().value("reference").toMap();
    QSemaphore entered, release;
    std::thread blocker([&] {
        (void)controller.m_analysis.m_analysisSourceCache->load("held-test-decoder", {}, [&](qint64) {
            entered.release(); release.acquire(); return TelemetrySession{};
        }, [](const TelemetrySession &) {});
    });
    const auto unblock = qScopeGuard([&] { release.release(); if (blocker.joinable()) blocker.join(); });
    QVERIFY(entered.tryAcquire(1, 5000));
    QVERIFY(controller.selectComparisonLap(0, a)); QTRY_VERIFY(controller.m_analysis.m_comparisonPending);
    const auto obsolete = controller.m_analysis.m_comparisonCancellation;
    QVERIFY(controller.selectComparisonLap(0, b)); QVERIFY(obsolete->load());
    QVERIFY(controller.selectComparisonLap(0, a)); QVERIFY(controller.selectComparisonLap(0, b));
    release.release(); blocker.join();
    QTRY_COMPARE(controller.m_analysis.m_comparisonSlots[0].state, QString("ready"));
    QCOMPARE(controller.m_analysis.m_comparisonSlots[0].row.value("reference").toMap(), b);
    QCOMPARE(controller.m_analysis.m_comparisonSlots[1].state, QString("empty"));
    QVERIFY(controller.m_analysis.m_comparisonSlots[0].error.isEmpty());
    QVERIFY(controller.m_analysis.m_analysisSourceCache->usedBytes() <= controller.m_analysis.m_analysisSourceCache->limitBytes());
}

void TelemetryTests::excludesChannelMissingFromOneComparisonSlot()
{
    // KAN-42: a comparison pair where only one lap's recording has a given
    // channel (e.g. one logger was wired up for speed, the other was not)
    // must never fabricate or borrow the missing side's values.
    // comparisonAvailableChannels() excludes it, and requesting it directly
    // on the lacking slot reports "channelMissing" rather than empty-but-silent
    // or interpolated data.
    QTemporaryDir directory; QVERIFY(directory.isValid());
    QSettings settings; settings.clear(); settings.sync();
    const auto first = directory.filePath("morning.vbo"), second = directory.filePath("afternoon.vbo");
    QVERIFY(writeBytes(first, withSyntheticSpeedChannel(EventProjectFixture::routeVbo())));
    QVERIFY(writeBytes(second, EventProjectFixture::routeVbo(130, -2, 2))); // no "speed" column
    AppController controller(nullptr, directory.filePath("recovery.json"));
    QVERIFY(controller.importAnalysisRuns("Missing sensor", {QUrl::fromLocalFile(first), QUrl::fromLocalFile(second)}));
    QTRY_COMPARE(controller.vboLoadState(), QString("ready"));
    QTRY_VERIFY(!controller.outingLapsLoading());
    const auto candidates = controller.comparisonLaps(); QVERIFY(candidates.size() >= 2);
    QVariantMap a, b;
    for (const auto &value : candidates) {
        const auto row = value.toMap();
        if (a.isEmpty()) a = row;
        else if (b.isEmpty() && row.value("runId") != a.value("runId")
            && row.value("compatibilityGroupId") == a.value("compatibilityGroupId")) b = row;
    }
    QVERIFY(!a.isEmpty() && !b.isEmpty());
    QVERIFY(controller.selectComparisonLap(0, a.value("reference").toMap()));
    QVERIFY(controller.selectComparisonLap(1, b.value("reference").toMap()));
    QTRY_VERIFY(controller.comparisonPairReady());

    QVERIFY(controller.m_analysis.m_comparisonSlots[0].session->channels.contains("speed"));
    QVERIFY(!controller.m_analysis.m_comparisonSlots[1].session->channels.contains("speed"));
    const auto available = controller.comparisonAvailableChannels();
    QVERIFY(available.contains("latitude"));
    QVERIFY2(!available.contains("speed"), "a channel missing from one recording must not appear as shared");

    const double axisLength = controller.comparisonProgressAxisLength();
    QVERIFY(axisLength > 0.0);
    const auto presentSide = controller.comparisonChannelSeriesByProgress(0, "speed", 0, axisLength, 50);
    QVERIFY2(!presentSide.value("segments").toList().isEmpty(), "the recording that has speed must still serve it");
    const auto missingSide = controller.comparisonChannelSeriesByProgress(1, "speed", 0, axisLength, 50);
    QCOMPARE(missingSide.value("reason").toString(), QString("channelMissing"));
    QVERIFY2(!missingSide.contains("segments"), "a missing channel must report a reason, not fabricated/borrowed segments");
}

void TelemetryTests::comparesKnownDeltaThroughFullComparisonPipeline()
{
    // KAN-42: elevate the known-delta guarantee already proven at the
    // TrackProgress unit level (knownDelayHasCorrectSignAndFinishLineMagnitude)
    // to the full AppController import -> comparison pipeline: two SEPARATE
    // imported runs of the identical physical path, one uniformly 10% slower,
    // must produce a comparison delta with the correct sign throughout and a
    // finish-line magnitude approximating the true known lap-time difference.
    QTemporaryDir directory; QVERIFY(directory.isValid());
    QSettings settings; settings.clear(); settings.sync();
    const auto fastPath = directory.filePath("fast.vbo"), slowPath = directory.filePath("slow.vbo");
    // Default turns (4) is required, not incidental: inferTrack() needs at
    // least 2 complete laps to resolve a route (TelemetryTests::
    // infersRoutesFromOrderedCompleteLaps asserts turns=1 is unsupported),
    // and every other comparison test in this file relies on that default.
    const auto fastBytes = EventProjectFixture::routeVbo(240, -1.0, 0, false, 300);
    constexpr double slowdownFactor = 1.10;
    QVERIFY(writeBytes(fastPath, fastBytes));
    QVERIFY(writeBytes(slowPath, routeVboWithTimeScale(slowdownFactor, fastBytes)));
    AppController controller(nullptr, directory.filePath("recovery.json"));
    QVERIFY(controller.importAnalysisRuns("Known delta", {QUrl::fromLocalFile(fastPath), QUrl::fromLocalFile(slowPath)}));
    QTRY_COMPARE(controller.vboLoadState(), QString("ready"));
    QTRY_VERIFY(!controller.outingLapsLoading());
    const auto runFast = controller.eventRuns()[0].toMap().value("id").toString();
    QVariantMap a, b;
    for (const auto &value : controller.comparisonLaps()) {
        const auto row = value.toMap();
        if (row.value("runId").toString() == runFast) { if (a.isEmpty()) a = row; }
        else if (b.isEmpty()) b = row;
    }
    QVERIFY2(!a.isEmpty() && !b.isEmpty(), "fixture must resolve one lap from each run");
    QCOMPARE(b.value("compatibilityGroupId"), a.value("compatibilityGroupId"));
    const double durationA = a.value("durationSeconds").toDouble();
    const double durationB = b.value("durationSeconds").toDouble();
    QVERIFY2(std::abs(durationB - durationA * slowdownFactor) < 0.05,
        "the slow run's lap must be the known 10% slower duration");

    QVERIFY(controller.selectComparisonLap(0, a.value("reference").toMap()));
    QVERIFY(controller.selectComparisonLap(1, b.value("reference").toMap()));
    QTRY_VERIFY(controller.comparisonPairReady());

    const double axisLength = controller.comparisonProgressAxisLength();
    QVERIFY(axisLength > 0.0);
    const auto deltaSeries = controller.comparisonDeltaSeriesByProgress(0, axisLength, 50);
    QVERIFY2(!deltaSeries.value("segments").toList().isEmpty(), "time delta series should not be empty");
    double lastDelta = 0.0;
    bool sawClearlyAhead = false;
    for (const auto &segmentValue : deltaSeries.value("segments").toList()) {
        for (const auto &pointValue : segmentValue.toList()) {
            const double delta = pointValue.toPointF().y();
            // A (the fixed-pace run) must never read as meaningfully behind
            // the known 10%-slower B.
            QVERIFY(delta < 0.5);
            if (delta < -0.2) sawClearlyAhead = true;
            lastDelta = delta;
        }
    }
    QVERIFY2(sawClearlyAhead, "A should read as clearly ahead of the known-slower B somewhere along the lap");
    const double actualDurationDifference = durationA - durationB; // A - B, negative since B is slower
    QVERIFY2(std::abs(lastDelta - actualDurationDifference) < 1.0,
        "the delta near the finish line should approximate the true known lap-time difference");
}

void TelemetryTests::keepsComparisonAndOutingLapVideoIndependent()
{
    // KAN-42: "exercise two-run comparison with optional video" (M2 editor
    // independence). This app has exactly one central video slot
    // (m_videoSource/m_sync/m_exportSourceInfo, gated to the currently open
    // outing lap's run per KAN-39) and, separately, the two comparison slots
    // populated by selectComparisonLap. Neither production surface reads the
    // other's state (ComparisonDetailPanel.qml/AppControllerComparison.cpp
    // have no video code at all today) -- prove that opening/advancing video
    // on the outing-lap side, and selecting/clearing A/B on the comparison
    // side, cannot disturb each other while both are open at once. Dual,
    // side-by-side comparison video remains separate backlog scope (KAN-104
    // through KAN-107 per the delivery ledger), not implemented here.
    QTemporaryDir directory; QVERIFY(directory.isValid());
    QSettings settings; settings.clear(); settings.sync();
    const auto first = directory.filePath("first.vbo"), second = directory.filePath("second.vbo");
    QVERIFY(writeBytes(first, EventProjectFixture::routeVbo()));
    QVERIFY(writeBytes(second, EventProjectFixture::routeVbo(130, -2, 2)));
    AppController controller(nullptr, directory.filePath("recovery.json"));
    QVERIFY(controller.importAnalysisRuns("Comparison and video", {QUrl::fromLocalFile(first), QUrl::fromLocalFile(second)}));
    QTRY_COMPARE(controller.vboLoadState(), QString("ready"));
    QTRY_VERIFY(!controller.outingLapsLoading());
    const auto active = controller.activeRunId();
    QVariantMap sameRunLap, a, b;
    for (const auto &value : controller.outingLaps()) {
        const auto row = value.toMap();
        if (row.value("type") == "LAP" && row.value("runId").toString() == active) { sameRunLap = row; break; }
    }
    for (const auto &value : controller.comparisonLaps()) {
        const auto row = value.toMap();
        if (a.isEmpty()) a = row;
        else if (b.isEmpty() && row.value("runId") != a.value("runId")
            && row.value("compatibilityGroupId") == a.value("compatibilityGroupId")) b = row;
    }
    QVERIFY(!sameRunLap.isEmpty() && !a.isEmpty() && !b.isEmpty());

    // Populate the A/B comparison pair first.
    QVERIFY(controller.selectComparisonLap(0, a.value("reference").toMap()));
    QVERIFY(controller.selectComparisonLap(1, b.value("reference").toMap()));
    QTRY_VERIFY(controller.comparisonPairReady());
    const auto sessionA = controller.m_analysis.m_comparisonSlots[0].session;
    const auto sessionB = controller.m_analysis.m_comparisonSlots[1].session;
    const auto axisLength = controller.comparisonProgressAxisLength();
    const auto deltaBefore = controller.comparisonDeltaSeriesByProgress(0, axisLength, 50);

    // Open the single-lap outing detail on the active run and attach a
    // synthetic video, exactly as KAN-39's linksOutingLapVideoToActiveRunOnly does.
    QVERIFY(controller.selectOutingLapReference(sameRunLap.value("reference").toMap()));
    QTRY_COMPARE(controller.outingLapDetailState(), QString("ready"));
    const auto start = sameRunLap.value("startTime").toDouble(), end = sameRunLap.value("endTime").toDouble();
    const auto midpoint = (start + end) / 2.0;
    controller.m_videoSource = QUrl::fromLocalFile(QStringLiteral("/synthetic/video.mp4"));
    MediaInfo info;
    info.frameRate = {30, 1};
    info.averageFrameRate = {30, 1};
    info.videoFrameCount = qRound64((end + 5.0) * 30.0);
    info.timeBase = {1, 30};
    info.videoDurationTicks = info.videoFrameCount;
    controller.m_exportSourceInfo = info;
    controller.m_sync = {0.0, 1.0};
    controller.setOutingLapCursor(midpoint);
    QVERIFY(controller.outingLapVideoAvailable());
    QCOMPARE(controller.outingLapVideoPositionMilliseconds(), qRound64(midpoint * 1000.0));

    // The comparison pair must not have moved at all while video state changed.
    QCOMPARE(controller.m_analysis.m_comparisonSlots[0].session, sessionA);
    QCOMPARE(controller.m_analysis.m_comparisonSlots[1].session, sessionB);
    QVERIFY(controller.comparisonPairReady());
    QCOMPARE(controller.comparisonDeltaSeriesByProgress(0, axisLength, 50), deltaBefore);

    // Advancing the video-driven cursor further must still not touch the
    // comparison slots.
    QVERIFY(controller.followOutingLapVideoPosition(qRound64((midpoint + 1.0) * 1000.0)));
    QCOMPARE(controller.m_analysis.m_comparisonSlots[0].session, sessionA);
    QCOMPARE(controller.m_analysis.m_comparisonSlots[1].session, sessionB);

    // Conversely, clearing/reselecting a comparison slot must not disturb the
    // still-open outing lap's video availability or cursor.
    const auto cursorBeforeClear = controller.outingLapCursor();
    controller.clearComparisonLap(1);
    QVERIFY(controller.outingLapVideoAvailable());
    QCOMPARE(controller.outingLapCursor(), cursorBeforeClear);
    QVERIFY(controller.selectComparisonLap(1, b.value("reference").toMap()));
    QTRY_VERIFY(controller.comparisonPairReady());
    QVERIFY(controller.outingLapVideoAvailable());
    QCOMPARE(controller.outingLapCursor(), cursorBeforeClear);

    // Closing the outing lap detail (and its video) leaves the comparison
    // pair fully intact.
    controller.closeOutingLap();
    QCOMPARE(controller.m_analysis.m_comparisonSlots[0].session, sessionA);
    QCOMPARE(controller.m_analysis.m_comparisonSlots[1].session, sessionB);
    QVERIFY(controller.comparisonPairReady());
}

void TelemetryTests::presentsDayResultStatesWithoutVideo()
{
    QTemporaryDir directory; QVERIFY(directory.isValid());
    QSettings settings; settings.clear(); settings.sync();
    const auto first = directory.filePath("morning.vbo"), second = directory.filePath("afternoon.vbo");
    const auto firstBytes = EventProjectFixture::routeVbo();
    const auto secondBytes = EventProjectFixture::routeVbo(130, -2, 2);
    QVERIFY(writeBytes(first, firstBytes)); QVERIFY(writeBytes(second, secondBytes));
    AppController controller(nullptr, directory.filePath("recovery.json"));
    QTRY_COMPARE(controller.outingAnalysisStatus().value("state").toString(), QString("empty"));
    QVERIFY(!controller.retryOutingAnalysis());
    QVERIFY(controller.importAnalysisRuns("Video-free results", {QUrl::fromLocalFile(first), QUrl::fromLocalFile(second)}));
    QTRY_COMPARE(controller.vboLoadState(), QString("ready"));
    QTRY_COMPARE(controller.outingRanking().value("state").toString(), QString("available"));
    QCOMPARE(controller.outingAnalysisStatus().value("state").toString(), QString("ready"));
    QCOMPARE(controller.outingAnalysisStatus().value("readyRunCount").toInt(), 2);
    QCOMPARE(controller.outingProgression().value("runs").toList().size(), 2);
    const auto runA = controller.eventRuns()[0].toMap().value("id").toString();
    const auto runB = controller.eventRuns()[1].toMap().value("id").toString();
    const auto runStatus = [&controller](const QString &id) {
        for (const auto &value : controller.outingAnalysisStatus().value("runs").toList())
            if (value.toMap().value("runId") == id) return value.toMap();
        return QVariantMap{};
    };
    controller.setSyncOffset(19); controller.setTimeScale(1.3); controller.setPlaybackTime(7);
    const auto saved = directory.filePath("day.fetproject"); QVERIFY(controller.saveProject(QUrl::fromLocalFile(saved)));
    QTRY_VERIFY(!controller.outingLapsLoading());
    const auto before = controller.currentProjectObject();
    const auto revision = controller.m_document.m_documentState.revision();
    const auto active = controller.activeRunId();

    QQmlEngine engine; engine.rootContext()->setContextProperty("appController", &controller);
    QSignalSpy warnings(&engine, &QQmlEngine::warnings); QQmlComponent component(&engine);
    component.setData("import QtQuick\nWindow { width: 760; height: 480; OutingLapPanel { anchors.fill: parent } }",
        QUrl::fromLocalFile(QStringLiteral(ANALYSIS_PANEL_QML_PATH)));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    std::unique_ptr<QObject> object(component.create()); QVERIFY2(object, qPrintable(component.errorString()));
    auto *window = qobject_cast<QQuickWindow *>(object.get()); QVERIFY(window);
    window->show(); QVERIFY(QTest::qWaitForWindowExposed(window));
    auto *status = window->findChild<QObject *>("outingAnalysisStatus");
    auto *retry = window->findChild<QQuickItem *>("retryOutingAnalysis");
    auto *best = window->findChild<QQuickItem *>("openBestDayLap");
    auto *lapList = window->findChild<QQuickItem *>("outingLapList");
    QVERIFY(status && retry && best && lapList);
    QVERIFY(!retry->isVisible()); QVERIFY(best->isEnabled());

    // Inspect the other run through the actual results control. This must not
    // select its editor source or alter the editor synchronization.
    best->forceActiveFocus(); QTest::keyClick(window, Qt::Key_Space);
    QTRY_COMPARE(controller.outingLapDetailState(), QString("ready"));
    QCOMPARE(controller.activeRunId(), active);
    QCOMPARE(controller.syncOffset(), 19.0); QCOMPARE(controller.timeScale(), 1.3);
    controller.closeOutingLap();
    for (const auto &value : controller.outingLaps()) {
        const auto row = value.toMap();
        if (row.value("runId") == runA && row.value("type") == "LAP") {
            QVERIFY(controller.selectOutingLapReference(row.value("reference").toMap())); break;
        }
    }
    QTRY_COMPARE(controller.outingLapDetailState(), QString("ready"));
    const auto detail = controller.m_analysis.m_outingLapDetailSession;
    const auto track = controller.outingLapTrack();
    const auto cursor = controller.outingLapCursor();

    QVERIFY(QFile::remove(second));
    QVERIFY(controller.retryOutingAnalysis());
    QCOMPARE(controller.outingAnalysisStatus().value("state").toString(), QString("loading"));
    QVERIFY(!controller.retryOutingAnalysis()); // One bounded worker, even under repeated clicks.
    QTRY_VERIFY(!controller.outingLapsLoading());
    QCOMPARE(controller.outingAnalysisStatus().value("state").toString(), QString("ready"));
    QVERIFY(controller.outingAnalysisStatus().value("partial").toBool());
    QCOMPARE(runStatus(runB).value("state").toString(), QString("missing-source"));
    QCOMPARE(runStatus(runA).value("state").toString(), QString("ready"));
    QCOMPARE(controller.outingRanking().value("runs").toList().size(), 1);
    QCOMPARE(controller.m_analysis.m_outingLapDetailSession, detail);
    QCOMPARE(controller.outingLapTrack(), track); QCOMPARE(controller.outingLapCursor(), cursor);
    QTRY_VERIFY(retry->isVisible() && retry->isEnabled());
    auto *runStatuses = window->findChild<QObject *>("outingRunStatuses"); QVERIFY(runStatuses);
    QTRY_COMPARE(runStatuses->property("count").toInt(), 1);
    QQuickItem *missing = nullptr;
    QTRY_VERIFY(QMetaObject::invokeMethod(runStatuses, "itemAt", Q_RETURN_ARG(QQuickItem *, missing), Q_ARG(int, 0)) && missing);
    QCOMPARE(missing->objectName(), "outingRunStatus_" + runB);
    QVERIFY(missing->isVisible());
    QVERIFY(missing->property("text").toString().contains(controller.runMetadata(runB).value("name").toString()));
    QVERIFY(missing->property("text").toString().contains("missing"));
    QTRY_VERIFY(lapList->height() > 30);
    QVERIFY(retry->mapRectToScene(retry->boundingRect()).bottom() <= window->height());

    // Restoring the exact file recovers from the production retry control.
    QVERIFY(writeBytes(second, secondBytes));
    retry->forceActiveFocus(); QTest::keyClick(window, Qt::Key_Space);
    QTRY_COMPARE(controller.outingAnalysisStatus().value("readyRunCount").toInt(), 2);
    QTRY_VERIFY(!retry->isVisible());
    QVERIFY(!controller.outingAnalysisStatus().value("partial").toBool());
    QCOMPARE(controller.m_analysis.m_outingLapDetailSession, detail);

    // A different file at the same path is an identity error, never an empty
    // result or a silently accepted replacement. The other run stays usable.
    QVERIFY(writeBytes(second, "not the original recording\n"));
    QVERIFY(controller.retryOutingAnalysis()); QTRY_VERIFY(!controller.outingLapsLoading());
    QCOMPARE(runStatus(runB).value("state").toString(), QString("error"));
    QVERIFY(runStatus(runB).value("message").toString().contains("identity"));
    QCOMPARE(controller.outingAnalysisStatus().value("state").toString(), QString("ready"));
    QVERIFY(QFile::remove(first));
    QVERIFY(controller.retryOutingAnalysis()); QTRY_VERIFY(!controller.outingLapsLoading());
    QCOMPARE(controller.outingAnalysisStatus().value("state").toString(), QString("error"));
    QVERIFY(!best->isEnabled());
    QVERIFY(!status->property("text").toString().contains("No recorded sections"));
    QVERIFY(QFile::remove(second));
    QVERIFY(controller.retryOutingAnalysis()); QTRY_VERIFY(!controller.outingLapsLoading());
    QCOMPARE(controller.outingAnalysisStatus().value("state").toString(), QString("missing-source"));
    QCOMPARE(controller.outingAnalysisStatus().value("missingRunCount").toInt(), 2);
    QVERIFY(controller.outingLaps().isEmpty());

    QVERIFY(writeBytes(first, firstBytes)); QVERIFY(writeBytes(second, secondBytes));
    QVERIFY(controller.retryOutingAnalysis()); QTRY_VERIFY(!controller.outingLapsLoading());
    QCOMPARE(controller.outingAnalysisStatus().value("state").toString(), QString("ready"));
    QCOMPARE(controller.currentProjectObject(), before);
    QCOMPARE(controller.m_document.m_documentState.revision(), revision); QVERIFY(!controller.dirty());
    QCOMPARE(controller.activeRunId(), active);
    QCOMPARE(controller.syncOffset(), 19.0); QCOMPARE(controller.timeScale(), 1.3);

    // A superseded worker must publish neither obsolete errors nor run names.
    AnalysisController::OutingLapResult stale;
    stale.key = controller.m_analysis.outingLapKey(); stale.generation = controller.m_document.m_sourceGeneration;
    stale.messages.append({runB, "Obsolete source error", "error"});
    QPromise<AnalysisController::OutingLapResult> promise; promise.start();
    controller.m_analysis.m_outingLapWatcher.setFuture(promise.future());
    controller.requestNewProject();
    promise.addResult(stale); promise.finish();
    QTRY_VERIFY(!controller.m_analysis.m_outingLapWatcher.isRunning());
    QTRY_COMPARE(controller.outingAnalysisStatus().value("state").toString(), QString("empty"));
    QVERIFY(controller.outingAnalysisStatus().value("runs").toList().isEmpty());
    QVERIFY(controller.outingAnalysisStatus().value("notices").toStringList().isEmpty());
    QCOMPARE(warnings.size(), 0);
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
    GForceWidget { frame: frameData }
    F1GForceRadarWidget { frame: frameData }
}
)QML", QUrl::fromLocalFile(QStringLiteral(BATCH_IMPORT_QML_PATH)));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    std::unique_ptr<QObject> root(component.create());
    QVERIFY2(root, qPrintable(component.errorString()));
    const auto dots = root->findChildren<QQuickItem *>(QStringLiteral("gForceDot"));
    QCOMPARE(dots.size(), 2);
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

void TelemetryTests::displaysTimedLapsWithoutVideo()
{
    QTemporaryDir directory; QVERIFY(directory.isValid());
    QSettings settings; settings.clear(); settings.sync();
    AppController controller(nullptr, directory.filePath("recovery.json"));
    QQmlEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("appController"), &controller);
    QSignalSpy warnings(&engine, &QQmlEngine::warnings);
    const auto panelPath = QFileInfo(QStringLiteral(BATCH_IMPORT_QML_PATH)).dir().filePath("LapTimingPanel.qml");
    QQmlComponent component(&engine, QUrl::fromLocalFile(panelPath));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    std::unique_ptr<QObject> panel(component.createWithInitialProperties({{"width", 760}, {"height", 158}}));
    QVERIFY2(panel, qPrintable(component.errorString()));
    auto *list = panel->findChild<QObject *>(QStringLiteral("timedLapList"));
    QVERIFY(list);
    QCOMPARE(list->property("count").toInt(), 0);
    controller.loadVbo(QUrl::fromLocalFile(QFileInfo(QStringLiteral(TEST_FIXTURE_PATH)).dir().filePath("event-laps.vbo")));
    QTRY_COMPARE(controller.vboLoadState(), QStringLiteral("ready"));
    QCOMPARE(controller.lapSummaries().size(), 3);
    QVERIFY(controller.lapNavigationSegments().isEmpty());
    QTRY_COMPARE(list->property("count").toInt(), 3);
    QVERIFY(list->property("visible").toBool());
    controller.loadVbo(QUrl::fromLocalFile(QStringLiteral(TEST_FIXTURE_PATH)));
    QTRY_COMPARE(controller.vboLoadState(), QStringLiteral("ready"));
    QTRY_COMPARE(list->property("count").toInt(), 0);
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
    QVERIFY(controller.beginBatchImport({QUrl::fromLocalFile(link)}));
    QTRY_COMPARE(controller.batchImportState(), QStringLiteral("review"));
    QCOMPARE(controller.batchImportRows()[0].toMap().value("status").toString(), QStringLiteral("error"));
    QVERIFY(independentBatchChoices(controller.batchImportRows()).isEmpty());
    QVERIFY(!controller.confirmBatchImport("Day", false, {}));
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
        QFileInfo(QStringLiteral(ANALYSIS_PANEL_QML_PATH)).dir().filePath("Main.qml")));
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
        QFileInfo(QStringLiteral(ANALYSIS_PANEL_QML_PATH)).dir().filePath("Main.qml")));
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
    captureUserGuide(options);
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
    QVERIFY(controller.beginBatchImport({QUrl::fromLocalFile(first), QUrl::fromLocalFile(second), QUrl::fromLocalFile(alternative)}));
    QTRY_COMPARE_WITH_TIMEOUT(controller.batchImportState(), QString("review"), 20000);
    QHash<QString, QString> proposals;
    for (const auto &value : controller.batchImportRows())
        proposals.insert(QFileInfo(value.toMap().value("path").toString()).fileName(), value.toMap().value("proposalId").toString());
    QCOMPARE(proposals.size(), 3);
    QSignalSpy committed(&controller, &AppController::batchImportCommitted);
    QVERIFY(controller.confirmBatchImport("Protected day", false, {
        QVariantMap{{"proposalId", proposals.value("first.vbo")}, {"groupId", proposals.value("first.vbo")}},
        QVariantMap{{"proposalId", proposals.value("second.vbo")}, {"groupId", proposals.value("second.vbo")}},
        QVariantMap{{"proposalId", proposals.value("second-alternative.vbo")}, {"groupId", proposals.value("second.vbo")}}}));
    QTRY_COMPARE_WITH_TIMEOUT(committed.size(), 1, 20000);
    QCOMPARE(controller.eventRuns().size(), 2);
    QTRY_COMPARE_WITH_TIMEOUT(controller.vboLoadState(), QString("ready"), 20000);
    controller.loadVideo(QUrl::fromLocalFile(video));
    QTRY_COMPARE_WITH_TIMEOUT(controller.videoLoadState(), QString("ready"), 20000);

    // The day-analysis state a real day carries: approved segments and an exclusion.
    QTRY_VERIFY_WITH_TIMEOUT(!controller.outingLapsLoading() && !controller.outingComparisonGroupId().isEmpty(), 20000);
    QVERIFY(!approveAllSegmentsOnRun(controller, "Session 1").isEmpty());
    QVariantMap excluded;
    for (const auto &value : controller.outingLaps())
        if (value.toMap().value("type") == "LAP" && value.toMap().value("runName") == "Session 2") { excluded = value.toMap().value("reference").toMap(); break; }
    QVERIFY(controller.setOutingLapExcluded(excluded, true, "Traffic"));
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
    for (const QString &name : {"first.vbo", "second-alternative.vbo", "day.fetproject"}) {
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

void TelemetryTests::keepsAnalysisControlsReachableAtMinimumSize_data()
{
    QTest::addColumn<QSize>("size");
    QTest::newRow("editor minimum 1180x720") << QSize(1180, 720);
    QTest::newRow("analysis window minimum 760x480") << QSize(760, 480);
}

void TelemetryTests::keepsAnalysisControlsReachableAtMinimumSize()
{
    // KAN-78: at the supported minimum sizes every control of the day-analysis
    // workflow -- the lap list, an open lap with segment editing, the
    // comparison, theoretical best, time losses, the progression tabs and the
    // day report -- is on screen or reachable by scrolling, no dialog is larger
    // than the window, and Escape while typing never closes the view being
    // edited. FLAPPEDEAR_LAYOUT_REVIEW_DIR, when set, receives screenshots.
    QFETCH(QSize, size);
    QTemporaryDir directory; QVERIFY(directory.isValid());
    QSettings settings; settings.clear(); settings.sync();
    const auto fullA = directory.filePath("full-a.vbo"), fullB = directory.filePath("full-b.vbo");
    QVERIFY(writeBytes(fullA, fullM4Vbo(true, 140, 80, 1.0)));
    QVERIFY(writeBytes(fullB, fullM4Vbo(false, 150, 85, 0.9)));
    AppController controller(nullptr, directory.filePath("recovery.json"));
    QVERIFY(controller.importAnalysisRuns("Layout day", {QUrl::fromLocalFile(fullA), QUrl::fromLocalFile(fullB)}));
    QTRY_VERIFY_WITH_TIMEOUT(controller.eventRuns().size() == 2 && !controller.outingLapsLoading(), 30000);
    QTRY_VERIFY(!controller.outingComparisonGroupId().isEmpty());
    QVERIFY(!approveAllSegmentsOnRun(controller, "Session 1").isEmpty());
    controller.requestOutingDayReport();
    const auto settled = [&] {
        for (const auto &value : controller.outingDayReport().value("results").toList()) {
            const auto status = value.toMap().value("status").toString();
            if (status == "notComputed" || status == "computing" || status == "stale") return false;
        }
        return true;
    };
    QTRY_VERIFY_WITH_TIMEOUT(settled(), 60000);

    QQmlEngine engine; engine.rootContext()->setContextProperty("appController", &controller);
    QQmlComponent component(&engine);
    component.loadUrl(QUrl::fromLocalFile(QFileInfo(QStringLiteral(ANALYSIS_PANEL_QML_PATH)).dir().filePath("AnalysisWindow.qml")));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    std::unique_ptr<QObject> object(component.createWithInitialProperties({{"videoSource", QUrl{}},
        {"playbackPosition", 0}, {"playbackRunning", false}, {"mediaDuration", 0}}));
    QVERIFY2(object, qPrintable(component.errorString()));
    auto *window = qobject_cast<QQuickWindow *>(object.get()); QVERIFY(window);
    window->resize(size);
    controller.setAnalysisVisible(true);
    window->show(); QVERIFY(QTest::qWaitForWindowExposed(window));
    // A display smaller than the minimum (a CI runner) cannot show it; the
    // check needs the real size, so it is skipped there rather than faked.
    if (!QTest::qWaitFor([&] { return window->size() == size; }, 3000))
        QSKIP(qPrintable(QStringLiteral("This display cannot show a %1x%2 window (got %3x%4).")
            .arg(size.width()).arg(size.height()).arg(window->width()).arg(window->height())));
    const QString reviewDirectory = qEnvironmentVariable("FLAPPEDEAR_LAYOUT_REVIEW_DIR");
    QStringList failures;
    const auto check = [&](const QString &state) {
        QTest::qWait(400);
        // A grab polishes and renders the scene first: a tab bar's list view
        // positions its delegates only when polished, and item geometry
        // read between polishes can be stale.
        static_cast<void>(window->grabWindow());
        const auto problems = unreachableControls(window);
        for (const auto &problem : problems) failures << state + ": " + problem;
        if (!reviewDirectory.isEmpty())
            static_cast<void>(window->grabWindow().save(QDir(reviewDirectory).filePath(
                QStringLiteral("%1x%2-%3.png").arg(size.width()).arg(size.height()).arg(state))));
    };
    check("lap-list");

    // An open lap with segment review and an approved segment in edit mode.
    int lapIndex = -1;
    const auto rows = controller.outingLaps();
    for (int i = 0; i < rows.size() && lapIndex < 0; ++i)
        if (rows[i].toMap().value("type") == "LAP" && rows[i].toMap().value("runName") == "Session 1") lapIndex = i;
    QVERIFY(controller.selectOutingLap(lapIndex));
    QTRY_COMPARE_WITH_TIMEOUT(controller.outingLapDetailState(), QString("ready"), 20000);
    check("lap");
    auto *review = window->findChild<QQuickItem *>("toggleSegmentReview");
    QVERIFY(review);
    review->forceActiveFocus(); QTest::keyClick(window, Qt::Key_Space);
    QTRY_COMPARE_WITH_TIMEOUT(controller.segmentReviewState(), QString("ready"), 20000);
    check("lap-segments");
    // Escape while typing a reason keeps the lap open (the text field has it).
    auto *reason = window->findChild<QQuickItem *>("lapExclusionReason");
    QVERIFY(reason);
    reason->forceActiveFocus();
    for (const char c : QByteArray("traffic")) QTest::keyClick(window, c);
    QTest::keyClick(window, Qt::Key_Escape);
    QVERIFY2(!controller.selectedOutingLap().isEmpty(), "Escape in the exclusion reason closed the lap");
    // Repeater delegates are only reachable through the visual tree.
    const auto findVisual = [&](const QString &objectName) {
        QQuickItem *found = nullptr;
        std::function<void(QQuickItem *)> search = [&](QQuickItem *item) {
            if (found || !item->isVisible()) return;
            if (item->objectName() == objectName) { found = item; return; }
            for (auto *child : item->childItems()) search(child);
        };
        search(window->contentItem());
        return found;
    };
    {
        auto *edit = findVisual("editApprovedSegment");
        QVERIFY2(edit, "no approved segment to edit");
        edit->forceActiveFocus(); QTest::keyClick(window, Qt::Key_Space);
        check("lap-segment-edit");
        QQuickItem *name = nullptr;
        std::function<void(QQuickItem *)> find = [&](QQuickItem *item) {
            if (name || !item->isVisible()) return;
            if (item->property("placeholderText").toString() == "Name" && item->inherits("QQuickTextInput")) { name = item; return; }
            for (auto *child : item->childItems()) find(child);
        };
        find(window->contentItem());
        QVERIFY(name);
        name->forceActiveFocus();
        for (const char c : QByteArray("Hairpin")) QTest::keyClick(window, c);
        QTest::keyClick(window, Qt::Key_Escape);
        QVERIFY2(!controller.selectedOutingLap().isEmpty(), "Escape in a segment name closed the lap");
    }
    controller.closeOutingLap();

    // The comparison with the Corner Analyzer on the largest loss.
    const auto losses = [&] {
        for (const auto &value : controller.outingDayReport().value("results").toList())
            if (value.toMap().value("id") == "timeLosses") return value.toMap().value("evidence").toList();
        return QVariantList{};
    }();
    QVERIFY(!losses.isEmpty());
    QVERIFY(controller.openTimeLoss({{"segmentId", losses.first().toMap().value("segmentId")},
                                     {"lapReference", losses.first().toMap().value("reference")}}));
    QTRY_VERIFY_WITH_TIMEOUT(controller.comparisonPairReady(), 20000);
    check("comparison");
    controller.setComparisonViewOpen(false);

    for (const auto &[name, state] : {std::pair{"theoreticalBestDialog", "theoretical-best"},
                                      std::pair{"timeLossDialog", "time-losses"},
                                      std::pair{"dayReportDialog", "day-report"}}) {
        auto *dialog = window->findChild<QObject *>(name);
        QVERIFY2(dialog, name);
        QVERIFY(QMetaObject::invokeMethod(dialog, "open"));
        QTRY_VERIFY(dialog->property("opened").toBool());
        check(state);
        QVERIFY(QMetaObject::invokeMethod(dialog, "close"));
        QTRY_VERIFY(!dialog->property("visible").toBool());
    }
    auto *progression = window->findChild<QObject *>("outingProgressionDialog");
    auto *tabs = window->findChild<QObject *>("progressionTabs");
    QVERIFY(progression && tabs);
    QVERIFY(QMetaObject::invokeMethod(progression, "open"));
    QTRY_VERIFY(progression->property("opened").toBool());
    for (int tab = 0; tab < 3; ++tab) {
        tabs->setProperty("currentIndex", tab);
        check(QStringLiteral("progression-%1").arg(tab));
    }
    QVERIFY(QMetaObject::invokeMethod(progression, "close"));
    QVERIFY2(failures.isEmpty(), qPrintable(failures.join('\n')));
}

void TelemetryTests::keepsACompleteDayThroughMoveRelinkAndRecovery()
{
    // KAN-82: one analysed day -- run notes, approved segments, an A/B pair
    // with its range, and the computed report -- through Save, a move of the
    // project with its recordings, a missing and relinked recording, a
    // refused wrong relink, crash recovery and discard. The saved project
    // stays the authoritative clean state throughout.
    QTemporaryDir directory; QVERIFY(directory.isValid());
    QSettings settings; settings.clear(); settings.sync();
    const QString day = directory.filePath("TrackDay");
    QVERIFY(QDir().mkpath(QDir(day).filePath("media")));
    const auto fullA = QDir(day).filePath("media/full-a.vbo"), fullB = QDir(day).filePath("media/full-b.vbo");
    QVERIFY(writeBytes(fullA, fullM4Vbo(true, 140, 80, 1.0)));
    QVERIFY(writeBytes(fullB, fullM4Vbo(false, 150, 85, 0.9)));
    const QString recovery = directory.filePath("recovery.json");
    const auto result = [](const QVariantMap &report, const QString &id) {
        for (const auto &value : report.value("results").toList())
            if (value.toMap().value("id") == id) return value.toMap();
        return QVariantMap{};
    };
    const auto settle = [&](AppController &controller) {
        controller.requestOutingDayReport();
        return QTest::qWaitFor([&] {
            const auto results = controller.outingDayReport().value("results").toList();
            if (results.isEmpty()) return false;
            for (const auto &value : results) {
                const auto status = value.toMap().value("status").toString();
                if (status == "notComputed" || status == "computing" || status == "stale") return false;
            }
            return true;
        }, 60000);
    };
    // What must survive: notes, segmentation, the pair and its range, the report.
    struct Snapshot {
        QString notes, decisions, bestLabel; double theoretical = 0; int sectors = 0;
        QVariantMap lapA, lapB; double rangeStart = 0, rangeEnd = 0;
        bool operator==(const Snapshot &other) const {
            return notes == other.notes && decisions == other.decisions && bestLabel == other.bestLabel
                && qFuzzyCompare(theoretical, other.theoretical) && sectors == other.sectors
                && lapA == other.lapA && lapB == other.lapB
                && qFuzzyCompare(rangeStart + 1, other.rangeStart + 1) && qFuzzyCompare(rangeEnd, other.rangeEnd);
        }
    };
    const auto snapshot = [&](AppController &controller, const QString &runId) {
        Snapshot s;
        s.notes = controller.runMetadata(runId).value("notes").toString();
        const auto report = controller.outingDayReport();
        s.decisions = report.value("decisionsKey").toString();
        s.bestLabel = result(report, "bestLap").value("value").toMap().value("label").toString();
        s.theoretical = result(report, "theoreticalBest").value("value").toMap().value("totalSeconds").toDouble();
        s.sectors = result(report, "theoreticalBest").value("value").toMap().value("sectors").toList().size();
        s.lapA = controller.comparisonSlots()[0].toMap().value("lap").toMap().value("reference").toMap();
        s.lapB = controller.comparisonSlots()[1].toMap().value("lap").toMap().value("reference").toMap();
        s.rangeStart = controller.comparisonPersistedRangeMeters().value("startMeters").toDouble();
        s.rangeEnd = controller.comparisonPersistedRangeMeters().value("endMeters").toDouble();
        return s;
    };
    const auto describe = [](const Snapshot &s) {
        return QString("notes=%1 decisions=%2 best=%3 theoretical=%4 sectors=%5 A=%6 B=%7 range=%8-%9")
            .arg(s.notes, s.decisions.left(12), s.bestLabel).arg(s.theoretical).arg(s.sectors)
            .arg(s.lapA.value("lapNumber").toInt()).arg(s.lapB.value("lapNumber").toInt()).arg(s.rangeStart).arg(s.rangeEnd);
    };
    const auto openDay = [&](AppController &controller, const QString &project) {
        controller.requestOpenProject(QUrl::fromLocalFile(project));
        return QTest::qWaitFor([&] { return controller.eventRuns().size() == 2 && !controller.projectLoading()
            && !controller.outingLapsLoading(); }, 30000);
    };

    // 1. Analyse and save.
    const QString project = QDir(day).filePath("day.fetproject");
    QString runB;
    Snapshot saved;
    {
        AppController controller(nullptr, recovery);
        QVERIFY(controller.importAnalysisRuns("Complete day", {QUrl::fromLocalFile(fullA), QUrl::fromLocalFile(fullB)}));
        QTRY_VERIFY_WITH_TIMEOUT(controller.eventRuns().size() == 2 && !controller.outingLapsLoading(), 30000);
        QTRY_VERIFY(!controller.outingComparisonGroupId().isEmpty());
        QVERIFY(!approveAllSegmentsOnRun(controller, "Session 1").isEmpty());
        runB = controller.eventRuns()[1].toMap().value("id").toString();
        const auto metadata = controller.runMetadata(runB);
        QVERIFY(controller.updateRunMetadata(runB, metadata.value("editToken").toString(), metadata.value("name").toString(),
            "Soft tyres, 1.9 bar hot", "Dry, 24 °C", "Front bar +1"));
        QVERIFY(settle(controller));
        const auto losses = result(controller.outingDayReport(), "timeLosses").value("evidence").toList();
        QVERIFY(!losses.isEmpty());
        QVERIFY(controller.openTimeLoss({{"segmentId", losses.first().toMap().value("segmentId")},
                                         {"lapReference", losses.first().toMap().value("reference")}}));
        QTRY_VERIFY_WITH_TIMEOUT(controller.comparisonPairReady(), 20000);
        controller.persistComparisonRange(100.0, 600.0);
        QVERIFY(controller.saveProject(QUrl::fromLocalFile(project)));
        QVERIFY(!controller.dirty());
        // Save As keys the day to its new path: let the results settle again.
        QTRY_VERIFY_WITH_TIMEOUT(!controller.outingLapsLoading(), 30000);
        QTRY_VERIFY_WITH_TIMEOUT(controller.comparisonPairReady(), 20000);
        QVERIFY(settle(controller));
        QVERIFY(!controller.dirty());
        saved = snapshot(controller, runB);
        QVERIFY2(!saved.lapA.isEmpty() && !saved.lapB.isEmpty() && saved.sectors > 0, qPrintable(describe(saved)));
    }

    // 2. Move the project with its recordings; reopen: clean and identical.
    const QString archive = directory.filePath("Archive");
    QVERIFY(QDir().mkpath(archive));
    QVERIFY(QDir().rename(day, QDir(archive).filePath("TrackDay")));
    const QString movedDay = QDir(archive).filePath("TrackDay"), movedProject = QDir(movedDay).filePath("day.fetproject");
    {
        AppController controller(nullptr, recovery);
        QVERIFY(openDay(controller, movedProject));
        QTRY_VERIFY_WITH_TIMEOUT(controller.comparisonPairReady(), 20000);
        QVERIFY(settle(controller));
        QVERIFY(!controller.dirty());
        const auto reopened = snapshot(controller, runB);
        QVERIFY2(reopened == saved, qPrintable(describe(saved) + " vs " + describe(reopened)));
    }

    // 3. A recording goes missing; the other run stays usable. A wrong file is
    // refused by identity; the right one relinks and the day is whole again.
    const QString activeRecording = QDir(movedDay).filePath("media/full-a.vbo");
    const QString renamed = QDir(movedDay).filePath("media/renamed.vbo");
    QVERIFY(QFile::rename(activeRecording, renamed));
    {
        AppController controller(nullptr, recovery);
        controller.requestOpenProject(QUrl::fromLocalFile(movedProject));
        QTRY_COMPARE_WITH_TIMEOUT(controller.vboLoadState(), QString("missing"), 30000);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.outingLapsLoading(), 30000);
        QVariantMap missingRun;
        for (const auto &value : controller.outingAnalysisStatus().value("runs").toList())
            if (value.toMap().value("state") != "ready") missingRun = value.toMap();
        QCOMPARE(missingRun.value("state").toString(), QString("missing-source"));
        QVERIFY(!controller.outingLaps().isEmpty()); // the other session remains inspectable
        controller.relinkVbo(QUrl::fromLocalFile(QDir(movedDay).filePath("media/full-b.vbo")));
        QTRY_COMPARE_WITH_TIMEOUT(controller.sourceMismatchType(), QString("telemetry"), 30000);
        controller.resolveSourceMismatch(false);
        QVERIFY(controller.vboLoadState() != "ready");
        controller.relinkVbo(QUrl::fromLocalFile(renamed));
        QTRY_COMPARE_WITH_TIMEOUT(controller.vboLoadState(), QString("ready"), 30000);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.outingLapsLoading(), 30000);
        QTRY_VERIFY_WITH_TIMEOUT(controller.comparisonPairReady(), 20000);
        QVERIFY(settle(controller));
        QVERIFY(controller.dirty()); // the new location is a change to save
        QVERIFY(controller.saveCurrentProject());
        const auto relinked = snapshot(controller, runB);
        QVERIFY2(relinked == saved, qPrintable(describe(saved) + " vs " + describe(relinked)));
    }

    // 4. Crash with an unsaved edit: recovery offers it back; discard keeps
    // the saved project, which stays authoritative and clean.
    {
        AppController controller(nullptr, recovery);
        QVERIFY(openDay(controller, movedProject));
        const auto metadata = controller.runMetadata(runB);
        QVERIFY(controller.updateRunMetadata(runB, metadata.value("editToken").toString(), metadata.value("name").toString(),
            "Unsaved note", "Dry, 24 °C", "Front bar +1"));
        QVERIFY(controller.dirty());
        controller.m_document.writeRecoverySnapshot();
        // The controller ends without saving, as after a crash.
    }
    {
        AppController controller(nullptr, recovery);
        QVERIFY(controller.recoveryPending());
        controller.resolveStartupRecovery("recover");
        QTRY_VERIFY_WITH_TIMEOUT(controller.eventRuns().size() == 2 && !controller.outingLapsLoading(), 30000);
        QCOMPARE(controller.runMetadata(runB).value("notes").toString(), QString("Unsaved note"));
        QVERIFY(controller.dirty());
        QTRY_VERIFY_WITH_TIMEOUT(controller.comparisonPairReady(), 20000);
        QVERIFY(settle(controller));
        auto recovered = snapshot(controller, runB);
        recovered.notes = saved.notes; // everything but the unsaved note is the saved day
        QVERIFY2(recovered == saved, qPrintable(describe(saved) + " vs " + describe(recovered)));
        controller.m_document.writeRecoverySnapshot();
    }
    {
        AppController controller(nullptr, recovery);
        QVERIFY(controller.recoveryPending());
        controller.resolveStartupRecovery("discard");
        QTRY_VERIFY_WITH_TIMEOUT(controller.eventRuns().size() == 2 && !controller.outingLapsLoading(), 30000);
        QCOMPARE(controller.runMetadata(runB).value("notes").toString(), saved.notes);
        QVERIFY(!controller.dirty());
        QVERIFY(!controller.recoveryPending());
    }
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
    {
        AppController controller(nullptr, recovery);
        controller.loadVbo(QUrl::fromLocalFile(QStringLiteral(TEST_FIXTURE_PATH)));
        QTRY_COMPARE(controller.vboLoadState(), QStringLiteral("ready"));
        QVERIFY(controller.saveProject(QUrl::fromLocalFile(projectA)));
        QCOMPARE(QSettings().value("project/path").toString(), projectA);
        QSignalSpy committed(&controller, &AppController::batchImportCommitted);
        QVERIFY(controller.importAnalysisRuns("Imported day", {QUrl::fromLocalFile(first), QUrl::fromLocalFile(second)}));
        QTRY_COMPARE_WITH_TIMEOUT(committed.size(), 1, 30000);
        QTRY_VERIFY_WITH_TIMEOUT(controller.vboLoadState() == "ready" && !controller.outingLapsLoading(), 30000);
        // The untitled event no longer points at project A.
        QVERIFY(QSettings().value("project/path").toString().isEmpty());
        const auto runId = controller.eventRuns().first().toMap().value("id").toString();
        const auto metadata = controller.runMetadata(runId);
        QVERIFY(controller.updateRunMetadata(runId, metadata.value("editToken").toString(), metadata.value("name").toString(),
            "Unsaved note", "", ""));
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
        QTRY_VERIFY_WITH_TIMEOUT(controller.eventRuns().size() == 2 && !controller.outingLapsLoading(), 30000);
        QCOMPARE(controller.eventName(), QStringLiteral("Imported day"));
        QCOMPARE(controller.runMetadata(controller.eventRuns().first().toMap().value("id").toString()).value("notes").toString(),
                 QStringLiteral("Unsaved note"));
    }
}

void TelemetryTests::importsDroppedFilesAndFolders()
{
    // KAN-88: dropping onto the Analysis window uses the same review as the
    // pickers; unsupported items and busy states get explicit outcomes and
    // never change the current project.
    QTemporaryDir directory; QVERIFY(directory.isValid());
    QSettings settings; settings.clear(); settings.sync();
    const QDir root(directory.path());
    QVERIFY(root.mkpath("day/later"));
    QVERIFY(writeBytes(root.filePath("day/first.vbo"), warpedRouteVbo(true)));
    QVERIFY(writeBytes(root.filePath("day/second.vbo"), warpedRouteVbo(false)));
    QVERIFY(writeBytes(root.filePath("day/later/third.vbo"), warpedRouteVbo(true, 200)));
    QVERIFY(writeBytes(root.filePath("photo.jpg"), "not telemetry"));
    AppController controller(nullptr, directory.filePath("recovery.json"));
    QQmlEngine engine; engine.rootContext()->setContextProperty("appController", &controller);
    QSignalSpy warnings(&engine, &QQmlEngine::warnings);
    QQmlComponent component(&engine, QUrl::fromLocalFile(
        QFileInfo(QStringLiteral(ANALYSIS_PANEL_QML_PATH)).dir().filePath("AnalysisWindow.qml")));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    std::unique_ptr<QObject> window(component.createWithInitialProperties({{"videoSource", QUrl{}},
        {"playbackPosition", 0}, {"playbackRunning", false}, {"mediaDuration", 0}}));
    QVERIFY2(window, qPrintable(component.errorString()));
    QVERIFY(window->findChild<QObject *>("analysisDropArea"));
    const auto drop = [&](const QList<QUrl> &urls) -> bool {
        QVariant result;
        if (!QMetaObject::invokeMethod(window.get(), "dropUrls", Q_RETURN_ARG(QVariant, result),
                                       Q_ARG(QVariant, QVariant::fromValue(urls)))) return false;
        return result.toBool();
    };
    QSignalSpy committed(&controller, &AppController::batchImportCommitted);

    // Without an outing name the drop says so and imports nothing.
    QVERIFY(!drop({QUrl::fromLocalFile(root.filePath("day"))}));
    QVERIFY(controller.batchImportError().contains("outing name"));
    QVERIFY(controller.eventRuns().isEmpty());

    // A folder and an unsupported file: the recordings are imported, the
    // file is reported. The start panel's subfolder choice is off.
    window->findChild<QObject *>("analysisOutingName")->setProperty("text", "Dropped day");
    QVERIFY(drop({QUrl::fromLocalFile(root.filePath("day")), QUrl::fromLocalFile(root.filePath("photo.jpg"))}));
    QTRY_COMPARE_WITH_TIMEOUT(committed.size(), 1, 20000);
    QCOMPARE(controller.eventRuns().size(), 2);
    QVERIFY(controller.analysisImportMessages().join(' ').contains("photo.jpg: not a VBO or RCZ recording"));
    QVERIFY(controller.dirty());
    // Let the new day settle (its recording loads, laps derive) before the next drop.
    QTRY_VERIFY_WITH_TIMEOUT(controller.vboLoadState() == "ready" && !controller.outingLapsLoading(), 20000);

    // A drop during a pending decision is refused and changes nothing.
    controller.requestNewProject();
    QVERIFY(!controller.pendingDestructiveAction().isEmpty());
    const auto project = controller.currentProjectObject();
    QVERIFY(!drop({QUrl::fromLocalFile(root.filePath("day/later"))}));
    QVERIFY(!window->property("dropNotice").toString().isEmpty());
    QCOMPARE(controller.currentProjectObject(), project);
    controller.cancelPendingDestructiveAction();

    // In an open day a dropped folder is added without its subfolders;
    // dropping the subfolder itself adds its recording.
    QVERIFY(drop({QUrl::fromLocalFile(root.filePath("day"))}));
    // Both recordings are already in the day: nothing changes, and it says so.
    QTRY_COMPARE_WITH_TIMEOUT(controller.batchImportState(), QString("idle"), 20000);
    QVERIFY(controller.batchImportError().isEmpty());
    QVERIFY(controller.analysisImportMessages().join(' ').contains("Nothing new to add"));
    QVERIFY(controller.analysisImportMessages().join(' ').contains("Already in this outing; skipped."));
    QCOMPARE(controller.eventRuns().size(), 2);
    QTRY_VERIFY_WITH_TIMEOUT(controller.vboLoadState() == "ready" && !controller.outingLapsLoading(), 20000);
    QVERIFY(drop({QUrl::fromLocalFile(root.filePath("day/later"))}));
    QTRY_COMPARE_WITH_TIMEOUT(committed.size(), 2, 20000);
    QCOMPARE(controller.eventRuns().size(), 3);
    QCOMPARE(warnings.size(), 0);
}

void TelemetryTests::switchesTheActiveRunPrimaryWithoutStaleEditorState()
{
    // KAN-90: the editor holds the active run's primary recording. Switching
    // the primary reloads the editor from the new one, so a save cannot write
    // the old reference back over it.
    QTemporaryDir directory; QVERIFY(directory.isValid());
    QSettings settings; settings.clear(); settings.sync();
    const auto primary = directory.filePath("session.vbo"), alternative = directory.filePath("session-copy.vbo");
    QVERIFY(writeBytes(primary, warpedRouteVbo(true)));
    QVERIFY(writeBytes(alternative, warpedRouteVbo(true, 200)));
    AppController controller(nullptr, directory.filePath("recovery.json"));
    QSignalSpy committed(&controller, &AppController::batchImportCommitted);
    QVERIFY(controller.importAnalysisRuns("Active primary", {QUrl::fromLocalFile(primary)}));
    QTRY_COMPARE_WITH_TIMEOUT(committed.size(), 1, 20000);
    QTRY_COMPARE_WITH_TIMEOUT(controller.vboLoadState(), QString("ready"), 20000);
    QCOMPARE(controller.telemetryName(), QString("session.vbo"));
    // Attached at once (KAN-150): the analysis's own bookkeeping while it
    // settles does not make the review stale.
    const auto runId = controller.activeRunId();
    QVERIFY(controller.attachRunRecording(runId, QUrl::fromLocalFile(alternative)));
    QTRY_COMPARE_WITH_TIMEOUT(controller.runRecordingReview().value("state").toString(), QString("review"), 20000);
    QVERIFY(controller.confirmRunRecording());
    QTRY_VERIFY_WITH_TIMEOUT(controller.runRecordingReview().isEmpty(), 20000);
    const auto newPrimary = controller.runRecordings(runId)[1].toMap().value("sourceId").toString();
    // Through the run details dialog, as the driver does it.
    QQmlEngine engine; engine.rootContext()->setContextProperty("appController", &controller);
    QSignalSpy warnings(&engine, &QQmlEngine::warnings);
    QQmlComponent component(&engine, QUrl::fromLocalFile(
        QFileInfo(QStringLiteral(ANALYSIS_PANEL_QML_PATH)).dir().filePath("AnalysisWindow.qml")));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    std::unique_ptr<QObject> object(component.createWithInitialProperties({{"videoSource", QUrl{}},
        {"playbackPosition", 0}, {"playbackRunning", false}, {"mediaDuration", 0}}));
    auto *window = qobject_cast<QQuickWindow *>(object.get()); QVERIFY(window);
    window->resize(1180, 720);
    window->show(); QVERIFY(QTest::qWaitForWindowExposed(window));
    auto *dialog = window->findChild<QObject *>("runDetailsDialog"); QVERIFY(dialog);
    QVERIFY(QMetaObject::invokeMethod(dialog, "open"));
    QTRY_VERIFY(dialog->property("opened").toBool());
    QCOMPARE(dialog->property("recordings").toList().size(), 2);
    QQuickItem *makePrimary = nullptr;
    std::function<void(QQuickItem *)> find = [&](QQuickItem *item) {
        if (makePrimary || !item->isVisible()) return;
        if (item->objectName() == "makePrimaryRecording") { makePrimary = item; return; }
        for (auto *child : item->childItems()) find(child);
    };
    find(window->contentItem());
    QVERIFY(makePrimary && makePrimary->isEnabled());
    // KAN-101: comparing the clocks first, from the same row.
    QQuickItem *checkClock = nullptr, *reviewLabel = nullptr;
    std::function<void(QQuickItem *)> findClock = [&](QQuickItem *item) {
        if (!item->isVisible()) return;
        if (item->objectName() == "checkRecordingClock") checkClock = item;
        if (item->objectName() == "runRecordingReview") reviewLabel = item;
        for (auto *child : item->childItems()) findClock(child);
    };
    findClock(window->contentItem());
    QVERIFY(checkClock && checkClock->isEnabled());
    checkClock->forceActiveFocus(); QTest::keyClick(window, Qt::Key_Space);
    QTRY_COMPARE_WITH_TIMEOUT(controller.runRecordingReview().value("state").toString(), QString("alignment"), 20000);
    QTRY_VERIFY((findClock(window->contentItem()), reviewLabel && reviewLabel->isVisible()));
    const auto clockText = reviewLabel->property("text").toString();
    QVERIFY2(clockText.contains("not enough evidence") && clockText.contains("no speed channel")
        && clockText.contains("nothing is merged"), qPrintable(clockText));
    const QString review = qEnvironmentVariable("FLAPPEDEAR_LAYOUT_REVIEW_DIR");
    if (!review.isEmpty()) {
        auto *scroll = window->findChild<QQuickItem *>("runDetailsScroll");
        auto *flick = scroll ? scroll->property("contentItem").value<QQuickItem *>() : nullptr;
        QTest::qWait(300);
        if (flick) flick->setProperty("contentY", flick->property("contentHeight").toDouble() - flick->height());
        QTest::qWait(300);
        static_cast<void>(window->grabWindow().save(QDir(review).filePath("recording-clock.png")));
    }
    controller.cancelRunRecording();
    QTRY_VERIFY(controller.runRecordingReview().isEmpty());
    if (!review.isEmpty()) {
        auto *scroll = window->findChild<QQuickItem *>("runDetailsScroll");
        auto *flick = scroll ? scroll->property("contentItem").value<QQuickItem *>() : nullptr;
        if (flick) flick->setProperty("contentY", flick->property("contentHeight").toDouble() - flick->height());
        QTest::qWait(300);
        static_cast<void>(window->grabWindow().save(QDir(review).filePath("run-recordings.png")));
    }
    makePrimary->forceActiveFocus(); QTest::keyClick(window, Qt::Key_Space);
    QTRY_VERIFY_WITH_TIMEOUT(controller.runRecordingReview().isEmpty()
        && controller.runRecordings(runId)[1].toMap().value("primary").toBool(), 20000);
    QTRY_COMPARE(dialog->property("recordings").toList()[1].toMap().value("primary").toBool(), true);
    QCOMPARE(warnings.size(), 0);
    QTRY_COMPARE_WITH_TIMEOUT(controller.telemetryName(), QString("session-copy.vbo"), 20000);
    QTRY_COMPARE_WITH_TIMEOUT(controller.vboLoadState(), QString("ready"), 20000);
    const auto path = directory.filePath("day.fetproject");
    QVERIFY(controller.saveProject(QUrl::fromLocalFile(path)));
    const auto run = QJsonDocument::fromJson(readBytes(path)).object().value("event").toObject()
        .value("runs").toArray().first().toObject();
    QCOMPARE(run.value("primaryTelemetrySourceId").toString(), newPrimary);
    for (const auto &value : run.value("sources").toObject().value("telemetry").toArray()) {
        const auto source = value.toObject();
        const auto file = QFileInfo(source.value("reference").toObject().value("absolutePath").toString()).fileName();
        QCOMPARE(file, source.value("id").toString() == newPrimary ? QString("session-copy.vbo") : QString("session.vbo"));
    }
}

void TelemetryTests::showsCoastingOnTheOpenLap()
{
    // KAN-92: the lap view's Coasting pane -- totals, provenance, segment
    // rows and episodes, drawn on the map; selecting an episode moves the
    // lap cursor there. The fixture has no pedal channels: inferred.
    QTemporaryDir directory; QVERIFY(directory.isValid());
    QSettings settings; settings.clear(); settings.sync();
    const auto fullA = directory.filePath("full-a.vbo"), fullB = directory.filePath("full-b.vbo");
    // Coasting needs speed; the route fixture has none.
    QVERIFY(writeBytes(fullA, withVelocity(fullM4Vbo(true, 140, 80, 1.0))));
    QVERIFY(writeBytes(fullB, withVelocity(fullM4Vbo(false, 150, 85, 0.9))));
    AppController controller(nullptr, directory.filePath("recovery.json"));
    QVERIFY(controller.importAnalysisRuns("Coasting day", {QUrl::fromLocalFile(fullA), QUrl::fromLocalFile(fullB)}));
    QTRY_VERIFY_WITH_TIMEOUT(controller.eventRuns().size() == 2 && !controller.outingLapsLoading(), 30000);
    QTRY_VERIFY(!controller.outingComparisonGroupId().isEmpty());
    QVERIFY(!approveAllSegmentsOnRun(controller, "Session 1").isEmpty());
    QQmlEngine engine; engine.rootContext()->setContextProperty("appController", &controller);
    QSignalSpy warnings(&engine, &QQmlEngine::warnings);
    QQmlComponent component(&engine, QUrl::fromLocalFile(
        QFileInfo(QStringLiteral(ANALYSIS_PANEL_QML_PATH)).dir().filePath("AnalysisWindow.qml")));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    std::unique_ptr<QObject> object(component.createWithInitialProperties({{"videoSource", QUrl{}},
        {"playbackPosition", 0}, {"playbackRunning", false}, {"mediaDuration", 0}}));
    auto *window = qobject_cast<QQuickWindow *>(object.get()); QVERIFY(window);
    window->resize(1180, 720);
    window->show(); QVERIFY(QTest::qWaitForWindowExposed(window));
    int lapIndex = -1;
    const auto rows = controller.outingLaps();
    for (int i = 0; i < rows.size() && lapIndex < 0; ++i)
        if (rows[i].toMap().value("type") == "LAP" && rows[i].toMap().value("runName") == "Session 1") lapIndex = i;
    QVERIFY(controller.selectOutingLap(lapIndex));
    QTRY_COMPARE_WITH_TIMEOUT(controller.outingLapDetailState(), QString("ready"), 20000);
    auto *toggle = window->findChild<QQuickItem *>("toggleCoasting"); QVERIFY(toggle);
    toggle->forceActiveFocus(); QTest::keyClick(window, Qt::Key_Space);
    QTRY_VERIFY(window->findChild<QQuickItem *>("coastingPanel"));
    auto *panel = window->findChild<QQuickItem *>("coastingPanel");
    QTRY_VERIFY_WITH_TIMEOUT(panel->property("coasting").toMap().value("segmentsReady").toBool(), 20000);
    const auto coasting = panel->property("coasting").toMap();
    QCOMPARE(coasting.value("provenance").toString(), QString("inferred"));
    QVERIFY(window->findChild<QQuickItem *>("coastingProvenance")->property("text").toString().contains("Inferred"));
    const auto episodes = coasting.value("episodes").toList();
    QVERIFY(!episodes.isEmpty());
    QVERIFY(!coasting.value("segments").toList().isEmpty());
    QCOMPARE(panel->property("mapLayers").toList().size(), episodes.size());
    QVERIFY(window->findChild<QQuickItem *>("coastingMapLayer")->isVisible());
    // Selecting an episode puts the lap cursor at its start.
    QQuickItem *first = nullptr;
    std::function<void(QQuickItem *)> find = [&](QQuickItem *item) {
        if (first || !item->isVisible()) return;
        if (item->objectName() == "coastingEpisode0") { first = item; return; }
        for (auto *child : item->childItems()) find(child);
    };
    find(window->contentItem());
    QVERIFY(first);
    first->forceActiveFocus(); QTest::keyClick(window, Qt::Key_Space);
    QTRY_VERIFY(std::abs(controller.outingLapCursor() - episodes.first().toMap().value("startTime").toDouble()) < 1e-6);
    const QString review = qEnvironmentVariable("FLAPPEDEAR_LAYOUT_REVIEW_DIR");
    if (!review.isEmpty()) static_cast<void>(window->grabWindow().save(QDir(review).filePath("coasting.png")));
    QVERIFY(unreachableControls(window).isEmpty());
    QCOMPARE(warnings.size(), 0);
}

void TelemetryTests::showsTrailBrakingInTheCornerAnalyzer()
{
    // KAN-93: the Corner Analyzer's Trail braking row -- A/B braking while
    // cornering on the selected segment, its provenance (no brake channel
    // here: inferred from deceleration; lateral G calculated), strips, and
    // the brake and lateral G signals brought into the charts on request.
    QTemporaryDir directory; QVERIFY(directory.isValid());
    QSettings settings; settings.clear(); settings.sync();
    const auto fullA = directory.filePath("full-a.vbo"), fullB = directory.filePath("full-b.vbo");
    QVERIFY(writeBytes(fullA, withVelocity(fullM4Vbo(true, 140, 80, 1.0))));
    QVERIFY(writeBytes(fullB, withVelocity(fullM4Vbo(false, 150, 85, 0.9))));
    AppController controller(nullptr, directory.filePath("recovery.json"));
    QVERIFY(controller.importAnalysisRuns("Trail day", {QUrl::fromLocalFile(fullA), QUrl::fromLocalFile(fullB)}));
    QTRY_VERIFY_WITH_TIMEOUT(controller.eventRuns().size() == 2 && !controller.outingLapsLoading(), 30000);
    QTRY_VERIFY(!controller.outingComparisonGroupId().isEmpty());
    QVERIFY(!approveAllSegmentsOnRun(controller, "Session 1").isEmpty());
    // Opened from a time loss, as a driver does: both laps are then measured
    // against the approved segments.
    controller.requestOutingDayReport();
    QTRY_VERIFY_WITH_TIMEOUT([&] {
        for (const auto &value : controller.outingDayReport().value("results").toList())
            if (value.toMap().value("id") == "timeLosses") return value.toMap().value("status") == "available";
        return false;
    }(), 60000);
    QVariantList losses;
    for (const auto &value : controller.outingDayReport().value("results").toList())
        if (value.toMap().value("id") == "timeLosses") losses = value.toMap().value("evidence").toList();
    QVERIFY(controller.openTimeLoss({{"segmentId", losses.first().toMap().value("segmentId")},
                                     {"lapReference", losses.first().toMap().value("reference")}}));
    QTRY_VERIFY_WITH_TIMEOUT(controller.comparisonPairReady(), 20000);
    // The corner with the most braking while cornering.
    QVariantMap corner;
    double most = -1;
    for (const auto &value : controller.comparisonApprovedSegments()) {
        const auto segment = value.toMap();
        const auto trail = controller.comparisonTrailBraking(segment.value("startMeters").toDouble(), segment.value("endMeters").toDouble());
        const auto overlap = trail.value("laps").toList().value(0).toMap().value("overlapSeconds").toDouble();
        if (overlap > most) { most = overlap; corner = segment; }
    }
    QVERIFY(most > 0);
    const auto trail = controller.comparisonTrailBraking(corner.value("startMeters").toDouble(), corner.value("endMeters").toDouble());
    for (const auto &value : trail.value("laps").toList()) {
        const auto lap = value.toMap();
        QVERIFY(lap.value("valid").toBool());
        QCOMPARE(lap.value("brakingProvenance").toString(), QString("inferred"));
        QCOMPARE(lap.value("corneringProvenance").toString(), QString("calculated"));
        QVERIFY(lap.value("overlapMeters").toDouble() > 0);
        QVERIFY(!lap.value("strips").toMap().value("overlap").toList().isEmpty());
    }

    QQmlEngine engine; engine.rootContext()->setContextProperty("appController", &controller);
    QSignalSpy warnings(&engine, &QQmlEngine::warnings);
    QQmlComponent component(&engine, QUrl::fromLocalFile(
        QFileInfo(QStringLiteral(ANALYSIS_PANEL_QML_PATH)).dir().filePath("AnalysisWindow.qml")));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    std::unique_ptr<QObject> object(component.createWithInitialProperties({{"videoSource", QUrl{}},
        {"playbackPosition", 0}, {"playbackRunning", false}, {"mediaDuration", 0}}));
    auto *window = qobject_cast<QQuickWindow *>(object.get()); QVERIFY(window);
    window->resize(1180, 720);
    window->show(); QVERIFY(QTest::qWaitForWindowExposed(window));
    auto *panel = window->findChild<QQuickItem *>("comparisonSegmentPanel"); QVERIFY(panel);
    if (!panel->isVisible()) {
        auto *toggle = window->findChild<QQuickItem *>("comparisonToggleCornerAnalyzer"); QVERIFY(toggle);
        toggle->forceActiveFocus(); QTest::keyClick(window, Qt::Key_Space);
        QTRY_VERIFY(panel->isVisible());
    }
    panel->setProperty("selectedSegmentId", corner.value("id"));
    QTRY_VERIFY(window->findChild<QQuickItem *>("cornerAnalyzerTrailBrakingRow")->isVisible());
    QVERIFY(window->findChild<QQuickItem *>("cornerAnalyzerTrailNote")->property("text").toString().contains("not automatically better"));
    QVERIFY(window->findChild<QQuickItem *>("cornerAnalyzerTrailNote")->property("text").toString().contains("inferred from deceleration"));
    // The strips are Repeater delegates: search the visual tree once created.
    const auto stripShown = [&] {
        QQuickItem *strip = nullptr;
        std::function<void(QQuickItem *)> find = [&](QQuickItem *item) {
            if (strip || !item->isVisible()) return;
            if (item->objectName() == "cornerAnalyzerTrailStrip0") { strip = item; return; }
            for (auto *child : item->childItems()) find(child);
        };
        find(window->contentItem());
        return strip && strip->width() > 0;
    };
    QTRY_VERIFY(stripShown());
    auto *show = window->findChild<QQuickItem *>("cornerAnalyzerTrailShow"); QVERIFY(show);
    show->forceActiveFocus(); QTest::keyClick(window, Qt::Key_Space);
    QTRY_VERIFY(controller.comparisonPersistedChannels().contains("longacc-calc"));
    QVERIFY(controller.comparisonPersistedChannels().contains("latacc-calc"));
    const QString review = qEnvironmentVariable("FLAPPEDEAR_LAYOUT_REVIEW_DIR");
    if (!review.isEmpty()) {
        // Scroll the Corner Analyzer to its end to show the strips and note.
        if (auto *scroll = window->findChild<QQuickItem *>("cornerAnalyzerScroll"))
            scroll->setProperty("contentY", std::max(0.0, scroll->property("contentHeight").toDouble() - scroll->height()));
        QTest::qWait(500);
        static_cast<void>(window->grabWindow().save(QDir(review).filePath("trail-braking.png")));
    }
    QCOMPARE(warnings.size(), 0);
}

void TelemetryTests::coloursTheComparisonMapByAChannel()
{
    // KAN-97: analytical map layers on the A/B map. Recorded speed, G and a
    // temperature colour the chosen lap's line with a legend; the A-B delta
    // is diverging around zero; a channel neither lap recorded (brake,
    // throttle here) is unavailable, never inferred.
    QTemporaryDir directory; QVERIFY(directory.isValid());
    QSettings settings; settings.clear(); settings.sync();
    const auto fullA = directory.filePath("full-a.vbo"), fullB = directory.filePath("full-b.vbo");
    QVERIFY(writeBytes(fullA, withVelocity(fullM4Vbo(true, 140, 80, 1.0))));
    QVERIFY(writeBytes(fullB, withVelocity(fullM4Vbo(false, 150, 85, 0.9))));
    AppController controller(nullptr, directory.filePath("recovery.json"));
    QVERIFY(controller.importAnalysisRuns("Map day", {QUrl::fromLocalFile(fullA), QUrl::fromLocalFile(fullB)}));
    QTRY_VERIFY_WITH_TIMEOUT(controller.eventRuns().size() == 2 && !controller.outingLapsLoading(), 30000);
    QTRY_VERIFY(!controller.outingComparisonGroupId().isEmpty());
    QVERIFY(!approveAllSegmentsOnRun(controller, "Session 1").isEmpty());
    controller.requestOutingDayReport();
    QTRY_VERIFY_WITH_TIMEOUT([&] {
        for (const auto &value : controller.outingDayReport().value("results").toList())
            if (value.toMap().value("id") == "timeLosses") return value.toMap().value("status") == "available";
        return false;
    }(), 60000);
    QVariantList losses;
    for (const auto &value : controller.outingDayReport().value("results").toList())
        if (value.toMap().value("id") == "timeLosses") losses = value.toMap().value("evidence").toList();
    QVERIFY(controller.openTimeLoss({{"segmentId", losses.first().toMap().value("segmentId")},
                                     {"lapReference", losses.first().toMap().value("reference")}}));
    QTRY_VERIFY_WITH_TIMEOUT(controller.comparisonPairReady(), 20000);

    QHash<QString, bool> available;
    for (const auto &value : controller.comparisonMapLayerOptions())
        available.insert(value.toMap().value("id").toString(), value.toMap().value("available").toBool());
    QVERIFY(available.value("speed") && available.value("delta") && available.value("lateralG")
        && available.value("longitudinalG") && available.value("temperature:coolant_temp-obd"));
    QVERIFY(available.contains("brake") && !available.value("brake"));
    QVERIFY(available.contains("throttle") && !available.value("throttle"));

    const auto temperature = controller.comparisonMapLayer("temperature:coolant_temp-obd", 0);
    QVERIFY(temperature.value("valid").toBool());
    QCOMPARE(temperature.value("scale").toString(), QString("sequential"));
    // The recorded coolant rises 0.01 °C per sample from 80 or 85 °C.
    QVERIFY(temperature.value("minimum").toDouble() >= 80.0 && temperature.value("maximum").toDouble() <= 90.0);
    QVERIFY(temperature.value("maximum").toDouble() > temperature.value("minimum").toDouble());
    for (const auto &value : temperature.value("polylines").toList()) {
        const auto polyline = value.toMap();
        QCOMPARE(polyline.value("points").toList().size(), polyline.value("values").toList().size());
        for (const auto &point : polyline.value("points").toList()) {
            QVERIFY(point.toPointF().x() >= -0.01 && point.toPointF().x() <= 1.01);
            QVERIFY(point.toPointF().y() >= -0.01 && point.toPointF().y() <= 1.01);
        }
    }
    const auto lateral = controller.comparisonMapLayer("lateralG", 1);
    QVERIFY(lateral.value("valid").toBool());
    QCOMPARE(lateral.value("scale").toString(), QString("diverging"));
    QCOMPARE(lateral.value("provenance").toString(), QString("calculated"));
    QVERIFY(lateral.value("minimum").toDouble() < 0 && lateral.value("maximum").toDouble() > 0);
    const auto delta = controller.comparisonMapLayer("delta", 1);
    QVERIFY(delta.value("valid").toBool());
    QCOMPARE(delta.value("unit").toString(), QString("s"));
    QCOMPARE(controller.comparisonMapLayer("brake", 0).value("reason").toString(), QString("channelMissing"));
    QCOMPARE(controller.comparisonMapLayer("nonsense", 0).value("reason").toString(), QString("unknownLayer"));
    QVERIFY(!controller.comparisonMapLayer("speed", 2).value("valid").toBool());

    QQmlEngine engine; engine.rootContext()->setContextProperty("appController", &controller);
    QSignalSpy warnings(&engine, &QQmlEngine::warnings);
    QQmlComponent component(&engine, QUrl::fromLocalFile(
        QFileInfo(QStringLiteral(ANALYSIS_PANEL_QML_PATH)).dir().filePath("AnalysisWindow.qml")));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    std::unique_ptr<QObject> object(component.createWithInitialProperties({{"videoSource", QUrl{}},
        {"playbackPosition", 0}, {"playbackRunning", false}, {"mediaDuration", 0}}));
    auto *window = qobject_cast<QQuickWindow *>(object.get()); QVERIFY(window);
    window->resize(1180, 656);
    window->show(); QVERIFY(QTest::qWaitForWindowExposed(window));
    auto *map = window->findChild<QQuickItem *>("comparisonOverlayMap"); QVERIFY(map);
    auto *picker = window->findChild<QQuickItem *>("comparisonMapLayerPicker"); QVERIFY(picker);
    auto *layerCanvas = window->findChild<QQuickItem *>("comparisonMapLayer"); QVERIFY(layerCanvas);
    auto *legend = window->findChild<QQuickItem *>("comparisonMapLegend"); QVERIFY(legend);
    QTRY_VERIFY(picker->isVisible());
    QVERIFY(!layerCanvas->isVisible() && !legend->isVisible()); // off by default

    // Choosing the temperature in the picker colours lap B and shows the legend.
    const auto options = map->property("layerOptions").toList();
    int temperatureIndex = -1;
    for (int index = 0; index < options.size(); ++index)
        if (options[index].toMap().value("id") == "temperature:coolant_temp-obd") temperatureIndex = index + 1;
    QVERIFY(temperatureIndex > 0);
    QVERIFY(QMetaObject::invokeMethod(picker, "activated", Q_ARG(int, temperatureIndex)));
    QCOMPARE(map->property("layerId").toString(), QString("temperature:coolant_temp-obd"));
    QTRY_VERIFY(layerCanvas->isVisible() && legend->isVisible());
    QVERIFY(window->findChild<QQuickItem *>("comparisonMapLegendSource")->property("text").toString().contains("lap B"));
    QVERIFY(window->findChild<QQuickItem *>("comparisonMapLegendHigh")->property("text").toString().startsWith("8"));
    // Lap A instead.
    auto *slotA = window->findChild<QQuickItem *>("comparisonMapLayerSlotA"); QVERIFY(slotA && slotA->isVisible());
    slotA->forceActiveFocus(); QTest::keyClick(window, Qt::Key_Space);
    QTRY_COMPARE(map->property("layerSlot").toInt(), 0);
    QTRY_VERIFY(window->findChild<QQuickItem *>("comparisonMapLegendSource")->property("text").toString().contains("lap A"));

    // Diverging delta: symmetric around zero, labelled.
    map->setProperty("layerId", "delta");
    QTRY_VERIFY(layerCanvas->isVisible());
    QVERIFY(std::abs(map->property("layerLow").toDouble() + map->property("layerHigh").toDouble()) < 1e-9);
    QVERIFY(window->findChild<QQuickItem *>("comparisonMapLegendHigh")->property("text").toString().contains("A behind"));
    // Hovering moves only the markers; the layer is not rebuilt.
    const auto polylines = layerCanvas->property("polylines");
    map->setProperty("hoverDistanceMeters", 100.0);
    QTest::qWait(100);
    QCOMPARE(layerCanvas->property("polylines"), polylines);

    // Not recorded: unavailable, nothing drawn.
    map->setProperty("layerId", "brake");
    QTRY_VERIFY(!layerCanvas->isVisible());
    QCOMPARE(window->findChild<QQuickItem *>("comparisonMapLayerUnavailable")->property("text").toString(),
        QString("Not recorded on either lap."));
    const QString review = qEnvironmentVariable("FLAPPEDEAR_LAYOUT_REVIEW_DIR");
    map->setProperty("layerId", "speed");
    QTRY_VERIFY(layerCanvas->isVisible());
    if (!review.isEmpty()) {
        QTest::qWait(300);
        static_cast<void>(window->grabWindow().save(QDir(review).filePath("map-layer.png")));
    }
    QVERIFY(unreachableControls(window).isEmpty());
    // Back to plain A/B lines.
    QVERIFY(QMetaObject::invokeMethod(picker, "activated", Q_ARG(int, 0)));
    QTRY_VERIFY(!layerCanvas->isVisible() && !legend->isVisible());
    QCOMPARE(warnings.size(), 0);
}

void TelemetryTests::associatesTemperaturesWithLapPerformance()
{
    // KAN-100: each recorded temperature against lap time and strong
    // acceleration over the comparison group's eligible laps, with counts,
    // the laps behind it and the time-of-day confound; too few laps claim
    // nothing, and excluding laps changes the population.
    QTemporaryDir directory; QVERIFY(directory.isValid());
    QSettings settings; settings.clear(); settings.sync();
    // Four sessions whose coolant starts warmer each time (80, 85, 90, 95).
    QList<QUrl> files;
    for (int run = 0; run < 4; ++run) {
        const auto path = directory.filePath(QString("run-%1.vbo").arg(run));
        QVERIFY(writeBytes(path, withVelocity(fullM4Vbo(run % 2 == 0, 140, 80 + 5 * run, 1.0 - 0.05 * run))));
        files.append(QUrl::fromLocalFile(path));
    }
    AppController controller(nullptr, directory.filePath("recovery.json"));
    QVERIFY(controller.importAnalysisRuns("Warm day", files.mid(0, 2)));
    QTRY_VERIFY_WITH_TIMEOUT(controller.eventRuns().size() == 2 && !controller.outingLapsLoading(), 30000);
    QTRY_VERIFY(!controller.outingComparisonGroupId().isEmpty());
    // Nothing until the channel summaries are ready.
    QVERIFY(controller.outingTemperatureAssociations().value("channels").toList().isEmpty());
    controller.requestOutingChannelSummaries();
    QTRY_COMPARE_WITH_TIMEOUT(controller.outingChannelSummaries().value("state").toString(), QString("ready"), 30000);
    auto associations = controller.outingTemperatureAssociations();
    QCOMPARE(associations.value("algorithm").toString(), QString("spearman-rank-v1"));
    QCOMPARE(associations.value("eligibleLaps").toInt(), 6);
    auto coolant = associations.value("channels").toList().value(0).toMap();
    QCOMPARE(coolant.value("channel").toString(), QString("coolant_temp-obd"));
    // Six laps: below the minimum of eight, so nothing is claimed.
    QCOMPARE(coolant.value("lapTime").toMap().value("unavailableReason").toString(), QString("tooFewSamples"));
    QCOMPARE(coolant.value("lapTime").toMap().value("count").toInt(), 6);
    QVERIFY(!coolant.value("confoundedByOrder").toBool());
    QCOMPARE(coolant.value("observations").toList().size(), 6);

    // Two more sessions: twelve laps, the coolant warming through the day.
    QVERIFY(controller.importAnalysisRuns("Warm day", files.mid(2, 2)));
    QTRY_VERIFY_WITH_TIMEOUT(controller.eventRuns().size() == 4 && !controller.outingLapsLoading(), 30000);
    QTRY_COMPARE_WITH_TIMEOUT(controller.outingChannelSummaries().value("state").toString(), QString("idle"), 30000);
    controller.requestOutingChannelSummaries();
    QTRY_COMPARE_WITH_TIMEOUT(controller.outingChannelSummaries().value("state").toString(), QString("ready"), 30000);
    associations = controller.outingTemperatureAssociations();
    QCOMPARE(associations.value("eligibleLaps").toInt(), 12);
    coolant = associations.value("channels").toList().value(0).toMap();
    const auto lapTime = coolant.value("lapTime").toMap(), acceleration = coolant.value("acceleration").toMap();
    QVERIFY(lapTime.value("available").toBool());
    QCOMPARE(lapTime.value("count").toInt(), 12);
    QVERIFY(std::abs(lapTime.value("coefficient").toDouble()) <= 1.0);
    QVERIFY(acceleration.value("available").toBool()); // from longacc-calc
    // The coolant only rises through the day: the order confound is flagged.
    QVERIFY(coolant.value("order").toMap().value("coefficient").toDouble() > 0.95);
    QVERIFY(coolant.value("confoundedByOrder").toBool());
    const auto observations = coolant.value("observations").toList();
    QCOMPARE(observations.size(), 12);
    for (const auto &value : observations) {
        const auto observation = value.toMap();
        QVERIFY(observation.value("temperature").toDouble() >= 80.0 && observation.value("temperature").toDouble() < 110.0);
        QVERIFY(observation.value("lapTime").toDouble() > 0.0);
        QVERIFY(observation.value("coverage").toDouble() >= 0.8);
        QVERIFY(observation.contains("strongAccelerationG"));
    }

    // Excluding laps removes them from the population.
    int excluded = 0;
    for (const auto &value : observations) {
        if (excluded == 5) break;
        QVERIFY(controller.setOutingLapExcluded(value.toMap().value("reference").toMap(), true, "Traffic"));
        ++excluded;
    }
    QTRY_COMPARE(controller.outingTemperatureAssociations().value("eligibleLaps").toInt(), 7);
    coolant = controller.outingTemperatureAssociations().value("channels").toList().value(0).toMap();
    QCOMPARE(coolant.value("lapTime").toMap().value("unavailableReason").toString(), QString("tooFewSamples"));

    // The Car & driver card shows the association and its laps.
    for (const auto &value : observations) controller.setOutingLapExcluded(value.toMap().value("reference").toMap(), false);
    QTRY_COMPARE(controller.outingTemperatureAssociations().value("eligibleLaps").toInt(), 12);
    QQmlEngine engine; engine.rootContext()->setContextProperty("appController", &controller);
    QSignalSpy warnings(&engine, &QQmlEngine::warnings);
    QQmlComponent component(&engine);
    component.setData("import QtQuick\nWindow { width: 900; height: 900; visible: true; CarDriverView { anchors.fill: parent } }",
        QUrl::fromLocalFile(QStringLiteral(ANALYSIS_PANEL_QML_PATH)));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    std::unique_ptr<QObject> object(component.create()); QVERIFY2(object, qPrintable(component.errorString()));
    auto *window = qobject_cast<QQuickWindow *>(object.get()); QVERIFY(window);
    QVERIFY(QTest::qWaitForWindowExposed(window));
    const auto findVisual = [](auto &&self, QQuickItem *item, const QString &name) -> QQuickItem * {
        if (item->objectName() == name) return item;
        for (auto *child : item->childItems()) if (auto *found = self(self, child, name)) return found;
        return nullptr;
    };
    const auto find = [&](const QString &name) { return findVisual(findVisual, window->contentItem(), name); };
    QQuickItem *lapTimeLabel = nullptr;
    QTRY_VERIFY((lapTimeLabel = find("carDriverAssociationLapTime0")));
    QTRY_VERIFY(lapTimeLabel->property("text").toString().contains("12 laps"));
    QVERIFY(lapTimeLabel->property("text").toString().startsWith("Lap time: ρ "));
    QVERIFY(find("carDriverAssociationAcceleration0")->property("text").toString().startsWith("Strong acceleration: ρ "));
    auto *confound = find("carDriverAssociationConfound0"); QVERIFY(confound);
    QVERIFY(confound->isVisible());
    QVERIFY(confound->property("text").toString().contains("cannot be told apart"));
    QVERIFY(find("carDriverAssociationScatter0")->isVisible());
    auto *laps = find("carDriverAssociationLaps0"); QVERIFY(laps);
    QVERIFY(!laps->isVisible());
    auto *show = find("carDriverAssociationShowLaps0"); QVERIFY(show);
    QVERIFY(show->property("text").toString().contains("12"));
    show->forceActiveFocus(); QTest::keyClick(window, Qt::Key_Space);
    QTRY_VERIFY(laps->isVisible());
    QTRY_COMPARE(laps->childItems().size(), 13); // 12 laps and the Repeater
    const QString review = qEnvironmentVariable("FLAPPEDEAR_LAYOUT_REVIEW_DIR");
    if (!review.isEmpty()) {
        QTest::qWait(300);
        static_cast<void>(window->grabWindow().save(QDir(review).filePath("temperature-association.png")));
    }
    QCOMPARE(warnings.size(), 0);
}

void TelemetryTests::reviewsSourceFusionInRunDetails()
{
    // KAN-103: Run details -> Fuse... shows the clock alignment and the
    // resulting channels; a conflicting channel needs a rule before Approve
    // is enabled; approving marks the recording Fused.
    QTemporaryDir directory; QVERIFY(directory.isValid());
    QSettings settings; settings.clear(); settings.sync();
    const auto primary = directory.filePath("session.vbo"), biased = directory.filePath("session-obd.vbo");
    QVERIFY(writeBytes(primary, withSessionSpeed(warpedRouteVbo(true), false)));
    QVERIFY(writeBytes(biased, withSessionSpeed(warpedRouteVbo(true), true, 8.0)));
    AppController controller(nullptr, directory.filePath("recovery.json"));
    QSignalSpy committed(&controller, &AppController::batchImportCommitted);
    QVERIFY(controller.importAnalysisRuns("Fusion review", {QUrl::fromLocalFile(primary)}));
    QTRY_COMPARE_WITH_TIMEOUT(committed.size(), 1, 20000);
    // Straight after the import (KAN-150): the analysis may still record its
    // track inference, which no longer makes the attach review stale.
    const auto runId = controller.activeRunId();
    QVERIFY(controller.attachRunRecording(runId, QUrl::fromLocalFile(biased)));
    QTRY_COMPARE_WITH_TIMEOUT(controller.runRecordingReview().value("state").toString(), QString("review"), 20000);
    QVERIFY(controller.confirmRunRecording());
    QTRY_VERIFY_WITH_TIMEOUT(controller.runRecordingReview().isEmpty(), 20000);

    QQmlEngine engine; engine.rootContext()->setContextProperty("appController", &controller);
    QSignalSpy warnings(&engine, &QQmlEngine::warnings);
    QQmlComponent component(&engine, QUrl::fromLocalFile(
        QFileInfo(QStringLiteral(ANALYSIS_PANEL_QML_PATH)).dir().filePath("AnalysisWindow.qml")));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    std::unique_ptr<QObject> object(component.createWithInitialProperties({{"videoSource", QUrl{}},
        {"playbackPosition", 0}, {"playbackRunning", false}, {"mediaDuration", 0}}));
    auto *window = qobject_cast<QQuickWindow *>(object.get()); QVERIFY(window);
    window->resize(1180, 720);
    window->show(); QVERIFY(QTest::qWaitForWindowExposed(window));
    auto *dialog = window->findChild<QObject *>("runDetailsDialog"); QVERIFY(dialog);
    QVERIFY(QMetaObject::invokeMethod(dialog, "open"));
    QTRY_VERIFY(dialog->property("opened").toBool());
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
    QQuickItem *fuse = nullptr;
    QTRY_VERIFY((fuse = findVisible("reviewRunFusion")) && fuse->isEnabled());
    fuse->forceActiveFocus(); QTest::keyClick(window, Qt::Key_Space);
    QTRY_COMPARE_WITH_TIMEOUT(controller.runRecordingReview().value("state").toString(), QString("fusionReview"), 30000);
    QQuickItem *approve = nullptr, *rule = nullptr, *channels = nullptr;
    QTRY_VERIFY((approve = findVisible("approveRunFusion")) && (rule = findVisible("runFusionRule-speed"))
        && (channels = findVisible("runFusionChannels")));
    QVERIFY(!approve->isEnabled()); // speed disagrees by 8 km/h: a rule is required
    QVERIFY(findVisible("runRecordingReview")->property("text").toString().contains("lines up with the primary"));
    const QString review = qEnvironmentVariable("FLAPPEDEAR_LAYOUT_REVIEW_DIR");
    const auto capture = [&](const QString &name) {
        if (review.isEmpty()) return;
        auto *scroll = window->findChild<QQuickItem *>("runDetailsScroll");
        auto *flick = scroll ? scroll->property("contentItem").value<QQuickItem *>() : nullptr;
        QTest::qWait(300);
        if (flick) flick->setProperty("contentY", std::max(0.0, flick->property("contentHeight").toDouble() - flick->height()));
        QTest::qWait(300);
        static_cast<void>(window->grabWindow().save(QDir(review).filePath(name)));
    };
    capture("fusion-review.png");
    // "Fill the primary's gaps" (the second choice for a conflicting channel).
    QVERIFY(QMetaObject::invokeMethod(rule, "activated", Q_ARG(int, 1)));
    QTRY_VERIFY(approve->isEnabled());
    QCOMPARE(dialog->property("fusionRules").toMap().value("speed").toString(), QString("fillGaps"));
    approve->forceActiveFocus(); QTest::keyClick(window, Qt::Key_Space);
    QTRY_VERIFY_WITH_TIMEOUT(controller.runRecordingReview().isEmpty(), 20000);
    QQuickItem *state = nullptr;
    QTRY_VERIFY((state = findVisible("recordingFusionState")) && state->property("text").toString() == "Fused");
    QVERIFY(findVisible("removeRunFusion"));
    capture("fusion-approved.png");
    QCOMPARE(warnings.size(), 0);
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
        QUrl::fromLocalFile(QStringLiteral(ANALYSIS_PANEL_QML_PATH)));
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
        QFileInfo(QStringLiteral(ANALYSIS_PANEL_QML_PATH)).dir().filePath("Main.qml")));
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

void TelemetryTests::showsSideBySideLapVideo()
{
    // KAN-107: each lap of an A/B pair from different runs is shown on its
    // own run's footage at the same track point: the active run's loaded
    // video, the other run's saved video verified in the background (here in
    // two chapters). A run without footage says so and never blocks the other.
    const QString ffmpeg = FfmpegTools::ffmpegPath();
    if (ffmpeg.isEmpty()) QSKIP("FFmpeg is unavailable for the side-by-side video test.");
    QTemporaryDir directory; QVERIFY(directory.isValid());
    QSettings settings; settings.clear(); settings.sync();
    const auto encode = [&](const QString &name, const int seconds) {
        QProcess encoder;
        encoder.start(ffmpeg, {"-hide_banner", "-loglevel", "error", "-y", "-f", "lavfi", "-i",
            QString("testsrc2=s=160x90:r=5:d=%1").arg(seconds), "-c:v", "libx264", "-preset", "ultrafast", "-pix_fmt", "yuv420p",
            directory.filePath(name)});
        return encoder.waitForFinished(60'000) && encoder.exitCode() == 0;
    };
    QVERIFY(encode("run-a.mp4", 220));
    QVERIFY(encode("GX010400.MP4", 110));
    QVERIFY(encode("GX020400.MP4", 110));
    const auto fullA = directory.filePath("full-a.vbo"), fullB = directory.filePath("full-b.vbo");
    QVERIFY(writeBytes(fullA, withVelocity(fullM4Vbo(true, 140, 80, 1.0))));
    QVERIFY(writeBytes(fullB, withVelocity(fullM4Vbo(false, 150, 85, 0.9))));
    AppController controller(nullptr, directory.filePath("recovery.json"));
    QVERIFY(controller.importAnalysisRuns("Video day", {QUrl::fromLocalFile(fullA), QUrl::fromLocalFile(fullB)}));
    QTRY_VERIFY_WITH_TIMEOUT(controller.eventRuns().size() == 2 && !controller.outingLapsLoading(), 30000);
    QTRY_COMPARE_WITH_TIMEOUT(controller.vboLoadState(), QString("ready"), 30000);
    const auto runs = controller.eventRuns();
    const auto runA = runs[0].toMap().value("id").toString(), runB = runs[1].toMap().value("id").toString();
    // Run A (active) gets an ordinary video.
    QCOMPARE(controller.activeRunId(), runA);
    controller.loadVideo(QUrl::fromLocalFile(directory.filePath("run-a.mp4")));
    QTRY_COMPARE_WITH_TIMEOUT(controller.videoLoadState(), QString("ready"), 30000);
    const auto projectPath = directory.filePath("video-day.fetproject");
    QVERIFY(controller.saveProject(QUrl::fromLocalFile(projectPath)));

    // A comparable lap from each run, once the laps have settled.
    QVariantMap lapA, lapB;
    const auto openPair = [&] {
        QTRY_VERIFY_WITH_TIMEOUT(!controller.outingLapsLoading() && !controller.outingComparisonGroupId().isEmpty(), 30000);
        lapA.clear(); lapB.clear();
        for (const auto &value : controller.outingLaps()) {
            const auto lap = value.toMap();
            if (lap.value("type") != "LAP" || !lap.value("referenceEligible").toBool() || !lap.value("compatibilityResolved").toBool())
                continue;
            if (lap.value("runId") == runA && lapA.isEmpty()) lapA = lap;
            if (lap.value("runId") == runB && lapB.isEmpty()) lapB = lap;
        }
        QVERIFY(!lapA.isEmpty() && !lapB.isEmpty());
        QVERIFY(controller.selectComparisonLap(0, lapA.value("reference").toMap()));
        QVERIFY(controller.selectComparisonLap(1, lapB.value("reference").toMap()));
        QTRY_VERIFY_WITH_TIMEOUT(controller.comparisonPairReady(), 30000);
    };
    openPair();
    // Run B has no footage yet: said so, and A still maps.
    QTRY_COMPARE_WITH_TIMEOUT(controller.comparisonVideo(1).value("state").toString(), QString("novideo"), 20000);
    QCOMPARE(controller.comparisonVideo(0).value("state").toString(), QString("ready"));
    const double axis = controller.comparisonProgressAxisLength();
    QVERIFY(axis > 100.0);
    const auto atA = controller.comparisonVideoAtProgress(0, axis / 2);
    QCOMPARE(QFileInfo(atA.value("url").toUrl().toLocalFile()).fileName(), QString("run-a.mp4"));
    QVERIFY(std::abs(controller.comparisonProgressForVideo(0, 0, atA.value("localMilliseconds").toDouble()) - axis / 2) < 2.0);
    QVERIFY(controller.comparisonVideoAtProgress(1, axis / 2).isEmpty());

    // Run B gets its footage in two chapters.
    QVERIFY(controller.selectEventRun(runB));
    QTRY_COMPARE_WITH_TIMEOUT(controller.activeRunId(), runB, 20000);
    QTRY_COMPARE_WITH_TIMEOUT(controller.vboLoadState(), QString("ready"), 30000);
    controller.loadVideoChapters({QUrl::fromLocalFile(directory.filePath("GX010400.MP4")),
        QUrl::fromLocalFile(directory.filePath("GX020400.MP4"))});
    QTRY_COMPARE_WITH_TIMEOUT(controller.videoLoadState(), QString("ready"), 30000);
    QVERIFY(controller.videoChaptered());
    QVERIFY(controller.saveProject(QUrl::fromLocalFile(projectPath)));
    QTRY_VERIFY_WITH_TIMEOUT(!controller.outingLapsLoading(), 30000);
    openPair();
    // Now run A is the inactive one: its saved video is verified in the background.
    QTRY_COMPARE_WITH_TIMEOUT(controller.comparisonVideo(0).value("state").toString(), QString("ready"), 30000);
    QTRY_COMPARE_WITH_TIMEOUT(controller.comparisonVideo(1).value("state").toString(), QString("ready"), 30000);
    QCOMPARE(controller.comparisonVideo(1).value("chapters").toInt(), 2);
    // Every point of B's lap maps to the right chapter and back to the same progress.
    for (double meters = axis * 0.1; meters < axis * 0.95; meters += axis * 0.2) {
        for (int slot = 0; slot < 2; ++slot) {
            const auto at = controller.comparisonVideoAtProgress(slot, meters);
            if (at.isEmpty()) continue; // this lap's projection may not reach every point
            const double back = controller.comparisonProgressForVideo(slot, at.value("chapter").toInt(), at.value("localMilliseconds").toDouble());
            QVERIFY2(std::abs(back - meters) < 2.0, qPrintable(QString("slot %1 at %2 m came back at %3 m").arg(slot).arg(meters).arg(back)));
            if (slot == 1) QVERIFY(at.value("localMilliseconds").toDouble() <= 110'500.0);
        }
    }

    // In the comparison view: the Video column with both panes.
    QQmlEngine engine; engine.rootContext()->setContextProperty("appController", &controller);
    QSignalSpy warnings(&engine, &QQmlEngine::warnings);
    QQmlComponent component(&engine, QUrl::fromLocalFile(
        QFileInfo(QStringLiteral(ANALYSIS_PANEL_QML_PATH)).dir().filePath("AnalysisWindow.qml")));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    std::unique_ptr<QObject> object(component.createWithInitialProperties({{"videoSource", QUrl{}},
        {"playbackPosition", 0}, {"playbackRunning", false}, {"mediaDuration", 0}}));
    auto *window = qobject_cast<QQuickWindow *>(object.get()); QVERIFY(window);
    window->resize(1180, 720);
    controller.setAnalysisVisible(true);
    controller.setComparisonViewOpen(true);
    window->show(); QVERIFY(QTest::qWaitForWindowExposed(window));
    auto *toggle = window->findChild<QQuickItem *>("comparisonToggleVideo");
    QTRY_VERIFY(toggle && toggle->isVisible());
    toggle->forceActiveFocus(); QTest::keyClick(window, Qt::Key_Space);
    auto *panel = window->findChild<QQuickItem *>("comparisonVideoPanel"); QVERIFY(panel);
    QTRY_VERIFY(panel->isVisible());
    const auto findVisual = [](auto &&self, QQuickItem *item, const QString &name) -> QQuickItem * {
        if (item->objectName() == name) return item;
        for (auto *child : item->childItems()) if (auto *found = self(self, child, name)) return found;
        return nullptr;
    };
    QQuickItem *paneB = nullptr;
    QTRY_VERIFY((paneB = findVisual(findVisual, window->contentItem(), "comparisonVideoPane1")));
    auto *detail = window->findChild<QQuickItem *>("comparisonDetailPanel"); QVERIFY(detail);
    detail->setProperty("hoverDistanceMeters", axis / 2);
    QTRY_VERIFY_WITH_TIMEOUT(paneB->property("covered").toBool(), 15000);
    QVERIFY(findVisual(findVisual, window->contentItem(), "comparisonVideoPlay")->isEnabled());
    // Both panes show a real frame of their own footage.
    for (int slot = 0; slot < 2; ++slot) {
        auto *output = findVisual(findVisual, window->contentItem(), QString("comparisonVideoOutput%1").arg(slot));
        QVERIFY(output);
        auto *sink = output->property("videoSink").value<QVideoSink *>();
        QVERIFY(sink);
        if (!QTest::qWaitFor([&] { return sink->videoFrame().isValid(); }, 15000))
            QSKIP("Media playback is unavailable here.");
    }
    const QString review = qEnvironmentVariable("FLAPPEDEAR_LAYOUT_REVIEW_DIR");
    if (!review.isEmpty()) { QTest::qWait(1500); static_cast<void>(window->grabWindow().save(QDir(review).filePath("ab-video.png"))); }
    QVERIFY(unreachableControls(window).isEmpty());
    // Play: lap A runs in real time and the shared cursor (charts, map, lap B) follows its track position.
    auto *play = findVisual(findVisual, window->contentItem(), "comparisonVideoPlay");
    play->forceActiveFocus(); QTest::keyClick(window, Qt::Key_Space);
    QTRY_VERIFY_WITH_TIMEOUT(detail->property("hoverDistanceMeters").toDouble() > axis / 2 + 20.0, 10000);
    play->forceActiveFocus(); QTest::keyClick(window, Qt::Key_Space);
    QTRY_VERIFY(!detail->property("videoPlaying").toBool());
    QCOMPARE(warnings.size(), 0);
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
    const int custom = source.addWidget("customValue");
    source.setSetting(custom, "source", "oiltemp");
    source.setSetting(custom, "label", "Oil temperature");
    source.setWidgetProperty(custom, "rotation", 12.0);

    WidgetModel restored;
    QVERIFY(restored.fromJson(source.toJson()));
    QCOMPARE(restored.count(), source.count());
    const QVariantMap widget = restored.widget(custom);
    QCOMPARE(widget.value("type").toString(), QString("customValue"));
    QCOMPARE(widget.value("rotation").toDouble(), 12.0);
    QCOMPARE(widget.value("settings").toMap().value("source").toString(), QString("oiltemp"));
}

void TelemetryTests::normalizesWidgetSemanticsAcrossMutationAndImport()
{
    WidgetModel edited;
    const int editedIndex = edited.addWidget(QStringLiteral("rpm"));
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
                    {QStringLiteral("type"), QStringLiteral("rpm")},
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
                 .value(QStringLiteral("maxValue")).toDouble(), 8000.0);
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
        {QStringLiteral("type"), QStringLiteral("rpm")},
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
                    {QStringLiteral("type"), QStringLiteral("rpm")}},
    };
    QVERIFY(!model.fromJson(duplicates));
}

void TelemetryTests::loadsVisualTemplates()
{
    WidgetModel model;
    QVERIFY(model.templates().size() >= 3);
    QVERIFY(model.applyTemplate("minimal"));
    QCOMPARE(model.count(), 2);
    QCOMPARE(model.widget(0).value("type").toString(), QString("speed"));
    QCOMPARE(
        model.widget(0).value("settings").toMap().value("showBackground").toBool(),
        false);
    QVERIFY(model.applyTemplate("performance"));
    QCOMPARE(model.count(), 3);
    QCOMPARE(model.widget(0).value("type").toString(), QString("arcGauge"));
    QCOMPARE(model.widget(1).value("type").toString(), QString("dialGauge"));
    QCOMPARE(model.widget(2).value("type").toString(), QString("telemetryOverlay"));
    QVERIFY(model.applyTemplate("2000s-grand-prix"));
    QCOMPARE(model.count(), 6);
    QCOMPARE(model.widget(0).value("type").toString(), QString("retroTachometer"));
    QCOMPARE(model.widget(1).value("type").toString(), QString("retroGear"));
    QCOMPARE(model.widget(2).value("type").toString(), QString("retroPedal"));
    QCOMPARE(model.widget(3).value("settings").toMap().value("source").toString(), QString("brake_pos-obd"));
    QVERIFY(model.applyTemplate("motorsport-broadcast-smoke"));
    QCOMPARE(model.count(), 9);
    QCOMPARE(model.widget(0).value("type").toString(), QString("retroTachometer"));
    QCOMPARE(model.widget(6).value("type").toString(), QString("heartRate"));
    QCOMPARE(model.widget(7).value("type").toString(), QString("f1GForceRadar"));
    QCOMPARE(model.widget(8).value("type").toString(), QString("gForceMagnitudeBar"));
    QCOMPARE(model.widget(3).value("settings").toMap().value("stackPosition").toString(), QString("top"));
    QCOMPARE(model.widget(4).value("settings").toMap().value("stackPosition").toString(), QString("middle"));
    QCOMPARE(model.widget(5).value("settings").toMap().value("stackPosition").toString(), QString("bottom"));
    QVERIFY(!model.applyTemplate("missing-template"));
}

void TelemetryTests::providesCustomizableArchetypes()
{
    WidgetModel model;
    const QHash<QString, QStringList> specialized = {
        {"speed", {"showGauge", "unit", "maxValue"}},
        {"rpm", {"showBar", "warningValue", "maxValue"}},
        {"heartRate", {"showIcon", "unit", "accentColor"}},
        {"pedals", {"acceleratorSource", "brakeSource", "acceleratorColor", "brakeColor"}},
        {"gForce", {"lateralSource", "longitudinalSource", "invertLateral", "invertLongitudinal", "gRange", "gridColor"}},
        {"f1GForceRadar", {"lateralSource", "longitudinalSource", "invertLateral", "invertLongitudinal", "maxG", "ringStepG", "showCrosshair", "showCenterBox", "showRingLabels", "radarBackgroundColor", "dotColor", "gridColor"}},
        {"gForceMagnitudeBar", {"lateralSource", "longitudinalSource", "invertLateral", "invertLongitudinal", "maxG", "labelText", "showLabel", "showValue", "barColor", "barBackgroundColor", "barRadius"}},
        {"track", {"lineColor", "lineWidth", "markerColor", "mirrorX", "mirrorY"}},
        {"customValue", {"label", "decimals", "multiplier"}},
        {"retroCustomValue", {"source", "label", "fallbackText", "panelColor", "valueColor", "labelColor", "icon", "stackPosition", "showSeparator"}},
        {"arcGauge", {"source", "startAngle", "endAngle", "arcWidth", "trackColor"}},
        {"dialGauge", {"source", "startAngle", "endAngle", "majorTicks", "needleColor"}},
        {"telemetryOverlay", {"source1", "source2", "source3", "source4", "columns"}},
        {"retroGrandPrix", {"rpmSource", "speedSource", "gearSource", "throttleSource", "brakeSource", "driverName"}},
        {"retroTachometer", {"source", "minValue", "maxValue", "needleColor"}},
        {"retroGear", {"source", "label", "fallbackText", "panelColor"}},
        {"retroPedal", {"source", "minValue", "maxValue", "fillColor", "emptyColor"}},
        {"retroSpeedArc", {"source", "minValue", "maxValue", "segments", "lowColor"}},
        {"retroNameplate", {"topSource", "bottomSource", "topText", "bottomText"}},
        {"brandLogo", {"logoOpacity", "logoScale"}},
        {"tyres", {"label", "showTemperature", "showPressure", "pressureUnit", "coldBelow", "hotAbove"}},
    };
    for (auto iterator = specialized.cbegin(); iterator != specialized.cend(); ++iterator) {
        const int index = model.addWidget(iterator.key());
        QVERIFY(index >= 0);
        const QVariantMap settings = model.widget(index).value("settings").toMap();
        for (const QString &common : {
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
    QVERIFY(source.applyTemplate("minimal"));
    const QString templateId = source.saveCurrentAsTemplate("My layout", "Keep this description");
    QVERIFY(!templateId.isEmpty());
    const int templateCount = source.templates().size();
    source.setSetting(0, "fontSize", 48);
    source.setSetting(0, "futureCompatibleSetting", "preserve me");
    QCOMPARE(source.addWidget("retroCustomValue"), 2);
    QVERIFY(source.updateTemplate(templateId));
    QCOMPARE(source.templates().size(), templateCount);
    QVERIFY(!source.updateTemplate("minimal"));

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
    QVERIFY(model.applyTemplate("minimal"));
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
    QVERIFY(source.applyTemplate("minimal"));
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
    QVERIFY(model.applyTemplate("minimal"));
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
        QVERIFY(restarted.applyTemplate("minimal")); // Built-ins remain usable.
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
    const int gear = source.addWidget("retroGear");
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
    const int gForce = source.addWidget("gForce");
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
    QVERIFY(source.applyTemplate("2000s-grand-prix"));
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
    const int widget = source.addWidget("telemetryOverlay");
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
    const int first = model.addWidget("retroGear");
    const int second = model.addWidget("retroPedal");
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

void TelemetryTests::preservesLongitudeConventionInLapDetail()
{
    QTemporaryDir directory; QVERIFY(directory.isValid());
    QSettings settings; settings.clear(); settings.sync();
    const auto path = directory.filePath("west-positive.vbo");
    const QByteArray recording =
        "[comments]\nGenerated by RaceChrono Pro v10.2.4\n[column names]\ntime latitude longitude velocity\n[data]\n"
        "0 3120 -1260 70\n1 3120 -1263.75 71\n2 3121.875 -1263.75 72\n3 3120.9375 -1260.9375 73\n";
    QVERIFY(writeBytes(path, recording));
    AppController controller(nullptr, directory.filePath("recovery.json"));
    QVERIFY(controller.importAnalysisRuns("Map orientation", {QUrl::fromLocalFile(path)}));
    QTRY_COMPARE(controller.outingLaps().size(), 1);
    QTRY_COMPARE(controller.vboLoadState(), QStringLiteral("ready"));
    const auto document = controller.currentProjectObject();
    QVERIFY(controller.selectOutingLap(0));
    QTRY_COMPARE(controller.outingLapDetailState(), QStringLiteral("ready"));
    const auto track = controller.outingLapTrack();
    QCOMPARE(track.size(), 1);
    const auto points = track.first().toList();
    QCOMPARE(points.size(), 4);
    QVERIFY(points[1].toMap().value("x").toDouble() > points[0].toMap().value("x").toDouble());
    QVERIFY(points[2].toMap().value("y").toDouble() < points[1].toMap().value("y").toDouble());
    controller.setOutingLapCursor(1);
    QCOMPARE(controller.outingLapTrackPoint(), points[1].toMap());
    QCOMPARE(controller.currentTrackPoint().value("x").toDouble(), points[0].toMap().value("x").toDouble());
    QCOMPARE(controller.outingLapTrack(), track);
    QCOMPARE(controller.currentProjectObject(), document);
    QCOMPARE(readBytes(path), recording);
}

void TelemetryTests::persistsAndInvalidatesRunTrackConfiguration()
{
    QTemporaryDir directory; QVERIFY(directory.isValid());
    QSettings settings; settings.clear(); settings.sync();
    const auto path = directory.filePath("run.vbo");
    QVERIFY(writeBytes(path, EventProjectFixture::lapsVbo()));
    AppController controller(nullptr, directory.filePath("recovery.json"));
    QVERIFY(controller.importAnalysisRuns("Identity", {QUrl::fromLocalFile(path)}));
    QTRY_COMPARE(controller.vboLoadState(), QString("ready"));
    QTRY_VERIFY(!controller.outingLaps().isEmpty());
    const auto runId = controller.activeRunId();
    auto run = EventProjectFixture::runs(controller.currentProjectObject())[0].toObject();
    const auto imported = EventProjectCodec::trackConfiguration(run);
    QVERIFY(imported.value("layoutId").isNull());
    QCOMPARE(imported.value("direction").toString(), QString("unknown"));
    QCOMPARE(imported.value("gateRevision").toString(), timingGateRevision(*controller.m_session));
    const auto before = controller.currentProjectObject();
    QVERIFY(!controller.setRunTrackConfiguration("foreign-run", "layout", "clockwise"));
    QVERIFY(!controller.setRunTrackConfiguration(runId, "layout", "forward"));
    QCOMPARE(controller.currentProjectObject(), before);
    QVERIFY(controller.selectOutingLap(0));
    QTRY_COMPARE(controller.outingLapDetailState(), QString("ready"));
    const auto generation = controller.m_document.m_sourceGeneration;
    const auto oldKey = controller.m_analysis.outingLapKey();
    QVERIFY(controller.setRunTrackConfiguration(runId, "jastrzab-full", "clockwise"));
    QCOMPARE(controller.outingLapDetailState(), QString("idle"));
    QVERIFY(controller.m_analysis.outingLapKey() != oldKey);
    QCOMPARE(controller.m_document.m_sourceGeneration, generation); // Metadata edits do not reload the editor.
    QTRY_VERIFY(!controller.outingLapsLoading() && !controller.outingLaps().isEmpty());
    QVERIFY(controller.dirty());
    const auto revision = controller.m_document.m_documentState.revision();
    QVERIFY(controller.setRunTrackConfiguration(runId, "jastrzab-full", "clockwise"));
    QCOMPARE(controller.m_document.m_documentState.revision(), revision);
    const auto savedPath = directory.filePath("identity.fetproject");
    QVERIFY(controller.saveProject(QUrl::fromLocalFile(savedPath)));
    const auto saved = QJsonDocument::fromJson(readBytes(savedPath)).object();
    AppController reopened(nullptr, directory.filePath("reopened-recovery.json"));
    QVERIFY(reopened.m_document.beginProjectLoad(savedPath, saved));
    QTRY_COMPARE(reopened.vboLoadState(), QString("ready"));
    run = EventProjectFixture::runs(reopened.currentProjectObject())[0].toObject();
    const auto config = EventProjectCodec::trackConfiguration(run);
    QCOMPARE(config.value("layoutId").toString(), QString("jastrzab-full"));
    QCOMPARE(config.value("direction").toString(), QString("clockwise"));
    QCOMPARE(config.value("gateRevision"), imported.value("gateRevision"));
    // Simulate an asserted gate revision edit without changing the recording:
    // cache identity changes and the worker must refuse stale gate metadata.
    auto stale = reopened.currentProjectObject(); auto runs = EventProjectFixture::runs(stale);
    run = runs[0].toObject(); auto staleConfig = config;
    staleConfig.insert("gateRevision", "gates-v1:" + QString(64, '0'));
    run.insert("trackConfiguration", staleConfig); runs[0] = run; EventProjectFixture::setRuns(stale, runs);
    const auto oldGateKey = reopened.m_analysis.outingLapKey();
    reopened.m_document.m_projectTemplate = stale; reopened.markPersistentChange();
    QVERIFY(reopened.m_analysis.outingLapKey() != oldGateKey);
    QTRY_VERIFY(!reopened.outingLapsLoading() && reopened.outingLapMessages().join(" ").contains("Timing-gate revision"));
    QVERIFY(reopened.outingLaps().isEmpty());
    // Replacing the source clears the source-bound assertions in the real editor path.
    reopened.loadVbo(QUrl::fromLocalFile(QStringLiteral(TEST_FIXTURE_PATH)));
    QTRY_COMPARE(reopened.vboLoadState(), QString("ready"));
    run = EventProjectFixture::runs(reopened.currentProjectObject())[0].toObject();
    const auto unknown = EventProjectCodec::trackConfiguration(run);
    QVERIFY(unknown.value("layoutId").isNull()); QVERIFY(unknown.value("gateRevision").isNull());
    QCOMPARE(unknown.value("direction").toString(), QString("unknown"));
    QString error; QVERIFY2(ProjectLimits::validateProject(reopened.currentProjectObject(), &error), qPrintable(error));
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

void TelemetryTests::keepsStaticTrackIndependentFromTime()
{
    QFile source(QStringLiteral(TRACK_WIDGET_QML_PATH));
    QVERIFY2(source.open(QIODevice::ReadOnly), qPrintable(source.errorString()));
    const QByteArray qml = source.readAll();
    QVERIFY(qml.contains("frame.renderContext.trackRevision"));
    QVERIFY(qml.contains("PathPolyline"));
    QVERIFY(!qml.contains("function onTimeChanged()"));
    QVERIFY(!qml.contains("requestPaint"));
    QVERIFY(!qml.contains("onRevisionChanged"));
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
    AppController controller;
    controller.loadVbo(QUrl::fromLocalFile(QStringLiteral(TEST_FIXTURE_PATH)));
    QTRY_COMPARE(controller.vboLoadState(), QStringLiteral("ready"));
    QVERIFY(controller.dirty());

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
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
    controller.setAnalysisVisible(true);
    QVERIFY(controller.analysisVisible());

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
    QVERIFY(!controller.analysisVisible());
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
    settings.setValue(QStringLiteral("analysis/windowWidth"), 777);
    settings.sync();

    {
        AppController controller(nullptr, recoveryPath);
        QTRY_VERIFY(!controller.projectLoading());
        QCOMPARE(controller.syncOffset(), 1.25);
        QVERIFY(!controller.dirty());
        QVERIFY(!controller.analysisVisible());
        controller.setAnalysisVisible(true);
        QVERIFY(!controller.dirty());
        controller.setAnalysisVisible(false);
        QVERIFY(!controller.dirty());
        QCOMPARE(controller.analysisWindowWidth(), 777);
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
        controller.widgetModel()->addWidget(QStringLiteral("customValue"));
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
        controller.widgetModel()->addWidget(QStringLiteral("customValue"));
        controller.setSyncOffset(8.0);
        controller.setAnalysisVisible(false);
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
        QVERIFY(!controller.analysisVisible());
        QVERIFY(!controller.dirty());

        controller.setSyncOffset(8.0);
        QTRY_VERIFY(QFileInfo(recoveryPath).isFile());
        controller.requestNewProject();
        controller.resolveDestructiveAction(QStringLiteral("discard"));
        QCOMPARE(controller.syncOffset(), 0.0);
        QVERIFY(controller.projectPath().isEmpty());
        QVERIFY(!controller.analysisVisible());
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
        QStringLiteral("Flapped Ear Overlays Export Log\nExport ID: a83f91c2d4e5f678"), &error, started);
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
            for (int c = 0; c < 3; ++c) pixels[((frame * height + y) * width + x) * 3 + c] = (frame & (1 << bit)) ? char(255) : char(0);
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
    QTest::newRow("arcGauge") << QStringLiteral("arcGauge");
    QTest::newRow("dialGauge") << QStringLiteral("dialGauge");
    QTest::newRow("retroGrandPrix") << QStringLiteral("retroGrandPrix");
    QTest::newRow("retroSpeedArc") << QStringLiteral("retroSpeedArc");
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

void TelemetryTests::rendersAllComparisonTilesInProductionScene()
{
    TelemetrySession session = speedSession(0.0, 2.0, 0.0);
    WidgetModel widgets;
    const QStringList types{
        QStringLiteral("lapBest"), QStringLiteral("lapCurrent"), QStringLiteral("lapDelta"),
        QStringLiteral("speedBest"), QStringLiteral("speedCurrent"), QStringLiteral("speedDelta")};
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
                 qPrintable(QStringLiteral("%1 did not create an opaque comparison tile at %2,%3")
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
    encoderQuery.start(ffmpeg, {QStringLiteral("-hide_banner"), QStringLiteral("-h"),
                                QStringLiteral("encoder=hevc_videotoolbox")});
    if (!encoderQuery.waitForStarted() || !encoderQuery.waitForFinished(10'000)
        || encoderQuery.exitCode() != 0) {
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
