#pragma once

#include "telemetry/TrackProgress.h"
#include "telemetry/TrackSegmentReview.h"

#include <QJsonObject>
#include <QString>
#include <QVector>
#include <optional>

namespace FlappedEar {

// Sector times per lap from the approved segmentation (KAN-51). Boundary
// crossings are interpolated on the lap's projection onto the shared progress
// axis; the gate boundaries (progress 0 and the axis length) use the lap's own
// timed start and end. Results carry the approved revision through a
// SegmentationResultStamp tagged with this algorithm.
inline constexpr auto sectorTimingAlgorithm = "sector-timing-v1";

// A complete partition's sector times must sum to the lap time within this.
inline constexpr double sectorSumToleranceSeconds = 0.001;

// Why a sector has no numeric time.
// No longer produced for sector times: since KAN-120 a gate-crossing sector is
// timed within the lap (part after its start + part before its end).
inline constexpr auto sectorCrossesGate = "crossesGate";
inline constexpr auto sectorIncompleteCoverage = "incompleteCoverage"; // GPS/projection gap inside or at a boundary

struct SectorTime {
    QString segmentId;
    QString name;
    QString type;
    double startProgressMeters = 0.0;
    double endProgressMeters = 0.0;
    double lengthMeters = 0.0;
    double coveredMeters = 0.0; // projected coverage of the lap inside the sector
    std::optional<double> startTime;  // telemetry time at the start boundary
    std::optional<double> endTime;    // telemetry time at the end boundary
    std::optional<double> seconds;    // only when the whole sector is covered
    QString unavailableReason;        // set when `seconds` is absent
};

struct LapSectorTimes {
    QJsonObject lapReference;
    SegmentationResultStamp stamp;
    QVector<SectorTime> sectors; // in approved order
    // True when approved segments tile [0, axis length] without gaps or a
    // gate-crossing segment, so their times can be compared with the lap time.
    bool completePartition = false;
    double lapSeconds = 0.0;
    std::optional<double> sumSeconds;             // complete partition, every sector timed
    std::optional<double> partitionErrorSeconds;  // |sum - lap time|
    bool valid = false;
};

// `lapTrace` is the lap's projection on the axis the segments were approved
// on (see docs/telemetry-semantics.md for the axis limitation).
[[nodiscard]] LapSectorTimes computeLapSectorTimes(const ApprovedSegmentation &approved, double axisLengthMeters,
    const QVector<ProgressSegment> &lapTrace, double lapStartTime, double lapEndTime, const QJsonObject &lapReference);

inline constexpr auto sectorTimeDifferentSegmentOrRevision = "differentSegmentOrRevision";
inline constexpr auto sectorTimeSegmentNotFound = "segmentNotFound";

// A minus B for one segment, from two laps' whole-lap results for the same
// approved revision (KAN-55).
struct SectorTimeComparison {
    std::optional<double> secondsDelta;
    QString unavailableReason;
    bool valid = false;
};

[[nodiscard]] SectorTimeComparison compareSectorTimes(
    const LapSectorTimes &a, const LapSectorTimes &b, const QString &segmentId);

// Metres of [fromMeters, toMeters] (from <= to) covered by the lap's projection,
// with the same gate tolerance as sector timing.
[[nodiscard]] double projectedCoverageMeters(
    const QVector<ProgressSegment> &lapTrace, double fromMeters, double toMeters, double axisLengthMeters);

} // namespace FlappedEar
