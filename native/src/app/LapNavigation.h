#pragma once

#include "app/PreviewTimeline.h"
#include "telemetry/LapTiming.h"

#include <QString>
#include <QVariantList>

#include <functional>
#include <optional>

namespace FlappedEar {

// KAN-215: how the editor presents the opened run's laps: the lap timing
// status line, the lap list, the lap bar under the preview (out lap, laps, in
// lap in video time) and the lap under the playhead. Pure functions of the lap
// session; AppController supplies the run, the preview timeline and the
// telemetry-to-video mapping.
namespace LapNavigation {

[[nodiscard]] QString timingStatus(const LapSession &laps, bool telemetryOpen);
[[nodiscard]] QVariantList summaries(const LapSession &laps, const QString &runId);
// videoMilliseconds maps a telemetry time to a preview position, or -1 when it
// lies outside the video.
[[nodiscard]] QVariantList segments(const LapSession &laps, const PreviewTimeline &timeline,
                                    const std::function<qint64(double)> &videoMilliseconds);
// The timed lap containing that telemetry time, or 0.
[[nodiscard]] int lapNumberAt(const LapSession &laps, std::optional<double> telemetryTime);

} // namespace LapNavigation

} // namespace FlappedEar
