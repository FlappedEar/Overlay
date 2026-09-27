#pragma once

#include "telemetry/SourceOperation.h"

#include <QDateTime>
#include <QSize>
#include <QString>
#include <QStringList>
#include <functional>
#include <optional>

namespace FlappedEar {

struct MediaRational {
    qint64 numerator = 0;
    qint64 denominator = 1;
    [[nodiscard]] double value() const;
    [[nodiscard]] bool isValid() const;
    [[nodiscard]] bool isEquivalentTo(const MediaRational &other) const;
};

enum class SourceColorClass {
    Unknown,
    Sdr,
    HdrHlg,
    HdrPq,
    LogOrExtended,
};

[[nodiscard]] QString sourceColorClassName(SourceColorClass classification);

struct MediaInfo {
    QString path;
    double duration = 0.0;
    QSize codedVideoSize;
    QSize videoSize;
    QSize displayVideoSize;
    MediaRational frameRate;
    MediaRational averageFrameRate;
    MediaRational timeBase;
    qint64 videoStartTicks = 0;
    qint64 videoDurationTicks = 0;
    QString videoCodec;
    QString videoCodecProfile;
    QString pixelFormat;
    std::optional<int> bitDepth;
    std::optional<qint64> sourceVideoBitrate;
    MediaRational sampleAspectRatio;
    std::optional<int> rotationDegrees;
    QString colorRange;
    QString colorSpace;
    QString colorTransfer;
    QString colorPrimaries;
    QString masteringDisplayMetadata;
    QString contentLightMetadata;
    SourceColorClass sourceColorClass = SourceColorClass::Unknown;
    QStringList audioCodecs;
    qsizetype videoFrameCount = 0;
    qsizetype videoPacketCount = 0;
    double startTime = 0.0;
    double videoStartTime = 0.0;
    double videoDuration = 0.0;
    double audioStartTime = 0.0;
    double audioDuration = 0.0;
    MediaRational audioTimeBase;
    int audioSampleRate = 0;
    bool likelyVariableFrameRate = false;
    // The container's creation_time tag (UTC) when present and parseable;
    // cameras write it, and it is only as correct as the camera's clock.
    QDateTime creationTime;
};

struct MediaProbeEvent {
    enum class Phase { Started, Heartbeat, Finished };
    Phase phase = Phase::Started;
    qint64 elapsedMilliseconds = 0;
    QString executable;
    QStringList arguments;
    QString targetPath;
    QString mode;
    int exitCode = 0;
};

using MediaProbeProgressCallback = std::function<void(const MediaProbeEvent &event)>;
using MediaProbeCancellationCallback = CancellationCheck;

class MediaProbe final {
public:
    [[nodiscard]] static MediaInfo probe(
        const QString &path,
        const QString &ffprobePath = {},
        bool countVideoFrames = false,
        int timeoutMilliseconds = -1,
        const MediaProbeProgressCallback &progressCallback = {},
        const MediaProbeCancellationCallback &cancellationCallback = {},
        bool countVideoPackets = false);
    [[nodiscard]] static MediaInfo probeSummary(
        const QString &path,
        const QString &ffprobePath = {},
        int timeoutMilliseconds = 30'000,
        const MediaProbeProgressCallback &progressCallback = {},
        const MediaProbeCancellationCallback &cancellationCallback = {});
    [[nodiscard]] static MediaInfo parseJson(const QByteArray &json, const QString &path = {});
    [[nodiscard]] static MediaRational parseRational(const QString &value);
    [[nodiscard]] static std::optional<int> bitDepthForPixelFormat(const QString &pixelFormat);
    [[nodiscard]] static SourceColorClass classifyColor(
        const QString &colorTransfer, const QString &colorSpace,
        const QString &colorPrimaries);
};

} // namespace FlappedEar
