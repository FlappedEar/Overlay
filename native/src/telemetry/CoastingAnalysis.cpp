#include "telemetry/CoastingAnalysis.h"

#include <QJsonObject>

#include <algorithm>
#include <cmath>
#include <iterator>

namespace FlappedEar {
namespace {

bool contains(const QJsonObject &segment, const double progress)
{
    const double start = segment.value("startProgressMeters").toDouble();
    const double end = segment.value("endProgressMeters").toDouble();
    // A segment across start/finish has end < start.
    return start <= end ? progress >= start && progress < end : progress >= start || progress < end;
}

} // namespace

CoastingSummary summarizeCoasting(const TelemetrySession &session, const double startTime, const double endTime,
    const QVector<ProgressSegment> *lapTrace, const ApprovedSegmentation *approved, const DrivingStateOptions &options)
{
    CoastingSummary summary;
    const auto states = classifyDrivingStates(session, startTime, endTime, options);
    if (!states.valid) return summary;
    summary.valid = true;
    summary.lapSeconds = endTime - startTime;
    summary.provenance = states.coasting.provenance;
    summary.unresolvedReason = states.coasting.unresolvedReason;
    for (const auto &known : states.coasting.known) summary.knownSeconds += known.end - known.start;
    QVector<QJsonObject> segments;
    if (approved) {
        for (const auto &value : approved->segments) {
            const auto segment = value.toObject();
            segments.append(segment);
            summary.segments.append({segment.value("id").toString(), segment.value("name").toString(),
                                     segment.value("type").toString()});
        }
    }
    const auto segmentAt = [&](const double time) -> qsizetype {
        if (!lapTrace || segments.isEmpty()) return -1;
        const auto progress = progressAtTime(*lapTrace, time);
        if (!progress) return -1;
        for (qsizetype i = 0; i < segments.size(); ++i) if (contains(segments[i], *progress)) return i;
        return -1;
    };
    const auto speedName = session.aliases.value(QStringLiteral("speed"));
    const auto speedChannel = session.channels.constFind(speedName);
    for (const auto &interval : states.coasting.active) {
        CoastingEpisode episode;
        episode.startTime = interval.start;
        episode.endTime = interval.end;
        episode.seconds = interval.end - interval.start;
        if (lapTrace) {
            episode.startProgressMeters = progressAtTime(*lapTrace, interval.start);
            episode.endProgressMeters = progressAtTime(*lapTrace, interval.end);
        }
        const auto startSegment = segmentAt(interval.start);
        if (startSegment >= 0) {
            episode.segmentId = summary.segments[startSegment].segmentId;
            ++summary.segments[startSegment].episodes;
        }
        // Distance and segment shares from the speed samples inside the episode.
        if (speedChannel != session.channels.cend()) {
            const auto &times = speedChannel->timestamps;
            const auto &values = speedChannel->values;
            auto index = std::distance(times.cbegin(), std::lower_bound(times.cbegin(), times.cend(), interval.start));
            for (; index + 1 < times.size() && times[index + 1] <= interval.end; ++index) {
                const double a = values[index], b = values[index + 1];
                if (!std::isfinite(a) || !std::isfinite(b)) continue;
                const double seconds = times[index + 1] - times[index];
                const double meters = (a + b) / 2.0 / 3.6 * seconds;
                episode.meters += meters;
                const auto segment = segmentAt((times[index] + times[index + 1]) / 2.0);
                if (segment >= 0) {
                    summary.segments[segment].seconds += seconds;
                    summary.segments[segment].meters += meters;
                }
            }
        }
        summary.coastingSeconds += episode.seconds;
        summary.coastingMeters += episode.meters;
        summary.episodes.append(episode);
    }
    return summary;
}

} // namespace FlappedEar
