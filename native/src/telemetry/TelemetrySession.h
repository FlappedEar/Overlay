#pragma once

#include "telemetry/SourceOperation.h"
#include "telemetry/TimingGate.h"

#include <QHash>
#include <QPointF>
#include <QString>
#include <QStringList>
#include <QVector>
#include <mutex>
#include <optional>

namespace FlappedEar {

enum class InterpolationMode { Nearest, Previous, Linear };

// sampledSegments() used to return the same empty result for an invalid range,
// a missing channel, a malformed channel, and a genuinely empty overlap,
// making a real problem indistinguishable from an ordinary telemetry gap.
// SegmentsTruncated: more separate segments than the point budget; a uniform
// choice of whole segments is returned, one point each (KAN-210).
enum class SampledSegmentsStatus { Ok, InvalidRange, ChannelMissing, ChannelMalformed, SegmentsTruncated };

// The cadence statistic behind the gap rule (KAN-157), cached with its channel.
// It is safe to read from several threads at once, and it is recomputed when the
// channel's timestamp buffer, length or end points change (KAN-209), so a session
// edited after a lookup, or shared before it was warmed, never reads a stale or
// half-written value.
class ChannelCadenceCache {
public:
    ChannelCadenceCache() = default;
    ChannelCadenceCache(const ChannelCadenceCache &other);
    ChannelCadenceCache &operator=(const ChannelCadenceCache &other);

    // The median positive sample interval, 0 when there is none.
    [[nodiscard]] double baseInterval(const QVector<double> &timestamps) const;
    [[nodiscard]] bool isCurrent(const QVector<double> &timestamps) const;
    [[nodiscard]] qsizetype computations() const;

private:
    struct Entry {
        bool valid = false;
        const double *data = nullptr;
        qsizetype size = 0;
        quint64 firstBits = 0;
        quint64 lastBits = 0;
        double interval = 0.0;
        qsizetype computations = 0;
    };
    [[nodiscard]] static bool matches(const Entry &entry, const QVector<double> &timestamps);
    mutable std::mutex m_mutex;
    mutable Entry m_entry;
};

// One telemetry channel (KAN-209). Its timestamps are finite and strictly
// increasing, with one value per timestamp; a value may be NaN, which marks a
// gap. The samples change only through the setters, which reject anything
// else with std::invalid_argument, so every reader can rely on that order.
class TelemetryChannel {
public:
    QString name;
    QString unit;

    TelemetryChannel() = default;
    TelemetryChannel(QString name, QString unit, QVector<double> timestamps = {}, QVector<float> values = {});

    [[nodiscard]] const QVector<double> &timestamps() const noexcept { return m_timestamps; }
    [[nodiscard]] const QVector<float> &values() const noexcept { return m_values; }
    [[nodiscard]] qsizetype sampleCount() const noexcept { return m_timestamps.size(); }
    [[nodiscard]] bool isEmpty() const noexcept { return m_timestamps.isEmpty(); }
    [[nodiscard]] const ChannelCadenceCache &cadence() const noexcept { return m_cadence; }

    void setSamples(QVector<double> timestamps, QVector<float> values);
    // The time must be finite and later than the last sample's.
    void appendSample(double time, float value);
    void setValue(qsizetype index, float value);
    void reserve(qsizetype size);
    void clear();

private:
    QVector<double> m_timestamps;
    QVector<float> m_values;
    ChannelCadenceCache m_cadence {};
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
    // to approximately twice maximumPoints while retaining short extrema. Every
    // segment keeps at least one point unless the status is SegmentsTruncated.
    [[nodiscard]] QVector<QVector<QPointF>> sampledSegments(
        const QString &channelName,
        double startTime,
        double endTime,
        int maximumPoints,
        SampledSegmentsStatus *status = nullptr) const;
    // The same runs of actual samples (time, value) without any reduction.
    [[nodiscard]] QVector<QVector<QPointF>> rawSegments(
        const QString &channelName,
        double startTime,
        double endTime,
        SampledSegmentsStatus *status = nullptr) const;
};

// Up to `budget` of `count` items spread evenly by index, first item included
// (KAN-220). Keeps close to the budget where a whole-number stride would keep
// about half of it once the count is just over it.
[[nodiscard]] inline bool keepEvenlySpread(const qsizetype index, const qsizetype count, const qsizetype budget)
{
    if (count <= budget) return true;
    return index == 0 || (index * budget) / count != ((index - 1) * budget) / count;
}

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

// The median positive sample interval of a channel (cached, thread-safe).
[[nodiscard]] double telemetryBaseInterval(const TelemetryChannel &channel);

// Computes every channel's cadence statistic up front, so the first lookups on
// a newly loaded session do not pay for it. Not required for thread safety.
void freezeCachedStatistics(const TelemetrySession &session, const CancellationCheck &cancelled = {});

// RaceChrono writes 0 bpm where its heart-rate monitor has no reading, for
// example in the final row of a VBO export (KAN-222). No driver's heart rate
// lies outside this range, so the parsers turn such values into no data.
inline constexpr double kHeartRateMinimumPlausible = 30.0;
inline constexpr double kHeartRateMaximumPlausible = 230.0;
void markImplausibleHeartRate(TelemetrySession &session);

} // namespace FlappedEar
