#pragma once

#include "telemetry/TelemetrySession.h"

#include <QString>
#include <QVariantMap>

namespace FlappedEar {

// A channel over [start, end] telemetry seconds as chart segments (x
// normalised to the range); gaps stay separate segments. Empty for an empty
// overlap, or {reason} when the range or channel is unusable. Shared by the
// editor's channel charts and the analysis (KAN-166).
[[nodiscard]] QVariantMap channelSeries(const TelemetrySession &session, const QString &channel,
    double start, double end, int maximumPoints);

} // namespace FlappedEar
