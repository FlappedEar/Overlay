#include "app/PlaybackReadout.h"

#include "telemetry/ChannelSeries.h"

#include <QtGlobal>

#include <cmath>

namespace FlappedEar::PlaybackReadout {

std::optional<double> valueAt(const TelemetrySession *session, const SyncTransform &sync, const double videoTime,
    const QString &channel)
{
    if (!session || channel.isEmpty()) return std::nullopt;
    const auto time = videoToTelemetryTime(videoTime, sync);
    if (!time) return std::nullopt;
    return session->valueAt(channel, *time);
}

QString valueText(const TelemetrySession *session, const SyncTransform &sync, const double videoTime,
    const QString &channel, const int decimals)
{
    const auto value = valueAt(session, sync, videoTime, channel);
    return value ? QString::number(*value, 'f', qBound(0, decimals, 6)) : QStringLiteral("—");
}

QVariantMap series(const TelemetrySession *session, const SyncTransform &sync, const QString &channel,
    const double videoStart, const double videoEnd, const int maximumPoints)
{
    if (!session || channel.isEmpty() || !std::isfinite(videoStart) || !std::isfinite(videoEnd)
        || maximumPoints < 2) {
        return {};
    }
    const auto telemetryStart = videoToTelemetryTime(videoStart, sync);
    const auto telemetryEnd = videoToTelemetryTime(videoEnd, sync);
    if (!telemetryStart || !telemetryEnd) return {};
    return channelSeries(*session, channel, *telemetryStart, *telemetryEnd, maximumPoints);
}

} // namespace FlappedEar::PlaybackReadout
