#include "app/VideoChapterReview.h"

#include "export/MediaProbe.h"
#include "telemetry/SourceOperation.h"

#include <QFileInfo>
#include <QtConcurrent>

namespace FlappedEar {
namespace {

constexpr int maximumReviewFiles = 64;
constexpr int probeTimeoutMilliseconds = 30'000; // per file, as for any metadata probe

QVariantMap entryMap(const ChapterEntry &entry)
{
    const auto &file = entry.file;
    return {{"path", file.path}, {"name", QFileInfo(file.path).fileName()}, {"chapter", entry.chapter},
        {"probed", file.probed}, {"error", file.probeError}, {"duration", file.duration}, {"codec", file.codec},
        {"width", file.size.width()}, {"height", file.size.height()}, {"frameRate", file.frameRate},
        {"creationTime", file.creationTime.isValid() ? file.creationTime.toString(Qt::ISODate) : QString()}};
}

} // namespace

VideoChapterReview::VideoChapterReview(QObject *parent)
    : QObject(parent)
{
    connect(&m_watcher, &QFutureWatcher<ProbeResult>::finished, this, [this] {
        const auto result = m_watcher.future().takeResult();
        if (result.request != m_request || result.cancelled) return; // superseded or cancelled
        m_groups = proposeChapterGroups(result.candidates);
        m_state = QStringLiteral("ready");
        m_message.clear();
        emit changed();
    });
}

VideoChapterReview::~VideoChapterReview()
{
    if (m_cancellation) m_cancellation->store(true);
    m_watcher.waitForFinished();
}

QVariantList VideoChapterReview::groups() const
{
    QVariantList result;
    for (const auto &group : m_groups) {
        QVariantList chapters, duplicates, missing;
        for (const auto &entry : group.chapters) chapters.append(entryMap(entry));
        for (const auto &entry : group.duplicates) duplicates.append(entryMap(entry));
        for (const int chapter : group.missingChapters) missing.append(chapter);
        result.append(QVariantMap{{"key", group.key}, {"goPro", group.goPro}, {"chapters", chapters},
            {"duplicates", duplicates}, {"missingChapters", missing}, {"issues", group.issues},
            {"needsReview", group.needsReview()}, {"manualOrder", group.manualOrder},
            {"totalDuration", group.totalDuration}});
    }
    return result;
}

bool VideoChapterReview::needsReview(const QList<QUrl> &urls)
{
    return urls.size() != 1 || parseGoProChapterName(QFileInfo(urls.first().toLocalFile()).fileName()).has_value();
}

bool VideoChapterReview::review(const QList<QUrl> &urls)
{
    QStringList paths;
    for (const auto &url : urls) {
        if (!url.isLocalFile()) return false;
        paths.append(url.toLocalFile());
    }
    if (paths.isEmpty() || paths.size() > maximumReviewFiles) {
        m_state = QStringLiteral("error");
        m_message = tr("Choose between 1 and %1 video files.").arg(maximumReviewFiles);
        emit changed();
        return false;
    }
    if (m_cancellation) m_cancellation->store(true);
    m_cancellation = std::make_shared<std::atomic_bool>(false);
    const auto cancellation = m_cancellation;
    const auto request = ++m_request;
    m_groups.clear();
    m_state = QStringLiteral("probing");
    m_message = tr("Reading %n video file(s)…", nullptr, static_cast<int>(paths.size()));
    emit changed();
    m_watcher.setFuture(QtConcurrent::run([paths, request, cancellation] {
        ProbeResult result;
        result.request = request;
        for (const auto &path : paths) {
            if (cancellation->load()) { result.cancelled = true; return result; }
            ChapterCandidate candidate;
            candidate.path = path;
            try {
                const auto info = MediaProbe::probeSummary(path, {}, probeTimeoutMilliseconds, {}, [cancellation] { return cancellation->load(); });
                candidate.probed = true;
                candidate.duration = info.duration;
                candidate.codec = info.videoCodec;
                candidate.size = info.videoSize;
                candidate.frameRate = info.frameRate.isValid() ? info.frameRate.value() : 0.0;
                candidate.creationTime = info.creationTime;
            } catch (const OperationCancelled &) {
                result.cancelled = true;
                return result;
            } catch (const std::exception &error) {
                candidate.probeError = QString::fromUtf8(error.what());
            }
            result.candidates.append(candidate);
        }
        return result;
    }));
    return true;
}

bool VideoChapterReview::moveChapter(const int group, const int from, const int to)
{
    if (m_state != QLatin1String("ready") || group < 0 || group >= m_groups.size()) return false;
    const auto &chapters = m_groups[group].chapters;
    if (from < 0 || from >= chapters.size() || to < 0 || to >= chapters.size() || from == to) return false;
    QStringList order;
    for (const auto &entry : chapters) order.append(entry.file.path);
    order.move(from, to);
    const auto reordered = reorderChapterGroup(m_groups[group], order);
    if (!reordered) return false;
    m_groups[group] = *reordered;
    emit changed();
    return true;
}

bool VideoChapterReview::choose(const int group)
{
    if (m_state != QLatin1String("ready") || group < 0 || group >= m_groups.size()) return false;
    const auto &chosen = m_groups[group];
    QList<QUrl> files;
    for (const auto &entry : chosen.chapters) {
        if (!entry.file.probed) return false; // an unreadable chapter cannot be used
        files.append(QUrl::fromLocalFile(entry.file.path));
    }
    if (files.isEmpty()) return false;
    const bool goPro = chosen.goPro;
    m_groups.clear();
    m_state = QStringLiteral("idle");
    m_message.clear();
    emit changed();
    emit groupChosen(files, goPro);
    return true;
}

void VideoChapterReview::cancel()
{
    if (m_cancellation) m_cancellation->store(true);
    ++m_request;
    m_groups.clear();
    m_state = QStringLiteral("idle");
    m_message.clear();
    emit changed();
}

} // namespace FlappedEar
