#include "gopro/GoProChapters.h"

#include <QFileInfo>
#include <QMap>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>

namespace FlappedEar {
namespace {

void addIssue(ChapterGroup &group, const char *issue)
{
    const auto key = QString::fromLatin1(issue);
    if (!group.issues.contains(key)) group.issues.append(key);
}

// Checks that depend on the order: format against the first chapter, and
// each chapter's creation time against the previous chapter's end.
void checkOrder(ChapterGroup &group)
{
    for (const auto *issue : {chapterIncompatible, chapterOrderConflict, chapterTimingGap, chapterOrderFromNames, chapterUnreadable})
        group.issues.removeAll(QString::fromLatin1(issue));
    group.totalDuration = 0.0;
    if (group.chapters.isEmpty()) return;
    const auto *reference = static_cast<const ChapterCandidate *>(nullptr);
    for (const auto &entry : group.chapters) {
        if (!entry.file.probed) { addIssue(group, chapterUnreadable); continue; }
        group.totalDuration += entry.file.duration;
        if (!reference) { reference = &entry.file; continue; }
        if (entry.file.codec != reference->codec || entry.file.size != reference->size
            || std::abs(entry.file.frameRate - reference->frameRate) > 0.01)
            addIssue(group, chapterIncompatible);
    }
    if (group.chapters.size() < 2) return;
    bool verified = false;
    for (qsizetype index = 1; index < group.chapters.size(); ++index) {
        const auto &previous = group.chapters[index - 1].file, &current = group.chapters[index].file;
        if (!previous.probed || !current.probed || !previous.creationTime.isValid() || !current.creationTime.isValid()
            || previous.creationTime == current.creationTime) // some cameras stamp every chapter alike
            continue;
        verified = true;
        const double expected = previous.duration;
        const double actual = previous.creationTime.msecsTo(current.creationTime) / 1000.0;
        if (actual < expected - chapterTimingToleranceSeconds) addIssue(group, chapterOrderConflict);
        else if (actual > expected + chapterTimingToleranceSeconds) addIssue(group, chapterTimingGap);
    }
    if (!verified) addIssue(group, chapterOrderFromNames);
}

} // namespace

bool ChapterGroup::needsReview() const
{
    // Order known from the names alone is normal for many cameras; everything
    // else needs a look before the group is used.
    for (const auto &issue : issues)
        if (issue != QLatin1String(chapterOrderFromNames)) return true;
    return false;
}

std::optional<ChapterName> parseGoProChapterName(const QString &fileName)
{
    static const QRegularExpression modern(QStringLiteral("^(G[XHLS])(\\d{2})(\\d{4})\\.MP4$"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression legacyFirst(QStringLiteral("^GOPR(\\d{4})\\.MP4$"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression legacyNext(QStringLiteral("^GP(\\d{2})(\\d{4})\\.MP4$"), QRegularExpression::CaseInsensitiveOption);
    if (const auto match = modern.match(fileName); match.hasMatch()) {
        const int chapter = match.captured(2).toInt();
        if (chapter < 1) return std::nullopt;
        return ChapterName{match.captured(1).toUpper() + match.captured(3), chapter};
    }
    if (const auto match = legacyFirst.match(fileName); match.hasMatch())
        return ChapterName{QStringLiteral("GOPR") + match.captured(1), 1};
    if (const auto match = legacyNext.match(fileName); match.hasMatch()) {
        const int chapter = match.captured(1).toInt();
        if (chapter < 1) return std::nullopt;
        return ChapterName{QStringLiteral("GOPR") + match.captured(2), chapter + 1};
    }
    return std::nullopt;
}

QVector<ChapterGroup> proposeChapterGroups(const QVector<ChapterCandidate> &candidates)
{
    QMap<QString, ChapterGroup> goPro;
    QVector<ChapterGroup> ordinary;
    for (const auto &candidate : candidates) {
        const auto name = parseGoProChapterName(QFileInfo(candidate.path).fileName());
        if (!name) {
            ChapterGroup group;
            group.key = QFileInfo(candidate.path).fileName();
            group.chapters.append({candidate, 0});
            if (!candidate.probed) addIssue(group, chapterUnreadable);
            group.totalDuration = candidate.probed ? candidate.duration : 0.0;
            ordinary.append(group);
            continue;
        }
        auto &group = goPro[name->key];
        group.key = name->key;
        group.goPro = true;
        const bool seen = std::any_of(group.chapters.cbegin(), group.chapters.cend(),
            [&](const ChapterEntry &entry) { return entry.chapter == name->chapter; });
        (seen ? group.duplicates : group.chapters).append({candidate, name->chapter});
    }
    QVector<ChapterGroup> result;
    for (auto group : goPro) {
        std::stable_sort(group.chapters.begin(), group.chapters.end(),
            [](const ChapterEntry &a, const ChapterEntry &b) { return a.chapter < b.chapter; });
        if (!group.duplicates.isEmpty()) addIssue(group, chapterDuplicate);
        const int last = group.chapters.last().chapter;
        for (int chapter = 1; chapter <= last; ++chapter)
            if (std::none_of(group.chapters.cbegin(), group.chapters.cend(),
                    [chapter](const ChapterEntry &entry) { return entry.chapter == chapter; }))
                group.missingChapters.append(chapter);
        if (!group.missingChapters.isEmpty()) addIssue(group, chapterMissing);
        checkOrder(group);
        result.append(group);
    }
    return result + ordinary;
}

std::optional<ChapterGroup> reorderChapterGroup(const ChapterGroup &group, const QStringList &orderedPaths)
{
    if (orderedPaths.size() != group.chapters.size()) return std::nullopt;
    ChapterGroup reordered = group;
    reordered.chapters.clear();
    for (const auto &path : orderedPaths) {
        const auto found = std::find_if(group.chapters.cbegin(), group.chapters.cend(),
            [&](const ChapterEntry &entry) { return entry.file.path == path; });
        if (found == group.chapters.cend()
            || std::any_of(reordered.chapters.cbegin(), reordered.chapters.cend(),
                   [&](const ChapterEntry &entry) { return entry.file.path == path; }))
            return std::nullopt;
        reordered.chapters.append(*found);
    }
    QStringList proposed;
    for (const auto &entry : group.chapters) proposed.append(entry.file.path);
    reordered.manualOrder = group.manualOrder || orderedPaths != proposed;
    checkOrder(reordered);
    return reordered;
}

} // namespace FlappedEar
