#pragma once

#include "telemetry/TelemetrySession.h"

#include <QString>
#include <QStringList>
#include <limits>
#include <optional>

namespace FlappedEar {

// Summary of one recorded channel over a time interval (KAN-67, KAN-69).
//
// Mean: time-weighted. Consecutive valid samples closer together than the
// channel's gap threshold are joined linearly; the mean is the integral of
// that line over the covered time divided by the covered time. A gap is never
// bridged, so a dropout neither pulls the mean nor counts as covered.
// Coverage: covered time / interval length (0..1).
// Artifacts (documented policy, always counted, never silently dropped):
//  - a value outside the policy's plausible range is excluded;
//  - with zeroIsPlaceholder, an exact 0 is excluded when the channel's typical
//    (median) value is far from zero -- OBD adapters report 0 before a first
//    response or after a dropout.
// Units are reported as declared ("" when the recording does not declare them).
inline constexpr auto channelSummaryAlgorithm = "channel-summary-v1";
inline constexpr auto channelSummaryMissing = "channelMissing";
inline constexpr auto channelSummaryNoSamples = "noValidSamples";

struct ChannelSummaryPolicy {
    double minimumPlausible = -std::numeric_limits<double>::infinity();
    double maximumPlausible = std::numeric_limits<double>::infinity();
    bool zeroIsPlaceholder = false;
    double placeholderTypicalAbove = 20.0; // zero counts as a placeholder only above this median
};

// Temperatures (°C): -40..250 plausible, exact zeros are placeholders.
[[nodiscard]] ChannelSummaryPolicy temperatureSummaryPolicy();
// Heart rate (bpm): 30..230 plausible.
[[nodiscard]] ChannelSummaryPolicy heartRateSummaryPolicy();

struct ChannelSummary {
    QString channel;
    QString unit;
    double startTime = 0.0;
    double endTime = 0.0;
    qsizetype sampleCount = 0;       // valid samples used
    qsizetype excludedArtifacts = 0; // implausible values and placeholders
    std::optional<double> minimum, maximum, mean;
    std::optional<double> minimumTime, maximumTime;
    double coveredSeconds = 0.0;
    double coverage = 0.0;
    QString unavailableReason;
    bool valid = false;
};

[[nodiscard]] ChannelSummary summarizeChannel(const TelemetrySession &session, const QString &channelOrAlias,
    double startTime, double endTime, const ChannelSummaryPolicy &policy);

// The recording's own temperature channels (name contains "temp"), in name
// order. Never invented: an absent sensor is simply not listed.
[[nodiscard]] QStringList recordedTemperatureChannels(const TelemetrySession &session);

} // namespace FlappedEar
