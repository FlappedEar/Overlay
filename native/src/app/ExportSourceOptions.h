#pragma once

#include "export/MediaProbe.h"

#include <QSize>
#include <QString>
#include <QVariantMap>

namespace FlappedEar {

// KAN-215: what the export dialog offers for the opened video: the source
// summary, the sizes and frame rates to pick from, the recommended bitrate, and
// the frame ranges for the whole video, a typed range or a lap. It reads the
// source it is given and keeps no copy, so build one per call.
class ExportSourceOptions final {
public:
    explicit ExportSourceOptions(const MediaInfo &source);

    // Empty when no video is open.
    [[nodiscard]] QVariantMap sourceInfo() const;
    // {"sizes": [{width, height, label}], "rates": [{numerator, denominator, label}]}, source first.
    [[nodiscard]] QVariantMap formatOptions() const;
    [[nodiscard]] qint64 recommendedBitrate(const QSize &size, const MediaRational &rate, const QString &quality) const;
    // The first or last frame of the whole video at that rate, empty if it has none.
    [[nodiscard]] QString fullRangeTimecode(const MediaRational &rate, bool outPoint) const;
    // 0 when the range is not valid for the source.
    [[nodiscard]] double rangeDurationSeconds(
        const MediaRational &rate, const QString &rangeIn, const QString &rangeOut) const;
    // A lap from videoStart to videoEnd (seconds of video) with handleSeconds
    // (0 to 30) either side, clamped to the video; {"valid": false} if it has none.
    [[nodiscard]] QVariantMap lapRange(int lapNumber, double videoStart, double videoEnd,
                                       const MediaRational &rate, int handleSeconds) const;

private:
    [[nodiscard]] MediaRational sourceRate() const;

    const MediaInfo *m_source;
};

} // namespace FlappedEar
