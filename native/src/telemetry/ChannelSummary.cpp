#include "telemetry/ChannelSummary.h"

#include <QRegularExpression>

#include <algorithm>
#include <cmath>

namespace FlappedEar {

ChannelSummaryPolicy temperatureSummaryPolicy()
{
    ChannelSummaryPolicy policy;
    policy.minimumPlausible = -40.0;
    policy.maximumPlausible = 250.0;
    policy.zeroIsPlaceholder = true;
    return policy;
}

ChannelSummaryPolicy heartRateSummaryPolicy()
{
    ChannelSummaryPolicy policy;
    policy.minimumPlausible = kHeartRateMinimumPlausible;
    policy.maximumPlausible = kHeartRateMaximumPlausible;
    return policy;
}

bool zeroIsPlaceholder(const TelemetryChannel &channel, const ChannelSummaryPolicy &policy)
{
    // The channel's typical value decides whether an exact zero is a placeholder.
    if (!policy.zeroIsPlaceholder) return false;
    QVector<float> finite;
    for (const float value : channel.values()) if (std::isfinite(value)) finite.append(value);
    if (finite.isEmpty()) return false;
    std::nth_element(finite.begin(), finite.begin() + finite.size() / 2, finite.end());
    return std::abs(finite[finite.size() / 2]) > policy.placeholderTypicalAbove;
}

bool plausibleSample(const double value, const ChannelSummaryPolicy &policy, const bool zeroPlaceholder)
{
    return std::isfinite(value) && value >= policy.minimumPlausible && value <= policy.maximumPlausible
        && !(zeroPlaceholder && value == 0.0);
}

ChannelSummary summarizeChannel(const TelemetrySession &session, const QString &channelOrAlias,
    const double startTime, const double endTime, const ChannelSummaryPolicy &policy)
{
    ChannelSummary result;
    result.channel = session.aliases.value(channelOrAlias, channelOrAlias);
    result.startTime = startTime;
    result.endTime = endTime;
    const auto found = session.channels.constFind(result.channel);
    if (found == session.channels.cend() || found->timestamps().size() != found->values().size()) {
        result.unavailableReason = channelSummaryMissing;
        return result;
    }
    const auto &channel = *found;
    result.unit = channel.unit;
    if (!std::isfinite(startTime) || !std::isfinite(endTime) || endTime <= startTime) {
        result.unavailableReason = channelSummaryNoSamples;
        return result;
    }
    const bool zeroPlaceholder = zeroIsPlaceholder(channel, policy);
    std::optional<std::pair<double, double>> previous; // time, value
    double integral = 0.0;
    for (qsizetype i = 0; i < channel.timestamps().size(); ++i) {
        const double time = channel.timestamps()[i];
        if (time < startTime || time > endTime) continue;
        const double value = channel.values()[i];
        if (!std::isfinite(value)) { previous.reset(); continue; }
        if (value < policy.minimumPlausible || value > policy.maximumPlausible || (zeroPlaceholder && value == 0.0)) {
            ++result.excludedArtifacts;
            previous.reset(); // never join across an excluded sample
            continue;
        }
        ++result.sampleCount;
        if (!result.minimum || value < *result.minimum) { result.minimum = value; result.minimumTime = time; }
        if (!result.maximum || value > *result.maximum) { result.maximum = value; result.maximumTime = time; }
        if (previous && !telemetryIsGap(channel, previous->first, time)) {
            const double span = time - previous->first;
            integral += span * (value + previous->second) / 2.0;
            result.coveredSeconds += span;
        }
        previous = std::pair{time, value};
    }
    if (result.sampleCount == 0) {
        result.unavailableReason = channelSummaryNoSamples;
        result.minimum.reset(); result.maximum.reset();
        return result;
    }
    // A single isolated sample has a value but no covered time.
    result.mean = result.coveredSeconds > 0.0 ? integral / result.coveredSeconds : *result.minimum;
    result.coverage = std::clamp(result.coveredSeconds / (endTime - startTime), 0.0, 1.0);
    result.valid = true;
    return result;
}

ChannelSummary combineChannelSummaries(const QVector<ChannelSummary> &parts)
{
    if (parts.size() == 1) return parts.first();
    ChannelSummary result;
    if (parts.isEmpty()) { result.unavailableReason = channelSummaryNoSamples; return result; }
    result.channel = parts.first().channel;
    result.unit = parts.first().unit;
    result.startTime = parts.first().startTime;
    result.endTime = parts.last().endTime;
    double length = 0.0, weighted = 0.0;
    for (const auto &part : parts) {
        length += std::max(0.0, part.endTime - part.startTime);
        result.excludedArtifacts += part.excludedArtifacts;
        if (!part.valid) continue;
        result.sampleCount += part.sampleCount;
        result.coveredSeconds += part.coveredSeconds;
        weighted += *part.mean * part.coveredSeconds;
        if (!result.minimum || *part.minimum < *result.minimum) { result.minimum = part.minimum; result.minimumTime = part.minimumTime; }
        if (!result.maximum || *part.maximum > *result.maximum) { result.maximum = part.maximum; result.maximumTime = part.maximumTime; }
    }
    if (!result.minimum || result.coveredSeconds <= 0.0) {
        result.minimum.reset(); result.maximum.reset(); result.minimumTime.reset(); result.maximumTime.reset();
        result.coveredSeconds = 0.0;
        // Missing everywhere stays "missing"; otherwise there were no usable samples.
        const bool missing = std::all_of(parts.cbegin(), parts.cend(),
            [](const ChannelSummary &part) { return part.unavailableReason == channelSummaryMissing; });
        result.unavailableReason = missing ? channelSummaryMissing : channelSummaryNoSamples;
        return result;
    }
    result.mean = weighted / result.coveredSeconds;
    result.coverage = length > 0.0 ? std::clamp(result.coveredSeconds / length, 0.0, 1.0) : 0.0;
    result.valid = true;
    return result;
}

QStringList recordedTemperatureChannels(const TelemetrySession &session)
{
    static const QRegularExpression temperature(QStringLiteral("temp"), QRegularExpression::CaseInsensitiveOption);
    QStringList names;
    for (const auto &name : session.channelNames())
        if (temperature.match(name).hasMatch()) names.append(name);
    return names;
}

QVector<CoolingInterval> findCoolingIntervals(const TelemetrySession &session, const QString &channelOrAlias,
    const ChannelSummaryPolicy &policy, const CoolingOptions &options)
{
    QVector<CoolingInterval> intervals;
    const auto found = session.channels.constFind(session.aliases.value(channelOrAlias, channelOrAlias));
    if (found == session.channels.cend() || found->timestamps().size() != found->values().size()) return intervals;
    const auto &channel = *found;
    const bool zeroPlaceholder = zeroIsPlaceholder(channel, policy);
    // Split into continuously recorded stretches of valid samples.
    QVector<QVector<std::pair<double, double>>> stretches(1);
    double previousTime = -std::numeric_limits<double>::infinity();
    for (qsizetype i = 0; i < channel.timestamps().size(); ++i) {
        const double time = channel.timestamps()[i];
        const double value = channel.values()[i];
        const bool valid = std::isfinite(value) && value >= policy.minimumPlausible && value <= policy.maximumPlausible
            && !(zeroPlaceholder && value == 0.0);
        if (!valid || telemetryIsGap(channel, previousTime, time)) {
            if (!stretches.last().isEmpty()) stretches.append(QVector<std::pair<double, double>>{});
        }
        if (!valid) continue;
        stretches.last().append({time, value});
        previousTime = time;
    }
    for (const auto &stretch : stretches) {
        if (stretch.size() < 3) continue;
        // Centred moving average over the smoothing window (two pointers).
        QVector<double> smooth(stretch.size());
        qsizetype lo = 0, hi = 0;
        double sum = 0.0;
        const double half = options.smoothingSeconds / 2.0;
        for (qsizetype i = 0; i < stretch.size(); ++i) {
            while (hi < stretch.size() && stretch[hi].first <= stretch[i].first + half) sum += stretch[hi++].second;
            while (stretch[lo].first < stretch[i].first - half) sum -= stretch[lo++].second;
            smooth[i] = sum / static_cast<double>(hi - lo);
        }
        // Peak -> following trough, walking the smoothed series once.
        qsizetype peak = 0, trough = 0;
        bool falling = false;
        const auto close = [&] {
            const CoolingInterval interval{stretch[peak].first, stretch[trough].first, smooth[peak], smooth[trough]};
            if (interval.drop() >= options.minimumDrop && interval.seconds() >= options.minimumSeconds) intervals.append(interval);
        };
        for (qsizetype i = 1; i < stretch.size(); ++i) {
            if (!falling) {
                if (smooth[i] >= smooth[peak]) { peak = i; continue; }
                if (smooth[peak] - smooth[i] > 0.0) { falling = true; trough = i; }
            } else {
                if (smooth[i] < smooth[trough] - 1e-9) { trough = i; continue; }
                if (smooth[i] <= smooth[trough] + 1e-9) continue; // a flat bottom does not extend the cooling
                // Rising again: close this cooling when the rise is real, not noise.
                if (smooth[i] - smooth[trough] >= 1.0) {
                    close();
                    falling = false;
                    peak = i;
                }
            }
        }
        if (falling) close();
    }
    return intervals;
}

} // namespace FlappedEar
