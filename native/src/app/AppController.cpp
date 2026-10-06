#include "app/AppController.h"
#include "project/VideoChapters.h"
#include "app/PreviewPlayback.h"
#include "export/ExportFormat.h"
#include "export/ExportEngine.h"
#include "export/ChapterSource.h"
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
#include "telemetry/ChannelSeries.h"
#include "telemetry/TelemetrySyncEngine.h"
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

} // namespace

AppController::AppController(QObject *parent, QString recoveryPath,
                             ProjectRecoveryStore::Operations recoveryOperations)
    : QObject(parent)
    , m_settings()
    , m_previewRenderContext(this)
    , m_document(*this, std::move(recoveryPath), std::move(recoveryOperations))
{
    m_exportDiagnosticNotifier.setSingleShot(true);
    m_exportDiagnosticNotifier.setInterval(200);
    connect(&m_exportDiagnosticNotifier, &QTimer::timeout, this, &AppController::exportDiagnosticLogChanged);
    m_sync = {};
    m_previewRenderContext.setSyncTransform(m_sync);
    // KAN-104: a reviewed chapter group.
    connect(&m_videoChapters, &VideoChapterReview::groupChosen, this, [this](const QList<QUrl> &files, bool) {
        // KAN-105: several chapters play as one timeline.
        m_videoChapterNotice.clear();
        if (files.size() > 1) loadVideoChapters(files);
        else loadVideo(files.first());
    });
    m_widgetModel.resetDefaults();
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
        if (result.generation != m_document.sourceGeneration()
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
        if (result.generation != m_document.sourceGeneration()) {
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
        if (result.generation != m_document.sourceGeneration()) {
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
    initializeDocument();
    m_document.startup();
}

AppController::~AppController()
{
    m_document.cancelImport();
    cancelSourceJobs();
    QElapsedTimer sourceShutdown;
    sourceShutdown.start();
    while (sourceShutdown.elapsed() < 2'000
           && (m_videoProbeWatcher.isRunning() || m_vboLoadWatcher.isRunning()
               || m_document.projectLoadRunning() || m_syncWatcher.isRunning() || m_document.importRunning())) {
        QThread::msleep(10);
    }
    if (m_videoProbeWatcher.isRunning() || m_vboLoadWatcher.isRunning()
        || m_document.projectLoadRunning() || m_syncWatcher.isRunning() || m_document.importRunning()) {
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
bool AppController::documentBusy() const { return exporting(); }
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
        isUnsupportedColorManagedClass(m_exportSourceInfo.sourceColorClass);
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
std::optional<qint64> AppController::timelineLastFrame() const
{
    // KAN-105: the whole chapter timeline, framed at the first chapter's rate.
    const MediaRational rate = m_exportSourceInfo.averageFrameRate.isValid()
        ? m_exportSourceInfo.averageFrameRate : m_exportSourceInfo.frameRate;
    if (!videoChaptered() || !rate.isValid()) return std::nullopt;
    const auto frames = static_cast<qint64>(std::floor(m_videoTimeline.durationSeconds() * rate.numerator / rate.denominator + 1e-6));
    return frames >= 1 ? std::optional<qint64>(frames - 1) : std::nullopt;
}
qint64 AppController::previewEndPositionMilliseconds() const
{
    const MediaRational rate = m_exportSourceInfo.averageFrameRate.isValid()
        ? m_exportSourceInfo.averageFrameRate : m_exportSourceInfo.frameRate;
    if (const auto last = timelineLastFrame())
        return PreviewPlayback::framePositionMilliseconds(*last, rate).value_or(0);
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
    if (const auto last = timelineLastFrame())
        return PreviewPlayback::clampPositionMilliseconds(requestedMilliseconds, *last, rate).value_or(0);
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
    auto range = ExportEngine::fullVideoFrameRange(m_exportSourceInfo, rate);
    if (const auto last = timelineLastFrame()) range = ExportFrameRange{0, *last};
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
    if (const auto last = timelineLastFrame()) return ExportEngine::formatSmpteTimecode(*last, rate);
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
int AppController::windowX() const { return m_settings.value("window/x", -1).toInt(); }
int AppController::windowY() const { return m_settings.value("window/y", -1).toInt(); }
int AppController::windowWidth() const { return m_settings.value("window/width", 1440).toInt(); }
int AppController::windowHeight() const { return m_settings.value("window/height", 900).toInt(); }
QString AppController::videoLoadState() const { return m_videoLoadState; }
QString AppController::vboLoadState() const { return m_vboLoadState; }
QString AppController::sourceMismatchType() const { return m_sourceMismatchType; }
QString AppController::formatElapsedTime(const double seconds)
{
    return FlappedEar::formatElapsedTime(seconds);
}

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
    const quint64 generation = beginSourceGeneration(true);
    if (restartOther && !request.path.isEmpty()) {
        if (replacingVideo) startVboLoad(request.path, generation, request.markDocumentDirty,
                                        request.expectedFingerprint, request.relink);
        else startVideoProbe(request.path, generation, request.markDocumentDirty,
                             request.expectedFingerprint, request.relink, request.chapters);
    }
    return generation;
}

quint64 AppController::beginSourceGeneration(const bool /*preserveOuting: no analysis to keep since KAN-166*/)
{
    const quint64 generation = m_document.nextSourceGeneration();
    if (!m_sourceMismatchType.isEmpty()) {
        m_sourceMismatchType.clear();
        m_pendingMismatchVideo = {};
        m_pendingMismatchVbo = {};
        emit sourceMismatchChanged();
    }
    const bool replacing = m_videoProbeWatcher.isRunning() || m_vboLoadWatcher.isRunning()
        || m_document.projectLoadRunning() || m_syncWatcher.isRunning();
    cancelSourceJobs();
    if (replacing) AppLog::info(QStringLiteral("Previous source load cancelled after replacement"));
    // A load whose job has ended but whose result is not yet delivered is also
    // stopped: the new generation rejects that result.
    m_videoLoadInterrupted = m_videoLoadState == QStringLiteral("loading");
    m_vboLoadInterrupted = m_vboLoadState == QStringLiteral("loading");
    if (m_videoLoadInterrupted) {
        m_videoLoadState = QStringLiteral("idle");
    }
    if (m_vboLoadInterrupted) {
        m_vboLoadState = QStringLiteral("idle");
    }
    emit sourceLoadStateChanged();
    m_document.setProjectLoadState(false);
    return generation;
}

void AppController::resumeInterruptedSources()
{
    const quint64 generation = m_document.sourceGeneration();
    const bool video = m_videoLoadInterrupted && !m_videoLoadRequest.path.isEmpty();
    const bool vbo = m_vboLoadInterrupted && !m_vboLoadRequest.path.isEmpty();
    m_videoLoadInterrupted = false;
    m_vboLoadInterrupted = false;
    if (video) {
        const auto request = m_videoLoadRequest;
        startVideoProbe(request.path, generation, request.markDocumentDirty,
                        request.expectedFingerprint, request.relink, request.chapters);
    }
    if (vbo) {
        const auto request = m_vboLoadRequest;
        startVboLoad(request.path, generation, request.markDocumentDirty,
                     request.expectedFingerprint, request.relink);
    }
    if (video || vbo) AppLog::info(QStringLiteral("Interrupted source loads resumed after a failed project open"));
}

void AppController::cancelSourceJobs()
{
    m_document.cancelProjectLoad();
    for (const auto &cancellation : {m_videoProbeCancellation, m_vboLoadCancellation, m_syncCancellation}) {
        if (cancellation) {
            cancellation->store(true);
        }
    }
}

void AppController::startVideoProbe(
    const QString &path, const quint64 generation, const bool markDocumentDirty,
    QJsonObject expectedFingerprint, const bool relink, QVector<VideoChapterInput> chapters)
{
    AppLog::info(QStringLiteral("Video load/probe started: %1").arg(path));
    m_videoProbeCancellation = std::make_shared<std::atomic_bool>(false);
    const std::shared_ptr<std::atomic_bool> cancellation = m_videoProbeCancellation;
    m_videoLoadRequest = {path, markDocumentDirty, expectedFingerprint, relink, chapters};
    m_pendingVideoPath = path;
    m_videoLoadMarksDocumentDirty = markDocumentDirty;
    m_videoLoadState = QStringLiteral("loading");
    emit sourceLoadStateChanged();
    m_videoProbeWatcher.setFuture(QtConcurrent::run(
        [path, generation, cancellation, expectedFingerprint = std::move(expectedFingerprint), relink,
         chapters = std::move(chapters)] {
        VideoProbeResult result;
        result.path = path;
        result.generation = generation;
        result.expectedFingerprint = expectedFingerprint;
        result.relink = relink;
        try {
            result.mediaInfo = MediaProbe::probe(
                path, {}, false, -1, {}, [cancellation] { return cancellation->load(); });
            result.fingerprint = videoSourceFingerprint(path, result.mediaInfo);
            // KAN-105: every further chapter, probed and checked against its
            // saved fingerprint. One that is missing, unreadable or no longer
            // the same file is a gap of its saved duration.
            if (!chapters.isEmpty()) {
                const auto videoDuration = [](const MediaInfo &info) {
                    return info.videoDuration > 0.0 ? info.videoDuration : info.duration;
                };
                result.chapters.append({ProjectSourceReferenceCodec::forLoadedSource(path, result.fingerprint), path,
                    videoDuration(result.mediaInfo), true, {}, result.mediaInfo});
                for (const auto &input : chapters) {
                    if (cancellation->load()) break;
                    VideoChapterState chapter{input.reference, input.path, input.durationSeconds, false, {}, {}};
                    if (input.path.isEmpty()) {
                        chapter.problem = QStringLiteral("missing");
                    } else {
                        try {
                            const auto info = MediaProbe::probeSummary(input.path, {}, 30'000, {},
                                [cancellation] { return cancellation->load(); });
                            const auto fingerprint = videoSourceFingerprint(input.path, info);
                            if (ProjectSourceReferenceCodec::compareFingerprints(input.reference.fingerprint, fingerprint)
                                == SourceFingerprintMatch::Mismatch) {
                                chapter.problem = QStringLiteral("mismatch");
                            } else {
                                chapter.reference = ProjectSourceReferenceCodec::forLoadedSource(input.path, fingerprint);
                                chapter.durationSeconds = videoDuration(info);
                                chapter.available = true;
                                chapter.mediaInfo = info;
                            }
                        } catch (const OperationCancelled &) {
                            throw;
                        } catch (const std::exception &error) {
                            chapter.problem = QString::fromUtf8(error.what());
                        }
                    }
                    if (!chapter.available) chapter.path.clear();
                    result.chapters.append(chapter);
                }
            }
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

// The editor's lap navigation and export bind saved lap exclusions to the
// active run's primary recording, read from the document (KAN-166).
QJsonObject AppController::activeLapBinding() const
{
    auto binding = EventProjectCodec::primaryTelemetryBinding(currentProjectObject(), activeRunId());
    if (binding.isEmpty()) return {};
    binding.remove("reference");
    binding.insert("sourceRevision", QString::fromLatin1(m_loadedSourceRevision));
    return binding;
}

void AppController::applyActiveLapExclusions()
{
    const auto exclusions = currentProjectObject().value("event").toObject().value("lapExclusions").toArray();
    applyLapExclusions(m_lapSession, activeLapBinding(), exclusions);
    m_previewRenderContext.setLapSession(m_lapSession);
    emit lapNavigationChanged();
    emit liveValuesChanged();
}

void AppController::startVboLoad(
    const QString &path, const quint64 generation, const bool markDocumentDirty,
    QJsonObject expectedFingerprint, const bool relink)
{
    AppLog::info(QStringLiteral("Telemetry load started: %1").arg(path));
    m_vboLoadCancellation = std::make_shared<std::atomic_bool>(false);
    const std::shared_ptr<std::atomic_bool> cancellation = m_vboLoadCancellation;
    m_vboLoadRequest = {path, markDocumentDirty, expectedFingerprint, relink, {}};
    m_pendingVboPath = path;
    m_vboLoadMarksDocumentDirty = markDocumentDirty;
    m_vboLoadState = QStringLiteral("loading");
    emit sourceLoadStateChanged();
    QByteArray expectedRevision;
    if (!expectedFingerprint.isEmpty()) {
        const auto binding = EventProjectCodec::primaryTelemetryBinding(currentProjectObject(), activeRunId());
        if (binding.value("reference").toObject().value("fingerprint").toObject() == expectedFingerprint)
            expectedRevision = binding.value("expectedRevision").toString().toLatin1();
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
    // KAN-105: chapters play as one timeline; a gap keeps its saved duration.
    m_videoChapterStates = result.chapters;
    m_videoChapterIndex = 0;
    QVector<TimelineChapter> timelineChapters;
    for (const auto &chapter : m_videoChapterStates)
        timelineChapters.append({chapter.path, chapter.durationSeconds, chapter.available});
    m_videoTimeline = MediaTimeline::fromChapters(timelineChapters);
    if (!m_videoChapterStates.isEmpty() && !m_videoTimeline.isValid()) {
        // A chapter without a known duration cannot hold its place in time.
        AppLog::warn(QStringLiteral("Video chapters without usable durations; opening the first chapter alone"));
        m_videoChapterStates.clear();
        m_videoChapterNotice = tr("Some chapters have no known duration, so only the first chapter is open.");
    }
    if (m_videoChapterStates.isEmpty()) m_videoTimeline = {};
    int gaps = 0;
    for (const auto &chapter : m_videoChapterStates) gaps += chapter.available ? 0 : 1;
    if (gaps > 0)
        m_videoChapterNotice = tr("%n chapter(s) are missing or changed and play as a gap. Choose the recording's chapters again to fill it.",
                                  nullptr, gaps);
    // KAN-106: export reads every chapter as one source, so its frame domain
    // and timecodes cover the whole timeline. A gap cannot be exported.
    m_exportChapterPaths.clear();
    m_exportChapterProblem.clear();
    if (videoChaptered()) {
        if (gaps > 0) {
            m_exportChapterProblem = tr("Some chapters of this recording are missing or changed. "
                                        "Choose the recording's chapters again before exporting.");
        } else {
            QVector<MediaInfo> chapters;
            QStringList paths;
            for (const auto &chapter : m_videoChapterStates) {
                chapters.append(chapter.mediaInfo);
                paths.append(chapter.path);
            }
            const auto combination = ChapterSource::combine(chapters);
            if (combination.info) {
                m_exportSourceInfo = *combination.info;
                m_exportChapterPaths = paths;
            } else {
                m_exportChapterProblem = combination.error;
            }
        }
        if (!m_exportChapterProblem.isEmpty())
            AppLog::warn(QStringLiteral("Chaptered video cannot be exported: %1").arg(m_exportChapterProblem));
    }
    emit videoChaptersChanged();
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
    setStatus(QStringLiteral("Video opened: %1").arg(QFileInfo(result.path).fileName())
        + (m_videoChapterNotice.isEmpty() ? QString() : QStringLiteral(". ") + m_videoChapterNotice));
    m_videoChapterNotice.clear();
}

void AppController::commitVboLoad(const VboLoadResult &result, const bool markDocumentDirty)
{
    const DocumentController::DirtySuppression suppressDirty(m_document, !markDocumentDirty);
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
    if (markDocumentDirty && EventProjectCodec::isEvent(m_document.storedProject())) {
        // Replacement clears asserted layout/direction, then records the gates
        // actually verified in the new source. This enables fresh inference;
        // the old source's inference goes, as Overlays no longer derives one.
        auto project = currentProjectObject(); auto event = project.value("event").toObject();
        auto runs = event.value("runs").toArray();
        for (qsizetype i = 0; i < runs.size(); ++i) {
            auto run = runs[i].toObject();
            if (run.value("id").toString() != activeRunId()) continue;
            auto config = EventProjectCodec::trackConfiguration(run);
            const auto gates = timingGateRevision(result.session);
            config.insert("gateRevision", gates.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(gates));
            run.insert("trackConfiguration", config); run.remove("trackInference"); runs[i] = run;
        }
        event.insert("runs", runs); project.insert("event", event); m_document.replaceStoredProject(project);
    }
    applyActiveLapExclusions();
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

QUrl AppController::videoChapterSource() const
{
    if (!videoChaptered()) return m_videoSource;
    const auto &chapter = m_videoTimeline.chapter(m_videoChapterIndex);
    return chapter.available ? QUrl::fromLocalFile(chapter.path) : QUrl();
}

qint64 AppController::videoChapterStartMilliseconds() const
{
    return videoChaptered() ? qRound64(m_videoTimeline.chapterStartSeconds(m_videoChapterIndex) * 1000.0) : 0;
}

QVariantList AppController::videoChapterList() const
{
    QVariantList list;
    if (!videoChaptered()) return list;
    for (int index = 0; index < m_videoTimeline.chapterCount(); ++index) {
        const auto &state = m_videoChapterStates.value(index);
        list.append(QVariantMap{{"index", index},
            {"startMilliseconds", qRound64(m_videoTimeline.chapterStartSeconds(index) * 1000.0)},
            {"durationMilliseconds", qRound64(m_videoTimeline.chapter(index).durationSeconds * 1000.0)},
            {"available", m_videoTimeline.chapter(index).available},
            {"url", m_videoTimeline.chapter(index).available ? QUrl::fromLocalFile(m_videoTimeline.chapter(index).path) : QUrl()},
            {"name", QFileInfo(state.reference.displayPath()).fileName()}, {"problem", state.problem}});
    }
    return list;
}

QVariantMap AppController::locateVideoTimeline(const qint64 timelineMilliseconds) const
{
    if (!videoChaptered()) return {{"chapter", 0}, {"localMilliseconds", timelineMilliseconds}, {"gap", false}};
    const auto position = m_videoTimeline.locate(std::clamp(timelineMilliseconds / 1000.0, 0.0, m_videoTimeline.durationSeconds()));
    if (!position) return {};
    return {{"chapter", position->chapter}, {"localMilliseconds", qRound64(position->localSeconds * 1000.0)}, {"gap", position->gap}};
}

bool AppController::setVideoChapter(const int index)
{
    if (!videoChaptered() || index < 0 || index >= m_videoTimeline.chapterCount()) return false;
    if (index == m_videoChapterIndex) return true;
    m_videoChapterIndex = index;
    emit videoChaptersChanged();
    return true;
}

QVector<AppController::VideoChapterInput> AppController::videoChapterInputs() const
{
    QVector<VideoChapterInput> inputs;
    for (qsizetype index = 1; index < m_videoChapterStates.size(); ++index) {
        const auto &chapter = m_videoChapterStates[index];
        inputs.append({chapter.reference, ProjectSourceReferenceCodec::resolve(chapter.reference, m_document.documentPath()),
            chapter.durationSeconds});
    }
    return inputs;
}

void AppController::loadVideoChapters(const QList<QUrl> &files)
{
    if (files.size() < 2 || files.size() > MediaTimeline::maximumChapters) return;
    QVector<VideoChapterInput> chapters;
    for (const auto &url : files) {
        const QFileInfo info(url.toLocalFile());
        const auto extension = info.suffix().toLower();
        if (!info.isFile() || (extension != "mp4" && extension != "mov")) {
            setStatus("Choose existing MP4 or MOV chapter files.");
            return;
        }
        if (url != files.first()) chapters.append({{}, normalizedSourcePath(info.absoluteFilePath()), 0.0});
    }
    const auto first = normalizedSourcePath(QFileInfo(files.first().toLocalFile()).absoluteFilePath());
    const quint64 generation = beginSourceReplacement(true);
    m_pendingVideoPath = first;
    startVideoProbe(first, generation, true, {}, false, chapters);
    setStatus(QStringLiteral("Loading %1 video chapters").arg(files.size()));
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
    // Relinking moves the first chapter; the other chapters keep their references.
    startVideoProbe(path, generation, true, m_videoReference.fingerprint, true, videoChapterInputs());
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
        && m_pendingMismatchVideo.generation == m_document.sourceGeneration()) {
        commitVideoProbe(m_pendingMismatchVideo, true);
    } else if (type == QStringLiteral("telemetry") && m_pendingMismatchVbo.success
               && m_pendingMismatchVbo.generation == m_document.sourceGeneration()) {
        commitVboLoad(m_pendingMismatchVbo, true);
    }
    m_pendingMismatchVideo = {};
    m_pendingMismatchVbo = {};
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
    return channelSeries(*m_session, channelName, *telemetryStart, *telemetryEnd, maximumPoints);
}

int AppController::lapNumberAtPlayback() const
{
    const auto telemetry = FlappedEar::videoToTelemetryTime(m_playbackTime, m_sync);
    if (!telemetry) return 0;
    for (const auto &lap : m_lapSession.timedLaps)
        if (*telemetry >= lap.startTelemetryTime && *telemetry < lap.endTelemetryTime) return lap.number;
    return 0;
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

void AppController::startEditorSources(const ProjectLoadResult &result)
{
    m_pendingVideoPath = result.resolvedVideoPath;
    m_pendingVboPath = result.resolvedVboPath;
    if (!result.resolvedVideoPath.isEmpty()) {
        // KAN-105: the saved chapters after the first, each resolved on its own.
        const auto editor = EventProjectCodec::editorProjection(result.project);
        QVector<VideoChapterInput> chapters;
        const auto saved = VideoChaptersCodec::read(editor.value("sources").toObject().value("video").toObject());
        for (qsizetype index = 1; index < saved.size(); ++index)
            chapters.append({saved[index].reference, ProjectSourceReferenceCodec::resolve(saved[index].reference, result.projectPath),
                saved[index].durationSeconds});
        startVideoProbe(result.resolvedVideoPath, result.generation, false,
                        result.videoReference.fingerprint, false, chapters);
    }
    if (!result.resolvedVboPath.isEmpty()) {
        startVboLoad(result.resolvedVboPath, result.generation, false,
                     result.vboReference.fingerprint, false);
    }
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
    const quint64 generation = m_document.sourceGeneration();
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
    if (videoChaptered() && m_exportChapterPaths.isEmpty()) {
        // KAN-106: never export the first chapter alone as if it were the
        // whole recording.
        m_exportError = m_exportChapterProblem.isEmpty()
            ? QStringLiteral("The chapters of this recording cannot be exported together.") : m_exportChapterProblem;
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
    if (isUnsupportedColorManagedClass(m_exportSourceInfo.sourceColorClass)) {
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
    protectedPaths += m_exportChapterPaths; // every chapter is a source (KAN-106)
    if (EventProjectCodec::isEvent(m_document.storedProject())) {
        protectedPaths.append(m_document.documentPath());
        protectedPaths.append(EventProjectCodec::referencedPaths(currentProjectObject(), m_document.documentPath()));
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
    QJsonArray chapterDurationTicks;
    if (!m_exportChapterPaths.isEmpty())
        for (const auto &chapter : m_videoChapterStates) chapterDurationTicks.append(chapter.mediaInfo.videoDurationTicks);
    const QJsonObject config = {
        {"inputPath", inputPath},
        {"chapterPaths", QJsonArray::fromStringList(m_exportChapterPaths)},
        {"chapterDurationTicks", chapterDurationTicks},
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
    m_exportDiagnosticNotifier.stop();
    emit exportDiagnosticLogChanged();
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
        "FlappedEar Overlays Export Log\n\n"
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
    if (!m_exportDiagnosticNotifier.isActive()) m_exportDiagnosticNotifier.start();
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
    // The limit is per message (line), not for what one read delivered: several
    // normal messages can arrive together (KAN-148).
    const auto rejectOversizedMessage = [this] {
        m_exportStdout.clear();
        m_exportError = QStringLiteral("Export worker emitted a message longer than %1 bytes.")
                            .arg(ProcessOutputLimits::workerMessageBytes);
        AppLog::error(m_exportError);
        if (m_exportSupervisor) static_cast<void>(m_exportSupervisor->stopAndWait());
    };
    qsizetype newline = -1;
    while ((newline = m_exportStdout.indexOf('\n')) >= 0) {
        if (newline > ProcessOutputLimits::workerMessageBytes) { rejectOversizedMessage(); return; }
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
    // A partial message already beyond the limit can never become valid.
    if (m_exportStdout.size() > ProcessOutputLimits::workerMessageBytes) rejectOversizedMessage();
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
    markPersistentChange();
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
    markPersistentChange();
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
