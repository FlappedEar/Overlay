#pragma once

#include "telemetry/TelemetrySession.h"

#include <QString>
#include <QVector>

namespace FlappedEar {

// Longitudinal/lateral acceleration pairs for a G-G diagram (KAN-65).
//
// Clock: the longitudinal channel's own samples are the clock. The lateral
// value at each of those times is the lateral sample at the same time when
// the two channels share a clock, otherwise a linear interpolation between
// the two lateral samples around it -- but only when those samples are no
// further apart than the lateral channel's gap threshold. Gaps are never
// bridged; a longitudinal sample without a lateral value yields no point.
//
// Signs (as recorded by RaceChrono, verified on the owner's recordings):
// longitudinal + accelerating, - braking; lateral + toward the left.
// Units: declared "g" is used as is, "m/s2" / "m/s^2" / "m/s²" is converted
// (1 g = 9.80665 m/s²), an undeclared unit is kept and reported, any other
// unit makes the pairs unavailable rather than guessing.
// Outliers: a value beyond the plausible limit for a road car is excluded and
// counted, never clipped.
inline constexpr auto ggPairsAlgorithm = "gg-pairs-v1";
inline constexpr double ggPlausibleLimitG = 4.0;
inline constexpr double standardGravity = 9.80665;

inline constexpr auto ggMissingLongitudinal = "missingLongitudinalAcceleration";
inline constexpr auto ggMissingLateral = "missingLateralAcceleration";
inline constexpr auto ggUnsupportedUnit = "unsupportedUnit";
inline constexpr auto ggNoOverlap = "noOverlappingSamples";

struct GgPoint {
    double time = 0.0;
    double longitudinalG = 0.0;
    double lateralG = 0.0;
};

struct GgPairs {
    QVector<GgPoint> points;
    QString longitudinalChannel;
    QString lateralChannel;
    QString longitudinalUnit; // as declared by the recording ("" = undeclared)
    QString lateralUnit;
    bool unitsDeclared = false;
    bool sharedClock = false;          // lateral samples at exactly the longitudinal times
    double maximumPairingOffsetSeconds = 0.0; // largest time to the nearest lateral sample used
    qsizetype candidateCount = 0;      // finite longitudinal samples in range
    qsizetype skippedForGap = 0;       // no lateral value without bridging a gap
    qsizetype excludedOutliers = 0;
    QString unavailableReason;
    bool valid = false;
};

[[nodiscard]] GgPairs buildGgPairs(const TelemetrySession &session, double startTime, double endTime);

} // namespace FlappedEar
