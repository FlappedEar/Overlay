#pragma once

#include "export/MediaTimeline.h"

#include <QString>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

namespace FlappedEar {

// KAN-215: how the editor's preview plays a chaptered video (KAN-105) as one
// timeline: which chapter file to show, where it starts, the chapter list
// for the chapter bar, and the chapter and local time for a timeline
// position. Pure functions of the video's MediaTimeline; AppController keeps
// the selected chapter and the saved chapter references.
namespace ChapterPlayback {

// What the chapter list shows besides the timeline: the chapter's file name
// and why it is a gap, if it is one.
struct ChapterLabel {
    QString name;
    QString problem;
};

// The chapter's file, or an empty URL when it is a gap.
[[nodiscard]] QUrl chapterSource(const MediaTimeline &timeline, int chapter);
[[nodiscard]] qint64 chapterStartMilliseconds(const MediaTimeline &timeline, int chapter);
// One entry per chapter; labels is indexed like the chapters and may be short.
[[nodiscard]] QVariantList chapterList(const MediaTimeline &timeline, const QVector<ChapterLabel> &labels);
// {chapter, localMilliseconds, gap} for a timeline position, clamped to the
// timeline; empty when the timeline is invalid.
[[nodiscard]] QVariantMap locate(const MediaTimeline &timeline, qint64 timelineMilliseconds);

} // namespace ChapterPlayback

} // namespace FlappedEar
