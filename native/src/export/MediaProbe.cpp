#include "export/MediaProbe.h"

#include "export/FfmpegTools.h"
#include "export/ExportDiagnostics.h"
#include "export/BoundedProcessOutput.h"

#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSet>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace FlappedEar {

namespace {

constexpr int metadataProbeTimeoutMilliseconds = 30'000;
constexpr int frameCountProbeTimeoutMilliseconds = 300'000;
constexpr int processShutdownTimeoutMilliseconds = 1'000;

QString stderrDiagnostic(const QByteArray &stderrOutput)
{
    const QString text = QString::fromUtf8(stderrOutput).trimmed();
    return text.isEmpty() ? QStringLiteral("<no stderr output>") : text;
}

QString secondsText(const qint64 milliseconds)
{
    return QString::number(milliseconds / 1'000.0, 'f', milliseconds >= 1'000 ? 1 : 3);
}

void stopAndReap(QProcess &process)
{
    if (process.state() == QProcess::NotRunning) return;
    process.terminate();
    if (!process.waitForFinished(processShutdownTimeoutMilliseconds)) {
        process.kill();
        static_cast<void>(process.waitForFinished(processShutdownTimeoutMilliseconds));
    }
}

double jsonNumber(const QJsonValue &value, const double fallback = 0.0)
{
    if (value.isDouble()) {
        const double result = value.toDouble();
        return std::isfinite(result) ? result : fallback;
    }
    bool ok = false;
    const double result = value.toString().toDouble(&ok);
    return ok && std::isfinite(result) ? result : fallback;
}

qsizetype jsonCount(const QJsonObject &object, const QString &primary, const QString &fallback = {})
{
    bool ok = false;
    qint64 count = object.value(primary).toString().toLongLong(&ok);
    if (!ok && !fallback.isEmpty()) {
        count = object.value(fallback).toString().toLongLong(&ok);
    }
    return ok && count >= 0 ? static_cast<qsizetype>(count) : 0;
}

std::optional<qint64> jsonInteger(const QJsonValue &value)
{
    bool ok = false;
    qint64 parsed = 0;
    if (value.isDouble()) {
        const double number = value.toDouble();
        ok = std::isfinite(number) && number > 0.0
            && number <= static_cast<double>(std::numeric_limits<qint64>::max());
        if (ok) parsed = static_cast<qint64>(number);
    } else {
        parsed = value.toString().toLongLong(&ok);
    }
    return ok && parsed > 0 ? std::optional<qint64>(parsed) : std::nullopt;
}

// A signed integer, for timestamps that may be negative or zero (KAN-148).
std::optional<qint64> jsonSignedInteger(const QJsonValue &value)
{
    if (value.isDouble()) {
        const double number = value.toDouble();
        if (!std::isfinite(number) || std::trunc(number) != number
            || std::abs(number) > static_cast<double>(std::numeric_limits<qint64>::max()))
            return std::nullopt;
        return static_cast<qint64>(number);
    }
    bool ok = false;
    const qint64 parsed = value.toString().toLongLong(&ok);
    return ok ? std::optional<qint64>(parsed) : std::nullopt;
}

MediaInfo runProbe(
    const QString &path,
    const QString &requestedFfprobePath,
    const QStringList &arguments,
    const int timeoutMilliseconds,
    const QString &mode,
    const MediaProbeProgressCallback &progressCallback,
    const MediaProbeCancellationCallback &cancellationCallback)
{
    const QString executable = requestedFfprobePath.isEmpty()
        ? FfmpegTools::ffprobePath()
        : requestedFfprobePath;
    if (executable.isEmpty()) {
        throw std::runtime_error(FfmpegTools::missingToolsMessage(false).toStdString());
    }

    QProcess process;
    process.setProcessChannelMode(QProcess::SeparateChannels);
    QElapsedTimer elapsed;
    elapsed.start();
    process.start(executable, arguments);
    if (!process.waitForStarted()) {
        throw std::runtime_error(
            QStringLiteral("Could not start ffprobe while probing: %1 (%2)")
                .arg(path, process.errorString()).toStdString());
    }
    if (progressCallback) {
        progressCallback({MediaProbeEvent::Phase::Started, 0, executable, arguments, path, mode, 0});
    }
    DiagnosticHeartbeat heartbeat(500);
    BoundedProcessOutput stdoutOutput(BoundedProcessOutput::Mode::CompletePayload, ProcessOutputLimits::ffprobeJsonBytes);
    BoundedProcessOutput stderrOutput(BoundedProcessOutput::Mode::DiagnosticTail, ProcessOutputLimits::ffmpegDiagnosticTailBytes);
    const auto drain = [&] {
        stdoutOutput.append(process.readAllStandardOutput());
        stderrOutput.append(process.readAllStandardError());
    };
    bool finished = false;
    while (!finished && elapsed.elapsed() < timeoutMilliseconds) {
        if (cancellationCallback && cancellationCallback()) {
            stopAndReap(process);
            throw OperationCancelled(
                QStringLiteral("ffprobe cancelled while probing: %1").arg(path).toStdString());
        }
        drain();
        if (stdoutOutput.exceeded()) {
            stopAndReap(process); drain();
            throw std::runtime_error(QStringLiteral("ffprobe %1 output exceeded %2 bytes while probing %3 (observed %4 bytes).").arg(mode).arg(ProcessOutputLimits::ffprobeJsonBytes).arg(path).arg(stdoutOutput.observedBytes()).toStdString());
        }
        const qint64 remaining = timeoutMilliseconds - elapsed.elapsed();
        finished = process.waitForFinished(static_cast<int>(qMin<qint64>(250, remaining)));
        if (!finished && progressCallback && heartbeat.shouldEmit(elapsed.elapsed())) {
            progressCallback({MediaProbeEvent::Phase::Heartbeat, elapsed.elapsed(), executable,
                              arguments, path, mode, 0});
        }
    }
    if (!finished) {
        stopAndReap(process);
        drain();
        throw std::runtime_error(
            QStringLiteral("ffprobe timed out after %1 seconds while probing: %2 "
                           "(elapsed %3 seconds, stdout %4 bytes). stderr: %5")
                .arg(secondsText(timeoutMilliseconds), path, secondsText(elapsed.elapsed()),
                     QString::number(stdoutOutput.observedBytes()), stderrDiagnostic(stderrOutput.bytes())).toStdString());
    }

    drain();
    if (stdoutOutput.exceeded()) {
        throw std::runtime_error(QStringLiteral("ffprobe %1 output exceeded %2 bytes while probing %3 (observed %4 bytes).").arg(mode).arg(ProcessOutputLimits::ffprobeJsonBytes).arg(path).arg(stdoutOutput.observedBytes()).toStdString());
    }
    if (progressCallback) {
        progressCallback({MediaProbeEvent::Phase::Finished, elapsed.elapsed(), executable,
                          arguments, path, mode, process.exitCode()});
    }
    if (process.exitStatus() != QProcess::NormalExit) {
        throw std::runtime_error(
            QStringLiteral("ffprobe crashed while probing: %1. stderr: %2")
                .arg(path, stderrDiagnostic(stderrOutput.bytes())).toStdString());
    }
    if (process.exitCode() != 0) {
        throw std::runtime_error(
            QStringLiteral("ffprobe exited with code %1 while probing: %2. stderr: %3")
                .arg(process.exitCode()).arg(path, stderrDiagnostic(stderrOutput.bytes())).toStdString());
    }
    return MediaProbe::parseJson(stdoutOutput.bytes(), path);
}

} // namespace

double MediaRational::value() const
{
    return denominator == 0 ? 0.0 : static_cast<double>(numerator) / denominator;
}

bool MediaRational::isValid() const { return numerator > 0 && denominator > 0; }

bool MediaRational::isEquivalentTo(const MediaRational &other) const
{
    if (!isValid() || !other.isValid()) {
        return false;
    }
    const qint64 thisDivisor = std::gcd(numerator, denominator);
    const qint64 otherDivisor = std::gcd(other.numerator, other.denominator);
    return numerator / thisDivisor == other.numerator / otherDivisor
        && denominator / thisDivisor == other.denominator / otherDivisor;
}

MediaInfo MediaProbe::probe(
    const QString &path,
    const QString &requestedFfprobePath,
    const bool countVideoFrames,
    const int timeoutMilliseconds,
    const MediaProbeProgressCallback &progressCallback,
    const MediaProbeCancellationCallback &cancellationCallback,
    const bool countVideoPackets)
{
    QStringList arguments{"-v", "error", "-print_format", "json"};
    if (countVideoFrames) {
        arguments.append("-count_frames");
    }
    if (countVideoPackets) {
        arguments.append("-count_packets");
    }
    arguments.append({"-show_format", "-show_streams", path});
    const int effectiveTimeout = timeoutMilliseconds >= 0
        ? timeoutMilliseconds
        : (countVideoFrames ? frameCountProbeTimeoutMilliseconds : metadataProbeTimeoutMilliseconds);
    return runProbe(path, requestedFfprobePath, arguments, effectiveTimeout,
                    countVideoFrames ? QStringLiteral("frameCount")
                                     : (countVideoPackets ? QStringLiteral("packetCount")
                                                          : QStringLiteral("full")),
                    progressCallback, cancellationCallback);
}

MediaInfo MediaProbe::probeSummary(
    const QString &path,
    const QString &requestedFfprobePath,
    const int timeoutMilliseconds,
    const MediaProbeProgressCallback &progressCallback,
    const MediaProbeCancellationCallback &cancellationCallback)
{
    const QStringList arguments{
        "-v", "error",
        "-show_entries",
        "format=duration,start_time:format_tags=creation_time:stream=index,codec_type,codec_name,profile,width,height,coded_width,coded_height,r_frame_rate,avg_frame_rate,time_base,start_time,start_pts,duration,duration_ts,nb_frames,nb_read_frames,nb_packets,nb_read_packets,sample_rate,pix_fmt,bits_per_raw_sample,bit_rate,sample_aspect_ratio,color_range,color_space,color_transfer,color_primaries:stream_side_data=rotation,side_data_type,max_content,mastering_display_metadata",
        "-of", "json",
        path,
    };
    return runProbe(path, requestedFfprobePath, arguments, timeoutMilliseconds,
                    QStringLiteral("summary"), progressCallback, cancellationCallback);
}

MediaInfo MediaProbe::parseJson(const QByteArray &json, const QString &path)
{
    const QJsonDocument document = QJsonDocument::fromJson(json);
    if (!document.isObject()) {
        throw std::runtime_error("ffprobe returned invalid JSON.");
    }
    MediaInfo info;
    info.path = path;
    const QJsonObject format = document.object().value("format").toObject();
    info.duration = jsonNumber(format.value("duration"));
    info.startTime = jsonNumber(format.value("start_time"));
    const auto creation = QDateTime::fromString(format.value("tags").toObject().value("creation_time").toString(), Qt::ISODateWithMs);
    if (creation.isValid()) info.creationTime = creation.toUTC();
    for (const QJsonValue &value : document.object().value("streams").toArray()) {
        const QJsonObject stream = value.toObject();
        if (stream.value("codec_type").toString() == "video" && info.videoCodec.isEmpty()) {
            info.videoSize = {stream.value("width").toInt(), stream.value("height").toInt()};
            info.codedVideoSize = {stream.value("coded_width").toInt(),
                                   stream.value("coded_height").toInt()};
            if (!info.codedVideoSize.isValid()) info.codedVideoSize = info.videoSize;
            info.frameRate = parseRational(stream.value("r_frame_rate").toString());
            info.averageFrameRate = parseRational(stream.value("avg_frame_rate").toString());
            info.timeBase = parseRational(stream.value("time_base").toString());
            info.videoCodec = stream.value("codec_name").toString();
            info.videoCodecProfile = stream.value("profile").toString();
            info.pixelFormat = stream.value("pix_fmt").toString();
            const auto rawBits = jsonInteger(stream.value("bits_per_raw_sample"));
            info.bitDepth = rawBits && *rawBits <= 64
                ? std::optional<int>(static_cast<int>(*rawBits))
                : bitDepthForPixelFormat(info.pixelFormat);
            info.sourceVideoBitrate = jsonInteger(stream.value("bit_rate"));
            info.sampleAspectRatio = parseRational(stream.value("sample_aspect_ratio").toString());
            info.colorRange = stream.value("color_range").toString();
            info.colorSpace = stream.value("color_space").toString();
            info.colorTransfer = stream.value("color_transfer").toString();
            info.colorPrimaries = stream.value("color_primaries").toString();
            for (const QJsonValue &sideValue : stream.value("side_data_list").toArray()) {
                const QJsonObject sideData = sideValue.toObject();
                if (sideData.contains("rotation")) {
                    bool rotationOk = false;
                    const int rotation = sideData.value("rotation").toVariant().toInt(&rotationOk);
                    if (rotationOk) info.rotationDegrees = rotation;
                }
                const QString sideType = sideData.value("side_data_type").toString();
                if (sideType.contains("Mastering display", Qt::CaseInsensitive)) {
                    info.masteringDisplayMetadata = QString::fromUtf8(
                        QJsonDocument(sideData).toJson(QJsonDocument::Compact));
                } else if (sideType.contains("Content light", Qt::CaseInsensitive)) {
                    info.contentLightMetadata = QString::fromUtf8(
                        QJsonDocument(sideData).toJson(QJsonDocument::Compact));
                }
            }
            info.displayVideoSize = info.videoSize;
            if (info.rotationDegrees
                && (qAbs(static_cast<qint64>(*info.rotationDegrees)) % 180) == 90) {
                info.displayVideoSize.transpose();
            }
            info.sourceColorClass = classifyColor(
                info.colorTransfer, info.colorSpace, info.colorPrimaries);
            info.videoFrameCount = jsonCount(stream, QStringLiteral("nb_read_frames"),
                                              QStringLiteral("nb_frames"));
            info.videoPacketCount = jsonCount(stream, QStringLiteral("nb_read_packets"),
                                               QStringLiteral("nb_packets"));
            const auto startTicks = jsonSignedInteger(stream.value("start_pts"));
            info.videoStartTicks = startTicks.value_or(0);
            info.videoStartKnown = startTicks.has_value();
            info.videoDurationTicks = jsonInteger(stream.value("duration_ts")).value_or(0);
            info.videoStartTime = jsonNumber(stream.value("start_time"), info.startTime);
            info.videoDuration = jsonNumber(stream.value("duration"), info.duration);
            info.startTime = info.videoStartTime;
        } else if (stream.value("codec_type").toString() == "audio") {
            info.audioCodecs.append(stream.value("codec_name").toString());
            if (info.audioCodecs.size() == 1) {
                info.audioStartTime = jsonNumber(stream.value("start_time"), info.startTime);
                info.audioDuration = jsonNumber(stream.value("duration"), info.duration);
                info.audioTimeBase = parseRational(stream.value("time_base").toString());
                info.audioSampleRate = stream.value("sample_rate").toString().toInt();
            }
        }
    }
    if (info.videoSize.isEmpty() || info.videoCodec.isEmpty() || info.duration <= 0.0) {
        throw std::runtime_error("ffprobe did not find a usable video stream.");
    }
    // r_frame_rate is the stream's nominal codec rate; avg_frame_rate is the
    // observed presentation cadence. A meaningful mismatch is a conservative
    // signal that the source may have variable frame timing.
    if (info.frameRate.isValid() && info.averageFrameRate.isValid()) {
        const double nominal = info.frameRate.value();
        const double average = info.averageFrameRate.value();
        info.likelyVariableFrameRate = qAbs(nominal - average) > 0.01 * qMax(1.0, nominal);
    }
    return info;
}

MediaRational MediaProbe::parseRational(const QString &value)
{
    QStringList pieces = value.split('/');
    if (pieces.size() != 2) pieces = value.split(':');
    if (pieces.size() != 2) {
        return {};
    }
    bool numeratorOk = false;
    bool denominatorOk = false;
    const qint64 numerator = pieces[0].toLongLong(&numeratorOk);
    const qint64 denominator = pieces[1].toLongLong(&denominatorOk);
    return numeratorOk && denominatorOk && denominator > 0 ? MediaRational{numerator, denominator}
                                                            : MediaRational{};
}

std::optional<int> MediaProbe::bitDepthForPixelFormat(const QString &pixelFormat)
{
    const QString normalized = pixelFormat.trimmed().toLower();
    static const QSet<QString> eightBitFormats{
        QStringLiteral("yuv420p"), QStringLiteral("yuvj420p"), QStringLiteral("nv12"),
        QStringLiteral("rgba"), QStringLiteral("bgra"), QStringLiteral("rgb24"),
    };
    static const QSet<QString> tenBitFormats{
        QStringLiteral("yuv420p10le"), QStringLiteral("yuv420p10be"),
        QStringLiteral("p010le"), QStringLiteral("p010be"),
    };
    if (eightBitFormats.contains(normalized)) return 8;
    if (tenBitFormats.contains(normalized)) return 10;
    return std::nullopt;
}

SourceColorClass MediaProbe::classifyColor(
    const QString &colorTransfer, const QString &colorSpace, const QString &colorPrimaries)
{
    const QString transfer = colorTransfer.trimmed().toLower();
    if (transfer == QStringLiteral("arib-std-b67")) return SourceColorClass::HdrHlg;
    if (transfer == QStringLiteral("smpte2084")) return SourceColorClass::HdrPq;
    if (transfer == QStringLiteral("log100") || transfer == QStringLiteral("log316")
        || transfer == QStringLiteral("iec61966-2-4") || transfer == QStringLiteral("bt1361e")) {
        return SourceColorClass::LogOrExtended;
    }
    if (transfer == QStringLiteral("bt709") || transfer == QStringLiteral("smpte170m")
        || transfer == QStringLiteral("gamma22") || transfer == QStringLiteral("gamma28")
        || transfer == QStringLiteral("iec61966-2-1") || transfer == QStringLiteral("bt2020-10")
        || transfer == QStringLiteral("bt2020-12")) {
        return SourceColorClass::Sdr;
    }
    if (transfer.isEmpty() && colorSpace.isEmpty() && colorPrimaries.isEmpty()) {
        return SourceColorClass::Unknown;
    }
    return SourceColorClass::Unknown;
}

QString sourceColorClassName(const SourceColorClass classification)
{
    switch (classification) {
    case SourceColorClass::Sdr: return QStringLiteral("SDR");
    case SourceColorClass::HdrHlg: return QStringLiteral("HLG HDR");
    case SourceColorClass::HdrPq: return QStringLiteral("PQ HDR");
    case SourceColorClass::LogOrExtended: return QStringLiteral("Log/extended");
    case SourceColorClass::Unknown: return QStringLiteral("Unknown color");
    }
    return QStringLiteral("Unknown color");
}

} // namespace FlappedEar
