#include "app/AppController.h"
#include "app/PreviewPlayback.h"
#include "export/ExportFormat.h"
#include "export/ExportEngine.h"
#include "export/VideoFingerprint.h"
#include "export/ExportCancellation.h"
#include "export/ExportMediaProfile.h"
#include "app/AppLog.h"

#include "gopro/GoProTelemetrySource.h"
#include "export/ExportArtifactManifest.h"
#include "export/PersistentExportLog.h"
#include "project/BoundedJsonLoader.h"
#include "project/ProjectLimits.h"
#include "project/EventProjectCodec.h"
#include "sync/TelemetrySyncEngine.h"
#include "telemetry/VboParser.h"
#include "telemetry/TelemetrySource.h"

#include <QFileInfo>
#include <QClipboard>
#include <QGuiApplication>
#include <QFontDatabase>
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QDateTime>
#include <QStandardPaths>
#include <QScopedValueRollback>
#include <QPointer>
#include <QTimer>
#include <QElapsedTimer>
#include <QThread>
#include <QUuid>
#include <QtConcurrent>
#include <QtGlobal>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <optional>
#include <utility>

namespace FlappedEar {

namespace {

constexpr int RecoveryWriteDelayMs = 250;
constexpr int RecoveryRetryDelayMs = 5'000;

std::optional<qint64> frameAtOrBeforePresentationTime(
    const double seconds, const MediaRational &frameRate)
{
    if (!std::isfinite(seconds) || seconds < 0.0 || !frameRate.isValid()) return std::nullopt;
    const long double frame = static_cast<long double>(seconds)
        * static_cast<long double>(frameRate.numerator) / static_cast<long double>(frameRate.denominator);
    if (frame < 0.0L || frame > static_cast<long double>(std::numeric_limits<qint64>::max())) {
        return std::nullopt;
    }
    // This selects the frame that contains the requested presentation time. The
    // resulting inclusive frame range is still the sole export authority.
    return static_cast<qint64>(frame);
}

struct SavedDocumentMetadata final {
    QString id;
    quint64 revision = 0;
};

bool parseSavedDocumentMetadata(const QJsonObject &project, SavedDocumentMetadata *metadata)
{
    const QJsonValue stateValue = project.value(QStringLiteral("documentState"));
    if (!stateValue.isObject()) return false;
    const QJsonObject state = stateValue.toObject();
    const QString id = state.value(QStringLiteral("id")).toString();
    if (id.isEmpty() || id.size() > 128 || !state.value(QStringLiteral("savedRevision")).isString()) {
        return false;
    }
    bool revisionOk = false;
    const quint64 revision = state.value(QStringLiteral("savedRevision")).toString().toULongLong(&revisionOk);
    if (!revisionOk) return false;
    if (metadata) *metadata = {id, revision};
    return true;
}

std::optional<SavedDocumentMetadata> loadSavedDocumentMetadata(const QString &path)
{
    if (path.isEmpty() || !QFileInfo(path).isFile()) return std::nullopt;
    const auto loaded = BoundedJsonLoader::loadFile(
        path, ProjectLimits::projectBytes, QStringLiteral("Saved project"));
    SavedDocumentMetadata metadata;
    QString validationError;
    if (!loaded.success() || !loaded.document.isObject()
        || !ProjectLimits::validateProject(loaded.document.object(), &validationError)
        || !parseSavedDocumentMetadata(loaded.document.object(), &metadata)) {
        return std::nullopt;
    }
    return metadata;
}

enum class RecoveryValidity {
    Valid,
    Stale,
    Invalid,
};

RecoveryValidity recoveryValidity(const ProjectRecoverySnapshot &snapshot,
                                  const QString &rememberedProjectPath)
{
    if (!snapshot.hasLogicalMetadata) return RecoveryValidity::Valid;
    if (snapshot.revision <= snapshot.lastSavedRevision) return RecoveryValidity::Stale;

    QStringList authorityPaths;
    if (!rememberedProjectPath.isEmpty()) authorityPaths.append(rememberedProjectPath);
    if (!snapshot.originalProjectPath.isEmpty()
        && !authorityPaths.contains(snapshot.originalProjectPath)) {
        authorityPaths.append(snapshot.originalProjectPath);
    }
    bool foundAuthority = false;
    for (const QString &path : authorityPaths) {
        const auto metadata = loadSavedDocumentMetadata(path);
        if (!metadata) continue;
        foundAuthority = true;
        if (metadata->id != snapshot.documentId) continue;
        return snapshot.revision <= metadata->revision
            ? RecoveryValidity::Stale : RecoveryValidity::Valid;
    }
    return foundAuthority ? RecoveryValidity::Invalid : RecoveryValidity::Valid;
}

} // namespace

AppController::AppController(QObject *parent, QString recoveryPath,
                             ProjectRecoveryStore::Operations recoveryOperations)
    : QObject(parent)
    , m_settings()
    , m_previewRenderContext(this)
    , m_recoveryStore(std::move(recoveryPath), std::move(recoveryOperations))
{
    m_documentId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_sync = {};
    m_previewRenderContext.setSyncTransform(m_sync);
    m_widgetModel.resetDefaults();
    m_recoveryTimer.setSingleShot(true);
    m_recoveryTimer.setInterval(RecoveryWriteDelayMs);
    connect(&m_recoveryTimer, &QTimer::timeout, this, &AppController::writeRecoverySnapshot);
    connect(&m_widgetModel, &WidgetModel::revisionChanged, this, [this] {
        markPersistentChange();
    });
    connect(&m_widgetModel, &WidgetModel::lastErrorChanged, this, [this] {
        if (!m_widgetModel.lastError().isEmpty()) setStatus(m_widgetModel.lastError());
    });
    m_selectedTemplateId = m_settings.value(QStringLiteral("ui/selectedTemplateId")).toString();
    connect(&m_widgetModel, &WidgetModel::templatesChanged, this, [this] {
        reconcileTemplateSelection();
        if (templateIndexForId(m_activeTemplateId) < 0) {
            clearActiveTemplate();
        }
    });
    reconcileTemplateSelection();
    connect(&m_syncWatcher, &QFutureWatcher<AutoSyncResult>::finished, this, [this] {
        const AutoSyncResult result = m_syncWatcher.result();
        emit syncingChanged();
        if (result.generation != m_sourceGeneration
            || result.syncRevision != m_syncRevision
            || result.videoPath != normalizedSourcePath(m_videoSource.toLocalFile())
            || result.vboPath != normalizedSourcePath(m_telemetryPath)) {
            AppLog::warn(QStringLiteral("Stale auto-sync result rejected"));
            return;
        }
        if (!result.success) {
            if (result.cancelled) {
                AppLog::warn(QStringLiteral("Auto-sync cancelled"));
                setStatus(QStringLiteral("Auto sync cancelled."));
                return;
            }
            AppLog::error(QStringLiteral("Auto-sync failed: %1").arg(result.error));
            m_syncCandidate.clear();
            emit syncCandidateChanged();
            setStatus(QStringLiteral("Auto sync failed: %1").arg(result.error));
            return;
        }
        const bool automaticallyApplied = shouldAutoApplySyncCandidate(result.candidate);
        if (automaticallyApplied) {
            setSyncOffset(result.candidate.offset);
            setTimeScale(result.candidate.timeScale);
        }
        m_syncCandidate = {
            {"offset", result.candidate.offset},
            {"timeScale", result.candidate.timeScale},
            {"confidence", result.candidate.confidence},
            {"correlation", result.candidate.diagnostics.correlation},
            {"peakUniqueness", result.candidate.diagnostics.peakUniqueness},
            {"validSamples", result.candidate.diagnostics.validSamples},
            {"coarseOffset", result.candidate.diagnostics.coarseOffset},
            {"packetCount", result.packetCount},
            {"gpsSampleCount", result.gpsSampleCount},
            {"gpsStream", result.gpsStream},
            {"level", syncCandidateLevelName(result.candidate.confidence)},
            {"automaticallyApplied", automaticallyApplied},
            {"canApply", true},
        };
        emit syncCandidateChanged();
        AppLog::info(QStringLiteral("Auto-sync result: offset=%1 s, confidence=%2%, %3")
                         .arg(result.candidate.offset, 0, 'f', 3)
                         .arg(result.candidate.confidence * 100.0, 0, 'f', 0)
                         .arg(automaticallyApplied ? QStringLiteral("applied")
                                                   : QStringLiteral("review required")));
        setStatus(QStringLiteral("Auto sync %1: %2 s · correlation %3 · confidence %4% · %5 GPS samples")
                      .arg(automaticallyApplied ? QStringLiteral("applied")
                                                : QStringLiteral("candidate requires review"))
                      .arg(result.candidate.offset, 0, 'f', 3)
                      .arg(result.candidate.diagnostics.correlation, 0, 'f', 3)
                      .arg(result.candidate.confidence * 100.0, 0, 'f', 0)
                      .arg(result.gpsSampleCount));
    });
    connect(&m_videoProbeWatcher, &QFutureWatcher<VideoProbeResult>::finished, this, [this] {
        const VideoProbeResult result = m_videoProbeWatcher.result();
        if (result.generation != m_sourceGeneration) {
            AppLog::warn(QStringLiteral("Stale video probe result rejected: %1").arg(result.path));
            return;
        }
        if (!result.success) {
            if (result.cancelled) {
                AppLog::warn(QStringLiteral("Video probe cancelled: %1").arg(result.path));
                m_videoLoadState = QStringLiteral("idle");
                emit sourceLoadStateChanged();
                return;
            }
            AppLog::error(QStringLiteral("Video load failed: %1: %2").arg(result.path, result.error));
            m_videoLoadState = m_videoSource.isEmpty() ? QStringLiteral("error")
                                                       : QStringLiteral("ready");
            emit sourceLoadStateChanged();
            setStatus(QStringLiteral("Could not open video: %1\n%2").arg(result.path, result.error));
            return;
        }
        if (ProjectSourceReferenceCodec::compareFingerprints(
                result.expectedFingerprint, result.fingerprint)
            == SourceFingerprintMatch::Mismatch) {
            m_videoLoadState = QStringLiteral("mismatch");
            if (result.relink) {
                m_pendingMismatchVideo = result;
                m_pendingMismatchVbo = {};
                m_sourceMismatchType = QStringLiteral("video");
                emit sourceMismatchChanged();
            }
            emit sourceLoadStateChanged();
            setStatus(QStringLiteral("Video source does not match the project fingerprint."));
            return;
        }
        commitVideoProbe(result, m_videoLoadMarksDocumentDirty);
    });
    connect(&m_vboLoadWatcher, &QFutureWatcher<VboLoadResult>::finished, this, [this] {
        const VboLoadResult result = m_vboLoadWatcher.result();
        if (result.generation != m_sourceGeneration) {
            AppLog::warn(QStringLiteral("Stale Telemetry load result rejected: %1").arg(result.path));
            return;
        }
        if (!result.success) {
            if (result.cancelled) {
                AppLog::warn(QStringLiteral("Telemetry load cancelled: %1").arg(result.path));
                m_vboLoadState = QStringLiteral("idle");
                emit sourceLoadStateChanged();
                return;
            }
            AppLog::error(QStringLiteral("Telemetry load failed: %1: %2").arg(result.path, result.error));
            m_vboLoadState = m_session ? QStringLiteral("ready") : QStringLiteral("error");
            emit sourceLoadStateChanged();
            setStatus(QStringLiteral("Could not parse telemetry: %1\n%2").arg(result.path, result.error));
            return;
        }
        if (result.contentMismatch || ProjectSourceReferenceCodec::compareFingerprints(
                result.expectedFingerprint, result.fingerprint)
            == SourceFingerprintMatch::Mismatch) {
            m_vboLoadState = QStringLiteral("mismatch");
            if (result.relink) {
                m_pendingMismatchVbo = result;
                m_pendingMismatchVideo = {};
                m_sourceMismatchType = QStringLiteral("telemetry");
                emit sourceMismatchChanged();
            }
            emit sourceLoadStateChanged();
            setStatus(QStringLiteral("Telemetry source does not match the project fingerprint."));
            return;
        }
        commitVboLoad(result, m_vboLoadMarksDocumentDirty);
    });
    connect(&m_projectLoadWatcher, &QFutureWatcher<ProjectLoadResult>::finished, this, [this] {
        const ProjectLoadResult result = m_projectLoadWatcher.result();
        if (result.generation != m_sourceGeneration) {
            AppLog::warn(QStringLiteral("Stale project load result rejected: %1")
                             .arg(result.projectPath));
            return;
        }
        if (!result.success) {
            if (result.cancelled) {
                AppLog::warn(QStringLiteral("Project source load cancelled: %1")
                                 .arg(result.projectPath));
                setProjectLoadState(false);
                return;
            }
            AppLog::error(QStringLiteral("Project load failed: %1: %2")
                              .arg(result.projectPath, result.error));
            setProjectLoadState(false, {}, result.error);
            setStatus(QStringLiteral("Project could not be opened: %1").arg(result.error));
            return;
        }
        if (result.documentRevisionAtStart != m_documentState.revision()) {
            AppLog::warn(QStringLiteral("Stale project load rejected due to document revision: %1")
                             .arg(result.projectPath));
            setProjectLoadState(false, {}, QStringLiteral("document changed while project was loading."));
            setStatus(QStringLiteral("Project load cancelled because the current document changed."));
            return;
        }
        if (commitProjectLoad(result)) {
            startProjectSources(result);
        }
    });
    initializeBatchImport();
    initializeOutingLaps();
    initializeOutingLapDetail();
    initializeSegmentReview();
    initializeComparisonLaps();
    initializeOutingTheoreticalBest();
    initializeOutingChannelSummaries();
    initializeOutingDayReport();
    retireLegacyDocumentSettings();
    restoreStartupState();
}

AppController::~AppController()
{
    if (m_batchCancellation) m_batchCancellation->store(true);
    cancelSourceJobs();
    QElapsedTimer sourceShutdown;
    sourceShutdown.start();
    while (sourceShutdown.elapsed() < 2'000
           && (m_videoProbeWatcher.isRunning() || m_vboLoadWatcher.isRunning()
               || m_projectLoadWatcher.isRunning() || m_syncWatcher.isRunning() || m_batchWatcher.isRunning()
               || m_outingLapWatcher.isRunning() || m_outingLapDetailWatcher.isRunning() || m_comparisonWatcher.isRunning()
               || m_segmentReviewWatcher.isRunning())) {
        QThread::msleep(10);
    }
    if (m_videoProbeWatcher.isRunning() || m_vboLoadWatcher.isRunning()
        || m_projectLoadWatcher.isRunning() || m_syncWatcher.isRunning() || m_batchWatcher.isRunning()
        || m_outingLapWatcher.isRunning() || m_outingLapDetailWatcher.isRunning() || m_comparisonWatcher.isRunning()
        || m_segmentReviewWatcher.isRunning()) {
        AppLog::warn(QStringLiteral("Source worker shutdown exceeded the bounded wait"));
    }
    bool exportStopped = true;
    if (m_exportProcess) {
        // Blocking waits can emit finished/readyRead during destruction.
        disconnect(m_exportProcess.get(), nullptr, this, nullptr);
        QFile cancellationFile(m_exportCancelPath);
        if (cancellationFile.open(QIODevice::WriteOnly)) {
            cancellationFile.close();
        }
        exportStopped = m_exportSupervisor && m_exportSupervisor->stopAndWait();
    }
    if (!exportStopped) {
        if (m_exportOutputTransaction) m_exportOutputTransaction->deferCleanup();
        if (m_exportConfig) m_exportConfig->setAutoRemove(false);
        AppLog::warn(QStringLiteral("Export cleanup deferred: process tree shutdown was not confirmed"));
    }
    m_exportOutputTransaction.reset();
    // On abnormal destruction the manifest intentionally remains for startup
    // recovery. A normal finished callback performs the authorized cleanup.
    if (exportStopped && !m_exportCancelPath.isEmpty()) QFile::remove(m_exportCancelPath);
}

QUrl AppController::videoSource() const { return m_videoSource; }
QString AppController::videoName() const
{
    const QString path = m_videoSource.isEmpty() ? m_videoReference.displayPath()
                                                  : m_videoSource.toLocalFile();
    return QFileInfo(path).fileName();
}
QString AppController::telemetryName() const
{
    const QString path = m_telemetryPath.isEmpty() ? m_vboReference.displayPath() : m_telemetryPath;
    return QFileInfo(path).fileName();
}
QString AppController::statusText() const { return m_statusText; }
QStringList AppController::channelNames() const { return m_session ? m_session->channelNames() : QStringList(); }
qsizetype AppController::sampleCount() const { return m_session ? m_session->sampleCount : 0; }
double AppController::telemetryDuration() const { return m_session ? m_session->duration : 0.0; }
double AppController::playbackTime() const { return m_playbackTime; }
double AppController::syncOffset() const { return m_sync.offset; }
double AppController::timeScale() const { return m_sync.timeScale; }
bool AppController::syncing() const { return m_syncWatcher.isRunning(); }
bool AppController::exporting() const { return m_exportProcess != nullptr; }
int AppController::exportProgress() const { return m_exportProgress; }
QString AppController::exportState() const { return m_exportState; }
QString AppController::exportError() const { return m_exportError; }
QVariantMap AppController::exportSourceInfo() const
{
    if (!m_exportSourceInfo.videoSize.isValid()) {
        return {};
    }
    const MediaRational rate = m_exportSourceInfo.averageFrameRate.isValid()
        ? m_exportSourceInfo.averageFrameRate
        : m_exportSourceInfo.frameRate;
    const QString colorSummary = m_exportSourceInfo.sourceColorClass == SourceColorClass::Sdr
        && m_exportSourceInfo.colorPrimaries == QStringLiteral("bt709")
        ? QStringLiteral("Rec.709 SDR")
        : sourceColorClassName(m_exportSourceInfo.sourceColorClass);
    const bool unsupportedColorManagedSource =
        m_exportSourceInfo.sourceColorClass == SourceColorClass::HdrHlg
        || m_exportSourceInfo.sourceColorClass == SourceColorClass::HdrPq
        || m_exportSourceInfo.sourceColorClass == SourceColorClass::LogOrExtended;
    return {
        {"width", m_exportSourceInfo.videoSize.width()},
        {"height", m_exportSourceInfo.videoSize.height()},
        {"codedWidth", m_exportSourceInfo.codedVideoSize.width()},
        {"codedHeight", m_exportSourceInfo.codedVideoSize.height()},
        {"displayWidth", m_exportSourceInfo.displayVideoSize.width()},
        {"displayHeight", m_exportSourceInfo.displayVideoSize.height()},
        {"duration", m_exportSourceInfo.duration},
        {"frameRate", rate.value()},
        {"frameRateText", QStringLiteral("%1/%2 (%3 fps)")
                              .arg(rate.numerator)
                              .arg(rate.denominator)
                              .arg(rate.value(), 0, 'f', 3)},
        {"videoCodec", m_exportSourceInfo.videoCodec},
        {"videoCodecProfile", m_exportSourceInfo.videoCodecProfile},
        {"pixelFormat", m_exportSourceInfo.pixelFormat},
        {"bitDepth", m_exportSourceInfo.bitDepth
                         ? QVariant(*m_exportSourceInfo.bitDepth) : QVariant()},
        {"sourceVideoBitrate", m_exportSourceInfo.sourceVideoBitrate
                                  ? QVariant(*m_exportSourceInfo.sourceVideoBitrate) : QVariant()},
        {"sampleAspectRatio", m_exportSourceInfo.sampleAspectRatio.isValid()
                                  ? QStringLiteral("%1:%2")
                                        .arg(m_exportSourceInfo.sampleAspectRatio.numerator)
                                        .arg(m_exportSourceInfo.sampleAspectRatio.denominator)
                                  : QString()},
        {"rotationDegrees", m_exportSourceInfo.rotationDegrees
                                ? QVariant(*m_exportSourceInfo.rotationDegrees) : QVariant()},
        {"colorRange", m_exportSourceInfo.colorRange},
        {"colorSpace", m_exportSourceInfo.colorSpace},
        {"colorTransfer", m_exportSourceInfo.colorTransfer},
        {"colorPrimaries", m_exportSourceInfo.colorPrimaries},
        {"colorClass", sourceColorClassName(m_exportSourceInfo.sourceColorClass)},
        {"colorSummary", colorSummary},
        {"unsupportedColorManagedSource", unsupportedColorManagedSource},
        {"audioCodecs", m_exportSourceInfo.audioCodecs.join(QStringLiteral(", "))},
        {"likelyVariableFrameRate", m_exportSourceInfo.likelyVariableFrameRate},
    };
}
QVariantMap AppController::exportFormatOptions() const
{
    const MediaRational sourceRate = m_exportSourceInfo.averageFrameRate.isValid()
        ? m_exportSourceInfo.averageFrameRate : m_exportSourceInfo.frameRate;
    QVariantList sizes, rates;
    int index = 0;
    for (const QSize &size : ExportFormat::resolutionOptions(m_exportSourceInfo.videoSize)) {
        const QString label = QStringLiteral("%1×%2%3").arg(size.width()).arg(size.height())
                                  .arg(index++ == 0 ? QStringLiteral(" (Source)") : QString());
        sizes.append(QVariantMap{{"width", size.width()}, {"height", size.height()}, {"label", label}});
    }
    index = 0;
    for (const MediaRational &rate : ExportFormat::frameRateOptions(sourceRate)) {
        const QString label = QString::number(rate.value(), 'f', 2) + QStringLiteral(" fps")
                              + (index++ == 0 ? QStringLiteral(" (Source)") : QString());
        rates.append(QVariantMap{{"numerator", rate.numerator}, {"denominator", rate.denominator},
                                 {"label", label}});
    }
    return {{"sizes", sizes}, {"rates", rates}};
}
qint64 AppController::estimateExportSize(const qint64 videoBitrate, const bool audioEnabled, const double seconds) const
{
    return ExportFormat::estimatedBytes(videoBitrate, audioEnabled, seconds);
}
QString AppController::formatEstimatedExportSize(const qint64 bytes) const
{
    return ExportFormat::formatEstimatedSize(bytes);
}
QVariantMap AppController::previewViewport(const int availableWidth, const int availableHeight) const
{
    const QSize sourceSize = m_exportSourceInfo.displayVideoSize.isValid()
        ? m_exportSourceInfo.displayVideoSize : m_exportSourceInfo.videoSize;
    const QRect viewport = PreviewPlayback::aspectFitViewport({availableWidth, availableHeight}, sourceSize);
    return {{"x", viewport.x()}, {"y", viewport.y()},
            {"width", viewport.width()}, {"height", viewport.height()}};
}
qint64 AppController::previewEndPositionMilliseconds() const
{
    const MediaRational rate = m_exportSourceInfo.averageFrameRate.isValid()
        ? m_exportSourceInfo.averageFrameRate : m_exportSourceInfo.frameRate;
    const auto range = ExportEngine::fullVideoFrameRange(m_exportSourceInfo, rate);
    const auto position = range ? PreviewPlayback::framePositionMilliseconds(range->lastFrame, rate) : std::nullopt;
    return position.value_or(0);
}
qint64 AppController::previewInitialPositionMilliseconds() const
{
    const MediaRational rate = m_exportSourceInfo.averageFrameRate.isValid()
        ? m_exportSourceInfo.averageFrameRate : m_exportSourceInfo.frameRate;
    const auto range = ExportEngine::fullVideoFrameRange(m_exportSourceInfo, rate);
    if (!range || range->lastFrame < 1) return 0;
    return PreviewPlayback::firstTimelineFramePositionMilliseconds(rate).value_or(0);
}
qint64 AppController::clampPreviewPositionMilliseconds(const qint64 requestedMilliseconds) const
{
    const MediaRational rate = m_exportSourceInfo.averageFrameRate.isValid()
        ? m_exportSourceInfo.averageFrameRate : m_exportSourceInfo.frameRate;
    const auto range = ExportEngine::fullVideoFrameRange(m_exportSourceInfo, rate);
    const auto position = range
        ? PreviewPlayback::clampPositionMilliseconds(requestedMilliseconds, range->lastFrame, rate)
        : std::nullopt;
    return position.value_or(0);
}
QString AppController::previewTimecodeForPositionMilliseconds(const qint64 positionMilliseconds) const
{
    const MediaRational rate = m_exportSourceInfo.averageFrameRate.isValid()
        ? m_exportSourceInfo.averageFrameRate : m_exportSourceInfo.frameRate;
    const auto range = ExportEngine::fullVideoFrameRange(m_exportSourceInfo, rate);
    if (!range || !rate.isValid() || positionMilliseconds < 0) return {};
    const qint64 bounded = clampPreviewPositionMilliseconds(positionMilliseconds);
    if (bounded >= previewEndPositionMilliseconds()) {
        return ExportEngine::formatSmpteTimecode(range->lastFrame, rate);
    }
    const qint64 frame = static_cast<qint64>(bounded) * rate.numerator / (rate.denominator * 1'000);
    return ExportEngine::formatSmpteTimecode(qBound(range->firstFrame, frame, range->lastFrame), rate);
}
QString AppController::previewEndTimecode() const
{
    const MediaRational rate = m_exportSourceInfo.averageFrameRate.isValid()
        ? m_exportSourceInfo.averageFrameRate : m_exportSourceInfo.frameRate;
    const auto range = ExportEngine::fullVideoFrameRange(m_exportSourceInfo, rate);
    return range ? ExportEngine::formatSmpteTimecode(range->lastFrame, rate) : QString();
}
void AppController::reportPlaybackError(const QString &message)
{
    const QString normalized = message.trimmed();
    const QString detail = normalized.isEmpty()
        ? QStringLiteral("The selected video could not be decoded.")
        : normalized.left(1'024);
    AppLog::error(QStringLiteral("Video playback failed: %1").arg(detail));
    setStatus(QStringLiteral("Video playback failed: %1").arg(detail));
}
qint64 AppController::recommendedExportBitrate(const int width, const int height, const qint64 numerator,
                                               const qint64 denominator, const QString &quality) const
{
    return ExportFormat::bitrateForQuality(
        quality, {width, height}, {numerator, denominator},
        m_exportSourceInfo.bitDepth.value_or(8));
}
QVariantMap AppController::exportMetrics() const { return m_exportMetrics; }
QVariantMap AppController::exportProgressInfo() const { return m_exportProgressInfo; }
bool AppController::exportProgressVisible() const { return m_exportProgressVisible; }
QString AppController::exportDiagnosticLog() const { return m_exportDiagnosticLog.text(); }
QString AppController::fixedFontFamily() const
{
    return QFontDatabase::systemFont(QFontDatabase::FixedFont).family();
}
QVariantMap AppController::syncCandidate() const { return m_syncCandidate; }
QVariant AppController::speed() const { return semanticValue("speed"); }
QVariant AppController::rpm() const { return semanticValue("rpm"); }
QVariant AppController::heartRate() const { return semanticValue("heartRate"); }
TelemetryRenderContext *AppController::renderContext() { return &m_previewRenderContext; }
WidgetModel *AppController::widgetModel() { return &m_widgetModel; }
QVariantList AppController::trackPoints() const { return m_trackPoints; }
QVariantMap AppController::currentTrackPoint() const
{
    return m_previewRenderContext.currentTrackPoint();
}
QString AppController::lapTimingStatus() const
{
    if (!m_session) return QStringLiteral("Open telemetry for lap timing");
    switch (m_lapSession.status) {
    case LapSessionStatus::Available:
        return QStringLiteral("%1 complete lap%2")
            .arg(m_lapSession.timedLaps.size())
            .arg(m_lapSession.timedLaps.size() == 1 ? QString() : QStringLiteral("s"));
    case LapSessionStatus::NoSourceStartGate:
        return QStringLiteral("No Start gate in telemetry");
    case LapSessionStatus::AmbiguousSourceStartGate:
        return QStringLiteral("Multiple Start gates in telemetry");
    case LapSessionStatus::InvalidGate:
        return QStringLiteral("Start gate geometry is invalid");
    case LapSessionStatus::NoUsableGps:
        return QStringLiteral("No usable GPS for lap timing");
    case LapSessionStatus::NoAcceptedPasses:
        return QStringLiteral("No Start-line passages detected");
    case LapSessionStatus::InsufficientPasses:
        return QStringLiteral("One Start-line passage detected; no complete lap");
    }
    return QStringLiteral("Lap timing unavailable");
}
QVariantList AppController::lapSummaries() const
{
    if (m_lapSession.status != LapSessionStatus::Available) return {};
    QVariantList summaries;
    summaries.reserve(m_lapSession.timedLaps.size());
    for (qsizetype index = 0; index < m_lapSession.timedLaps.size(); ++index) {
        const TimedLap &lap = m_lapSession.timedLaps[index];
        summaries.append(QVariantMap{
            {QStringLiteral("runId"), activeRunId()},
            {QStringLiteral("number"), lap.number},
            {QStringLiteral("startTelemetryTime"), lap.startTelemetryTime},
            {QStringLiteral("durationSeconds"), lap.durationSeconds},
            {QStringLiteral("hasDelta"), lap.referenceEligible() && m_lapSession.fastestLapIndex.has_value()},
            {QStringLiteral("referenceEligible"), lap.referenceEligible()},
            {QStringLiteral("exclusionReason"), lap.userExclusionReason},
            {QStringLiteral("referenceIssue"), lap.referenceIssue == LapReferenceIssue::GpsGap
                ? QStringLiteral("GPS gap") : lap.referenceIssue == LapReferenceIssue::InvalidGps
                    ? QStringLiteral("Invalid GPS") : QString()},
            {QStringLiteral("deltaToBestSeconds"), lap.referenceEligible() && m_lapSession.fastestLapIndex
                ? QVariant(lap.deltaToBestSeconds) : QVariant()},
            {QStringLiteral("isBest"), m_lapSession.fastestLapIndex
                    && *m_lapSession.fastestLapIndex == index},
        });
    }
    return summaries;
}
QVariantList AppController::lapNavigationSegments() const
{
    if (m_lapSession.status != LapSessionStatus::Available
        || m_lapSession.timedLaps.isEmpty() || previewEndPositionMilliseconds() <= 0) {
        return {};
    }

    struct VideoLap final {
        const TimedLap *lap = nullptr;
        qsizetype lapIndex = 0;
        qint64 startMilliseconds = -1;
        qint64 endMilliseconds = -1;
    };
    QVector<VideoLap> videoLaps;
    videoLaps.reserve(m_lapSession.timedLaps.size());
    for (qsizetype index = 0; index < m_lapSession.timedLaps.size(); ++index) {
        const TimedLap &lap = m_lapSession.timedLaps[index];
        const qint64 start = videoMillisecondsForTelemetryTime(lap.startTelemetryTime);
        const qint64 end = videoMillisecondsForTelemetryTime(lap.startTelemetryTime + lap.durationSeconds);
        if (start < 0 || end < start) continue;
        videoLaps.append({&lap, index, start, end});
    }
    if (videoLaps.isEmpty()) return {};

    const qint64 videoEnd = previewEndPositionMilliseconds();
    QVariantList segments;
    const auto appendFragment = [this, &segments](const QString &kind, const QString &label,
                                                   const qint64 start, const qint64 end,
                                                   const TimedLap *lap = nullptr,
                                                   const bool isBest = false) {
        if (start < 0 || end < start) return;
        QVariantMap segment{{QStringLiteral("kind"), kind}, {QStringLiteral("label"), label},
                            {QStringLiteral("startMilliseconds"), start},
                            {QStringLiteral("endMilliseconds"), end},
                            {QStringLiteral("durationMilliseconds"), end - start},
                            {QStringLiteral("startTimecode"), previewTimecodeForPositionMilliseconds(start)},
                            {QStringLiteral("endTimecode"), previewTimecodeForPositionMilliseconds(end)},
                            {QStringLiteral("seekMilliseconds"), start}};
        if (lap) {
            segment.insert(QStringLiteral("number"), lap->number);
            segment.insert(QStringLiteral("durationSeconds"), lap->durationSeconds);
            const bool hasDelta = lap->referenceEligible() && m_lapSession.fastestLapIndex.has_value();
            segment.insert(QStringLiteral("hasDelta"), hasDelta);
            segment.insert(QStringLiteral("referenceEligible"), lap->referenceEligible());
            segment.insert(QStringLiteral("deltaToBestSeconds"), hasDelta ? QVariant(lap->deltaToBestSeconds) : QVariant());
            segment.insert(QStringLiteral("isBest"), isBest);
        }
        segments.append(segment);
    };

    const VideoLap &first = videoLaps.constFirst();
    if (first.startMilliseconds > 0) {
        appendFragment(QStringLiteral("outlap"), QStringLiteral("Out lap"), 0, first.startMilliseconds);
    }
    for (const VideoLap &videoLap : videoLaps) {
        appendFragment(QStringLiteral("lap"), QStringLiteral("Lap %1").arg(videoLap.lap->number),
                       videoLap.startMilliseconds, videoLap.endMilliseconds, videoLap.lap,
                       m_lapSession.fastestLapIndex && *m_lapSession.fastestLapIndex == videoLap.lapIndex);
    }
    const VideoLap &last = videoLaps.constLast();
    if (last.endMilliseconds < videoEnd) {
        appendFragment(QStringLiteral("inlap"), QStringLiteral("In lap"), last.endMilliseconds, videoEnd);
    }
    return segments;
}
QStringList AppController::analysisChannels() const { return m_analysisChannels; }
bool AppController::analysisVisible() const { return m_analysisVisible; }
int AppController::analysisWindowX() const { return m_settings.value("analysis/windowX", -1).toInt(); }
int AppController::analysisWindowY() const { return m_settings.value("analysis/windowY", -1).toInt(); }
int AppController::analysisWindowWidth() const { return m_settings.value("analysis/windowWidth", 1240).toInt(); }
int AppController::analysisWindowHeight() const { return m_settings.value("analysis/windowHeight", 760).toInt(); }
int AppController::analysisSidebarWidth() const { return m_settings.value("analysis/sidebarWidth", 360).toInt(); }
int AppController::analysisVideoHeight() const { return m_settings.value("analysis/videoHeight", 360).toInt(); }
int AppController::windowX() const { return m_settings.value("window/x", -1).toInt(); }
int AppController::windowY() const { return m_settings.value("window/y", -1).toInt(); }
int AppController::windowWidth() const { return m_settings.value("window/width", 1440).toInt(); }
int AppController::windowHeight() const { return m_settings.value("window/height", 900).toInt(); }
QUrl AppController::projectPath() const
{
    return m_documentState.projectPath().isEmpty()
        ? QUrl() : QUrl::fromLocalFile(m_documentState.projectPath());
}
QString AppController::eventName() const
{
    return m_projectTemplate.value(QStringLiteral("event")).toObject().value(QStringLiteral("name")).toString();
}
QString AppController::activeRunId() const
{
    return m_projectTemplate.value(QStringLiteral("event")).toObject().value(QStringLiteral("activeRunId")).toString();
}
QVariantList AppController::eventRuns() const
{
    QVariantList result;
    const QJsonObject event = m_projectTemplate.value(QStringLiteral("event")).toObject();
    for (const QJsonValue &value : event.value(QStringLiteral("runs")).toArray()) {
        const QJsonObject run = value.toObject();
        result.append(QVariantMap{{QStringLiteral("id"), run.value(QStringLiteral("id")).toString()},
                                  {QStringLiteral("name"), run.value(QStringLiteral("name")).toString()}});
    }
    return result;
}

bool AppController::selectEventRun(const QString &runId)
{
    if (!EventProjectCodec::isEvent(m_projectTemplate) || projectLoading() || exporting()
        || recoveryPending() || m_documentState.pendingAction() != ProjectDocumentState::DestructiveAction::None) return false;
    if (runId == activeRunId()) return true;
    if (m_documentState.revision() == std::numeric_limits<quint64>::max()) return false;
    QJsonObject project = currentProjectObject();
    QJsonObject event = project.value(QStringLiteral("event")).toObject();
    bool found = false;
    for (const QJsonValue &value : event.value(QStringLiteral("runs")).toArray()) {
        found |= value.toObject().value(QStringLiteral("id")).toString() == runId;
    }
    if (!found) return false;
    event.insert(QStringLiteral("activeRunId"), runId);
    project.insert(QStringLiteral("event"), event);
    // Commit the complete document only after validation; source-generation cancellation
    // then prevents late results from the previous run from reaching preview/export.
    return beginProjectLoad(m_documentState.projectPath(), project, false, 0, 0, {}, true);
}
bool AppController::dirty() const { return m_documentState.dirty(); }
quint64 AppController::lastSavedRevision() const { return m_documentState.lastSavedRevision(); }
QString AppController::pendingDestructiveAction() const
{
    return ProjectDocumentState::actionName(m_documentState.pendingAction());
}

QString AppController::videoLoadState() const { return m_videoLoadState; }
QString AppController::vboLoadState() const { return m_vboLoadState; }
bool AppController::projectLoading() const { return m_projectLoading; }
QString AppController::projectLoadStage() const { return m_projectLoadStage; }
QString AppController::projectLoadError() const { return m_projectLoadError; }
bool AppController::recoveryPending() const { return m_recoveryPending; }
bool AppController::recoveryDegraded() const { return m_recoveryDegraded; }
QString AppController::recoveryError() const { return m_recoveryError; }
QString AppController::sourceMismatchType() const { return m_sourceMismatchType; }
QString AppController::sourceMismatchCandidateName() const
{
    if (m_sourceMismatchType == QStringLiteral("video")) {
        return QFileInfo(m_pendingMismatchVideo.path).fileName();
    }
    if (m_sourceMismatchType == QStringLiteral("telemetry")) {
        return QFileInfo(m_pendingMismatchVbo.path).fileName();
    }
    return {};
}

QString AppController::selectedTemplateId() const { return m_selectedTemplateId; }
QString AppController::activeTemplateId() const { return m_activeTemplateId; }

int AppController::templateIndexForId(const QString &templateId) const
{
    const QVariantList templates = m_widgetModel.templates();
    for (qsizetype index = 0; index < templates.size(); ++index) {
        if (templates[index].toMap().value(QStringLiteral("id")).toString() == templateId) {
            return static_cast<int>(index);
        }
    }
    return -1;
}

void AppController::selectTemplate(const QString &templateId)
{
    if (templateIndexForId(templateId) < 0 || m_selectedTemplateId == templateId) {
        return;
    }
    m_selectedTemplateId = templateId;
    m_settings.setValue(QStringLiteral("ui/selectedTemplateId"), m_selectedTemplateId);
    m_settings.sync();
    emit templateUiStateChanged();
}

void AppController::reconcileTemplateSelection()
{
    if (templateIndexForId(m_selectedTemplateId) >= 0) {
        return;
    }
    const QVariantList templates = m_widgetModel.templates();
    const QString fallback = templates.isEmpty()
        ? QString() : templates.constFirst().toMap().value(QStringLiteral("id")).toString();
    if (m_selectedTemplateId == fallback) {
        return;
    }
    m_selectedTemplateId = fallback;
    m_settings.setValue(QStringLiteral("ui/selectedTemplateId"), m_selectedTemplateId);
    m_settings.sync();
    emit templateUiStateChanged();
}

bool AppController::applyTemplate(const QString &templateId)
{
    if (templateIndexForId(templateId) < 0 || !m_widgetModel.applyTemplate(templateId)) {
        return false;
    }
    selectTemplate(templateId);
    markTemplateActive(templateId);
    return true;
}

void AppController::markTemplateActive(const QString &templateId)
{
    const QString activeId = templateIndexForId(templateId) >= 0 ? templateId : QString();
    if (m_activeTemplateId == activeId) {
        return;
    }
    m_activeTemplateId = activeId;
    emit templateUiStateChanged();
}

void AppController::clearActiveTemplate()
{
    markTemplateActive({});
}

bool AppController::saveActiveTemplate()
{
    const int index = templateIndexForId(m_activeTemplateId);
    if (index < 0 || m_widgetModel.templates()[index].toMap().value(QStringLiteral("builtIn")).toBool()) {
        return false;
    }
    return m_widgetModel.updateTemplate(m_activeTemplateId);
}

QString AppController::normalizedSourcePath(const QString &path)
{
    const QFileInfo info(path);
    const QString canonical = info.canonicalFilePath();
    return canonical.isEmpty() ? info.absoluteFilePath() : canonical;
}

QVariantList AppController::trackPointsFor(const TrackGeometry &geometry)
{
    QVariantList points;
    points.reserve(geometry.points.size());
    for (const QPointF &point : geometry.points) {
        points.append(point);
    }
    return points;
}

quint64 AppController::beginSourceReplacement(const bool replacingVideo)
{
    const bool restartOther = (replacingVideo ? m_vboLoadState : m_videoLoadState) == QStringLiteral("loading");
    const auto request = replacingVideo ? m_vboLoadRequest : m_videoLoadRequest;
    if (!replacingVideo) {
        ++m_outingRunGenerations[activeRunId()];
        invalidateOutingLapDetail();
        invalidateComparisonLaps();
    }
    const quint64 generation = beginSourceGeneration(true);
    if (restartOther && !request.path.isEmpty()) {
        if (replacingVideo) startVboLoad(request.path, generation, request.markDocumentDirty,
                                        request.expectedFingerprint, request.relink);
        else startVideoProbe(request.path, generation, request.markDocumentDirty,
                             request.expectedFingerprint, request.relink);
    }
    return generation;
}

quint64 AppController::beginSourceGeneration(const bool preserveOuting)
{
    if (!preserveOuting) {
        ++m_outingDocumentGeneration;
        m_outingRunCache.clear();
        m_outingInferredGroups = {};
        m_outingRunGenerations.clear();
        closeOutingLap();
        resetComparisonSlot(0);
        resetComparisonSlot(1);
        setComparisonViewOpen(false);
        m_comparisonRestoreAttempted = false;
    }
    ++m_sourceGeneration;
    if (!m_sourceMismatchType.isEmpty()) {
        m_sourceMismatchType.clear();
        m_pendingMismatchVideo = {};
        m_pendingMismatchVbo = {};
        emit sourceMismatchChanged();
    }
    const bool replacing = m_videoProbeWatcher.isRunning() || m_vboLoadWatcher.isRunning()
        || m_projectLoadWatcher.isRunning() || m_syncWatcher.isRunning();
    cancelSourceJobs(!preserveOuting);
    if (replacing) AppLog::info(QStringLiteral("Previous source load cancelled after replacement"));
    if (m_videoProbeWatcher.isRunning()) {
        m_videoLoadState = QStringLiteral("idle");
    }
    if (m_vboLoadWatcher.isRunning()) {
        m_vboLoadState = QStringLiteral("idle");
    }
    emit sourceLoadStateChanged();
    setProjectLoadState(false);
    return m_sourceGeneration;
}

void AppController::cancelSourceJobs(const bool cancelOutingDetail)
{
    if (cancelOutingDetail && m_comparisonCancellation) m_comparisonCancellation->store(true);
    if (cancelOutingDetail && m_outingLapDetailCancellation) m_outingLapDetailCancellation->store(true);
    if (cancelOutingDetail && m_segmentReviewCancellation) m_segmentReviewCancellation->store(true);
    for (const auto &cancellation : {m_videoProbeCancellation, m_vboLoadCancellation,
                                     m_projectLoadCancellation, m_syncCancellation, m_outingLapCancellation}) {
        if (cancellation) {
            cancellation->store(true);
        }
    }
}

void AppController::startVideoProbe(
    const QString &path, const quint64 generation, const bool markDocumentDirty,
    QJsonObject expectedFingerprint, const bool relink)
{
    AppLog::info(QStringLiteral("Video load/probe started: %1").arg(path));
    m_videoProbeCancellation = std::make_shared<std::atomic_bool>(false);
    const std::shared_ptr<std::atomic_bool> cancellation = m_videoProbeCancellation;
    m_videoLoadRequest = {path, markDocumentDirty, expectedFingerprint, relink};
    m_pendingVideoPath = path;
    m_videoLoadMarksDocumentDirty = markDocumentDirty;
    m_videoLoadState = QStringLiteral("loading");
    emit sourceLoadStateChanged();
    m_videoProbeWatcher.setFuture(QtConcurrent::run(
        [path, generation, cancellation, expectedFingerprint = std::move(expectedFingerprint), relink] {
        VideoProbeResult result;
        result.path = path;
        result.generation = generation;
        result.expectedFingerprint = expectedFingerprint;
        result.relink = relink;
        try {
            result.mediaInfo = MediaProbe::probe(
                path, {}, false, -1, {}, [cancellation] { return cancellation->load(); });
            result.fingerprint = videoSourceFingerprint(path, result.mediaInfo);
            result.success = !cancellation->load();
            if (!result.success) {
                result.cancelled = true;
                result.error = QStringLiteral("Video loading was cancelled.");
            }
        } catch (const OperationCancelled &) {
            result.cancelled = true;
            result.error = QStringLiteral("Video loading was cancelled.");
        } catch (const std::exception &error) {
            result.error = QString::fromUtf8(error.what());
        }
        return result;
    }));
}

void AppController::startVboLoad(
    const QString &path, const quint64 generation, const bool markDocumentDirty,
    QJsonObject expectedFingerprint, const bool relink)
{
    AppLog::info(QStringLiteral("Telemetry load started: %1").arg(path));
    m_vboLoadCancellation = std::make_shared<std::atomic_bool>(false);
    const std::shared_ptr<std::atomic_bool> cancellation = m_vboLoadCancellation;
    m_vboLoadRequest = {path, markDocumentDirty, expectedFingerprint, relink};
    m_pendingVboPath = path;
    m_vboLoadMarksDocumentDirty = markDocumentDirty;
    m_vboLoadState = QStringLiteral("loading");
    emit sourceLoadStateChanged();
    QByteArray expectedRevision;
    if (!expectedFingerprint.isEmpty()) {
        for (const auto &value : outingLapSources()) {
            const auto source = value.toObject();
            if (source.value("runId").toString() == activeRunId()
                && source.value("reference").toObject().value("fingerprint").toObject() == expectedFingerprint)
                expectedRevision = source.value("expectedRevision").toString().toLatin1();
        }
    }
    m_vboLoadWatcher.setFuture(QtConcurrent::run(
        [path, generation, cancellation, expectedRevision, expectedFingerprint = std::move(expectedFingerprint), relink] {
        VboLoadResult result;
        result.path = path;
        result.generation = generation;
        result.expectedFingerprint = expectedFingerprint;
        result.relink = relink;
        try {
            const auto sourceSize = QFileInfo(path).size();
            result.contentRevision = TelemetrySource::contentSha256(path, sourceSize,
                [cancellation] { return cancellation->load(); }).toHex();
            result.contentMismatch = !expectedRevision.isEmpty() && result.contentRevision != expectedRevision;
            result.session = TelemetrySource::load(
                path, [cancellation] { return cancellation->load(); });
            if (cancellation->load()) {
                result.cancelled = true;
                result.error = QStringLiteral("Telemetry loading was cancelled.");
                return result;
            }
            result.geometry = buildTrackGeometry(
                result.session, [cancellation] { return cancellation->load(); });
            const auto cancelled = [cancellation] { return cancellation->load(); };
            result.lapSession = deriveSourceLapSession(result.session, {}, cancelled);
            result.fingerprint = ProjectSourceReferenceCodec::telemetryFingerprint(
                path, result.session);
            if (TelemetrySource::contentSha256(path, sourceSize, cancelled).toHex() != result.contentRevision)
                throw std::runtime_error("Recording changed during loading; reload this source.");
            result.success = !cancellation->load();
            if (!result.success) {
                result.cancelled = true;
                result.error = QStringLiteral("Telemetry loading was cancelled.");
            }
        } catch (const OperationCancelled &) {
            result.cancelled = true;
            result.error = QStringLiteral("Telemetry loading was cancelled.");
        } catch (const std::exception &error) {
            result.error = QString::fromUtf8(error.what());
        }
        return result;
    }));
}

void AppController::commitVideoProbe(const VideoProbeResult &result, const bool markDocumentDirty)
{
    AppLog::info(QStringLiteral("Video load succeeded: %1").arg(result.path));
    m_videoSource = QUrl::fromLocalFile(result.path);
    m_exportSourceInfo = result.mediaInfo;
    m_videoReference = ProjectSourceReferenceCodec::forLoadedSource(
        result.path, result.fingerprint);
    m_videoLoadState = QStringLiteral("ready");
    m_pendingVideoPath.clear();
    m_syncCandidate.clear();
    emit videoSourceChanged();
    emit previewMetadataChanged();
    emit lapNavigationChanged();
    emit exportChanged();
    emit syncCandidateChanged();
    emit sourceLoadStateChanged();
    if (markDocumentDirty) {
        markPersistentChange();
    }
    setStatus(QStringLiteral("Video opened: %1").arg(QFileInfo(result.path).fileName()));
}

void AppController::commitVboLoad(const VboLoadResult &result, const bool markDocumentDirty)
{
    const QScopedValueRollback suppressDirty(
        m_suppressDirtyTracking, m_suppressDirtyTracking || !markDocumentDirty);
    AppLog::info(QStringLiteral("Telemetry load succeeded: %1").arg(result.path));
    m_session = std::make_unique<TelemetrySession>(result.session);
    m_trackGeometry = result.geometry;
    m_lapSession = result.lapSession;
    m_loadedSourceRevision = result.contentRevision;
    m_trackPoints = trackPointsFor(m_trackGeometry);
    m_telemetryPath = result.path;
    m_vboReference = ProjectSourceReferenceCodec::forLoadedSource(
        result.path, result.fingerprint);
    m_vboLoadState = QStringLiteral("ready");
    m_pendingVboPath.clear();
    m_syncCandidate.clear();
    m_previewRenderContext.setSession(m_session.get());
    m_previewRenderContext.setTrackGeometry(&m_trackGeometry);
    if (markDocumentDirty && EventProjectCodec::isEvent(m_projectTemplate)) {
        // Replacement clears asserted layout/direction, then records the gates
        // actually verified in the new source. This enables fresh inference.
        auto project = currentProjectObject(); auto event = project.value("event").toObject();
        auto runs = event.value("runs").toArray();
        for (qsizetype i = 0; i < runs.size(); ++i) {
            auto run = runs[i].toObject();
            if (run.value("id").toString() != activeRunId()) continue;
            auto config = EventProjectCodec::trackConfiguration(run);
            const auto gates = timingGateRevision(result.session);
            config.insert("gateRevision", gates.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(gates));
            run.insert("trackConfiguration", config); runs[i] = run;
        }
        event.insert("runs", runs); project.insert("event", event); m_projectTemplate = project;
    }
    refreshLapExclusionPolicy();
    reconcileAnalysisChannels();
    emit telemetryChanged();
    emit lapNavigationChanged();
    emit liveValuesChanged();
    emit syncCandidateChanged();
    emit sourceLoadStateChanged();
    if (markDocumentDirty) {
        markPersistentChange();
    }
    QString message = QStringLiteral("Telemetry opened: %1 samples, %2 numeric channels.")
        .arg(m_session->sampleCount).arg(m_session->channels.size());
    for (const auto &warning : m_session->warnings) AppLog::warn(warning);
    if (!m_session->warnings.isEmpty()) message += "\n" + m_session->warnings.mid(0, 3).join("\n");
    setStatus(message);
}

void AppController::loadVideo(const QUrl &url)
{
    const QString path = url.toLocalFile();
    const QFileInfo info(path);
    const QString extension = info.suffix().toLower();
    if (!info.isFile() || (extension != "mp4" && extension != "mov")) {
        setStatus("Choose an existing MP4 or MOV video.");
        return;
    }
    const QString normalizedPath = normalizedSourcePath(info.absoluteFilePath());
    if (normalizedPath == normalizedSourcePath(m_videoSource.toLocalFile())) {
        return;
    }
    const quint64 generation = beginSourceReplacement(true);
    m_pendingVideoPath = normalizedPath;
    startVideoProbe(normalizedPath, generation, true);
    setStatus(QStringLiteral("Loading video metadata: %1").arg(info.fileName()));
}

void AppController::loadVbo(const QUrl &url)
{
    const QString path = url.toLocalFile();
    const QFileInfo info(path);
    const QString absolutePath = normalizedSourcePath(path);
    if (!info.isFile() || !TelemetrySource::supportsPath(path)) {
        setStatus("Choose an existing VBO or RaceChrono RCZ telemetry file.");
        return;
    }
    if (!m_telemetryPath.isEmpty()
        && ExportOutputTransaction::normalizedComparisonPath(absolutePath)
            == ExportOutputTransaction::normalizedComparisonPath(m_telemetryPath)) {
        return;
    }
    const quint64 generation = beginSourceReplacement(false);
    m_pendingVboPath = absolutePath;
    startVboLoad(absolutePath, generation, true);
    setStatus(QStringLiteral("Loading telemetry: %1").arg(info.fileName()));
}

void AppController::relinkVideo(const QUrl &url)
{
    const QFileInfo info(url.toLocalFile());
    const QString extension = info.suffix().toLower();
    if (!info.isFile() || (extension != QStringLiteral("mp4")
                           && extension != QStringLiteral("mov"))) {
        setStatus(QStringLiteral("Choose an existing MP4 or MOV video."));
        return;
    }
    const quint64 generation = beginSourceReplacement(true);
    const QString path = normalizedSourcePath(info.absoluteFilePath());
    m_pendingVideoPath = path;
    startVideoProbe(path, generation, true, m_videoReference.fingerprint, true);
}

void AppController::relinkVbo(const QUrl &url)
{
    const QFileInfo info(url.toLocalFile());
    if (!info.isFile() || !TelemetrySource::supportsPath(info.filePath())) {
        setStatus(QStringLiteral("Choose an existing VBO or RaceChrono RCZ telemetry file."));
        return;
    }
    const quint64 generation = beginSourceReplacement(false);
    const QString path = normalizedSourcePath(info.absoluteFilePath());
    m_pendingVboPath = path;
    startVboLoad(path, generation, true, m_vboReference.fingerprint, true);
}

void AppController::resolveSourceMismatch(const bool acceptReplacement)
{
    const QString type = m_sourceMismatchType;
    m_sourceMismatchType.clear();
    emit sourceMismatchChanged();
    if (!acceptReplacement) {
        m_pendingMismatchVideo = {};
        m_pendingMismatchVbo = {};
        setStatus(QStringLiteral("Source replacement cancelled."));
        return;
    }
    if (type == QStringLiteral("video") && m_pendingMismatchVideo.success
        && m_pendingMismatchVideo.generation == m_sourceGeneration) {
        commitVideoProbe(m_pendingMismatchVideo, true);
    } else if (type == QStringLiteral("telemetry") && m_pendingMismatchVbo.success
               && m_pendingMismatchVbo.generation == m_sourceGeneration) {
        commitVboLoad(m_pendingMismatchVbo, true);
    }
    m_pendingMismatchVideo = {};
    m_pendingMismatchVbo = {};
}

void AppController::performClearProject()
{
    const QScopedValueRollback suppressDirty(m_suppressDirtyTracking, true);
    static_cast<void>(beginSourceGeneration());
    m_videoLoadState = QStringLiteral("idle");
    m_vboLoadState = QStringLiteral("idle");
    m_pendingVideoPath.clear();
    m_pendingVboPath.clear();
    setProjectLoadState(false);
    m_videoSource = QUrl();
    m_videoReference = {};
    m_exportSourceInfo = {};
    m_exportMetrics.clear();
    m_exportDiagnosticLog.clear();
    m_exportProgressInfo.clear();
    m_exportProgressVisible = false;
    m_telemetryPath.clear();
    m_vboReference = {};
    m_session.reset();
    m_lapSession = {};
    m_loadedSourceRevision.clear();
    m_trackGeometry = {};
    m_previewRenderContext.setSession(nullptr);
    m_previewRenderContext.setTrackGeometry(nullptr);
    m_previewRenderContext.setLapSession({});
    m_trackPoints.clear();
    setAnalysisChannels({});
    setAnalysisVisible(false);
    m_playbackTime = 0.0;
    m_sync = {};
    m_syncCandidate.clear();
    m_projectTemplate = {};
    clearActiveTemplate();
    m_widgetModel.resetDefaults();
    emit videoSourceChanged();
    emit previewMetadataChanged();
    emit telemetryChanged();
    emit lapNavigationChanged();
    emit playbackTimeChanged();
    emit syncChanged();
    emit syncCandidateChanged();
    emit sourceLoadStateChanged();
    emit liveValuesChanged();
    m_documentId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_documentState.reset();
    m_pendingOpenProject = QUrl();
    m_settings.remove("project/path");
    m_settings.sync();
    emit documentStateChanged();
    emit destructiveActionChanged();
    setStatus("New native project created.");
}

QString AppController::valueText(const QString &channelName, const int decimals) const
{
    if (!m_session) {
        return QStringLiteral("—");
    }
    const auto time = videoToTelemetryTime(m_playbackTime, m_sync);
    if (!time) return QStringLiteral("—");
    const auto value = m_session->valueAt(channelName, *time);
    return value ? QString::number(*value, 'f', qBound(0, decimals, 6)) : QStringLiteral("—");
}

QVariant AppController::telemetryValue(const QString &channelName) const
{
    if (!m_session || channelName.isEmpty()) {
        return {};
    }
    const auto time = videoToTelemetryTime(m_playbackTime, m_sync);
    if (!time) return {};
    const auto value = m_session->valueAt(channelName, *time);
    return value ? QVariant(*value) : QVariant();
}

QVariantMap AppController::telemetrySeries(
    const QString &channelName,
    const double videoStart,
    const double videoEnd,
    const int maximumPoints) const
{
    if (!m_session || channelName.isEmpty() || !std::isfinite(videoStart)
        || !std::isfinite(videoEnd) || maximumPoints < 2) {
        return {};
    }
    const auto telemetryStart = videoToTelemetryTime(videoStart, m_sync);
    const auto telemetryEnd = videoToTelemetryTime(videoEnd, m_sync);
    if (!telemetryStart || !telemetryEnd) return {};
    return sessionSeries(*m_session, channelName, *telemetryStart, *telemetryEnd, maximumPoints);
}

QVariantMap AppController::sessionSeries(const TelemetrySession &session, const QString &channelName,
    double telemetryStart, double telemetryEnd, int maximumPoints)
{
    SampledSegmentsStatus status = SampledSegmentsStatus::Ok;
    const QVector<QVector<QPointF>> sampledSegments = session.sampledSegments(
        channelName, telemetryStart, telemetryEnd, qBound(2, maximumPoints, 2000), &status);
    if (sampledSegments.isEmpty()) {
        // A genuinely empty overlap (status Ok) stays the plain "no data" shape
        // callers already expect; only a real range/channel problem gets a reason
        // so QML can tell "no data" apart from a rendering/data-shape failure.
        if (status == SampledSegmentsStatus::Ok) return {};
        QString reason;
        switch (status) {
        case SampledSegmentsStatus::InvalidRange: reason = QStringLiteral("invalidRange"); break;
        case SampledSegmentsStatus::ChannelMissing: reason = QStringLiteral("channelMissing"); break;
        case SampledSegmentsStatus::ChannelMalformed: reason = QStringLiteral("channelMalformed"); break;
        case SampledSegmentsStatus::Ok: break;
        }
        return {{"reason", reason}};
    }
    double minimum = sampledSegments.front().front().y();
    double maximum = minimum;
    for (const QVector<QPointF> &segment : sampledSegments) {
        for (const QPointF &sample : segment) {
            minimum = std::min(minimum, sample.y());
            maximum = std::max(maximum, sample.y());
        }
    }
    const double telemetrySpan = telemetryEnd - telemetryStart;
    QVariantList segments;
    segments.reserve(sampledSegments.size());
    for (const QVector<QPointF> &sampledSegment : sampledSegments) {
        QVariantList points;
        points.reserve(sampledSegment.size());
        for (const QPointF &sample : sampledSegment) {
            const double normalizedTime = telemetrySpan == 0.0
                ? 0.0
                : (sample.x() - telemetryStart) / telemetrySpan;
            points.append(QVariantMap{{"x", normalizedTime}, {"y", sample.y()}});
        }
        // QVariantList has an overload that appends another list's elements.
        // Wrap the points list explicitly so QML receives segments -> points,
        // preserving telemetry gaps as separate polylines.
        segments.append(QVariant::fromValue(points));
    }
    const QString resolved = session.aliases.value(channelName, channelName);
    const auto channel = session.channels.constFind(resolved);
    return {
        {"segments", segments},
        // Presentation only: retain signed samples and units for inspection.
        {"brakingUp", resolved == session.aliases.value("longitudinalAcceleration")},
        {"minimum", minimum},
        {"maximum", maximum},
        {"unit", channel == session.channels.cend() ? QString() : channel->unit},
    };
}

qint64 AppController::videoMillisecondsForTelemetryTime(const double telemetryTime) const
{
    if (!m_exportSourceInfo.videoSize.isValid()) return -1;
    const auto videoTime = telemetryToVideoTime(telemetryTime, m_sync);
    if (!videoTime || *videoTime < 0.0) return -1;
    const double milliseconds = *videoTime * 1'000.0;
    if (!std::isfinite(milliseconds) || milliseconds >= 0x1p63
        || milliseconds > static_cast<double>(previewEndPositionMilliseconds())) {
        return -1;
    }
    return static_cast<qint64>(std::llround(milliseconds));
}

void AppController::toggleAnalysisChannel(const QString &channelName)
{
    QStringList channels = m_analysisChannels;
    if (channels.contains(channelName)) {
        channels.removeAll(channelName);
    } else if (!channelName.isEmpty() && channels.size() < 4) {
        channels.append(channelName);
    }
    setAnalysisChannels(channels);
}

void AppController::requestNewProject()
{
    AppLog::info(QStringLiteral("New project requested"));
    beginDestructiveAction(ProjectDocumentState::DestructiveAction::NewProject);
}

void AppController::requestOpenProject(const QUrl &url)
{
    AppLog::info(QStringLiteral("Open project requested: %1").arg(url.toLocalFile()));
    beginDestructiveAction(ProjectDocumentState::DestructiveAction::OpenProject, url);
}

void AppController::requestQuit()
{
    AppLog::info(QStringLiteral("Quit requested"));
    if (exporting()) {
        AppLog::warn(QStringLiteral("Quit request deferred while export is running"));
        return;
    }
    beginDestructiveAction(ProjectDocumentState::DestructiveAction::Quit);
}

void AppController::resolveDestructiveAction(const QString &decision)
{
    if (m_documentState.pendingAction() == ProjectDocumentState::DestructiveAction::None) {
        return;
    }
    if (decision == QStringLiteral("cancel")) {
        cancelPendingDestructiveAction();
        return;
    }
    AppLog::info(QStringLiteral("Dirty project decision: %1").arg(decision));
    if (decision == QStringLiteral("discard")) {
        performPendingDestructiveAction();
        return;
    }
    if (decision != QStringLiteral("save")) {
        return;
    }
    if (m_documentState.projectPath().isEmpty()) {
        emit saveAsRequested();
        return;
    }
    saveCurrentProject();
}

void AppController::cancelPendingDestructiveAction()
{
    if (m_documentState.pendingAction() == ProjectDocumentState::DestructiveAction::None) {
        return;
    }
    AppLog::info(QStringLiteral("Dirty project decision: cancel"));
    m_documentState.cancelPendingAction();
    m_pendingOpenProject = QUrl();
    emit destructiveActionChanged();
}

bool AppController::saveCurrentProject()
{
    if (m_documentState.projectPath().isEmpty()) {
        AppLog::info(QStringLiteral("Project save requested: save as"));
        emit saveAsRequested();
        return false;
    }
    return saveProject(QUrl::fromLocalFile(m_documentState.projectPath()));
}

void AppController::resolveStartupRecovery(const QString &decision)
{
    if (!m_recoveryPending || (decision != QStringLiteral("recover")
                               && decision != QStringLiteral("discard"))) {
        return;
    }
    const ProjectRecoverySnapshot snapshot = m_pendingRecovery;
    m_pendingRecovery = {};
    m_recoveryPending = false;
    emit recoveryChanged();
    if (decision == QStringLiteral("recover")) {
        if (!beginProjectLoad(snapshot.originalProjectPath, snapshot.project, true,
                              snapshot.revision, snapshot.lastSavedRevision, snapshot.documentId)) {
            m_pendingRecovery = snapshot;
            m_recoveryPending = true;
            emit recoveryChanged();
        }
        return;
    }
    if (!discardRecovery(snapshot, QStringLiteral("startup discard"))) {
        m_pendingRecovery = snapshot;
        m_recoveryPending = true;
        emit recoveryChanged();
        setStatus(QStringLiteral("Could not discard recovery data."));
        return;
    }
    AppLog::info(QStringLiteral("Recovery discarded"));
    if (!snapshot.originalProjectPath.isEmpty() && QFileInfo(snapshot.originalProjectPath).isFile()) {
        performOpenProject(QUrl::fromLocalFile(snapshot.originalProjectPath));
    } else {
        performClearProject();
    }
}

bool AppController::performOpenProject(const QUrl &url)
{
    if (!url.isLocalFile()) return false;
    const QString projectPath = normalizedSourcePath(url.toLocalFile());
    AppLog::info(QStringLiteral("Project load started: %1").arg(projectPath));
    const quint64 generation = beginSourceGeneration();
    const quint64 documentRevision = m_documentState.revision();
    m_projectLoadCancellation = std::make_shared<std::atomic_bool>(false);
    const std::shared_ptr<std::atomic_bool> cancellation = m_projectLoadCancellation;
    setProjectLoadState(true, QStringLiteral("Validating project"));
    m_projectLoadWatcher.setFuture(QtConcurrent::run([projectPath, generation, documentRevision, cancellation] {
        ProjectLoadResult result;
        result.projectPath = projectPath;
        result.generation = generation;
        result.documentRevisionAtStart = documentRevision;
        if (cancellation->load()) { result.cancelled = true; return result; }
        const auto loaded = BoundedJsonLoader::loadFile(
            projectPath, ProjectLimits::projectBytes, QStringLiteral("Project"));
        if (!loaded.success() || !loaded.document.isObject()) { result.error = loaded.error; return result; }
        const QJsonObject project = loaded.document.object();
        QString validationError;
        if (!ProjectLimits::validateProject(project, &validationError)) { result.error = validationError; return result; }
        if (cancellation->load()) { result.cancelled = true; return result; }
        const QJsonObject editor = EventProjectCodec::editorProjection(project);
        const QJsonObject sync = editor.value(QStringLiteral("sync")).toObject();
        const double offset = sync.value(QStringLiteral("offset")).toDouble();
        const double timeScale = sync.value(QStringLiteral("timeScale")).toDouble(1.0);
        if (!std::isfinite(offset) || !std::isfinite(timeScale) || timeScale <= 0.0) {
            result.error = QStringLiteral("Synchronization state is invalid."); return result;
        }
        const QJsonObject analysis = project.value(QStringLiteral("analysis")).toObject();
        for (const QJsonValue &value : analysis.value(QStringLiteral("channels")).toArray()) {
            if (!value.isString() || value.toString().size() > ProjectLimits::maximumStringCharacters) {
                result.error = QStringLiteral("Analysis channels are malformed."); return result;
            }
            result.analysisChannels.append(value.toString());
        }
        result.success = true;
        result.project = project;
        result.widgets = project.value(QStringLiteral("scene")).toObject().value(QStringLiteral("widgets")).toArray();
        result.sync = {offset, timeScale};
        result.videoReference = ProjectSourceReferenceCodec::fromProject(editor, QStringLiteral("video"), QStringLiteral("videoPath"));
        result.vboReference = ProjectSourceReferenceCodec::fromProject(editor, QStringLiteral("telemetry"), QStringLiteral("vboPath"));
        result.resolvedVideoPath = ProjectSourceReferenceCodec::resolve(result.videoReference, projectPath);
        result.resolvedVboPath = ProjectSourceReferenceCodec::resolve(result.vboReference, projectPath);
        return result;
    }));
    return true;
}

bool AppController::beginProjectLoad(
    QString projectPath, const QJsonObject &project, const bool recovered,
    const quint64 recoveredRevision, const quint64 recoveredLastSavedRevision,
    QString recoveredDocumentId, const bool runSelection)
{
    QString validationError;
    if (!ProjectLimits::validateProject(project, &validationError)) {
        AppLog::error(QStringLiteral("Project load failed: %1").arg(validationError));
        setStatus(QStringLiteral("Project error: %1").arg(validationError));
        return false;
    }
    const QJsonObject scene = project.value("scene").toObject();
    WidgetModel candidateWidgets;
    if (!candidateWidgets.fromJson(scene.value("widgets").toArray())) {
        AppLog::error(QStringLiteral("Project load failed: unsupported or invalid file: %1")
                          .arg(projectPath));
        setStatus("Project error: unsupported or invalid .fetproject file.");
        return false;
    }
    const QJsonObject editor = EventProjectCodec::editorProjection(project);
    const QJsonObject sync = editor.value("sync").toObject();
    const double offset = sync.value("offset").toDouble();
    const double timeScale = sync.value("timeScale").toDouble(1.0);
    if (!std::isfinite(offset) || !std::isfinite(timeScale) || timeScale <= 0.0) {
        AppLog::error(QStringLiteral("Project load failed: invalid synchronization state: %1")
                          .arg(projectPath));
        setStatus("Project error: synchronization state is invalid.");
        return false;
    }
    const QJsonObject analysis = project.value("analysis").toObject();
    QStringList channels;
    if (analysis.value("channels").isArray()) {
        for (const QJsonValue &value : analysis.value("channels").toArray()) {
            channels.append(value.toString());
        }
    }
    const quint64 generation = beginSourceGeneration(runSelection);
    ProjectLoadResult result;
    result.success = true;
    result.projectPath = std::move(projectPath);
    result.project = project;
    result.widgets = scene.value(QStringLiteral("widgets")).toArray();
    result.analysisChannels = channels;
    result.sync = {offset, timeScale};
    result.generation = generation;
    result.recovered = recovered;
    result.runSelection = runSelection;
    result.recoveredRevision = recoveredRevision;
    result.recoveredLastSavedRevision = recoveredLastSavedRevision;
    result.recoveredDocumentId = std::move(recoveredDocumentId);
    result.videoReference = ProjectSourceReferenceCodec::fromProject(
        editor, QStringLiteral("video"), QStringLiteral("videoPath"));
    result.vboReference = ProjectSourceReferenceCodec::fromProject(
        editor, QStringLiteral("telemetry"), QStringLiteral("vboPath"));
    result.resolvedVideoPath = ProjectSourceReferenceCodec::resolve(
        result.videoReference, result.projectPath);
    result.resolvedVboPath = ProjectSourceReferenceCodec::resolve(
        result.vboReference, result.projectPath);

    setProjectLoadState(true, QStringLiteral("Applying project"));
    if (!commitProjectLoad(result)) {
        return false;
    }

    startProjectSources(result);
    return true;
}

void AppController::startProjectSources(const ProjectLoadResult &result)
{
    m_pendingVideoPath = result.resolvedVideoPath;
    m_pendingVboPath = result.resolvedVboPath;
    if (!result.resolvedVideoPath.isEmpty()) {
        startVideoProbe(result.resolvedVideoPath, result.generation, false,
                        result.videoReference.fingerprint, false);
    }
    if (!result.resolvedVboPath.isEmpty()) {
        startVboLoad(result.resolvedVboPath, result.generation, false,
                     result.vboReference.fingerprint, false);
    }
}

void AppController::setProjectLoadState(bool loading, QString stage, QString error)
{
    if (m_projectLoading == loading && m_projectLoadStage == stage && m_projectLoadError == error) {
        return;
    }
    m_projectLoading = loading;
    m_projectLoadStage = std::move(stage);
    m_projectLoadError = std::move(error);
    emit projectLoadChanged();
}

bool AppController::commitProjectLoad(const ProjectLoadResult &result)
{
    const QScopedValueRollback suppressDirty(m_suppressDirtyTracking, true);
    setProjectLoadState(true, QStringLiteral("Applying project"));
    if (!m_widgetModel.fromJson(result.widgets)) {
        AppLog::error(QStringLiteral("Project load failed while applying widget scene: %1")
                          .arg(result.projectPath));
        setProjectLoadState(false, {}, QStringLiteral("widget scene could not be applied."));
        setStatus("Project could not be opened: widget scene could not be applied.");
        return false;
    }
    m_projectTemplate = result.project;
    m_videoReference = result.videoReference;
    m_vboReference = result.vboReference;
    m_videoSource = QUrl();
    m_exportSourceInfo = {};
    m_videoLoadState = result.videoReference.isEmpty() ? QStringLiteral("idle")
        : result.resolvedVideoPath.isEmpty() ? QStringLiteral("missing")
                                             : QStringLiteral("loading");
    m_telemetryPath.clear();
    m_session.reset();
    m_lapSession = {};
    m_loadedSourceRevision.clear();
    m_trackGeometry = {};
    m_trackPoints = trackPointsFor(m_trackGeometry);
    m_vboLoadState = result.vboReference.isEmpty() ? QStringLiteral("idle")
        : result.resolvedVboPath.isEmpty() ? QStringLiteral("missing")
                                           : QStringLiteral("loading");
    m_previewRenderContext.setSession(nullptr);
    m_previewRenderContext.setTrackGeometry(nullptr);
    m_previewRenderContext.setLapSession({});
    m_sync = result.sync;
    m_previewRenderContext.setSyncTransform(m_sync);
    m_playbackTime = 0.0;
    m_syncCandidate.clear();
    // A .fetproject stores a scene, not template provenance. Retain the picker preference,
    // but never let a newly opened scene overwrite a visible custom template in place.
    if (!result.runSelection) clearActiveTemplate();
    m_analysisChannels.clear();
    setAnalysisChannels(result.analysisChannels);
    if (!result.runSelection) setAnalysisVisible(false);
    reconcileAnalysisChannels();
    if (!result.projectPath.isEmpty()) {
        m_settings.setValue("project/path", result.projectPath);
    }
    if (result.runSelection) {
        // Selecting a run changes the same document, never its saved identity or clean revision.
        m_documentState.markChanged();
    } else if (result.recovered) {
        m_documentId = result.recoveredDocumentId.isEmpty()
            ? QUuid::createUuid().toString(QUuid::WithoutBraces)
            : result.recoveredDocumentId;
        m_documentState.restoreUnsaved(
            result.projectPath, result.recoveredRevision, result.recoveredLastSavedRevision);
    } else {
        SavedDocumentMetadata metadata;
        if (parseSavedDocumentMetadata(result.project, &metadata)) {
            m_documentId = metadata.id;
            m_documentState.reset(result.projectPath, metadata.revision);
        } else {
            m_documentId = QUuid::createUuid().toString(QUuid::WithoutBraces);
            m_documentState.reset(result.projectPath);
        }
    }
    emit videoSourceChanged();
    emit previewMetadataChanged();
    emit telemetryChanged();
    emit lapNavigationChanged();
    emit exportChanged();
    emit playbackTimeChanged();
    emit syncChanged();
    emit syncCandidateChanged();
    emit liveValuesChanged();
    emit sourceLoadStateChanged();
    emit documentStateChanged();
    setProjectLoadState(false);
    if (result.runSelection) {
        scheduleRecoveryWrite();
        setStatus(QStringLiteral("Run selected. Loading its available sources."));
    } else if (result.recovered) {
        AppLog::info(QStringLiteral("Recovery accepted"));
        setStatus(QStringLiteral("Recovered unsaved changes. Save to keep them."));
    } else {
        AppLog::info(QStringLiteral("Project load succeeded: %1").arg(result.projectPath));
        AppLog::info(QStringLiteral("Saved project restored from disk: %1").arg(result.projectPath));
        setStatus(QStringLiteral("Project opened: %1").arg(QFileInfo(result.projectPath).fileName()));
    }
    return true;
}

QJsonObject AppController::currentProjectObject(
    const QString &projectPath, const std::optional<quint64> savedRevision) const
{
    const bool eventProject = EventProjectCodec::isEvent(m_projectTemplate);
    QJsonObject project = EventProjectCodec::editorProjection(m_projectTemplate);
    project.insert("version", 2);
    project.remove(QStringLiteral("videoPath"));
    project.remove(QStringLiteral("vboPath"));
    const QString targetProjectPath = projectPath.isEmpty()
        ? m_documentState.projectPath() : projectPath;
    QJsonObject sources = project.value(QStringLiteral("sources")).toObject();
    const QJsonObject video = eventProject
        ? EventProjectCodec::referenceForSave(m_videoReference, m_documentState.projectPath(), targetProjectPath)
        : ProjectSourceReferenceCodec::toJson(m_videoReference, targetProjectPath);
    const QJsonObject telemetry = eventProject
        ? EventProjectCodec::referenceForSave(m_vboReference, m_documentState.projectPath(), targetProjectPath)
        : ProjectSourceReferenceCodec::toJson(m_vboReference, targetProjectPath);
    const auto overlaySource = [&sources](const QString &key, const QJsonObject &known) {
        QJsonObject source = sources.value(key).toObject();
        source.remove(QStringLiteral("relativePath"));
        source.remove(QStringLiteral("absolutePath"));
        source.remove(QStringLiteral("fingerprint"));
        for (auto it = known.begin(); it != known.end(); ++it) source.insert(it.key(), it.value());
        if (source.isEmpty()) sources.remove(key);
        else sources.insert(key, source);
    };
    overlaySource(QStringLiteral("video"), video);
    overlaySource(QStringLiteral("telemetry"), telemetry);
    project.insert(QStringLiteral("sources"), sources);
    QJsonObject sync = project.value("sync").toObject();
    sync.insert("offset", m_sync.offset);
    sync.insert("timeScale", m_sync.timeScale);
    project.insert("sync", sync);
    QJsonObject scene = project.value("scene").toObject();
    scene.insert("widgets", m_widgetModel.toJson());
    project.insert("scene", scene);
    QJsonObject analysis = project.value("analysis").toObject();
    analysis.insert("channels", QJsonArray::fromStringList(m_analysisChannels));
    analysis.remove(QStringLiteral("visible"));
    project.insert("analysis", analysis);
    QJsonObject documentState = project.value(QStringLiteral("documentState")).toObject();
    documentState.insert(QStringLiteral("id"), m_documentId);
    documentState.insert(QStringLiteral("savedRevision"), QString::number(
        savedRevision.value_or(m_documentState.lastSavedRevision())));
    project.insert(QStringLiteral("documentState"), documentState);
    if (!project.contains("mapSettings")) {
        project.insert("mapSettings", QJsonObject{{"providerId", "none"}});
    }
    if (!project.contains("exportSettings")) {
        project.insert("exportSettings", QJsonObject{{"quality", "high"}});
    }
    project = eventProject
        ? EventProjectCodec::withEditorState(m_projectTemplate, project, m_documentState.projectPath(), targetProjectPath,
            m_vboLoadState == "ready" ? m_loadedSourceRevision : QByteArray{})
        : project;
    return savedRevision ? projectWithOutingInference(project) : project;
}

bool AppController::saveProject(const QUrl &url)
{
    QString path = url.toLocalFile();
    if (!path.endsWith(".fetproject", Qt::CaseInsensitive)) {
        path.append(".fetproject");
    }
    AppLog::info(QStringLiteral("Project save requested: %1").arg(path));
    const QJsonObject project = currentProjectObject(path, m_documentState.revision());
    QString validationError;
    if (!ProjectLimits::validateProject(project, &validationError)) {
        AppLog::error(QStringLiteral("Project save rejected: %1").arg(validationError));
        setStatus(QStringLiteral("Project save error: %1").arg(validationError));
        if (m_documentState.pendingAction() != ProjectDocumentState::DestructiveAction::None) {
            emit destructiveActionChanged();
        }
        return false;
    }
    const QByteArray payload = QJsonDocument(project).toJson(QJsonDocument::Indented);
    if (payload.size() > ProjectLimits::projectBytes) {
        const QString sizeError = QStringLiteral("Project is %1 bytes; the limit is %2 bytes.")
                                      .arg(payload.size())
                                      .arg(ProjectLimits::projectBytes);
        AppLog::error(QStringLiteral("Project save rejected: %1").arg(sizeError));
        setStatus(QStringLiteral("Project save error: %1").arg(sizeError));
        if (m_documentState.pendingAction() != ProjectDocumentState::DestructiveAction::None) {
            emit destructiveActionChanged();
        }
        return false;
    }
    const ProjectWriter::Result writeResult = m_projectWriter.write(path, payload);
    if (!writeResult.success) {
        AppLog::error(QStringLiteral("Project save failed: %1: %2").arg(path, writeResult.error));
        setStatus(QStringLiteral("Project save error: %1").arg(writeResult.error));
        if (m_documentState.pendingAction() != ProjectDocumentState::DestructiveAction::None) {
            emit destructiveActionChanged();
        }
        return false;
    }
    m_projectTemplate = project;
    if (EventProjectCodec::isEvent(project)) {
        const QJsonObject editor = EventProjectCodec::editorProjection(project);
        m_videoReference = ProjectSourceReferenceCodec::fromProject(editor, QStringLiteral("video"), QStringLiteral("videoPath"));
        m_vboReference = ProjectSourceReferenceCodec::fromProject(editor, QStringLiteral("telemetry"), QStringLiteral("vboPath"));
    }
    m_settings.setValue("project/path", path);
    m_settings.sync();
    m_documentState.markSaved(path);
    m_recoveryTimer.stop();
    clearRecovery(QStringLiteral("successful save"));
    emit documentStateChanged();
    AppLog::info(QStringLiteral("Project save succeeded: %1").arg(path));
    setStatus(QStringLiteral("Project saved: %1").arg(QFileInfo(path).fileName()));
    if (m_documentState.pendingAction() != ProjectDocumentState::DestructiveAction::None) {
        performPendingDestructiveAction();
    }
    return true;
}

void AppController::autoSync()
{
    AppLog::info(QStringLiteral("Auto-sync requested"));
    if (m_syncWatcher.isRunning()) {
        return;
    }
    const QString videoPath = m_videoSource.toLocalFile();
    if (videoPath.isEmpty() || !m_session) {
        setStatus("Open both a GoPro video and telemetry before auto sync.");
        return;
    }
    const TelemetrySession telemetry = *m_session;
    const quint64 generation = m_sourceGeneration;
    const quint64 syncRevision = m_syncRevision;
    const QString normalizedVideoPath = normalizedSourcePath(videoPath);
    const QString normalizedVboPath = normalizedSourcePath(m_telemetryPath);
    m_syncCancellation = std::make_shared<std::atomic_bool>(false);
    const std::shared_ptr<std::atomic_bool> cancellation = m_syncCancellation;
    m_syncCandidate.clear();
    emit syncCandidateChanged();
    setStatus("Indexing GoPro telemetry and matching GPS speed…");
    m_syncWatcher.setFuture(QtConcurrent::run(
        [normalizedVideoPath, normalizedVboPath, telemetry, generation, syncRevision, cancellation] {
        AutoSyncResult result;
        result.generation = generation;
        result.syncRevision = syncRevision;
        result.videoPath = normalizedVideoPath;
        result.vboPath = normalizedVboPath;
        try {
            if (cancellation->load()) {
                result.cancelled = true;
                return result;
            }
            const auto cancelled = [cancellation] { return cancellation->load(); };
            const GoProTelemetryResult videoTelemetry = GoProTelemetrySource::load(
                normalizedVideoPath, cancelled);
            if (cancellation->load()) {
                result.cancelled = true;
                return result;
            }
            result.candidate = TelemetrySyncEngine::synchronize(
                videoTelemetry.session, telemetry, cancelled);
            if (cancellation->load()) {
                result.cancelled = true;
                return result;
            }
            result.packetCount = videoTelemetry.packetCount;
            result.gpsSampleCount = videoTelemetry.session.sampleCount;
            result.gpsStream = videoTelemetry.gpsStream;
            result.success = true;
        } catch (const OperationCancelled &) {
            result.cancelled = true;
            result.error = QStringLiteral("Auto sync was cancelled.");
        } catch (const std::exception &error) {
            result.error = QString::fromUtf8(error.what());
        }
        return result;
    }));
    emit syncingChanged();
}

void AppController::applySyncCandidate()
{
    if (m_syncCandidate.isEmpty()) {
        return;
    }
    const QVariantMap candidate = m_syncCandidate;
    const double offset = candidate.value("offset").toDouble();
    const double scale = m_syncCandidate.value("timeScale", 1.0).toDouble();
    if (!std::isfinite(offset) || !std::isfinite(scale) || scale <= 0.0) {
        setStatus("Synchronization candidate is invalid and cannot be applied.");
        return;
    }
    setSyncOffset(offset);
    setTimeScale(scale);
    AppLog::info(QStringLiteral("Auto-sync candidate applied: offset=%1 s, scale=%2")
                     .arg(offset, 0, 'f', 3).arg(scale, 0, 'g', 12));
    m_syncCandidate = candidate;
    m_syncCandidate.insert("automaticallyApplied", true);
    m_syncCandidate.insert("appliedManually", true);
    emit syncCandidateChanged();
    setStatus(QStringLiteral("Synchronization candidate applied: %1 s.").arg(offset, 0, 'f', 3));
}

void AppController::ignoreSyncCandidate()
{
    if (m_syncCandidate.isEmpty()) {
        return;
    }
    AppLog::info(QStringLiteral("Auto-sync candidate rejected"));
    m_syncCandidate.clear();
    emit syncCandidateChanged();
    setStatus("Synchronization candidate ignored; existing timing was retained.");
}

bool AppController::startExport(
    const QUrl &output,
    const int outputWidth, const int outputHeight, const qint64 frameRateNumerator, const qint64 frameRateDenominator,
    const qint64 videoBitrate,
    const bool audioEnabled,
    const bool customRange,
    const QString &rangeIn,
    const QString &rangeOut,
    const bool overwriteAllowed)
{
    AppLog::info(QStringLiteral("Export requested: %1").arg(output.toLocalFile()));
    if (exporting()) {
        AppLog::warn(QStringLiteral("Export request ignored because an export is already running"));
        return false;
    }
    const QString inputPath = m_videoSource.toLocalFile();
    const QString outputPath = output.toLocalFile();
    if (!m_session || inputPath.isEmpty() || m_telemetryPath.isEmpty() || outputPath.isEmpty()) {
        m_exportError = QStringLiteral("Open a video and telemetry, then choose an output file.");
        m_exportState = QStringLiteral("failed");
        AppLog::error(QStringLiteral("Export failed: %1").arg(m_exportError));
        emit exportChanged();
        return false;
    }
    if (!m_exportSourceInfo.videoSize.isValid()) {
        m_exportError = QStringLiteral("Video metadata is still loading or unavailable.");
        m_exportState = QStringLiteral("failed");
        AppLog::error(QStringLiteral("Export failed: %1").arg(m_exportError));
        emit exportChanged();
        return false;
    }
    if (const QString displayTransformError =
            ExportMediaProfile::unsupportedDisplayTransformError(m_exportSourceInfo);
        !displayTransformError.isEmpty()) {
        m_exportError = displayTransformError;
        m_exportState = QStringLiteral("failed");
        AppLog::error(QStringLiteral("Export failed: %1").arg(m_exportError));
        emit exportChanged();
        return false;
    }
    if (m_exportSourceInfo.sourceColorClass == SourceColorClass::HdrHlg
        || m_exportSourceInfo.sourceColorClass == SourceColorClass::HdrPq
        || m_exportSourceInfo.sourceColorClass == SourceColorClass::LogOrExtended) {
        m_exportError = QStringLiteral(
            "%1 source detected. Color-managed HDR/Log preservation is not yet supported; "
            "export will not silently convert it to SDR.")
                            .arg(sourceColorClassName(m_exportSourceInfo.sourceColorClass));
        m_exportState = QStringLiteral("failed");
        AppLog::error(QStringLiteral("Export failed: %1").arg(m_exportError));
        emit exportChanged();
        return false;
    }
    if (!m_exportSourceInfo.bitDepth) {
        m_exportError = QStringLiteral(
            "Source bit depth is unknown (pixel format: %1); safe preservation cannot be verified.")
                            .arg(m_exportSourceInfo.pixelFormat.isEmpty()
                                     ? QStringLiteral("unknown") : m_exportSourceInfo.pixelFormat);
        m_exportState = QStringLiteral("failed");
        AppLog::error(QStringLiteral("Export failed: %1").arg(m_exportError));
        emit exportChanged();
        return false;
    }
    const QSize outputSize(outputWidth, outputHeight);
    const MediaRational outputRate{frameRateNumerator, frameRateDenominator};
    if (!outputSize.isValid() || outputSize.width() % 2 || outputSize.height() % 2
        || outputSize.width() > m_exportSourceInfo.videoSize.width() || outputSize.height() > m_exportSourceInfo.videoSize.height()
        || !outputRate.isValid() || outputRate.value() > ExportEngine::effectiveFrameRate(m_exportSourceInfo).value()
        || !ExportFormat::validCustomBitrate(videoBitrate)) {
        m_exportError = QStringLiteral("Export format is invalid. Choose an even, non-upscaled size, supported frame rate, and 0.5–500 Mbps bitrate.");
        m_exportState = QStringLiteral("failed"); emit exportChanged(); return false;
    }
    const auto fullRange = ExportEngine::fullVideoFrameRange(m_exportSourceInfo, outputRate);
    const auto selectedRange = customRange
        ? ExportEngine::frameRangeForSourceTimecode(m_exportSourceInfo, outputRate, rangeIn, rangeOut)
        : fullRange;
    if (!selectedRange) {
        m_exportError = QStringLiteral("Export range must use valid inclusive SMPTE IN and OUT timecodes within the source frame domain.");
        m_exportState = QStringLiteral("failed");
        AppLog::error(QStringLiteral("Export failed: %1").arg(m_exportError));
        emit exportChanged();
        return false;
    }
    m_exportOutputTransaction = std::make_unique<ExportOutputTransaction>();
    QStringList protectedPaths{m_telemetryPath};
    if (EventProjectCodec::isEvent(m_projectTemplate)) {
        protectedPaths.append(m_documentState.projectPath());
        protectedPaths.append(EventProjectCodec::referencedPaths(currentProjectObject(), m_documentState.projectPath()));
    }
    const auto preparation = m_exportOutputTransaction->prepare(
        outputPath, inputPath, protectedPaths, overwriteAllowed);
    if (preparation.status == ExportOutputTransaction::PreparationStatus::OverwriteConfirmationRequired) {
        m_exportState = QStringLiteral("overwriteConfirmationRequired");
        m_exportError.clear();
        m_exportProgressInfo = {{"outputPath", m_exportOutputTransaction->userTargetPath()},
                                {"outputName", QFileInfo(outputPath).fileName()},
                                {"targetExistedBeforeExport", true}};
        m_exportOutputTransaction.reset();
        emit exportChanged();
        return false;
    }
    if (preparation.status == ExportOutputTransaction::PreparationStatus::Error) {
        m_exportError = preparation.error;
        m_exportState = QStringLiteral("failed");
        AppLog::error(QStringLiteral("Export failed: %1").arg(m_exportError));
        m_exportOutputTransaction.reset();
        emit exportChanged();
        return false;
    }
    const QString exportId = m_exportOutputTransaction->transactionId();
    const QString temporaryOverlayPath = QDir::temp().filePath(
        QStringLiteral("flappedear-overlay-%1.mkv").arg(exportId));
    const ExportArtifactManifestData manifest{exportId, QDateTime::currentMSecsSinceEpoch(),
        temporaryOverlayPath, m_exportOutputTransaction->stagingPath(),
        m_exportOutputTransaction->userTargetPath(), 0, QStringLiteral("preparing")};
    QString manifestError;
    if (!ExportArtifactManifest::create(manifest, &manifestError)) {
        m_exportError = QStringLiteral("Could not create export ownership manifest: %1").arg(manifestError);
        m_exportState = QStringLiteral("failed");
        AppLog::error(QStringLiteral("Export failed: %1").arg(m_exportError));
        m_exportOutputTransaction.reset();
        emit exportChanged();
        return false;
    }
    m_exportManifestPath = ExportArtifactManifest::manifestPathFor(exportId);
    m_exportConfig = std::make_unique<QTemporaryFile>(
        QDir::temp().filePath(QStringLiteral("flappedear-export-XXXXXX.json")));
    if (!m_exportConfig->open()) {
        m_exportError = QStringLiteral("Could not create temporary export configuration.");
        m_exportState = QStringLiteral("failed");
        AppLog::error(QStringLiteral("Export failed: %1").arg(m_exportError));
        static_cast<void>(ExportArtifactManifest::cleanupOwned(m_exportManifestPath));
        m_exportManifestPath.clear();
        m_exportOutputTransaction.reset();
        emit exportChanged();
        return false;
    }
    m_exportCancelPath = m_exportConfig->fileName() + QStringLiteral(".cancel");
    m_exportSupervisionReadyPath = m_exportConfig->fileName() + QStringLiteral(".supervision-ready");
    QFile::remove(m_exportCancelPath);
    QFile::remove(m_exportSupervisionReadyPath);
    const QJsonObject config = {
        {"inputPath", inputPath},
        {"outputPath", m_exportOutputTransaction->stagingPath()},
        {"vboPath", m_telemetryPath},
        {"lapBinding", activeLapBinding()},
        {"lapExclusions", currentProjectObject().value("event").toObject().value("lapExclusions").toArray()},
        {"widgets", m_widgetModel.toJson()},
        {"sync", QJsonObject{{"offset", m_sync.offset}, {"timeScale", m_sync.timeScale}}},
        {"outputWidth", outputSize.width()}, {"outputHeight", outputSize.height()},
        {"frameRateNumerator", outputRate.numerator}, {"frameRateDenominator", outputRate.denominator},
        {"videoBitrate", videoBitrate},
        {"audioEnabled", audioEnabled},
        {"firstFrame", selectedRange->firstFrame},
        {"lastFrame", selectedRange->lastFrame},
        {"cancelPath", m_exportCancelPath},
        {"supervisionReadyPath", m_exportSupervisionReadyPath},
        {"temporaryOverlayPath", temporaryOverlayPath},
        {"manifestPath", m_exportManifestPath},
    };
    if (m_exportConfig->write(QJsonDocument(config).toJson(QJsonDocument::Compact)) < 0) {
        m_exportError = QStringLiteral("Could not write temporary export configuration.");
        m_exportState = QStringLiteral("failed");
        AppLog::error(QStringLiteral("Export failed: %1").arg(m_exportError));
        static_cast<void>(ExportArtifactManifest::cleanupOwned(m_exportManifestPath));
        m_exportManifestPath.clear();
        m_exportOutputTransaction.reset();
        m_exportConfig.reset();
        emit exportChanged();
        return false;
    }
    m_exportConfig->flush();
    // The worker is a separate process; closing before it starts avoids a
    // Windows sharing violation while the controller retains ownership for
    // cleanup after completion.
    m_exportConfig->close();
    m_exportStdout.clear();
    m_exportStderr = BoundedProcessOutput(BoundedProcessOutput::Mode::DiagnosticTail,
                                          ProcessOutputLimits::ffmpegDiagnosticTailBytes);
    m_exportProgress = 0;
    m_exportError.clear();
    m_exportMetrics.clear();
    m_exportDiagnosticLog.clear();
    const QDateTime exportStarted = QDateTime::currentDateTime();
    const QString exportLogDirectory = QDir(
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)).filePath(
        QStringLiteral("exports"));
    const QString sourceRate = QStringLiteral("%1/%2")
                                   .arg(m_exportSourceInfo.frameRate.numerator)
                                   .arg(m_exportSourceInfo.frameRate.denominator);
    const QString outputRateText = QStringLiteral("%1/%2")
                                      .arg(outputRate.numerator)
                                      .arg(outputRate.denominator);
    const QString exportHeader = QStringLiteral(
        "Flapped Ear Telemetry Export Log\n\n"
        "Started: %1\n"
        "Export ID: %2\n"
        "Application version: %3\n\n"
        "Source:\n"
        "  Path: %4\n"
        "  Video: %5x%6, %7 fps, codec %8, duration %9 s\n\n"
        "Output:\n"
        "  Target: %10\n"
        "  Requested: %11x%12, %13 fps\n"
        "  Video bitrate: %14 bps\n"
        "  Audio: %15\n\n"
        "Range: %16 -> %17 (inclusive)\n")
        .arg(exportStarted.toString(Qt::ISODate), exportId, QCoreApplication::applicationVersion(), inputPath)
        .arg(m_exportSourceInfo.videoSize.width()).arg(m_exportSourceInfo.videoSize.height())
        .arg(sourceRate, m_exportSourceInfo.videoCodec)
        .arg(m_exportSourceInfo.duration, 0, 'f', 3)
        .arg(m_exportOutputTransaction->userTargetPath())
        .arg(outputSize.width()).arg(outputSize.height()).arg(outputRateText)
        .arg(videoBitrate)
        .arg(audioEnabled ? QStringLiteral("enabled, AAC %1 bps").arg(ExportFormat::audioBitrate)
                           : QStringLiteral("disabled"))
        .arg(ExportEngine::formatSmpteTimecode(selectedRange->firstFrame, outputRate),
             ExportEngine::formatSmpteTimecode(selectedRange->lastFrame, outputRate));
    QString exportLogError;
    m_persistentExportLog = PersistentExportLog::create(
        exportLogDirectory, exportId, exportHeader, &exportLogError, exportStarted);
    if (m_persistentExportLog) {
        PersistentExportLog::retainNewest(
            exportLogDirectory, m_persistentExportLog->path());
        AppLog::info(QStringLiteral("Export diagnostics: %1").arg(m_persistentExportLog->path()));
    } else {
        AppLog::warn(QStringLiteral("Could not create export diagnostic log: %1").arg(exportLogError));
    }
    m_exportProgressInfo = {{"outputPath", m_exportOutputTransaction->userTargetPath()},
                            {"stagingPath", m_exportOutputTransaction->stagingPath()},
                            {"outputName", QFileInfo(outputPath).fileName()},
                            {"targetExistedBeforeExport", m_exportOutputTransaction->targetExistedBeforeExport()},
                            {"syncOffset", m_sync.offset}, {"timeScale", m_sync.timeScale},
                            {"width", outputSize.width()}, {"height", outputSize.height()},
                            {"frameRate", outputRate.value()}, {"videoBitrate", videoBitrate},
                            {"audioLabel", audioEnabled ? QStringLiteral("AAC audio") : QStringLiteral("No audio")}};
    m_exportProgressVisible = true;
    m_exportState = QStringLiteral("starting");
    appendExportLifecycle(QStringLiteral("Preparing"));
    AppLog::info(QStringLiteral("Export Stage A preparing"));
    m_exportProcess = std::make_unique<QProcess>(this);
    m_exportSupervisor = std::make_unique<ExportProcessSupervisor>(*m_exportProcess);
    m_exportProcess->setProcessChannelMode(QProcess::SeparateChannels);
    connect(m_exportProcess.get(), &QProcess::readyReadStandardOutput, this, &AppController::handleExportOutput);
    connect(
        m_exportProcess.get(),
        qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
        this,
        [this, process = QPointer<QProcess>(m_exportProcess.get())](int code, QProcess::ExitStatus status) {
            if (process && process == m_exportProcess.get()) finishExport(code, status);
        },
        Qt::QueuedConnection);
    m_exportSupervisor->start(
        QCoreApplication::applicationFilePath(), {"--export-worker", m_exportConfig->fileName()});
    if (!m_exportSupervisor->waitForStarted(5'000)) {
        const QString supervisionError = m_exportSupervisor->supervisionError();
        m_exportError = supervisionError.isEmpty()
            ? QStringLiteral("Could not start export worker: %1").arg(m_exportProcess->errorString())
            : QStringLiteral("Could not establish export process supervision: %1").arg(supervisionError);
        m_exportState = QStringLiteral("failed");
        AppLog::error(QStringLiteral("Export failed: %1").arg(m_exportError));
        finishExport(1, QProcess::CrashExit);
        return false;
    }
    if (!m_exportSupervisor->supervisionActive()) {
        m_exportError = QStringLiteral("Export process supervision was not established.");
        static_cast<void>(m_exportSupervisor->stopAndWait());
        m_exportState = QStringLiteral("failed");
        AppLog::error(QStringLiteral("Export failed: %1").arg(m_exportError));
        finishPersistentExportLog(QStringLiteral("FAILED"), m_exportError);
        emit exportChanged();
        return false;
    }
    ExportArtifactManifestData activeManifest;
    if (!ExportArtifactManifest::read(m_exportManifestPath, &activeManifest, &manifestError)) {
        m_exportError = QStringLiteral("Could not read active export ownership manifest: %1").arg(manifestError);
        static_cast<void>(m_exportSupervisor->stopAndWait());
        m_exportState = QStringLiteral("failed");
        AppLog::error(QStringLiteral("Export failed: %1").arg(m_exportError));
        finishPersistentExportLog(QStringLiteral("FAILED"), m_exportError);
        emit exportChanged();
        return false;
    }
    activeManifest.workerPid = m_exportProcess->processId();
    if (!ExportArtifactManifest::update(m_exportManifestPath, activeManifest, &manifestError)) {
        m_exportError = QStringLiteral("Could not update active export ownership manifest: %1").arg(manifestError);
        static_cast<void>(m_exportSupervisor->stopAndWait());
        m_exportState = QStringLiteral("failed");
        AppLog::error(QStringLiteral("Export failed: %1").arg(m_exportError));
        finishPersistentExportLog(QStringLiteral("FAILED"), m_exportError);
        emit exportChanged();
        return false;
    }
    QFile supervisionReady(m_exportSupervisionReadyPath);
    if (!supervisionReady.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
        m_exportError = QStringLiteral("Could not release supervised export worker: %1")
                            .arg(supervisionReady.errorString());
        static_cast<void>(m_exportSupervisor->stopAndWait());
        m_exportState = QStringLiteral("failed");
        AppLog::error(QStringLiteral("Export failed: %1").arg(m_exportError));
        finishPersistentExportLog(QStringLiteral("FAILED"), m_exportError);
        emit exportChanged();
        return false;
    }
    supervisionReady.close();
    emit exportChanged();
    return true;
}

QString AppController::exportFullRangeTimecode(
    const qint64 frameRateNumerator, const qint64 frameRateDenominator, const bool outPoint) const
{
    const MediaRational rate{frameRateNumerator, frameRateDenominator};
    const auto range = ExportEngine::fullVideoFrameRange(m_exportSourceInfo, rate);
    return range ? ExportEngine::formatSmpteTimecode(
        outPoint ? range->lastFrame : range->firstFrame, rate) : QString();
}

QVariantMap AppController::lapExportRange(
    const int lapNumber, const qint64 frameRateNumerator, const qint64 frameRateDenominator,
    const int handleSeconds) const
{
    const MediaRational rate{frameRateNumerator, frameRateDenominator};
    const auto fullRange = ExportEngine::fullVideoFrameRange(m_exportSourceInfo, rate);
    if (!fullRange || handleSeconds < 0 || handleSeconds > 30) {
        return {{QStringLiteral("valid"), false}};
    }
    const auto lap = std::find_if(m_lapSession.timedLaps.cbegin(), m_lapSession.timedLaps.cend(),
                                  [lapNumber](const TimedLap &candidate) {
        return candidate.number == lapNumber;
    });
    if (lap == m_lapSession.timedLaps.cend()) return {{QStringLiteral("valid"), false}};

    const auto videoStart = telemetryToVideoTime(lap->startTelemetryTime, m_sync);
    const auto videoEnd = telemetryToVideoTime(lap->startTelemetryTime + lap->durationSeconds, m_sync);
    if (!videoStart || !videoEnd || *videoEnd < *videoStart) {
        return {{QStringLiteral("valid"), false}};
    }
    const auto requestedFirst = frameAtOrBeforePresentationTime(
        std::max(0.0, *videoStart - static_cast<double>(handleSeconds)), rate);
    const auto requestedLast = frameAtOrBeforePresentationTime(
        std::max(0.0, *videoEnd + static_cast<double>(handleSeconds)), rate);
    if (!requestedFirst || !requestedLast) return {{QStringLiteral("valid"), false}};

    const auto range = ExportEngine::frameRangeFromInclusiveFrames(
        qBound(fullRange->firstFrame, *requestedFirst, fullRange->lastFrame),
        qBound(fullRange->firstFrame, *requestedLast, fullRange->lastFrame));
    if (!range) return {{QStringLiteral("valid"), false}};
    return {{QStringLiteral("valid"), true}, {QStringLiteral("lapNumber"), lapNumber},
            {QStringLiteral("handleSeconds"), handleSeconds},
            {QStringLiteral("firstFrame"), range->firstFrame},
            {QStringLiteral("lastFrame"), range->lastFrame},
            {QStringLiteral("inTimecode"), ExportEngine::formatSmpteTimecode(range->firstFrame, rate)},
            {QStringLiteral("outTimecode"), ExportEngine::formatSmpteTimecode(range->lastFrame, rate)},
            {QStringLiteral("durationSeconds"), static_cast<double>(range->frameCount())
                * static_cast<double>(rate.denominator) / static_cast<double>(rate.numerator)}};
}

double AppController::exportRangeDurationSeconds(
    const qint64 frameRateNumerator, const qint64 frameRateDenominator,
    const QString &rangeIn, const QString &rangeOut) const
{
    const MediaRational rate{frameRateNumerator, frameRateDenominator};
    const auto range = ExportEngine::frameRangeForSourceTimecode(
        m_exportSourceInfo, rate, rangeIn, rangeOut);
    return range ? static_cast<double>(range->frameCount())
            * static_cast<double>(rate.denominator) / static_cast<double>(rate.numerator) : 0.0;
}

void AppController::cancelExport()
{
    if (!exporting()) {
        return;
    }
    m_exportState = QStringLiteral("cancelling");
    m_exportProgressInfo.insert("stage", QStringLiteral("cancelling"));
    const ExportCancellationResult cancellation =
        ExportCancellation::request(m_exportCancelPath, m_exportSupervisor.get());
    if (cancellation.markerCreated) {
        AppLog::warn(QStringLiteral("Export cancellation requested"));
        appendExportLifecycle(QStringLiteral("Cancellation requested"));
        emit exportChanged();
        return;
    }

    const QString reason = cancellation.error.isEmpty()
        ? QStringLiteral("unknown cancellation marker error") : cancellation.error;
    AppLog::error(QStringLiteral("Export cancellation marker creation failed: %1").arg(reason));
    appendExportLifecycle(QStringLiteral("Cancellation marker failed; supervised stop requested"));
    if (cancellation.workerStopped) {
        m_exportError = QStringLiteral(
            "Could not create the export cancellation marker; the export worker was stopped: %1.")
                            .arg(reason);
        // stopAndWait completed before publishing this terminal state. The
        // finished callback retains the same error and performs cleanup.
        m_exportState = QStringLiteral("failed");
        AppLog::error(QStringLiteral("Export cancelled by supervised fallback: %1").arg(m_exportError));
    } else {
        m_exportError = QStringLiteral(
            "Could not create the export cancellation marker and the supervised worker is still stopping: %1.")
                            .arg(reason);
        // Do not claim a terminal state while a process may still own export
        // artifacts. finishExport will publish the final result on exit.
        AppLog::error(QStringLiteral("Export cancellation fallback is still stopping: %1").arg(m_exportError));
    }
    emit exportChanged();
}

void AppController::cancelExportAndQuit()
{
    if (!exporting()) {
        requestQuit();
        return;
    }
    m_quitAfterExport = true;
    cancelExport();
    // Escalate through the dedicated process-tree owner: the worker and every
    // inherited FFmpeg/ffprobe descendant are stopped as one lifetime unit.
    QTimer::singleShot(7'000, this, [this] {
        if (!exporting()) {
            return;
        }
        if (m_exportSupervisor) static_cast<void>(m_exportSupervisor->stopAndWait(3'000, 3'000));
    });
}

void AppController::dismissExportProgress()
{
    if (exporting() || m_exportState == "cancelling") return;
    m_exportProgressVisible = false;
    if (m_exportState == "complete" || m_exportState == "validationWarning"
        || m_exportState == "cancelled") {
        m_exportState = QStringLiteral("idle");
    }
    emit exportChanged();
}

void AppController::copyExportDiagnostics()
{
    if (QGuiApplication::clipboard()) {
        QGuiApplication::clipboard()->setText(m_exportDiagnosticLog.text());
    }
}

void AppController::appendExportDiagnostic(const QString &entry)
{
    m_exportDiagnosticLog.append(entry);
    if (m_persistentExportLog && !m_persistentExportLog->append(entry)) {
        AppLog::warn(QStringLiteral("Could not append export diagnostic log: %1")
                         .arg(m_persistentExportLog->path()));
        m_persistentExportLog.reset();
    }
}

void AppController::appendExportLifecycle(const QString &event)
{
    appendExportDiagnostic(QStringLiteral("[lifecycle] %1").arg(event));
}

void AppController::finishPersistentExportLog(const QString &result, const QString &error)
{
    if (!m_persistentExportLog) return;
    QString footer = QStringLiteral("\nFinished: %1\nResult: %2\n")
                         .arg(QDateTime::currentDateTime().toString(Qt::ISODate), result);
    if (!error.isEmpty()) footer += QStringLiteral("Error: %1\n").arg(error);
    const auto value = [this](const QString &key) { return m_exportProgressInfo.value(key).toString(); };
    if (result == QStringLiteral("SUCCESS")) {
        footer += QStringLiteral("Output: %1x%2\nAverage FPS: %3\nEncoded frames: %4\n"
                                 "Output bytes: %5\nEncoder: %6\nValidation: %7\n")
                      .arg(value(QStringLiteral("outputWidth")), value(QStringLiteral("outputHeight")),
                           value(QStringLiteral("outputAverageFrameRate")), value(QStringLiteral("encodedFrames")),
                           value(QStringLiteral("outputBytes")), value(QStringLiteral("encoderName")),
                           m_exportState == QStringLiteral("validationWarning")
                               ? QStringLiteral("warning") : QStringLiteral("passed"));
    }
    appendExportDiagnostic(footer.trimmed());
    m_persistentExportLog.reset();
}

namespace {

QString diagnosticTimestamp(const qint64 elapsedMilliseconds)
{
    const qint64 hours = elapsedMilliseconds / 3'600'000;
    const qint64 minutes = (elapsedMilliseconds / 60'000) % 60;
    const qint64 seconds = (elapsedMilliseconds / 1'000) % 60;
    const qint64 milliseconds = elapsedMilliseconds % 1'000;
    return QStringLiteral("%1:%2:%3.%4")
        .arg(hours, 2, 10, QLatin1Char('0'))
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(seconds, 2, 10, QLatin1Char('0'))
        .arg(milliseconds, 3, 10, QLatin1Char('0'));
}

QString diagnosticValue(const QVariant &value)
{
    if (value.metaType().id() == QMetaType::QStringList) {
        return value.toStringList().join(QLatin1Char(' '));
    }
    if (value.metaType().id() == QMetaType::QVariantList) {
        QStringList items;
        for (const QVariant &item : value.toList()) items.append(item.toString());
        return items.join(QLatin1Char(' '));
    }
    return value.toString();
}

QString formatDiagnosticEvent(const QJsonObject &event)
{
    QString result = QStringLiteral("[%1] %2")
                         .arg(diagnosticTimestamp(event.value("timestampMilliseconds").toInteger()),
                              event.value("message").toString());
    const QVariantMap details = event.value("details").toObject().toVariantMap();
    QStringList keys = details.keys();
    std::sort(keys.begin(), keys.end());
    for (const QString &key : std::as_const(keys)) {
        const QString value = diagnosticValue(details.value(key));
        if (!value.isEmpty()) result += QStringLiteral("\n    %1: %2").arg(key, value);
    }
    const QString error = event.value("error").toString();
    if (!error.isEmpty()) result += QStringLiteral("\n    error: %1").arg(error);
    const QString diagnostics = event.value("diagnostics").toString();
    if (!diagnostics.isEmpty()) result += QStringLiteral("\n    diagnostics:\n%1").arg(diagnostics);
    return result;
}

} // namespace

void AppController::handleExportOutput()
{
    if (!m_exportProcess) {
        return;
    }
    m_exportStdout.append(m_exportProcess->readAllStandardOutput());
    m_exportStderr.append(m_exportProcess->readAllStandardError());
    if (m_exportStdout.size() > ProcessOutputLimits::workerMessageBytes) {
        m_exportStdout.clear();
        m_exportError = QStringLiteral("Export worker emitted a message longer than %1 bytes.")
                            .arg(ProcessOutputLimits::workerMessageBytes);
        AppLog::error(m_exportError);
        if (m_exportSupervisor) static_cast<void>(m_exportSupervisor->stopAndWait());
        return;
    }
    qsizetype newline = -1;
    while ((newline = m_exportStdout.indexOf('\n')) >= 0) {
        const QByteArray line = m_exportStdout.left(newline);
        m_exportStdout.remove(0, newline + 1);
        const QJsonObject event = QJsonDocument::fromJson(line).object();
        if (event.value("type").toString() == QStringLiteral("log")) {
            appendExportDiagnostic(formatDiagnosticEvent(event));
        }
        const QString state = event.value("state").toString();
        if (!state.isEmpty()) {
            if (state != m_exportState) {
                if (state == QStringLiteral("renderingOverlay")) {
                    AppLog::info(QStringLiteral("Export Stage A started"));
                    appendExportLifecycle(QStringLiteral("Stage A started"));
                } else if (state == QStringLiteral("validatingOverlay")) {
                    AppLog::info(QStringLiteral("Export Stage A ended"));
                    AppLog::info(QStringLiteral("Export overlay validation started"));
                    appendExportLifecycle(QStringLiteral("Stage A completed; temporary overlay validation started"));
                } else if (state == QStringLiteral("encodingVideo")) {
                    AppLog::info(QStringLiteral("Export overlay validation passed"));
                    AppLog::info(QStringLiteral("Export Stage B started"));
                    appendExportLifecycle(QStringLiteral("Temporary overlay validation passed; Stage B started"));
                } else if (state == QStringLiteral("validatingOutput")) {
                    AppLog::info(QStringLiteral("Export Stage B ended"));
                    AppLog::info(QStringLiteral("Export final validation started"));
                    appendExportLifecycle(QStringLiteral("Stage B completed; final validation started"));
                } else if (state == QStringLiteral("complete")) {
                    AppLog::info(QStringLiteral("Export final validation passed"));
                    appendExportLifecycle(QStringLiteral("Final validation passed"));
                } else if (state == QStringLiteral("validationWarning")) {
                    AppLog::warn(QStringLiteral("Export validation completed with a warning"));
                    appendExportLifecycle(QStringLiteral("Validation warning"));
                } else if (state == QStringLiteral("cancelled")) {
                    AppLog::warn(QStringLiteral("Export cancelled"));
                    appendExportLifecycle(QStringLiteral("Cancelled"));
                }
            }
            m_exportState = state;
        }
        for (const QString &key : {QStringLiteral("generatedFrames"), QStringLiteral("renderedFrames"), QStringLiteral("expectedFrames"),
                                   QStringLiteral("sourceRangeStart"), QStringLiteral("sourceRangeEnd"),
                                   QStringLiteral("exportDuration"), QStringLiteral("exportRelativeTime"),
                                   QStringLiteral("sourceVideoTime"),
                                   QStringLiteral("telemetryTime"), QStringLiteral("elapsedMilliseconds"),
                                   QStringLiteral("throughputFps"), QStringLiteral("realtimeFactor"),
                                   QStringLiteral("etaSeconds"), QStringLiteral("outputBytes"),
                                   QStringLiteral("encoderId"), QStringLiteral("encoderName"),
                                   QStringLiteral("width"), QStringLiteral("height"), QStringLiteral("frameRate"),
                                   QStringLiteral("audioEnabled"), QStringLiteral("encodedFrames"),
                                   QStringLiteral("encodedSeconds"), QStringLiteral("encodedProgress"),
                                   QStringLiteral("encoderFps"), QStringLiteral("encoderRealtimeFactor"),
                                   QStringLiteral("rendererFps"), QStringLiteral("queuedBytes"),
                                   QStringLiteral("maximumQueuedBytes"), QStringLiteral("temporaryOverlayBytes"),
                                   QStringLiteral("estimatedTemporaryOverlayBytes"),
                                   QStringLiteral("estimatedFinalOutputBytes"),
                                   QStringLiteral("safetyReserveBytes"),
                                   QStringLiteral("estimateBasis"), QStringLiteral("sampleFrames"),
                                   QStringLiteral("sampleEncodedBytes"), QStringLiteral("sampleBytesPerFrame"),
                                   QStringLiteral("sampleSafetyMargin"), QStringLiteral("sampleError"),
                                   QStringLiteral("temporaryFilesystemRoot"),
                                   QStringLiteral("temporaryFilesystemInspectedPath"),
                                   QStringLiteral("temporaryFilesystemProbePath"),
                                   QStringLiteral("temporaryFilesystemAvailableBytes"),
                                   QStringLiteral("destinationFilesystemRoot"),
                                   QStringLiteral("destinationFilesystemInspectedPath"),
                                   QStringLiteral("destinationFilesystemProbePath"),
                                   QStringLiteral("destinationFilesystemAvailableBytes"),
                                   QStringLiteral("outputBytes"), QStringLiteral("currentOperation"),
                                   QStringLiteral("operation"), QStringLiteral("stageElapsedMilliseconds"),
                                   QStringLiteral("totalElapsedMilliseconds"), QStringLiteral("stageDurations"),
                                   QStringLiteral("outputVideoCodec"), QStringLiteral("outputWidth"),
                                   QStringLiteral("outputVideoProfile"),
                                   QStringLiteral("outputPixelFormat"), QStringLiteral("outputBitDepth"),
                                   QStringLiteral("outputColorRange"), QStringLiteral("outputColorSpace"),
                                   QStringLiteral("outputColorTransfer"), QStringLiteral("outputColorPrimaries"),
                                   QStringLiteral("outputHeight"), QStringLiteral("outputDuration"),
                                   QStringLiteral("outputVideoDuration"), QStringLiteral("outputVideoStart"),
                                   QStringLiteral("outputVideoPacketCount"),
                                   QStringLiteral("outputAverageFrameRate"),
                                   QStringLiteral("outputAudioCodecs"), QStringLiteral("outputAudioStart"),
                                   QStringLiteral("outputAudioDuration"),
                                   QStringLiteral("exportFrameRateNumerator"),
                                   QStringLiteral("exportFrameRateDenominator"),
                                   QStringLiteral("exportFrameRate"), QStringLiteral("diagnostics"),
                                   QStringLiteral("warning"), QStringLiteral("sourceFrameCount"),
                                   QStringLiteral("sourceFrameCountSource"), QStringLiteral("firstFrame"),
                                   QStringLiteral("lastFrame"), QStringLiteral("finalFrameCount"),
                                   QStringLiteral("frameDeficit"), QStringLiteral("resultClassification")}) {
            if (event.contains(key)) m_exportProgressInfo.insert(key, event.value(key).toVariant());
        }
        if (!state.isEmpty()) m_exportProgressInfo.insert("stage", state);
        if (event.contains("visibleProgress")) {
            m_exportProgress = qRound(event.value("visibleProgress").toDouble());
            m_exportProgressInfo.insert("progressPercent", event.value("visibleProgress").toDouble());
        } else if (state == "validatingOverlay") {
            m_exportProgress = qMax(m_exportProgress, 60);
            m_exportProgressInfo.insert("progressPercent", m_exportProgress);
        }
        if (state == "validatingOutput" || state == "validating") {
            m_exportProgress = qMax(m_exportProgress, 99);
            m_exportProgressInfo.insert("progressPercent", m_exportProgress);
        }
        if (event.contains("error")) {
            m_exportError = event.value("error").toString();
            AppLog::error(QStringLiteral("Export failed: %1").arg(m_exportError));
        }
        if (event.contains("renderMilliseconds")) {
            m_exportMetrics = {
                {"elapsedMilliseconds", event.value("elapsedMilliseconds").toInteger()},
                {"renderMilliseconds", event.value("renderMilliseconds").toInteger()},
                {"renderNanoseconds", event.value("renderNanoseconds").toInteger()},
                {"polishNanoseconds", event.value("polishNanoseconds").toInteger()},
                {"syncRenderNanoseconds", event.value("syncRenderNanoseconds").toInteger()},
                {"readbackNanoseconds", event.value("readbackNanoseconds").toInteger()},
                {"cpuCopyNanoseconds", event.value("cpuCopyNanoseconds").toInteger()},
                {"ffmpegWriteNanoseconds", event.value("ffmpegWriteNanoseconds").toInteger()},
                {"renderedFrames", event.value("renderedFrames").toInteger()},
            };
        }
        emit exportChanged();
    }
}

void AppController::finishExport(const int exitCode, const QProcess::ExitStatus exitStatus)
{
    if (!m_exportProcess) return;
    const bool remainingWriters = m_exportSupervisor && m_exportSupervisor->isRunning();
    // A worker exit is not a process-tree completion boundary. Stop any
    // remaining writers before reading their final output, committing or cleanup.
    if (!m_exportSupervisor || !m_exportSupervisor->stopAndWait()) {
        m_exportState = QStringLiteral("cancelling");
        m_exportError = QStringLiteral("Export processes are still stopping; temporary files were retained.");
        m_exportProgressInfo.insert("stage", m_exportState);
        emit exportChanged();
        QTimer::singleShot(1'000, this,
            [this, process = QPointer<QProcess>(m_exportProcess.get()), exitCode, exitStatus] {
                if (process && process == m_exportProcess.get()) finishExport(exitCode, exitStatus);
            });
        return;
    }
    handleExportOutput();
    const bool cancelled = QFileInfo::exists(m_exportCancelPath);
    if (remainingWriters && !cancelled) {
        m_exportState = QStringLiteral("failed");
        m_exportError = QStringLiteral("Export worker exited with child processes still running; output was not committed.");
    }
    if (m_exportProcess) m_exportStderr.append(m_exportProcess->readAllStandardError());
    const QString workerError = m_exportStderr.text();
    QString persistentResult;
    QString persistentError;
    if (cancelled) {
        m_exportState = QStringLiteral("cancelled");
        m_exportError.clear();
        setStatus("Export cancelled.");
        persistentResult = QStringLiteral("CANCELLED");
    } else if (exitStatus == QProcess::NormalExit && exitCode == 0 && m_exportError.isEmpty()
               && (m_exportState == QStringLiteral("complete")
                   || m_exportState == QStringLiteral("validationWarning"))) {
        QString commitError;
        if (m_exportOutputTransaction && m_exportOutputTransaction->commit(&commitError)) {
            ExportArtifactManifestData manifest;
            if (ExportArtifactManifest::read(m_exportManifestPath, &manifest)) {
                manifest.state = QStringLiteral("completed");
                static_cast<void>(ExportArtifactManifest::update(m_exportManifestPath, manifest));
            }
            m_exportProgress = 100;
            const bool warning = m_exportState == QStringLiteral("validationWarning");
            m_exportState = warning ? QStringLiteral("validationWarning") : QStringLiteral("complete");
            m_exportError.clear();
            AppLog::info(QStringLiteral("Export succeeded: %1")
                             .arg(m_exportOutputTransaction->userTargetPath()));
            setStatus(warning ? QStringLiteral("HEVC export finished with a validation warning.")
                              : QStringLiteral("HEVC export finished and passed validation."));
            persistentResult = warning ? QStringLiteral("SUCCESS_WITH_WARNING") : QStringLiteral("SUCCESS");
        } else {
            m_exportState = QStringLiteral("failed");
            m_exportError = commitError.isEmpty()
                ? QStringLiteral("Validated export could not be committed to its target.") : commitError;
            AppLog::error(QStringLiteral("Export failed: %1").arg(m_exportError));
            setStatus(QStringLiteral("Export failed: %1").arg(m_exportError));
            persistentResult = QStringLiteral("FAILED");
            persistentError = m_exportError;
        }
    } else {
        m_exportState = QStringLiteral("failed");
        if (m_exportError.isEmpty()) {
            m_exportError = workerError.isEmpty() ? QStringLiteral("Export worker failed.") : workerError;
        }
        AppLog::error(QStringLiteral("Export failed: %1").arg(m_exportError));
        setStatus(QStringLiteral("Export failed: %1").arg(m_exportError));
        persistentResult = QStringLiteral("FAILED");
        persistentError = m_exportError;
    }
    m_exportProgressInfo.insert("stage", m_exportState);
    m_exportProgressInfo.insert("progressPercent", m_exportProgress);
    QFile::remove(m_exportCancelPath);
    QFile::remove(m_exportSupervisionReadyPath);
    appendExportLifecycle(QStringLiteral("Cleanup started"));
    QString cleanupError;
    if (!m_exportManifestPath.isEmpty()
        && !ExportArtifactManifest::cleanupOwned(m_exportManifestPath, &cleanupError)) {
        appendExportDiagnostic(QStringLiteral("Owned export cleanup deferred: %1").arg(cleanupError));
    }
    appendExportLifecycle(cleanupError.isEmpty() ? QStringLiteral("Cleanup completed")
                                                 : QStringLiteral("Cleanup deferred"));
    finishPersistentExportLog(persistentResult, persistentError);
    m_exportManifestPath.clear();
    m_exportSupervisor.reset();
    m_exportProcess.reset();
    m_exportConfig.reset();
    m_exportOutputTransaction.reset();
    emit exportChanged();
    if (m_quitAfterExport) {
        m_quitAfterExport = false;
        requestQuit();
    }
}

void AppController::saveWindowState(const int x, const int y, const int width, const int height)
{
    m_settings.setValue("window/x", x);
    m_settings.setValue("window/y", y);
    m_settings.setValue("window/width", width);
    m_settings.setValue("window/height", height);
    m_settings.sync();
}

void AppController::saveAnalysisWindowState(
    const int x,
    const int y,
    const int width,
    const int height,
    const int sidebarWidth,
    const int videoHeight)
{
    m_settings.setValue("analysis/windowX", x);
    m_settings.setValue("analysis/windowY", y);
    m_settings.setValue("analysis/windowWidth", width);
    m_settings.setValue("analysis/windowHeight", height);
    m_settings.setValue("analysis/sidebarWidth", sidebarWidth);
    m_settings.setValue("analysis/videoHeight", videoHeight);
    m_settings.sync();
}

void AppController::setPlaybackTime(const double seconds)
{
    if (qFuzzyCompare(m_playbackTime, seconds)) {
        return;
    }
    m_playbackTime = seconds;
    m_previewRenderContext.setTime(seconds);
    emit playbackTimeChanged();
    emit liveValuesChanged();
}

void AppController::invalidateSyncForTimingEdit()
{
    ++m_syncRevision;
    if (m_syncCancellation) m_syncCancellation->store(true);
    if (!m_syncCandidate.isEmpty()) {
        m_syncCandidate.clear();
        emit syncCandidateChanged();
    }
}

void AppController::setSyncOffset(const double seconds)
{
    if (!std::isfinite(seconds) || qFuzzyCompare(m_sync.offset, seconds)) {
        return;
    }
    invalidateSyncForTimingEdit();
    m_sync.offset = seconds;
    m_previewRenderContext.setSyncTransform(m_sync);
    emit syncChanged();
    emit lapNavigationChanged();
    emit liveValuesChanged();
    markPersistentChange(); // also emits documentStateChanged, which refreshes outingLapVideoAvailable/Position
}

void AppController::setTimeScale(const double scale)
{
    if (!std::isfinite(scale) || scale <= 0.0 || qFuzzyCompare(m_sync.timeScale, scale)) {
        return;
    }
    invalidateSyncForTimingEdit();
    m_sync.timeScale = scale;
    m_previewRenderContext.setSyncTransform(m_sync);
    emit syncChanged();
    emit lapNavigationChanged();
    emit liveValuesChanged();
    markPersistentChange(); // also emits documentStateChanged, which refreshes outingLapVideoAvailable/Position
}

void AppController::setAnalysisChannels(const QStringList &channels)
{
    QStringList normalized;
    for (const QString &channel : channels) {
        if (!channel.isEmpty() && !normalized.contains(channel)
            && (!m_session || m_session->channels.contains(channel))) {
            normalized.append(channel);
        }
        if (normalized.size() == 4) {
            break;
        }
    }
    if (normalized == m_analysisChannels) {
        return;
    }
    m_analysisChannels = normalized;
    emit analysisChanged();
    markPersistentChange();
}

void AppController::setAnalysisVisible(const bool visible)
{
    if (visible == m_analysisVisible) {
        return;
    }
    m_analysisVisible = visible;
    emit analysisChanged();
}

QVariant AppController::semanticValue(const QString &alias) const
{
    if (!m_session) {
        return {};
    }
    const auto time = videoToTelemetryTime(m_playbackTime, m_sync);
    if (!time) return {};
    const auto value = m_session->valueAt(alias, *time);
    return value ? QVariant(*value) : QVariant();
}

void AppController::setStatus(QString status)
{
    if (m_statusText == status) {
        return;
    }
    AppLog::info(QStringLiteral("Status: %1").arg(status));
    m_statusText = std::move(status);
    emit statusTextChanged();
}

void AppController::markPersistentChange()
{
    if (m_suppressDirtyTracking) {
        return;
    }
    m_documentState.markChanged();
    emit documentStateChanged();
    scheduleRecoveryWrite();
}

void AppController::scheduleRecoveryWrite()
{
    if (!m_recoveryPending && m_documentState.dirty()) {
        // A normal edit retries immediately; a failed write below schedules a
        // bounded backoff so a broken filesystem cannot cause a busy loop.
        m_recoveryTimer.start(RecoveryWriteDelayMs);
    }
}

void AppController::writeRecoverySnapshot()
{
    if (m_recoveryPending || !m_documentState.dirty()) {
        return;
    }
    const ProjectRecoverySnapshot snapshot{
        m_documentState.projectPath(),
        m_documentId,
        m_documentState.revision(),
        m_documentState.lastSavedRevision(),
        QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs),
        currentProjectObject(),
        true,
    };
    QString error;
    if (!m_recoveryStore.write(snapshot, &error)) {
        AppLog::error(QStringLiteral("Recovery snapshot write failed: %1").arg(error));
        const bool changed = !m_recoveryDegraded || m_recoveryError != error;
        m_recoveryDegraded = true;
        m_recoveryError = error;
        if (changed) emit recoveryChanged();
        m_recoveryTimer.start(RecoveryRetryDelayMs);
        return;
    }
    if (m_recoveryDegraded) {
        m_recoveryDegraded = false;
        m_recoveryError.clear();
        emit recoveryChanged();
    }
    AppLog::info(QStringLiteral("Recovery snapshot written"));
}

bool AppController::clearRecovery(const QString &reason)
{
    const bool existed = m_recoveryStore.exists();
    QString error;
    if (!m_recoveryStore.clear(&error)) {
        AppLog::error(QStringLiteral("Recovery clear failed: %1").arg(error));
        return false;
    }
    if (existed) {
        AppLog::info(QStringLiteral("Recovery cleared after %1").arg(reason));
    }
    return true;
}

void AppController::clearDiscardTombstoneAfterRecoveryCleanup()
{
    QString error;
    if (!m_recoveryStore.clearDiscardTombstone(&error)) {
        AppLog::error(QStringLiteral("Recovery discard tombstone cleanup failed: %1").arg(error));
    }
}

bool AppController::discardRecovery(const ProjectRecoverySnapshot &snapshot, const QString &reason)
{
    if (!m_recoveryStore.exists()) return true;
    if (!snapshot.hasLogicalMetadata) return clearRecovery(reason);

    QString tombstoneError;
    const ProjectRecoveryDiscardTombstone tombstone{
        snapshot.documentId, snapshot.revision,
    };
    if (m_recoveryStore.writeDiscardTombstone(tombstone, &tombstoneError)) {
        AppLog::info(QStringLiteral("Recovery discard intent persisted through revision %1")
                         .arg(snapshot.revision));
        if (!clearRecovery(reason)) {
            AppLog::warn(QStringLiteral("Recovery cleanup deferred after durable discard intent"));
            return true;
        }
        clearDiscardTombstoneAfterRecoveryCleanup();
        return true;
    }

    AppLog::error(QStringLiteral("Recovery discard intent persistence failed: %1").arg(tombstoneError));
    return clearRecovery(reason);
}

void AppController::retireLegacyDocumentSettings()
{
    m_settings.remove(QStringLiteral("editor/widgets"));
    m_settings.remove(QStringLiteral("sync/offset"));
    m_settings.remove(QStringLiteral("sync/timeScale"));
    m_settings.remove(QStringLiteral("sources/video"));
    m_settings.remove(QStringLiteral("sources/vbo"));
    m_settings.remove(QStringLiteral("analysis/channels"));
    m_settings.remove(QStringLiteral("analysis/visible"));
    m_settings.sync();
}

void AppController::restoreStartupState()
{
    ProjectRecoverySnapshot snapshot;
    QString error;
    if (!m_recoveryStore.exists()
        && QFileInfo(m_recoveryStore.discardTombstonePath()).exists()) {
        clearDiscardTombstoneAfterRecoveryCleanup();
    }
    if (m_recoveryStore.exists() && m_recoveryStore.load(&snapshot, &error)) {
        bool discardedByTombstone = false;
        if (snapshot.hasLogicalMetadata) {
            ProjectRecoveryDiscardTombstone tombstone;
            QString tombstoneError;
            if (m_recoveryStore.loadDiscardTombstone(&tombstone, &tombstoneError)) {
                if (tombstone.documentId == snapshot.documentId
                    && snapshot.revision <= tombstone.discardedThroughRevision) {
                    AppLog::info(QStringLiteral("Recovery snapshot suppressed by durable discard intent"));
                    discardedByTombstone = true;
                    if (clearRecovery(QStringLiteral("startup discarded recovery"))) {
                        clearDiscardTombstoneAfterRecoveryCleanup();
                    } else {
                        AppLog::warn(QStringLiteral("Discarded recovery cleanup remains pending"));
                    }
                }
            } else if (QFileInfo(m_recoveryStore.discardTombstonePath()).exists()) {
                AppLog::error(QStringLiteral("Recovery discard tombstone ignored: %1").arg(tombstoneError));
            }
        }
        if (!discardedByTombstone) {
            const RecoveryValidity validity = recoveryValidity(
                snapshot, m_settings.value(QStringLiteral("project/path")).toString());
            if (validity == RecoveryValidity::Stale) {
                AppLog::info(QStringLiteral("Stale recovery snapshot ignored"));
                clearRecovery(QStringLiteral("stale startup recovery"));
            } else if (validity == RecoveryValidity::Invalid) {
                AppLog::error(QStringLiteral("Recovery snapshot ignored: document identity does not match authority"));
            } else {
                m_pendingRecovery = snapshot;
                m_recoveryPending = true;
                m_documentState.reset();
                AppLog::info(QStringLiteral("Recovery detected"));
                setStatus(QStringLiteral("Unsaved changes are available for recovery."));
                return;
            }
        }
    }
    if (m_recoveryStore.exists() && !error.isEmpty()) {
        AppLog::error(QStringLiteral("Recovery snapshot ignored: %1").arg(error));
    }
    const QString projectPath = m_settings.value(QStringLiteral("project/path")).toString();
    if (projectPath.isEmpty()) {
        m_documentState.reset();
        return;
    }
    if (!QFileInfo(projectPath).isFile()) {
        m_settings.remove(QStringLiteral("project/path"));
        m_settings.sync();
        m_documentState.reset();
        setStatus(QStringLiteral("The previous project could not be found; a new project was started."));
        return;
    }
    m_documentState.reset();
    performOpenProject(QUrl::fromLocalFile(projectPath));
}

void AppController::beginDestructiveAction(
    const ProjectDocumentState::DestructiveAction action, const QUrl &openUrl)
{
    if (action == ProjectDocumentState::DestructiveAction::OpenProject) {
        if (!openUrl.isLocalFile() || openUrl.toLocalFile().isEmpty()) {
            setStatus("Project error: choose a local .fetproject file.");
            return;
        }
        m_pendingOpenProject = openUrl;
    }
    const auto result = m_documentState.request(action);
    emit destructiveActionChanged();
    if (result == ProjectDocumentState::RequestResult::ContinueImmediately) {
        performPendingDestructiveAction();
    } else {
        AppLog::info(QStringLiteral("Dirty project decision requested for: %1")
                         .arg(ProjectDocumentState::actionName(action)));
    }
}

void AppController::performPendingDestructiveAction()
{
    const ProjectDocumentState::DestructiveAction action = m_documentState.takePendingAction();
    const QUrl openUrl = m_pendingOpenProject;
    m_pendingOpenProject = QUrl();
    emit destructiveActionChanged();
    m_recoveryTimer.stop();
    const ProjectRecoverySnapshot snapshot{
        m_documentState.projectPath(), m_documentId, m_documentState.revision(),
        m_documentState.lastSavedRevision(), {}, {}, true,
    };
    if (action != ProjectDocumentState::DestructiveAction::None
        && !discardRecovery(snapshot, QStringLiteral("discarded document state"))) {
        setStatus(QStringLiteral("Could not discard recovery data; action cancelled."));
        return;
    }
    switch (action) {
    case ProjectDocumentState::DestructiveAction::NewProject:
        performClearProject();
        break;
    case ProjectDocumentState::DestructiveAction::OpenProject:
        performOpenProject(openUrl);
        break;
    case ProjectDocumentState::DestructiveAction::Quit:
        AppLog::info(QStringLiteral("Quit approved"));
        emit quitApproved();
        break;
    case ProjectDocumentState::DestructiveAction::None:
        break;
    }
}

void AppController::reconcileAnalysisChannels()
{
    if (!m_session) {
        return;
    }
    QStringList channels;
    for (const QString &channel : std::as_const(m_analysisChannels)) {
        if (m_session->channels.contains(channel) && !channels.contains(channel)) {
            channels.append(channel);
        }
    }
    for (const QString &alias : {QStringLiteral("speed"), QStringLiteral("rpm"),
                                 QStringLiteral("throttle"), QStringLiteral("brake")}) {
        const QString resolved = m_session->aliases.value(alias);
        if (!resolved.isEmpty() && !channels.contains(resolved)) {
            channels.append(resolved);
        }
        if (channels.size() >= 3) {
            break;
        }
    }
    for (const QString &channel : m_session->channelNames()) {
        if (channels.size() >= 3) {
            break;
        }
        if (!channels.contains(channel)) {
            channels.append(channel);
        }
    }
    setAnalysisChannels(channels);
}

QString AppController::syncCandidateLevelName(const double confidence)
{
    switch (syncConfidenceLevel(confidence)) {
    case SyncConfidenceLevel::High:
        return QStringLiteral("high");
    case SyncConfidenceLevel::Medium:
        return QStringLiteral("medium");
    case SyncConfidenceLevel::Low:
        return QStringLiteral("low");
    }
    return QStringLiteral("low");
}

} // namespace FlappedEar
