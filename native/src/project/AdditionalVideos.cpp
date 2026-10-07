#include "project/AdditionalVideos.h"

#include <QDir>
#include <QSet>

#include <cmath>

namespace FlappedEar::AdditionalVideosCodec {
namespace {

bool validPath(const QJsonObject &object, const QString &key, const bool absolute)
{
    if (!object.contains(key)) return true;
    const auto value = object.value(key);
    if (!value.isString() || value.toString().contains(QChar::Null) || value.toString().size() > 4096) return false;
    const auto path = value.toString();
    return path.isEmpty() || QDir::isAbsolutePath(path) == absolute;
}

bool validText(const QJsonValue &value, const int limit)
{
    return value.isString() && !value.toString().contains(QChar::Null) && value.toString().size() <= limit;
}

bool validSync(const QJsonValue &value)
{
    if (!value.isObject()) return false;
    const auto offset = value.toObject().value("offset");
    const auto scale = value.toObject().value("timeScale");
    return offset.isDouble() && scale.isDouble() && std::isfinite(offset.toDouble())
        && std::isfinite(scale.toDouble()) && scale.toDouble() > 0.0;
}

ProjectSourceReference referenceOf(const QJsonObject &object)
{
    const auto digest = object.value("contentSha256").toString();
    return {object.value("relativePath").toString(), object.value("absolutePath").toString(),
        object.value("fingerprint").toObject(),
        ProjectSourceReferenceCodec::isContentSha256(digest) ? digest : QString()};
}

QString layoutName(const VideoLayoutMode mode)
{
    return mode == VideoLayoutMode::SideBySide ? QStringLiteral("sideBySide") : QStringLiteral("pictureInPicture");
}

} // namespace

bool valid(const QJsonValue &additionalVideos)
{
    if (additionalVideos.isUndefined()) return true;
    if (!additionalVideos.isArray()) return false;
    const auto videos = additionalVideos.toArray();
    if (videos.size() > maximumAdditionalVideos) return false;
    QSet<QString> ids;
    for (const auto &value : videos) {
        if (!value.isObject()) return false;
        const auto video = value.toObject();
        const auto id = video.value("id");
        if (!validText(id, maximumIdCharacters) || id.toString().trimmed().isEmpty() || ids.contains(id.toString()))
            return false;
        ids.insert(id.toString());
        if (video.contains("label") && !validText(video.value("label"), maximumLabelCharacters)) return false;
        if (!validPath(video, "relativePath", false) || !validPath(video, "absolutePath", true)) return false;
        if (video.value("relativePath").toString().trimmed().isEmpty()
            && video.value("absolutePath").toString().trimmed().isEmpty())
            return false;
        if (video.contains("fingerprint") && !video.value("fingerprint").isObject()) return false;
        if (video.contains("contentSha256")
            && !ProjectSourceReferenceCodec::isContentSha256(video.value("contentSha256").toString()))
            return false;
        if (!validSync(video.value("sync"))) return false;
    }
    return true;
}

QVector<AdditionalVideo> read(const QJsonValue &additionalVideos)
{
    QVector<AdditionalVideo> videos;
    if (!valid(additionalVideos)) return videos;
    for (const auto &value : additionalVideos.toArray()) {
        const auto object = value.toObject();
        const auto sync = object.value("sync").toObject();
        videos.append({object.value("id").toString(), object.value("label").toString(), referenceOf(object),
            {sync.value("offset").toDouble(), sync.value("timeScale").toDouble()}});
    }
    return videos;
}

QJsonArray write(const QVector<AdditionalVideo> &videos, const QJsonValue &previous,
    const std::function<QJsonObject(const ProjectSourceReference &)> &serialize)
{
    QHash<QString, QJsonObject> previousById;
    for (const auto &value : previous.toArray()) {
        const auto object = value.toObject();
        previousById.insert(object.value("id").toString(), object);
    }
    QJsonArray array;
    for (const auto &video : videos) {
        QJsonObject object = previousById.value(video.id);
        for (const auto &key : {"relativePath", "absolutePath", "fingerprint", "contentSha256"}) object.remove(key);
        const auto reference = serialize(video.reference);
        for (auto it = reference.begin(); it != reference.end(); ++it) object.insert(it.key(), it.value());
        object.insert("id", video.id);
        if (video.label.isEmpty()) object.remove("label");
        else object.insert("label", video.label);
        QJsonObject sync = object.value("sync").toObject();
        sync.insert("offset", video.sync.offset);
        sync.insert("timeScale", video.sync.timeScale);
        object.insert("sync", sync);
        array.append(object);
    }
    return array;
}

bool validLayout(const QJsonValue &layout)
{
    if (layout.isUndefined()) return true;
    if (!layout.isObject()) return false;
    const auto mode = layout.toObject().value("mode");
    return mode == QJsonValue("pictureInPicture") || mode == QJsonValue("sideBySide");
}

VideoLayoutMode readLayout(const QJsonValue &layout)
{
    return validLayout(layout) && layout.toObject().value("mode") == QJsonValue("sideBySide")
        ? VideoLayoutMode::SideBySide : VideoLayoutMode::PictureInPicture;
}

QJsonObject writeLayout(const VideoLayoutMode mode, const QJsonValue &previous)
{
    QJsonObject object = previous.toObject();
    object.insert("mode", layoutName(mode));
    return object;
}

} // namespace FlappedEar::AdditionalVideosCodec
