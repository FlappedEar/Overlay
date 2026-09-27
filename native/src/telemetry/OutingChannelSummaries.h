#pragma once

#include "telemetry/ChannelSummary.h"
#include "telemetry/OutingLaps.h"

#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>
#include <atomic>
#include <memory>

namespace FlappedEar {

// A ChannelSummary as the map the screens and the day report read.
[[nodiscard]] QVariantMap channelSummaryMap(const ChannelSummary &summary);

struct OutingChannelSummariesResult {
    QString error;
    QVariantList runs;
};

// Per run (in first-seen order of `rows`): every recorded temperature channel
// with its whole-recording summary, one summary per recorded section, a
// bounded gap-aware trend trace and the continuously recorded cooling
// intervals; heart rate from the recording's own channel; and each section's
// strong acceleration for KAN-100 ("laps", section order). Each run's
// recording is decoded and verified once (loadOutingLapDetail); a run whose
// recording is unavailable says why. Cooperatively cancellable.
[[nodiscard]] OutingChannelSummariesResult summarizeOutingChannels(const QVector<OutingLapRow> &rows,
    const QHash<QString, QJsonObject> &sourcesByRunId, const QString &projectPath, quint64 request,
    const std::shared_ptr<std::atomic_bool> &cancellation);

} // namespace FlappedEar
