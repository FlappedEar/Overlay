#include "app/LapNavigation.h"

#include <QVariantMap>
#include <QVector>

namespace FlappedEar::LapNavigation {

QString timingStatus(const LapSession &laps, const bool telemetryOpen)
{
    if (!telemetryOpen) return QStringLiteral("Open telemetry for lap timing");
    switch (laps.status) {
    case LapSessionStatus::Available:
        return QStringLiteral("%1 complete lap%2")
            .arg(laps.timedLaps.size())
            .arg(laps.timedLaps.size() == 1 ? QString() : QStringLiteral("s"));
    case LapSessionStatus::NoSourceStartGate:
        return QStringLiteral("No Start gate in telemetry");
    case LapSessionStatus::AmbiguousSourceStartGate:
        return QStringLiteral("Multiple Start gates in telemetry");
    case LapSessionStatus::InvalidGate:
        return QStringLiteral("Start gate geometry is invalid");
    case LapSessionStatus::NoUsableGps:
        return QStringLiteral("No usable GPS for lap timing");
    case LapSessionStatus::NoAcceptedPasses:
        return QStringLiteral("No Start-line passages detected");
    case LapSessionStatus::InsufficientPasses:
        return QStringLiteral("One Start-line passage detected; no complete lap");
    }
    return QStringLiteral("Lap timing unavailable");
}

QVariantList summaries(const LapSession &laps, const QString &runId)
{
    if (laps.status != LapSessionStatus::Available) return {};
    QVariantList summaries;
    summaries.reserve(laps.timedLaps.size());
    for (qsizetype index = 0; index < laps.timedLaps.size(); ++index) {
        const TimedLap &lap = laps.timedLaps[index];
        summaries.append(QVariantMap{
            {QStringLiteral("runId"), runId},
            {QStringLiteral("number"), lap.number},
            {QStringLiteral("startTelemetryTime"), lap.startTelemetryTime},
            {QStringLiteral("durationSeconds"), lap.durationSeconds},
            {QStringLiteral("hasDelta"), lap.referenceEligible() && laps.fastestLapIndex.has_value()},
            {QStringLiteral("referenceEligible"), lap.referenceEligible()},
            {QStringLiteral("exclusionReason"), lap.userExclusionReason},
            {QStringLiteral("referenceIssue"), lap.referenceIssue == LapReferenceIssue::GpsGap
                ? QStringLiteral("GPS gap") : lap.referenceIssue == LapReferenceIssue::InvalidGps
                    ? QStringLiteral("Invalid GPS") : lap.referenceIssue == LapReferenceIssue::ImplausibleLap
                        ? QStringLiteral("Implausible lap") : QString()},
            {QStringLiteral("deltaToBestSeconds"), lap.referenceEligible() && laps.fastestLapIndex
                ? QVariant(lap.deltaToBestSeconds) : QVariant()},
            {QStringLiteral("isBest"), laps.fastestLapIndex
                    && *laps.fastestLapIndex == index},
        });
    }
    return summaries;
}

QVariantList segments(const LapSession &laps, const PreviewTimeline &timeline,
                      const std::function<qint64(double)> &videoMilliseconds)
{
    if (laps.status != LapSessionStatus::Available
        || laps.timedLaps.isEmpty() || timeline.endPositionMilliseconds() <= 0) {
        return {};
    }

    struct VideoLap final {
        const TimedLap *lap = nullptr;
        qsizetype lapIndex = 0;
        qint64 startMilliseconds = -1;
        qint64 endMilliseconds = -1;
    };
    QVector<VideoLap> videoLaps;
    videoLaps.reserve(laps.timedLaps.size());
    for (qsizetype index = 0; index < laps.timedLaps.size(); ++index) {
        const TimedLap &lap = laps.timedLaps[index];
        const qint64 start = videoMilliseconds(lap.startTelemetryTime);
        const qint64 end = videoMilliseconds(lap.startTelemetryTime + lap.durationSeconds);
        if (start < 0 || end < start) continue;
        videoLaps.append({&lap, index, start, end});
    }
    if (videoLaps.isEmpty()) return {};

    const qint64 videoEnd = timeline.endPositionMilliseconds();
    QVariantList segments;
    const auto appendFragment = [&laps, &timeline, &segments](const QString &kind, const QString &label,
                                                   const qint64 start, const qint64 end,
                                                   const TimedLap *lap = nullptr,
                                                   const bool isBest = false) {
        if (start < 0 || end < start) return;
        QVariantMap segment{{QStringLiteral("kind"), kind}, {QStringLiteral("label"), label},
                            {QStringLiteral("startMilliseconds"), start},
                            {QStringLiteral("endMilliseconds"), end},
                            {QStringLiteral("durationMilliseconds"), end - start},
                            {QStringLiteral("startTimecode"), timeline.timecodeForPositionMilliseconds(start)},
                            {QStringLiteral("endTimecode"), timeline.timecodeForPositionMilliseconds(end)},
                            {QStringLiteral("seekMilliseconds"), start}};
        if (lap) {
            segment.insert(QStringLiteral("number"), lap->number);
            segment.insert(QStringLiteral("durationSeconds"), lap->durationSeconds);
            const bool hasDelta = lap->referenceEligible() && laps.fastestLapIndex.has_value();
            segment.insert(QStringLiteral("hasDelta"), hasDelta);
            segment.insert(QStringLiteral("referenceEligible"), lap->referenceEligible());
            segment.insert(QStringLiteral("deltaToBestSeconds"), hasDelta ? QVariant(lap->deltaToBestSeconds) : QVariant());
            segment.insert(QStringLiteral("isBest"), isBest);
        }
        segments.append(segment);
    };

    const VideoLap &first = videoLaps.constFirst();
    if (first.startMilliseconds > 0) {
        appendFragment(QStringLiteral("outlap"), QStringLiteral("Out lap"), 0, first.startMilliseconds);
    }
    for (const VideoLap &videoLap : videoLaps) {
        appendFragment(QStringLiteral("lap"), QStringLiteral("Lap %1").arg(videoLap.lap->number),
                       videoLap.startMilliseconds, videoLap.endMilliseconds, videoLap.lap,
                       laps.fastestLapIndex && *laps.fastestLapIndex == videoLap.lapIndex);
    }
    const VideoLap &last = videoLaps.constLast();
    if (last.endMilliseconds < videoEnd) {
        appendFragment(QStringLiteral("inlap"), QStringLiteral("In lap"), last.endMilliseconds, videoEnd);
    }
    return segments;
}

int lapNumberAt(const LapSession &laps, const std::optional<double> telemetryTime)
{
    if (!telemetryTime) return 0;
    for (const auto &lap : laps.timedLaps)
        if (*telemetryTime >= lap.startTelemetryTime && *telemetryTime < lap.endTelemetryTime) return lap.number;
    return 0;
}

} // namespace FlappedEar::LapNavigation
