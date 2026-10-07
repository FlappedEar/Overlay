#pragma once

#include "export/MediaProbe.h"
#include "export/ExportMediaProfile.h"
#include "export/VideoComposition.h"

#include <QRect>
#include <QSize>
#include <QVariantMap>
#include <QString>
#include <functional>
#include <optional>

namespace FlappedEar {

class TelemetryFrameRenderer;

struct ExportFrameRange {
    qint64 firstFrame = 0;
    qint64 lastFrame = -1;

    [[nodiscard]] bool isValid() const { return firstFrame >= 0 && lastFrame >= firstFrame; }
    [[nodiscard]] qint64 frameCount() const { return isValid() ? lastFrame - firstFrame + 1 : 0; }
};

struct ExportObservation {
    QString type = QStringLiteral("status");
    QString state;
    QString operation;
    QString message;
    QString component = QStringLiteral("export");
    QVariantMap details;
};

struct ExportPipelineProgress {
    qsizetype generatedFrames = 0;
    qsizetype submittedFrames = 0;
    qsizetype expectedFrames = 0;
    double sourceRangeStart = 0.0;
    double sourceRangeEnd = 0.0;
    double exportDuration = 0.0;
    double exportRelativeTime = 0.0;
    double sourceVideoTime = 0.0;
    qsizetype encodedFrames = 0;
    double encodedSeconds = 0.0;
    double outputDurationSeconds = 0.0;
    qint64 queuedBytes = 0;
    qint64 maximumQueuedBytes = 0;
    qint64 temporaryOverlayBytes = 0;
    double encoderFps = 0.0;
    double encoderRealtimeFactor = 0.0;
    QString stage = QStringLiteral("rendering");
};

// KAN-131: a video placed with the main one, such as a helmet camera.
struct ExportAdditionalVideo {
    QString path;
    QString label;
    SyncTransform sync;
    MediaInfo info;
};

struct ExportSettings {
    QString inputPath;
    // KAN-106: every chapter of a chaptered recording, inputPath first; the
    // chapters are exported as one source. Empty for one video file.
    QStringList chapterPaths;
    // Each chapter's video duration_ts as the editor probed it.
    QVector<qint64> chapterDurationTicks;
    // The main video's sync and the additional videos (KAN-131).
    SyncTransform sync;
    QVector<ExportAdditionalVideo> additionalVideos;
    VideoLayoutMode videoLayout = VideoLayoutMode::PictureInPicture;
    QString outputPath;
    QSize outputSize;
    MediaRational frameRate;
    ExportFrameRange frameRange;
    QString encoder;
    qint64 videoBitrate = 0;
    qint64 audioBitrate = 192'000;
    bool audioEnabled = true;
    QString cancellationFilePath;
    // Also polled for cancellation, for example a vanished parent (KAN-156).
    std::function<bool()> cancelled;
    QString temporaryOverlayPath;
    QString manifestPath;
    std::function<void(const QString &state)> stateCallback;
    std::function<void(const ExportPipelineProgress &progress)> progressCallback;
    std::function<void(const QString &id, const QString &displayName)> encoderCallback;
    std::function<void(const ExportObservation &observation)> observationCallback;
};

struct ExportResult {
    bool success = false;
    QString error;
    QString validationWarning;
    MediaInfo mediaInfo;
    ExportMediaProfile mediaProfile;
    MediaRational exportFrameRate;
    qint64 firstFrame = 0;
    qint64 lastFrame = -1;
    qint64 sourceFrameCount = 0;
    qint64 finalFrameCount = 0;
    qint64 frameDeficit = 0;
    qsizetype expectedFrames = 0;
    qsizetype generatedFrames = 0;
    qsizetype renderedFrames = 0;
    qint64 elapsedMilliseconds = 0;
    qint64 renderMilliseconds = 0;
    qint64 renderNanoseconds = 0;
    qint64 polishNanoseconds = 0;
    qint64 syncRenderNanoseconds = 0;
    qint64 readbackNanoseconds = 0;
    qint64 cpuCopyNanoseconds = 0;
    qint64 ffmpegWriteNanoseconds = 0;
    qint64 maximumQueuedBytes = 0;
    qint64 temporaryOverlayBytes = 0;
    qint64 outputBytes = 0;
    qsizetype encodedFrames = 0;
    double encodedSeconds = 0.0;
    QString diagnostics;
    bool cancelled = false;
};

// Stage B uses FFmpeg input seeking to avoid decoding an entire source prefix.
// The requested range is expressed on the source's original FFmpeg timeline;
// -copyts and -seek_timestamp keep seek and trim in that same original domain.
struct StageBSourceAccess {
    QString inputSeekTimestamp;
    QString trimStartTimestamp;
    QString trimEndTimestamp;
};

// KAN-131: the additional videos in Stage B, FFmpeg inputs 2, 3, ... in
// order. Each is shown in its rectangle from its first frame inside the
// export to its last; outside that the main video shows through.
struct StageBComposition {
    // Empty: the main video fills the output, as without additional videos.
    QRect mainRect;
    struct Input {
        QRect rect;
        double timeFactor = 1.0;
        double timeShift = 0.0;
    };
    QVector<Input> additional;
};

class ExportEngine final {
public:
    [[nodiscard]] static MediaRational effectiveFrameRate(
        const MediaInfo &source, const MediaRational &requested = {});
    [[nodiscard]] static ExportResult exportVideo(
        const ExportSettings &settings, TelemetryFrameRenderer &renderer);
    // Final validation reads every packet: its timeout grows with the file
    // (30 s plus 1 s per 15 MB, at most 2 hours), so a long export to a slow
    // disk is not failed after the whole encode (KAN-148).
    [[nodiscard]] static int finalValidationTimeoutMilliseconds(qint64 outputBytes);
    [[nodiscard]] static std::optional<ExportFrameRange> frameRangeFromInclusiveFrames(
        qint64 firstFrame, qint64 lastFrame);
    [[nodiscard]] static std::optional<ExportFrameRange> fullVideoFrameRange(
        const MediaInfo &source, const MediaRational &exportFrameRate);
    [[nodiscard]] static QString formatSmpteTimecode(
        qint64 frame, const MediaRational &frameRate);
    [[nodiscard]] static std::optional<qint64> parseSmpteTimecode(
        const QString &timecode, const MediaRational &frameRate);
    [[nodiscard]] static std::optional<ExportFrameRange> frameRangeForSourceTimecode(
        const MediaInfo &source, const MediaRational &exportFrameRate,
        const QString &inTimecode, const QString &outTimecode);
    [[nodiscard]] static double audioDurationForRange(
        const MediaInfo &source, double sourceRangeStart, double sourceRangeEnd);
    [[nodiscard]] static double exportRelativeTime(
        qsizetype frameIndex, const MediaRational &frameRate);
    [[nodiscard]] static double outputDuration(
        qsizetype frameCount, const MediaRational &frameRate);
    [[nodiscard]] static double sourceVideoTime(
        double sourceRangeStart, qsizetype frameIndex, const MediaRational &frameRate);
    [[nodiscard]] static double framePresentationTime(
        double startTime, qsizetype frameIndex, const MediaRational &frameRate);
    [[nodiscard]] static std::optional<StageBSourceAccess> stageBSourceAccess(
        const MediaInfo &source, const ExportFrameRange &range,
        const MediaRational &frameRate, qint64 prerollSeconds = 5);
    [[nodiscard]] static double audioStartForRange(const MediaInfo &source, double start, double end);
    [[nodiscard]] static QString stageBAudioFilterGraph(const StageBSourceAccess &access, bool chaptered = false);
    [[nodiscard]] static QStringList stageBInputArguments(const StageBSourceAccess &access, const QString &path);
    // An additional video's input, read from its timing's seek point with
    // its own timestamps kept.
    [[nodiscard]] static QStringList stageBAdditionalInputArguments(
        const VideoComposition::Timing &timing, const QString &path);
    // Empty on success; runs the actual composition graph before rendering
    // telemetry, with `additionalInputs` stand-ins for the additional videos.
    [[nodiscard]] static QString verifyCompositionFilters(
        const QString &program, const QString &graph, const std::function<bool()> &cancelled = {},
        int additionalInputs = 0);
    [[nodiscard]] static QString stageBVideoFilterGraph(
        const StageBSourceAccess &sourceAccess,
        const QSize &sourceSize,
        const QSize &outputSize,
        const MediaRational &frameRate,
        qsizetype expectedFrames,
        const ExportMediaProfile &mediaProfile,
        const StageBComposition &composition = {});
};

} // namespace FlappedEar
