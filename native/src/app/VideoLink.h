#pragma once

#include <QString>
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
};

} // namespace FlappedEar
