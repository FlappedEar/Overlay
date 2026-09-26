#pragma once

#include "telemetry/Consistency.h"
#include "telemetry/OutingTheoreticalBest.h"
#include "telemetry/TimeLoss.h"

#include <QJsonObject>
#include <QString>
#include <QVariantMap>
#include <functional>

namespace FlappedEar {

// Resolves a lap reference to its readable name ("Session 3 · LAP 2").
using LapLabel = std::function<QString(const QJsonObject &reference)>;

// Driver-readable text for a theoretical-best unavailability reason.
[[nodiscard]] QString theoreticalBestReasonText(const QString &reason);
// A consistency summary as the published map (median, IQR, count, ...).
[[nodiscard]] QVariantMap consistencySummaryMap(const ConsistencySummary &summary);

// Losses of each session's fastest lap (or every eligible lap) against the
// actual best. Requires a computed result with a timed actual best.
[[nodiscard]] TimeLossRanking rankOutingTimeLosses(const OutingTheoreticalBest &computed, bool allLaps,
    qsizetype maximumResults);

// The published forms read by the screens and the day report. `state` and
// `message` are the worker state ("idle", "loading", "ready", "error",
// "unavailable"); `computed` is only read when the state is "ready".
[[nodiscard]] QVariantMap publishTheoreticalBest(const OutingTheoreticalBest &computed, const QString &state,
    const QString &message, const LapLabel &lapLabel);
[[nodiscard]] QVariantMap publishTimeLossRanking(const OutingTheoreticalBest &computed, const QString &state,
    const QString &message, bool allLaps, const LapLabel &lapLabel);
[[nodiscard]] QVariantMap publishSectorProgression(const OutingTheoreticalBest &computed, const QString &state,
    const QString &message, const QVariantMap &progression, const LapLabel &lapLabel);

} // namespace FlappedEar
