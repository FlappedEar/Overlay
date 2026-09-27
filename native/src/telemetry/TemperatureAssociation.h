#pragma once

#include "telemetry/TelemetrySession.h"

#include <QString>
#include <QVector>

#include <optional>

namespace FlappedEar {

// KAN-100: how a recorded temperature moves together with a lap metric (lap
// time, peak acceleration) over a population of comparable laps. The measure
// is Spearman's rank correlation: both series are ranked (ties share their
// average rank) and the Pearson correlation of the ranks is reported, from
// -1 (one rises as the other falls) to +1 (both rise together). It describes
// a monotonic association in the observed laps only; it never establishes a
// critical temperature or a cause.
inline constexpr auto temperatureAssociationAlgorithm = "spearman-rank-v1";
inline constexpr qsizetype minimumAssociationSamples = 8;
// A lap's temperature counts only when the sensor covered most of the lap.
inline constexpr double minimumAssociationCoverage = 0.8;
// Temperature rising (or falling) with the order of laps through the day at
// least this strongly means any association cannot be told apart from
// everything else that changes over a day (the driver, tyres, track, fuel).
inline constexpr double associationOrderConfoundLevel = 0.6;

inline constexpr auto associationTooFewSamples = "tooFewSamples";
inline constexpr auto associationNoSpread = "noSpread";

struct RankCorrelation {
    qsizetype count = 0;              // finite pairs used
    std::optional<double> coefficient; // Spearman's rho
    QString unavailableReason;
};

// Pairs with a non-finite value are skipped. Unavailable below `minimum`
// pairs, or when either series has a single distinct value.
[[nodiscard]] RankCorrelation spearmanCorrelation(const QVector<double> &x, const QVector<double> &y,
    qsizetype minimum = minimumAssociationSamples);

// "weak" below 0.3 in magnitude, "moderate" below 0.6, otherwise "strong".
[[nodiscard]] QString associationStrength(double coefficient);

struct AssociationObservation {
    double temperature = 0.0;
    double value = 0.0;  // the lap metric
    double order = 0.0;  // position in the day (lap start time)
};

struct TemperatureAssociation {
    RankCorrelation withValue;  // temperature vs the metric
    RankCorrelation withOrder;  // temperature vs time of day
    bool confoundedByOrder = false;
};

// A lap's strong acceleration: the 90th percentile of its positive
// longitudinal G samples (the "longitudinalAcceleration" alias, g or
// undeclared units, |G| up to 4). A percentile rather than the peak, so one
// noisy sample does not decide it. None with fewer than 20 positive samples.
inline constexpr qsizetype minimumAccelerationSamples = 20;
struct LapAcceleration {
    std::optional<double> strongG;
    QString channel;
    qsizetype sampleCount = 0;
};
[[nodiscard]] LapAcceleration lapStrongAcceleration(const TelemetrySession &session, double startTime, double endTime);

[[nodiscard]] TemperatureAssociation associateTemperature(const QVector<AssociationObservation> &observations,
    qsizetype minimum = minimumAssociationSamples);

} // namespace FlappedEar
