#pragma once

#include "project/AdditionalVideos.h"
#include "telemetry/TelemetrySession.h"

#include <QRect>
#include <QSize>
#include <QVector>

#include <optional>

namespace FlappedEar {

// KAN-131: where export and preview put the run's additional videos, and
// which of their moments each output frame shows. Both use these rules, so
// the preview matches the exported file.
namespace VideoComposition {

// Picture in picture: each additional video fits a box of this share of the
// output's width and height, stacked down the right edge from the top.
inline constexpr double pictureInPictureShare = 0.28;
inline constexpr double marginShare = 0.03;

// The rectangle of each video in an output frame: index 0 is the main video,
// then each additional video in order. `sourceSizes` are the videos' display
// sizes, main first. Each video keeps its aspect ratio, except the main video
// in picture in picture, which fills the frame as it does without additional
// videos. Every position and size is even, as 4:2:0 output needs. Empty when
// a size is not valid.
[[nodiscard]] QVector<QRect> layout(VideoLayoutMode mode, const QSize &output, const QVector<QSize> &sourceSizes);

// Which moment of an additional video an output frame shows. Both videos'
// syncs map their video time to the same telemetry time, so the additional
// video's time is linear in the main video's.
struct Timing {
    // Where FFmpeg starts reading the additional video, on its own timestamps
    // (its start time included), one second before the first frame needed.
    double inputSeekSeconds = 0.0;
    // Output time = input timestamp x timeFactor + timeShift, in seconds;
    // output time 0 is the export's first frame.
    double timeFactor = 1.0;
    double timeShift = 0.0;
};

// `mainStartSeconds` is the export's first frame on the main video's time,
// `exportSeconds` its duration. `videoStartSeconds` and `videoSeconds` are
// the additional video's first timestamp and duration. Empty when the
// additional video has no frame inside the export, or a value is not finite.
[[nodiscard]] std::optional<Timing> timing(const SyncTransform &mainSync, const SyncTransform &videoSync,
    double mainStartSeconds, double exportSeconds, double videoStartSeconds, double videoSeconds);

} // namespace VideoComposition
} // namespace FlappedEar
