#pragma once

#include "telemetry/DrivingStates.h"
#include "telemetry/TrackProgress.h"
#include "telemetry/TrackSegmentReview.h"

#include <QString>
#include <QVector>
#include <optional>

namespace FlappedEar {

// KAN-92: where and how long a lap coasts -- time at speed with neither
// pedal active (see DrivingStates) -- per episode, per approved segment and
// for the lap. An observation, not a verdict: coasting is sometimes the
// right thing to do (a lift to settle the car, traffic), so nothing here
// calls it a loss.
inline constexpr auto coastingAlgorithm = "coasting-v1";

struct CoastingEpisode {
    double startTime = 0.0;
    double endTime = 0.0;
    double seconds = 0.0;
    double meters = 0.0;              // speed integrated over the episode
    std::optional<double> startProgressMeters;
    std::optional<double> endProgressMeters;
    QString segmentId;                // the approved segment where it starts
};

struct CoastingSegment {
    QString segmentId;
    QString name;
    QString type;
    double seconds = 0.0;
    double meters = 0.0;
    int episodes = 0;
};

struct CoastingSummary {
    QString algorithm = coastingAlgorithm;
    // "measured" when both pedals are recorded, "inferred" when derived from
    // acceleration, "unknown" when it cannot be told (see unresolvedReason).
    QString provenance = drivingStateUnknown;
    QString unresolvedReason;
    double lapSeconds = 0.0;
    double knownSeconds = 0.0;        // time the pedal states and speed were known
    double coastingSeconds = 0.0;
    double coastingMeters = 0.0;
    QVector<CoastingEpisode> episodes;
    QVector<CoastingSegment> segments; // approved segments in axis order, with zero rows kept
    bool valid = false;
};

// Coasting over [startTime, endTime]. `lapTrace` maps time to the lap's
// progress (episode positions, segment attribution); `approved` gives the
// segments to attribute to. Either may be null: the lap totals remain.
[[nodiscard]] CoastingSummary summarizeCoasting(const TelemetrySession &session, double startTime, double endTime,
    const QVector<ProgressSegment> *lapTrace = nullptr, const ApprovedSegmentation *approved = nullptr,
    const DrivingStateOptions &options = {});

} // namespace FlappedEar
