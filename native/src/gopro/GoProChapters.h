#pragma once

#include <QDateTime>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QVector>

#include <optional>

namespace FlappedEar {

// KAN-104: GoPro splits one recording into chapter files. Their names carry
// the recording number and the chapter:
// - HERO6 and later: GXccnnnn / GHccnnnn / GLccnnnn / GSccnnnn.MP4,
//   cc = chapter from 01, nnnn = recording;
// - HERO5 and earlier: GOPRnnnn.MP4 is chapter 1, GPccnnnn.MP4 chapter cc + 1.
// Files are grouped by recording and ordered by chapter; the metadata then
// checks the proposal: matching format, and creation times that follow the
// previous chapter's end. Anything that does not fit is an explicit issue,
// never silently corrected. A file without a GoPro name is an ordinary video
// on its own.
inline constexpr auto goProChaptersAlgorithm = "gopro-chapters-v1";

// A file offered for grouping, with what probing it found.
struct ChapterCandidate {
    QString path;
    bool probed = false;       // false: unreadable; probeError says why
    QString probeError;
    double duration = 0.0;
    QString codec;
    QSize size;
    double frameRate = 0.0;
    QDateTime creationTime;    // invalid when absent
};

struct ChapterEntry {
    ChapterCandidate file;
    int chapter = 0;           // 1-based; 0 for an ordinary video
};

// Issue keys, in the order they are checked.
inline constexpr auto chapterMissing = "missingChapter";       // a gap in the numbers, or no first chapter
inline constexpr auto chapterDuplicate = "duplicateChapter";   // the same chapter more than once
inline constexpr auto chapterUnreadable = "unreadable";        // a chapter could not be probed
inline constexpr auto chapterIncompatible = "incompatibleFormat"; // codec, size or frame rate differs
inline constexpr auto chapterOrderConflict = "orderConflict";  // a chapter was created before the previous one ended
inline constexpr auto chapterTimingGap = "timingGap";          // a chapter starts well after the previous one ended
inline constexpr auto chapterOrderFromNames = "orderFromNamesOnly"; // no usable creation times: order is the names' alone

struct ChapterGroup {
    QString key;               // "GX0123", "GOPR0456"; the file name for an ordinary video
    bool goPro = false;
    QVector<ChapterEntry> chapters;   // proposed order
    QVector<ChapterEntry> duplicates; // left out: same chapter again
    QVector<int> missingChapters;
    QStringList issues;
    bool manualOrder = false;
    double totalDuration = 0.0;
    [[nodiscard]] bool needsReview() const;
};

// Chapter boundaries within this tolerance of the expected time agree.
inline constexpr double chapterTimingToleranceSeconds = 3.0;

struct ChapterName {
    QString key;
    int chapter = 0;
};
// The recording key and chapter a GoPro file name encodes, if it is one.
[[nodiscard]] std::optional<ChapterName> parseGoProChapterName(const QString &fileName);

// Groups in a stable order (GoPro recordings by key, then ordinary videos
// in the order given).
[[nodiscard]] QVector<ChapterGroup> proposeChapterGroups(const QVector<ChapterCandidate> &candidates);

// The group in the order the user chose (a permutation of its chapter
// paths), re-checked; nullopt when `orderedPaths` is not a permutation.
[[nodiscard]] std::optional<ChapterGroup> reorderChapterGroup(const ChapterGroup &group, const QStringList &orderedPaths);

} // namespace FlappedEar
