#pragma once

#include "telemetry/DrivingVariability.h"
#include "telemetry/OutingLaps.h"
#include "telemetry/TheoreticalBest.h"
#include "telemetry/TimeLoss.h"
#include "telemetry/TrackProgress.h"

#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QVector>
#include <atomic>
#include <memory>
#include <optional>

namespace FlappedEar {

// The day's theoretical best and what is measured on its shared axis.
struct OutingTheoreticalBest {
    QString error;
    TheoreticalBestLap best;
    // KAN-57: the group's actual best lap timed on the same axis, so
    // per-sector losses compare like with like.
    std::optional<LapSectorTimes> actualBest;
    QString canonicalRunId;
    // KAN-60: every eligible lap timed on the canonical axis, kept for the
    // time-loss ranking against the actual best.
    QVector<TimedLapSectors> population;
    ApprovedSegmentation approved;
    double axisLengthMeters = 0.0;
    ProgressAxis axis; // KAN-120: drawn as the track map
    // KAN-63: each lap's corner metrics, by corner segment id.
    QHash<QString, QVector<CornerLapObservation>> cornerObservations;
};

// Times every lap of `population` on one axis built from the canonical run's
// fastest lap, against `approved` (the canonical run's approved segments):
// sector times, the theoretical best, the actual best on the same axis, and
// each corner's speeds, braking point, pickup and line offset. Recordings
// are loaded and verified with loadOutingLapDetail, one run at a time. A lap
// whose recording cannot be decoded contributes nothing. Never throws:
// failures and cancellation are reported in `error`.
[[nodiscard]] OutingTheoreticalBest calculateOutingTheoreticalBest(QVector<OutingLapRow> population,
    const QHash<QString, QJsonObject> &sourcesByRunId, const QString &projectPath, const ApprovedSegmentation &approved,
    const QString &canonicalRunId, const QJsonObject &actualBestReference, quint64 request,
    const std::shared_ptr<std::atomic_bool> &cancellation);

} // namespace FlappedEar
