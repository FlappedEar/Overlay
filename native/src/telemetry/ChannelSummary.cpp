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
    policy.minimumPlausible = 30.0;
    policy.maximumPlausible = 230.0;
    return policy;
}

ChannelSummary summarizeChannel(const TelemetrySession &session, const QString &channelOrAlias,
    const double startTime, const double endTime, const ChannelSummaryPolicy &policy)
{
    ChannelSummary result;
    result.channel = session.aliases.value(channelOrAlias, channelOrAlias);
    result.startTime = startTime;
    result.endTime = endTime;
    const auto found = session.channels.constFind(result.channel);
    if (found == session.channels.cend() || found->timestamps.size() != found->values.size()) {
        result.unavailableReason = channelSummaryMissing;
        return result;
    }
    const auto &channel = *found;
    result.unit = channel.unit;
    if (!std::isfinite(startTime) || !std::isfinite(endTime) || endTime <= startTime) {
        result.unavailableReason = channelSummaryNoSamples;
        return result;
    }
    // The channel's typical value decides whether an exact zero is a placeholder.
    bool zeroPlaceholder = false;
    if (policy.zeroIsPlaceholder) {
        QVector<float> finite;
        for (const float value : channel.values) if (std::isfinite(value)) finite.append(value);
        if (!finite.isEmpty()) {
            std::nth_element(finite.begin(), finite.begin() + finite.size() / 2, finite.end());
            zeroPlaceholder = std::abs(finite[finite.size() / 2]) > policy.placeholderTypicalAbove;
        }
    }
    const double gapLimit = telemetryGapThreshold(channel);
    std::optional<std::pair<double, double>> previous; // time, value
    double integral = 0.0;
    for (qsizetype i = 0; i < channel.timestamps.size(); ++i) {
        const double time = channel.timestamps[i];
        if (time < startTime || time > endTime) continue;
        const double value = channel.values[i];
        if (!std::isfinite(value)) { previous.reset(); continue; }
        if (value < policy.minimumPlausible || value > policy.maximumPlausible || (zeroPlaceholder && value == 0.0)) {
            ++result.excludedArtifacts;
            previous.reset(); // never join across an excluded sample
            continue;
        }
        ++result.sampleCount;
        if (!result.minimum || value < *result.minimum) { result.minimum = value; result.minimumTime = time; }
        if (!result.maximum || value > *result.maximum) { result.maximum = value; result.maximumTime = time; }
        if (previous && time - previous->first <= gapLimit) {
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

QStringList recordedTemperatureChannels(const TelemetrySession &session)
{
    static const QRegularExpression temperature(QStringLiteral("temp"), QRegularExpression::CaseInsensitiveOption);
    QStringList names;
    for (const auto &name : session.channelNames())
        if (temperature.match(name).hasMatch()) names.append(name);
    return names;
}

} // namespace FlappedEar
