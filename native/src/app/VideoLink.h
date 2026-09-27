#pragma once

#include <QString>
#include <QVector>
#include <QtGlobal>
#include <optional>

namespace FlappedEar {

// KAN-124: the analysis side's only view of video. Flapped Ear Telemetry has
// no video and no link; Overlays implements it with the loaded run's synced
// footage. Analysis code (the KAN-39 lap video, opening a comparison lap at a
// position) asks through this interface and never reads video, sync or
// preview state directly.
class VideoLink {
public:
    virtual ~VideoLink() = default;
    // Video position (ms) showing `telemetrySeconds` of run `runId`; nullopt
    // when that run has no loaded video or the time falls outside the footage
    // (never clamped into a misleading nearby frame).
    [[nodiscard]] virtual std::optional<qint64> videoPositionForTelemetry(const QString &runId, double telemetrySeconds) const = 0;
    // Telemetry time of run `runId` shown at `videoMilliseconds`; nullopt when
    // the run is not the one the video belongs to.
    [[nodiscard]] virtual std::optional<double> telemetryForVideoPosition(const QString &runId, qint64 videoMilliseconds) const = 0;

    // KAN-107: any run's own footage, for side-by-side A/B video. The active
    // run's is the loaded video; another run's saved video (and chapters) is
    // resolved and each file's fingerprint verified in the background before
    // it is ever shown -- a matching path alone is not enough.
    struct RunVideoChapter {
        QString path;             // empty for a gap
        double startSeconds = 0.0;
        double durationSeconds = 0.0;
        bool available = false;
    };
    struct RunVideo {
        // none (the run has no video), verifying, ready, missing, mismatch, error
        QString state = QStringLiteral("none");
        QString message;
        QVector<RunVideoChapter> chapters;  // timeline order; one entry for a single video
        double offset = 0.0;                 // the run's sync: telemetry = video * timeScale + offset
        double timeScale = 1.0;
    };
    [[nodiscard]] virtual RunVideo runVideo(const QString &runId) const = 0;
    // Starts verifying the run's saved video if that has not been done for
    // its current references; the analysis is told when it finishes.
    virtual void requestRunVideo(const QString &runId) = 0;
};

} // namespace FlappedEar
