// GoPro chapter groups (native/src/gopro/GoProChapters.*, KAN-104): names
// propose the groups and order; metadata checks them; missing chapters,
// duplicates, incompatible formats, unreadable files and timing that does not
// follow are explicit issues; ordinary videos stay ordinary.

#include "export/MediaProbe.h"
#include "gopro/GoProChapters.h"

#include <QtTest>

using namespace FlappedEar;

namespace {

const QDateTime start = QDateTime::fromString("2026-08-29T10:00:00Z", Qt::ISODate);

ChapterCandidate chapter(const QString &path, const double duration, const double startOffset,
    const QString &codec = "hevc", const QSize size = {3840, 2160}, const double rate = 59.94)
{
    ChapterCandidate candidate;
    candidate.path = path;
    candidate.probed = true;
    candidate.duration = duration;
    candidate.codec = codec;
    candidate.size = size;
    candidate.frameRate = rate;
    candidate.creationTime = start.addMSecs(qRound64(startOffset * 1000.0));
    return candidate;
}

QStringList paths(const ChapterGroup &group)
{
    QStringList result;
    for (const auto &entry : group.chapters) result.append(entry.file.path);
    return result;
}

} // namespace

class GoProChaptersTests : public QObject {
    Q_OBJECT

private slots:
    void parsesModernAndLegacyNames();
    void groupsAndOrdersChapters();
    void reportsMissingAndDuplicateChapters();
    void reportsIncompatibleAndUnreadableChapters();
    void checksChapterTiming();
    void keepsOrdinaryVideosOrdinary();
    void reordersOnlyAPermutation();
    void readsTheCreationTimeTag();
};

void GoProChaptersTests::parsesModernAndLegacyNames()
{
    QCOMPARE(parseGoProChapterName("GX010123.MP4")->key, QString("GX0123"));
    QCOMPARE(parseGoProChapterName("GX010123.MP4")->chapter, 1);
    QCOMPARE(parseGoProChapterName("gh030456.mp4")->key, QString("GH0456"));
    QCOMPARE(parseGoProChapterName("gh030456.mp4")->chapter, 3);
    QCOMPARE(parseGoProChapterName("GOPR0789.MP4")->key, QString("GOPR0789"));
    QCOMPARE(parseGoProChapterName("GOPR0789.MP4")->chapter, 1);
    QCOMPARE(parseGoProChapterName("GP010789.MP4")->chapter, 2); // the second chapter
    QCOMPARE(parseGoProChapterName("GP010789.MP4")->key, QString("GOPR0789"));
    for (const auto *name : {"GX000123.MP4", "GX10123.MP4", "GX010123.MOV", "GX010123.MP4.bak", "onboard.mp4", "GXAB0123.MP4", ""})
        QVERIFY2(!parseGoProChapterName(QString::fromLatin1(name)), name);
}

void GoProChaptersTests::groupsAndOrdersChapters()
{
    // Offered out of order, and two recordings mixed together.
    const auto groups = proposeChapterGroups({
        chapter("/day/GX030123.MP4", 100.0, 1060.0), chapter("/day/GX010123.MP4", 530.0, 0.0),
        chapter("/day/GX010124.MP4", 60.0, 2000.0), chapter("/day/GX020123.MP4", 530.0, 530.0)});
    QCOMPARE(groups.size(), 2);
    QCOMPARE(groups[0].key, QString("GX0123"));
    QVERIFY(groups[0].goPro);
    QCOMPARE(paths(groups[0]), (QStringList{"/day/GX010123.MP4", "/day/GX020123.MP4", "/day/GX030123.MP4"}));
    QVERIFY(groups[0].issues.isEmpty());
    QVERIFY(!groups[0].needsReview());
    QCOMPARE(groups[0].totalDuration, 1160.0);
    QCOMPARE(groups[1].key, QString("GX0124"));
    QCOMPARE(groups[1].chapters.size(), 1);
    // Legacy naming joins GOPR and GP chapters.
    const auto legacy = proposeChapterGroups({chapter("/d/GP010789.MP4", 50.0, 600.0, "h264"), chapter("/d/GOPR0789.MP4", 600.0, 0.0, "h264")});
    QCOMPARE(legacy.size(), 1);
    QCOMPARE(paths(legacy[0]), (QStringList{"/d/GOPR0789.MP4", "/d/GP010789.MP4"}));
    QVERIFY(legacy[0].issues.isEmpty());
}

void GoProChaptersTests::reportsMissingAndDuplicateChapters()
{
    // Chapter 2 is missing; chapter 1 was copied twice.
    const auto groups = proposeChapterGroups({chapter("/a/GX010123.MP4", 530.0, 0.0), chapter("/b/GX010123.MP4", 530.0, 0.0),
        chapter("/a/GX030123.MP4", 100.0, 1060.0)});
    QCOMPARE(groups.size(), 1);
    const auto &group = groups.first();
    QCOMPARE(group.missingChapters, QVector<int>{2});
    QVERIFY(group.issues.contains(chapterMissing) && group.issues.contains(chapterDuplicate));
    QCOMPARE(group.duplicates.size(), 1);
    QCOMPARE(group.duplicates.first().file.path, QString("/b/GX010123.MP4"));
    QCOMPARE(group.chapters.size(), 2);
    QVERIFY(group.needsReview());
    // No first chapter at all.
    const auto late = proposeChapterGroups({chapter("/a/GX020123.MP4", 530.0, 530.0)});
    QCOMPARE(late.first().missingChapters, QVector<int>{1});
}

void GoProChaptersTests::reportsIncompatibleAndUnreadableChapters()
{
    auto broken = chapter("/a/GX020123.MP4", 0.0, 530.0);
    broken.probed = false;
    broken.probeError = "moov atom not found";
    const auto unreadable = proposeChapterGroups({chapter("/a/GX010123.MP4", 530.0, 0.0), broken});
    QVERIFY(unreadable.first().issues.contains(chapterUnreadable));
    QVERIFY(unreadable.first().needsReview());
    for (const auto &different : {chapter("/a/GX020123.MP4", 100.0, 530.0, "h264"),
             chapter("/a/GX020123.MP4", 100.0, 530.0, "hevc", {1920, 1080}),
             chapter("/a/GX020123.MP4", 100.0, 530.0, "hevc", {3840, 2160}, 29.97)}) {
        const auto groups = proposeChapterGroups({chapter("/a/GX010123.MP4", 530.0, 0.0), different});
        QVERIFY(groups.first().issues.contains(chapterIncompatible));
    }
}

void GoProChaptersTests::checksChapterTiming()
{
    // Chapter 2 created 60 s after chapter 1 ended: something is missing between.
    const auto gap = proposeChapterGroups({chapter("/a/GX010123.MP4", 530.0, 0.0), chapter("/a/GX020123.MP4", 100.0, 590.0)});
    QVERIFY(gap.first().issues.contains(chapterTimingGap));
    // Chapter 2 created before chapter 1 ended: the names and clocks disagree.
    const auto conflict = proposeChapterGroups({chapter("/a/GX010123.MP4", 530.0, 0.0), chapter("/a/GX020123.MP4", 100.0, 200.0)});
    QVERIFY(conflict.first().issues.contains(chapterOrderConflict));
    // Within tolerance: fine.
    QVERIFY(proposeChapterGroups({chapter("/a/GX010123.MP4", 530.0, 0.0), chapter("/a/GX020123.MP4", 100.0, 532.0)})
                .first().issues.isEmpty());
    // No creation times, or every chapter stamped alike: the order is the names' only.
    auto first = chapter("/a/GX010123.MP4", 530.0, 0.0), second = chapter("/a/GX020123.MP4", 100.0, 0.0);
    const auto alike = proposeChapterGroups({first, second});
    QCOMPARE(alike.first().issues, QStringList{chapterOrderFromNames});
    QVERIFY(!alike.first().needsReview());
    first.creationTime = {}; second.creationTime = {};
    QCOMPARE(proposeChapterGroups({first, second}).first().issues, QStringList{chapterOrderFromNames});
}

void GoProChaptersTests::keepsOrdinaryVideosOrdinary()
{
    auto unreadable = chapter("/a/broken.mov", 0.0, 0.0);
    unreadable.probed = false;
    const auto groups = proposeChapterGroups({chapter("/a/onboard.mp4", 900.0, 0.0, "h264"), unreadable,
        chapter("/a/GX010123.MP4", 530.0, 0.0)});
    QCOMPARE(groups.size(), 3);
    QCOMPARE(groups[0].key, QString("GX0123")); // GoPro recordings first
    QCOMPARE(groups[1].key, QString("onboard.mp4"));
    QVERIFY(!groups[1].goPro);
    QCOMPARE(groups[1].chapters.size(), 1);
    QCOMPARE(groups[1].chapters.first().chapter, 0);
    QVERIFY(groups[1].issues.isEmpty());
    QVERIFY(groups[2].issues.contains(chapterUnreadable));
    QVERIFY(proposeChapterGroups({}).isEmpty());
}

void GoProChaptersTests::reordersOnlyAPermutation()
{
    // Names say 1, 2 but the clocks say otherwise; the user swaps them.
    const auto groups = proposeChapterGroups({chapter("/a/GX010123.MP4", 100.0, 530.0), chapter("/a/GX020123.MP4", 530.0, 0.0)});
    QVERIFY(groups.first().issues.contains(chapterOrderConflict));
    const auto swapped = reorderChapterGroup(groups.first(), {"/a/GX020123.MP4", "/a/GX010123.MP4"});
    QVERIFY(swapped);
    QVERIFY(swapped->manualOrder);
    QVERIFY(swapped->issues.isEmpty()); // the clocks now agree
    QCOMPARE(paths(*swapped), (QStringList{"/a/GX020123.MP4", "/a/GX010123.MP4"}));
    const auto same = reorderChapterGroup(groups.first(), paths(groups.first()));
    QVERIFY(same && !same->manualOrder);
    QVERIFY(!reorderChapterGroup(groups.first(), {"/a/GX010123.MP4"}));
    QVERIFY(!reorderChapterGroup(groups.first(), {"/a/GX010123.MP4", "/a/GX010123.MP4"}));
    QVERIFY(!reorderChapterGroup(groups.first(), {"/a/GX010123.MP4", "/elsewhere.MP4"}));
}

void GoProChaptersTests::readsTheCreationTimeTag()
{
    const auto info = MediaProbe::parseJson(R"({"format": {"duration": "530.03", "tags": {"creation_time": "2026-08-29T10:08:50.000000Z"}},
        "streams": [{"codec_type": "video", "codec_name": "hevc", "width": 3840, "height": 2160, "r_frame_rate": "60000/1001"}]})", "/a/GX020123.MP4");
    QVERIFY(info.creationTime.isValid());
    QCOMPARE(info.creationTime, QDateTime::fromString("2026-08-29T10:08:50Z", Qt::ISODate));
    const auto untagged = MediaProbe::parseJson(R"({"format": {"duration": "10", "tags": {"creation_time": "yesterday"}},
        "streams": [{"codec_type": "video", "codec_name": "h264", "width": 1920, "height": 1080, "r_frame_rate": "30/1"}]})", "/a/x.mp4");
    QVERIFY(!untagged.creationTime.isValid());
}

QTEST_GUILESS_MAIN(GoProChaptersTests)
#include "GoProChaptersTests.moc"
