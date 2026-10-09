#include "app/AdditionalVideoController.h"

#include "app/AppLog.h"
#include "app/DocumentController.h"
#include "export/VideoComposition.h"
#include "export/VideoFingerprint.h"

#include <QFileInfo>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>

#include <cmath>

namespace FlappedEar {
namespace {

bool supportedVideo(const QFileInfo &info)
{
    const QString extension = info.suffix().toLower();
    return info.isFile() && (extension == QStringLiteral("mp4") || extension == QStringLiteral("mov"));
}

double videoDuration(const MediaInfo &info)
{
    return info.videoDuration > 0.0 ? info.videoDuration : info.duration;
}

} // namespace

AdditionalVideoController::AdditionalVideoController(CurrentMainVideo mainVideo, QObject *parent)
    : QObject(parent), m_mainVideo(std::move(mainVideo))
{
}

AdditionalVideoController::~AdditionalVideoController()
{
    cancelProbes();
}

QVector<AdditionalVideo> AdditionalVideoController::videos() const
{
    QVector<AdditionalVideo> videos;
    for (const auto &entry : m_entries) videos.append(entry.video);
    return videos;
}

QVector<AdditionalVideoController::ExportVideo> AdditionalVideoController::exportVideos(QString *problem) const
{
    if (m_pendingAdds > 0) {
        if (problem) *problem = QStringLiteral("An additional video is still loading. Export when it is ready.");
        return {};
    }
    QVector<ExportVideo> videos;
    for (const auto &entry : m_entries) {
        const QString name = entry.video.label.isEmpty()
            ? QFileInfo(entry.path.isEmpty() ? entry.video.reference.displayPath() : entry.path).fileName()
            : entry.video.label;
        QString reason;
        if (entry.state == QStringLiteral("loading")) reason = QStringLiteral("is still loading. Export when it is ready.");
        else if (entry.state == QStringLiteral("missing")) reason = QStringLiteral("was not found. Locate it or remove it before exporting.");
        else if (entry.state == QStringLiteral("mismatch")) reason = QStringLiteral("has changed since it was saved. Locate the original or remove it before exporting.");
        else if (entry.state != QStringLiteral("ready")) reason = QStringLiteral("could not be read. Remove it before exporting.");
        if (!reason.isEmpty()) {
            if (problem) *problem = QStringLiteral("The additional video \"%1\" %2").arg(name, reason);
            return {};
        }
        videos.append({entry.video.id, entry.path, name, entry.video.sync});
    }
    return videos;
}

QVariantList AdditionalVideoController::videoList() const
{
    QVariantList list;
    for (const auto &entry : m_entries) {
        const QString shown = entry.path.isEmpty() ? entry.video.reference.displayPath() : entry.path;
        list.append(QVariantMap{
            {QStringLiteral("id"), entry.video.id},
            {QStringLiteral("label"), entry.video.label},
            {QStringLiteral("name"), QFileInfo(shown).fileName()},
            {QStringLiteral("path"), shown},
            {QStringLiteral("url"), entry.state == QStringLiteral("ready") ? QUrl::fromLocalFile(entry.path) : QUrl()},
            {QStringLiteral("state"), entry.state},
            {QStringLiteral("problem"), entry.problem},
            {QStringLiteral("offset"), entry.video.sync.offset},
            {QStringLiteral("timeScale"), entry.video.sync.timeScale},
            {QStringLiteral("durationSeconds"), videoDuration(entry.mediaInfo)},
            {QStringLiteral("width"), entry.mediaInfo.displayVideoSize.width()},
            {QStringLiteral("height"), entry.mediaInfo.displayVideoSize.height()},
            {QStringLiteral("frameRate"), entry.mediaInfo.frameRate.isValid() ? entry.mediaInfo.frameRate.value() : 0.0},
        });
    }
    return list;
}

bool AdditionalVideoController::loading() const
{
    if (m_pendingAdds > 0) return true;
    for (const auto &entry : m_entries)
        if (entry.state == QStringLiteral("loading")) return true;
    return false;
}

QString AdditionalVideoController::layout() const
{
    return m_layout.mode == VideoLayoutMode::SideBySide ? QStringLiteral("sideBySide") : QStringLiteral("pictureInPicture");
}

void AdditionalVideoController::setLayout(const QString &layout)
{
    VideoLayoutMode mode;
    if (layout == QStringLiteral("sideBySide")) mode = VideoLayoutMode::SideBySide;
    else if (layout == QStringLiteral("pictureInPicture")) mode = VideoLayoutMode::PictureInPicture;
    else return;
    if (mode == m_layout.mode) return;
    m_layout.mode = mode;
    emit edited();
    emit changed();
}

void AdditionalVideoController::setVideoLayout(const VideoLayout &layout)
{
    if (layout == m_layout) return;
    m_layout = layout;
    emit edited();
    emit changed();
}

void AdditionalVideoController::restore(
    const QVector<AdditionalVideo> &videos, const VideoLayoutMode layout, const QString &projectPath)
{
    VideoLayout settings;
    settings.mode = layout;
    restore(videos, settings, projectPath);
}

void AdditionalVideoController::restore(
    const QVector<AdditionalVideo> &videos, const VideoLayout &layout, const QString &projectPath)
{
    cancelProbes();
    ++m_generation;
    m_pendingAdds = 0;
    m_entries.clear();
    m_layout = layout;
    for (const auto &video : videos) {
        Entry entry;
        entry.video = video;
        entry.state = QStringLiteral("missing");
        const QString resolved = ProjectSourceReferenceCodec::resolve(video.reference, projectPath);
        if (!resolved.isEmpty()) {
            entry.path = DocumentController::normalizedSourcePath(resolved);
            entry.state = QStringLiteral("loading");
        }
        m_entries.append(entry);
    }
    for (const auto &entry : std::as_const(m_entries))
        if (entry.state == QStringLiteral("loading"))
            probe(entry.video.id, entry.path, ProbePurpose::Restore);
    emit changed();
}

void AdditionalVideoController::clear()
{
    restore({}, VideoLayoutMode::PictureInPicture, {});
}

void AdditionalVideoController::updateReferences(const QVector<AdditionalVideo> &saved)
{
    for (const auto &video : saved) {
        const qsizetype index = indexOf(video.id);
        if (index >= 0) m_entries[index].video.reference = video.reference;
    }
}

void AdditionalVideoController::addVideo(const QUrl &url)
{
    const QFileInfo info(url.toLocalFile());
    if (!supportedVideo(info)) {
        emit statusMessage(QStringLiteral("Choose an existing MP4 or MOV video."));
        return;
    }
    if (m_entries.size() + m_pendingAdds >= maximum()) {
        emit statusMessage(QStringLiteral("A run can have at most %1 additional videos.").arg(maximum()));
        return;
    }
    const QString path = DocumentController::normalizedSourcePath(info.absoluteFilePath());
    if (pathInUse(path, -1)) {
        emit statusMessage(QStringLiteral("%1 is already a video of this run.").arg(info.fileName()));
        return;
    }
    ++m_pendingAdds;
    probe(unusedId(), path, ProbePurpose::Add);
    emit statusMessage(QStringLiteral("Loading additional video: %1").arg(info.fileName()));
    emit changed();
}

void AdditionalVideoController::removeVideo(const int index)
{
    if (!validIndex(index)) return;
    m_entries.removeAt(index);
    emit edited();
    emit changed();
}

void AdditionalVideoController::relinkVideo(const int index, const QUrl &url)
{
    if (!validIndex(index)) return;
    const QFileInfo info(url.toLocalFile());
    if (!supportedVideo(info)) {
        emit statusMessage(QStringLiteral("Choose an existing MP4 or MOV video."));
        return;
    }
    const QString path = DocumentController::normalizedSourcePath(info.absoluteFilePath());
    if (pathInUse(path, index)) {
        emit statusMessage(QStringLiteral("%1 is already a video of this run.").arg(info.fileName()));
        return;
    }
    auto &entry = m_entries[index];
    if (entry.state != QStringLiteral("loading")) entry.stateBeforeRelink = entry.state;
    entry.state = QStringLiteral("loading");
    probe(entry.video.id, path, ProbePurpose::Relink);
    emit changed();
}

void AdditionalVideoController::setLabel(const int index, const QString &label)
{
    if (!validIndex(index)) return;
    const QString trimmed = label.trimmed().left(AdditionalVideosCodec::maximumLabelCharacters);
    if (trimmed.contains(QChar::Null) || trimmed == m_entries[index].video.label) return;
    m_entries[index].video.label = trimmed;
    emit edited();
    emit changed();
}

void AdditionalVideoController::setOffset(const int index, const double seconds)
{
    if (!validIndex(index) || !std::isfinite(seconds) || seconds == m_entries[index].video.sync.offset) return;
    m_entries[index].video.sync.offset = seconds;
    emit edited();
    emit changed();
}

void AdditionalVideoController::setTimeScale(const int index, const double scale)
{
    if (!validIndex(index) || !std::isfinite(scale) || scale <= 0.0
        || scale == m_entries[index].video.sync.timeScale)
        return;
    m_entries[index].video.sync.timeScale = scale;
    emit edited();
    emit changed();
}

void AdditionalVideoController::alignAt(const int index, const double mainVideoSeconds, const double videoSeconds)
{
    if (!validIndex(index) || !std::isfinite(mainVideoSeconds) || !std::isfinite(videoSeconds)) return;
    const auto telemetry = videoToTelemetryTime(mainVideoSeconds, m_mainVideo().sync);
    if (telemetry) setOffset(index, *telemetry - videoSeconds * m_entries[index].video.sync.timeScale);
}

double AdditionalVideoController::videoSecondsFor(const int index, const double mainVideoSeconds) const
{
    if (!validIndex(index) || !std::isfinite(mainVideoSeconds)) return 0.0;
    const auto telemetry = videoToTelemetryTime(mainVideoSeconds, m_mainVideo().sync);
    const auto video = telemetry ? telemetryToVideoTime(*telemetry, m_entries[index].video.sync) : std::nullopt;
    return video.value_or(0.0);
}

double AdditionalVideoController::playbackRateFor(const int index) const
{
    if (!validIndex(index)) return 1.0;
    const double scale = m_entries[index].video.sync.timeScale;
    return scale > 0.0 ? m_mainVideo().sync.timeScale / scale : 1.0;
}

QVariantList AdditionalVideoController::previewRects(const double width, const double height) const
{
    const QSize frame(static_cast<int>(std::lround(width)), static_cast<int>(std::lround(height)));
    QVector<QSize> sizes{frame};
    for (const auto &entry : m_entries) {
        const QSize size = entry.mediaInfo.displayVideoSize.isEmpty() ? entry.mediaInfo.videoSize : entry.mediaInfo.displayVideoSize;
        sizes.append(size.isEmpty() ? QSize(16, 9) : size); // not ready yet: keep its place
    }
    QVariantList rects;
    for (const QRect &rect : VideoComposition::layout(m_layout.mode, frame, sizes))
        rects.append(QVariantMap{{QStringLiteral("x"), rect.x()}, {QStringLiteral("y"), rect.y()},
                                 {QStringLiteral("width"), rect.width()}, {QStringLiteral("height"), rect.height()}});
    return rects;
}

QVariantList AdditionalVideoController::previewLayers(const double width, const double height) const
{
    if (m_layout.mode != VideoLayoutMode::PictureInPicture || m_entries.isEmpty()) return {};
    const QSize frame(static_cast<int>(std::lround(width)), static_cast<int>(std::lround(height)));
    QStringList ids{mainCameraId};
    QVector<QSize> sizes{frame};
    for (const auto &entry : m_entries) {
        ids.append(entry.video.id);
        const QSize size = entry.mediaInfo.displayVideoSize.isEmpty() ? entry.mediaInfo.videoSize : entry.mediaInfo.displayVideoSize;
        sizes.append(size.isEmpty() ? QSize(16, 9) : size); // not ready yet: keep its place
    }
    QVariantList result;
    const auto rectMap = [](QVariantMap &map, const QString &prefix, const QRect &rect) {
        const bool outer = prefix.isEmpty();
        map.insert(outer ? QStringLiteral("x") : prefix + QStringLiteral("X"), rect.x());
        map.insert(outer ? QStringLiteral("y") : prefix + QStringLiteral("Y"), rect.y());
        map.insert(outer ? QStringLiteral("width") : prefix + QStringLiteral("Width"), rect.width());
        map.insert(outer ? QStringLiteral("height") : prefix + QStringLiteral("Height"), rect.height());
    };
    int index = 0;
    for (const auto &layer : VideoComposition::plan(m_layout, frame, ids, sizes, 1.0e7)) {
        QVariantMap map{{QStringLiteral("camera"), layer.camera}, {QStringLiteral("index"), index++},
            {QStringLiteral("onAir"), layer.onAir}, {QStringLiteral("border"), layer.border},
            {QStringLiteral("borderColor"), layer.borderColor}, {QStringLiteral("fadeIn"), layer.fadeInSeconds}};
        rectMap(map, QString(), layer.rect);
        rectMap(map, QStringLiteral("content"), layer.content);
        QVariantList windows;
        for (const auto &window : layer.windows)
            windows.append(QVariantMap{{QStringLiteral("start"), window.start}, {QStringLiteral("end"), window.end}});
        map.insert(QStringLiteral("windows"), windows);
        result.append(map);
    }
    return result;
}

void AdditionalVideoController::probe(const QString &id, const QString &path, const ProbePurpose purpose)
{
    const quint64 generation = m_generation;
    const quint64 request = ++m_nextRequest;
    if (purpose != ProbePurpose::Add) {
        const qsizetype index = indexOf(id);
        if (index >= 0) m_entries[index].request = request;
    }
    AppLog::info(QStringLiteral("Additional video probe started: %1").arg(path));
    const auto cancellation = m_cancellation;
    auto *watcher = new QFutureWatcher<ProbeResult>(this);
    connect(watcher, &QFutureWatcher<ProbeResult>::finished, this,
        [this, watcher, id, generation, request, path, purpose] {
            const ProbeResult result = watcher->result();
            watcher->deleteLater();
            finishProbe(id, generation, request, path, purpose, result);
        });
    watcher->setFuture(QtConcurrent::run([path, cancellation] {
        ProbeResult result;
        try {
            result.mediaInfo = MediaProbe::probeSummary(path, {}, 30'000, {}, [cancellation] { return cancellation->load(); });
            result.fingerprint = videoSourceFingerprint(path, result.mediaInfo);
            result.success = !result.mediaInfo.displayVideoSize.isEmpty() && videoDuration(result.mediaInfo) > 0.0;
            if (!result.success) result.error = QStringLiteral("no readable video stream");
        } catch (const std::exception &error) {
            result.cancelled = cancellation->load();
            result.error = QString::fromUtf8(error.what());
        }
        return result;
    }));
}

void AdditionalVideoController::finishProbe(const QString &id, const quint64 generation, const quint64 request,
    const QString &path, const ProbePurpose purpose, const ProbeResult &result)
{
    if (generation != m_generation) {
        AppLog::warn(QStringLiteral("Stale additional video probe result rejected: %1").arg(path));
        return;
    }
    const QString fileName = QFileInfo(path).fileName();
    if (purpose == ProbePurpose::Add) {
        m_pendingAdds = std::max(0, m_pendingAdds - 1);
        if (!result.success) {
            if (!result.cancelled) emit statusMessage(QStringLiteral("Could not open video: %1\n%2").arg(path, result.error));
            emit changed();
            return;
        }
        if (m_entries.size() >= maximum() || pathInUse(path, -1) || indexOf(id) >= 0) {
            emit changed();
            return;
        }
        const MainVideo main = m_mainVideo();
        Entry entry;
        entry.video = {id, QFileInfo(path).completeBaseName().left(AdditionalVideosCodec::maximumLabelCharacters),
            ProjectSourceReferenceCodec::forLoadedSource(path, result.fingerprint), main.sync};
        entry.path = path;
        entry.state = QStringLiteral("ready");
        entry.mediaInfo = result.mediaInfo;
        m_entries.append(entry);
        emit statusMessage(QStringLiteral("Added video %1. Align it in Sync.").arg(fileName));
        emit edited();
        emit changed();
        return;
    }
    const qsizetype index = indexOf(id);
    if (index < 0 || m_entries[index].request != request) {
        AppLog::warn(QStringLiteral("Superseded additional video probe result rejected: %1").arg(path));
        return;
    }
    auto &entry = m_entries[index];
    if (!result.success) {
        if (purpose == ProbePurpose::Relink) {
            entry.state = entry.stateBeforeRelink;
            if (!result.cancelled) emit statusMessage(QStringLiteral("Could not open video: %1\n%2").arg(path, result.error));
        } else {
            entry.state = result.cancelled ? QStringLiteral("missing") : QStringLiteral("error");
            entry.problem = result.error;
        }
        emit changed();
        return;
    }
    if (ProjectSourceReferenceCodec::compareFingerprints(entry.video.reference.fingerprint, result.fingerprint)
        == SourceFingerprintMatch::Mismatch) {
        if (purpose == ProbePurpose::Relink) {
            entry.state = entry.stateBeforeRelink;
            emit statusMessage(QStringLiteral("%1 is not the video this project saved. Remove it and add the new file instead.").arg(fileName));
        } else {
            entry.state = QStringLiteral("mismatch");
            entry.problem = QStringLiteral("The file no longer matches the saved video.");
        }
        emit changed();
        return;
    }
    const bool moved = purpose == ProbePurpose::Relink && path != entry.path;
    entry.path = path;
    entry.state = QStringLiteral("ready");
    entry.problem.clear();
    entry.mediaInfo = result.mediaInfo;
    // A relink records the new location; a restore keeps the saved spelling.
    if (moved || entry.video.reference.fingerprint.isEmpty())
        entry.video.reference = ProjectSourceReferenceCodec::forLoadedSource(path, result.fingerprint);
    if (moved) emit edited();
    emit changed();
}

qsizetype AdditionalVideoController::indexOf(const QString &id) const
{
    for (qsizetype index = 0; index < m_entries.size(); ++index)
        if (m_entries[index].video.id == id) return index;
    return -1;
}

QString AdditionalVideoController::unusedId() const
{
    // Request numbers are unique in this session, so two pending adds never
    // share an id; a saved id that happens to match is skipped.
    for (quint64 number = m_nextRequest + 1;; ++number) {
        const QString id = QStringLiteral("video-%1").arg(number);
        if (indexOf(id) < 0) return id;
    }
}

bool AdditionalVideoController::pathInUse(const QString &path, const qsizetype except) const
{
    if (!m_mainVideo().path.isEmpty() && m_mainVideo().path == path) return true;
    for (qsizetype index = 0; index < m_entries.size(); ++index)
        if (index != except && m_entries[index].path == path) return true;
    return false;
}

void AdditionalVideoController::cancelProbes()
{
    m_cancellation->store(true);
    m_cancellation = std::make_shared<std::atomic_bool>(false);
}

} // namespace FlappedEar
