#pragma once

#include "project/ProjectSourceReference.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QVector>

#include <functional>

namespace FlappedEar {

// KAN-105: a video made of GoPro chapter files is stored as the ordinary
// `sources.video` reference (its first chapter, so a reader that knows only
// one video still opens the right start of the recording) plus
// `sources.video.chapters`: every chapter in timeline order, the first
// included, each a source reference with its probed video duration. The
// durations keep the timeline's time when a chapter file is missing, so a
// missing chapter is a gap of known length, never closed up.
struct VideoChapterReference {
    ProjectSourceReference reference;
    double durationSeconds = 0.0;
};

namespace VideoChaptersCodec {

inline constexpr int minimumChapters = 2;
inline constexpr int maximumChapters = 64;
inline constexpr double maximumChapterSeconds = 86400.0;

// Absent is valid (an ordinary video). Present: 2..64 objects, each a
// reference with a relative or absolute path and a finite duration in
// (0, 86400] s; the first must name the same file as `video` itself.
[[nodiscard]] bool valid(const QJsonObject &video);
[[nodiscard]] QVector<VideoChapterReference> read(const QJsonObject &video);
// Chapter references written with `serialize` (editor or event rules).
[[nodiscard]] QJsonArray write(const QVector<VideoChapterReference> &chapters,
    const std::function<QJsonObject(const ProjectSourceReference &)> &serialize);

} // namespace VideoChaptersCodec
} // namespace FlappedEar
