#include "app/ExportController.h"

#include "app/AppLog.h"
#include "export/ExportArtifactManifest.h"
#include "export/ExportCancellation.h"
#include "export/ExportEngine.h"
#include "export/ExportFormat.h"
#include "export/ExportMediaProfile.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QPointer>
#include <QStandardPaths>

#include <algorithm>

namespace FlappedEar {

ExportController::ExportController(QObject *parent) : QObject(parent)
{
    m_exportDiagnosticNotifier.setSingleShot(true);
    m_exportDiagnosticNotifier.setInterval(200);
    connect(&m_exportDiagnosticNotifier, &QTimer::timeout, this, &ExportController::diagnosticLogChanged);
}

ExportController::~ExportController()
{
    bool exportStopped = true;
    if (m_exportProcess) {
        // Blocking waits can emit finished/readyRead during destruction.
        disconnect(m_exportProcess.get(), nullptr, this, nullptr);
        QFile cancellationFile(m_exportCancelPath);
        if (cancellationFile.open(QIODevice::WriteOnly)) {
            cancellationFile.close();
        }
        exportStopped = m_exportSupervisor && m_exportSupervisor->stopAndWait();
    }
    if (!exportStopped) {
        if (m_exportOutputTransaction) m_exportOutputTransaction->deferCleanup();
        if (m_exportConfig) m_exportConfig->setAutoRemove(false);
        AppLog::warn(QStringLiteral("Export cleanup deferred: process tree shutdown was not confirmed"));
    }
    m_exportOutputTransaction.reset();
    // On abnormal destruction the manifest intentionally remains for startup
    // recovery. A normal finished callback performs the authorized cleanup.
    if (exportStopped && !m_exportCancelPath.isEmpty()) QFile::remove(m_exportCancelPath);
}

void ExportController::fail(const QString &error)
{
    m_exportError = error;
    m_exportState = QStringLiteral("failed");
    AppLog::error(QStringLiteral("Export failed: %1").arg(m_exportError));
    emit changed();
}

void ExportController::reset()
{
    m_exportMetrics.clear();
    m_exportDiagnosticLog.clear();
    m_exportDiagnosticNotifier.stop();
    emit diagnosticLogChanged();
    m_exportProgressInfo.clear();
    m_exportProgressVisible = false;
}

bool ExportController::start(const Job &job, const Request &request)
{
    AppLog::info(QStringLiteral("Export requested: %1").arg(request.outputPath));
    if (exporting()) {
        AppLog::warn(QStringLiteral("Export request ignored because an export is already running"));
        return false;
    }
    const QString &inputPath = job.inputPath;
    const QString &outputPath = request.outputPath;
    const MediaInfo &source = job.source;
    if (!source.videoSize.isValid()) {
        m_exportError = QStringLiteral("Video metadata is still loading or unavailable.");
        m_exportState = QStringLiteral("failed");
        AppLog::error(QStringLiteral("Export failed: %1").arg(m_exportError));
        emit changed();
        return false;
    }
    if (const QString displayTransformError =
            ExportMediaProfile::unsupportedDisplayTransformError(source);
        !displayTransformError.isEmpty()) {
        m_exportError = displayTransformError;
        m_exportState = QStringLiteral("failed");
        AppLog::error(QStringLiteral("Export failed: %1").arg(m_exportError));
        emit changed();
        return false;
    }
    if (isUnsupportedColorManagedClass(source.sourceColorClass)) {
        m_exportError = QStringLiteral(
            "%1 source detected. Color-managed HDR/Log preservation is not yet supported; "
            "export will not silently convert it to SDR.")
                            .arg(sourceColorClassName(source.sourceColorClass));
        m_exportState = QStringLiteral("failed");
        AppLog::error(QStringLiteral("Export failed: %1").arg(m_exportError));
        emit changed();
        return false;
    }
    if (!source.bitDepth) {
        m_exportError = QStringLiteral(
            "Source bit depth is unknown (pixel format: %1); safe preservation cannot be verified.")
                            .arg(source.pixelFormat.isEmpty()
                                     ? QStringLiteral("unknown") : source.pixelFormat);
        m_exportState = QStringLiteral("failed");
        AppLog::error(QStringLiteral("Export failed: %1").arg(m_exportError));
        emit changed();
        return false;
    }
    const QSize outputSize = request.outputSize;
    const MediaRational outputRate = request.frameRate;
    const qint64 videoBitrate = request.videoBitrate;
    const bool audioEnabled = request.audioEnabled;
    if (!outputSize.isValid() || outputSize.width() % 2 || outputSize.height() % 2
        || outputSize.width() > source.videoSize.width() || outputSize.height() > source.videoSize.height()
        || !outputRate.isValid() || outputRate.value() > ExportEngine::effectiveFrameRate(source).value()
        || !ExportFormat::validCustomBitrate(videoBitrate)) {
        m_exportError = QStringLiteral("Export format is invalid. Choose an even, non-upscaled size, supported frame rate, and 0.5–500 Mbps bitrate.");
        m_exportState = QStringLiteral("failed"); emit changed(); return false;
    }
    const auto fullRange = ExportEngine::fullVideoFrameRange(source, outputRate);
    const auto selectedRange = request.customRange
        ? ExportEngine::frameRangeForSourceTimecode(source, outputRate, request.rangeIn, request.rangeOut)
        : fullRange;
    if (!selectedRange) {
        m_exportError = QStringLiteral("Export range must use valid inclusive SMPTE IN and OUT timecodes within the source frame domain.");
        m_exportState = QStringLiteral("failed");
        AppLog::error(QStringLiteral("Export failed: %1").arg(m_exportError));
        emit changed();
        return false;
    }
    m_exportOutputTransaction = std::make_unique<ExportOutputTransaction>();
    const auto preparation = m_exportOutputTransaction->prepare(
        outputPath, inputPath, job.protectedPaths, request.overwriteAllowed);
    if (preparation.status == ExportOutputTransaction::PreparationStatus::OverwriteConfirmationRequired) {
        m_exportState = QStringLiteral("overwriteConfirmationRequired");
        m_exportError.clear();
        m_exportProgressInfo = {{"outputPath", m_exportOutputTransaction->userTargetPath()},
                                {"outputName", QFileInfo(outputPath).fileName()},
                                {"targetExistedBeforeExport", true}};
        m_exportOutputTransaction.reset();
        emit changed();
        return false;
    }
    if (preparation.status == ExportOutputTransaction::PreparationStatus::Error) {
        m_exportError = preparation.error;
        m_exportState = QStringLiteral("failed");
        AppLog::error(QStringLiteral("Export failed: %1").arg(m_exportError));
        m_exportOutputTransaction.reset();
        emit changed();
        return false;
    }
    const QString exportId = m_exportOutputTransaction->transactionId();
    const QString temporaryOverlayPath = QDir::temp().filePath(
        QStringLiteral("flappedear-overlay-%1.mkv").arg(exportId));
    const ExportArtifactManifestData manifest{exportId, QDateTime::currentMSecsSinceEpoch(),
        temporaryOverlayPath, m_exportOutputTransaction->stagingPath(),
        m_exportOutputTransaction->userTargetPath(), 0, QStringLiteral("preparing")};
    QString manifestError;
    if (!ExportArtifactManifest::create(manifest, &manifestError)) {
        m_exportError = QStringLiteral("Could not create export ownership manifest: %1").arg(manifestError);
        m_exportState = QStringLiteral("failed");
        AppLog::error(QStringLiteral("Export failed: %1").arg(m_exportError));
        m_exportOutputTransaction.reset();
        emit changed();
        return false;
    }
    m_exportManifestPath = ExportArtifactManifest::manifestPathFor(exportId);
    m_exportConfig = std::make_unique<QTemporaryFile>(
        QDir::temp().filePath(QStringLiteral("flappedear-export-XXXXXX.json")));
    if (!m_exportConfig->open()) {
        m_exportError = QStringLiteral("Could not create temporary export configuration.");
        m_exportState = QStringLiteral("failed");
        AppLog::error(QStringLiteral("Export failed: %1").arg(m_exportError));
        static_cast<void>(ExportArtifactManifest::cleanupOwned(m_exportManifestPath));
        m_exportManifestPath.clear();
        m_exportOutputTransaction.reset();
        emit changed();
        return false;
    }
    m_exportCancelPath = m_exportConfig->fileName() + QStringLiteral(".cancel");
    m_exportSupervisionReadyPath = m_exportConfig->fileName() + QStringLiteral(".supervision-ready");
    QFile::remove(m_exportCancelPath);
    QFile::remove(m_exportSupervisionReadyPath);
    QJsonArray additionalVideos;
    for (const auto &video : job.additionalVideos)
        additionalVideos.append(QJsonObject{{"id", video.id}, {"path", video.path}, {"label", video.label},
            {"sync", QJsonObject{{"offset", video.sync.offset}, {"timeScale", video.sync.timeScale}}}});
    const QJsonObject config = {
        {"inputPath", inputPath},
        {"chapterPaths", QJsonArray::fromStringList(job.chapterPaths)},
        {"chapterDurationTicks", job.chapterDurationTicks},
        {"outputPath", m_exportOutputTransaction->stagingPath()},
        {"vboPath", job.telemetryPath},
        {"lapBinding", job.lapBinding},
        {"lapExclusions", job.lapExclusions},
        {"widgets", job.widgets},
        {"sync", QJsonObject{{"offset", job.sync.offset}, {"timeScale", job.sync.timeScale}}},
        {"additionalVideos", additionalVideos},
        {"videoLayout", AdditionalVideosCodec::writeVideoLayout(job.videoLayout, {})},
        {"outputWidth", outputSize.width()}, {"outputHeight", outputSize.height()},
        {"frameRateNumerator", outputRate.numerator}, {"frameRateDenominator", outputRate.denominator},
        {"videoBitrate", videoBitrate},
        {"audioEnabled", audioEnabled},
        {"firstFrame", selectedRange->firstFrame},
        {"lastFrame", selectedRange->lastFrame},
        {"cancelPath", m_exportCancelPath},
        {"supervisionReadyPath", m_exportSupervisionReadyPath},
        {"temporaryOverlayPath", temporaryOverlayPath},
        {"manifestPath", m_exportManifestPath},
    };
    if (m_exportConfig->write(QJsonDocument(config).toJson(QJsonDocument::Compact)) < 0) {
        m_exportError = QStringLiteral("Could not write temporary export configuration.");
        m_exportState = QStringLiteral("failed");
        AppLog::error(QStringLiteral("Export failed: %1").arg(m_exportError));
        static_cast<void>(ExportArtifactManifest::cleanupOwned(m_exportManifestPath));
        m_exportManifestPath.clear();
        m_exportOutputTransaction.reset();
        m_exportConfig.reset();
        emit changed();
        return false;
    }
    m_exportConfig->flush();
    // The worker is a separate process; closing before it starts avoids a
    // Windows sharing violation while the controller retains ownership for
    // cleanup after completion.
    m_exportConfig->close();
    m_exportStdout.clear();
    m_exportStderr = BoundedProcessOutput(BoundedProcessOutput::Mode::DiagnosticTail,
                                          ProcessOutputLimits::ffmpegDiagnosticTailBytes);
    m_exportProgress = 0;
    m_exportError.clear();
    m_exportMetrics.clear();
    m_exportDiagnosticLog.clear();
    m_exportDiagnosticNotifier.stop();
    emit diagnosticLogChanged();
    const QDateTime exportStarted = QDateTime::currentDateTime();
    const QString exportLogDirectory = QDir(
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)).filePath(
        QStringLiteral("exports"));
    const QString sourceRate = QStringLiteral("%1/%2")
                                   .arg(source.frameRate.numerator)
                                   .arg(source.frameRate.denominator);
    const QString outputRateText = QStringLiteral("%1/%2")
                                      .arg(outputRate.numerator)
                                      .arg(outputRate.denominator);
    const QString exportHeader = QStringLiteral(
        "FlappedEar Overlays Export Log\n\n"
        "Started: %1\n"
        "Export ID: %2\n"
        "Application version: %3\n\n"
        "Source:\n"
        "  Path: %4\n"
        "  Video: %5x%6, %7 fps, codec %8, duration %9 s\n\n"
        "Output:\n"
        "  Target: %10\n"
        "  Requested: %11x%12, %13 fps\n"
        "  Video bitrate: %14 bps\n"
        "  Audio: %15\n\n"
        "Range: %16 -> %17 (inclusive)\n")
        .arg(exportStarted.toString(Qt::ISODate), exportId, QCoreApplication::applicationVersion(), inputPath)
        .arg(source.videoSize.width()).arg(source.videoSize.height())
        .arg(sourceRate, source.videoCodec)
        .arg(source.duration, 0, 'f', 3)
        .arg(m_exportOutputTransaction->userTargetPath())
        .arg(outputSize.width()).arg(outputSize.height()).arg(outputRateText)
        .arg(videoBitrate)
        .arg(audioEnabled ? QStringLiteral("enabled, AAC %1 bps").arg(ExportFormat::audioBitrate)
                           : QStringLiteral("disabled"))
        .arg(ExportEngine::formatSmpteTimecode(selectedRange->firstFrame, outputRate),
             ExportEngine::formatSmpteTimecode(selectedRange->lastFrame, outputRate));
    QString exportLogError;
    m_persistentExportLog = PersistentExportLog::create(
        exportLogDirectory, exportId, exportHeader, &exportLogError, exportStarted);
    if (m_persistentExportLog) {
        PersistentExportLog::retainNewest(
            exportLogDirectory, m_persistentExportLog->path());
        AppLog::info(QStringLiteral("Export diagnostics: %1").arg(m_persistentExportLog->path()));
    } else {
        AppLog::warn(QStringLiteral("Could not create export diagnostic log: %1").arg(exportLogError));
    }
    m_exportProgressInfo = {{"outputPath", m_exportOutputTransaction->userTargetPath()},
                            {"stagingPath", m_exportOutputTransaction->stagingPath()},
                            {"outputName", QFileInfo(outputPath).fileName()},
                            {"targetExistedBeforeExport", m_exportOutputTransaction->targetExistedBeforeExport()},
                            {"syncOffset", job.sync.offset}, {"timeScale", job.sync.timeScale},
                            {"width", outputSize.width()}, {"height", outputSize.height()},
                            {"frameRate", outputRate.value()}, {"videoBitrate", videoBitrate},
                            {"audioLabel", audioEnabled ? QStringLiteral("AAC audio") : QStringLiteral("No audio")}};
    m_exportProgressVisible = true;
    m_exportState = QStringLiteral("starting");
    appendExportLifecycle(QStringLiteral("Preparing"));
    AppLog::info(QStringLiteral("Export Stage A preparing"));
    m_exportProcess = std::make_unique<QProcess>(this);
    m_exportSupervisor = std::make_unique<ExportProcessSupervisor>(*m_exportProcess);
    m_exportProcess->setProcessChannelMode(QProcess::SeparateChannels);
    connect(m_exportProcess.get(), &QProcess::readyReadStandardOutput, this, &ExportController::handleExportOutput);
    connect(
        m_exportProcess.get(),
        qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
        this,
        [this, process = QPointer<QProcess>(m_exportProcess.get())](int code, QProcess::ExitStatus status) {
            if (process && process == m_exportProcess.get()) finishExport(code, status);
        },
        Qt::QueuedConnection);
    m_exportSupervisor->start(
        QCoreApplication::applicationFilePath(), {"--export-worker", m_exportConfig->fileName()});
    if (!m_exportSupervisor->waitForStarted(5'000)) {
        const QString supervisionError = m_exportSupervisor->supervisionError();
        m_exportError = supervisionError.isEmpty()
            ? QStringLiteral("Could not start export worker: %1").arg(m_exportProcess->errorString())
            : QStringLiteral("Could not establish export process supervision: %1").arg(supervisionError);
        m_exportState = QStringLiteral("failed");
        AppLog::error(QStringLiteral("Export failed: %1").arg(m_exportError));
        finishExport(1, QProcess::CrashExit);
        return false;
    }
    if (!m_exportSupervisor->supervisionActive()) {
        m_exportError = QStringLiteral("Export process supervision was not established.");
        static_cast<void>(m_exportSupervisor->stopAndWait());
        m_exportState = QStringLiteral("failed");
        AppLog::error(QStringLiteral("Export failed: %1").arg(m_exportError));
        finishPersistentExportLog(QStringLiteral("FAILED"), m_exportError);
        emit changed();
        return false;
    }
    ExportArtifactManifestData activeManifest;
    if (!ExportArtifactManifest::read(m_exportManifestPath, &activeManifest, &manifestError)) {
        m_exportError = QStringLiteral("Could not read active export ownership manifest: %1").arg(manifestError);
        static_cast<void>(m_exportSupervisor->stopAndWait());
        m_exportState = QStringLiteral("failed");
        AppLog::error(QStringLiteral("Export failed: %1").arg(m_exportError));
        finishPersistentExportLog(QStringLiteral("FAILED"), m_exportError);
        emit changed();
        return false;
    }
    activeManifest.workerPid = m_exportProcess->processId();
    if (!ExportArtifactManifest::update(m_exportManifestPath, activeManifest, &manifestError)) {
        m_exportError = QStringLiteral("Could not update active export ownership manifest: %1").arg(manifestError);
        static_cast<void>(m_exportSupervisor->stopAndWait());
        m_exportState = QStringLiteral("failed");
        AppLog::error(QStringLiteral("Export failed: %1").arg(m_exportError));
        finishPersistentExportLog(QStringLiteral("FAILED"), m_exportError);
        emit changed();
        return false;
    }
    QFile supervisionReady(m_exportSupervisionReadyPath);
    if (!supervisionReady.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
        m_exportError = QStringLiteral("Could not release supervised export worker: %1")
                            .arg(supervisionReady.errorString());
        static_cast<void>(m_exportSupervisor->stopAndWait());
        m_exportState = QStringLiteral("failed");
        AppLog::error(QStringLiteral("Export failed: %1").arg(m_exportError));
        finishPersistentExportLog(QStringLiteral("FAILED"), m_exportError);
        emit changed();
        return false;
    }
    supervisionReady.close();
    emit changed();
    return true;
}

void ExportController::cancel()
{
    if (!exporting()) {
        return;
    }
    m_exportState = QStringLiteral("cancelling");
    m_exportProgressInfo.insert("stage", QStringLiteral("cancelling"));
    const ExportCancellationResult cancellation =
        ExportCancellation::request(m_exportCancelPath, m_exportSupervisor.get());
    if (cancellation.markerCreated) {
        AppLog::warn(QStringLiteral("Export cancellation requested"));
        appendExportLifecycle(QStringLiteral("Cancellation requested"));
        emit changed();
        return;
    }

    const QString reason = cancellation.error.isEmpty()
        ? QStringLiteral("unknown cancellation marker error") : cancellation.error;
    AppLog::error(QStringLiteral("Export cancellation marker creation failed: %1").arg(reason));
    appendExportLifecycle(QStringLiteral("Cancellation marker failed; supervised stop requested"));
    if (cancellation.workerStopped) {
        m_exportError = QStringLiteral(
            "Could not create the export cancellation marker; the export worker was stopped: %1.")
                            .arg(reason);
        // stopAndWait completed before publishing this terminal state. The
        // finished callback retains the same error and performs cleanup.
        m_exportState = QStringLiteral("failed");
        AppLog::error(QStringLiteral("Export cancelled by supervised fallback: %1").arg(m_exportError));
    } else {
        m_exportError = QStringLiteral(
            "Could not create the export cancellation marker and the supervised worker is still stopping: %1.")
                            .arg(reason);
        // Do not claim a terminal state while a process may still own export
        // artifacts. finishExport will publish the final result on exit.
        AppLog::error(QStringLiteral("Export cancellation fallback is still stopping: %1").arg(m_exportError));
    }
    emit changed();
}

void ExportController::cancelAndQuit()
{
    if (!exporting()) {
        emit quitRequested();
        return;
    }
    m_quitAfterExport = true;
    cancel();
    // Escalate through the dedicated process-tree owner: the worker and every
    // inherited FFmpeg/ffprobe descendant are stopped as one lifetime unit.
    QTimer::singleShot(7'000, this, [this] {
        if (!exporting()) {
            return;
        }
        if (m_exportSupervisor) static_cast<void>(m_exportSupervisor->stopAndWait(3'000, 3'000));
    });
}

void ExportController::dismissProgress()
{
    if (exporting() || m_exportState == "cancelling") return;
    m_exportProgressVisible = false;
    if (m_exportState == "complete" || m_exportState == "validationWarning"
        || m_exportState == "cancelled") {
        m_exportState = QStringLiteral("idle");
    }
    emit changed();
}

void ExportController::copyDiagnostics()
{
    if (QGuiApplication::clipboard()) {
        QGuiApplication::clipboard()->setText(m_exportDiagnosticLog.text());
    }
}

void ExportController::appendExportDiagnostic(const QString &entry)
{
    m_exportDiagnosticLog.append(entry);
    if (!m_exportDiagnosticNotifier.isActive()) m_exportDiagnosticNotifier.start();
    if (m_persistentExportLog && !m_persistentExportLog->append(entry)) {
        AppLog::warn(QStringLiteral("Could not append export diagnostic log: %1")
                         .arg(m_persistentExportLog->path()));
        m_persistentExportLog.reset();
    }
}

void ExportController::appendExportLifecycle(const QString &event)
{
    appendExportDiagnostic(QStringLiteral("[lifecycle] %1").arg(event));
}

void ExportController::finishPersistentExportLog(const QString &result, const QString &error)
{
    if (!m_persistentExportLog) return;
    QString footer = QStringLiteral("\nFinished: %1\nResult: %2\n")
                         .arg(QDateTime::currentDateTime().toString(Qt::ISODate), result);
    if (!error.isEmpty()) footer += QStringLiteral("Error: %1\n").arg(error);
    const auto value = [this](const QString &key) { return m_exportProgressInfo.value(key).toString(); };
    if (result == QStringLiteral("SUCCESS")) {
        footer += QStringLiteral("Output: %1x%2\nAverage FPS: %3\nEncoded frames: %4\n"
                                 "Output bytes: %5\nEncoder: %6\nValidation: %7\n")
                      .arg(value(QStringLiteral("outputWidth")), value(QStringLiteral("outputHeight")),
                           value(QStringLiteral("outputAverageFrameRate")), value(QStringLiteral("encodedFrames")),
                           value(QStringLiteral("outputBytes")), value(QStringLiteral("encoderName")),
                           m_exportState == QStringLiteral("validationWarning")
                               ? QStringLiteral("warning") : QStringLiteral("passed"));
    }
    appendExportDiagnostic(footer.trimmed());
    m_persistentExportLog.reset();
}

namespace {

QString diagnosticTimestamp(const qint64 elapsedMilliseconds)
{
    const qint64 hours = elapsedMilliseconds / 3'600'000;
    const qint64 minutes = (elapsedMilliseconds / 60'000) % 60;
    const qint64 seconds = (elapsedMilliseconds / 1'000) % 60;
    const qint64 milliseconds = elapsedMilliseconds % 1'000;
    return QStringLiteral("%1:%2:%3.%4")
        .arg(hours, 2, 10, QLatin1Char('0'))
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(seconds, 2, 10, QLatin1Char('0'))
        .arg(milliseconds, 3, 10, QLatin1Char('0'));
}

QString diagnosticValue(const QVariant &value)
{
    if (value.metaType().id() == QMetaType::QStringList) {
        return value.toStringList().join(QLatin1Char(' '));
    }
    if (value.metaType().id() == QMetaType::QVariantList) {
        QStringList items;
        for (const QVariant &item : value.toList()) items.append(item.toString());
        return items.join(QLatin1Char(' '));
    }
    return value.toString();
}

QString formatDiagnosticEvent(const QJsonObject &event)
{
    QString result = QStringLiteral("[%1] %2")
                         .arg(diagnosticTimestamp(event.value("timestampMilliseconds").toInteger()),
                              event.value("message").toString());
    const QVariantMap details = event.value("details").toObject().toVariantMap();
    QStringList keys = details.keys();
    std::sort(keys.begin(), keys.end());
    for (const QString &key : std::as_const(keys)) {
        const QString value = diagnosticValue(details.value(key));
        if (!value.isEmpty()) result += QStringLiteral("\n    %1: %2").arg(key, value);
    }
    const QString error = event.value("error").toString();
    if (!error.isEmpty()) result += QStringLiteral("\n    error: %1").arg(error);
    const QString diagnostics = event.value("diagnostics").toString();
    if (!diagnostics.isEmpty()) result += QStringLiteral("\n    diagnostics:\n%1").arg(diagnostics);
    return result;
}

} // namespace

void ExportController::handleExportOutput()
{
    if (!m_exportProcess) {
        return;
    }
    m_exportStdout.append(m_exportProcess->readAllStandardOutput());
    m_exportStderr.append(m_exportProcess->readAllStandardError());
    // The limit is per message (line), not for what one read delivered: several
    // normal messages can arrive together (KAN-148).
    const auto rejectOversizedMessage = [this] {
        m_exportStdout.clear();
        m_exportError = QStringLiteral("Export worker emitted a message longer than %1 bytes.")
                            .arg(ProcessOutputLimits::workerMessageBytes);
        AppLog::error(m_exportError);
        if (m_exportSupervisor) static_cast<void>(m_exportSupervisor->stopAndWait());
    };
    qsizetype newline = -1;
    while ((newline = m_exportStdout.indexOf('\n')) >= 0) {
        if (newline > ProcessOutputLimits::workerMessageBytes) { rejectOversizedMessage(); return; }
        const QByteArray line = m_exportStdout.left(newline);
        m_exportStdout.remove(0, newline + 1);
        const QJsonObject event = QJsonDocument::fromJson(line).object();
        if (event.value("type").toString() == QStringLiteral("log")) {
            appendExportDiagnostic(formatDiagnosticEvent(event));
        }
        const QString state = event.value("state").toString();
        if (!state.isEmpty()) {
            if (state != m_exportState) {
                if (state == QStringLiteral("renderingOverlay")) {
                    AppLog::info(QStringLiteral("Export Stage A started"));
                    appendExportLifecycle(QStringLiteral("Stage A started"));
                } else if (state == QStringLiteral("validatingOverlay")) {
                    AppLog::info(QStringLiteral("Export Stage A ended"));
                    AppLog::info(QStringLiteral("Export overlay validation started"));
                    appendExportLifecycle(QStringLiteral("Stage A completed; temporary overlay validation started"));
                } else if (state == QStringLiteral("encodingVideo")) {
                    AppLog::info(QStringLiteral("Export overlay validation passed"));
                    AppLog::info(QStringLiteral("Export Stage B started"));
                    appendExportLifecycle(QStringLiteral("Temporary overlay validation passed; Stage B started"));
                } else if (state == QStringLiteral("validatingOutput")) {
                    AppLog::info(QStringLiteral("Export Stage B ended"));
                    AppLog::info(QStringLiteral("Export final validation started"));
                    appendExportLifecycle(QStringLiteral("Stage B completed; final validation started"));
                } else if (state == QStringLiteral("complete")) {
                    AppLog::info(QStringLiteral("Export final validation passed"));
                    appendExportLifecycle(QStringLiteral("Final validation passed"));
                } else if (state == QStringLiteral("validationWarning")) {
                    AppLog::warn(QStringLiteral("Export validation completed with a warning"));
                    appendExportLifecycle(QStringLiteral("Validation warning"));
                } else if (state == QStringLiteral("cancelled")) {
                    AppLog::warn(QStringLiteral("Export cancelled"));
                    appendExportLifecycle(QStringLiteral("Cancelled"));
                }
            }
            m_exportState = state;
        }
        for (const QString &key : {QStringLiteral("generatedFrames"), QStringLiteral("renderedFrames"), QStringLiteral("expectedFrames"),
                                   QStringLiteral("sourceRangeStart"), QStringLiteral("sourceRangeEnd"),
                                   QStringLiteral("exportDuration"), QStringLiteral("exportRelativeTime"),
                                   QStringLiteral("sourceVideoTime"),
                                   QStringLiteral("telemetryTime"), QStringLiteral("elapsedMilliseconds"),
                                   QStringLiteral("throughputFps"), QStringLiteral("realtimeFactor"),
                                   QStringLiteral("etaSeconds"), QStringLiteral("outputBytes"),
                                   QStringLiteral("encoderId"), QStringLiteral("encoderName"),
                                   QStringLiteral("width"), QStringLiteral("height"), QStringLiteral("frameRate"),
                                   QStringLiteral("audioEnabled"), QStringLiteral("encodedFrames"),
                                   QStringLiteral("encodedSeconds"), QStringLiteral("encodedProgress"),
                                   QStringLiteral("encoderFps"), QStringLiteral("encoderRealtimeFactor"),
                                   QStringLiteral("rendererFps"), QStringLiteral("queuedBytes"),
                                   QStringLiteral("maximumQueuedBytes"), QStringLiteral("temporaryOverlayBytes"),
                                   QStringLiteral("estimatedTemporaryOverlayBytes"),
                                   QStringLiteral("estimatedFinalOutputBytes"),
                                   QStringLiteral("safetyReserveBytes"),
                                   QStringLiteral("estimateBasis"), QStringLiteral("sampleFrames"),
                                   QStringLiteral("sampleEncodedBytes"), QStringLiteral("sampleBytesPerFrame"),
                                   QStringLiteral("sampleSafetyMargin"), QStringLiteral("sampleError"),
                                   QStringLiteral("temporaryFilesystemRoot"),
                                   QStringLiteral("temporaryFilesystemInspectedPath"),
                                   QStringLiteral("temporaryFilesystemProbePath"),
                                   QStringLiteral("temporaryFilesystemAvailableBytes"),
                                   QStringLiteral("destinationFilesystemRoot"),
                                   QStringLiteral("destinationFilesystemInspectedPath"),
                                   QStringLiteral("destinationFilesystemProbePath"),
                                   QStringLiteral("destinationFilesystemAvailableBytes"),
                                   QStringLiteral("outputBytes"), QStringLiteral("currentOperation"),
                                   QStringLiteral("operation"), QStringLiteral("stageElapsedMilliseconds"),
                                   QStringLiteral("totalElapsedMilliseconds"), QStringLiteral("stageDurations"),
                                   QStringLiteral("outputVideoCodec"), QStringLiteral("outputWidth"),
                                   QStringLiteral("outputVideoProfile"),
                                   QStringLiteral("outputPixelFormat"), QStringLiteral("outputBitDepth"),
                                   QStringLiteral("outputColorRange"), QStringLiteral("outputColorSpace"),
                                   QStringLiteral("outputColorTransfer"), QStringLiteral("outputColorPrimaries"),
                                   QStringLiteral("outputHeight"), QStringLiteral("outputDuration"),
                                   QStringLiteral("outputVideoDuration"), QStringLiteral("outputVideoStart"),
                                   QStringLiteral("outputVideoPacketCount"),
                                   QStringLiteral("outputAverageFrameRate"),
                                   QStringLiteral("outputAudioCodecs"), QStringLiteral("outputAudioStart"),
                                   QStringLiteral("outputAudioDuration"),
                                   QStringLiteral("exportFrameRateNumerator"),
                                   QStringLiteral("exportFrameRateDenominator"),
                                   QStringLiteral("exportFrameRate"), QStringLiteral("diagnostics"),
                                   QStringLiteral("warning"), QStringLiteral("sourceFrameCount"),
                                   QStringLiteral("sourceFrameCountSource"), QStringLiteral("firstFrame"),
                                   QStringLiteral("lastFrame"), QStringLiteral("finalFrameCount"),
                                   QStringLiteral("frameDeficit"), QStringLiteral("resultClassification")}) {
            if (event.contains(key)) m_exportProgressInfo.insert(key, event.value(key).toVariant());
        }
        if (!state.isEmpty()) m_exportProgressInfo.insert("stage", state);
        if (event.contains("visibleProgress")) {
            m_exportProgress = qRound(event.value("visibleProgress").toDouble());
            m_exportProgressInfo.insert("progressPercent", event.value("visibleProgress").toDouble());
        } else if (state == "validatingOverlay") {
            m_exportProgress = qMax(m_exportProgress, 60);
            m_exportProgressInfo.insert("progressPercent", m_exportProgress);
        }
        if (state == "validatingOutput" || state == "validating") {
            m_exportProgress = qMax(m_exportProgress, 99);
            m_exportProgressInfo.insert("progressPercent", m_exportProgress);
        }
        if (event.contains("error")) {
            m_exportError = event.value("error").toString();
            AppLog::error(QStringLiteral("Export failed: %1").arg(m_exportError));
        }
        if (event.contains("renderMilliseconds")) {
            m_exportMetrics = {
                {"elapsedMilliseconds", event.value("elapsedMilliseconds").toInteger()},
                {"renderMilliseconds", event.value("renderMilliseconds").toInteger()},
                {"renderNanoseconds", event.value("renderNanoseconds").toInteger()},
                {"polishNanoseconds", event.value("polishNanoseconds").toInteger()},
                {"syncRenderNanoseconds", event.value("syncRenderNanoseconds").toInteger()},
                {"readbackNanoseconds", event.value("readbackNanoseconds").toInteger()},
                {"cpuCopyNanoseconds", event.value("cpuCopyNanoseconds").toInteger()},
                {"ffmpegWriteNanoseconds", event.value("ffmpegWriteNanoseconds").toInteger()},
                {"renderedFrames", event.value("renderedFrames").toInteger()},
            };
        }
        emit changed();
    }
    // A partial message already beyond the limit can never become valid.
    if (m_exportStdout.size() > ProcessOutputLimits::workerMessageBytes) rejectOversizedMessage();
}

void ExportController::finishExport(const int exitCode, const QProcess::ExitStatus exitStatus)
{
    if (!m_exportProcess) return;
    const bool remainingWriters = m_exportSupervisor && m_exportSupervisor->isRunning();
    // A worker exit is not a process-tree completion boundary. Stop any
    // remaining writers before reading their final output, committing or cleanup.
    if (!m_exportSupervisor || !m_exportSupervisor->stopAndWait()) {
        m_exportState = QStringLiteral("cancelling");
        m_exportError = QStringLiteral("Export processes are still stopping; temporary files were retained.");
        m_exportProgressInfo.insert("stage", m_exportState);
        emit changed();
        QTimer::singleShot(1'000, this,
            [this, process = QPointer<QProcess>(m_exportProcess.get()), exitCode, exitStatus] {
                if (process && process == m_exportProcess.get()) finishExport(exitCode, exitStatus);
            });
        return;
    }
    handleExportOutput();
    const bool cancelled = QFileInfo::exists(m_exportCancelPath);
    if (remainingWriters && !cancelled) {
        m_exportState = QStringLiteral("failed");
        m_exportError = QStringLiteral("Export worker exited with child processes still running; output was not committed.");
    }
    if (m_exportProcess) m_exportStderr.append(m_exportProcess->readAllStandardError());
    const QString workerError = m_exportStderr.text();
    QString persistentResult;
    QString persistentError;
    if (cancelled) {
        m_exportState = QStringLiteral("cancelled");
        m_exportError.clear();
        emit statusMessage("Export cancelled.");
        persistentResult = QStringLiteral("CANCELLED");
    } else if (exitStatus == QProcess::NormalExit && exitCode == 0 && m_exportError.isEmpty()
               && (m_exportState == QStringLiteral("complete")
                   || m_exportState == QStringLiteral("validationWarning"))) {
        QString commitError;
        if (m_exportOutputTransaction && m_exportOutputTransaction->commit(&commitError)) {
            ExportArtifactManifestData manifest;
            if (ExportArtifactManifest::read(m_exportManifestPath, &manifest)) {
                manifest.state = QStringLiteral("completed");
                static_cast<void>(ExportArtifactManifest::update(m_exportManifestPath, manifest));
            }
            m_exportProgress = 100;
            const bool warning = m_exportState == QStringLiteral("validationWarning");
            m_exportState = warning ? QStringLiteral("validationWarning") : QStringLiteral("complete");
            m_exportError.clear();
            AppLog::info(QStringLiteral("Export succeeded: %1")
                             .arg(m_exportOutputTransaction->userTargetPath()));
            emit statusMessage(warning ? QStringLiteral("HEVC export finished with a validation warning.")
                              : QStringLiteral("HEVC export finished and passed validation."));
            persistentResult = warning ? QStringLiteral("SUCCESS_WITH_WARNING") : QStringLiteral("SUCCESS");
        } else {
            m_exportState = QStringLiteral("failed");
            m_exportError = commitError.isEmpty()
                ? QStringLiteral("Validated export could not be committed to its target.") : commitError;
            AppLog::error(QStringLiteral("Export failed: %1").arg(m_exportError));
            emit statusMessage(QStringLiteral("Export failed: %1").arg(m_exportError));
            persistentResult = QStringLiteral("FAILED");
            persistentError = m_exportError;
        }
    } else {
        m_exportState = QStringLiteral("failed");
        if (m_exportError.isEmpty()) {
            m_exportError = workerError.isEmpty() ? QStringLiteral("Export worker failed.") : workerError;
        }
        AppLog::error(QStringLiteral("Export failed: %1").arg(m_exportError));
        emit statusMessage(QStringLiteral("Export failed: %1").arg(m_exportError));
        persistentResult = QStringLiteral("FAILED");
        persistentError = m_exportError;
    }
    m_exportProgressInfo.insert("stage", m_exportState);
    m_exportProgressInfo.insert("progressPercent", m_exportProgress);
    QFile::remove(m_exportCancelPath);
    QFile::remove(m_exportSupervisionReadyPath);
    appendExportLifecycle(QStringLiteral("Cleanup started"));
    QString cleanupError;
    if (!m_exportManifestPath.isEmpty()
        && !ExportArtifactManifest::cleanupOwned(m_exportManifestPath, &cleanupError)) {
        appendExportDiagnostic(QStringLiteral("Owned export cleanup deferred: %1").arg(cleanupError));
    }
    appendExportLifecycle(cleanupError.isEmpty() ? QStringLiteral("Cleanup completed")
                                                 : QStringLiteral("Cleanup deferred"));
    finishPersistentExportLog(persistentResult, persistentError);
    m_exportManifestPath.clear();
    m_exportSupervisor.reset();
    m_exportProcess.reset();
    m_exportConfig.reset();
    m_exportOutputTransaction.reset();
    emit changed();
    if (m_quitAfterExport) {
        m_quitAfterExport = false;
        emit quitRequested();
    }
}

} // namespace FlappedEar
