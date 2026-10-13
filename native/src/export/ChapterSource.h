#pragma once

#include "export/MediaProbe.h"

#include <QStringList>
#include <QVector>

#include <optional>

namespace FlappedEar {

// KAN-106: a recording split into chapter files, exported as one source.
// FFmpeg reads the chapters through its concat demuxer, given as a data: URL
// so that the export writes no extra file. Each chapter is cut to its video
// stream ([start, start + duration)) and starts where the previous chapter's
// video ended, which is MediaTimeline's chapter timeline (KAN-105), so
// telemetry time and synchronization carry across every file boundary.
//
// The concat demuxer's own seek skips a first chapter's first keyframe when
// its decode timestamp is negative (any B-frame stream), losing frames. So
// the export never seeks the concat input: the script starts at the chapter
// holding the seek point, with an inpoint there, and -itsoffset puts its
// timestamps back on the chapter timeline.
struct ChapterSourceCombination {
    std::optional<MediaInfo> info; // the chapters as one source starting at zero
    QString error;                 // why the chapters cannot be exported together
};

class ChapterSource final {
public:
    // Chapters must share codec, profile, raster, frame rate, time base,
    // pixel format, colour, orientation and audio layout, and each must have
    // an exact video duration (duration_ts).
    [[nodiscard]] static ChapterSourceCombination combine(const QVector<MediaInfo> &chapters);
    // KAN-265: clips with recorded gaps between them, possibly of different size,
    // frame rate or audio (a BetterDriver series). They cannot go through the
    // concat demuxer; the export normalises each clip to the first one's picture
    // and joins them in a filter graph, with black and silence over each gap.
    // gapsBefore[i] is the recorded time without video before clip i, in seconds.
    // The combined source starts at zero and lasts every clip and every gap.
    [[nodiscard]] static ChapterSourceCombination combineSeries(const QVector<MediaInfo> &clips,
                                                                const QVector<double> &gapsBefore);
    struct SeriesGraph {
        QString video; // ends with ';'; produces [seriesV]; empty when it cannot be built
        QString audio; // ends with ';'; produces [seriesA]
    };
    // FFmpeg input 0 is the first clip; clip k >= 1 is input firstExtraInput + k - 1.
    [[nodiscard]] static SeriesGraph seriesGraph(const QVector<MediaInfo> &clips, const QVector<double> &gapsBefore,
                                                 const QString &frameRate, int firstExtraInput);
    // The ffconcat script from seekTicks on the chapter timeline (in the
    // chapters' time base) to the end: one entry per chapter, cut to its
    // video stream. Empty when the timing cannot be written exactly.
    [[nodiscard]] static QString concatScript(const QVector<MediaInfo> &chapters, qint64 seekTicks = 0);
    // The script as a data: URL.
    [[nodiscard]] static QString inputUrl(const QVector<MediaInfo> &chapters, qint64 seekTicks = 0);
    // FFmpeg input options reading the chapters from seekSeconds (a decimal,
    // rounded down to a tick) with timestamps on the chapter timeline, as
    // -copyts with -ss gives for one file. Empty when not expressible.
    [[nodiscard]] static QStringList inputArguments(const QVector<MediaInfo> &chapters, const QString &seekSeconds);

    struct Probed {
        MediaInfo info;               // combined
        QVector<MediaInfo> chapters;  // each chapter's own probe
    };
    // Probes every chapter and combines them; throws std::runtime_error with
    // the reason when they cannot be exported together, or when a chapter's
    // video duration differs from expectedDurationTicks (the editor's probe),
    // so a chapter replaced after loading is never exported.
    [[nodiscard]] static Probed probe(const QStringList &paths,
                                      const QVector<qint64> &expectedDurationTicks,
                                      const MediaProbeProgressCallback &progressCallback = {},
                                      const MediaProbeCancellationCallback &cancellationCallback = {},
                                      const QVector<double> &gapsBefore = {}); // non-empty: a series (KAN-265)
};

} // namespace FlappedEar
