#include "app/AppController.h"
#include "export/VideoComposition.h"
#include "app/PreviewTimeline.h"
#include "app/ExportSourceOptions.h"
#include "app/LapNavigation.h"
#include "app/ChapterPlayback.h"
#include "app/ExportJobPlan.h"
#include "app/PlaybackReadout.h"
#include "app/SourceLoading.h"
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

AppController::AppController(QObject *parent, QString recoveryPath,
                             ProjectRecoveryStore::Operations recoveryOperations)
    : QObject(parent)
    , m_settings()
    , m_previewRenderContext(this)
    , m_document(*this, std::move(recoveryPath), std::move(recoveryOperations))
{
    connect(&m_export, &ExportController::statusMessage, this, &AppController::setStatus);
    connect(&m_export, &ExportController::quitRequested, this, &AppController::requestQuit);
    m_previewRenderContext.setSyncTransform(m_syncController.transform());
    connect(&m_syncController, &SyncController::statusMessage, this, &AppController::setStatus);
    connect(&m_syncController, &SyncController::edited, this, [this] {
        m_previewRenderContext.setSyncTransform(m_syncController.transform());
        emit lapNavigationChanged();
        emit liveValuesChanged();
        markPersistentChange();
    });
    connect(&m_additionalVideos, &AdditionalVideoController::statusMessage, this, &AppController::setStatus);
    connect(&m_additionalVideos, &AdditionalVideoController::edited, this, [this] { markPersistentChange(); });
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
        // KAN-254: camera box widgets place the picture-in-picture boxes.
        m_additionalVideos.setCameraBoxes(VideoComposition::cameraBoxes(m_widgetModel.toJson()));
    });
    connect(&m_widgetModel, &WidgetModel::lastErrorChanged, this, [this] {
        if (!m_widgetModel.lastError().isEmpty()) setStatus(m_widgetModel.lastError());
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
               || m_document.projectLoadRunning() || m_syncController.running() || m_document.importRunning())) {
        QThread::msleep(10);
    }
    if (m_videoProbeWatcher.isRunning() || m_vboLoadWatcher.isRunning()
        || m_document.projectLoadRunning() || m_syncController.running() || m_document.importRunning()) {
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
bool AppController::documentBusy() const { return m_export.exporting(); }
QVariantMap AppController::exportSourceInfo() const
{
    return ExportSourceOptions(m_exportSourceInfo).sourceInfo();
}
QVariantMap AppController::exportFormatOptions() const
{
    return ExportSourceOptions(m_exportSourceInfo).formatOptions();
}
qint64 AppController::estimateExportSize(const qint64 videoBitrate, const bool audioEnabled, const double seconds) const
{
    return ExportFormat::estimatedBytes(videoBitrate, audioEnabled, seconds);
}
QString AppController::formatEstimatedExportSize(const qint64 bytes) const
{
    return ExportFormat::formatEstimatedSize(bytes);
}
PreviewTimeline AppController::previewTimeline() const
{
    return {m_exportSourceInfo,
            videoChaptered() ? std::optional<double>(m_videoTimeline.durationSeconds()) : std::nullopt};
}
QVariantMap AppController::previewViewport(const int availableWidth, const int availableHeight) const
{
    const QRect viewport = previewTimeline().viewport({availableWidth, availableHeight});
    return {{"x", viewport.x()}, {"y", viewport.y()},
            {"width", viewport.width()}, {"height", viewport.height()}};
}
qint64 AppController::previewEndPositionMilliseconds() const
{
    return previewTimeline().endPositionMilliseconds();
}
qint64 AppController::previewInitialPositionMilliseconds() const
{
    return previewTimeline().initialPositionMilliseconds();
}
qint64 AppController::clampPreviewPositionMilliseconds(const qint64 requestedMilliseconds) const
{
    return previewTimeline().clampPositionMilliseconds(requestedMilliseconds);
}
QString AppController::previewTimecodeForPositionMilliseconds(const qint64 positionMilliseconds) const
{
    return previewTimeline().timecodeForPositionMilliseconds(positionMilliseconds);
}
QString AppController::previewEndTimecode() const
{
    return previewTimeline().endTimecode();
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
    return ExportSourceOptions(m_exportSourceInfo).recommendedBitrate(
        {width, height}, {numerator, denominator}, quality);
}
QString AppController::fixedFontFamily() const
{
    return QFontDatabase::systemFont(QFontDatabase::FixedFont).family();
}
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
    return LapNavigation::timingStatus(m_lapSession, m_session != nullptr);
}
QVariantList AppController::lapSummaries() const
{
    return LapNavigation::summaries(m_lapSession, activeRunId());
}
QVariantList AppController::lapNavigationSegments() const
{
    return LapNavigation::segments(m_lapSession, previewTimeline(), [this](const double telemetryTime) {
        return videoMillisecondsForTelemetryTime(telemetryTime);
    });
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
        || m_document.projectLoadRunning() || m_syncController.running();
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
                     request.expectedFingerprint, request.relink, request.expectedContentSha256);
    }
    if (video || vbo) AppLog::info(QStringLiteral("Interrupted source loads resumed after a failed project open"));
}

void AppController::cancelSourceJobs()
{
    m_document.cancelProjectLoad();
    m_syncController.cancel();
    for (const auto &cancellation : {m_videoProbeCancellation, m_vboLoadCancellation}) {
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
        return SourceLoading::probeVideo(path, generation, cancellation, expectedFingerprint, relink, chapters);
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
        return SourceLoading::loadTelemetry(path, generation, cancellation, expectedRevision, expectedFingerprint, relink);
    }));
}

void AppController::commitVideoProbe(const VideoProbeResult &result, const bool markDocumentDirty)
{
    AppLog::info(QStringLiteral("Video load succeeded: %1").arg(result.path));
    m_videoSource = QUrl::fromLocalFile(result.path);
    m_exportSourceInfo = result.mediaInfo;
    // KAN-208: videos keep the sampled video-v1 check; only telemetry carries
    // a full-content identity (owner decision, 6 October 2026).
    m_videoReference = ProjectSourceReferenceCodec::forLoadedSource(
        result.path, result.fingerprint);
    // KAN-105: chapters play as one timeline; a gap keeps its saved duration.
    // KAN-106: export reads every chapter as one source, so its frame domain
    // and timecodes cover the whole timeline.
    const auto derived = SourceLoading::deriveChapters(result.chapters);
    m_videoChapterStates = derived.chapters;
    m_videoChapterIndex = 0;
    m_videoTimeline = derived.timeline;
    if (derived.unusableDurations) {
        AppLog::warn(QStringLiteral("Video chapters without usable durations; opening the first chapter alone"));
        m_videoChapterNotice = tr("Some chapters have no known duration, so only the first chapter is open.");
    }
    if (derived.gaps > 0)
        m_videoChapterNotice = tr("%n chapter(s) are missing or changed and play as a gap. Choose the recording's chapters again to fill it.",
                                  nullptr, derived.gaps);
    m_exportChapterPaths = derived.exportPaths;
    if (derived.exportInfo) m_exportSourceInfo = *derived.exportInfo;
    m_exportChapterProblem = derived.gapsBlockExport
        ? tr("Some chapters of this recording are missing or changed. "
             "Choose the recording's chapters again before exporting.")
        : derived.exportProblem;
    if (!m_exportChapterProblem.isEmpty())
        AppLog::warn(QStringLiteral("Chaptered video cannot be exported: %1").arg(m_exportChapterProblem));
    emit videoChaptersChanged();
    m_videoLoadState = QStringLiteral("ready");
    m_pendingVideoPath.clear();
    m_syncController.clearCandidate();
    emit videoSourceChanged();
    emit previewMetadataChanged();
    emit lapNavigationChanged();
    emit exportChanged();
    emit m_syncController.candidateChanged();
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
        result.path, result.fingerprint, QString::fromLatin1(result.contentRevision));
    m_vboLoadState = QStringLiteral("ready");
    m_pendingVboPath.clear();
    m_syncController.clearCandidate();
    m_previewRenderContext.setSession(m_session.get());
    m_previewRenderContext.setTrackGeometry(&m_trackGeometry);
    if (markDocumentDirty && EventProjectCodec::isEvent(m_document.storedProject())) {
        // Replacement clears asserted layout/direction, then records the gates
        // actually verified in the new source. This enables fresh inference;
        // the old source's inference goes, as Overlays no longer derives one.
        m_document.replaceStoredProject(EventProjectCodec::withReplacedRecording(
            currentProjectObject(), activeRunId(), timingGateRevision(result.session)));
    }
    applyActiveLapExclusions();
    emit telemetryChanged();
    emit lapNavigationChanged();
    emit liveValuesChanged();
    emit m_syncController.candidateChanged();
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
    return ChapterPlayback::chapterSource(m_videoTimeline, m_videoChapterIndex);
}

qint64 AppController::videoChapterStartMilliseconds() const
{
    return videoChaptered() ? ChapterPlayback::chapterStartMilliseconds(m_videoTimeline, m_videoChapterIndex) : 0;
}

QVariantList AppController::videoChapterList() const
{
    if (!videoChaptered()) return {};
    QVector<ChapterPlayback::ChapterLabel> labels;
    labels.reserve(m_videoChapterStates.size());
    for (const VideoChapterState &state : m_videoChapterStates) {
        labels.append({QFileInfo(state.reference.displayPath()).fileName(), state.problem});
    }
    return ChapterPlayback::chapterList(m_videoTimeline, labels);
}

QVariantMap AppController::locateVideoTimeline(const qint64 timelineMilliseconds) const
{
    if (!videoChaptered()) return {{"chapter", 0}, {"localMilliseconds", timelineMilliseconds}, {"gap", false}};
    return ChapterPlayback::locate(m_videoTimeline, timelineMilliseconds);
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

int AppController::addWidget(const QString &type)
{
    if (type != QLatin1String("cameraBox")) return m_widgetModel.addWidget(type);
    const QJsonArray existing = m_widgetModel.toJson();
    QStringList used;
    QVector<QRectF> occupied;
    for (const QJsonValue &value : existing) {
        const QJsonObject widget = value.toObject();
        if (!widget.value("visible").toBool(true)) continue;
        // A widget scales about its centre.
        const double scale = std::max(0.0, widget.value("scale").toDouble(1.0));
        const double width = widget.value("width").toDouble() * scale, height = widget.value("height").toDouble() * scale;
        occupied.append(QRectF(widget.value("x").toDouble() + (widget.value("width").toDouble() - width) / 2.0,
                               widget.value("y").toDouble() + (widget.value("height").toDouble() - height) / 2.0, width, height));
        if (widget.value("type").toString() == QLatin1String("cameraBox"))
            used.append(widget.value("settings").toObject().value("camera").toString());
    }
    const int index = m_widgetModel.addWidget(type);
    if (index < 0) return index;
    const QVariantList cameras = m_additionalVideos.cameraList();
    QString camera;
    for (const QVariant &entry : cameras) {
        const QString id = entry.toMap().value(QStringLiteral("id")).toString();
        if (camera.isEmpty()) camera = id; // every camera has a box: start with the first
        if (!used.contains(id)) { camera = id; break; }
    }
    const QRectF area = VideoComposition::startingBoxArea(occupied);
    m_widgetModel.setSetting(index, QStringLiteral("camera"), camera);
    m_widgetModel.moveWidget(index, area.x(), area.y());
    return index;
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
    return PlaybackReadout::valueText(m_session.get(), m_syncController.transform(), m_playbackTime, channelName,
        decimals);
}

QVariant AppController::telemetryValue(const QString &channelName) const
{
    const auto value = PlaybackReadout::valueAt(m_session.get(), m_syncController.transform(), m_playbackTime,
        channelName);
    return value ? QVariant(*value) : QVariant();
}

QVariantMap AppController::telemetrySeries(
    const QString &channelName,
    const double videoStart,
    const double videoEnd,
    const int maximumPoints) const
{
    return PlaybackReadout::series(m_session.get(), m_syncController.transform(), channelName, videoStart, videoEnd,
        maximumPoints);
}

int AppController::lapNumberAtPlayback() const
{
    return LapNavigation::lapNumberAt(
        m_lapSession, FlappedEar::videoToTelemetryTime(m_playbackTime, m_syncController.transform()));
}

qint64 AppController::videoMillisecondsForTelemetryTime(const double telemetryTime) const
{
    if (!m_exportSourceInfo.videoSize.isValid()) return -1;
    const auto videoTime = telemetryToVideoTime(telemetryTime, m_syncController.transform());
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
        const auto chapters = SourceLoading::savedChapters(
            editor.value("sources").toObject().value("video").toObject(), result.projectPath).further;
        startVideoProbe(result.resolvedVideoPath, result.generation, false,
                        result.videoReference.fingerprint, false, chapters);
    }
    if (!result.resolvedVboPath.isEmpty()) {
        startVboLoad(result.resolvedVboPath, result.generation, false,
                     result.vboReference.fingerprint, false, result.vboReference.contentSha256);
    }
}

void AppController::autoSync()
{
    if (m_syncController.running()) {
        return;
    }
    if (m_videoSource.toLocalFile().isEmpty() || !m_session) {
        AppLog::info(QStringLiteral("Auto-sync requested"));
        setStatus("Open both a GoPro video and telemetry before auto sync.");
        return;
    }
    m_syncController.start(*m_session, currentSyncSources());
}

SyncController::Sources AppController::currentSyncSources() const
{
    return {m_document.sourceGeneration(), normalizedSourcePath(m_videoSource.toLocalFile()),
            normalizedSourcePath(m_telemetryPath)};
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
    ExportJobPlan::JobInputs inputs;
    inputs.sources = {m_session != nullptr, m_videoSource.toLocalFile(), m_telemetryPath, output.toLocalFile(),
                      videoChaptered(), m_exportChapterPaths, m_exportChapterProblem};
    if (const QString problem = ExportJobPlan::startProblem(inputs.sources); !problem.isEmpty()) {
        m_export.fail(problem);
        return false;
    }
    QString additionalProblem;
    inputs.additionalVideos = m_additionalVideos.exportVideos(&additionalProblem);
    if (!additionalProblem.isEmpty()) {
        m_export.fail(additionalProblem);
        return false;
    }
    inputs.chapters = m_videoChapterStates;
    inputs.isEvent = EventProjectCodec::isEvent(m_document.storedProject());
    if (inputs.isEvent) {
        inputs.documentPath = m_document.documentPath();
        inputs.referencedPaths = EventProjectCodec::referencedPaths(currentProjectObject(), m_document.documentPath());
    }
    inputs.lapBinding = activeLapBinding();
    inputs.lapExclusions = currentProjectObject().value("event").toObject().value("lapExclusions").toArray();
    inputs.widgets = m_widgetModel.toJson();
    inputs.sync = m_syncController.transform();
    inputs.source = m_exportSourceInfo;
    inputs.videoLayout = m_additionalVideos.videoLayout();
    const auto job = ExportJobPlan::buildJob(inputs);
    const QString outputPath = inputs.sources.outputPath;
    return m_export.start(job, {outputPath, QSize(outputWidth, outputHeight),
                                MediaRational{frameRateNumerator, frameRateDenominator}, videoBitrate,
                                audioEnabled, customRange, rangeIn, rangeOut, overwriteAllowed});
}

QString AppController::exportFullRangeTimecode(
    const qint64 frameRateNumerator, const qint64 frameRateDenominator, const bool outPoint) const
{
    return ExportSourceOptions(m_exportSourceInfo).fullRangeTimecode(
        {frameRateNumerator, frameRateDenominator}, outPoint);
}

QVariantMap AppController::lapExportRange(
    const int lapNumber, const qint64 frameRateNumerator, const qint64 frameRateDenominator,
    const int handleSeconds) const
{
    const auto lap = std::find_if(m_lapSession.timedLaps.cbegin(), m_lapSession.timedLaps.cend(),
                                  [lapNumber](const TimedLap &candidate) {
        return candidate.number == lapNumber;
    });
    if (lap == m_lapSession.timedLaps.cend()) return {{QStringLiteral("valid"), false}};

    const auto videoStart = telemetryToVideoTime(lap->startTelemetryTime, m_syncController.transform());
    const auto videoEnd = telemetryToVideoTime(lap->startTelemetryTime + lap->durationSeconds, m_syncController.transform());
    if (!videoStart || !videoEnd) return {{QStringLiteral("valid"), false}};
    return ExportSourceOptions(m_exportSourceInfo).lapRange(
        lapNumber, *videoStart, *videoEnd, {frameRateNumerator, frameRateDenominator}, handleSeconds);
}

double AppController::exportRangeDurationSeconds(
    const qint64 frameRateNumerator, const qint64 frameRateDenominator,
    const QString &rangeIn, const QString &rangeOut) const
{
    return ExportSourceOptions(m_exportSourceInfo).rangeDurationSeconds(
        {frameRateNumerator, frameRateDenominator}, rangeIn, rangeOut);
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

QVariant AppController::semanticValue(const QString &alias) const
{
    return telemetryValue(alias);
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

} // namespace FlappedEar
