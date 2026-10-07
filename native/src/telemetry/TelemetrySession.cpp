#include "telemetry/TelemetrySession.h"

#include "telemetry/TelemetryGeometry.h"

#include <QRegularExpression>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace FlappedEar {

namespace {

quint64 bitsOf(const double value)
{
    return std::bit_cast<quint64>(value);
}

double medianInterval(const QVector<double> &timestamps)
{
    QVector<double> intervals;
    intervals.reserve(std::max<qsizetype>(0, timestamps.size() - 1));
    for (qsizetype index = 1; index < timestamps.size(); ++index) {
        const double interval = timestamps[index] - timestamps[index - 1];
        if (std::isfinite(interval) && interval > 0.0) intervals.append(interval);
    }
    if (intervals.isEmpty()) return 0.0;
    const auto middle = intervals.begin() + intervals.size() / 2;
    std::nth_element(intervals.begin(), middle, intervals.end());
    return *middle;
}

} // namespace

ChannelCadenceCache::ChannelCadenceCache(const ChannelCadenceCache &other)
{
    const std::lock_guard lock(other.m_mutex);
    m_entry = other.m_entry;
}

ChannelCadenceCache &ChannelCadenceCache::operator=(const ChannelCadenceCache &other)
{
    if (this == &other) return *this;
    Entry copy;
    {
        const std::lock_guard lock(other.m_mutex);
        copy = other.m_entry;
    }
    const std::lock_guard lock(m_mutex);
    m_entry = copy;
    return *this;
}

bool ChannelCadenceCache::matches(const Entry &entry, const QVector<double> &timestamps)
{
    return entry.valid && entry.data == timestamps.constData() && entry.size == timestamps.size()
        && (timestamps.isEmpty()
            || (entry.firstBits == bitsOf(timestamps.front()) && entry.lastBits == bitsOf(timestamps.back())));
}

double ChannelCadenceCache::baseInterval(const QVector<double> &timestamps) const
{
    const std::lock_guard lock(m_mutex);
    if (!matches(m_entry, timestamps)) {
        m_entry.interval = medianInterval(timestamps);
        m_entry.data = timestamps.constData();
        m_entry.size = timestamps.size();
        m_entry.firstBits = timestamps.isEmpty() ? 0 : bitsOf(timestamps.front());
        m_entry.lastBits = timestamps.isEmpty() ? 0 : bitsOf(timestamps.back());
        m_entry.valid = true;
        ++m_entry.computations;
    }
    return m_entry.interval;
}

bool ChannelCadenceCache::isCurrent(const QVector<double> &timestamps) const
{
    const std::lock_guard lock(m_mutex);
    return matches(m_entry, timestamps);
}

qsizetype ChannelCadenceCache::computations() const
{
    const std::lock_guard lock(m_mutex);
    return m_entry.computations;
}

TelemetryChannel::TelemetryChannel(QString name, QString unit, QVector<double> timestamps, QVector<float> values)
    : name(std::move(name))
    , unit(std::move(unit))
{
    setSamples(std::move(timestamps), std::move(values));
}

void TelemetryChannel::setSamples(QVector<double> timestamps, QVector<float> values)
{
    if (timestamps.size() != values.size())
        throw std::invalid_argument("A telemetry channel needs one value per timestamp.");
    for (qsizetype index = 0; index < timestamps.size(); ++index) {
        if (!std::isfinite(timestamps[index]) || (index > 0 && timestamps[index] <= timestamps[index - 1]))
            throw std::invalid_argument("Telemetry timestamps must be finite and strictly increasing.");
    }
    m_timestamps = std::move(timestamps);
    m_values = std::move(values);
    m_cadence = {};
}

void TelemetryChannel::appendSample(double time, float value)
{
    if (!std::isfinite(time) || (!m_timestamps.isEmpty() && time <= m_timestamps.constLast()))
        throw std::invalid_argument("Telemetry timestamps must be finite and strictly increasing.");
    m_timestamps.append(time);
    m_values.append(value);
    m_cadence = {};
}

void TelemetryChannel::setValue(qsizetype index, float value)
{
    if (index < 0 || index >= m_values.size()) throw std::out_of_range("Telemetry sample index is out of range.");
    m_values[index] = value;
}

void TelemetryChannel::reserve(qsizetype size)
{
    m_timestamps.reserve(size);
    m_values.reserve(size);
}

void TelemetryChannel::clear()
{
    m_timestamps.clear();
    m_values.clear();
    m_cadence = {};
}

double telemetryBaseInterval(const TelemetryChannel &channel)
{
    return channel.cadence().baseInterval(channel.timestamps());
}

double telemetryGapThreshold(const TelemetryChannel &channel, const double minimumSeconds)
{
    return std::max(std::max(0.0, minimumSeconds), telemetryBaseInterval(channel) * 3.0);
}

bool telemetryIsGap(const TelemetryChannel &channel, const double before, const double after, const double minimumSeconds)
{
    const double span = after - before;
    if (!(span > telemetryGapThreshold(channel, minimumSeconds))) return false;
    constexpr qsizetype window = 8;
    const auto &times = channel.timestamps();
    const auto medianOf = [&times](qsizetype first, const qsizetype last) {
        // The median of the positive intervals times[i + 1] - times[i], first <= i < last.
        std::array<double, window> intervals {};
        qsizetype count = 0;
        for (first = std::max<qsizetype>(first, 0); first < last && first + 1 < times.size(); ++first) {
            const double interval = times[first + 1] - times[first];
            if (interval > 0.0) intervals[count++] = interval;
        }
        if (count == 0) return 0.0;
        const auto middle = intervals.begin() + count / 2;
        std::nth_element(intervals.begin(), middle, intervals.begin() + count);
        return *middle;
    };
    const qsizetype beforeIndex = std::lower_bound(times.cbegin(), times.cend(), before) - times.cbegin();
    const qsizetype afterIndex = std::lower_bound(times.cbegin(), times.cend(), after) - times.cbegin();
    const double local = std::max(medianOf(beforeIndex - window, beforeIndex), medianOf(afterIndex, afterIndex + window));
    return span > local * 3.0;
}

void markImplausibleHeartRate(TelemetrySession &session)
{
    const auto found = session.channels.find(session.aliases.value(QStringLiteral("heartRate")));
    if (found == session.channels.end()) return;
    TelemetryChannel &channel = found.value();
    for (qsizetype index = 0; index < channel.sampleCount(); ++index) {
        const float value = channel.values()[index];
        if (std::isfinite(value) && (value < kHeartRateMinimumPlausible || value > kHeartRateMaximumPlausible))
            channel.setValue(index, std::numeric_limits<float>::quiet_NaN());
    }
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
    if (channel.timestamps().isEmpty() || channel.values().isEmpty() || !std::isfinite(time)) {
        return std::nullopt;
    }
    const auto &timestamps = channel.timestamps();
    const auto &values = channel.values();
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
    // KAN-157: the one gap rule. Between two samples that enclose a gap there
    // is no data in any mode: a held, nearest or interpolated value would
    // bridge a loss of signal.
    if (telemetryGapThreshold(channel) > 0.0 && telemetryIsGap(channel, timestamps[previous], timestamps[next])) {
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
    const double difference = *nextValue - *previousValue;
    const double interpolated = mode == InterpolationMode::Longitude && std::abs(difference) > 180.0
        ? wrapLongitudeDegrees(*previousValue + wrapLongitudeDegrees(difference) * ratio)
        : *previousValue + difference * ratio;
    return std::isfinite(interpolated) ? std::optional<double>(interpolated) : std::nullopt;
}

QStringList TelemetrySession::channelNames() const
{
    QStringList names = channels.keys();
    names.sort(Qt::CaseInsensitive);
    return names;
}

QVector<QVector<QPointF>> TelemetrySession::rawSegments(
    const QString &channelName,
    double rangeStart,
    double rangeEnd,
    SampledSegmentsStatus *status) const
{
    if (status) *status = SampledSegmentsStatus::Ok;
    if (!std::isfinite(rangeStart) || !std::isfinite(rangeEnd)) {
        if (status) *status = SampledSegmentsStatus::InvalidRange;
        return {};
    }
    if (rangeStart > rangeEnd) {
        std::swap(rangeStart, rangeEnd);
    }
    const QString resolved = aliases.value(channelName, channelName);
    const auto channelIterator = channels.constFind(resolved);
    if (channelIterator == channels.cend()) {
        if (status) *status = SampledSegmentsStatus::ChannelMissing;
        return {};
    }
    const TelemetryChannel &channel = channelIterator.value();
    if (channel.timestamps().size() != channel.values().size() || channel.timestamps().isEmpty()) {
        if (status) *status = SampledSegmentsStatus::ChannelMalformed;
        return {};
    }

    QVector<QVector<QPointF>> rawSegments;
    QVector<QPointF> current;
    const double gapThreshold = telemetryGapThreshold(channel);
    for (qsizetype index = 0; index < channel.timestamps().size(); ++index) {
        const double timestamp = channel.timestamps()[index];
        const double value = channel.values()[index];
        if (!std::isfinite(timestamp) || timestamp < rangeStart || timestamp > rangeEnd) {
            continue;
        }
        if (!std::isfinite(value)) {
            if (!current.isEmpty()) {
                rawSegments.append(std::exchange(current, {}));
            }
            continue;
        }
        if (!current.isEmpty() && gapThreshold > 0.0
            && telemetryIsGap(channel, current.back().x(), timestamp)) {
            rawSegments.append(std::exchange(current, {}));
        }
        current.append(QPointF(timestamp, value));
    }
    if (!current.isEmpty()) {
        rawSegments.append(std::move(current));
    }
    return rawSegments;
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
    const auto rawSegments = this->rawSegments(channelName, rangeStart, rangeEnd, status);
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
    // KAN-210: every segment keeps at least its first point, so a short burst
    // between gaps never disappears. When there are more segments than the
    // budget, a uniform choice of segments is kept and the caller is told.
    QVector<qsizetype> segmentStarts;
    segmentStarts.reserve(result.size());
    for (qsizetype index = 0; index < candidates.size(); ++index) {
        if (index == 0 || candidates[index].segment != candidates[index - 1].segment)
            segmentStarts.append(index);
    }
    if (segmentStarts.size() > pointLimit) {
        if (status) *status = SampledSegmentsStatus::SegmentsTruncated;
        for (qsizetype slot = 0; slot < pointLimit; ++slot)
            selected[segmentStarts[slot * (segmentStarts.size() - 1) / (pointLimit - 1)]] = true;
    } else {
        for (const qsizetype start : std::as_const(segmentStarts)) selected[start] = true;
        qsizetype minimumIndex = 0;
        qsizetype maximumIndex = 0;
        for (qsizetype index = 1; index < candidates.size(); ++index) {
            if (candidates[index].point.y() < candidates[minimumIndex].point.y()) minimumIndex = index;
            if (candidates[index].point.y() > candidates[maximumIndex].point.y()) maximumIndex = index;
        }
        selected[minimumIndex] = true;
        selected[maximumIndex] = true;
        const qsizetype uniformBudget = pointLimit - segmentStarts.size() - 2;
        for (qsizetype slot = 0; slot < uniformBudget; ++slot) {
            const qsizetype index = uniformBudget == 1
                ? 0
                : slot * (candidates.size() - 1) / (uniformBudget - 1);
            selected[index] = true;
        }
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
    const auto finiteSamples = [&session](const QString &name) {
        const auto channel = session.channels.constFind(name);
        if (channel == session.channels.cend()) return qsizetype(0);
        return qsizetype(std::count_if(channel->values().cbegin(), channel->values().cend(),
            [](const float value) { return std::isfinite(value); }));
    };
    // The pedal replaces the throttle only when it has at least half the
    // throttle's finite samples (KAN-230, Telemetry FET-207).
    const qsizetype throttleSamples = finiteSamples(session.aliases.value(QStringLiteral("throttle")));
    for (const auto &name : session.channelNames()) {
        if (!pedal.match(name).hasMatch()) continue;
        const qsizetype samples = finiteSamples(name);
        if (samples > 0 && 2 * samples >= throttleSamples) {
            session.aliases.insert(QStringLiteral("throttle"), name);
            return;
        }
    }
}

} // namespace FlappedEar
