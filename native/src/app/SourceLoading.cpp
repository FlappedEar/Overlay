#include "app/SourceLoading.h"

#include "export/VideoFingerprint.h"
#include "telemetry/SourceOperation.h"
#include "telemetry/TelemetrySource.h"

#include <QFileInfo>

#include <exception>
#include <stdexcept>

namespace FlappedEar::SourceLoading {

VideoProbeResult probeVideo(const QString &path, const quint64 generation, const Cancellation &cancellation,
    const QJsonObject &expectedFingerprint, const bool relink, const QVector<VideoChapterInput> &chapters)
{
    VideoProbeResult result;
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
                videoDuration(result.mediaInfo), true, {}, result.mediaInfo});
            for (const auto &input : chapters) {
                if (cancellation->load()) break;
                VideoChapterState chapter{input.reference, input.path, input.durationSeconds, false, {}, {}};
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

} // namespace FlappedEar::SourceLoading
