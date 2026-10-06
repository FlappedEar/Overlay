#include "export/ExportDiagnostics.h"

#include <QtGlobal>

namespace FlappedEar {

void ExportStageTimer::start(const qint64 nowMilliseconds, const QString &stage)
{
    m_totalStartedMilliseconds = nowMilliseconds;
    m_stageStartedMilliseconds = nowMilliseconds;
    m_stage = stage;
    m_completedStageDurations.clear();
}

void ExportStageTimer::transition(const qint64 nowMilliseconds, const QString &stage)
{
    if (m_stage == stage) {
        return;
    }
    if (!m_stage.isEmpty()) {
        m_completedStageDurations.insert(
            m_stage, qMax<qint64>(0, nowMilliseconds - m_stageStartedMilliseconds));
    }
    m_stage = stage;
    m_stageStartedMilliseconds = nowMilliseconds;
}

qint64 ExportStageTimer::totalElapsedMilliseconds(const qint64 nowMilliseconds) const
{
    return qMax<qint64>(0, nowMilliseconds - m_totalStartedMilliseconds);
}

qint64 ExportStageTimer::stageElapsedMilliseconds(const qint64 nowMilliseconds) const
{
    return qMax<qint64>(0, nowMilliseconds - m_stageStartedMilliseconds);
}

QHash<QString, qint64> ExportStageTimer::completedStageDurations() const
{
    return m_completedStageDurations;
}

QString ExportStageTimer::stage() const { return m_stage; }

DiagnosticHeartbeat::DiagnosticHeartbeat(const qint64 intervalMilliseconds)
    : m_intervalMilliseconds(qMax<qint64>(1, intervalMilliseconds))
{
}

bool DiagnosticHeartbeat::shouldEmit(const qint64 elapsedMilliseconds)
{
    if (m_lastEmissionMilliseconds < 0
        || elapsedMilliseconds - m_lastEmissionMilliseconds >= m_intervalMilliseconds) {
        m_lastEmissionMilliseconds = elapsedMilliseconds;
        return true;
    }
    return false;
}

BoundedDiagnosticLog::BoundedDiagnosticLog(const qsizetype maximumEntries)
    : m_maximumEntries(qMax<qsizetype>(2, maximumEntries))
{
}

void BoundedDiagnosticLog::clear()
{
    m_entries.clear();
    m_droppedCharacters = 0;
    m_omissionMarkerNeeded = false;
}

void BoundedDiagnosticLog::append(const QString &entry)
{
    m_entries.append(entry);
    const qsizetype contentLimit = m_maximumEntries - 1;
    while (m_entries.size() > contentLimit) {
        m_droppedCharacters += m_entries.first().size() + 1;
        m_entries.removeFirst();
        m_omissionMarkerNeeded = true;
    }
}

QString BoundedDiagnosticLog::text() const
{
    QStringList visible = m_entries;
    if (m_omissionMarkerNeeded) {
        visible.prepend(QStringLiteral("[older diagnostic entries omitted]"));
    }
    return visible.join(QLatin1Char('\n'));
}

qsizetype BoundedDiagnosticLog::size() const
{
    return m_entries.size() + (m_omissionMarkerNeeded ? 1 : 0);
}

qsizetype BoundedDiagnosticLog::maximumEntries() const { return m_maximumEntries; }

QVariantMap stageAFailureDiagnosticDetails(const StageAFailureDiagnostics &diagnostics)
{
    return {{"reason", diagnostics.reason},
            {"submittedFrames", static_cast<qint64>(diagnostics.submittedFrames)},
            {"expectedFrames", static_cast<qint64>(diagnostics.expectedFrames)},
            {"exitCode", diagnostics.exitCode},
            {"exitStatus", diagnostics.exitStatus},
            {"processError", diagnostics.processError},
            {"processErrorString", diagnostics.processErrorString},
            {"lastEncodedFrame", static_cast<qint64>(diagnostics.lastEncodedFrame)},
            {"lastEncodedTimeMicroseconds", diagnostics.lastEncodedTimeMicroseconds},
            {"encoderFps", diagnostics.encoderFps},
            {"encoderRealtimeFactor", diagnostics.encoderRealtimeFactor},
            {"queuedBytes", diagnostics.queuedBytes},
            {"maximumQueuedBytes", diagnostics.maximumQueuedBytes},
            {"temporaryOverlayPath", diagnostics.temporaryOverlayPath},
            {"temporaryOverlayBytes", diagnostics.temporaryOverlayBytes},
            {"stderrTail", diagnostics.stderrTail},
            {"temporaryFilesystemRoot", diagnostics.temporaryFilesystemRoot},
            {"temporaryFilesystemAvailableBytes", diagnostics.temporaryFilesystemAvailableBytes},
            {"temporaryFilesystemTotalBytes", diagnostics.temporaryFilesystemTotalBytes},
            {"destinationFilesystemRoot", diagnostics.destinationFilesystemRoot},
            {"destinationFilesystemAvailableBytes", diagnostics.destinationFilesystemAvailableBytes},
            {"destinationFilesystemTotalBytes", diagnostics.destinationFilesystemTotalBytes},
            {"cancellationFilePath", diagnostics.cancellationFilePath},
            {"cancellationFileExists", diagnostics.cancellationFileExists}};
}

QString formatStageAFailureDiagnostics(const StageAFailureDiagnostics &diagnostics)
{
    const QString encodedTime = diagnostics.lastEncodedTimeMicroseconds >= 0
        ? QString::number(diagnostics.lastEncodedTimeMicroseconds / 1'000'000.0, 'f', 3)
        : QStringLiteral("unavailable");
    QString result = QStringLiteral(
        "Stage A exited unexpectedly\n"
        "Reason: %1\n"
        "Submitted frames: %2 / %3\n"
        "FFmpeg exit: code=%4 status=%5\n"
        "QProcess error: %6 (%7)\n"
        "Last FFmpeg progress: frame=%8 time=%9 s fps=%10 speed=%11x\n"
        "Pipe: queued=%12 bytes maximumQueued=%13 bytes\n"
        "Temporary overlay before cleanup: %14 (%15 bytes)\n"
        "Temporary filesystem: root=%16 available=%17 bytes total=%18 bytes\n"
        "Destination filesystem: root=%19 available=%20 bytes total=%21 bytes\n"
        "Cancellation marker: %22 exists=%23")
                         .arg(diagnostics.reason)
                         .arg(diagnostics.submittedFrames)
                         .arg(diagnostics.expectedFrames)
                         .arg(diagnostics.exitCode)
                         .arg(diagnostics.exitStatus)
                         .arg(diagnostics.processError, diagnostics.processErrorString)
                         .arg(diagnostics.lastEncodedFrame)
                         .arg(encodedTime)
                         .arg(diagnostics.encoderFps, 0, 'f', 2)
                         .arg(diagnostics.encoderRealtimeFactor, 0, 'f', 2)
                         .arg(diagnostics.queuedBytes)
                         .arg(diagnostics.maximumQueuedBytes)
                         .arg(diagnostics.temporaryOverlayPath)
                         .arg(diagnostics.temporaryOverlayBytes)
                         .arg(diagnostics.temporaryFilesystemRoot)
                         .arg(diagnostics.temporaryFilesystemAvailableBytes)
                         .arg(diagnostics.temporaryFilesystemTotalBytes)
                         .arg(diagnostics.destinationFilesystemRoot)
                         .arg(diagnostics.destinationFilesystemAvailableBytes)
                         .arg(diagnostics.destinationFilesystemTotalBytes)
                         .arg(diagnostics.cancellationFilePath)
                         .arg(diagnostics.cancellationFileExists ? QStringLiteral("yes")
                                                                 : QStringLiteral("no"));
    if (!diagnostics.stderrTail.trimmed().isEmpty()) {
        result += QStringLiteral("\n\nFFmpeg stderr tail:\n") + diagnostics.stderrTail.trimmed();
    }
    return result;
}

ExportFailureReport describeExportFailure(const QString &error, const QString &diagnostics)
{
    const bool outOfSpace = diagnostics.contains(QStringLiteral("No space left on device"))
        || error.contains(QStringLiteral("No space left on device"));
    if (!outOfSpace) return {error, diagnostics};
    return {QStringLiteral("The disk ran out of space during the export. Nothing was written to the "
                           "chosen file. Free some space, or choose another destination, and export again."),
            QStringLiteral("Original error: %1\n%2").arg(error, diagnostics)};
}

} // namespace FlappedEar
