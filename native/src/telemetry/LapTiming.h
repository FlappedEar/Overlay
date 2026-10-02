#pragma once

#include "telemetry/SourceOperation.h"
#include "telemetry/TelemetrySession.h"
#include "telemetry/TimingGate.h"

#include <QVector>
#include <optional>

namespace FlappedEar {

enum class LapSessionStatus {
    Available,
    NoSourceStartGate,
    AmbiguousSourceStartGate,
    InvalidGate,
    NoUsableGps,
    NoAcceptedPasses,
    InsufficientPasses,
};

struct LapDetectionOptions {
    double innerCorridorMeters = 5.0;
    double outerCorridorMeters = 10.0;
    double minimumGroundSpeedMetersPerSecond = 2.0;
    double minimumNormalSpeedMetersPerSecond = 2.0;
    double minimumNormalMotionRatio = 0.10;
    double refractorySeconds = 1.0;
    double maximumClusterSeconds = 5.0;
    double minimumGateLengthMeters = 1.0;
    double maximumGateLengthMeters = 200.0;
    qsizetype maximumAcceptedPasses = 100'000;
};

struct LapDetectionDiagnostics {
    qsizetype usableGpsSegments = 0;
    qsizetype candidateClusters = 0;
    qsizetype discardedGapClusters = 0;
    qsizetype rejectedSlowClusters = 0;
    qsizetype rejectedParallelClusters = 0;
    qsizetype rejectedLongClusters = 0;
    qsizetype rejectedOppositeDirectionClusters = 0;
    qsizetype invalidLapDurations = 0;
};

struct GatePass {
    double telemetryTime = 0.0;
    double closestDistanceMeters = 0.0;
    int direction = 0;
    double gateFraction = 0.0;
    double groundSpeedMetersPerSecond = 0.0;
    double normalSpeedMetersPerSecond = 0.0;
};

enum class LapReferenceIssue { None, GpsGap, InvalidGps };

struct TimedLap {
    int number = 0;
    double startTelemetryTime = 0.0;
    double endTelemetryTime = 0.0;
    double durationSeconds = 0.0;
    double deltaToBestSeconds = 0.0;
    // A measured gate-to-gate interval remains visible even when its GPS
    // coverage cannot support ranking or a spatial reference.
    LapReferenceIssue referenceIssue = LapReferenceIssue::None;
    QString userExclusionReason = {};
    [[nodiscard]] bool referenceEligible() const { return referenceIssue == LapReferenceIssue::None && userExclusionReason.isEmpty(); }
};

struct LapTracePoint {
    double telemetryTime = 0.0;
    double eastMeters = 0.0;
    double northMeters = 0.0;
};

struct LapTrace {
    int lapNumber = 0;
    double startTelemetryTime = 0.0;
    double durationSeconds = 0.0;
    QVector<LapTracePoint> points;
};

struct LapSession {
    LapSessionStatus status = LapSessionStatus::NoSourceStartGate;
    std::optional<TimingGate> selectedStartGate;
    QVector<GatePass> acceptedPasses;
    QVector<TimedLap> timedLaps;
    QVector<LapTrace> lapTraces;
    std::optional<qsizetype> fastestLapIndex;
    LapDetectionDiagnostics diagnostics;
};

// Shared ranking/statistics/potential input policy. Measured laps and traces remain inspectable.
[[nodiscard]] QVector<qsizetype> eligibleLapIndices(const LapSession &session);
void recomputeLapRanking(LapSession &session);

// A lap time as "M:SS" with 0-3 decimals ("1:40.23"). The time is rounded to
// the display precision before minutes are split, so 59.96 s at one decimal
// is "1:00.0", never "0:60.0" (KAN-149). Empty for a negative or non-finite
// time: callers show their own placeholder.
[[nodiscard]] QString formatLapTime(double seconds, int decimals);

// Content revision of the ordered source gates in east-positive coordinates.
// Empty means unresolved (missing/ambiguous start gate or invalid coordinates).
// Gate crossing sign is not a clockwise/counterclockwise layout direction.
[[nodiscard]] QString timingGateRevision(
    const TelemetrySession &session, const CancellationCheck &cancelled = {});

[[nodiscard]] LapSession detectLaps(
    const TelemetrySession &session,
    const TimingGate &startGate,
    const LapDetectionOptions &options = {},
    const CancellationCheck &cancelled = {});
[[nodiscard]] LapSession deriveSourceLapSession(
    const TelemetrySession &session,
    const LapDetectionOptions &options = {},
    const CancellationCheck &cancelled = {});

} // namespace FlappedEar
