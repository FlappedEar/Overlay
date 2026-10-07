#include "app/ChapterPlayback.h"

#include <QtGlobal>

#include <algorithm>

namespace FlappedEar::ChapterPlayback {

QUrl chapterSource(const MediaTimeline &timeline, const int chapter)
{
    if (chapter < 0 || chapter >= timeline.chapterCount()) return {};
    const auto &file = timeline.chapter(chapter);
    return file.available ? QUrl::fromLocalFile(file.path) : QUrl();
}

qint64 chapterStartMilliseconds(const MediaTimeline &timeline, const int chapter)
{
    if (chapter < 0 || chapter >= timeline.chapterCount()) return 0;
    return qRound64(timeline.chapterStartSeconds(chapter) * 1000.0);
}

QVariantList chapterList(const MediaTimeline &timeline, const QVector<ChapterLabel> &labels)
{
    QVariantList list;
    for (int index = 0; index < timeline.chapterCount(); ++index) {
        const ChapterLabel label = labels.value(index);
        list.append(QVariantMap{{"index", index},
            {"startMilliseconds", chapterStartMilliseconds(timeline, index)},
            {"durationMilliseconds", qRound64(timeline.chapter(index).durationSeconds * 1000.0)},
            {"available", timeline.chapter(index).available},
            {"url", chapterSource(timeline, index)},
            {"name", label.name}, {"problem", label.problem}});
    }
    return list;
}

QVariantMap locate(const MediaTimeline &timeline, const qint64 timelineMilliseconds)
{
    const auto position = timeline.locate(std::clamp(timelineMilliseconds / 1000.0, 0.0, timeline.durationSeconds()));
    if (!position) return {};
    return {{"chapter", position->chapter}, {"localMilliseconds", qRound64(position->localSeconds * 1000.0)},
            {"gap", position->gap}};
}

} // namespace FlappedEar::ChapterPlayback
