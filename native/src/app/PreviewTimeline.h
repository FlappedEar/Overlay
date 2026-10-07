#pragma once

#include "export/ExportEngine.h"
#include "export/MediaProbe.h"

#include <QRect>
#include <QSize>
#include <QString>

#include <optional>

namespace FlappedEar {

// KAN-215: the editor preview's timeline for the opened video: where playback
// starts and ends, how a seek is clamped, and the SMPTE timecode shown for a
// position. A chaptered recording (KAN-105) is one timeline of its whole
// duration, framed at the first chapter's rate. It reads the source it is given
// and keeps no copy, so build one per call.
class PreviewTimeline final {
public:
    // chapteredDurationSeconds: the whole recording's duration when the video
    // is chaptered; nullopt for a single file.
    PreviewTimeline(const MediaInfo &source, std::optional<double> chapteredDurationSeconds);

    [[nodiscard]] MediaRational frameRate() const;
    [[nodiscard]] QRect viewport(const QSize &available) const;
    [[nodiscard]] qint64 endPositionMilliseconds() const;
    [[nodiscard]] qint64 initialPositionMilliseconds() const;
    [[nodiscard]] qint64 clampPositionMilliseconds(qint64 requestedMilliseconds) const;
    [[nodiscard]] QString timecodeForPositionMilliseconds(qint64 positionMilliseconds) const;
    [[nodiscard]] QString endTimecode() const;

private:
    [[nodiscard]] std::optional<qint64> chapteredLastFrame() const;
    [[nodiscard]] std::optional<ExportFrameRange> frameRange() const;

    const MediaInfo *m_source;
    std::optional<double> m_chapteredDurationSeconds;
};

} // namespace FlappedEar
