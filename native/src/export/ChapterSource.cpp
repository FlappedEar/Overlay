#include "export/ChapterSource.h"

#include "export/MediaTimeline.h"

#include <QFileInfo>

#include <algorithm>
#include <stdexcept>
#include <cmath>
#include <limits>

namespace FlappedEar {
namespace {

// `ticks * timeBase` seconds as an exact decimal string with nine places
// (FFmpeg reads concat durations to the microsecond).
std::optional<QString> tickSeconds(const qint64 ticks, const MediaRational &timeBase)
{
    if (ticks < 0 || !timeBase.isValid() || timeBase.numerator <= 0) return std::nullopt;
    qint64 scaled = 0;
    if (qMulOverflow(ticks, timeBase.numerator, &scaled)) return std::nullopt;
    const qint64 whole = scaled / timeBase.denominator;
    qint64 remainder = scaled % timeBase.denominator;
    QString fraction;
    for (int digit = 0; digit < 9; ++digit) {
        if (qMulOverflow(remainder, qint64(10), &remainder)) return std::nullopt;
        fraction += QChar('0' + static_cast<int>(remainder / timeBase.denominator));
        remainder %= timeBase.denominator;
    }
    return QStringLiteral("%1.%2").arg(whole).arg(fraction);
}

// A non-negative decimal number of seconds as whole ticks, rounded down.
std::optional<qint64> floorTicks(const QString &seconds, const MediaRational &timeBase)
{
    if (!timeBase.isValid()) return std::nullopt;
    const auto parts = seconds.split(QLatin1Char('.'));
    bool ok = false;
    const qint64 whole = parts.value(0).toLongLong(&ok);
    if (!ok || whole < 0 || parts.size() > 2) return std::nullopt;
    // Nanoseconds are enough: a tick is far longer.
    const QString digits = (parts.value(1) + QStringLiteral("000000000")).left(9);
    const qint64 nanos = parts.size() == 2 ? digits.toLongLong(&ok) : 0;
    if (!ok || nanos < 0) return std::nullopt;
    // seconds * den / num, in two exact steps.
    qint64 scaled = 0;
    if (qMulOverflow(whole, timeBase.denominator, &scaled)) return std::nullopt;
    const qint64 wholeTicks = scaled / timeBase.numerator;
    const qint64 remainder = scaled % timeBase.numerator;
    qint64 fraction = 0, carried = 0, divisor = 0;
    if (qMulOverflow(nanos, timeBase.denominator, &fraction)
        || qMulOverflow(remainder, qint64(1'000'000'000), &carried)
        || qAddOverflow(fraction, carried, &fraction)
        || qMulOverflow(timeBase.numerator, qint64(1'000'000'000), &divisor))
        return std::nullopt;
    return wholeTicks + fraction / divisor;
}

QString quoted(const QString &path)
{
    // ffconcat: a quote inside a quoted string is written as '\''.
    QString text = path;
    text.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
    return QStringLiteral("'%1'").arg(text);
}

// Equal values, or both unknown (an unset sample aspect ratio is 0/1).
bool sameRational(const MediaRational &a, const MediaRational &b)
{
    return a.isValid() == b.isValid() && (!a.isValid() || a.isEquivalentTo(b));
}

bool hasAudio(const MediaInfo &info)
{
    return !info.audioCodecs.isEmpty() && info.audioDuration > 0.0;
}

QString mismatch(const int chapter, const QString &what)
{
    return QStringLiteral("Chapter %1 has a different %2 from the first chapter, so the chapters cannot be exported as one video.")
        .arg(chapter + 1).arg(what);
}

} // namespace

ChapterSourceCombination ChapterSource::combine(const QVector<MediaInfo> &chapters)
{
    if (chapters.size() < 2 || chapters.size() > MediaTimeline::maximumChapters)
        return {std::nullopt, QStringLiteral("A chaptered export needs 2 to %1 chapters.").arg(MediaTimeline::maximumChapters)};
    const MediaInfo &first = chapters.first();
    const bool audio = hasAudio(first);
    MediaInfo combined = first;
    qint64 durationTicks = 0;
    qsizetype frames = 0;
    qsizetype packets = 0;
    double startOfLast = 0.0;
    for (int index = 0; index < chapters.size(); ++index) {
        const MediaInfo &chapter = chapters[index];
        if (chapter.videoDurationTicks <= 0 || !chapter.timeBase.isValid()
            || !std::isfinite(chapter.videoDuration) || chapter.videoDuration <= 0.0)
            return {std::nullopt, QStringLiteral("Chapter %1 has no exact video duration.").arg(index + 1)};
        if (!chapter.videoStartKnown || chapter.videoStartTicks < 0)
            return {std::nullopt, QStringLiteral("Chapter %1 has no usable video start.").arg(index + 1)};
        if (index > 0) {
            if (chapter.videoCodec != first.videoCodec || chapter.videoCodecProfile != first.videoCodecProfile)
                return {std::nullopt, mismatch(index, QStringLiteral("video codec"))};
            if (chapter.videoSize != first.videoSize || chapter.codedVideoSize != first.codedVideoSize)
                return {std::nullopt, mismatch(index, QStringLiteral("picture size"))};
            if (!sameRational(chapter.frameRate, first.frameRate)
                || !sameRational(chapter.averageFrameRate, first.averageFrameRate))
                return {std::nullopt, mismatch(index, QStringLiteral("frame rate"))};
            if (!chapter.timeBase.isEquivalentTo(first.timeBase))
                return {std::nullopt, mismatch(index, QStringLiteral("time base"))};
            if (chapter.pixelFormat != first.pixelFormat || chapter.bitDepth != first.bitDepth)
                return {std::nullopt, mismatch(index, QStringLiteral("pixel format"))};
            if (chapter.colorRange != first.colorRange || chapter.colorSpace != first.colorSpace
                || chapter.colorTransfer != first.colorTransfer || chapter.colorPrimaries != first.colorPrimaries
                || chapter.sourceColorClass != first.sourceColorClass)
                return {std::nullopt, mismatch(index, QStringLiteral("colour description"))};
            if (chapter.rotationDegrees != first.rotationDegrees
                || !sameRational(chapter.sampleAspectRatio, first.sampleAspectRatio))
                return {std::nullopt, mismatch(index, QStringLiteral("orientation or pixel shape"))};
            if (hasAudio(chapter) != audio || chapter.audioCodecs != first.audioCodecs
                || chapter.audioSampleRate != first.audioSampleRate)
                return {std::nullopt, mismatch(index, QStringLiteral("audio track"))};
        }
        if (index == chapters.size() - 1) startOfLast = static_cast<double>(durationTicks)
            * static_cast<double>(first.timeBase.numerator) / static_cast<double>(first.timeBase.denominator);
        if (qAddOverflow(durationTicks, chapter.videoDurationTicks, &durationTicks))
            return {std::nullopt, QStringLiteral("The chapters are too long to export together.")};
        // A chapter without a frame count makes the total unknown; the
        // exact duration then decides the frame domain.
        frames = (frames >= 0 && chapter.videoFrameCount > 0) ? frames + chapter.videoFrameCount : -1;
        packets = (packets >= 0 && chapter.videoPacketCount > 0) ? packets + chapter.videoPacketCount : -1;
        combined.likelyVariableFrameRate = combined.likelyVariableFrameRate || chapter.likelyVariableFrameRate;
    }
    const double total = static_cast<double>(durationTicks)
        * static_cast<double>(first.timeBase.numerator) / static_cast<double>(first.timeBase.denominator);
    combined.videoStartTicks = 0;
    combined.videoStartKnown = true;
    combined.videoStartTime = 0.0;
    combined.startTime = 0.0;
    combined.videoDurationTicks = durationTicks;
    combined.videoDuration = total;
    combined.duration = total;
    combined.videoFrameCount = std::max<qsizetype>(frames, 0);
    combined.videoPacketCount = std::max<qsizetype>(packets, 0);
    if (audio) {
        // Audio is cut with its chapter's video, so it runs from the first
        // chapter's audio start to the last chapter's audio end on the timeline.
        const MediaInfo &last = chapters.last();
        combined.audioStartTime = std::max(0.0, first.audioStartTime - first.videoStartTime);
        const double lastEnd = startOfLast + std::min(last.videoDuration,
            last.audioStartTime + last.audioDuration - last.videoStartTime);
        combined.audioDuration = std::max(0.0, lastEnd - combined.audioStartTime);
    }
    return {combined, {}};
}

QString ChapterSource::concatScript(const QVector<MediaInfo> &chapters, const qint64 seekTicks)
{
    if (seekTicks < 0) return {};
    QString script = QStringLiteral("ffconcat version 1.0\n");
    qint64 chapterStart = 0; // on the chapter timeline
    bool any = false;
    for (const auto &chapter : chapters) {
        qint64 chapterEnd = 0;
        if (qAddOverflow(chapterStart, chapter.videoDurationTicks, &chapterEnd)) return {};
        if (seekTicks >= chapterEnd) {
            chapterStart = chapterEnd;
            continue;
        }
        // Ticks skipped at the start of the chapter holding the seek point.
        const qint64 skipped = std::max<qint64>(0, seekTicks - chapterStart);
        qint64 inpointTicks = 0, endTicks = 0;
        if (qAddOverflow(chapter.videoStartTicks, skipped, &inpointTicks)
            || qAddOverflow(chapter.videoStartTicks, chapter.videoDurationTicks, &endTicks))
            return {};
        const auto inpoint = tickSeconds(inpointTicks, chapter.timeBase);
        const auto end = tickSeconds(endTicks, chapter.timeBase);
        const auto duration = tickSeconds(chapter.videoDurationTicks - skipped, chapter.timeBase);
        if (!inpoint || !end || !duration) return {};
        // An absolute file: URL, since the script itself is a data: URL.
        script += QStringLiteral("file %1\n").arg(quoted(QStringLiteral("file:")
            + QFileInfo(chapter.path).absoluteFilePath()));
        // Without inpoint the demuxer aligns the file's earliest stream, not
        // its video; state the video start whenever the two can differ.
        if (inpointTicks > 0 || chapter.startTime != chapter.videoStartTime)
            script += QStringLiteral("inpoint %1\n").arg(*inpoint);
        script += QStringLiteral("outpoint %1\nduration %2\n").arg(*end, *duration);
        chapterStart = chapterEnd;
        any = true;
    }
    return any ? script : QString();
}

QString ChapterSource::inputUrl(const QVector<MediaInfo> &chapters, const qint64 seekTicks)
{
    const QString script = concatScript(chapters, seekTicks);
    if (script.isEmpty()) return {};
    return QStringLiteral("data:text/plain;base64,")
        + QString::fromLatin1(script.toUtf8().toBase64());
}

QStringList ChapterSource::inputArguments(const QVector<MediaInfo> &chapters, const QString &seekSeconds)
{
    if (chapters.isEmpty()) return {};
    const MediaRational timeBase = chapters.first().timeBase;
    const auto seekTicks = floorTicks(seekSeconds, timeBase);
    if (!seekTicks) return {};
    const auto offset = tickSeconds(*seekTicks, timeBase);
    const QString url = inputUrl(chapters, *seekTicks);
    if (!offset || url.isEmpty()) return {};
    // safe=0: chapter paths are absolute.
    return {"-copyts", "-itsoffset", *offset, "-f", "concat", "-safe", "0", "-i", url};
}

ChapterSource::Probed ChapterSource::probe(const QStringList &paths,
                                           const QVector<qint64> &expectedDurationTicks,
                                           const MediaProbeProgressCallback &progressCallback,
                                           const MediaProbeCancellationCallback &cancellationCallback)
{
    if (expectedDurationTicks.size() != paths.size())
        throw std::runtime_error("The export does not list every chapter's video duration.");
    QVector<MediaInfo> chapters;
    for (const auto &path : paths) {
        chapters.append(MediaProbe::probe(path, {}, false, -1, progressCallback, cancellationCallback));
        if (chapters.last().videoDurationTicks != expectedDurationTicks[chapters.size() - 1])
            throw std::runtime_error(QStringLiteral("Chapter %1 (%2) changed after it was loaded; open the recording again.")
                .arg(chapters.size()).arg(QFileInfo(path).fileName()).toStdString());
    }
    const auto combination = combine(chapters);
    if (!combination.info) throw std::runtime_error(combination.error.toStdString());
    if (inputUrl(chapters).isEmpty()) throw std::runtime_error("The chapters' video timing cannot be expressed exactly.");
    return {*combination.info, chapters};
}

} // namespace FlappedEar
