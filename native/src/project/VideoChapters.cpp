#include "project/VideoChapters.h"

#include <QDir>

#include <cmath>

namespace FlappedEar::VideoChaptersCodec {
namespace {

bool validPath(const QJsonObject &object, const QString &key, const bool absolute)
{
    if (!object.contains(key)) return true;
    const auto value = object.value(key);
    if (!value.isString() || value.toString().contains(QChar::Null) || value.toString().size() > 4096) return false;
    const auto path = value.toString();
    return path.isEmpty() || QDir::isAbsolutePath(path) == absolute;
}

ProjectSourceReference referenceOf(const QJsonObject &object)
{
    const auto digest = object.value("contentSha256").toString();
    return {object.value("relativePath").toString(), object.value("absolutePath").toString(),
        object.value("fingerprint").toObject(),
        ProjectSourceReferenceCodec::isContentSha256(digest) ? digest : QString()};
}

} // namespace

bool valid(const QJsonObject &video)
{
    if (!video.contains("chapters")) return true;
    const auto value = video.value("chapters");
    if (!value.isArray()) return false;
    const auto chapters = value.toArray();
    if (chapters.size() < minimumChapters || chapters.size() > maximumChapters) return false;
    for (qsizetype index = 0; index < chapters.size(); ++index) {
        if (!chapters[index].isObject()) return false;
        const auto chapter = chapters[index].toObject();
        if (!validPath(chapter, "relativePath", false) || !validPath(chapter, "absolutePath", true)) return false;
        if (chapter.value("relativePath").toString().trimmed().isEmpty()
            && chapter.value("absolutePath").toString().trimmed().isEmpty())
            return false;
        if (chapter.contains("fingerprint") && !chapter.value("fingerprint").isObject()) return false;
        if (chapter.contains("contentSha256")
            && !ProjectSourceReferenceCodec::isContentSha256(chapter.value("contentSha256").toString()))
            return false;
        const auto duration = chapter.value("durationSeconds");
        if (!duration.isDouble() || !std::isfinite(duration.toDouble()) || duration.toDouble() <= 0.0
            || duration.toDouble() > maximumChapterSeconds)
            return false;
    }
    // The first chapter is the video reference itself.
    const auto first = chapters.first().toObject();
    return first.value("relativePath") == video.value("relativePath")
        && first.value("absolutePath") == video.value("absolutePath")
        && first.value("fingerprint") == video.value("fingerprint");
}

QVector<VideoChapterReference> read(const QJsonObject &video)
{
    QVector<VideoChapterReference> chapters;
    if (!valid(video)) return chapters;
    for (const auto &value : video.value("chapters").toArray())
        chapters.append({referenceOf(value.toObject()), value.toObject().value("durationSeconds").toDouble()});
    return chapters;
}

QJsonArray write(const QVector<VideoChapterReference> &chapters,
    const std::function<QJsonObject(const ProjectSourceReference &)> &serialize)
{
    QJsonArray array;
    for (const auto &chapter : chapters) {
        auto object = serialize(chapter.reference);
        object.insert("durationSeconds", chapter.durationSeconds);
        array.append(object);
    }
    return array;
}

} // namespace FlappedEar::VideoChaptersCodec
