#pragma once

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVariantMap>

namespace FlappedEar {

class ExportStageTimer final {
public:
    void start(qint64 nowMilliseconds, const QString &stage);
    void transition(qint64 nowMilliseconds, const QString &stage);
    [[nodiscard]] qint64 totalElapsedMilliseconds(qint64 nowMilliseconds) const;
    [[nodiscard]] qint64 stageElapsedMilliseconds(qint64 nowMilliseconds) const;
    [[nodiscard]] QHash<QString, qint64> completedStageDurations() const;
    [[nodiscard]] QString stage() const;

private:
    qint64 m_totalStartedMilliseconds = 0;
    qint64 m_stageStartedMilliseconds = 0;
    QString m_stage;
    QHash<QString, qint64> m_completedStageDurations;
};

class DiagnosticHeartbeat final {
public:
    explicit DiagnosticHeartbeat(qint64 intervalMilliseconds = 500);
    [[nodiscard]] bool shouldEmit(qint64 elapsedMilliseconds);

private:
    qint64 m_intervalMilliseconds = 500;
    qint64 m_lastEmissionMilliseconds = 0;
};

class BoundedDiagnosticLog final {
public:
    explicit BoundedDiagnosticLog(qsizetype maximumEntries = 1500);
    void clear();
    void append(const QString &entry);
    [[nodiscard]] QString text() const;
    [[nodiscard]] qsizetype size() const;
    [[nodiscard]] qsizetype maximumEntries() const;
    // Characters of text() (entries and their line breaks) removed from the head since
    // the last clear(); a view uses it to keep the same history in place while the log trims.
    [[nodiscard]] qint64 droppedCharacters() const { return m_droppedCharacters; }

private:
    qsizetype m_maximumEntries = 1500;
    qint64 m_droppedCharacters = 0;
    bool m_omissionMarkerNeeded = false;
    QStringList m_entries;
};

// Captures the state that matters when the raw overlay producer loses FFmpeg
// before all expected frames have been accepted.  This deliberately contains
// only value types so formatting and event payloads can be regression tested
// without a live child process.
struct StageAFailureDiagnostics {
    QString reason;
    qsizetype submittedFrames = 0;
    qsizetype expectedFrames = 0;
    int exitCode = -1;
    QString exitStatus;
    QString processError;
    QString processErrorString;
    qsizetype lastEncodedFrame = 0;
    qint64 lastEncodedTimeMicroseconds = -1;
    double encoderFps = 0.0;
    double encoderRealtimeFactor = 0.0;
    qint64 queuedBytes = 0;
    qint64 maximumQueuedBytes = 0;
    QString temporaryOverlayPath;
    qint64 temporaryOverlayBytes = 0;
    QString stderrTail;
    QString temporaryFilesystemRoot;
    qint64 temporaryFilesystemAvailableBytes = -1;
    qint64 temporaryFilesystemTotalBytes = -1;
    QString destinationFilesystemRoot;
    qint64 destinationFilesystemAvailableBytes = -1;
    qint64 destinationFilesystemTotalBytes = -1;
    QString cancellationFilePath;
    bool cancellationFileExists = false;
};

[[nodiscard]] QVariantMap stageAFailureDiagnosticDetails(const StageAFailureDiagnostics &diagnostics);
[[nodiscard]] QString formatStageAFailureDiagnostics(const StageAFailureDiagnostics &diagnostics);

// KAN-75: a failed export whose diagnostics show the disk filled up (ENOSPC)
// gets a plain message instead of the stage's technical error, which moves
// into the diagnostics. Other failures are returned unchanged.
struct ExportFailureReport {
    QString error;
    QString diagnostics;
};
[[nodiscard]] ExportFailureReport describeExportFailure(const QString &error, const QString &diagnostics);

} // namespace FlappedEar
