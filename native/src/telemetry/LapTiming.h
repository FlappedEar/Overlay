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
    // Lap plausibility (KAN-225, as Telemetry FET-199). A lap outside these
    // limits stays listed but is never ranked, used for statistics or as a
    // reference. minimumLapSeconds is a floor only: the distance and speed
    // limits already rule out laps under 2 s, and minimumLapDistanceRatio of
    // the recording's median lap path is what catches a fake lap among real ones.
    double minimumLapSeconds = 3.0;
    double maximumLapSeconds = 3'600.0;
    double minimumLapDistanceMeters = 200.0;
    double maximumAverageSpeedMetersPerSecond = 100.0;
    double minimumLapDistanceRatio = 0.8;
};

struct LapDetectionDiagnostics {
    qsizetype usableGpsSegments = 0;
    qsizetype candidateClusters = 0;
    qsizetype discardedGapClusters = 0;
    qsizetype rejectedSlowClusters = 0;
    qsizetype rejectedParallelClusters = 0;
    qsizetype rejectedLongClusters = 0;
    // Passes that came near the line without crossing it (KAN-205).
    qsizetype rejectedNotCrossingClusters = 0;
    qsizetype rejectedOppositeDirectionClusters = 0;
    qsizetype invalidLapDurations = 0;
    // Laps kept visible but not ranked because their time or length is not
    // plausible (KAN-225).
    qsizetype implausibleLaps = 0;
};

struct GatePass {
    double telemetryTime = 0.0;
    double closestDistanceMeters = 0.0;
    int direction = 0;
    double gateFraction = 0.0;
    double groundSpeedMetersPerSecond = 0.0;
    double normalSpeedMetersPerSecond = 0.0;
};

// ImplausibleLap: its time, GPS path length or average speed cannot be a lap
// of the circuit (KAN-225); see LapDetectionOptions.
enum class LapReferenceIssue { None, GpsGap, InvalidGps, ImplausibleLap };

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
    // Length of the lap's GPS path, from the fix at or before its start to the
    // first at or after its end; empty when its GPS is not complete.
    std::optional<double> distanceMeters = {};
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
// A signed gap such as "+0.34" or "-1.20" (KAN-255); 1 to 3 decimals, empty if not finite.
[[nodiscard]] QString formatLapDelta(double seconds, int decimals);

// Content revision of the ordered source gates in east-positive coordinates.
// Empty means unresolved (missing/ambiguous start gate or invalid coordinates).
// Gate crossing sign is not a clockwise/counterclockwise layout direction.
[[nodiscard]] QString timingGateRevision(
    const TelemetrySession &session, const CancellationCheck &cancelled = {});

// A pass is accepted only when it really crosses the gate line: it starts
// strictly on one side, ends on the line or the other side, and reaches the line
// within the gate span widened by innerCorridorMeters at each end. Coming close
// and leaving on the same side is not a pass (KAN-205, Telemetry FET-198).
[[nodiscard]] LapSession detectLaps(
    const TelemetrySession &session,
    const TimingGate &startGate,
    const LapDetectionOptions &options = {},
    const CancellationCheck &cancelled = {});
[[nodiscard]] LapSession deriveSourceLapSession(
    const TelemetrySession &session,
    const LapDetectionOptions &options = {},
    const CancellationCheck &cancelled = {});

// Lap and split durations as the editor and analysis show them: "s.mmm s"
// under a minute, otherwise "m:ss.mmm"; an em dash when not finite.
[[nodiscard]] QString formatElapsedTime(double seconds);

} // namespace FlappedEar
