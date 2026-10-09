#include "project/AdditionalVideos.h"

#include <QDir>
#include <QRegularExpression>
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

bool finiteIn(const QJsonValue &value, const double low, const double high)
{
    return value.isDouble() && std::isfinite(value.toDouble()) && value.toDouble() >= low && value.toDouble() <= high;
}

bool validColor(const QJsonValue &value)
{
    if (!value.isString()) return false;
    static const QRegularExpression pattern(QStringLiteral("^#[0-9A-Fa-f]{6}$"));
    return pattern.match(value.toString()).hasMatch();
}

const QStringList &cornerNames()
{
    static const QStringList names{"topRight", "topLeft", "bottomRight", "bottomLeft"};
    return names;
}

bool validPip(const QJsonValue &pip)
{
    if (pip.isUndefined()) return true;
    if (!pip.isObject()) return false;
    const auto object = pip.toObject();
    if (object.contains("enabled") && !object.value("enabled").isBool()) return false;
    if (object.contains("corner")
        && (!object.value("corner").isString() || !cornerNames().contains(object.value("corner").toString())))
        return false;
    if (object.contains("size") && !finiteIn(object.value("size"), minimumPipSize, maximumPipSize)) return false;
    if (object.contains("margin") && !finiteIn(object.value("margin"), 0.0, maximumPipMargin)) return false;
    if (object.contains("borderWidth")) {
        const auto width = object.value("borderWidth");
        if (!finiteIn(width, 0.0, maximumBorderWidth) || width.toDouble() != std::floor(width.toDouble())) return false;
    }
    if (object.contains("borderColor") && !validColor(object.value("borderColor"))) return false;
    if (object.contains("cameras")) {
        const auto cameras = object.value("cameras");
        if (!cameras.isArray() || cameras.toArray().size() > maximumPipCameras) return false;
        for (const auto &camera : cameras.toArray())
            if (!validText(camera, maximumIdCharacters) || camera.toString().trimmed().isEmpty()) return false;
    }
    return true;
}

bool validProgram(const QJsonValue &program)
{
    if (program.isUndefined()) return true;
    if (!program.isObject()) return false;
    const auto object = program.toObject();
    if (object.contains("transition")
        && object.value("transition") != QJsonValue("cut") && object.value("transition") != QJsonValue("crossfade"))
        return false;
    if (object.contains("crossfadeSeconds")
        && !finiteIn(object.value("crossfadeSeconds"), minimumCrossfadeSeconds, maximumCrossfadeSeconds))
        return false;
    if (object.contains("cuts")) {
        const auto cuts = object.value("cuts");
        if (!cuts.isArray() || cuts.toArray().size() > maximumCuts) return false;
        double previous = -1.0;
        for (const auto &value : cuts.toArray()) {
            if (!value.isObject()) return false;
            const auto cut = value.toObject();
            if (!finiteIn(cut.value("time"), 0.0, 1.0e9) || cut.value("time").toDouble() <= previous) return false;
            if (!validText(cut.value("camera"), maximumIdCharacters) || cut.value("camera").toString().trimmed().isEmpty())
                return false;
            previous = cut.value("time").toDouble();
        }
    }
    return true;
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
        if (!validText(id, maximumIdCharacters) || id.toString().trimmed().isEmpty() || ids.contains(id.toString())
            || id.toString() == mainCameraId) // "main" names the main video in cuts (KAN-245)
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
    const auto object = layout.toObject();
    const auto mode = object.value("mode");
    return (mode == QJsonValue("pictureInPicture") || mode == QJsonValue("sideBySide"))
        && validPip(object.value("pip")) && validProgram(object.value("program"));
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

VideoLayout readVideoLayout(const QJsonValue &layout)
{
    VideoLayout result;
    if (!validLayout(layout)) return result;
    const auto object = layout.toObject();
    result.mode = readLayout(layout);
    const auto pip = object.value("pip").toObject();
    result.pip.enabled = pip.value("enabled").toBool(result.pip.enabled);
    if (pip.contains("corner")) {
        const auto index = cornerNames().indexOf(pip.value("corner").toString());
        result.pip.corner = static_cast<PipCorner>(index);
    }
    result.pip.size = pip.value("size").toDouble(result.pip.size);
    result.pip.margin = pip.value("margin").toDouble(result.pip.margin);
    result.pip.borderWidth = pip.value("borderWidth").toInt(result.pip.borderWidth);
    result.pip.borderColor = pip.value("borderColor").toString(result.pip.borderColor).toUpper();
    if (pip.contains("cameras")) {
        QStringList cameras;
        for (const auto &camera : pip.value("cameras").toArray()) cameras.append(camera.toString());
        result.pip.cameras = cameras;
    }
    const auto program = object.value("program").toObject();
    result.program.transition = program.value("transition").toString() == QStringLiteral("crossfade")
        ? ProgramTransition::Crossfade : ProgramTransition::Cut;
    result.program.crossfadeSeconds = program.value("crossfadeSeconds").toDouble(result.program.crossfadeSeconds);
    for (const auto &value : program.value("cuts").toArray()) {
        const auto cut = value.toObject();
        result.program.cuts.append({cut.value("time").toDouble(), cut.value("camera").toString()});
    }
    return result;
}

QJsonObject writeVideoLayout(const VideoLayout &layout, const QJsonValue &previous)
{
    QJsonObject object = writeLayout(layout.mode, previous);
    const PipOptions defaults;
    // Keys this build understands; others belong to a newer version and stay.
    const auto dropKnown = [&object](const char *name, const QStringList &known) {
        QJsonObject sub = object.value(name).toObject();
        for (const QString &key : known) sub.remove(key);
        if (sub.isEmpty()) object.remove(name);
        else object.insert(name, sub);
    };
    if (layout.pip == defaults) {
        dropKnown("pip", {"enabled", "corner", "size", "margin", "borderWidth", "borderColor", "cameras"});
    } else {
        QJsonObject pip = object.value("pip").toObject();
        pip.insert("enabled", layout.pip.enabled);
        pip.insert("corner", cornerNames().value(static_cast<int>(layout.pip.corner)));
        pip.insert("size", layout.pip.size);
        pip.insert("margin", layout.pip.margin);
        pip.insert("borderWidth", layout.pip.borderWidth);
        pip.insert("borderColor", layout.pip.borderColor);
        if (layout.pip.cameras) {
            QJsonArray cameras;
            for (const auto &camera : *layout.pip.cameras) cameras.append(camera);
            pip.insert("cameras", cameras);
        } else {
            pip.remove("cameras");
        }
        object.insert("pip", pip);
    }
    if (layout.program == ProgramOptions{}) {
        dropKnown("program", {"transition", "crossfadeSeconds", "cuts"});
    } else {
        QJsonObject program = object.value("program").toObject();
        // A cut that is still there keeps the keys of its previous object.
        QHash<QString, QJsonObject> previousCuts;
        for (const auto &value : program.value("cuts").toArray()) {
            const auto cut = value.toObject();
            previousCuts.insert(QString::number(cut.value("time").toDouble(), 'g', 17) + QLatin1Char('|') + cut.value("camera").toString(), cut);
        }
        program.insert("transition", layout.program.transition == ProgramTransition::Crossfade
            ? QStringLiteral("crossfade") : QStringLiteral("cut"));
        program.insert("crossfadeSeconds", layout.program.crossfadeSeconds);
        QJsonArray cuts;
        for (const auto &cut : layout.program.cuts) {
            QJsonObject entry = previousCuts.value(QString::number(cut.time, 'g', 17) + QLatin1Char('|') + cut.camera);
            entry.insert("time", cut.time);
            entry.insert("camera", cut.camera);
            cuts.append(entry);
        }
        program.insert("cuts", cuts);
        object.insert("program", program);
    }
    return object;
}

} // namespace FlappedEar::AdditionalVideosCodec
