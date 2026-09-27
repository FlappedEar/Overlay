#pragma once

#include "telemetry/BrakingOnset.h"
#include "telemetry/TelemetrySession.h"

#include <QString>
#include <QVector>

namespace FlappedEar {

// KAN-91: when a lap is braking, accelerating, cornering or coasting, with
// the provenance of each. States may overlap where driving does: cornering
// with braking (trail braking), with accelerating or with coasting; braking
// with accelerating only when both pedals are measured (a lift-and-brake or
// left-foot overlap cannot be seen in one acceleration channel). Coasting is
// the time at speed when both pedal states are known to be off.
inline constexpr auto drivingStatesAlgorithm = "driving-states-v1";

inline constexpr auto drivingStateMeasured = "measured";      // a recorded driver input or sensor
inline constexpr auto drivingStateCalculated = "calculated";  // recorded by the logger from GPS ("-calc")
inline constexpr auto drivingStateInferred = "inferred";      // derived from acceleration, never a pedal
inline constexpr auto drivingStateUnknown = "unknown";

struct DrivingStateOptions {
    BrakingThreshold measuredBrake{10.0, 5.0, "%"};
    BrakingThreshold measuredThrottle{15.0, 8.0, "%"};
    BrakingThreshold inferredBraking{0.15, 0.08, "g"};       // on negative longitudinal G
    BrakingThreshold inferredAcceleration{0.10, 0.05, "g"};  // on positive longitudinal G
    BrakingThreshold cornering{0.30, 0.20, "g"};             // on |lateral G|
    double minimumSpeedKmh = 10.0;      // below it the car is not coasting (pit lane, stop)
    double minimumDurationSeconds = 0.2; // shorter episodes are spikes
    bool allowInferred = true;
};

struct DrivingStateInterval {
    double start = 0.0;
    double end = 0.0;
};

// One state over the window: where it holds, where it is known (the
// channel had data), and how it was obtained.
struct DrivingStateTrack {
    QString provenance = drivingStateUnknown;
    QString channel;       // the channel it was classified from (empty when unknown)
    QString unit;          // as declared by the source
    BrakingThreshold threshold;
    QVector<DrivingStateInterval> active;
    QVector<DrivingStateInterval> known;
    QString unresolvedReason; // why the state is unknown throughout
    int rejectedSpikes = 0;
};

struct DrivingStateClassification {
    QString algorithm = drivingStatesAlgorithm;
    double start = 0.0;
    double end = 0.0;
    DrivingStateTrack braking;
    DrivingStateTrack accelerating;
    DrivingStateTrack cornering;
    DrivingStateTrack coasting;
    bool valid = false;
};

// Classifies [startTime, endTime] of `session`. Channels: the "brake" and
// "throttle" aliases (measured) and, when a pedal channel is missing and
// inference is allowed, "longitudinalAcceleration" (inferred); cornering
// from "lateralAcceleration"; coasting also needs "speed". Samples are not
// interpolated across gaps: a gap is unknown for every state that uses the
// channel. A channel whose declared unit differs from its threshold's is not
// used (the state is unknown), never rescaled.
[[nodiscard]] DrivingStateClassification classifyDrivingStates(const TelemetrySession &session,
    double startTime, double endTime, const DrivingStateOptions &options = {});

// Where two states hold together (for example braking while cornering).
[[nodiscard]] QVector<DrivingStateInterval> overlapOf(const QVector<DrivingStateInterval> &first,
    const QVector<DrivingStateInterval> &second);
// Distance travelled over `intervals`: the "speed" alias (km/h) integrated
// between its samples; nothing is counted across a missing speed sample.
[[nodiscard]] double travelledMeters(const TelemetrySession &session, const QVector<DrivingStateInterval> &intervals);

} // namespace FlappedEar
