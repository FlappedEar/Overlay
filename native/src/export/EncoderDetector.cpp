#include "export/EncoderDetector.h"

#include "export/FfmpegTools.h"
#include "export/ExportProcessSupervisor.h"
#include "export/BoundedProcessOutput.h"

#include <QElapsedTimer>
#include <QProcess>
#include <QRegularExpression>
#include <QMutex>
#include <QMutexLocker>
#include <stdexcept>

namespace FlappedEar {
namespace {

struct KnownEncoder {
    const char *id;
    const char *name;
    bool hardware;
};

constexpr KnownEncoder kKnownHevcEncoders[] = {
    {"hevc_videotoolbox", "Apple VideoToolbox HEVC", true},
    {"hevc_nvenc", "NVIDIA NVENC HEVC", true},
    {"hevc_qsv", "Intel Quick Sync HEVC", true},
    {"hevc_amf", "AMD AMF HEVC", true},
    {"libx265", "x265 HEVC", false},
};

bool waitForFinished(QProcess &process, ExportProcessSupervisor &supervisor,
                     const std::function<bool()> &cancelled, const int timeoutMilliseconds,
                     BoundedProcessOutput *stdoutOutput = nullptr,
                     BoundedProcessOutput *stderrOutput = nullptr)
{
    QElapsedTimer elapsed;
    elapsed.start();
    while (process.state() != QProcess::NotRunning && elapsed.elapsed() < timeoutMilliseconds) {
        if (stdoutOutput) stdoutOutput->append(process.readAllStandardOutput());
        if (stderrOutput) stderrOutput->append(process.readAllStandardError());
        if (cancelled && cancelled()) {
            static_cast<void>(supervisor.stopAndWait());
            throw OperationCancelled("Encoder discovery cancelled.");
        }
        process.waitForFinished(100);
    }
    if (stdoutOutput) stdoutOutput->append(process.readAllStandardOutput());
    if (stderrOutput) stderrOutput->append(process.readAllStandardError());
    return process.state() == QProcess::NotRunning && (!stdoutOutput || !stdoutOutput->exceeded());
}

// KAN-174: NVENC rejects frames below its minimum size (64x64 and 128x128 fail,
// 160x160 works), so a smaller probe made a working NVIDIA encoder look absent and
// exports silently fell back to another encoder.
constexpr int encoderAvailabilityProbeEdge = 256;
static_assert(encoderAvailabilityProbeEdge >= 160, "The probe frame must satisfy NVENC's minimum size.");

bool canEncodeHevc(const QString &executable, const QString &encoder,
                   const std::function<bool()> &cancelled)
{
    // `ffmpeg -encoders` reports compiled-in encoders. Hardware entries can
    // still be unusable because a driver, device, or operating-system service
    // is unavailable, so verify the selected binary with a small in-memory job.
    QProcess process;
    BoundedProcessOutput stdoutOutput(BoundedProcessOutput::Mode::ByteCountOnly, 0);
    BoundedProcessOutput stderrOutput(BoundedProcessOutput::Mode::DiagnosticTail,
                                      ProcessOutputLimits::ffmpegDiagnosticTailBytes);
    ExportProcessSupervisor supervisor(process, false);
    supervisor.start(
        executable,
        {"-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i",
         QStringLiteral("color=c=black:s=%1x%1:r=30").arg(encoderAvailabilityProbeEdge),
         "-frames:v", "1", "-c:v", encoder, "-f", "null", "-"});
    return supervisor.waitForStarted(5'000) && waitForFinished(
        process, supervisor, cancelled, 15'000, &stdoutOutput, &stderrOutput)
        && process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
}

EncoderProfileSupport probeProfile(
    const QString &executable, const EncoderProfileRequest &request,
    const std::function<bool()> &cancelled)
{
    QProcess process;
    BoundedProcessOutput stdoutOutput(BoundedProcessOutput::Mode::ByteCountOnly, 0);
    BoundedProcessOutput stderrOutput(BoundedProcessOutput::Mode::DiagnosticTail,
                                      ProcessOutputLimits::ffmpegDiagnosticTailBytes);
    process.setProcessChannelMode(QProcess::SeparateChannels);
    ExportProcessSupervisor supervisor(process, false);
    const QString rate = QStringLiteral("%1/%2")
                             .arg(request.frameRate.numerator)
                             .arg(request.frameRate.denominator);
    QStringList arguments{
        "-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i",
        QStringLiteral("color=c=black:s=%1x%2:r=%3")
            .arg(request.size.width()).arg(request.size.height()).arg(rate),
        "-vf", QStringLiteral("format=pix_fmts=%1").arg(request.pixelFormat),
        "-frames:v", "1", "-an", "-c:v", request.encoder,
        "-profile:v", request.profile, "-pix_fmt", request.pixelFormat,
        "-f", "null", "-",
    };
    supervisor.start(executable, arguments);
    if (!supervisor.waitForStarted(5'000)
        || !waitForFinished(process, supervisor, cancelled, 30'000, &stdoutOutput, &stderrOutput)
        || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        const QString diagnostics = stderrOutput.text();
        return {false, false,
            QStringLiteral("Encoder %1 cannot encode %2×%3 at %4 using %5/%6 (%7-bit).%8")
                .arg(request.encoder).arg(request.size.width()).arg(request.size.height())
                .arg(rate, request.pixelFormat, request.profile)
                .arg(request.bitDepth)
                .arg(diagnostics.isEmpty() ? QString() : QStringLiteral(" FFmpeg: %1").arg(diagnostics))};
    }
    return {true, false, {}};
}

} // namespace

QString EncoderProfileRequest::cacheKey() const
{
    return QStringLiteral("%1|%2|%3x%4|%5/%6|%7|%8|%9")
        .arg(ffmpegExecutable).arg(encoder).arg(size.width()).arg(size.height())
        .arg(frameRate.numerator).arg(frameRate.denominator)
        .arg(pixelFormat).arg(bitDepth).arg(profile);
}

EncoderProfileSupport EncoderCapabilityCache::verify(
    const EncoderProfileRequest &request, const Probe &probe)
{
    const QString key = request.cacheKey();
    const auto existing = m_results.constFind(key);
    if (existing != m_results.cend()) {
        EncoderProfileSupport cached = existing.value();
        cached.cacheHit = true;
        return cached;
    }
    EncoderProfileSupport result = probe(request);
    result.cacheHit = false;
    m_results.insert(key, result);
    return result;
}

qsizetype EncoderCapabilityCache::size() const { return m_results.size(); }

QList<EncoderCapability> EncoderDetector::discover(
    const QString &requestedFfmpegPath, const std::function<bool()> &cancelled)
{
    const QString executable = requestedFfmpegPath.isEmpty()
        ? FfmpegTools::ffmpegPath()
        : requestedFfmpegPath;
    if (executable.isEmpty()) {
        throw std::runtime_error(FfmpegTools::missingToolsMessage().toStdString());
    }
    QProcess process;
    BoundedProcessOutput stdoutOutput(BoundedProcessOutput::Mode::CompletePayload,
                                      ProcessOutputLimits::encoderListingBytes);
    BoundedProcessOutput stderrOutput(BoundedProcessOutput::Mode::DiagnosticTail,
                                      ProcessOutputLimits::ffmpegDiagnosticTailBytes);
    ExportProcessSupervisor supervisor(process, false);
    supervisor.start(executable, {"-hide_banner", "-encoders"});
    if (!supervisor.waitForStarted(5'000) || !waitForFinished(
            process, supervisor, cancelled, 15'000, &stdoutOutput, &stderrOutput)
        || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        if (stdoutOutput.exceeded()) {
            throw std::runtime_error(QStringLiteral("FFmpeg encoder listing exceeded %1 bytes (observed %2 bytes).")
                                         .arg(ProcessOutputLimits::encoderListingBytes)
                                         .arg(stdoutOutput.observedBytes()).toStdString());
        }
        throw std::runtime_error(QStringLiteral("Could not query FFmpeg encoders: %1").arg(stderrOutput.text()).toStdString());
    }
    const QList<EncoderCapability> advertised = parseEncoders(
        QString::fromUtf8(stdoutOutput.bytes()));
    QList<EncoderCapability> usable;
    for (const EncoderCapability &capability : advertised) {
        if (canEncodeHevc(executable, capability.id, cancelled)) {
            usable.append(capability);
        }
    }
    return usable;
}

QList<EncoderCapability> EncoderDetector::parseEncoders(const QString &output)
{
    QList<EncoderCapability> capabilities;
    for (const KnownEncoder &known : kKnownHevcEncoders) {
        const QRegularExpression expression(
            QStringLiteral("^\\s*V[^\\s]*\\s+%1(?:\\s|$)")
                .arg(QString::fromLatin1(known.id)),
            QRegularExpression::MultilineOption);
        if (expression.match(output).hasMatch()) {
            capabilities.append(
                {QString::fromLatin1(known.id), QString::fromLatin1(known.name), known.hardware});
        }
    }
    return capabilities;
}

QString EncoderDetector::preferredHevcEncoder(const QList<EncoderCapability> &encoders)
{
    // This order favours working platform hardware encoders, then a working
    // x265 build. discover() removes encoders merely advertised by FFmpeg.
    for (const KnownEncoder &known : kKnownHevcEncoders) {
        for (const EncoderCapability &capability : encoders) {
            if (capability.id == QLatin1String(known.id)) {
                return capability.id;
            }
        }
    }
    return {};
}

EncoderProfileSupport EncoderDetector::verifyProfile(
    const EncoderProfileRequest &request, const QString &requestedFfmpegPath,
    const std::function<bool()> &cancelled)
{
    const QString executable = requestedFfmpegPath.isEmpty()
        ? FfmpegTools::ffmpegPath() : requestedFfmpegPath;
    if (executable.isEmpty()) {
        return {false, false, FfmpegTools::missingToolsMessage()};
    }
    static QMutex mutex;
    static EncoderCapabilityCache cache;
    QMutexLocker locker(&mutex);
    EncoderProfileRequest keyedRequest = request;
    keyedRequest.ffmpegExecutable = executable;
    return cache.verify(keyedRequest, [&](const EncoderProfileRequest &profileRequest) {
        return probeProfile(executable, profileRequest, cancelled);
    });
}

} // namespace FlappedEar
