#include "telemetry/TelemetrySession.h"

#include <QRegularExpression>

#include <algorithm>
#include <cmath>
#include <utility>

namespace FlappedEar {

double telemetryGapThreshold(const TelemetryChannel &channel, const double minimumSeconds)
{
    if (!channel.cadenceStatisticsValid) {
        QVector<double> intervals;
        intervals.reserve(std::max<qsizetype>(0, channel.timestamps.size() - 1));
        for (qsizetype index = 1; index < channel.timestamps.size(); ++index) {
            const double interval = channel.timestamps[index] - channel.timestamps[index - 1];
            if (std::isfinite(interval) && interval > 0.0) intervals.append(interval);
        }
        if (!intervals.isEmpty()) {
            const auto middle = intervals.begin() + intervals.size() / 2;
            std::nth_element(intervals.begin(), middle, intervals.end());
            channel.cachedBaseIntervalSeconds = *middle;
        } else {
            channel.cachedBaseIntervalSeconds = 0.0;
        }
        channel.cadenceStatisticsValid = true;
        ++channel.cadenceStatisticComputationCount;
    }
    return std::max(std::max(0.0, minimumSeconds), channel.cachedBaseIntervalSeconds * 3.0);
}

void freezeCachedStatistics(const TelemetrySession &session, const CancellationCheck &cancelled)
{
    for (const auto &channel : std::as_const(session.channels)) {
        throwIfCancelled(cancelled);
        (void)telemetryGapThreshold(channel);
    }
}

std::optional<double> TelemetrySession::valueAt(
    const QString &channelName,
    const double time,
    const InterpolationMode mode) const
{
    const QString resolved = aliases.value(channelName, channelName);
    const auto channelIterator = channels.constFind(resolved);
    if (channelIterator == channels.cend()) {
        return std::nullopt;
    }
    return telemetryValueAt(channelIterator.value(), time, mode);
}

std::optional<double> telemetryValueAt(const TelemetryChannel &channel, const double time, const InterpolationMode mode)
{
    if (channel.timestamps.isEmpty() || channel.values.isEmpty() || !std::isfinite(time)) {
        return std::nullopt;
    }
    const auto &timestamps = channel.timestamps;
    const auto &values = channel.values;
    if (timestamps.size() != values.size() || time < timestamps.front() || time > timestamps.back()) {
        return std::nullopt;
    }

    const auto nextIterator = std::lower_bound(timestamps.cbegin(), timestamps.cend(), time);
    const qsizetype next = std::distance(timestamps.cbegin(), nextIterator);
    if (next < 0 || next >= values.size()) {
        return std::nullopt;
    }
    const auto finiteValueAt = [&values](const qsizetype index) -> std::optional<double> {
        if (index < 0 || index >= values.size() || !std::isfinite(values[index])) {
            return std::nullopt;
        }
        return values[index];
    };
    if (*nextIterator == time) {
        return finiteValueAt(next);
    }
    if (next == 0) {
        return std::nullopt;
    }
    const qsizetype previous = next - 1;
    // KAN-157: the one gap rule. Between two samples farther apart than the
    // channel's gap threshold there is no data in any mode: a held, nearest
    // or interpolated value would bridge a loss of signal.
    const double gapThreshold = telemetryGapThreshold(channel);
    if (gapThreshold > 0.0 && timestamps[next] - timestamps[previous] > gapThreshold) {
        return std::nullopt;
    }
    if (mode == InterpolationMode::Previous) {
        return finiteValueAt(previous);
    }
    if (mode == InterpolationMode::Nearest) {
        return time - timestamps[previous] <= timestamps[next] - time ? finiteValueAt(previous)
                                                                       : finiteValueAt(next);
    }
    const double span = timestamps[next] - timestamps[previous];
    const auto previousValue = finiteValueAt(previous);
    const auto nextValue = finiteValueAt(next);
    if (!previousValue || !nextValue || !std::isfinite(span) || span <= 0.0) {
        return std::nullopt;
    }
    const double ratio = (time - timestamps[previous]) / span;
    const double interpolated = *previousValue + (*nextValue - *previousValue) * ratio;
    return std::isfinite(interpolated) ? std::optional<double>(interpolated) : std::nullopt;
}

QStringList TelemetrySession::channelNames() const
{
    QStringList names = channels.keys();
    names.sort(Qt::CaseInsensitive);
    return names;
}

QVector<QVector<QPointF>> TelemetrySession::sampledSegments(
    const QString &channelName,
    double rangeStart,
    double rangeEnd,
    const int maximumPoints,
    SampledSegmentsStatus *status) const
{
    if (status) *status = SampledSegmentsStatus::Ok;
    if (!std::isfinite(rangeStart) || !std::isfinite(rangeEnd) || maximumPoints < 2) {
        if (status) *status = SampledSegmentsStatus::InvalidRange;
        return {};
    }
    if (rangeStart > rangeEnd) {
        std::swap(rangeStart, rangeEnd);
    }
    const double span = rangeEnd - rangeStart;
    // Finite endpoints can still subtract to infinity. Reject before bucket
    // arithmetic can produce NaN and reach a floating-to-integer conversion.
    if (!std::isfinite(span)) {
        if (status) *status = SampledSegmentsStatus::InvalidRange;
        return {};
    }
    const QString resolved = aliases.value(channelName, channelName);
    const auto channelIterator = channels.constFind(resolved);
    if (channelIterator == channels.cend()) {
        if (status) *status = SampledSegmentsStatus::ChannelMissing;
        return {};
    }
    const TelemetryChannel &channel = channelIterator.value();
    if (channel.timestamps.size() != channel.values.size() || channel.timestamps.isEmpty()) {
        if (status) *status = SampledSegmentsStatus::ChannelMalformed;
        return {};
    }

    QVector<QVector<QPointF>> rawSegments;
    QVector<QPointF> current;
    const double gapThreshold = telemetryGapThreshold(channel);
    for (qsizetype index = 0; index < channel.timestamps.size(); ++index) {
        const double timestamp = channel.timestamps[index];
        const double value = channel.values[index];
        if (!std::isfinite(timestamp) || timestamp < rangeStart || timestamp > rangeEnd) {
            continue;
        }
        if (!std::isfinite(value)) {
            if (!current.isEmpty()) {
                rawSegments.append(std::move(current));
                current.clear();
            }
            continue;
        }
        if (!current.isEmpty() && gapThreshold > 0.0
            && timestamp - current.back().x() > gapThreshold) {
            rawSegments.append(std::move(current));
            current.clear();
        }
        current.append(QPointF(timestamp, value));
    }
    if (!current.isEmpty()) {
        rawSegments.append(std::move(current));
    }
    if (rawSegments.isEmpty()) {
        return {};
    }

    if (span <= 0.0) {
        return rawSegments;
    }

    QVector<QVector<QPointF>> result;
    result.reserve(rawSegments.size());
    for (const QVector<QPointF> &segment : rawSegments) {
        QVector<QPointF> reduced;
        int activeBucket = -1;
        QPointF minimum;
        QPointF maximum;
        const auto flushBucket = [&reduced, &minimum, &maximum, &activeBucket]() {
            if (activeBucket < 0) return;
            if (minimum.x() <= maximum.x()) {
                reduced.append(minimum);
                if (maximum != minimum) reduced.append(maximum);
            } else {
                reduced.append(maximum);
                reduced.append(minimum);
            }
        };
        for (const QPointF &point : segment) {
            const double bucketPosition = (point.x() - rangeStart) / span * maximumPoints;
            if (!std::isfinite(bucketPosition)) return {};
            const int bucket = static_cast<int>(std::clamp(
                bucketPosition, 0.0, static_cast<double>(maximumPoints - 1)));
            if (bucket != activeBucket) {
                flushBucket();
                activeBucket = bucket;
                minimum = point;
                maximum = point;
            } else {
                if (point.y() < minimum.y()) minimum = point;
                if (point.y() > maximum.y()) maximum = point;
            }
        }
        flushBucket();
        if (!reduced.isEmpty()) result.append(std::move(reduced));
    }
    qsizetype totalPoints = 0;
    for (const QVector<QPointF> &segment : result) totalPoints += segment.size();
    const qsizetype pointLimit = static_cast<qsizetype>(maximumPoints) * 2;
    if (totalPoints <= pointLimit) return result;

    struct Candidate {
        qsizetype segment;
        QPointF point;
    };
    QVector<Candidate> candidates;
    candidates.reserve(totalPoints);
    for (qsizetype segmentIndex = 0; segmentIndex < result.size(); ++segmentIndex) {
        for (const QPointF &point : result[segmentIndex]) candidates.append({segmentIndex, point});
    }
    QVector<bool> selected(candidates.size(), false);
    qsizetype minimumIndex = 0;
    qsizetype maximumIndex = 0;
    for (qsizetype index = 1; index < candidates.size(); ++index) {
        if (candidates[index].point.y() < candidates[minimumIndex].point.y()) minimumIndex = index;
        if (candidates[index].point.y() > candidates[maximumIndex].point.y()) maximumIndex = index;
    }
    selected[minimumIndex] = true;
    selected[maximumIndex] = true;
    const qsizetype uniformBudget = std::max<qsizetype>(1, pointLimit - 2);
    for (qsizetype slot = 0; slot < uniformBudget; ++slot) {
        const qsizetype index = uniformBudget == 1
            ? 0
            : slot * (candidates.size() - 1) / (uniformBudget - 1);
        selected[index] = true;
    }
    QVector<QVector<QPointF>> bounded;
    qsizetype previousSegment = -1;
    for (qsizetype index = 0; index < candidates.size(); ++index) {
        if (!selected[index]) continue;
        if (bounded.isEmpty() || candidates[index].segment != previousSegment)
            bounded.append(QVector<QPointF>{});
        bounded.back().append(candidates[index].point);
        previousSegment = candidates[index].segment;
    }
    return bounded;
}

std::optional<double> videoToTelemetryTime(const double videoTime, const SyncTransform &transform)
{
    if (!std::isfinite(videoTime) || !std::isfinite(transform.offset)
        || !std::isfinite(transform.timeScale) || transform.timeScale <= 0.0)
        return std::nullopt;
    const double telemetryTime = videoTime * transform.timeScale + transform.offset;
    return std::isfinite(telemetryTime) ? std::optional<double>(telemetryTime) : std::nullopt;
}

std::optional<double> telemetryToVideoTime(
    const double telemetryTime, const SyncTransform &transform)
{
    if (!std::isfinite(telemetryTime) || !std::isfinite(transform.offset)
        || !std::isfinite(transform.timeScale) || transform.timeScale <= 0.0) {
        return std::nullopt;
    }
    const double videoTime = (telemetryTime - transform.offset) / transform.timeScale;
    return std::isfinite(videoTime) ? std::optional<double>(videoTime) : std::nullopt;
}

void preferAcceleratorPedalForThrottle(TelemetrySession &session)
{
    static const QRegularExpression pedal(QStringLiteral("^accelerator.?(?:pedal|pos)"),
        QRegularExpression::CaseInsensitiveOption);
    for (const auto &name : session.channelNames()) {
        if (!pedal.match(name).hasMatch()) continue;
        const auto &values = session.channels[name].values;
        if (std::any_of(values.cbegin(), values.cend(), [](const float value) { return std::isfinite(value); })) {
            session.aliases.insert(QStringLiteral("throttle"), name);
            return;
        }
    }
}

} // namespace FlappedEar
