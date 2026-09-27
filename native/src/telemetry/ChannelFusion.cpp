#include "telemetry/ChannelFusion.h"

#include <algorithm>
#include <cmath>

namespace FlappedEar {
namespace {

struct Sample {
    double time;
    float value;
    int source; // 0 = primary, 1 = the alternative
};

struct Span {
    double start;
    double end;
};

bool validClock(const SourceClock &clock)
{
    return std::isfinite(clock.offsetSeconds) && std::isfinite(clock.driftPpm) && 1.0 + clock.driftPpm * 1e-6 > 0.0;
}

double toPrimary(const double time, const SourceClock &clock)
{
    return time + clock.offsetSeconds + clock.driftPpm * 1e-6 * time;
}

double medianInterval(const QVector<double> &times)
{
    if (times.size() < 2) return 0.0;
    QVector<double> steps;
    steps.reserve(times.size() - 1);
    for (qsizetype index = 1; index < times.size(); ++index) steps.append(times[index] - times[index - 1]);
    std::nth_element(steps.begin(), steps.begin() + steps.size() / 2, steps.end());
    return steps[steps.size() / 2];
}

// Contiguous stretches: a step longer than `gap` splits them.
QVector<Span> spans(const QVector<double> &times, const double gap)
{
    QVector<Span> result;
    for (qsizetype index = 0; index < times.size(); ++index) {
        if (result.isEmpty() || (gap > 0.0 && times[index] - result.last().end > gap)) result.append({times[index], times[index]});
        else result.last().end = times[index];
    }
    return result;
}

bool covered(const QVector<Span> &coverage, const double time)
{
    const auto next = std::upper_bound(coverage.cbegin(), coverage.cend(), time,
        [](double value, const Span &span) { return value < span.start; });
    return next != coverage.cbegin() && time <= std::prev(next)->end;
}

// The key a channel is matched by: its alias when it has one, else its name.
QString channelKey(const TelemetrySession &session, const QString &name)
{
    QStringList aliases;
    for (auto it = session.aliases.cbegin(); it != session.aliases.cend(); ++it)
        if (it.value() == name) aliases.append(it.key());
    if (aliases.isEmpty()) return name;
    std::sort(aliases.begin(), aliases.end());
    return aliases.first();
}

QString primaryNameFor(const TelemetrySession &primary, const QString &key, const QString &alternativeName)
{
    const auto aliased = primary.aliases.value(key);
    if (!aliased.isEmpty() && primary.channels.contains(aliased)) return aliased;
    return primary.channels.contains(alternativeName) ? alternativeName : QString();
}

// Output samples of one source as segments of contiguous stretches.
void appendSegments(QVector<FusedSegment> &segments, const QVector<Sample> &samples, const int source,
    const QString &sourceId, const SourceClock &clock, const double gap, const double interval)
{
    for (qsizetype index = 0; index < samples.size(); ++index) {
        if (samples[index].source != source) continue;
        const bool continues = index > 0 && samples[index - 1].source == source
            && !(gap > 0.0 && samples[index].time - samples[index - 1].time > gap) && !segments.isEmpty()
            && segments.last().sourceId == sourceId;
        if (continues) segments.last().end = samples[index].time;
        else segments.append({sourceId, samples[index].time, samples[index].time, clock, interval});
    }
}

} // namespace

double fusionConflictTolerance(const QString &unit, const double overlapRange)
{
    const auto u = unit.trimmed().toLower();
    if (u == QLatin1String("km/h") || u == QLatin1String("kmh") || u == QLatin1String("kph")) return 2.0;
    if (u == QLatin1String("%")) return 3.0;
    if (u == QLatin1String("g")) return 0.05;
    if (u == QLatin1String("c") || u == QLatin1String("°c") || u == QLatin1String("degc")) return 2.0;
    if (u == QLatin1String("rpm")) return 100.0;
    return std::max(1e-9, 0.05 * std::abs(overlapRange));
}

ChannelFusionResult fuseChannels(const TelemetrySession &primary, const QString &primarySourceId,
    const QVector<FusionSource> &alternatives, const FusionPolicy &policy, const CancellationCheck &cancelled)
{
    ChannelFusionResult result;
    QHash<QString, qsizetype> byKey;
    // The primary's own channels, unchanged.
    QStringList primaryNames = primary.channels.keys();
    std::sort(primaryNames.begin(), primaryNames.end());
    for (const auto &name : primaryNames) {
        const auto &channel = primary.channels[name];
        FusedChannel fused;
        fused.key = channelKey(primary, name);
        fused.name = name;
        fused.unit = channel.unit;
        fused.rule = QStringLiteral("primary");
        fused.channel = channel;
        QVector<Sample> samples;
        for (qsizetype index = 0; index < channel.timestamps.size() && index < channel.values.size(); ++index)
            samples.append({channel.timestamps[index], channel.values[index], 0});
        appendSegments(fused.segments, samples, 0, primarySourceId, {}, telemetryGapThreshold(channel),
            medianInterval(channel.timestamps));
        byKey.insert(fused.key, result.channels.size());
        result.channels.append(fused);
    }

    for (const auto &source : alternatives) {
        throwIfCancelled(cancelled);
        if (!source.session || source.alignmentStatus != QLatin1String("aligned") || !validClock(source.clock)) {
            result.refusedSources.append(source.sourceId);
            continue;
        }
        QStringList names = source.session->channels.keys();
        std::sort(names.begin(), names.end());
        for (const auto &name : names) {
            throwIfCancelled(cancelled);
            const auto &channel = source.session->channels[name];
            if (channel.timestamps.size() != channel.values.size() || channel.timestamps.isEmpty()) continue;
            const auto key = channelKey(*source.session, name);
            QVector<double> times;
            times.reserve(channel.timestamps.size());
            for (const double time : channel.timestamps) times.append(toPrimary(time, source.clock));
            const double gap = telemetryGapThreshold(channel) * (1.0 + source.clock.driftPpm * 1e-6);
            const double interval = medianInterval(times);
            const auto primaryName = primaryNameFor(primary, key, name);
            const auto existing = byKey.constFind(key);

            if (primaryName.isEmpty() && existing == byKey.cend()) {
                // Only the alternative recorded it: added on the primary clock.
                FusedChannel fused;
                fused.key = key;
                fused.name = name;
                fused.unit = channel.unit;
                fused.rule = QStringLiteral("added");
                fused.channel.name = name;
                fused.channel.unit = channel.unit;
                fused.channel.timestamps = times;
                fused.channel.values = channel.values;
                QVector<Sample> samples;
                for (qsizetype index = 0; index < times.size(); ++index) samples.append({times[index], channel.values[index], 1});
                appendSegments(fused.segments, samples, 1, source.sourceId, source.clock, gap, interval);
                byKey.insert(key, result.channels.size());
                result.channels.append(fused);
                continue;
            }
            if (existing == byKey.cend()) continue; // added earlier by another alternative under a different key
            auto &fused = result.channels[*existing];
            if (fused.rule != QLatin1String("primary")) continue; // already decided by an earlier alternative
            if (fused.unit.trimmed().compare(channel.unit.trimmed(), Qt::CaseInsensitive) != 0) {
                result.unitMismatches.append(key + QStringLiteral(": ") + source.sourceId);
                continue;
            }

            // Compare the overlap at the alternative's own samples.
            QVector<double> differences;
            double low = std::numeric_limits<double>::infinity(), high = -low;
            for (qsizetype index = 0; index < times.size(); ++index) {
                const double value = channel.values[index];
                const auto reference = primary.valueAt(fused.name, times[index]);
                if (!std::isfinite(value) || !reference) continue;
                differences.append(std::abs(value - *reference));
                low = std::min(low, *reference);
                high = std::max(high, *reference);
            }
            fused.comparedSourceId = source.sourceId;
            fused.comparedSamples = differences.size();
            if (!differences.isEmpty()) {
                std::nth_element(differences.begin(), differences.begin() + differences.size() / 2, differences.end());
                fused.medianDifference = differences[differences.size() / 2];
                fused.conflicting = differences.size() >= 10
                    && fused.medianDifference > fusionConflictTolerance(fused.unit, high - low);
            }

            const auto chosen = policy.rules.constFind(key);
            const bool ruled = chosen != policy.rules.cend() && chosen->first == source.sourceId;
            if (!ruled || chosen->second == FusionRule::PrimaryOnly) {
                if (ruled) fused.rule = QStringLiteral("primary");
                else if (fused.conflicting) {
                    fused.rule = QStringLiteral("unresolvedConflict");
                    result.unresolved.append(key);
                }
                continue;
            }
            // Merge: the preferred source everywhere it has data, the other
            // only outside the preferred one's recorded stretches.
            const bool preferAlternative = chosen->second == FusionRule::PreferAlternative;
            QVector<Sample> primarySamples, alternativeSamples;
            for (qsizetype index = 0; index < fused.channel.timestamps.size(); ++index)
                primarySamples.append({fused.channel.timestamps[index], fused.channel.values[index], 0});
            for (qsizetype index = 0; index < times.size(); ++index)
                alternativeSamples.append({times[index], channel.values[index], 1});
            const auto &preferred = preferAlternative ? alternativeSamples : primarySamples;
            const auto &other = preferAlternative ? primarySamples : alternativeSamples;
            QVector<double> preferredTimes;
            for (const auto &sample : preferred) preferredTimes.append(sample.time);
            const double primaryGap = telemetryGapThreshold(fused.channel);
            const auto coverage = spans(preferredTimes, preferAlternative ? gap : primaryGap);
            QVector<Sample> merged = preferred;
            for (const auto &sample : other)
                if (!covered(coverage, sample.time)) merged.append(sample);
            std::sort(merged.begin(), merged.end(), [](const Sample &a, const Sample &b) { return a.time < b.time; });
            // Strictly increasing timestamps: an equal time keeps the preferred sample.
            QVector<Sample> ordered;
            for (const auto &sample : merged) {
                if (!ordered.isEmpty() && sample.time <= ordered.last().time) {
                    if (sample.source == (preferAlternative ? 1 : 0)) ordered.last() = sample;
                    continue;
                }
                ordered.append(sample);
            }
            // A fresh channel: a copy would carry the primary's cached cadence.
            TelemetryChannel output;
            output.name = fused.channel.name;
            output.unit = fused.channel.unit;
            for (const auto &sample : ordered) {
                output.timestamps.append(sample.time);
                output.values.append(sample.value);
            }
            fused.channel = output;
            fused.segments.clear();
            QVector<FusedSegment> primarySegments, alternativeSegments;
            appendSegments(primarySegments, ordered, 0, primarySourceId, {}, primaryGap,
                medianInterval(primary.channels[fused.name].timestamps));
            appendSegments(alternativeSegments, ordered, 1, source.sourceId, source.clock, gap, interval);
            fused.segments = primarySegments + alternativeSegments;
            std::sort(fused.segments.begin(), fused.segments.end(),
                [](const FusedSegment &a, const FusedSegment &b) { return a.start < b.start; });
            fused.rule = preferAlternative ? QStringLiteral("preferAlternative") : QStringLiteral("fillGaps");
        }
    }
    return result;
}

} // namespace FlappedEar
