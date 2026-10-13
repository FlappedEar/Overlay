#include "app/SourceLoading.h"

#include "export/ChapterSource.h"
#include "export/VideoFingerprint.h"
#include "project/VideoChapters.h"
#include "telemetry/SourceOperation.h"
#include "telemetry/TelemetrySource.h"

#include <QFileInfo>

#include <algorithm>
#include <cmath>
#include <exception>
#include <stdexcept>

namespace FlappedEar::SourceLoading {

VideoProbeResult probeVideo(const QString &path, const quint64 generation, const Cancellation &cancellation,
    const QJsonObject &expectedFingerprint, const bool relink, const QVector<VideoChapterInput> &chapters,
    const std::optional<double> recordingOffsetSeconds)
{
    VideoProbeResult result;
    result.recordingOffsetSeconds = recordingOffsetSeconds;
    result.path = path;
    result.generation = generation;
    result.expectedFingerprint = expectedFingerprint;
    result.relink = relink;
    try {
        result.mediaInfo = MediaProbe::probe(
            path, {}, false, -1, {}, [cancellation] { return cancellation->load(); });
        result.fingerprint = videoSourceFingerprint(path, result.mediaInfo);
        // KAN-105: every further chapter, probed and checked against its
        // saved fingerprint. One that is missing, unreadable or no longer
        // the same file is a gap of its saved duration.
        if (!chapters.isEmpty()) {
            const auto videoDuration = [](const MediaInfo &info) {
                return info.videoDuration > 0.0 ? info.videoDuration : info.duration;
            };
            result.chapters.append({ProjectSourceReferenceCodec::forLoadedSource(path, result.fingerprint), path,
                videoDuration(result.mediaInfo), true, {}, result.mediaInfo, 0.0, recordingOffsetSeconds});
            for (const auto &input : chapters) {
                if (cancellation->load()) break;
                VideoChapterState chapter{input.reference, input.path, input.durationSeconds, false, {}, {}, input.gapBeforeSeconds,
                    input.recordingOffsetSeconds};
                if (input.path.isEmpty()) {
                    chapter.problem = QStringLiteral("missing");
                } else {
                    try {
                        const auto info = MediaProbe::probeSummary(input.path, {}, 30'000, {},
                            [cancellation] { return cancellation->load(); });
                        const auto fingerprint = videoSourceFingerprint(input.path, info);
                        if (ProjectSourceReferenceCodec::compareFingerprints(input.reference.fingerprint, fingerprint)
                            == SourceFingerprintMatch::Mismatch) {
                            chapter.problem = QStringLiteral("mismatch");
                        } else {
                            chapter.reference = ProjectSourceReferenceCodec::forLoadedSource(input.path, fingerprint);
                            chapter.durationSeconds = videoDuration(info);
                            chapter.available = true;
                            chapter.mediaInfo = info;
                        }
                    } catch (const OperationCancelled &) {
                        throw;
                    } catch (const std::exception &error) {
                        chapter.problem = QString::fromUtf8(error.what());
                    }
                }
                if (!chapter.available) chapter.path.clear();
                result.chapters.append(chapter);
            }
            applyRecordedGaps(result.chapters);
        }
        result.success = !cancellation->load();
        if (!result.success) {
            result.cancelled = true;
            result.error = QStringLiteral("Video loading was cancelled.");
        }
    } catch (const OperationCancelled &) {
        result.cancelled = true;
        result.error = QStringLiteral("Video loading was cancelled.");
    } catch (const std::exception &error) {
        result.error = QString::fromUtf8(error.what());
    }
    return result;
}

VboLoadResult loadTelemetry(const QString &path, const quint64 generation, const Cancellation &cancellation,
    const QByteArray &expectedRevision, const QJsonObject &expectedFingerprint, const bool relink)
{
    VboLoadResult result;
    result.path = path;
    result.generation = generation;
    result.expectedFingerprint = expectedFingerprint;
    result.relink = relink;
    try {
        const auto sourceSize = QFileInfo(path).size();
        result.contentRevision = TelemetrySource::contentSha256(path, sourceSize,
            [cancellation] { return cancellation->load(); }).toHex();
        result.contentMismatch = !expectedRevision.isEmpty() && result.contentRevision != expectedRevision;
        result.session = TelemetrySource::load(
            path, [cancellation] { return cancellation->load(); });
        if (cancellation->load()) {
            result.cancelled = true;
            result.error = QStringLiteral("Telemetry loading was cancelled.");
            return result;
        }
        result.geometry = buildTrackGeometry(
            result.session, [cancellation] { return cancellation->load(); });
        const auto cancelled = [cancellation] { return cancellation->load(); };
        result.lapSession = deriveSourceLapSession(result.session, {}, cancelled);
        result.fingerprint = ProjectSourceReferenceCodec::telemetryFingerprint(
            path, result.session);
        if (TelemetrySource::contentSha256(path, sourceSize, cancelled).toHex() != result.contentRevision)
            throw std::runtime_error("Recording changed during loading; reload this source.");
        result.success = !cancellation->load();
        if (!result.success) {
            result.cancelled = true;
            result.error = QStringLiteral("Telemetry loading was cancelled.");
        }
    } catch (const OperationCancelled &) {
        result.cancelled = true;
        result.error = QStringLiteral("Telemetry loading was cancelled.");
    } catch (const std::exception &error) {
        result.error = QString::fromUtf8(error.what());
    }
    return result;
}

void applyRecordedGaps(QVector<VideoChapterState> &chapters)
{
    if (chapters.size() < 2 || !chapters.first().recordingOffsetSeconds) return;
    const double origin = *chapters.first().recordingOffsetSeconds;
    double end = chapters.first().durationSeconds; // where the previous chapter ended on the timeline
    for (qsizetype index = 1; index < chapters.size(); ++index) {
        auto &chapter = chapters[index];
        if (chapter.recordingOffsetSeconds && std::isfinite(origin) && std::isfinite(*chapter.recordingOffsetSeconds)) {
            const double gap = *chapter.recordingOffsetSeconds - origin - end;
            chapter.gapBeforeSeconds = std::clamp(std::isfinite(gap) ? gap : 0.0, 0.0, VideoChaptersCodec::maximumGapSeconds);
        }
        end += chapter.gapBeforeSeconds + chapter.durationSeconds;
    }
}

SavedChapters savedChapters(const QJsonObject &video, const QString &projectPath)
{
    SavedChapters saved;
    const auto chapters = VideoChaptersCodec::read(video);
    QVector<TimelineChapter> timelineChapters;
    for (qsizetype index = 0; index < chapters.size(); ++index) {
        const auto &chapter = chapters[index];
        saved.pending.append({chapter.reference, {}, chapter.durationSeconds, false, QStringLiteral("loading"), {},
            chapter.gapBeforeSeconds});
        timelineChapters.append({{}, chapter.durationSeconds, false, chapter.gapBeforeSeconds});
        if (index > 0) {
            saved.further.append({chapter.reference, ProjectSourceReferenceCodec::resolve(chapter.reference, projectPath),
                chapter.durationSeconds, chapter.gapBeforeSeconds});
        }
    }
    saved.timeline = MediaTimeline::fromChapters(timelineChapters);
    return saved;
}

ChapterDerivation deriveChapters(const QVector<VideoChapterState> &probed)
{
    ChapterDerivation derived;
    derived.chapters = probed;
    QVector<TimelineChapter> timelineChapters;
    for (const auto &chapter : derived.chapters)
        timelineChapters.append({chapter.path, chapter.durationSeconds, chapter.available, chapter.gapBeforeSeconds});
    derived.timeline = MediaTimeline::fromChapters(timelineChapters);
    if (!derived.chapters.isEmpty() && !derived.timeline.isValid()) {
        // A chapter without a known duration cannot hold its place in time.
        derived.unusableDurations = true;
        derived.chapters.clear();
    }
    if (derived.chapters.isEmpty()) derived.timeline = {};
    for (const auto &chapter : derived.chapters) derived.gaps += chapter.available ? 0 : 1;
    // KAN-106: export reads every chapter as one source. A gap cannot be exported.
    if (derived.timeline.chapterCount() > 1) {
        if (derived.gaps > 0) {
            derived.gapsBlockExport = true;
        } else if (derived.timeline.hasRecordedGaps()) {
            // KAN-265: a series with recorded gaps is joined in the export's
            // filter graph; the clips may differ in size, frame rate and audio.
            QVector<MediaInfo> infos;
            QVector<double> gaps;
            QStringList paths;
            for (const auto &chapter : derived.chapters) {
                infos.append(chapter.mediaInfo);
                gaps.append(chapter.gapBeforeSeconds);
                paths.append(chapter.path);
            }
            const auto combination = ChapterSource::combineSeries(infos, gaps);
            if (combination.info) {
                derived.exportInfo = *combination.info;
                derived.exportPaths = paths;
            } else {
                derived.exportProblem = combination.error;
            }
        } else {
            QVector<MediaInfo> infos;
            QStringList paths;
            for (const auto &chapter : derived.chapters) {
                infos.append(chapter.mediaInfo);
                paths.append(chapter.path);
            }
            const auto combination = ChapterSource::combine(infos);
            if (combination.info) {
                derived.exportInfo = *combination.info;
                derived.exportPaths = paths;
            } else {
                derived.exportProblem = combination.error;
            }
        }
    }
    return derived;
}

} // namespace FlappedEar::SourceLoading
