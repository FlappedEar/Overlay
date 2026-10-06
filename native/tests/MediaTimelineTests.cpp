// Multi-chapter media timeline (native/src/export/MediaTimeline.*, KAN-105):
// continuous time across chapters, exact boundaries, gaps for unavailable
// chapters, and malformed input; chapters combined into one export source
// (native/src/export/ChapterSource.*, KAN-106).

#include "export/ChapterSource.h"
#include "export/ExportEngine.h"
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
    void combinesChaptersIntoOneExportSource();
    void refusesChaptersThatDiffer();
    void writesExactConcatScripts();
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

namespace {
// A GoPro-like chapter: 59.94 fps, 1/60000 time base, AAC audio.
MediaInfo chapter(const QString &path, const qint64 frames)
{
    MediaInfo info;
    info.path = path;
    info.videoCodec = QStringLiteral("hevc");
    info.videoCodecProfile = QStringLiteral("Main");
    info.videoSize = info.codedVideoSize = QSize(3840, 2160);
    info.frameRate = info.averageFrameRate = {60000, 1001};
    info.timeBase = {1, 60000};
    info.pixelFormat = QStringLiteral("yuv420p");
    info.bitDepth = 8;
    info.colorPrimaries = info.colorTransfer = info.colorSpace = QStringLiteral("bt709");
    info.colorRange = QStringLiteral("tv");
    info.sourceColorClass = SourceColorClass::Sdr;
    info.videoStartKnown = true;
    info.videoFrameCount = frames;
    info.videoDurationTicks = frames * 1001;
    info.videoDuration = info.duration = static_cast<double>(frames) * 1001.0 / 60000.0;
    info.audioCodecs = {QStringLiteral("aac")};
    info.audioSampleRate = 48000;
    info.audioDuration = info.videoDuration;
    return info;
}
}

void MediaTimelineTests::combinesChaptersIntoOneExportSource()
{
    // Two full GoPro chapters (8:51.53 each) and a short last one.
    const auto combination = ChapterSource::combine(
        {chapter("/v/GX010091.MP4", 31860), chapter("/v/GX020091.MP4", 31860), chapter("/v/GX030091.MP4", 600)});
    QVERIFY2(combination.info, qPrintable(combination.error));
    const MediaInfo &info = *combination.info;
    QCOMPARE(info.videoFrameCount, qsizetype(64320));
    QCOMPARE(info.videoDurationTicks, qint64(64320) * 1001);
    QVERIFY(std::abs(info.videoDuration - 64320 * 1001.0 / 60000.0) < 1e-9);
    QCOMPARE(info.videoStartTicks, 0);
    QCOMPARE(info.audioStartTime, 0.0);
    QVERIFY(std::abs(info.audioDuration - info.videoDuration) < 1e-9);
    // The frame domain covers every chapter, matching the chapter timeline.
    const auto range = ExportEngine::fullVideoFrameRange(info, {60000, 1001});
    QVERIFY(range);
    QCOMPARE(range->lastFrame, 64319);
    const auto timeline = MediaTimeline::fromChapters({{"a", chapter("a", 31860).videoDuration},
        {"b", chapter("b", 31860).videoDuration}, {"c", chapter("c", 600).videoDuration}});
    QVERIFY(std::abs(timeline.durationSeconds() - info.videoDuration) < 1e-9);
    // Audio that ends early in the last chapter ends the combined track there.
    auto shortAudio = chapter("/v/GX030091.MP4", 600);
    shortAudio.audioDuration = 5.0;
    const auto shorter = ChapterSource::combine({chapter("/v/GX010091.MP4", 31860), shortAudio});
    QVERIFY(shorter.info);
    QVERIFY(std::abs(shorter.info->audioDuration - (31860 * 1001.0 / 60000.0 + 5.0)) < 1e-9);
}

void MediaTimelineTests::refusesChaptersThatDiffer()
{
    const auto first = chapter("/v/GX010091.MP4", 31860);
    QVERIFY(!ChapterSource::combine({first}).info);
    auto other = chapter("/v/GX020091.MP4", 600);
    other.frameRate = other.averageFrameRate = {30000, 1001};
    auto result = ChapterSource::combine({first, other});
    QVERIFY(!result.info);
    QVERIFY(result.error.contains(QStringLiteral("frame rate")));
    other = chapter("/v/GX020091.MP4", 600);
    other.videoSize = other.codedVideoSize = QSize(1920, 1080);
    QVERIFY(ChapterSource::combine({first, other}).error.contains(QStringLiteral("picture size")));
    other = chapter("/v/GX020091.MP4", 600);
    other.audioCodecs.clear();
    other.audioDuration = 0.0;
    QVERIFY(ChapterSource::combine({first, other}).error.contains(QStringLiteral("audio")));
    other = chapter("/v/GX020091.MP4", 600);
    other.videoDurationTicks = 0;
    QVERIFY(ChapterSource::combine({first, other}).error.contains(QStringLiteral("exact video duration")));
    other = chapter("/v/GX020091.MP4", 600);
    other.colorTransfer = QStringLiteral("arib-std-b67");
    QVERIFY(ChapterSource::combine({first, other}).error.contains(QStringLiteral("colour")));
}

void MediaTimelineTests::writesExactConcatScripts()
{
    auto second = chapter("/v/it's GX020091.MP4", 600);
    second.videoStartTicks = 1001;
    second.videoStartTime = 1001.0 / 60000.0;
    const QString script = ChapterSource::concatScript({chapter("/v/GX010091.MP4", 31860), second});
    QCOMPARE(script, QStringLiteral(
        "ffconcat version 1.0\n"
        "file 'file:/v/GX010091.MP4'\n"
        "outpoint 531.531000000\nduration 531.531000000\n"
        "file 'file:/v/it'\\''s GX020091.MP4'\n"
        "inpoint 0.016683333\n"
        "outpoint 10.026683333\nduration 10.010000000\n"));
    const QString url = ChapterSource::inputUrl({chapter("/v/GX010091.MP4", 31860), second});
    QVERIFY(url.startsWith(QStringLiteral("data:text/plain;base64,")));
    QCOMPARE(QString::fromUtf8(QByteArray::fromBase64(url.mid(23).toLatin1())), script);

    // A seek into the second chapter starts the script there, with an inpoint
    // after its own video start; the first chapter is left out.
    const QVector<MediaInfo> pair{chapter("/v/GX010091.MP4", 31860), second};
    const qint64 firstTicks = qint64(31860) * 1001;
    QCOMPARE(ChapterSource::concatScript(pair, firstTicks + 2002), QStringLiteral(
        "ffconcat version 1.0\n"
        "file 'file:/v/it'\\''s GX020091.MP4'\n"
        "inpoint 0.050050000\n"
        "outpoint 10.026683333\nduration 9.976633333\n"));
    QVERIFY(ChapterSource::concatScript(pair, firstTicks + 600 * 1001).isEmpty()); // past the end
    // A seek in the first chapter keeps both chapters and needs no first inpoint at zero.
    QVERIFY(ChapterSource::concatScript(pair, 0) == script);
    // The input reads from the seek point, rounded down to a tick, with
    // timestamps back on the chapter timeline and no concat seek.
    const QStringList arguments = ChapterSource::inputArguments(pair, QStringLiteral("531.55"));
    QCOMPARE(arguments.mid(0, 8), QStringList({"-copyts", "-itsoffset", "531.550000000", "-f", "concat", "-safe", "0", "-i"}));
    QVERIFY(!arguments.contains(QStringLiteral("-ss")));
    QCOMPARE(QString::fromUtf8(QByteArray::fromBase64(arguments.last().mid(23).toLatin1())),
             ChapterSource::concatScript(pair, firstTicks + 1140));
    QCOMPARE(ChapterSource::inputArguments(pair, QStringLiteral("0")).at(2), QStringLiteral("0.000000000"));
    QVERIFY(ChapterSource::inputArguments(pair, QStringLiteral("-1")).isEmpty());
    QVERIFY(ChapterSource::inputArguments(pair, QStringLiteral("9999")).isEmpty());
}

QTEST_GUILESS_MAIN(MediaTimelineTests)
#include "MediaTimelineTests.moc"
