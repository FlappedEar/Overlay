#pragma once

#include "telemetry/SourceOperation.h"
#include "telemetry/TelemetrySession.h"

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

namespace FlappedEar {

// KAN-102: channels from a run's alternative recordings brought onto the
// primary recording's clock, with the provenance of every output sample and
// an explicit rule wherever two recordings measure the same thing. Nothing
// is overwritten silently:
// - a source is used only when its clock alignment (KAN-101) was "aligned";
// - a channel only an alternative has is added on the primary clock;
// - a channel both have keeps the primary unless a rule says otherwise; if
//   their overlapping measurements disagree and no rule was chosen, the
//   conflict is reported as unresolved;
// - units must match exactly; nothing is rescaled.
// Samples are never resampled or interpolated: each output sample is a real
// sample of one source with its timestamp transformed, so gaps stay gaps and
// no precision is claimed beyond what a source recorded.
inline constexpr auto channelFusionAlgorithm = "channel-fusion-v1";

// primaryTime = sourceTime + offsetSeconds + driftPpm * 1e-6 * sourceTime
struct SourceClock {
    double offsetSeconds = 0.0;
    double driftPpm = 0.0;
};

struct FusionSource {
    QString sourceId;
    const TelemetrySession *session = nullptr;
    SourceClock clock;
    QString alignmentStatus; // must be "aligned"
};

enum class FusionRule {
    PrimaryOnly,        // keep the primary's samples
    FillGaps,           // the primary, plus the alternative where the primary has none
    PreferAlternative,  // the alternative, plus the primary where the alternative has none
};

struct FusionPolicy {
    // By channel key (the alias when the channel has one, e.g. "speed",
    // otherwise its name) to the alternative source the rule applies to.
    QHash<QString, QPair<QString, FusionRule>> rules;
};

struct FusedSegment {
    QString sourceId;
    double start = 0.0;  // on the primary clock
    double end = 0.0;
    SourceClock clock;
    double sampleIntervalSeconds = 0.0; // the source's median spacing: its real resolution
};

struct FusedChannel {
    QString key;
    QString name;
    QString unit;
    QString rule;        // "primary", "added", "fillGaps", "preferAlternative", "unresolvedConflict"
    QVector<FusedSegment> segments;
    TelemetryChannel channel;
    // When an alternative also measured it: how the overlap compared.
    QString comparedSourceId;
    qsizetype comparedSamples = 0;
    double medianDifference = 0.0;
    bool conflicting = false;
};

struct ChannelFusionResult {
    QVector<FusedChannel> channels;
    QStringList unresolved;            // keys whose conflict has no rule
    QStringList unitMismatches;        // "key: sourceId" pairs that were not fused
    QStringList refusedSources;        // source IDs whose clocks are not aligned
};

// The difference above which two recordings of one quantity conflict, by
// unit: km/h 2, % 3, g 0.05, °C 2, rpm 100; otherwise 5 % of the primary's
// range in the overlap.
[[nodiscard]] double fusionConflictTolerance(const QString &unit, double overlapRange);

[[nodiscard]] ChannelFusionResult fuseChannels(const TelemetrySession &primary, const QString &primarySourceId,
    const QVector<FusionSource> &alternatives, const FusionPolicy &policy = {}, const CancellationCheck &cancelled = {});

} // namespace FlappedEar
