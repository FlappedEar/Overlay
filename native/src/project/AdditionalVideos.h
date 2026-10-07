#pragma once

#include "project/ProjectSourceReference.h"
#include "telemetry/TelemetrySession.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QVector>

#include <functional>

namespace FlappedEar {

// KAN-131: videos besides the run's main one, such as a helmet camera without
// GPS. They are stored in `sources.additionalVideos`, so a reader that knows
// only `sources.video` still opens the main recording. Each is a source
// reference (the same path, fingerprint and contentSha256 rules as the main
// video) with its own `sync`: telemetry time = video time x timeScale +
// offset, as for the run's `sync`. Chapters are not supported here.
struct AdditionalVideo {
    QString id;
    QString label;
    ProjectSourceReference reference;
    SyncTransform sync;
};

// How export puts the additional videos beside the main one.
enum class VideoLayoutMode { PictureInPicture, SideBySide };

namespace AdditionalVideosCodec {

inline constexpr int maximumAdditionalVideos = 3;
inline constexpr int maximumIdCharacters = 64;
inline constexpr int maximumLabelCharacters = 128;

// Absent is valid (no additional video). Present: an array of at most three
// objects, each with a unique non-empty id, an optional label, a reference
// with a relative or absolute path, and a finite sync with timeScale > 0.
// Unknown keys are kept by every writer.
[[nodiscard]] bool valid(const QJsonValue &additionalVideos);
[[nodiscard]] QVector<AdditionalVideo> read(const QJsonValue &additionalVideos);
// Each video is written over its previous object (by id), so keys this build
// does not know survive; `serialize` writes the reference by editor or event
// rules.
[[nodiscard]] QJsonArray write(const QVector<AdditionalVideo> &videos, const QJsonValue &previous,
    const std::function<QJsonObject(const ProjectSourceReference &)> &serialize);

// `videoLayout` is absent (picture in picture) or {"mode": "pictureInPicture"
// | "sideBySide"}.
[[nodiscard]] bool validLayout(const QJsonValue &layout);
[[nodiscard]] VideoLayoutMode readLayout(const QJsonValue &layout);
[[nodiscard]] QJsonObject writeLayout(VideoLayoutMode mode, const QJsonValue &previous);

} // namespace AdditionalVideosCodec
} // namespace FlappedEar
