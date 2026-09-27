// KAN-107: side-by-side A/B video. Each lap of the pair is shown from its own
// run's footage, at the same point of the shared track-progress axis: the
// progress maps to the lap's telemetry time (its projected trace), then
// through that run's own synchronization to its video timeline, and then to
// a chapter file and local time. Nothing here decodes video; the footage
// itself comes from the VideoLink, verified before use.

#include "app/AnalysisController.h"
#include "app/VideoLink.h"

#include <QUrl>

#include <cmath>

using namespace FlappedEar;

namespace {

std::optional<double> videoSeconds(const VideoLink::RunVideo &video, const double telemetrySeconds)
{
    if (!std::isfinite(telemetrySeconds) || !(video.timeScale > 0.0)) return std::nullopt;
    const double seconds = (telemetrySeconds - video.offset) / video.timeScale;
    return std::isfinite(seconds) ? std::optional<double>(seconds) : std::nullopt;
}

} // namespace

QVariantMap AnalysisController::comparisonVideo(const int slot)
{
    if (slot < 0 || slot > 1 || !m_videoLink) return {{"state", QStringLiteral("none")}};
    const auto runId = m_comparisonSlots[slot].row.value("runId").toString();
    if (runId.isEmpty()) return {{"state", QStringLiteral("none")}};
    auto video = m_videoLink->runVideo(runId);
    if (video.state == QLatin1String("none") || video.state == QLatin1String("unverified")) {
        m_videoLink->requestRunVideo(runId);
        video = m_videoLink->runVideo(runId);
    }
    return {{"state", video.state}, {"message", video.message}, {"chapters", video.chapters.size()}};
}

QVariantMap AnalysisController::comparisonVideoAtProgress(const int slot, const double progressMeters) const
{
    if (slot < 0 || slot > 1 || !m_videoLink || !comparisonPairReady()) return {};
    ensureComparisonProgressAxis();
    if (!m_comparisonProgressAxis.valid) return {};
    const auto &comparisonSlot = m_comparisonSlots[slot];
    const auto video = m_videoLink->runVideo(comparisonSlot.row.value("runId").toString());
    if (video.state != QLatin1String("ready") || video.chapters.isEmpty()) return {};
    const auto telemetry = timeAtProgress(m_comparisonProgressTraceCache[slot], progressMeters);
    const auto seconds = telemetry ? videoSeconds(video, *telemetry) : std::nullopt;
    if (!seconds || *seconds < 0.0) return {};
    for (int index = 0; index < video.chapters.size(); ++index) {
        const auto &chapter = video.chapters[index];
        if (*seconds < chapter.startSeconds || *seconds > chapter.startSeconds + chapter.durationSeconds) continue;
        if (!chapter.available) return {{"gap", true}, {"chapter", index}};
        return {{"url", QUrl::fromLocalFile(chapter.path)}, {"chapter", index},
            {"localMilliseconds", qRound64((*seconds - chapter.startSeconds) * 1000.0)}, {"gap", false}};
    }
    return {}; // beyond the footage
}

double AnalysisController::comparisonProgressForVideo(const int slot, const int chapter, const double localMilliseconds) const
{
    if (slot < 0 || slot > 1 || !m_videoLink || !comparisonPairReady()) return -1.0;
    ensureComparisonProgressAxis();
    if (!m_comparisonProgressAxis.valid) return -1.0;
    const auto &comparisonSlot = m_comparisonSlots[slot];
    const auto video = m_videoLink->runVideo(comparisonSlot.row.value("runId").toString());
    if (video.state != QLatin1String("ready") || chapter < 0 || chapter >= video.chapters.size() || !std::isfinite(localMilliseconds))
        return -1.0;
    const double seconds = video.chapters[chapter].startSeconds + localMilliseconds / 1000.0;
    const double telemetry = seconds * video.timeScale + video.offset;
    const double start = comparisonSlot.row.value("startTime").toDouble(), end = comparisonSlot.row.value("endTime").toDouble();
    if (!(telemetry >= start && telemetry <= end)) return -1.0;
    const auto progress = progressAtTime(m_comparisonProgressTraceCache[slot], telemetry);
    return progress ? *progress : -1.0;
}
