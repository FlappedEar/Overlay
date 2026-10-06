#pragma once

#include "telemetry/TelemetrySession.h"
#include "telemetry/SourceOperation.h"

#include <QString>

namespace FlappedEar {

struct SyncDiagnostics {
    double correlation = -1.0;
    double peakUniqueness = 0.0;
    int validSamples = 0;
    double sampleRate = 0.0;
    double coarseOffset = 0.0;
};

struct SyncCandidate {
    double offset = 0.0;
    // Always 1.0 from synchronize(): it solves the offset only, assuming both
    // clocks run at the same rate (a documented limitation, KAN-214).
    double timeScale = 1.0;
    double confidence = 0.0;
    QString strategy = QStringLiteral("GPS speed");
    SyncDiagnostics diagnostics;
};

// Confidence is the engine's bounded composite of correlation strength, peak
// uniqueness, and usable overlap. A 0.75 automatic threshold intentionally
// requires all three signals to be strong: a high correlation alone cannot
// overcome an ambiguous peak or short overlap. This keeps the well-supported
// real-recording result automatic while making weaker matches reviewable.
inline constexpr double kAutomaticSyncConfidenceThreshold = 0.75;
inline constexpr double kMinimumSyncOverlapSeconds = 20.0;
// Below this correlation of the speed traces a candidate is only offered,
// never applied automatically (KAN-146).
inline constexpr double kMinimumAutomaticSyncCorrelation = 0.8;
// Resource budgets, not limits on saved manual transforms or recording length.
inline constexpr qsizetype kMaximumSyncSignalSamples = 1'000'000;
inline constexpr int kMaximumSyncGridSamples = 1'000'000;
inline constexpr int kMaximumSyncOffsets = 100'001;
inline constexpr qint64 kMaximumSyncSamplePairs = 50'000'000;

enum class SyncConfidenceLevel { High, Medium, Low };

[[nodiscard]] SyncConfidenceLevel syncConfidenceLevel(double confidence);
[[nodiscard]] bool shouldAutoApplySyncCandidate(const SyncCandidate &candidate);

class TelemetrySyncEngine {
public:
    [[nodiscard]] static SyncCandidate synchronize(
        const TelemetrySession &video,
        const TelemetrySession &telemetry,
        const CancellationCheck &cancelled = {});
};

} // namespace FlappedEar
