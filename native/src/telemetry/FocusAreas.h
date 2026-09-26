#pragma once

#include "telemetry/DrivingVariability.h"

#include <QJsonObject>
#include <QString>
#include <QVector>

namespace FlappedEar {

// KAN-73: a short list of areas to inspect next, selected from computed
// observations only. Each area states an observation (a measured number with
// its metric and sample count) separately from a hypothesis (what might be
// worth comparing). A hypothesis never claims a cause and never recommends a
// driving change as safe or faster: in particular a braking spread says
// nothing about whether earlier or later braking is better. Each area names
// the pair of laps to compare as its evidence.
inline constexpr auto focusAreasAlgorithm = "focus-areas-v1";

// Thresholds below which an observation is not worth a focus area.
inline constexpr double focusMinimumSectorGapSeconds = 0.05;
inline constexpr double focusMinimumLossSeconds = 0.05;
inline constexpr double focusMinimumBrakingSpreadMeters = 10.0;
inline constexpr double focusMinimumSpeedSpreadFraction = 0.05; // of the median minimum speed

// The best lap against the fastest recorded time through one sector.
struct FocusSectorGap {
    QString segmentId;
    QString name;
    double gapSeconds = 0.0; // best lap's sector time minus the fastest recorded
    QJsonObject bestLap;
    QString bestLapLabel;
    QJsonObject sourceLap;
    QString sourceLapLabel;
};

// One lap's loss through one window against the reference lap.
struct FocusLoss {
    QString segmentId;
    QString name;
    double lossSeconds = 0.0;
    QJsonObject lap;
};

struct FocusCorner {
    QString segmentId;
    QString name;
    QVector<CornerLapObservation> observations;
};

struct FocusInputs {
    QJsonObject referenceLap; // the group's best lap
    QString referenceLabel;
    qsizetype comparedLapCount = 0; // laps the losses were observed on
    QVector<FocusSectorGap> gaps;
    QVector<FocusLoss> losses;
    QVector<FocusCorner> corners;
    QString speedUnit;
};

struct FocusArea {
    QString kind; // "sectorGap" | "repeatedLoss" | "brakingSpread" | "minimumSpeedSpread"
    QString segmentId;
    QString name;
    QString observation; // measured, with numbers
    QString hypothesis;  // what may be worth comparing; never causal
    QString metric;      // e.g. "sectorGapSeconds"
    double value = 0.0;
    QString unit;
    qsizetype sampleCount = 0;
    QJsonObject lap;     // evidence: compare `lap` against `against` through the segment
    QJsonObject against;
    double score = 0.0;  // ordering within one kind
};

// At most `maximum` areas, at most one per segment. The first round takes
// the strongest area of each kind in the order sector gap, repeated loss,
// braking spread, minimum-speed spread; remaining places are filled
// round-robin in the same kind order, each kind by score. Deterministic for
// equal scores (segment id).
[[nodiscard]] QVector<FocusArea> selectFocusAreas(const FocusInputs &inputs, qsizetype maximum = 3);

} // namespace FlappedEar
