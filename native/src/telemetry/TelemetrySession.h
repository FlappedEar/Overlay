#pragma once

#include "telemetry/SourceOperation.h"
#include "telemetry/TimingGate.h"

#include <QHash>
#include <QPointF>
#include <QString>
#include <QStringList>
#include <QVector>
#include <optional>

namespace FlappedEar {

enum class InterpolationMode { Nearest, Previous, Linear };

// sampledSegments() used to return the same empty result for an invalid range,
// a missing channel, a malformed channel, and a genuinely empty overlap,
// making a real problem indistinguishable from an ordinary telemetry gap.
enum class SampledSegmentsStatus { Ok, InvalidRange, ChannelMissing, ChannelMalformed };

struct TelemetryChannel {
    QString name;
    QString unit;
    QVector<double> timestamps;
    QVector<float> values;

    // Telemetry sessions are immutable after source loading. Cache the cadence
    // statistic with its channel rather than recomputing a full timestamp
    // median for every presentation lookup.
    mutable bool cadenceStatisticsValid = false;
    mutable double cachedBaseIntervalSeconds = 0.0;
    mutable qsizetype cadenceStatisticComputationCount = 0;
};

struct SyncTransform {
    double offset = 0.0;
    double timeScale = 1.0;
};

class TelemetrySession {
public:
    double duration = 0.0;
    double startTime = 0.0;
    QHash<QString, QString> metadata;
    QHash<QString, TelemetryChannel> channels;
    QHash<QString, QString> aliases;
    QStringList warnings;
    QVector<TimingGate> timingGates;
    qsizetype sampleCount = 0;

    // Public telemetry semantics are intentionally strict: queries outside a
    // channel's range and internal non-finite samples are no data. Linear
    // interpolation requires two adjacent finite samples; gaps are never bridged
    // (telemetryValueAt, KAN-157).
    [[nodiscard]] std::optional<double> valueAt(
        const QString &channelName,
        double time,
        InterpolationMode mode = InterpolationMode::Linear) const;

    [[nodiscard]] QStringList channelNames() const;
    // Analysis uses actual samples, split at every missing value. Each time
    // bucket contributes its ordered minimum/maximum, so the result is bounded
    // to approximately twice maximumPoints while retaining short extrema.
    [[nodiscard]] QVector<QVector<QPointF>> sampledSegments(
        const QString &channelName,
        double startTime,
        double endTime,
        int maximumPoints,
        SampledSegmentsStatus *status = nullptr) const;
};

// The driver's throttle input (KAN-118). When a recording has an
// accelerator-pedal channel with numeric data, the "throttle" alias refers to
// it; the throttle plate (which follows the ECU: idle air, a lower full-open
// reading, rev-match blips with the pedal released) stays available under its
// own channel name. Aliases are not part of recording fingerprints.
void preferAcceleratorPedalForThrottle(TelemetrySession &session);

[[nodiscard]] std::optional<double> videoToTelemetryTime(double videoTime, const SyncTransform &transform);
[[nodiscard]] std::optional<double> telemetryToVideoTime(
    double telemetryTime, const SyncTransform &transform);
// A gap is two adjacent samples more than three median sample intervals apart
// (or minimumSeconds, if larger). Every consumer uses this one threshold.
[[nodiscard]] double telemetryGapThreshold(
    const TelemetryChannel &channel, double minimumSeconds = 0.0);
// KAN-157: the shared value lookup behind TelemetrySession::valueAt. Outside the
// channel, at a non-finite sample, or inside a gap there is no data.
[[nodiscard]] std::optional<double> telemetryValueAt(
    const TelemetryChannel &channel, double time, InterpolationMode mode = InterpolationMode::Linear);

// Every channel's lazily-computed cadence statistics are non-atomic mutable
// state. Call this once, single-threaded, before a TelemetrySession is
// published as a shared const object; otherwise concurrent const readers
// (GUI render vs. a background worker) race on the first access to any
// not-yet-warmed channel.
void freezeCachedStatistics(const TelemetrySession &session, const CancellationCheck &cancelled = {});

} // namespace FlappedEar
