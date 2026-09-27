// Multi-chapter media timeline (native/src/export/MediaTimeline.*, KAN-105):
// continuous time across chapters, exact boundaries, gaps for unavailable
// chapters, and malformed input.

#include "export/MediaTimeline.h"

#include <QtTest>
#include <cmath>
#include <limits>

using namespace FlappedEar;

class MediaTimelineTests : public QObject {
    Q_OBJECT

private slots:
    void mapsTimeAcrossChapters();
    void keepsUnavailableChaptersAsGaps();
    void rejectsMalformedChapters();
};

void MediaTimelineTests::mapsTimeAcrossChapters()
{
    const auto timeline = MediaTimeline::fromChapters({{"/a/GX010123.MP4", 530.53}, {"/a/GX020123.MP4", 530.53}, {"/a/GX030123.MP4", 100.1}});
    QVERIFY(timeline.isValid());
    QCOMPARE(timeline.chapterCount(), 3);
    QVERIFY(std::abs(timeline.durationSeconds() - 1161.16) < 1e-9);
    QCOMPARE(timeline.chapterStartSeconds(1), 530.53);
    // Inside the second chapter.
    auto position = timeline.locate(600.0);
    QVERIFY(position);
    QCOMPARE(position->chapter, 1);
    QVERIFY(std::abs(position->localSeconds - 69.47) < 1e-9);
    QVERIFY(!position->gap);
    // A chapter's exact start belongs to it; the timeline's end to the last one.
    QCOMPARE(timeline.locate(530.53)->chapter, 1);
    QCOMPARE(timeline.locate(530.53)->localSeconds, 0.0);
    QCOMPARE(timeline.locate(0.0)->chapter, 0);
    QCOMPARE(timeline.locate(timeline.durationSeconds())->chapter, 2);
    QVERIFY(std::abs(timeline.locate(timeline.durationSeconds())->localSeconds - 100.1) < 1e-9);
    QVERIFY(!timeline.locate(-0.001));
    QVERIFY(!timeline.locate(timeline.durationSeconds() + 0.001));
    QVERIFY(!timeline.locate(std::numeric_limits<double>::quiet_NaN()));
    // Back to the timeline: continuous, never reset at a boundary.
    QCOMPARE(*timeline.timelineSeconds(1, 69.47), 600.0);
    QCOMPARE(*timeline.timelineSeconds(2, 0.0), 1061.06);
    QCOMPARE(*timeline.timelineSeconds(0, 999.0), 530.53); // clamped to the chapter
    QVERIFY(!timeline.timelineSeconds(3, 0.0));
    QVERIFY(!timeline.timelineSeconds(-1, 0.0));
    for (double t = 0.0; t <= timeline.durationSeconds(); t += 37.3) {
        const auto located = timeline.locate(t);
        QVERIFY(std::abs(*timeline.timelineSeconds(located->chapter, located->localSeconds) - t) < 1e-9);
    }
}

void MediaTimelineTests::keepsUnavailableChaptersAsGaps()
{
    const auto timeline = MediaTimeline::fromChapters({{"/a/GX010123.MP4", 530.0}, {{}, 530.0, false}, {"/a/GX030123.MP4", 100.0}});
    QVERIFY(timeline.isValid());
    QVERIFY(timeline.hasGaps());
    QCOMPARE(timeline.durationSeconds(), 1160.0); // the missing chapter keeps its time
    QVERIFY(timeline.locate(700.0)->gap);
    QCOMPARE(timeline.locate(700.0)->chapter, 1);
    QVERIFY(!timeline.locate(1100.0)->gap);
    QCOMPARE(*timeline.timelineSeconds(2, 10.0), 1070.0);
}

void MediaTimelineTests::rejectsMalformedChapters()
{
    QVERIFY(!MediaTimeline::fromChapters({}).isValid());
    QVERIFY(!MediaTimeline::fromChapters({{"/a", 0.0}}).isValid());
    QVERIFY(!MediaTimeline::fromChapters({{"/a", -1.0}}).isValid());
    QVERIFY(!MediaTimeline::fromChapters({{"/a", std::numeric_limits<double>::infinity()}}).isValid());
    QVERIFY(!MediaTimeline::fromChapters({{"/a", std::numeric_limits<double>::quiet_NaN()}}).isValid());
    QVERIFY(!MediaTimeline::fromChapters({{"/a", std::numeric_limits<double>::max()}, {"/b", std::numeric_limits<double>::max()}}).isValid());
    QVector<TimelineChapter> many(MediaTimeline::maximumChapters + 1, TimelineChapter{"/a", 1.0});
    QVERIFY(!MediaTimeline::fromChapters(many).isValid());
    many.removeLast();
    QVERIFY(MediaTimeline::fromChapters(many).isValid());
    const MediaTimeline empty;
    QVERIFY(!empty.locate(0.0));
    QCOMPARE(empty.durationSeconds(), 0.0);
}

QTEST_GUILESS_MAIN(MediaTimelineTests)
#include "MediaTimelineTests.moc"
