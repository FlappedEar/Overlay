#pragma once

#include "telemetry/SourceOperation.h"
#include "telemetry/TelemetrySession.h"

#include <QString>
#include <QVector>

#include <optional>

namespace FlappedEar {

// KAN-101: how an alternative recording of a run (KAN-90) lines up in time
// with the run's primary recording, before any channel could be fused.
// Two kinds of evidence are kept apart:
// - declared: each logger's own start timestamp (RCZ always, a RaceChrono
//   VBO when it states one); a logger clock can be wrong or set to a zone;
// - measured: the recordings' speed traces cross-correlated by the central
//   sync engine, over the whole overlap and in windows along it, so a clock
//   running at a different rate (drift) shows as a trend of the windowed
//   offsets.
// The convention is primaryTime = candidateTime + offset + drift * candidateTime.
// Nothing here changes either recording: each stays usable on its own, and
// an alignment is only ever described, never applied.
inline constexpr auto recordingAlignmentAlgorithm = "recording-alignment-v1";

inline constexpr auto alignmentAligned = "aligned";
inline constexpr auto alignmentAmbiguous = "ambiguous";
inline constexpr auto alignmentConflicting = "conflicting";
inline constexpr auto alignmentInsufficient = "insufficient";

struct RecordingAlignmentOptions {
    double minimumWindowCorrelation = 0.9;   // a window's speed match must be this strong to count
    double maximumResidualSeconds = 0.3;     // windows must agree with the fitted line this closely
    double maximumPlausibleDriftPpm = 1000.0;
    double declaredToleranceSeconds = 2.0;   // declared vs measured before they conflict
    int maximumWindows = 8;
    double minimumWindowSeconds = 60.0;
};

struct AlignmentWindow {
    double candidateTime = 0.0; // window centre on the candidate's clock
    double offset = 0.0;
    double correlation = 0.0;
    bool used = false;
};

struct RecordingAlignment {
    QString status = alignmentInsufficient;
    QString reason;                        // why it is not "aligned"
    std::optional<double> declaredOffset;  // seconds, from the loggers' start timestamps
    std::optional<double> offset;          // measured, at candidate time 0
    std::optional<double> driftPpm;        // with three agreeing windows, when resolvable over the overlap
    std::optional<double> uncertaintySeconds;
    double correlation = -1.0;             // whole-overlap speed correlation
    double peakUniqueness = 0.0;
    double confidence = 0.0;               // the sync engine's composite
    double overlapSeconds = 0.0;
    QVector<AlignmentWindow> windows;
    qsizetype usedWindows = 0;
    // The speed match also fits elsewhere (a lap away); the agreeing declared
    // clock chose between the matches.
    bool resolvedByDeclaredClock = false;
};

// Aligns `candidate` to `primary`. Cooperatively cancellable (throws
// OperationCancelled).
[[nodiscard]] RecordingAlignment alignRecordings(const TelemetrySession &primary, const TelemetrySession &candidate,
    const CancellationCheck &cancelled = {}, const RecordingAlignmentOptions &options = {});

} // namespace FlappedEar
