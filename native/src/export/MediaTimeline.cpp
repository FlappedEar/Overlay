#include "export/MediaTimeline.h"

#include <algorithm>
#include <cmath>

namespace FlappedEar {

MediaTimeline MediaTimeline::fromChapters(const QVector<TimelineChapter> &chapters)
{
    MediaTimeline timeline;
    if (chapters.isEmpty() || chapters.size() > maximumChapters) return timeline;
    double start = 0.0;
    QVector<double> starts;
    for (qsizetype index = 0; index < chapters.size(); ++index) {
        const auto &chapter = chapters[index];
        if (!std::isfinite(chapter.durationSeconds) || chapter.durationSeconds <= 0.0) return timeline;
        if (!std::isfinite(chapter.gapBeforeSeconds) || chapter.gapBeforeSeconds < 0.0
            || (index == 0 && chapter.gapBeforeSeconds != 0.0))
            return timeline;
        start += chapter.gapBeforeSeconds;
        starts.append(start);
        start += chapter.durationSeconds;
    }
    if (!std::isfinite(start)) return timeline;
    starts.append(start);
    timeline.m_chapters = chapters;
    timeline.m_starts = starts;
    return timeline;
}

double MediaTimeline::chapterStartSeconds(const int index) const
{
    return index >= 0 && index < m_chapters.size() ? m_starts[index] : 0.0;
}

bool MediaTimeline::hasGaps() const
{
    return std::any_of(m_chapters.cbegin(), m_chapters.cend(), [](const TimelineChapter &chapter) { return !chapter.available; });
}

bool MediaTimeline::hasRecordedGaps() const
{
    return std::any_of(m_chapters.cbegin(), m_chapters.cend(), [](const TimelineChapter &chapter) { return chapter.gapBeforeSeconds > 0.0; });
}

std::optional<TimelinePosition> MediaTimeline::locate(const double timelineSeconds) const
{
    if (!isValid() || !std::isfinite(timelineSeconds) || timelineSeconds < 0.0 || timelineSeconds > durationSeconds())
        return std::nullopt;
    // The last chapter start at or before the time.
    const auto next = std::upper_bound(m_starts.cbegin(), m_starts.cend() - 1, timelineSeconds);
    const int index = std::max(0, static_cast<int>(std::distance(m_starts.cbegin(), next)) - 1);
    const auto &chapter = m_chapters[index];
    const double local = timelineSeconds - m_starts[index];
    // Past this chapter's end and before the next one's start: the recorded
    // gap before the next chapter (the end of the timeline stays here).
    if (local > chapter.durationSeconds && index + 1 < m_chapters.size())
        return TimelinePosition{index + 1, 0.0, true};
    return TimelinePosition{index, std::min(local, chapter.durationSeconds), !chapter.available};
}

std::optional<double> MediaTimeline::timelineSeconds(const int chapter, const double localSeconds) const
{
    if (chapter < 0 || chapter >= m_chapters.size() || !std::isfinite(localSeconds)) return std::nullopt;
    return m_starts[chapter] + std::clamp(localSeconds, 0.0, m_chapters[chapter].durationSeconds);
}

} // namespace FlappedEar
