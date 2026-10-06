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
    connect(&m_export, &ExportController::statusMessage, this, &AppController::setStatus);
    connect(&m_export, &ExportController::quitRequested, this, &AppController::requestQuit);
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
    connect(&m_videoHashWatcher, &QFutureWatcher<VideoHashResult>::finished, this, [this] {
        const VideoHashResult result = m_videoHashWatcher.result();
        if (result.cancelled || result.generation != m_document.sourceGeneration()
            || m_videoLoadState != QStringLiteral("ready") || m_videoSource.toLocalFile() != result.path)
            return;
        // KAN-208: a saved identity that the file no longer has is a mismatch;
        // otherwise the computed one is recorded and saved with the project.
        const auto expectedAt = [this](const qsizetype index) {
            return m_videoChapterStates.isEmpty() || index == 0 ? m_videoReference.contentSha256
                                                                 : m_videoChapterStates[index].reference.contentSha256;
        };
        bool mismatch = false;
        for (qsizetype index = 0; index < result.digests.size(); ++index) {
            const auto &digest = result.digests[index];
            if (!digest.isEmpty() && !expectedAt(index).isEmpty() && digest != expectedAt(index)) mismatch = true;
        }
        if (mismatch) {
            AppLog::warn(QStringLiteral("Video content differs from the project's saved identity: %1").arg(result.path));
            m_videoLoadState = QStringLiteral("mismatch");
            if (m_hashedVideoProbe.relink) {
                m_pendingMismatchVideo = m_hashedVideoProbe;
                m_pendingMismatchVbo = {};
                m_sourceMismatchType = QStringLiteral("video");
                emit sourceMismatchChanged();
            }
            emit sourceLoadStateChanged();
            emit exportChanged();
            setStatus(QStringLiteral("Video source does not match the project: its content changed."));
            return;
        }
        for (qsizetype index = 0; index < result.digests.size(); ++index) {
            if (result.digests[index].isEmpty()) continue;
            if (m_videoChapterStates.isEmpty() || index == 0) m_videoReference.contentSha256 = result.digests[index];
            if (index < m_videoChapterStates.size()) m_videoChapterStates[index].reference.contentSha256 = result.digests[index];
        }
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
           && (m_videoProbeWatcher.isRunning() || m_vboLoadWatcher.isRunning() || m_videoHashWatcher.isRunning()
               || m_document.projectLoadRunning() || m_syncWatcher.isRunning() || m_document.importRunning())) {
        QThread::msleep(10);
    }
    if (m_videoProbeWatcher.isRunning() || m_vboLoadWatcher.isRunning()
        || m_document.projectLoadRunning() || m_syncWatcher.isRunning() || m_document.importRunning()) {
        AppLog::warn(QStringLiteral("Source worker shutdown exceeded the bounded wait"));
    }
    // m_export stops a running export when it is destroyed, after the sources.
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
bool AppController::documentBusy() const { return m_export.exporting(); }
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
                                        request.expectedFingerprint, request.relink, request.expectedContentSha256);
        else startVideoProbe(request.path, generation, request.markDocumentDirty,
                             request.expectedFingerprint, request.relink, request.chapters, request.expectedContentSha256);
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
                        request.expectedFingerprint, request.relink, request.chapters, request.expectedContentSha256);
    }
    if (vbo) {
        const auto request = m_vboLoadRequest;
        startVboLoad(request.path, generation, request.markDocumentDirty,
                     request.expectedFingerprint, request.relink, request.expectedContentSha256);
    }
    if (video || vbo) AppLog::info(QStringLiteral("Interrupted source loads resumed after a failed project open"));
}

void AppController::cancelSourceJobs()
{
    m_document.cancelProjectLoad();
    for (const auto &cancellation : {m_videoProbeCancellation, m_vboLoadCancellation, m_syncCancellation,
                                     m_videoHashCancellation}) {
        if (cancellation) {
            cancellation->store(true);
        }
    }
}

void AppController::startVideoProbe(
    const QString &path, const quint64 generation, const bool markDocumentDirty,
    QJsonObject expectedFingerprint, const bool relink, QVector<VideoChapterInput> chapters,
    const QString &expectedContentSha256)
{
    AppLog::info(QStringLiteral("Video load/probe started: %1").arg(path));
    m_videoProbeCancellation = std::make_shared<std::atomic_bool>(false);
    const std::shared_ptr<std::atomic_bool> cancellation = m_videoProbeCancellation;
    m_videoLoadRequest = {path, markDocumentDirty, expectedFingerprint, relink, chapters, expectedContentSha256};
    m_pendingVideoPath = path;
    m_videoLoadMarksDocumentDirty = markDocumentDirty;
    m_videoLoadState = QStringLiteral("loading");
    emit sourceLoadStateChanged();
    m_videoProbeWatcher.setFuture(QtConcurrent::run(
        [path, generation, cancellation, expectedFingerprint = std::move(expectedFingerprint), relink,
         chapters = std::move(chapters), expectedContentSha256] {
        VideoProbeResult result;
        result.path = path;
        result.generation = generation;
        result.expectedFingerprint = expectedFingerprint;
        result.relink = relink;
        result.expectedContentSha256 = expectedContentSha256;
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
                result.chapters.append({ProjectSourceReferenceCodec::forLoadedSource(path, result.fingerprint, expectedContentSha256), path,
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
                                chapter.reference = ProjectSourceReferenceCodec::forLoadedSource(
                                    input.path, fingerprint, input.reference.contentSha256);
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
    QJsonObject expectedFingerprint, const bool relink, const QString &expectedContentSha256)
{
    AppLog::info(QStringLiteral("Telemetry load started: %1").arg(path));
    m_vboLoadCancellation = std::make_shared<std::atomic_bool>(false);
    const std::shared_ptr<std::atomic_bool> cancellation = m_vboLoadCancellation;
    m_vboLoadRequest = {path, markDocumentDirty, expectedFingerprint, relink, {}, expectedContentSha256};
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
    if (expectedRevision.isEmpty()) expectedRevision = expectedContentSha256.toLatin1();
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
    // KAN-208: the saved content identity stays until the background hash
    // confirms or contradicts it (startVideoHash).
    m_videoReference = ProjectSourceReferenceCodec::forLoadedSource(
        result.path, result.fingerprint, result.expectedContentSha256);
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
    startVideoHash(result);
}

void AppController::startVideoHash(const VideoProbeResult &committed)
{
    if (m_videoHashCancellation) m_videoHashCancellation->store(true);
    m_videoHashCancellation = std::make_shared<std::atomic_bool>(false);
    const auto cancellation = m_videoHashCancellation;
    m_hashedVideoProbe = committed;
    QStringList paths;
    if (m_videoChapterStates.isEmpty()) paths.append(committed.path);
    for (const auto &chapter : m_videoChapterStates) paths.append(chapter.available ? chapter.path : QString());
    m_videoHashWatcher.setFuture(QtConcurrent::run(
        [paths, generation = committed.generation, path = committed.path, cancellation] {
        VideoHashResult result{generation, path, {}, false};
        for (const auto &chapterPath : paths) {
            result.digests.append(chapterPath.isEmpty() ? QString()
                : ProjectSourceReferenceCodec::fileSha256(chapterPath, [cancellation] { return cancellation->load(); }));
        }
        result.cancelled = cancellation->load();
        return result;
    }));
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
        result.path, result.fingerprint, QString::fromLatin1(result.contentRevision));
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
    startVideoProbe(path, generation, true, m_videoReference.fingerprint, true, videoChapterInputs(),
                    m_videoReference.contentSha256);
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
    startVboLoad(path, generation, true, m_vboReference.fingerprint, true, m_vboReference.contentSha256);
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
        // A replacement has no saved content identity to keep (KAN-208).
        m_pendingMismatchVideo.expectedContentSha256.clear();
        for (auto &chapter : m_pendingMismatchVideo.chapters) chapter.reference.contentSha256.clear();
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
                        result.videoReference.fingerprint, false, chapters, result.videoReference.contentSha256);
    }
    if (!result.resolvedVboPath.isEmpty()) {
        startVboLoad(result.resolvedVboPath, result.generation, false,
                     result.vboReference.fingerprint, false, result.vboReference.contentSha256);
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
    if (m_export.exporting()) {
        AppLog::warn(QStringLiteral("Export request ignored because an export is already running"));
        return false;
    }
    const QString inputPath = m_videoSource.toLocalFile();
    const QString outputPath = output.toLocalFile();
    if (!m_session || inputPath.isEmpty() || m_telemetryPath.isEmpty() || outputPath.isEmpty()) {
        m_export.fail(QStringLiteral("Open a video and telemetry, then choose an output file."));
        return false;
    }
    if (videoChaptered() && m_exportChapterPaths.isEmpty()) {
        // KAN-106: never export the first chapter alone as if it were the
        // whole recording.
        m_export.fail(m_exportChapterProblem.isEmpty()
            ? QStringLiteral("The chapters of this recording cannot be exported together.") : m_exportChapterProblem);
        return false;
    }
    ExportController::Job job;
    job.inputPath = inputPath;
    job.chapterPaths = m_exportChapterPaths;
    if (!m_exportChapterPaths.isEmpty())
        for (const auto &chapter : m_videoChapterStates) job.chapterDurationTicks.append(chapter.mediaInfo.videoDurationTicks);
    job.telemetryPath = m_telemetryPath;
    job.protectedPaths = QStringList{m_telemetryPath} + m_exportChapterPaths; // every chapter is a source (KAN-106)
    if (EventProjectCodec::isEvent(m_document.storedProject())) {
        job.protectedPaths.append(m_document.documentPath());
        job.protectedPaths.append(EventProjectCodec::referencedPaths(currentProjectObject(), m_document.documentPath()));
    }
    job.lapBinding = activeLapBinding();
    job.lapExclusions = currentProjectObject().value("event").toObject().value("lapExclusions").toArray();
    job.widgets = m_widgetModel.toJson();
    job.sync = m_sync;
    job.source = m_exportSourceInfo;
    return m_export.start(job, {outputPath, QSize(outputWidth, outputHeight),
                                MediaRational{frameRateNumerator, frameRateDenominator}, videoBitrate,
                                audioEnabled, customRange, rangeIn, rangeOut, overwriteAllowed});
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
