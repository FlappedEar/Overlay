#include "export/ExportEngine.h"
#include "export/ChapterSource.h"
#include "export/BoundedProcessOutput.h"

#include "export/EncoderDetector.h"
#include "export/ExportArtifactManifest.h"
#include "export/ExportDiagnostics.h"
#include "export/ExportProcessSupervisor.h"
#include "export/ExportStoragePolicy.h"
#include "export/ExportFormat.h"
#include "export/FinalOutputValidation.h"
#include "export/ExportMediaProfile.h"
#include "export/FfmpegTools.h"
#include "export/ExportProgress.h"
#include "export/RawFrameTransport.h"
#include "export/TelemetryFrameRenderer.h"
#include "export/TemporaryOverlayValidation.h"

#include <QFileInfo>
#include <QMap>
#include <QSet>
#include <QDir>
#include <QProcess>
#include <QTemporaryFile>
#include <QElapsedTimer>
#include <QScopeGuard>
#include <QUuid>
#include <algorithm>
#include <cmath>
#include <numeric>

namespace FlappedEar {
namespace {

QByteArray rgbaBytes(const QImage &image, const QSize &size)
{
    const auto expectedBytes = ExportFormat::rgbaFrameBytes(size);
    const qint64 bytesPerRow = static_cast<qint64>(size.width()) * 4;
    if (!expectedBytes || *expectedBytes > std::numeric_limits<qsizetype>::max()
        || bytesPerRow > std::numeric_limits<int>::max()) return {};
    // Stage A carries premultiplied RGBA; straight RGBA goes through the conversion below.
    if (image.format() == QImage::Format_RGBA8888_Premultiplied
        && image.size() == size
        && image.bytesPerLine() == bytesPerRow) {
        return QByteArray::fromRawData(
            reinterpret_cast<const char *>(image.constBits()), static_cast<qsizetype>(*expectedBytes));
    }
    const QImage rgba = image.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
    QByteArray packed(static_cast<qsizetype>(*expectedBytes), Qt::Uninitialized);
    char *destination = packed.data();
    for (int row = 0; row < size.height(); ++row) {
        memcpy(destination + static_cast<qsizetype>(row) * bytesPerRow,
               rgba.constScanLine(row), static_cast<size_t>(bytesPerRow));
    }
    return packed;
}

QString rateString(const MediaRational &rate)
{
    return QStringLiteral("%1/%2").arg(rate.numerator).arg(rate.denominator);
}

bool isCancelled(const ExportSettings &settings)
{
    return (!settings.cancellationFilePath.isEmpty() && QFileInfo::exists(settings.cancellationFilePath))
        || (settings.cancelled && settings.cancelled());
}

// The ownership manifest the controller created is the authority for which paths this export
// may write and the janitor later removes; a configuration naming other paths is refused
// before anything is written.
QString manifestOwnershipError(const ExportSettings &settings)
{
    if (settings.manifestPath.isEmpty()) return {};
    const auto samePath = [](const QString &left, const QString &right) {
        return QDir::cleanPath(QFileInfo(left).absoluteFilePath())
            == QDir::cleanPath(QFileInfo(right).absoluteFilePath());
    };
    ExportArtifactManifestData manifest;
    QString error;
    if (!ExportArtifactManifest::read(settings.manifestPath, &manifest, &error)) {
        return QStringLiteral("Could not read export ownership manifest: %1").arg(error);
    }
    if (settings.outputPath.isEmpty() || !samePath(settings.outputPath, manifest.outputStagingPath)) {
        return QStringLiteral("Export output path does not match its ownership manifest.");
    }
    if (settings.temporaryOverlayPath.isEmpty()
        || !samePath(settings.temporaryOverlayPath, manifest.temporaryOverlayPath)) {
        return QStringLiteral("Temporary overlay path does not match its ownership manifest.");
    }
    return {};
}

bool updateManifestState(const ExportSettings &settings, const QString &state)
{
    if (settings.manifestPath.isEmpty()) return true;
    ExportArtifactManifestData manifest;
    if (!ExportArtifactManifest::read(settings.manifestPath, &manifest)) return false;
    manifest.state = state;
    return ExportArtifactManifest::update(settings.manifestPath, manifest);
}

void observe(
    const ExportSettings &settings,
    const QString &type,
    const QString &state,
    const QString &operation,
    const QString &message,
    const QString &component = QStringLiteral("export"),
    const QVariantMap &details = {})
{
    if (settings.observationCallback) {
        settings.observationCallback({type, state, operation, message, component, details});
    }
}

MediaProbeProgressCallback probeObservations(
    const ExportSettings &settings, const QString &state, const QString &operation)
{
    return [&settings, state, operation](const MediaProbeEvent &event) {
        QVariantMap details{{"mode", event.mode}, {"target", event.targetPath}};
        QString message;
        if (event.phase == MediaProbeEvent::Phase::Started) {
            details.insert("executable", event.executable);
            details.insert("arguments", event.arguments);
            message = QStringLiteral("ffprobe started");
        } else if (event.phase == MediaProbeEvent::Phase::Heartbeat) {
            details.insert("probeElapsedMilliseconds", event.elapsedMilliseconds);
            message = QStringLiteral("ffprobe running · %1 s")
                          .arg(event.elapsedMilliseconds / 1000.0, 0, 'f', 1);
        } else {
            details.insert("probeElapsedMilliseconds", event.elapsedMilliseconds);
            details.insert("exitCode", event.exitCode);
            message = QStringLiteral("ffprobe exited · code %1").arg(event.exitCode);
        }
        observe(settings, QStringLiteral("log"), state, operation, message,
                QStringLiteral("ffprobe"), details);
    };
}

QString formatArgumentList(const QStringList &arguments)
{
    QStringList quoted;
    quoted.reserve(arguments.size());
    for (const QString &argument : arguments) {
        QString escapedArgument = argument;
        escapedArgument.replace('"', QStringLiteral("\\\""));
        quoted.append(argument.contains(QLatin1Char(' ')) || argument.contains(QLatin1Char('"'))
                          ? QStringLiteral("\"") + escapedArgument + QStringLiteral("\"")
                          : escapedArgument);
    }
    return quoted.join(QLatin1Char(' '));
}

struct OverlaySampleResult {
    qsizetype frames = 0;
    qint64 encodedBytes = 0;
    qint64 maximumQueuedBytes = 0;
    QString error;
    bool cancelled = false;
};

OverlaySampleResult sampleTemporaryOverlay(
    const ExportSettings &settings, TelemetryFrameRenderer &renderer, const QSize &outputSize,
    const double sourceRangeStart, const qsizetype expectedFrames, const MediaRational &frameRate)
{
    OverlaySampleResult result;
    const qsizetype sampleFrames = qMin<qsizetype>(24, expectedFrames);
    if (sampleFrames == 0) return result;
    const QString size = QStringLiteral("%1x%2").arg(outputSize.width()).arg(outputSize.height());
    const QStringList arguments = {"-hide_banner", "-loglevel", "error", "-nostats", "-y",
        "-f", "rawvideo", "-pixel_format", "rgba", "-video_size", size,
        "-framerate", rateString(frameRate), "-i", "pipe:0", "-an", "-c:v", "ffv1",
        "-pix_fmt", "bgra", "-f", "matroska", "pipe:1"};
    QProcess process;
    process.setProcessChannelMode(QProcess::SeparateChannels);
    ExportProcessSupervisor supervisor(process, false);
    supervisor.start(FfmpegTools::ffmpegPath(), arguments);
    if (!supervisor.waitForStarted(5'000)) {
        result.error = QStringLiteral("Could not start FFmpeg representative sample: %1").arg(process.errorString());
        return result;
    }
    BoundedProcessOutput encoded(BoundedProcessOutput::Mode::ByteCountOnly, 0);
    BoundedProcessOutput standardError(BoundedProcessOutput::Mode::DiagnosticTail,
                                      ProcessOutputLimits::ffmpegDiagnosticTailBytes);
    const auto drain = [&] {
        encoded.append(process.readAllStandardOutput());
        standardError.append(process.readAllStandardError());
    };
    RawFrameTransport transport(
        process, {}, [&settings] { return isCancelled(settings); }, drain,
        [&result](const qint64, const qint64 maximumQueuedBytes) {
            result.maximumQueuedBytes = maximumQueuedBytes;
        });
    for (qsizetype sampleIndex = 0; sampleIndex < sampleFrames; ++sampleIndex) {
        if (isCancelled(settings)) {
            result.cancelled = true;
            static_cast<void>(supervisor.stopAndWait());
            return result;
        }
        const qsizetype frameIndex = sampleFrames == 1 ? 0
            : (sampleIndex * (expectedFrames - 1)) / (sampleFrames - 1);
        const QImage image = renderer.renderFrame(
            ExportEngine::sourceVideoTime(sourceRangeStart, frameIndex, frameRate));
        if (image.size() != outputSize) {
            result.error = renderer.errorString().isEmpty()
                ? QStringLiteral("Telemetry renderer could not produce a representative sample frame.")
                : renderer.errorString();
            static_cast<void>(supervisor.stopAndWait());
            return result;
        }
        const QByteArray bytes = rgbaBytes(image, outputSize);
        const RawFrameTransportResult writeResult = transport.writeFrame(bytes);
        if (writeResult.status == RawFrameTransportResult::Status::Cancelled) {
            result.cancelled = true;
            static_cast<void>(supervisor.stopAndWait());
            return result;
        }
        if (!writeResult.succeeded()) {
            result.error = QStringLiteral("Could not stream representative sample frame to FFmpeg: %1")
                .arg(writeResult.error);
            static_cast<void>(supervisor.stopAndWait());
            return result;
        }
        ++result.frames;
        drain();
    }
    process.closeWriteChannel();
    QElapsedTimer elapsed;
    elapsed.start();
    while (process.state() != QProcess::NotRunning && elapsed.elapsed() < 30'000) {
        if (isCancelled(settings)) {
            result.cancelled = true;
            static_cast<void>(supervisor.stopAndWait());
            return result;
        }
        process.waitForFinished(100);
        drain();
    }
    drain();
    if (process.state() != QProcess::NotRunning) {
        result.error = QStringLiteral("FFmpeg representative sample timed out.");
        static_cast<void>(supervisor.stopAndWait());
        return result;
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0 || encoded.observedBytes() == 0) {
        result.error = QStringLiteral("FFmpeg representative sample failed: %1")
                           .arg(standardError.text());
        return result;
    }
    result.encodedBytes = encoded.observedBytes();
    return result;
}

QString formatDiagnostics(
    const QProcess &process,
    const FfmpegProgress &progress,
    const QString &standardErrorText,
    const QString &stageName,
    const QStringList &arguments,
    const QString &temporaryOverlayPath = {},
    const qint64 temporaryOverlayBytes = 0,
    const QString &stdinCloseReason = {})
{
    QString details = QStringLiteral("Stage: %1\nExit code: %2\nExit status: %3\nLast encoded frame: %4\n"
                                     "Last encoded time: %5 s\nEncoder fps: %6\nEncoder realtime: %7x")
                          .arg(stageName)
                          .arg(process.exitCode())
                          .arg(process.exitStatus() == QProcess::NormalExit ? QStringLiteral("normal")
                                                                             : QStringLiteral("crash"))
                          .arg(progress.encodedFrames)
                          .arg(progress.outputMicroseconds >= 0
                                   ? QString::number(progress.outputMicroseconds / 1'000'000.0, 'f', 3)
                                   : QStringLiteral("unavailable"))
                          .arg(progress.encoderFps, 0, 'f', 2)
                          .arg(progress.realtimeFactor, 0, 'f', 2);
    details += QStringLiteral("\nFFmpeg arguments: %1").arg(formatArgumentList(arguments));
    if (!temporaryOverlayPath.isEmpty()) {
        details += QStringLiteral("\nTemporary overlay: %1 (%2 bytes)")
                       .arg(temporaryOverlayPath)
                       .arg(temporaryOverlayBytes);
    }
    if (!stdinCloseReason.isEmpty()) {
        details += QStringLiteral("\nStage A stdin close reason: %1").arg(stdinCloseReason);
    }
    if (!standardErrorText.trimmed().isEmpty()) {
        details += QStringLiteral("\n\nFFmpeg stderr:\n") + standardErrorText.trimmed();
    }
    return details;
}

QString processErrorName(const QProcess::ProcessError error)
{
    switch (error) {
    case QProcess::FailedToStart: return QStringLiteral("FailedToStart");
    case QProcess::Crashed: return QStringLiteral("Crashed");
    case QProcess::Timedout: return QStringLiteral("Timedout");
    case QProcess::WriteError: return QStringLiteral("WriteError");
    case QProcess::ReadError: return QStringLiteral("ReadError");
    case QProcess::UnknownError: return QStringLiteral("UnknownError");
    }
    return QStringLiteral("UnknownError");
}

QString processExitStatusName(const QProcess::ExitStatus status)
{
    return status == QProcess::NormalExit ? QStringLiteral("NormalExit")
                                          : QStringLiteral("CrashExit (Unix signal unavailable from QProcess)");
}

struct FilesystemSnapshot {
    QString root;
    QString inspectedPath;
    QString probePath;
    qint64 availableBytes = -1;
    qint64 totalBytes = -1;
};

FilesystemSnapshot filesystemSnapshot(const QString &path)
{
    const ExportFilesystemInfo filesystem = ExportStoragePolicy::filesystemForPath(path);
    return {filesystem.rootPath, filesystem.inspectedPath, filesystem.probePath,
            filesystem.availableBytes, filesystem.totalBytes};
}

bool checkedMultiply(const qint64 left, const qint64 right, qint64 *result)
{
    if (left < 0 || right < 0 || (right != 0 && left > std::numeric_limits<qint64>::max() / right)) return false;
    *result = left * right;
    return true;
}

bool checkedAdd(const qint64 left, const qint64 right, qint64 *result)
{
    if (left < 0 || right < 0 || left > std::numeric_limits<qint64>::max() - right) return false;
    *result = left + right;
    return true;
}

std::optional<qint64> multiplyDivideFloor(
    qint64 firstNumerator, qint64 secondNumerator, qint64 thirdNumerator,
    qint64 firstDenominator, qint64 secondDenominator)
{
    if (firstNumerator < 0 || secondNumerator < 0 || thirdNumerator < 0
        || firstDenominator <= 0 || secondDenominator <= 0) return std::nullopt;
    qint64 numerators[] = {firstNumerator, secondNumerator, thirdNumerator};
    qint64 denominators[] = {firstDenominator, secondDenominator};
    for (qint64 &denominator : denominators) {
        for (qint64 &numerator : numerators) {
            const qint64 divisor = std::gcd(numerator, denominator);
            numerator /= divisor;
            denominator /= divisor;
        }
    }
    qint64 product = 0;
    if (!checkedMultiply(numerators[0], numerators[1], &product)
        || !checkedMultiply(product, numerators[2], &product)) return std::nullopt;
    // Integer division can be applied sequentially for nonnegative operands:
    // floor(floor(n / a) / b) == floor(n / (a * b)), without overflowing a * b.
    return product / denominators[0] / denominators[1];
}

std::optional<qint64> nominalTimecodeRate(const MediaRational &frameRate)
{
    if (!frameRate.isValid() || frameRate.numerator > std::numeric_limits<qint64>::max()
            - frameRate.denominator / 2) return std::nullopt;
    const qint64 rate = (frameRate.numerator + frameRate.denominator / 2) / frameRate.denominator;
    return rate > 0 && rate <= 999 ? std::optional<qint64>(rate) : std::nullopt;
}

struct ExactSeconds {
    qint64 numerator = 0;
    qint64 denominator = 1;
};

std::optional<ExactSeconds> reduceSeconds(qint64 numerator, qint64 denominator)
{
    if (numerator < 0 || denominator <= 0) return std::nullopt;
    const qint64 divisor = std::gcd(numerator, denominator);
    return ExactSeconds{numerator / divisor, denominator / divisor};
}

std::optional<ExactSeconds> addSeconds(const ExactSeconds &left, const ExactSeconds &right)
{
    qint64 leftNumerator = left.numerator;
    qint64 rightNumerator = right.numerator;
    qint64 leftDenominator = left.denominator;
    qint64 rightDenominator = right.denominator;
    const qint64 divisor = std::gcd(leftDenominator, rightDenominator);
    leftDenominator /= divisor;
    rightDenominator /= divisor;
    qint64 leftScaled = 0;
    qint64 rightScaled = 0;
    qint64 denominator = 0;
    qint64 numerator = 0;
    if (!checkedMultiply(leftNumerator, rightDenominator, &leftScaled)
        || !checkedMultiply(rightNumerator, leftDenominator, &rightScaled)
        || !checkedAdd(leftScaled, rightScaled, &numerator)
        || !checkedMultiply(leftDenominator, right.denominator, &denominator)) return std::nullopt;
    return reduceSeconds(numerator, denominator);
}

std::optional<ExactSeconds> subtractSeconds(const ExactSeconds &left, const ExactSeconds &right)
{
    qint64 leftNumerator = left.numerator;
    qint64 rightNumerator = right.numerator;
    qint64 leftDenominator = left.denominator;
    qint64 rightDenominator = right.denominator;
    const qint64 divisor = std::gcd(leftDenominator, rightDenominator);
    leftDenominator /= divisor;
    rightDenominator /= divisor;
    qint64 leftScaled = 0;
    qint64 rightScaled = 0;
    qint64 denominator = 0;
    if (!checkedMultiply(leftNumerator, rightDenominator, &leftScaled)
        || !checkedMultiply(rightNumerator, leftDenominator, &rightScaled)
        || leftScaled < rightScaled
        || !checkedMultiply(leftDenominator, right.denominator, &denominator)) return std::nullopt;
    return reduceSeconds(leftScaled - rightScaled, denominator);
}

bool secondsLessThan(const ExactSeconds &left, const ExactSeconds &right)
{
    qint64 leftNumerator = left.numerator;
    qint64 rightNumerator = right.numerator;
    qint64 leftDenominator = left.denominator;
    qint64 rightDenominator = right.denominator;
    const qint64 divisor = std::gcd(leftDenominator, rightDenominator);
    leftDenominator /= divisor;
    rightDenominator /= divisor;
    qint64 leftScaled = 0;
    qint64 rightScaled = 0;
    return checkedMultiply(leftNumerator, rightDenominator, &leftScaled)
        && checkedMultiply(rightNumerator, leftDenominator, &rightScaled)
        && leftScaled < rightScaled;
}

QString decimalSeconds(const ExactSeconds &value)
{
    QString result = QString::number(value.numerator / value.denominator);
    qint64 remainder = value.numerator % value.denominator;
    if (remainder == 0) return result;
    result += QLatin1Char('.');
    constexpr int precision = 12;
    for (int digit = 0; digit < precision && remainder != 0; ++digit) {
        if (remainder > std::numeric_limits<qint64>::max() / 10) break;
        remainder *= 10;
        result += QLatin1Char(static_cast<char>('0' + remainder / value.denominator));
        remainder %= value.denominator;
    }
    while (result.endsWith(QLatin1Char('0'))) result.chop(1);
    if (result.endsWith(QLatin1Char('.'))) result.chop(1);
    return result;
}

std::optional<ExactSeconds> sourceFrameTimestamp(
    const MediaInfo &source, const qint64 frame, const MediaRational &frameRate)
{
    if (frame < 0 || source.videoStartTicks < 0 || !source.timeBase.isValid() || !frameRate.isValid()) {
        return std::nullopt;
    }
    qint64 originNumerator = 0;
    qint64 frameNumerator = 0;
    if (!checkedMultiply(source.videoStartTicks, source.timeBase.numerator, &originNumerator)
        || !checkedMultiply(frame, frameRate.denominator, &frameNumerator)) return std::nullopt;
    const auto origin = reduceSeconds(originNumerator, source.timeBase.denominator);
    const auto frameOffset = reduceSeconds(frameNumerator, frameRate.numerator);
    return origin && frameOffset ? addSeconds(*origin, *frameOffset) : std::nullopt;
}

} // namespace

int ExportEngine::finalValidationTimeoutMilliseconds(const qint64 outputBytes)
{
    constexpr qint64 baseMilliseconds = 30'000;
    constexpr qint64 bytesPerSecond = 15'000'000;
    constexpr qint64 maximumMilliseconds = 2LL * 3600 * 1000;
    const qint64 bytes = std::max<qint64>(0, outputBytes);
    return static_cast<int>(std::min(maximumMilliseconds, baseMilliseconds + bytes / (bytesPerSecond / 1000)));
}

std::optional<ExportFrameRange> ExportEngine::frameRangeFromInclusiveFrames(
    const qint64 firstFrame, const qint64 lastFrame)
{
    const ExportFrameRange range{firstFrame, lastFrame};
    return range.isValid() ? std::optional<ExportFrameRange>(range) : std::nullopt;
}

std::optional<ExportFrameRange> ExportEngine::fullVideoFrameRange(
    const MediaInfo &source, const MediaRational &exportFrameRate)
{
    const MediaRational sourceRate = effectiveFrameRate(source);
    if (source.videoFrameCount > 0 && sourceRate.isEquivalentTo(exportFrameRate)
        && source.videoFrameCount <= static_cast<qsizetype>(std::numeric_limits<qint64>::max())) {
        return frameRangeFromInclusiveFrames(0, static_cast<qint64>(source.videoFrameCount) - 1);
    }
    if (!source.timeBase.isValid() || source.videoDurationTicks <= 0 || !exportFrameRate.isValid()) {
        return std::nullopt;
    }
    const auto count = multiplyDivideFloor(
        source.videoDurationTicks, source.timeBase.numerator, exportFrameRate.numerator,
        source.timeBase.denominator, exportFrameRate.denominator);
    if (!count || *count <= 0) return std::nullopt;
    return frameRangeFromInclusiveFrames(0, *count - 1);
}

QString ExportEngine::formatSmpteTimecode(const qint64 frame, const MediaRational &frameRate)
{
    const auto nominalRate = nominalTimecodeRate(frameRate);
    if (frame < 0 || !nominalRate) return {};
    const qint64 framesPerMinute = *nominalRate * 60;
    const qint64 framesPerHour = framesPerMinute * 60;
    const qint64 hours = frame / framesPerHour;
    const qint64 withinHour = frame % framesPerHour;
    const qint64 minutes = withinHour / framesPerMinute;
    const qint64 withinMinute = withinHour % framesPerMinute;
    const qint64 seconds = withinMinute / *nominalRate;
    const qint64 frames = withinMinute % *nominalRate;
    return QStringLiteral("%1:%2:%3:%4")
        .arg(hours, 2, 10, QLatin1Char('0'))
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(seconds, 2, 10, QLatin1Char('0'))
        .arg(frames, 2, 10, QLatin1Char('0'));
}

std::optional<qint64> ExportEngine::parseSmpteTimecode(
    const QString &timecode, const MediaRational &frameRate)
{
    const auto nominalRate = nominalTimecodeRate(frameRate);
    const QStringList pieces = timecode.split(QLatin1Char(':'));
    if (!nominalRate || pieces.size() != 4 || timecode.contains(QLatin1Char(';'))) return std::nullopt;
    qint64 fields[4]{};
    for (int index = 0; index < 4; ++index) {
        bool ok = false;
        fields[index] = pieces[index].toLongLong(&ok);
        if (!ok || fields[index] < 0) return std::nullopt;
    }
    if (fields[1] >= 60 || fields[2] >= 60 || fields[3] >= *nominalRate) return std::nullopt;
    qint64 total = 0;
    qint64 component = 0;
    if (!checkedMultiply(fields[0], 3600, &component)
        || !checkedAdd(total, component, &total)
        || !checkedMultiply(fields[1], 60, &component)
        || !checkedAdd(total, component, &total)
        || !checkedAdd(total, fields[2], &total)
        || !checkedMultiply(total, *nominalRate, &total)
        || !checkedAdd(total, fields[3], &total)) return std::nullopt;
    return total;
}

std::optional<ExportFrameRange> ExportEngine::frameRangeForSourceTimecode(
    const MediaInfo &source, const MediaRational &exportFrameRate,
    const QString &inTimecode, const QString &outTimecode)
{
    const auto fullRange = fullVideoFrameRange(source, exportFrameRate);
    const auto firstFrame = parseSmpteTimecode(inTimecode, exportFrameRate);
    const auto lastFrame = parseSmpteTimecode(outTimecode, exportFrameRate);
    if (!fullRange || !firstFrame || !lastFrame || *lastFrame > fullRange->lastFrame) return std::nullopt;
    return frameRangeFromInclusiveFrames(*firstFrame, *lastFrame);
}

double ExportEngine::audioDurationForRange(
    const MediaInfo &source, const double sourceRangeStart, const double sourceRangeEnd)
{
    if (!std::isfinite(sourceRangeStart) || !std::isfinite(sourceRangeEnd)
        || sourceRangeEnd <= sourceRangeStart || !std::isfinite(source.audioStartTime)
        || !std::isfinite(source.audioDuration) || source.audioDuration <= 0.0) {
        return 0.0;
    }
    if (!std::isfinite(source.videoStartTime)) return 0.0;
    const double rangeStart = source.videoStartTime + sourceRangeStart;
    const double rangeEnd = source.videoStartTime + sourceRangeEnd;
    const double audioEnd = source.audioStartTime + source.audioDuration;
    if (!std::isfinite(audioEnd)) return 0.0;
    return qMax(0.0, qMin(rangeEnd, audioEnd)
                     - qMax(rangeStart, source.audioStartTime));
}

double ExportEngine::audioStartForRange(const MediaInfo &source, const double start, const double end)
{
    return audioDurationForRange(source, start, end) > 0.0
        ? std::max(0.0, source.audioStartTime - (source.videoStartTime + start)) : 0.0;
}

QString ExportEngine::stageBAudioFilterGraph(const StageBSourceAccess &access, const bool chaptered)
{
    // Chapters (KAN-106): each chapter's audio keeps its own timestamps on the
    // chapter timeline. A chapter whose audio ends before its video would
    // otherwise pull every later chapter's audio early; aresample fills such
    // gaps with silence and drops overlaps at the joins.
    const QString joins = chaptered ? QStringLiteral("aresample=async=1:min_hard_comp=0.001,") : QString();
    // Subtract the selected VIDEO origin, retaining a real track-relative delay.
    return QStringLiteral("[0:a]%3atrim=start=%1:end=%2,asetpts=PTS-(%1)/TB[audio]")
        .arg(access.trimStartTimestamp, access.trimEndTimestamp, joins);
}

double ExportEngine::framePresentationTime(
    const double startTime, const qsizetype frameIndex, const MediaRational &frameRate)
{
    return sourceVideoTime(startTime, frameIndex, frameRate);
}

double ExportEngine::exportRelativeTime(const qsizetype frameIndex, const MediaRational &frameRate)
{
    if (!frameRate.isValid()) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    return static_cast<double>(frameIndex) * static_cast<double>(frameRate.denominator)
        / static_cast<double>(frameRate.numerator);
}

double ExportEngine::outputDuration(const qsizetype frameCount, const MediaRational &frameRate)
{
    if (!frameRate.isValid()) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    return static_cast<double>(frameCount) * static_cast<double>(frameRate.denominator)
        / static_cast<double>(frameRate.numerator);
}

MediaRational ExportEngine::effectiveFrameRate(
    const MediaInfo &source, const MediaRational &requested)
{
    if (requested.isValid()) {
        return requested;
    }
    return source.averageFrameRate.isValid() ? source.averageFrameRate : source.frameRate;
}

double ExportEngine::sourceVideoTime(
    const double sourceRangeStart, const qsizetype frameIndex, const MediaRational &frameRate)
{
    return sourceRangeStart + exportRelativeTime(frameIndex, frameRate);
}

std::optional<StageBSourceAccess> ExportEngine::stageBSourceAccess(
    const MediaInfo &source, const ExportFrameRange &range,
    const MediaRational &frameRate, const qint64 prerollSeconds)
{
    if (!range.isValid() || prerollSeconds < 0) return std::nullopt;
    const auto start = sourceFrameTimestamp(source, range.firstFrame, frameRate);
    if (range.lastFrame == std::numeric_limits<qint64>::max()) return std::nullopt;
    const auto end = sourceFrameTimestamp(source, range.lastFrame + 1, frameRate);
    const auto preroll = reduceSeconds(prerollSeconds, 1);
    if (!start || !end || !preroll || secondsLessThan(*end, *start)) return std::nullopt;
    const ExactSeconds inputSeek = secondsLessThan(*start, *preroll)
        ? ExactSeconds{} : *subtractSeconds(*start, *preroll);
    return StageBSourceAccess{decimalSeconds(inputSeek), decimalSeconds(*start), decimalSeconds(*end)};
}

QStringList ExportEngine::stageBInputArguments(const StageBSourceAccess &access, const QString &path)
{
    // Retain original PTS, including across input seek. Both trim and seek now
    // refer to the same source timestamp domain rather than implicit rebasing.
    return {"-copyts", "-seek_timestamp", "1", "-ss", access.inputSeekTimestamp, "-i", path};
}

QStringList ExportEngine::stageBAdditionalInputArguments(
    const VideoComposition::Timing &timing, const QString &path)
{
    // -copyts is already global (the main input sets it), so the seek and the
    // filter's input timestamps are both the file's own.
    return {"-seek_timestamp", "1", "-ss", QString::number(timing.inputSeekSeconds, 'f', 6), "-i", path};
}

namespace {

int additionalInputCount(const StageBComposition &composition)
{
    int count = 0;
    for (const auto &camera : composition.cameras)
        if (camera.input >= 0) ++count;
    return count;
}

QString seconds6(const double value)
{
    return QString::number(value, 'f', 6);
}

// FFmpeg colour from "#RRGGBB".
QString ffmpegColor(const QString &color)
{
    return QStringLiteral("0x") + color.mid(1);
}

// KAN-245: the filter graph that draws the plan's layers over the main video.
// `mainLabel` is the main video prepared at the output size. Every layer is
// scaled, bordered or letterboxed, optionally faded in, and overlaid inside
// its windows. The result is `[sourceVideo]`.
QString layeredGraph(const StageBComposition &composition, const bool straight)
{
    using VideoComposition::Layer;
    const QString format = straight ? QStringLiteral("yuv420p10") : QStringLiteral("auto");
    const QString alphaFormat = straight ? QStringLiteral("yuva420p10le") : QStringLiteral("yuva420p");
    // The windows in export time; a layer with none left is not drawn.
    struct Drawn {
        const Layer *layer;
        QString enable; // empty: always
        double fadeStart;
    };
    QVector<Drawn> drawn;
    for (const Layer &layer : composition.layers) {
        QStringList terms;
        bool always = false;
        for (const auto &window : layer.windows) {
            const double start = window.start - composition.mainStartSeconds;
            const double end = window.end - composition.mainStartSeconds;
            if (end <= 0.0 || start >= composition.exportSeconds) continue;
            if (start <= 0.0 && end >= composition.exportSeconds) always = true;
            terms.append(QStringLiteral("gte(t,%1)*lt(t,%2)").arg(seconds6(start), seconds6(end)));
        }
        if (terms.isEmpty()) continue;
        const double firstStart = layer.windows.first().start - composition.mainStartSeconds;
        drawn.append({&layer, always ? QString() : terms.join(QLatin1Char('+')), firstStart});
    }
    if (drawn.isEmpty()) return QStringLiteral("[mainPre]null[sourceVideo];");
    QMap<int, int> uses; // layers per camera
    for (const Drawn &item : drawn) ++uses[item.layer->camera];
    QString graph;
    // The main video: the base and one copy for each layer that shows it.
    const int mainCopies = uses.value(0) + 1;
    graph += mainCopies == 1 ? QStringLiteral("[mainPre]null[mainBase];")
        : QStringLiteral("[mainPre]split=%1[mainBase]").arg(mainCopies) + [&] {
            QString labels;
            for (int copy = 0; copy < uses.value(0); ++copy) labels += QStringLiteral("[cam0_%1]").arg(copy);
            return labels + QLatin1Char(';');
        }();
    for (auto it = uses.cbegin(); it != uses.cend(); ++it) {
        if (it.key() == 0) continue;
        const auto &camera = composition.cameras[it.key()];
        QString labels;
        for (int copy = 0; copy < it.value(); ++copy) labels += QStringLiteral("[cam%1_%2]").arg(it.key()).arg(copy);
        graph += QStringLiteral("[%1:v]setpts=(T*%2+%3)/TB,%4%5;")
            .arg(camera.input)
            .arg(QString::number(camera.timeFactor, 'f', 12), QString::number(camera.timeShift, 'f', 9))
            .arg(it.value() == 1 ? QStringLiteral("null") : QStringLiteral("split=%1").arg(it.value()), labels);
    }
    QMap<int, int> nextCopy;
    QString accumulated = QStringLiteral("mainBase");
    for (qsizetype index = 0; index < drawn.size(); ++index) {
        const Layer &layer = *drawn[index].layer;
        QStringList chain;
        const bool fullFrameMain = layer.camera == 0 && layer.onAir;
        if (!fullFrameMain)
            chain.append(QStringLiteral("scale=%1:%2:flags=lanczos,setsar=1")
                .arg(layer.content.width()).arg(layer.content.height()));
        if (layer.rect != layer.content && !fullFrameMain)
            chain.append(QStringLiteral("pad=%1:%2:%3:%4:color=%5")
                .arg(layer.rect.width()).arg(layer.rect.height())
                .arg(layer.content.x() - layer.rect.x()).arg(layer.content.y() - layer.rect.y())
                .arg(ffmpegColor(layer.borderColor)));
        if (layer.fadeInSeconds > 0.0 && drawn[index].fadeStart >= 0.0)
            chain.append(QStringLiteral("format=%1,fade=t=in:st=%2:d=%3:alpha=1")
                .arg(alphaFormat, seconds6(drawn[index].fadeStart), seconds6(layer.fadeInSeconds)));
        if (chain.isEmpty()) chain.append(QStringLiteral("null"));
        const QString source = QStringLiteral("cam%1_%2").arg(layer.camera).arg(nextCopy[layer.camera]++);
        const bool last = index + 1 == drawn.size();
        const QString output = last ? QStringLiteral("sourceVideo") : QStringLiteral("acc%1").arg(index);
        const QString enable = drawn[index].enable.isEmpty() ? QString()
            : QStringLiteral(":enable='%1'").arg(drawn[index].enable);
        graph += QStringLiteral("[%1]%2[layer%3];[%4][layer%3]overlay=%5:%6:eof_action=pass:repeatlast=0:format=%7%8[%9];")
            .arg(source, chain.join(QLatin1Char(',')), QString::number(index), accumulated)
            .arg(layer.rect.x()).arg(layer.rect.y()).arg(format, enable, output);
        accumulated = output;
    }
    return graph;
}

} // namespace

QString ExportEngine::stageBVideoFilterGraph(
    const StageBSourceAccess &sourceAccess,
    const QSize &sourceSize,
    const QSize &outputSize,
    const MediaRational &frameRate,
    const qsizetype expectedFrames,
    const ExportMediaProfile &mediaProfile,
    const StageBComposition &composition)
{
    const bool requiresStraightOverlay = mediaProfile.outputBitDepth == 10;
    const QString overlayPreparation = requiresStraightOverlay
        ? QStringLiteral("format=pix_fmts=gbrap,unpremultiply=inplace=1,setparams=alpha_mode=straight")
        : QStringLiteral("setparams=alpha_mode=premultiplied");
    const QString overlayAlpha = requiresStraightOverlay
        ? QStringLiteral("straight") : QStringLiteral("premultiplied");
    const QString overlayFormat = requiresStraightOverlay
        ? QStringLiteral("yuv420p10") : QStringLiteral("auto");
    QString mainScale;
    if (composition.mainRect.isValid() && composition.mainRect != QRect(QPoint(0, 0), outputSize)) {
        // Side by side: the main video keeps its aspect ratio in its place.
        mainScale = QStringLiteral(",scale=%1:%2:flags=lanczos,setsar=1,pad=%3:%4:%5:%6:color=black")
            .arg(composition.mainRect.width()).arg(composition.mainRect.height())
            .arg(outputSize.width()).arg(outputSize.height())
            .arg(composition.mainRect.x()).arg(composition.mainRect.y());
    } else if (sourceSize != outputSize) {
        mainScale = QStringLiteral(",scale=%1:%2:flags=lanczos").arg(outputSize.width()).arg(outputSize.height());
    }
    // KAN-131: each additional video is retimed onto the output's timeline
    // and placed over the main video before the telemetry overlay, so widgets
    // stay on top. Before its first frame and after its last, the main video
    // shows through (eof_action=pass, no repeated last frame).
    QString additional;
    QString mainLabel = composition.additional.isEmpty() ? QStringLiteral("sourceVideo") : QStringLiteral("main0");
    if (!composition.layers.isEmpty()) {
        mainLabel = QStringLiteral("mainPre");
        additional = layeredGraph(composition, requiresStraightOverlay);
    }
    for (qsizetype index = 0; index < composition.additional.size(); ++index) {
        const StageBComposition::Input &input = composition.additional[index];
        const QString next = index + 1 == composition.additional.size()
            ? QStringLiteral("sourceVideo") : QStringLiteral("main%1").arg(index + 1);
        additional += QStringLiteral(
            "[%1:v]setpts=(T*%2+%3)/TB,scale=%4:%5:flags=lanczos,setsar=1[additional%6];"
            "[main%6][additional%6]overlay=%7:%8:eof_action=pass:repeatlast=0:format=%9[%10];")
            .arg(index + 2)
            .arg(QString::number(input.timeFactor, 'f', 12), QString::number(input.timeShift, 'f', 9))
            .arg(input.rect.width()).arg(input.rect.height())
            .arg(index)
            .arg(input.rect.x()).arg(input.rect.y())
            .arg(requiresStraightOverlay ? QStringLiteral("yuv420p10") : QStringLiteral("auto"), next);
    }
    return QStringLiteral(
        "[0:v]trim=start=%1,setpts=PTS-STARTPTS%4,"
        "fps=fps=%2:start_time=0:round=near:eof_action=round,"
        "trim=end_frame=%3,setpts=PTS-STARTPTS[%9];%10"
        "[1:v]setpts=PTS-STARTPTS,%5[temporaryOverlay];"
        "[sourceVideo][temporaryOverlay]overlay=0:0:shortest=1:repeatlast=0:eof_action=endall:alpha=%6:format=%7[composited];"
        "[composited]format=pix_fmts=%8[video]")
            .arg(sourceAccess.trimStartTimestamp)
            .arg(rateString(frameRate))
            .arg(expectedFrames)
            .arg(mainScale)
            .arg(overlayPreparation)
            .arg(overlayAlpha)
            .arg(overlayFormat)
            .arg(mediaProfile.outputPixelFormat)
            .arg(mainLabel, additional);
}

QString ExportEngine::verifyCompositionFilters(
    const QString &program, const QString &graph, const std::function<bool()> &cancelled,
    const int additionalInputs)
{
    throwIfCancelled(cancelled);
    QProcess process;
    ExportProcessSupervisor supervisor(process, false);
    QStringList arguments{"-hide_banner", "-loglevel", "error", "-nostdin",
        "-f", "lavfi", "-i", "color=black:s=64x64:r=30:d=0.1",
        "-f", "lavfi", "-i", "color=black@0:s=64x64:r=30:d=0.1,format=rgba"};
    for (int index = 0; index < additionalInputs; ++index)
        arguments += QStringList{"-f", "lavfi", "-i", "color=gray:s=48x36:r=25:d=0.1"};
    arguments += QStringList{"-filter_complex", graph, "-map", "[video]", "-frames:v", "3",
        "-c:v", "rawvideo", "-f", "null", "-"};
    supervisor.start(program, arguments);
    if (!supervisor.waitForStarted(5'000))
        return QStringLiteral("Could not start FFmpeg composition preflight: %1").arg(process.errorString());
    BoundedProcessOutput diagnostic(BoundedProcessOutput::Mode::DiagnosticTail,
                                    ProcessOutputLimits::ffmpegDiagnosticTailBytes);
    const auto drain = [&] {
        diagnostic.append(process.readAllStandardError());
        static_cast<void>(process.readAllStandardOutput());
    };
    QElapsedTimer timer;
    timer.start();
    while (process.state() != QProcess::NotRunning) {
        if (cancelled && cancelled()) {
            static_cast<void>(supervisor.stopAndWait());
            throwIfCancelled(cancelled);
        }
        if (timer.elapsed() >= 20'000) {
            static_cast<void>(supervisor.stopAndWait());
            return QStringLiteral("FFmpeg composition preflight timed out before telemetry rendering.");
        }
        process.waitForFinished(100);
        drain();
    }
    drain();
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
        return QStringLiteral("Installed FFmpeg cannot run the required overlay filters. "
                              "Install a compatible FFmpeg build with explicit alpha-mode support. %1")
            .arg(diagnostic.text());
    return {};
}

ExportResult ExportEngine::exportVideo(
    const ExportSettings &settings, TelemetryFrameRenderer &renderer)
{
    ExportResult result;
    QElapsedTimer elapsedTimer;
    elapsedTimer.start();
    const auto captureElapsed = qScopeGuard([&result, &elapsedTimer] {
        result.elapsedMilliseconds = elapsedTimer.elapsed();
        result.renderMilliseconds = result.renderNanoseconds / 1'000'000;
    });
    try {
        observe(settings, QStringLiteral("log"), QStringLiteral("preparing"),
                QStringLiteral("probeInput"), QStringLiteral("Export requested"));
        if (const QString ownershipError = manifestOwnershipError(settings);
            !ownershipError.isEmpty()) {
            result.error = ownershipError;
            return result;
        }
        if (settings.stateCallback) {
            settings.stateCallback(QStringLiteral("starting"));
        }
        observe(settings, QStringLiteral("status"), QStringLiteral("preparing"),
                QStringLiteral("probeInput"), QStringLiteral("Reading input metadata with ffprobe"));
        // KAN-106: a chaptered recording is read as one source through the
        // concat demuxer; its timeline is the chapters end to end.
        if (settings.chapterPaths.size() > 1 && settings.chapterPaths.first() != settings.inputPath) {
            result.error = QStringLiteral("The export's first chapter is not its input video.");
            return result;
        }
        QVector<MediaInfo> chapters;
        const MediaInfo source = [&] {
            const auto observations = probeObservations(settings, QStringLiteral("preparing"), QStringLiteral("probeInput"));
            const auto cancelled = [&settings] { return isCancelled(settings); };
            if (settings.chapterPaths.size() < 2)
                return MediaProbe::probe(settings.inputPath, {}, false, -1, observations, cancelled);
            auto chaptered = ChapterSource::probe(settings.chapterPaths, settings.chapterDurationTicks, observations, cancelled);
            chapters = chaptered.chapters;
            return chaptered.info;
        }();
        observe(settings, QStringLiteral("log"), QStringLiteral("preparing"),
                QStringLiteral("probeInput"), QStringLiteral("Input probed"), QStringLiteral("ffprobe"),
                {{"codec", source.videoCodec}, {"width", source.videoSize.width()},
                 {"height", source.videoSize.height()}, {"duration", source.duration},
                 {"chapters", std::max<qsizetype>(1, settings.chapterPaths.size())}});
        if (source.videoStartTicks < 0) {
            // Frame-correct trimming assumes a source that starts at or after
            // zero; refuse rather than cut at the wrong frame (KAN-148).
            result.error = QStringLiteral("The source video starts before zero (start_pts %1); exporting it is not supported yet.")
                               .arg(source.videoStartTicks);
            return result;
        }
        const QSize outputSize = settings.outputSize.isValid() ? settings.outputSize : source.videoSize;
        // One exact rational governs overlay generation, framesync conversion,
        // progress, and final-media validation.
        const MediaRational exportFrameRate = effectiveFrameRate(source, settings.frameRate);
        const auto fullRange = fullVideoFrameRange(source, exportFrameRate);
        const ExportFrameRange scheduledRange = settings.frameRange.isValid()
            ? settings.frameRange : fullRange.value_or(ExportFrameRange{});
        if (!fullRange || !scheduledRange.isValid()
            || scheduledRange.firstFrame < fullRange->firstFrame
            || scheduledRange.lastFrame > fullRange->lastFrame
            || scheduledRange.frameCount() > std::numeric_limits<qsizetype>::max()) {
            result.error = QStringLiteral("Requested frame range is outside the usable video-frame domain.");
            return result;
        }
        const qsizetype expectedFrames = static_cast<qsizetype>(scheduledRange.frameCount());
        result.firstFrame = scheduledRange.firstFrame;
        result.lastFrame = scheduledRange.lastFrame;
        result.sourceFrameCount = source.videoFrameCount <= static_cast<qsizetype>(std::numeric_limits<qint64>::max())
            ? static_cast<qint64>(source.videoFrameCount) : 0;
        observe(settings, QStringLiteral("log"), QStringLiteral("preparing"),
                QStringLiteral("resolveSourceFrameDomain"),
                QStringLiteral("Authoritative export frame range resolved"), QStringLiteral("schedule"),
                {{"sourceFrameCount", result.sourceFrameCount},
                 {"sourceFrameCountSource", source.videoFrameCount > 0 ? QStringLiteral("nb_frames")
                                                                    : QStringLiteral("duration_ts/time_base")},
                 {"firstFrame", result.firstFrame}, {"lastFrame", result.lastFrame},
                 {"expectedFrames", static_cast<qint64>(expectedFrames)},
                 {"resultClassification", QStringLiteral("Pending")}});
        // These doubles are presentation/filter diagnostics derived from an already-authoritative
        // frame range. They never choose a frame boundary or expected count.
        const double sourceRangeStart = exportRelativeTime(
            static_cast<qsizetype>(scheduledRange.firstFrame), exportFrameRate);
        const double sourceRangeEnd = exportRelativeTime(
            static_cast<qsizetype>(scheduledRange.lastFrame + 1), exportFrameRate);
        if (expectedFrames == 0 || !outputSize.isValid()) {
            result.error = QStringLiteral("Export range or frame rate is invalid.");
            return result;
        }
        const double exportDuration = outputDuration(expectedFrames, exportFrameRate);
        result.exportFrameRate = exportFrameRate;
        result.expectedFrames = expectedFrames;
        const QList<EncoderCapability> encoders = EncoderDetector::discover(
            {}, [&settings] { return isCancelled(settings); });
        const QString encoder = settings.encoder.isEmpty()
            ? EncoderDetector::preferredHevcEncoder(encoders)
            : settings.encoder;
        if (encoder.isEmpty()
            || !std::any_of(encoders.cbegin(), encoders.cend(), [&encoder](const auto &item) {
                   return item.id == encoder;
               })) {
            result.error = QStringLiteral("No requested HEVC encoder is available in this FFmpeg build.");
            return result;
        }
        const auto selected = std::find_if(encoders.cbegin(), encoders.cend(), [&encoder](const auto &item) {
            return item.id == encoder;
        });
        if (settings.encoderCallback && selected != encoders.cend()) {
            settings.encoderCallback(selected->id, selected->displayName);
        }
        result.mediaProfile = ExportMediaProfile::derive(
            source, outputSize, exportFrameRate, settings.videoBitrate, encoder);
        if (!result.mediaProfile.supported) {
            result.error = result.mediaProfile.error;
            return result;
        }
        const RendererCapabilityResult rendererCapability = renderer.capability();
        if (!rendererCapability.supported) {
            result.error = rendererCapability.error.isEmpty()
                ? QStringLiteral("The offscreen renderer does not support the requested export raster.")
                : rendererCapability.error;
            return result;
        }
        const EncoderProfileSupport encoderSupport = EncoderDetector::verifyProfile(
            {FfmpegTools::ffmpegPath(), encoder, outputSize, exportFrameRate,
             result.mediaProfile.outputPixelFormat, result.mediaProfile.outputBitDepth,
             result.mediaProfile.encoderProfile},
            {}, [&settings] { return isCancelled(settings); });
        observe(settings, QStringLiteral("log"), QStringLiteral("preparing"),
                QStringLiteral("verifyExportCapabilities"),
                encoderSupport.supported
                    ? QStringLiteral("Renderer and encoder capability preflight passed")
                    : QStringLiteral("Encoder capability preflight failed"),
                QStringLiteral("capability"),
                {{"width", outputSize.width()}, {"height", outputSize.height()},
                 {"frameBytes", rendererCapability.frameBytes},
                 {"pixelCount", rendererCapability.pixelCount},
                 {"rendererBackend", rendererCapability.backend},
                 {"rendererLimit", rendererCapability.maximumTextureSize},
                 {"encoder", encoder}, {"pixelFormat", result.mediaProfile.outputPixelFormat},
                 {"bitDepth", result.mediaProfile.outputBitDepth},
                 {"encoderProfile", result.mediaProfile.encoderProfile},
                 {"capabilityCacheHit", encoderSupport.cacheHit}});
        if (!encoderSupport.supported) {
            result.error = encoderSupport.error;
            return result;
        }
        // KAN-131: each additional video keeps its place in the layout; one
        // with no frame inside the export leaves its place empty.
        StageBComposition composition;
        StageBComposition preflightComposition;
        QStringList additionalInputArguments;
        if (!settings.additionalVideos.isEmpty()) {
            QVector<QSize> sourceSizes{source.displayVideoSize.isEmpty() ? source.videoSize : source.displayVideoSize};
            for (const auto &video : settings.additionalVideos)
                sourceSizes.append(video.info.displayVideoSize.isEmpty() ? video.info.videoSize : video.info.displayVideoSize);
            // Where each video is on the output timeline, or nothing when it
            // has no frame inside the export.
            QVector<std::optional<VideoComposition::Timing>> timings;
            for (const auto &video : settings.additionalVideos) {
                const auto timing = VideoComposition::timing(settings.sync, video.sync, sourceRangeStart, exportDuration,
                    video.info.videoStartTime, video.info.videoDuration > 0.0 ? video.info.videoDuration : video.info.duration);
                if (!timing) {
                    observe(settings, QStringLiteral("log"), QStringLiteral("preparing"),
                            QStringLiteral("placeAdditionalVideos"),
                            QStringLiteral("Additional video has no frame inside the export range"),
                            QStringLiteral("export"), {{"path", video.path}, {"label", video.label}});
                }
                timings.append(timing);
            }
            if (settings.videoLayout.mode == VideoLayoutMode::SideBySide) {
                const QVector<QRect> rects = VideoComposition::layout(settings.videoLayout.mode, outputSize, sourceSizes);
                const QVector<QRect> preflightRects = VideoComposition::layout(settings.videoLayout.mode, QSize(64, 64), sourceSizes);
                if (rects.size() != sourceSizes.size() || preflightRects.size() != sourceSizes.size()) {
                    result.error = QStringLiteral("Could not place the additional videos in the export.");
                    return result;
                }
                composition.mainRect = rects[0];
                preflightComposition.mainRect = preflightRects[0];
                for (qsizetype index = 0; index < settings.additionalVideos.size(); ++index) {
                    const auto &video = settings.additionalVideos[index];
                    const auto &timing = timings[index];
                    if (!timing) continue;
                    composition.additional.append({rects[index + 1], timing->timeFactor, timing->timeShift});
                    preflightComposition.additional.append({preflightRects[index + 1], 1.0, 0.0});
                    additionalInputArguments += stageBAdditionalInputArguments(*timing, video.path);
                    observe(settings, QStringLiteral("log"), QStringLiteral("preparing"),
                            QStringLiteral("placeAdditionalVideos"), QStringLiteral("Additional video placed"),
                            QStringLiteral("export"),
                            {{"path", video.path}, {"label", video.label},
                             {"x", rects[index + 1].x()}, {"y", rects[index + 1].y()},
                             {"width", rects[index + 1].width()}, {"height", rects[index + 1].height()},
                             {"inputSeekSeconds", timing->inputSeekSeconds},
                             {"timeFactor", timing->timeFactor}, {"timeShift", timing->timeShift}});
                }
            } else {
                // KAN-245: picture in picture and camera switching. Every
                // layer of the plan whose camera has footage in the export is
                // drawn; a camera without footage leaves the main video.
                QStringList cameraIds{mainCameraId};
                for (const auto &video : settings.additionalVideos) cameraIds.append(video.id);
                const double endSeconds = sourceRangeStart + exportDuration + 1.0;
                // Where each camera has footage, on the main video's timeline;
                // the main video is on air outside it.
                QVector<VideoComposition::Window> footage(cameraIds.size(), {0.0, endSeconds});
                for (qsizetype index = 0; index < settings.additionalVideos.size(); ++index) {
                    const auto &video = settings.additionalVideos[index];
                    const auto &timing = timings[index];
                    if (!timing) {
                        footage[index + 1] = {0.0, 0.0};
                        continue;
                    }
                    const double seconds = video.info.videoDuration > 0.0 ? video.info.videoDuration : video.info.duration;
                    const double first = video.info.videoStartTime * timing->timeFactor + timing->timeShift + sourceRangeStart;
                    const double last = (video.info.videoStartTime + seconds) * timing->timeFactor + timing->timeShift + sourceRangeStart;
                    footage[index + 1] = {first, last};
                }
                const auto layers = VideoComposition::plan(settings.videoLayout, outputSize, cameraIds, sourceSizes, endSeconds, footage);
                const auto preflightLayers = VideoComposition::plan(settings.videoLayout, QSize(64, 64), cameraIds, sourceSizes, endSeconds, footage);
                if (layers.isEmpty() != preflightLayers.isEmpty()) {
                    result.error = QStringLiteral("Could not place the additional videos in the export.");
                    return result;
                }
                composition.cameras.resize(cameraIds.size());
                preflightComposition.cameras.resize(cameraIds.size());
                QSet<int> used;
                for (const auto &layer : layers) used.insert(layer.camera);
                int input = 2;
                for (qsizetype index = 0; index < settings.additionalVideos.size(); ++index) {
                    const auto &video = settings.additionalVideos[index];
                    const auto &timing = timings[index];
                    if (!timing || !used.contains(static_cast<int>(index) + 1)) continue;
                    composition.cameras[index + 1] = {input, timing->timeFactor, timing->timeShift};
                    preflightComposition.cameras[index + 1] = {input, 1.0, 0.0};
                    ++input;
                    additionalInputArguments += stageBAdditionalInputArguments(*timing, video.path);
                    observe(settings, QStringLiteral("log"), QStringLiteral("preparing"),
                            QStringLiteral("placeAdditionalVideos"), QStringLiteral("Additional video placed"),
                            QStringLiteral("export"),
                            {{"path", video.path}, {"label", video.label}, {"camera", video.id},
                             {"inputSeekSeconds", timing->inputSeekSeconds},
                             {"timeFactor", timing->timeFactor}, {"timeShift", timing->timeShift}});
                }
                const auto available = [](const StageBComposition &target, const QVector<VideoComposition::Layer> &all) {
                    QVector<VideoComposition::Layer> kept;
                    for (const auto &layer : all)
                        if (layer.camera == 0 || target.cameras[layer.camera].input >= 0) kept.append(layer);
                    return kept;
                };
                composition.layers = available(composition, layers);
                preflightComposition.layers = available(preflightComposition, preflightLayers);
                composition.mainStartSeconds = preflightComposition.mainStartSeconds = sourceRangeStart;
                composition.exportSeconds = preflightComposition.exportSeconds = exportDuration;
            }
        }
        observe(settings, QStringLiteral("status"), QStringLiteral("preparing"),
                QStringLiteral("verifyCompositionFilters"),
                QStringLiteral("Checking FFmpeg overlay filter compatibility"));
        const QString compositionError = verifyCompositionFilters(
            FfmpegTools::ffmpegPath(),
            stageBVideoFilterGraph({"0", "0", "0.1"}, QSize(64, 64), QSize(64, 64),
                                  {30, 1}, 3, result.mediaProfile, preflightComposition),
            [&settings] { return isCancelled(settings); },
            static_cast<int>(preflightComposition.layers.isEmpty() ? preflightComposition.additional.size()
                : additionalInputCount(preflightComposition)));
        if (!compositionError.isEmpty()) {
            result.error = compositionError;
            return result;
        }
        const QString temporaryOverlayPath = settings.temporaryOverlayPath.isEmpty()
            ? QDir::temp().filePath(QStringLiteral("flappedear-overlay-%1.mkv")
                  .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)))
            : settings.temporaryOverlayPath;
        observe(settings, QStringLiteral("status"), QStringLiteral("preparing"),
                QStringLiteral("sampleTemporaryOverlay"),
                QStringLiteral("Measuring representative telemetry overlay sample"));
        const OverlaySampleResult overlaySample = sampleTemporaryOverlay(
            settings, renderer, outputSize, sourceRangeStart, expectedFrames, exportFrameRate);
        if (overlaySample.cancelled) {
            result.cancelled = true;
            return result;
        }
        const ExportStorageEstimate storageEstimate = overlaySample.error.isEmpty()
            ? ExportStoragePolicy::estimateFromSample(overlaySample.encodedBytes, overlaySample.frames,
                                                       expectedFrames, exportDuration, settings.videoBitrate)
            : ExportStoragePolicy::estimate(expectedFrames, outputSize, exportDuration, settings.videoBitrate);
        const ExportStoragePreflight storagePreflight = ExportStoragePolicy::evaluate(
            temporaryOverlayPath, settings.outputPath, storageEstimate);
        observe(settings, QStringLiteral("log"), QStringLiteral("preparing"),
                QStringLiteral("preflightStorage"), QStringLiteral("Export storage preflight"),
                QStringLiteral("storage"), {{"estimatedTemporaryOverlayBytes", storageEstimate.temporaryOverlayBytes},
                {"estimatedFinalOutputBytes", storageEstimate.finalOutputBytes},
                {"safetyReserveBytes", storageEstimate.safetyReserveBytes},
                {"estimateBasis", ExportStoragePolicy::estimateBasisText(storageEstimate.basis)},
                {"sampleFrames", static_cast<qint64>(storageEstimate.sampleFrames)},
                {"sampleEncodedBytes", storageEstimate.sampleBytes},
                {"sampleBytesPerFrame", storageEstimate.bytesPerFrame},
                {"sampleSafetyMargin", storageEstimate.safetyMargin},
                {"sampleError", overlaySample.error},
                {"sampleMaximumQueuedBytes", overlaySample.maximumQueuedBytes},
                {"temporaryFilesystemRoot", storagePreflight.temporaryFilesystem.rootPath},
                {"temporaryFilesystemInspectedPath", storagePreflight.temporaryFilesystem.inspectedPath},
                {"temporaryFilesystemProbePath", storagePreflight.temporaryFilesystem.probePath},
                {"temporaryFilesystemAvailableBytes", storagePreflight.temporaryFilesystem.availableBytes},
                {"destinationFilesystemRoot", storagePreflight.destinationFilesystem.rootPath},
                {"destinationFilesystemInspectedPath", storagePreflight.destinationFilesystem.inspectedPath},
                {"destinationFilesystemProbePath", storagePreflight.destinationFilesystem.probePath},
                {"destinationFilesystemAvailableBytes", storagePreflight.destinationFilesystem.availableBytes}});
        if (!storagePreflight.sufficient) { result.error = storagePreflight.error; return result; }
        if (!updateManifestState(settings, QStringLiteral("stageA"))) {
            result.error = QStringLiteral("Could not update export ownership manifest before Stage A.");
            return result;
        }
        const FilesystemSnapshot temporaryFilesystemAtStart = filesystemSnapshot(temporaryOverlayPath);
        const FilesystemSnapshot destinationFilesystemAtStart = filesystemSnapshot(settings.outputPath);
        const auto cleanupTemporaryOverlay = qScopeGuard([&] {
            const QFileInfo temporaryInfo(temporaryOverlayPath);
            observe(settings, QStringLiteral("status"), QStringLiteral("cleaningUp"),
                    QStringLiteral("removeTemporaryOverlay"),
                    QStringLiteral("Cleaning temporary overlay"));
            observe(settings, QStringLiteral("log"), QStringLiteral("cleaningUp"),
                    QStringLiteral("removeTemporaryOverlay"),
                    QStringLiteral("Removing temporary overlay"), QStringLiteral("cleanup"),
                    {{"path", temporaryOverlayPath}, {"bytes", temporaryInfo.size()}});
            // The controller's manifest janitor is the only authority allowed
            // to remove worker-owned artifacts. Legacy direct callers retain
            // local temporary-file cleanup behavior.
            if (!settings.manifestPath.isEmpty()) {
                observe(settings, QStringLiteral("log"), QStringLiteral("cleaningUp"),
                        QStringLiteral("removeTemporaryOverlay"),
                        temporaryInfo.exists()
                            ? QStringLiteral("Temporary overlay cleanup deferred to ownership manifest")
                            : QStringLiteral("Temporary overlay already cleaned"),
                        QStringLiteral("cleanup"),
                        {{"result", temporaryInfo.exists() ? "deferred" : "alreadyAbsent"}});
                return;
            }
            const bool alreadyAbsent = !temporaryInfo.exists();
            const bool removed = alreadyAbsent || QFile::remove(temporaryOverlayPath);
            observe(settings, QStringLiteral("log"), QStringLiteral("cleaningUp"),
                    QStringLiteral("removeTemporaryOverlay"),
                    alreadyAbsent ? QStringLiteral("Temporary overlay already cleaned")
                        : removed ? QStringLiteral("Temporary overlay deleted")
                                  : QStringLiteral("Temporary overlay deletion failed"),
                    QStringLiteral("cleanup"),
                    {{"result", alreadyAbsent ? "alreadyAbsent" : removed ? "deleted" : "failed"}});
        });
        QProcess ffmpeg;
        // Keep FFmpeg in the worker's group, so a forced GUI-worker shutdown
        // reaches it as well as any inherited descendants.
        ExportProcessSupervisor ffmpegSupervisor(ffmpeg, false);
        QProcess *activeFfmpeg = &ffmpeg;
        ExportProcessSupervisor *activeSupervisor = &ffmpegSupervisor;
        const QString size = QStringLiteral("%1x%2").arg(outputSize.width()).arg(outputSize.height());
        const QStringList overlayArguments = {
            "-hide_banner", "-loglevel", "error", "-nostats", "-progress", "pipe:1", "-y",
            "-f", "rawvideo", "-pixel_format", "rgba", "-video_size", size,
            "-framerate", rateString(exportFrameRate), "-i", "pipe:0", "-an",
            "-c:v", "ffv1", "-pix_fmt", "bgra", "-f", "matroska", temporaryOverlayPath,
        };
        observe(settings, QStringLiteral("status"), QStringLiteral("renderingOverlay"),
                QStringLiteral("encodeTemporaryOverlay"),
                QStringLiteral("Encoding FFV1 temporary overlay"));
        observe(settings, QStringLiteral("log"), QStringLiteral("renderingOverlay"),
                QStringLiteral("encodeTemporaryOverlay"), QStringLiteral("Stage A started"),
                QStringLiteral("ffmpeg"),
                {{"executable", FfmpegTools::ffmpegPath()}, {"arguments", overlayArguments},
                 {"temporaryOverlayPath", temporaryOverlayPath}, {"temporaryOverlayAutoRemove", false},
                 {"temporaryFilesystemRoot", temporaryFilesystemAtStart.root},
                 {"temporaryFilesystemAvailableBytes", temporaryFilesystemAtStart.availableBytes},
                 {"temporaryFilesystemTotalBytes", temporaryFilesystemAtStart.totalBytes},
                 {"destinationFilesystemRoot", destinationFilesystemAtStart.root},
                 {"destinationFilesystemAvailableBytes", destinationFilesystemAtStart.availableBytes},
                 {"destinationFilesystemTotalBytes", destinationFilesystemAtStart.totalBytes}});
        ffmpeg.setProcessChannelMode(QProcess::SeparateChannels);
        qInfo().noquote() << QStringLiteral("Stage A FFmpeg arguments: %1").arg(formatArgumentList(overlayArguments));
        ffmpegSupervisor.start(FfmpegTools::ffmpegPath(), overlayArguments);
        if (!ffmpegSupervisor.waitForStarted()) {
            result.error = QStringLiteral("Could not start FFmpeg: %1").arg(ffmpeg.errorString());
            return result;
        }
        if (isCancelled(settings)) {
            result.cancelled = true;
            static_cast<void>(ffmpegSupervisor.stopAndWait());
            return result;
        }
        FfmpegProgressParser progressParser;
        FfmpegProgress lastFfmpegProgress;
        BoundedProcessOutput standardErrorOutput(BoundedProcessOutput::Mode::DiagnosticTail,
                                                 ProcessOutputLimits::ffmpegDiagnosticTailBytes);
        QElapsedTimer activityTimer;
        activityTimer.start();
        QElapsedTimer storageMonitorTimer;
        storageMonitorTimer.start();
        bool finalizing = false;
        bool compositing = false;
        int lastOverlayMilestone = -5;
        int lastCompositionMilestone = -5;
        QString stageAStdinCloseReason;
        const auto refreshTemporaryOverlaySize = [&] {
            result.temporaryOverlayBytes = QFileInfo(temporaryOverlayPath).size();
        };
        const auto reportProgress = [&] {
            if (!compositing) {
                refreshTemporaryOverlaySize();
            }
            const qint64 queuedBytes = static_cast<qint64>(activeFfmpeg->bytesToWrite());
            result.maximumQueuedBytes = qMax(result.maximumQueuedBytes, queuedBytes);
            if (!settings.progressCallback) return;
            ExportPipelineProgress progress;
            progress.generatedFrames = result.generatedFrames;
            progress.submittedFrames = result.renderedFrames;
            progress.expectedFrames = expectedFrames;
            progress.sourceRangeStart = sourceRangeStart;
            progress.sourceRangeEnd = sourceRangeEnd;
            progress.exportDuration = exportDuration;
            progress.exportRelativeTime = result.renderedFrames > 0
                ? ExportEngine::exportRelativeTime(result.renderedFrames - 1, exportFrameRate) : 0.0;
            progress.sourceVideoTime = sourceVideoTime(
                sourceRangeStart, result.renderedFrames > 0 ? result.renderedFrames - 1 : 0,
                exportFrameRate);
            progress.encodedFrames = lastFfmpegProgress.encodedFrames;
            progress.encodedSeconds = compositing
                ? qMax(0.0, lastFfmpegProgress.outputMicroseconds / 1'000'000.0) : 0.0;
            progress.outputDurationSeconds = exportDuration;
            progress.queuedBytes = queuedBytes;
            progress.maximumQueuedBytes = result.maximumQueuedBytes;
            progress.temporaryOverlayBytes = result.temporaryOverlayBytes;
            progress.encoderFps = lastFfmpegProgress.encoderFps;
            progress.encoderRealtimeFactor = lastFfmpegProgress.realtimeFactor;
            progress.stage = compositing && finalizing ? QStringLiteral("finalizing")
                : (compositing ? QStringLiteral("encodingVideo") : QStringLiteral("renderingOverlay"));
            settings.progressCallback(progress);
            const int percent = progress.expectedFrames > 0
                ? qBound(0, static_cast<int>(100 * (compositing ? progress.encodedFrames
                                                                : progress.submittedFrames)
                                                   / progress.expectedFrames), 100)
                : 0;
            int &lastMilestone = compositing ? lastCompositionMilestone : lastOverlayMilestone;
            const int milestone = (percent / 5) * 5;
            if (milestone >= lastMilestone + 5 || percent == 100) {
                lastMilestone = milestone;
                observe(settings, QStringLiteral("log"),
                        compositing ? QStringLiteral("encodingVideo")
                                    : QStringLiteral("renderingOverlay"),
                        compositing ? QStringLiteral("encodeFinalVideo")
                                    : QStringLiteral("renderTelemetryOverlay"),
                        QStringLiteral("%1: %2% · %3/%4")
                            .arg(compositing ? QStringLiteral("Final encode")
                                             : QStringLiteral("Overlay"))
                            .arg(milestone)
                            .arg(compositing ? progress.encodedFrames : progress.submittedFrames)
                            .arg(progress.expectedFrames),
                        QStringLiteral("ffmpeg"));
            }
        };
        const auto pumpFfmpeg = [&] {
            const QByteArray stdoutData = activeFfmpeg->readAllStandardOutput();
            const QByteArray stderrData = activeFfmpeg->readAllStandardError();
            if (!stdoutData.isEmpty() || !stderrData.isEmpty()) {
                activityTimer.restart();
            }
            standardErrorOutput.append(stderrData);
            const QList<FfmpegProgress> updates = progressParser.append(stdoutData);
            for (const FfmpegProgress &update : updates) {
                lastFfmpegProgress = update;
                if (update.complete || update.outputMicroseconds >= static_cast<qint64>(exportDuration * 995'000.0)) {
                    finalizing = true;
                }
                reportProgress();
            }
        };
        const auto recordUnexpectedStageAExit = [&](const QString &reason,
                                                    const QString &processErrorOverride = {},
                                                    const QString &processErrorStringOverride = {}) {
            // QProcess may transition to NotRunning before its final readyRead
            // signals are dispatched. Drain both channels before any cleanup or
            // diagnostic snapshot so FFmpeg's actual failure is not lost.
            pumpFfmpeg();
            refreshTemporaryOverlaySize();
            const FilesystemSnapshot temporaryFilesystemNow = filesystemSnapshot(temporaryOverlayPath);
            const FilesystemSnapshot destinationFilesystemNow = filesystemSnapshot(settings.outputPath);
            const qint64 queuedBytes = static_cast<qint64>(ffmpeg.bytesToWrite());
            result.maximumQueuedBytes = qMax(result.maximumQueuedBytes, queuedBytes);
            const StageAFailureDiagnostics diagnostics{
                reason,
                result.renderedFrames,
                expectedFrames,
                ffmpeg.exitCode(),
                processExitStatusName(ffmpeg.exitStatus()),
                processErrorOverride.isEmpty() ? processErrorName(ffmpeg.error()) : processErrorOverride,
                processErrorStringOverride.isEmpty() ? ffmpeg.errorString() : processErrorStringOverride,
                lastFfmpegProgress.encodedFrames,
                lastFfmpegProgress.outputMicroseconds,
                lastFfmpegProgress.encoderFps,
                lastFfmpegProgress.realtimeFactor,
                queuedBytes,
                result.maximumQueuedBytes,
                temporaryOverlayPath,
                result.temporaryOverlayBytes,
                standardErrorOutput.text(),
                temporaryFilesystemNow.root,
                temporaryFilesystemNow.availableBytes,
                temporaryFilesystemNow.totalBytes,
                destinationFilesystemNow.root,
                destinationFilesystemNow.availableBytes,
                destinationFilesystemNow.totalBytes,
                settings.cancellationFilePath,
                isCancelled(settings)};
            result.diagnostics = formatStageAFailureDiagnostics(diagnostics);
            observe(settings, QStringLiteral("log"), QStringLiteral("renderingOverlay"),
                    QStringLiteral("encodeTemporaryOverlay"),
                    QStringLiteral("Stage A exited unexpectedly"), QStringLiteral("ffmpeg"),
                    stageAFailureDiagnosticDetails(diagnostics));
        };
        const auto cancelFfmpeg = [&] {
            result.cancelled = true;
            if (!compositing && stageAStdinCloseReason.isEmpty()) {
                stageAStdinCloseReason = QStringLiteral("export cancellation after %1 of %2 frames submitted")
                                            .arg(result.renderedFrames).arg(expectedFrames);
                activeFfmpeg->closeWriteChannel();
            }
            static_cast<void>(activeSupervisor->stopAndWait());
        };
        const auto checkTemporarySpace = [&] {
            if (storageMonitorTimer.elapsed() < 1'000) return true;
            storageMonitorTimer.restart();
            const ExportFilesystemInfo filesystem = ExportStoragePolicy::filesystemForPath(temporaryOverlayPath);
            observe(settings, QStringLiteral("log"), compositing ? QStringLiteral("encodingVideo")
                : QStringLiteral("renderingOverlay"), QStringLiteral("monitorStorage"),
                QStringLiteral("Temporary volume free-space sample"), QStringLiteral("storage"),
                {{"temporaryFilesystemRoot", filesystem.rootPath},
                 {"temporaryFilesystemAvailableBytes", filesystem.availableBytes}});
            if (!ExportStoragePolicy::criticallyLow(filesystem, storageEstimate.safetyReserveBytes)) return true;
            result.error = QStringLiteral("Not enough free space for export. Temporary filesystem %1 fell below the %2 safety reserve.")
                .arg(filesystem.rootPath, ExportStoragePolicy::bytesText(storageEstimate.safetyReserveBytes));
            static_cast<void>(activeSupervisor->stopAndWait());
            return false;
        };
        const RawFrameTransportConfig transportConfig;
        RawFrameTransport transport(
            ffmpeg, transportConfig, [&settings] { return isCancelled(settings); },
            [&] { pumpFfmpeg(); reportProgress(); },
            [&result](const qint64, const qint64 maximumQueuedBytes) {
                result.maximumQueuedBytes = qMax(result.maximumQueuedBytes, maximumQueuedBytes);
            });
        observe(settings, QStringLiteral("log"), QStringLiteral("renderingOverlay"),
                QStringLiteral("configureRawFrameTransport"),
                QStringLiteral("Bounded raw-frame transport configured"), QStringLiteral("transport"),
                {{"highWaterBytes", transportConfig.highWaterBytes},
                 {"lowWaterBytes", transportConfig.lowWaterBytes},
                 {"writeChunkBytes", static_cast<qint64>(transportConfig.writeChunkBytes)},
                 {"stallTimeoutMilliseconds", transportConfig.stallTimeoutMilliseconds}});
        for (qsizetype frameIndex = 0; frameIndex < expectedFrames; ++frameIndex) {
            if (isCancelled(settings)) {
                cancelFfmpeg();
                return result;
            }
            pumpFfmpeg();
            if (!checkTemporarySpace()) return result;
            if (ffmpeg.state() == QProcess::NotRunning) {
                result.error = QStringLiteral("Temporary overlay encoder exited early: %1 of %2 frames submitted.")
                                   .arg(result.renderedFrames).arg(expectedFrames);
                recordUnexpectedStageAExit(QStringLiteral("FFmpeg stopped before the next overlay frame was rendered"));
                return result;
            }
            const double currentSourceVideoTime = sourceVideoTime(
                sourceRangeStart, frameIndex, exportFrameRate);
            QElapsedTimer renderTimer;
            renderTimer.start();
            const QImage image = renderer.renderFrame(currentSourceVideoTime);
            result.renderNanoseconds += renderTimer.nsecsElapsed();
            if (image.size() != outputSize) {
                result.error = renderer.errorString().isEmpty()
                    ? QStringLiteral("Telemetry renderer returned an invalid frame.")
                    : renderer.errorString();
                static_cast<void>(ffmpegSupervisor.stopAndWait());
                return result;
            }
            ++result.generatedFrames;
            QElapsedTimer copyTimer;
            copyTimer.start();
            const QByteArray bytes = rgbaBytes(image, outputSize);
            result.cpuCopyNanoseconds += copyTimer.nsecsElapsed();
            QElapsedTimer writeTimer;
            writeTimer.start();
            const RawFrameTransportResult writeResult = transport.writeFrame(bytes);
            if (writeResult.status == RawFrameTransportResult::Status::Cancelled) {
                cancelFfmpeg();
                return result;
            }
            if (!writeResult.succeeded()) {
                result.error = QStringLiteral("Could not stream overlay frame to FFmpeg: %1")
                                   .arg(writeResult.error);
                // Capture the original QProcess error before intentionally
                // stopping a still-running child, then drain its final stderr.
                const QString writeError = processErrorName(ffmpeg.error());
                const QString writeErrorString = ffmpeg.errorString();
                if (ffmpeg.state() != QProcess::NotRunning) {
                    static_cast<void>(ffmpegSupervisor.stopAndWait());
                }
                recordUnexpectedStageAExit(
                    writeResult.error,
                    writeError, writeErrorString);
                return result;
            }
            result.ffmpegWriteNanoseconds += writeTimer.nsecsElapsed();
            ++result.renderedFrames;
            reportProgress();
        }
        Q_ASSERT(result.renderedFrames == expectedFrames);
        stageAStdinCloseReason = QStringLiteral("all %1 expected overlay frames submitted").arg(expectedFrames);
        observe(settings, QStringLiteral("log"), QStringLiteral("renderingOverlay"),
                QStringLiteral("flushTemporaryOverlay"), QStringLiteral("Stage A stdin closed"),
                QStringLiteral("ffmpeg"), {{"reason", stageAStdinCloseReason}});
        observe(settings, QStringLiteral("status"), QStringLiteral("renderingOverlay"),
                QStringLiteral("flushTemporaryOverlay"),
                QStringLiteral("Flushing temporary overlay container"));
        ffmpeg.closeWriteChannel();
        // FFmpeg may still be encoding a bounded amount of raw input and then
        // muxing. There is deliberately no fixed total timeout: only a generous
        // no-activity watchdog detects an actual encoder stall.
        while (ffmpeg.state() != QProcess::NotRunning) {
            if (isCancelled(settings)) {
                cancelFfmpeg();
                return result;
            }
            ffmpeg.waitForFinished(250);
            pumpFfmpeg();
            reportProgress();
            if (!checkTemporarySpace()) return result;
            if (activityTimer.elapsed() > 300'000) {
                result.error = QStringLiteral("FFmpeg encoder appears stalled (no progress for five minutes).");
                static_cast<void>(ffmpegSupervisor.stopAndWait());
                pumpFfmpeg();
                result.diagnostics = formatDiagnostics(
                    ffmpeg, lastFfmpegProgress, standardErrorOutput.text(), QStringLiteral("Stage A overlay"), overlayArguments,
                    temporaryOverlayPath, result.temporaryOverlayBytes, stageAStdinCloseReason);
                return result;
            }
        }
        pumpFfmpeg();
        if (ffmpeg.exitStatus() != QProcess::NormalExit || ffmpeg.exitCode() != 0) {
            result.error = QStringLiteral("Temporary overlay encoder failed.");
            recordUnexpectedStageAExit(QStringLiteral("FFmpeg returned a nonzero or abnormal exit after Stage A stdin closed"));
            return result;
        }
        observe(settings, QStringLiteral("log"), QStringLiteral("renderingOverlay"),
                QStringLiteral("flushTemporaryOverlay"),
                QStringLiteral("Stage A FFmpeg exited · code %1").arg(ffmpeg.exitCode()),
                QStringLiteral("ffmpeg"), {{"exitCode", ffmpeg.exitCode()},
                                            {"stderr", standardErrorOutput.text()}});
        if (!QFileInfo(temporaryOverlayPath).isFile()
            || QFileInfo(temporaryOverlayPath).size() <= 0) {
            result.error = QStringLiteral("FFmpeg did not create the temporary telemetry overlay.");
            return result;
        }
        refreshTemporaryOverlaySize();
        if (result.generatedFrames != expectedFrames || result.renderedFrames != expectedFrames) {
            result.error = QStringLiteral("Temporary telemetry overlay frame count is incomplete.");
            result.diagnostics = QStringLiteral("Generated %1, submitted %2, expected %3 telemetry frames; "
                                                "FFmpeg stored %4 temporary overlay frames.")
                                     .arg(result.generatedFrames).arg(result.renderedFrames).arg(expectedFrames)
                                     .arg(lastFfmpegProgress.encodedFrames);
            return result;
        }
        qsizetype stagedEncodedFrames = lastFfmpegProgress.encodedFrames;
        QString stagedFrameCountSource = QStringLiteral("FFmpeg final progress");
        if (!lastFfmpegProgress.complete) {
            observe(settings, QStringLiteral("status"), QStringLiteral("validatingOverlay"),
                    QStringLiteral("countTemporaryOverlayPackets"),
                    QStringLiteral("FFmpeg final progress was incomplete; counting temporary overlay packets"));
            observe(settings, QStringLiteral("log"), QStringLiteral("validatingOverlay"),
                    QStringLiteral("countTemporaryOverlayPackets"),
                    QStringLiteral("Temporary overlay packet-count fallback started"),
                    QStringLiteral("validation"));
            const MediaInfo packetCountInfo = MediaProbe::probe(
                temporaryOverlayPath, {}, false, 30'000,
                probeObservations(settings, QStringLiteral("validatingOverlay"),
                                  QStringLiteral("countTemporaryOverlayPackets")),
                [&settings] { return isCancelled(settings); }, true);
            stagedEncodedFrames = packetCountInfo.videoPacketCount;
            stagedFrameCountSource = QStringLiteral("FFmpeg packet-count fallback");
        }
        if (stagedEncodedFrames != expectedFrames) {
            result.error = QStringLiteral("FFmpeg did not report every expected temporary overlay frame.");
            result.diagnostics = QStringLiteral(
                "Generated %1, submitted %2, FFmpeg encoded %3, expected %4 temporary overlay frames.")
                                     .arg(result.generatedFrames).arg(result.renderedFrames)
                                     .arg(stagedEncodedFrames).arg(expectedFrames);
            return result;
        }
        if (settings.stateCallback) {
            settings.stateCallback(QStringLiteral("validating"));
        }
        if (!updateManifestState(settings, QStringLiteral("validating"))) {
            result.error = QStringLiteral("Could not update export ownership manifest before validation.");
            return result;
        }
        observe(settings, QStringLiteral("status"), QStringLiteral("validatingOverlay"),
                QStringLiteral("probeTemporaryOverlayMetadata"),
                QStringLiteral("Reading temporary overlay metadata with ffprobe"));
        observe(settings, QStringLiteral("log"), QStringLiteral("validatingOverlay"),
                QStringLiteral("probeTemporaryOverlayMetadata"),
                QStringLiteral("Temporary overlay metadata validation started"), QStringLiteral("validation"),
                {{"frameCountSource", QStringLiteral("producer + %1").arg(stagedFrameCountSource)},
                 {"generatedFrames", static_cast<qint64>(result.generatedFrames)},
                 {"submittedFrames", static_cast<qint64>(result.renderedFrames)},
                 {"encodedFrames", static_cast<qint64>(stagedEncodedFrames)},
                 {"expectedFrames", static_cast<qint64>(expectedFrames)}});
        QElapsedTimer temporaryValidationTimer;
        temporaryValidationTimer.start();
        const MediaInfo temporaryOverlayInfo = MediaProbe::probeSummary(
            temporaryOverlayPath, {}, 30'000,
            probeObservations(settings, QStringLiteral("validatingOverlay"),
                              QStringLiteral("probeTemporaryOverlayMetadata")),
            [&settings] { return isCancelled(settings); });
        const TemporaryOverlayValidationResult temporaryValidation =
            TemporaryOverlayValidation::validate(
                temporaryOverlayInfo, outputSize, exportFrameRate, expectedFrames, exportDuration);
        const double frameInterval = 1.0 / exportFrameRate.value();
        const auto validationLog = [&](const QString &name, const QVariant &expected,
                                       const QVariant &actual, const bool passed) {
            observe(settings, QStringLiteral("log"), QStringLiteral("validatingOverlay"),
                    QStringLiteral("validateTemporaryOverlay"),
                    QStringLiteral("%1 expected=%2 actual=%3 %4")
                        .arg(name, expected.toString(), actual.toString(),
                             passed ? QStringLiteral("PASS") : QStringLiteral("FAIL")),
                    QStringLiteral("validation"),
                    {{"check", name}, {"expected", expected}, {"actual", actual},
                     {"passed", passed}});
        };
        validationLog(QStringLiteral("Temporary codec"), QStringLiteral("ffv1"),
                      temporaryOverlayInfo.videoCodec, temporaryValidation.codecOk);
        validationLog(QStringLiteral("Temporary resolution"),
                      QStringLiteral("%1x%2").arg(outputSize.width()).arg(outputSize.height()),
                      QStringLiteral("%1x%2").arg(temporaryOverlayInfo.videoSize.width())
                                               .arg(temporaryOverlayInfo.videoSize.height()), temporaryValidation.dimensionsOk);
        validationLog(QStringLiteral("Temporary nominal rate"), rateString(exportFrameRate),
                      rateString(temporaryOverlayInfo.frameRate), temporaryValidation.nominalRateOk);
        validationLog(QStringLiteral("Temporary observed average rate"), rateString(exportFrameRate),
                      rateString(temporaryOverlayInfo.averageFrameRate), temporaryValidation.averageRateOk);
        validationLog(QStringLiteral("Temporary encoded frames"), expectedFrames,
                      stagedEncodedFrames, true);
        validationLog(QStringLiteral("Temporary duration"), exportDuration,
                      temporaryOverlayInfo.duration, temporaryValidation.durationOk);
        validationLog(QStringLiteral("Temporary video start"), QStringLiteral("0"),
                      temporaryOverlayInfo.videoStartTime, temporaryValidation.startTimeOk);
        observe(settings, QStringLiteral("log"), QStringLiteral("validatingOverlay"),
                QStringLiteral("probeTemporaryOverlayMetadata"),
                QStringLiteral("Temporary overlay metadata validation elapsed %1 ms")
                    .arg(temporaryValidationTimer.elapsed()), QStringLiteral("validation"),
                {{"validationElapsedMilliseconds", temporaryValidationTimer.elapsed()},
                 {"frameCountSource", QStringLiteral("producer + %1").arg(stagedFrameCountSource)},
                 {"scheduledRate", rateString(exportFrameRate)},
                 {"reportedNominalRate", rateString(temporaryOverlayInfo.frameRate)},
                 {"reportedAverageRate", rateString(temporaryOverlayInfo.averageFrameRate)},
                 {"timeBase", rateString(temporaryOverlayInfo.timeBase)},
                 {"timestampToleranceMicroseconds", temporaryValidation.timestampToleranceMicroseconds},
                 {"rateTolerance", temporaryValidation.rateTolerance}});
        if (!temporaryValidation.passed()) {
            result.error = QStringLiteral("Temporary telemetry overlay failed validation.");
            result.diagnostics = QStringLiteral(
                "Expected FFV1 %1x%2 at scheduled %3 fps, %4 frames, %5 s; staged %6 %7x%8 with nominal %9 fps, average %10 fps, "
                "time base %11, duration %12 s, %13 bytes. Timestamp tolerance=%14 us.\n"
                "FFmpeg arguments: %15")
                                     .arg(outputSize.width()).arg(outputSize.height())
                                     .arg(exportFrameRate.value(), 0, 'f', 6).arg(expectedFrames)
                                     .arg(exportDuration, 0, 'f', 6)
                                     .arg(temporaryOverlayInfo.videoCodec)
                                     .arg(temporaryOverlayInfo.videoSize.width())
                                     .arg(temporaryOverlayInfo.videoSize.height())
                                     .arg(temporaryOverlayInfo.frameRate.value(), 0, 'f', 6)
                                     .arg(temporaryOverlayInfo.averageFrameRate.value(), 0, 'f', 6)
                                     .arg(rateString(temporaryOverlayInfo.timeBase))
                                     .arg(temporaryOverlayInfo.duration, 0, 'f', 6)
                                     .arg(result.temporaryOverlayBytes)
                                     .arg(temporaryValidation.timestampToleranceMicroseconds)
                                     .arg(formatArgumentList(overlayArguments));
            return result;
        }
        observe(settings, QStringLiteral("log"), QStringLiteral("validatingOverlay"),
                QStringLiteral("validateTemporaryOverlay"),
                QStringLiteral("Temporary overlay validation passed"));
        // Framesync selects the latest secondary frame at or before each primary
        // timestamp. A live secondary pipe can therefore repeat stale telemetry
        // while the primary decoder advances. Compose only after a complete,
        // rational-rate FFV1/BGRA overlay stream exists on disk.
        QProcess compositor;
        ExportProcessSupervisor compositorSupervisor(compositor, false);
        activeFfmpeg = &compositor;
        activeSupervisor = &compositorSupervisor;
        progressParser = FfmpegProgressParser{};
        lastFfmpegProgress = {};
        standardErrorOutput = BoundedProcessOutput(BoundedProcessOutput::Mode::DiagnosticTail,
                                                   ProcessOutputLimits::ffmpegDiagnosticTailBytes);
        activityTimer.restart();
        compositing = true;
        finalizing = false;
        const auto sourceAccess = stageBSourceAccess(source, scheduledRange, exportFrameRate);
        if (!sourceAccess) {
            result.error = QStringLiteral("Could not derive exact Stage B source timestamps from the frame range.");
            return result;
        }
        const QString timeRangeFilter = stageBVideoFilterGraph(
            *sourceAccess, source.videoSize, outputSize, exportFrameRate,
            expectedFrames, result.mediaProfile, composition);
        QStringList compositionArguments = {
            "-hide_banner", "-loglevel", "error", "-nostats", "-progress", "pipe:1", "-y",
        };
        if (chapters.isEmpty()) {
            compositionArguments += stageBInputArguments(*sourceAccess, settings.inputPath);
        } else {
            const QStringList chapterInput = ChapterSource::inputArguments(chapters, sourceAccess->inputSeekTimestamp);
            if (chapterInput.isEmpty()) {
                result.error = QStringLiteral("Could not express the export range across the video's chapters exactly.");
                return result;
            }
            compositionArguments += chapterInput;
        }
        compositionArguments += QStringList{"-i", temporaryOverlayPath};
        compositionArguments += additionalInputArguments;
        compositionArguments += QStringList{
            "-filter_complex", timeRangeFilter,
            "-map", "[video]", "-fps_mode:v", "cfr", "-c:v", encoder,
            "-b:v", QString::number(settings.videoBitrate),
            "-tag:v", "hvc1", "-profile:v", result.mediaProfile.encoderProfile,
            "-pix_fmt", result.mediaProfile.outputPixelFormat,
        };
        const auto appendColorOption = [&compositionArguments](const QString &option, const QString &value) {
            if (!value.isEmpty()) compositionArguments.append({option, value});
        };
        appendColorOption(QStringLiteral("-color_range"), result.mediaProfile.colorRange);
        appendColorOption(QStringLiteral("-colorspace"), result.mediaProfile.colorSpace);
        appendColorOption(QStringLiteral("-color_trc"), result.mediaProfile.colorTransfer);
        appendColorOption(QStringLiteral("-color_primaries"), result.mediaProfile.colorPrimaries);
        if (encoder == QStringLiteral("libx265")) {
            QStringList x265ColorParameters;
            if (!result.mediaProfile.colorPrimaries.isEmpty()) {
                x265ColorParameters.append(
                    QStringLiteral("colorprim=%1").arg(result.mediaProfile.colorPrimaries));
            }
            if (!result.mediaProfile.colorTransfer.isEmpty()) {
                x265ColorParameters.append(
                    QStringLiteral("transfer=%1").arg(result.mediaProfile.colorTransfer));
            }
            if (!result.mediaProfile.colorSpace.isEmpty()) {
                x265ColorParameters.append(
                    QStringLiteral("colormatrix=%1").arg(result.mediaProfile.colorSpace));
            }
            if (result.mediaProfile.colorRange == QStringLiteral("tv")) {
                x265ColorParameters.append(QStringLiteral("range=limited"));
            } else if (result.mediaProfile.colorRange == QStringLiteral("pc")) {
                x265ColorParameters.append(QStringLiteral("range=full"));
            }
            if (!x265ColorParameters.isEmpty()) {
                compositionArguments.append(
                    {QStringLiteral("-x265-params"), x265ColorParameters.join(':')});
            }
        }
        if (settings.audioEnabled && !source.audioCodecs.isEmpty()
            && audioDurationForRange(source, sourceRangeStart, sourceRangeEnd) > 0.0) {
            compositionArguments[compositionArguments.indexOf("-filter_complex") + 1] += ";" + stageBAudioFilterGraph(*sourceAccess, !chapters.isEmpty());
            compositionArguments.append({"-map", "[audio]", "-c:a", "aac", "-b:a", QString::number(settings.audioBitrate)});
        } else {
            compositionArguments.append("-an");
        }
        compositionArguments.append(settings.outputPath);
        observe(settings, QStringLiteral("status"), QStringLiteral("encodingVideo"),
                QStringLiteral("startFinalComposition"),
                QStringLiteral("Starting final HEVC composition"));
        observe(settings, QStringLiteral("log"), QStringLiteral("encodingVideo"),
                QStringLiteral("startFinalComposition"), QStringLiteral("Stage B started"),
                QStringLiteral("ffmpeg"), {{"executable", FfmpegTools::ffmpegPath()},
                                             {"arguments", compositionArguments},
                                             {"sourceRangeStart", sourceRangeStart},
                                             {"sourceRangeEnd", sourceRangeEnd},
                                             {"inputSeekTimestamp", sourceAccess->inputSeekTimestamp},
                                             {"trimStartTimestamp", sourceAccess->trimStartTimestamp},
                                             {"trimEndTimestamp", sourceAccess->trimEndTimestamp}});
        compositor.setProcessChannelMode(QProcess::SeparateChannels);
        qInfo().noquote() << QStringLiteral("Stage B FFmpeg arguments: %1").arg(formatArgumentList(compositionArguments));
        if (!updateManifestState(settings, QStringLiteral("stageB"))) {
            result.error = QStringLiteral("Could not update export ownership manifest before Stage B.");
            return result;
        }
        compositorSupervisor.start(FfmpegTools::ffmpegPath(), compositionArguments);
        if (!compositorSupervisor.waitForStarted()) {
            result.error = QStringLiteral("Could not start FFmpeg composition: %1").arg(compositor.errorString());
            return result;
        }
        QElapsedTimer stageBTimer;
        stageBTimer.start();
        bool receivedFirstStageBOutputFrame = false;
        while (compositor.state() != QProcess::NotRunning) {
            if (isCancelled(settings)) {
                cancelFfmpeg();
                return result;
            }
            compositor.waitForFinished(250);
            pumpFfmpeg();
            if (!receivedFirstStageBOutputFrame && lastFfmpegProgress.encodedFrames > 0) {
                receivedFirstStageBOutputFrame = true;
                observe(settings, QStringLiteral("log"), QStringLiteral("encodingVideo"),
                        QStringLiteral("firstStageBOutputFrame"),
                        QStringLiteral("Stage B produced its first output frame in %1 ms")
                            .arg(stageBTimer.elapsed()), QStringLiteral("ffmpeg"),
                        {{"timeToFirstStageBOutputFrame", stageBTimer.elapsed()},
                         {"inputSeekTimestamp", sourceAccess->inputSeekTimestamp},
                         {"trimStartTimestamp", sourceAccess->trimStartTimestamp},
                         {"sourceRangeStart", sourceRangeStart},
                         {"sourceRangeEnd", sourceRangeEnd}});
            }
            reportProgress();
            if (!checkTemporarySpace()) return result;
            if (lastFfmpegProgress.encodedFrames > result.renderedFrames) {
                result.error = QStringLiteral("FFmpeg output advanced beyond available telemetry overlay frames.");
                static_cast<void>(compositorSupervisor.stopAndWait());
                pumpFfmpeg();
                result.diagnostics = formatDiagnostics(
                    compositor, lastFfmpegProgress, standardErrorOutput.text(), QStringLiteral("Stage B composition"),
                    compositionArguments, temporaryOverlayPath, result.temporaryOverlayBytes,
                    stageAStdinCloseReason);
                return result;
            }
            if (activityTimer.elapsed() > 300'000) {
                result.error = QStringLiteral("FFmpeg encoder appears stalled (no progress for five minutes).");
                static_cast<void>(compositorSupervisor.stopAndWait());
                pumpFfmpeg();
                result.diagnostics = formatDiagnostics(
                    compositor, lastFfmpegProgress, standardErrorOutput.text(), QStringLiteral("Stage B composition"),
                    compositionArguments, temporaryOverlayPath, result.temporaryOverlayBytes,
                    stageAStdinCloseReason);
                return result;
            }
        }
        pumpFfmpeg();
        observe(settings, QStringLiteral("status"), QStringLiteral("encodingVideo"),
                QStringLiteral("flushOutputContainer"), QStringLiteral("Flushing MP4 container"));
        if (compositor.exitStatus() != QProcess::NormalExit || compositor.exitCode() != 0) {
            result.error = QStringLiteral("FFmpeg composition failed.");
            result.diagnostics = formatDiagnostics(
                compositor, lastFfmpegProgress, standardErrorOutput.text(), QStringLiteral("Stage B composition"),
                compositionArguments, temporaryOverlayPath, result.temporaryOverlayBytes,
                stageAStdinCloseReason);
            return result;
        }
        observe(settings, QStringLiteral("log"), QStringLiteral("encodingVideo"),
                QStringLiteral("flushOutputContainer"),
                QStringLiteral("Stage B FFmpeg exited · code %1").arg(compositor.exitCode()),
                QStringLiteral("ffmpeg"), {{"exitCode", compositor.exitCode()},
                                            {"stderr", standardErrorOutput.text()}});
        if (lastFfmpegProgress.encodedFrames != expectedFrames) {
            observe(settings, QStringLiteral("log"), QStringLiteral("validatingOutput"),
                    QStringLiteral("compareProgressFrameCount"),
                    QStringLiteral("FFmpeg progress frame count differs from the schedule; final packet count will decide validation"),
                    QStringLiteral("validation"),
                    {{"expectedFrames", static_cast<qint64>(expectedFrames)},
                     {"progressFrames", static_cast<qint64>(lastFfmpegProgress.encodedFrames)}});
        }
        observe(settings, QStringLiteral("status"), QStringLiteral("validatingOutput"),
                QStringLiteral("checkOutputExists"), QStringLiteral("Checking output file exists"));
        const QFileInfo outputInfo(settings.outputPath);
        const bool outputExists = outputInfo.isFile();
        observe(settings, QStringLiteral("log"), QStringLiteral("validatingOutput"),
                QStringLiteral("checkOutputExists"),
                QStringLiteral("Output file exists actual=%1 %2")
                    .arg(outputExists ? QStringLiteral("yes") : QStringLiteral("no"),
                         outputExists ? QStringLiteral("PASS") : QStringLiteral("FAIL")),
                QStringLiteral("validation"), {{"passed", outputExists}});
        observe(settings, QStringLiteral("status"), QStringLiteral("validatingOutput"),
                QStringLiteral("checkOutputSize"), QStringLiteral("Checking output file size"));
        const bool outputSizeOk = outputInfo.size() > 0;
        observe(settings, QStringLiteral("log"), QStringLiteral("validatingOutput"),
                QStringLiteral("checkOutputSize"),
                QStringLiteral("Output size actual=%1 bytes %2")
                    .arg(outputInfo.size()).arg(outputSizeOk ? QStringLiteral("PASS")
                                                            : QStringLiteral("FAIL")),
                QStringLiteral("validation"), {{"bytes", outputInfo.size()}, {"passed", outputSizeOk}});
        if (!outputExists || !outputSizeOk) {
            result.error = QStringLiteral("FFmpeg did not create an output file.");
            return result;
        }
        result.outputBytes = outputInfo.size();
        result.encodedFrames = lastFfmpegProgress.encodedFrames;
        result.encodedSeconds = qMax(0.0, lastFfmpegProgress.outputMicroseconds / 1'000'000.0);
        const TelemetryFrameRenderer::TimingMetrics rendererMetrics = renderer.timingMetrics();
        result.polishNanoseconds = rendererMetrics.polishNanoseconds;
        result.syncRenderNanoseconds = rendererMetrics.syncRenderNanoseconds;
        result.readbackNanoseconds = rendererMetrics.readbackNanoseconds;
        if (settings.stateCallback) {
            settings.stateCallback(QStringLiteral("validating"));
        }
        observe(settings, QStringLiteral("status"), QStringLiteral("validatingOutput"),
                QStringLiteral("probeFinalOutput"),
                QStringLiteral("Reading output metadata with ffprobe"));
        observe(settings, QStringLiteral("log"), QStringLiteral("validatingOutput"),
                QStringLiteral("probeFinalOutput"), QStringLiteral("Final validation started"));
        try {
            result.mediaInfo = MediaProbe::probe(
                settings.outputPath, {}, false,
                finalValidationTimeoutMilliseconds(QFileInfo(settings.outputPath).size()),
                probeObservations(settings, QStringLiteral("validatingOutput"),
                                  QStringLiteral("probeFinalOutput")),
                [&settings] { return isCancelled(settings); }, true);
        } catch (const OperationCancelled &) {
            result.cancelled = true;
            return result;
        } catch (const std::exception &error) {
            result.error = QStringLiteral("Automatic media validation failed; the staged output was not committed.");
            result.diagnostics = QString::fromUtf8(error.what());
            return result;
        }
        const bool codecOk = result.mediaInfo.videoCodec == QStringLiteral("hevc");
        const bool dimensionsOk = result.mediaInfo.videoSize == outputSize;
        const bool averageRateOk = result.mediaInfo.averageFrameRate.isEquivalentTo(exportFrameRate);
        const bool nominalRateOk = result.mediaInfo.frameRate.isEquivalentTo(exportFrameRate);
        const bool pixelFormatOk = result.mediaProfile.acceptsOutputPixelFormat(
            result.mediaInfo.pixelFormat);
        const bool bitDepthOk = result.mediaInfo.bitDepth
            && *result.mediaInfo.bitDepth == result.mediaProfile.outputBitDepth;
        const QString actualProfile = result.mediaInfo.videoCodecProfile.toLower().remove(' ').remove('_');
        const QString expectedProfile = result.mediaProfile.encoderProfile.toLower().remove(' ').remove('_');
        const bool encoderProfileOk = !actualProfile.isEmpty()
            && (actualProfile == expectedProfile
                || (expectedProfile == QStringLiteral("main10")
                    && actualProfile.startsWith(QStringLiteral("main10"))));
        const auto preservedTag = [](const QString &expected, const QString &actual) {
            return expected.isEmpty() || expected == actual;
        };
        const bool colorRangeOk = preservedTag(result.mediaProfile.colorRange, result.mediaInfo.colorRange);
        const bool colorSpaceOk = preservedTag(result.mediaProfile.colorSpace, result.mediaInfo.colorSpace);
        const bool colorTransferOk = preservedTag(result.mediaProfile.colorTransfer, result.mediaInfo.colorTransfer);
        const bool colorPrimariesOk = preservedTag(result.mediaProfile.colorPrimaries, result.mediaInfo.colorPrimaries);
        const bool packetCountAvailable = result.mediaInfo.videoPacketCount > 0;
        const qint64 finalFrameCount = packetCountAvailable
            ? static_cast<qint64>(result.mediaInfo.videoPacketCount) : 0;
        const qint64 expectedFrameCount = static_cast<qint64>(expectedFrames);
        const double videoDuration = result.mediaInfo.videoDuration > 0.0
            ? result.mediaInfo.videoDuration : result.mediaInfo.duration;
        const bool audioExpected = settings.audioEnabled && !source.audioCodecs.isEmpty()
            && audioDurationForRange(source, sourceRangeStart, sourceRangeEnd) > 0.0;
        // Audio can legitimately end before the video stream (as on real action-camera
        // recordings). Validate against the selected audio-timeline intersection.
        const double expectedAudioDuration = audioDurationForRange(
            source, sourceRangeStart, sourceRangeEnd);
        const double audioFrameDuration = result.mediaInfo.audioSampleRate > 0
            ? 1024.0 / result.mediaInfo.audioSampleRate : frameInterval;
        const double audioTimingTolerance = qMax(
            result.mediaInfo.audioTimeBase.isValid() ? result.mediaInfo.audioTimeBase.value() : 0.0,
            audioFrameDuration);
        const bool audioPresent = !result.mediaInfo.audioCodecs.isEmpty();
        const double expectedAudioStart = audioStartForRange(source, sourceRangeStart, sourceRangeEnd);
        const bool audioStartOk = !audioExpected
            || qAbs(result.mediaInfo.audioStartTime - result.mediaInfo.videoStartTime - expectedAudioStart)
                <= audioTimingTolerance + 1e-6;
        const bool audioDurationOk = !audioExpected
            || qAbs(result.mediaInfo.audioDuration - expectedAudioDuration) <= audioTimingTolerance + 1e-6;
        const bool audioOk = !audioExpected || (audioPresent && audioStartOk && audioDurationOk);
        const bool otherValidationPassed = codecOk && dimensionsOk && averageRateOk && nominalRateOk
            && pixelFormatOk && bitDepthOk && encoderProfileOk && colorRangeOk && colorSpaceOk
            && colorTransferOk && colorPrimariesOk && audioOk;
        const FinalOutputValidationResult outputValidation = FinalOutputValidation::evaluate({
            expectedFrameCount,
            static_cast<qint64>(result.generatedFrames),
            static_cast<qint64>(result.renderedFrames),
            static_cast<qint64>(stagedEncodedFrames),
            lastFfmpegProgress.encodedFramesAvailable
                ? std::optional<qint64>(static_cast<qint64>(lastFfmpegProgress.encodedFrames))
                : std::nullopt,
            finalFrameCount,
            result.mediaInfo,
            exportFrameRate,
            otherValidationPassed,
            outputExists && !settings.manifestPath.isEmpty(),
        });
        result.encodedFrames = packetCountAvailable ? result.mediaInfo.videoPacketCount : 0;
        result.finalFrameCount = finalFrameCount;
        result.frameDeficit = outputValidation.frameDeficit;
        const bool packetCountOk = outputValidation.accepted();
        const bool videoStartOk = outputValidation.startsAtOrigin;
        const bool durationOk = outputValidation.contiguousCfrTiming;
        const auto finalValidationLog = [&](const QString &operation, const QString &name,
                                            const QVariant &expected, const QVariant &actual,
                                            const bool passed) {
            observe(settings, QStringLiteral("status"), QStringLiteral("validatingOutput"), operation,
                    QStringLiteral("Checking %1").arg(name.toLower()));
            observe(settings, QStringLiteral("log"), QStringLiteral("validatingOutput"), operation,
                    QStringLiteral("%1 expected=%2 actual=%3 %4")
                        .arg(name, expected.toString(), actual.toString(),
                             passed ? QStringLiteral("PASS") : QStringLiteral("FAIL")),
                    QStringLiteral("validation"),
                    {{"check", name}, {"expected", expected}, {"actual", actual},
                     {"passed", passed}});
        };
        finalValidationLog(QStringLiteral("checkVideoCodec"), QStringLiteral("Video codec"),
                           QStringLiteral("hevc"), result.mediaInfo.videoCodec, codecOk);
        finalValidationLog(QStringLiteral("checkDimensions"), QStringLiteral("Resolution"),
                           QStringLiteral("%1x%2").arg(outputSize.width()).arg(outputSize.height()),
                           QStringLiteral("%1x%2").arg(result.mediaInfo.videoSize.width())
                                                    .arg(result.mediaInfo.videoSize.height()), dimensionsOk);
        finalValidationLog(QStringLiteral("checkDuration"), QStringLiteral("Duration"),
                           QString::number(exportDuration, 'f', 6),
                           QString::number(videoDuration, 'f', 6), durationOk);
        finalValidationLog(QStringLiteral("checkAverageFrameRate"), QStringLiteral("Average frame rate"),
                           rateString(exportFrameRate), rateString(result.mediaInfo.averageFrameRate), averageRateOk);
        finalValidationLog(QStringLiteral("checkNominalFrameRate"), QStringLiteral("Nominal frame rate"),
                           rateString(exportFrameRate), rateString(result.mediaInfo.frameRate), nominalRateOk);
        finalValidationLog(QStringLiteral("checkPixelFormat"), QStringLiteral("Pixel format"),
                           result.mediaProfile.outputPixelFormat, result.mediaInfo.pixelFormat, pixelFormatOk);
        finalValidationLog(QStringLiteral("checkBitDepth"), QStringLiteral("Bit depth"),
                           result.mediaProfile.outputBitDepth,
                           result.mediaInfo.bitDepth ? QVariant(*result.mediaInfo.bitDepth)
                                                     : QVariant(QStringLiteral("unknown")), bitDepthOk);
        finalValidationLog(QStringLiteral("checkEncoderProfile"), QStringLiteral("Encoder profile"),
                           result.mediaProfile.encoderProfile, result.mediaInfo.videoCodecProfile,
                           encoderProfileOk);
        finalValidationLog(QStringLiteral("checkColorRange"), QStringLiteral("Color range"),
                           result.mediaProfile.colorRange, result.mediaInfo.colorRange, colorRangeOk);
        finalValidationLog(QStringLiteral("checkColorSpace"), QStringLiteral("Color space"),
                           result.mediaProfile.colorSpace, result.mediaInfo.colorSpace, colorSpaceOk);
        finalValidationLog(QStringLiteral("checkColorTransfer"), QStringLiteral("Color transfer"),
                           result.mediaProfile.colorTransfer, result.mediaInfo.colorTransfer, colorTransferOk);
        finalValidationLog(QStringLiteral("checkColorPrimaries"), QStringLiteral("Color primaries"),
                           result.mediaProfile.colorPrimaries, result.mediaInfo.colorPrimaries, colorPrimariesOk);
        finalValidationLog(QStringLiteral("checkPacketCount"), QStringLiteral("Video packet count"),
                           expectedFrames,
                           packetCountAvailable ? QVariant::fromValue(result.mediaInfo.videoPacketCount)
                                                : QVariant(QStringLiteral("unavailable")), packetCountOk);
        finalValidationLog(QStringLiteral("checkVideoStart"), QStringLiteral("Video start"),
                           QStringLiteral("0"),
                           QString::number(result.mediaInfo.videoStartTime, 'f', 9), videoStartOk);
        finalValidationLog(QStringLiteral("checkStageBProgress"), QStringLiteral("Stage B progress"),
                           packetCountAvailable ? QVariant(finalFrameCount) : QVariant(QStringLiteral("unavailable")),
                           lastFfmpegProgress.encodedFramesAvailable
                               ? QVariant(static_cast<qint64>(lastFfmpegProgress.encodedFrames))
                               : QVariant(QStringLiteral("unavailable")),
                           outputValidation.stageBProgressMatches);
        finalValidationLog(QStringLiteral("checkAudio"), QStringLiteral("Audio"),
                           audioExpected ? QStringLiteral("yes") : QStringLiteral("not required"),
                           result.mediaInfo.audioCodecs.isEmpty()
                               ? QStringLiteral("none") : result.mediaInfo.audioCodecs.join(','), audioOk);
        if (audioExpected) {
            finalValidationLog(QStringLiteral("checkAudioStart"), QStringLiteral("A/V start delta"),
                               QStringLiteral("%1 ± %2 s").arg(expectedAudioStart, 0, 'f', 6).arg(audioTimingTolerance, 0, 'f', 6),
                               QString::number(qAbs(result.mediaInfo.audioStartTime
                                                    - result.mediaInfo.videoStartTime), 'f', 9),
                               audioStartOk);
            finalValidationLog(QStringLiteral("checkAudioDuration"), QStringLiteral("Audio duration"),
                               QString::number(expectedAudioDuration, 'f', 6),
                               QString::number(result.mediaInfo.audioDuration, 'f', 6), audioDurationOk);
        }
        if (!outputValidation.accepted()) {
            result.error = QStringLiteral("Export failed final media, frame-count, or contiguous-CFR validation.");
            result.diagnostics = QStringLiteral(
                "Prepared %1 telemetry frames; final ffprobe packet count is %2; "
                "Stage B progress is %3; frame-origin=%4; contiguous-CFR=%5; stage counts match=%6.")
                                     .arg(expectedFrames)
                                     .arg(result.mediaInfo.videoPacketCount)
                                     .arg(lastFfmpegProgress.encodedFrames)
                                     .arg(outputValidation.startsAtOrigin ? QStringLiteral("yes") : QStringLiteral("no"))
                                     .arg(outputValidation.contiguousCfrTiming ? QStringLiteral("yes") : QStringLiteral("no"))
                                     .arg(outputValidation.stageCountsMatch ? QStringLiteral("yes") : QStringLiteral("no"));
            return result;
        }
        if (outputValidation.classification == FinalOutputClassification::SuccessWithWarning) {
            result.validationWarning = QStringLiteral(
                "Export completed with warning\n\nThe final video ended %1 frames earlier than scheduled.\n\nExpected: %2 frames\nEncoded: %3 frames\nDifference: %1 frames\n\nThe exported file was preserved.")
                .arg(outputValidation.frameDeficit).arg(expectedFrameCount).arg(finalFrameCount);
            observe(settings, QStringLiteral("log"), QStringLiteral("validatingOutput"),
                    QStringLiteral("terminalFrameDeficitAccepted"), result.validationWarning,
                    QStringLiteral("validation"),
                    {{"expectedFrames", expectedFrameCount}, {"finalFrameCount", finalFrameCount},
                     {"frameDeficit", outputValidation.frameDeficit}, {"resultClassification", QStringLiteral("SuccessWithWarning")}});
        }
        observe(settings, QStringLiteral("log"), QStringLiteral("validatingOutput"),
                QStringLiteral("validationComplete"), QStringLiteral("Final validation passed"),
                QStringLiteral("validation"));
        result.success = true;
    } catch (const OperationCancelled &) {
        result.cancelled = true;
    } catch (const std::exception &error) {
        result.error = QString::fromUtf8(error.what());
    }
    return result;
}

} // namespace FlappedEar
