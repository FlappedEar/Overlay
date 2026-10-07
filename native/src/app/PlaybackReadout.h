#pragma once

#include "telemetry/TelemetrySession.h"

#include <QString>
#include <QVariantMap>

#include <optional>

namespace FlappedEar {

// KAN-215: what the editor reads from telemetry at a video time: a channel's
// value under the playhead, its text for a readout, and a channel's chart
// series over a video range. Video time maps to telemetry time through the
// run's sync transform; a missing session, an unknown channel, an unmappable
// time or a gap is no value. AppController passes its session, sync and
// playhead.
namespace PlaybackReadout {

[[nodiscard]] std::optional<double> valueAt(const TelemetrySession *session, const SyncTransform &sync,
    double videoTime, const QString &channel);
// The value with 0 to 6 decimals, or an em dash when there is none.
[[nodiscard]] QString valueText(const TelemetrySession *session, const SyncTransform &sync, double videoTime,
    const QString &channel, int decimals);
// channelSeries over [videoStart, videoEnd] in telemetry time; empty when the
// range cannot be mapped or maximumPoints is below 2.
[[nodiscard]] QVariantMap series(const TelemetrySession *session, const SyncTransform &sync,
    const QString &channel, double videoStart, double videoEnd, int maximumPoints);

} // namespace PlaybackReadout

} // namespace FlappedEar
