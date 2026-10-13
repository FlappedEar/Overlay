#pragma once

#include <QString>
#include <QVector>

#include <optional>

namespace FlappedEar {

// KAN-105: an ordered list of chapter files played as one time-based video.
// Timeline time runs continuously across chapters: a chapter starts where
// the previous one's recorded video ended (its probed stream duration), so
// telemetry time and synchronization never reset at a file boundary. A
// chapter whose file is unavailable keeps its recorded duration and is an
// explicit gap, never skipped or closed up.
struct TimelineChapter {
    QString path;              // empty when unavailable
    double durationSeconds = 0.0;
    bool available = true;
    // KAN-262: recorded time without video before this chapter (BetterDriver
    // starts a new clip some 0.6 to 1.5 s after the last ends). Unlike an
    // unavailable chapter it is not a missing file: nothing was recorded.
    double gapBeforeSeconds = 0.0;
};

struct TimelinePosition {
    int chapter = -1;
    double localSeconds = 0.0; // within the chapter
    bool gap = false;          // the chapter's file is unavailable
};

class MediaTimeline {
public:
    MediaTimeline() = default;
    // Invalid (empty) when any duration is non-finite or not positive, a gap
    // before a chapter is negative, non-finite or before the first chapter,
    // or there are more than maximumChapters.
    static MediaTimeline fromChapters(const QVector<TimelineChapter> &chapters);
    static constexpr int maximumChapters = 64;

    [[nodiscard]] bool isValid() const { return !m_chapters.isEmpty(); }
    [[nodiscard]] int chapterCount() const { return static_cast<int>(m_chapters.size()); }
    [[nodiscard]] const TimelineChapter &chapter(int index) const { return m_chapters[index]; }
    [[nodiscard]] double durationSeconds() const { return m_starts.isEmpty() ? 0.0 : m_starts.last(); }
    [[nodiscard]] double chapterStartSeconds(int index) const;
    [[nodiscard]] bool hasGaps() const;
    // KAN-262: any chapter has recorded time without video before it.
    [[nodiscard]] bool hasRecordedGaps() const;

    // The chapter and local time for a timeline time in [0, duration]; the
    // exact start of a chapter belongs to it, the end of the timeline to the
    // last chapter. A time in the recorded gap before a chapter is that
    // chapter at local time 0, flagged `gap` like a missing file (no picture).
    // None outside the timeline or when invalid.
    [[nodiscard]] std::optional<TimelinePosition> locate(double timelineSeconds) const;
    // The timeline time of a chapter's local time (clamped to the chapter).
    [[nodiscard]] std::optional<double> timelineSeconds(int chapter, double localSeconds) const;

private:
    QVector<TimelineChapter> m_chapters;
    QVector<double> m_starts; // chapter starts, then the total duration
};

} // namespace FlappedEar
