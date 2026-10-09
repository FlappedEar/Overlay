#pragma once

#include "export/BoundedProcessOutput.h"
#include "export/ExportDiagnostics.h"
#include "export/ExportOutputTransaction.h"
#include "export/ExportProcessSupervisor.h"
#include "export/MediaProbe.h"
#include "export/PersistentExportLog.h"
#include "project/AdditionalVideos.h"
#include "telemetry/TelemetrySession.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QProcess>
#include <QSize>
#include <QStringList>
#include <QTemporaryFile>
#include <QTimer>
#include <QVariantMap>

#include <memory>

class ExportTests;
class ProjectTests;
class SourceTests;

namespace FlappedEar {

// KAN-215: one export run of the editor's overlay. It validates the request
// against the source, prepares the output transaction and its ownership
// manifest, runs and supervises the export worker, follows its progress and
// diagnostics, and commits or cleans up when the worker ends. QML reaches it
// as appController.exporter.
class ExportController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool exporting READ exporting NOTIFY changed)
    Q_PROPERTY(int progress READ progress NOTIFY changed)
    Q_PROPERTY(QString state READ state NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QVariantMap metrics READ metrics NOTIFY changed)
    Q_PROPERTY(QVariantMap progressInfo READ progressInfo NOTIFY changed)
    Q_PROPERTY(bool progressVisible READ progressVisible NOTIFY changed)
    Q_PROPERTY(QString diagnosticLog READ diagnosticLog NOTIFY diagnosticLogChanged)
    Q_PROPERTY(qint64 diagnosticDroppedCharacters READ diagnosticDroppedCharacters NOTIFY diagnosticLogChanged)

public:
    // What the editor exports: the source video (or its chapters), the
    // telemetry, and the scene drawn over it.
    struct Job {
        QString inputPath;
        QStringList chapterPaths;          // every chapter of a chaptered source (KAN-106)
        QJsonArray chapterDurationTicks;
        QString telemetryPath;
        QStringList protectedPaths;        // never overwritten by the output
        QJsonObject lapBinding;
        QJsonArray lapExclusions;
        QJsonArray widgets;
        SyncTransform sync;
        MediaInfo source;
        // KAN-131: videos composed with the main one, and their layout.
        struct AdditionalVideo {
            QString id;
            QString path;
            QString label;
            SyncTransform sync;
        };
        QVector<AdditionalVideo> additionalVideos;
        VideoLayout videoLayout;
    };
    // What the export dialog asked for.
    struct Request {
        QString outputPath;
        QSize outputSize;
        MediaRational frameRate;
        qint64 videoBitrate = 0;
        bool audioEnabled = false;
        bool customRange = false;
        QString rangeIn;
        QString rangeOut;
        bool overwriteAllowed = false;
    };

    explicit ExportController(QObject *parent = nullptr);
    ~ExportController() override;

    [[nodiscard]] bool exporting() const { return m_exportProcess != nullptr; }
    [[nodiscard]] int progress() const { return m_exportProgress; }
    [[nodiscard]] QString state() const { return m_exportState; }
    [[nodiscard]] QString error() const { return m_exportError; }
    [[nodiscard]] QVariantMap metrics() const { return m_exportMetrics; }
    [[nodiscard]] QVariantMap progressInfo() const { return m_exportProgressInfo; }
    [[nodiscard]] bool progressVisible() const { return m_exportProgressVisible; }
    [[nodiscard]] QString diagnosticLog() const { return m_exportDiagnosticLog.text(); }
    [[nodiscard]] qint64 diagnosticDroppedCharacters() const { return m_exportDiagnosticLog.droppedCharacters(); }

    // Starts the worker; false with state() and error() saying why not.
    bool start(const Job &job, const Request &request);
    // Publishes a request refused before start() could check it.
    void fail(const QString &error);
    // Clears the last run's progress and diagnostics when the editor closes
    // its sources.
    void reset();

    Q_INVOKABLE void cancel();
    Q_INVOKABLE void cancelAndQuit();
    Q_INVOKABLE void dismissProgress();
    Q_INVOKABLE void copyDiagnostics();

signals:
    void changed();
    void diagnosticLogChanged();
    // A line for the editor's status bar.
    void statusMessage(const QString &status);
    // cancelAndQuit() has nothing left to wait for.
    void quitRequested();

private:
    friend class ::ExportTests; // Controlled asynchronous completion in regression tests.
    friend class ::ProjectTests;
    friend class ::SourceTests;
    void handleExportOutput();
    void finishExport(int exitCode, QProcess::ExitStatus exitStatus);
    void appendExportDiagnostic(const QString &entry);
    void appendExportLifecycle(const QString &event);
    void finishPersistentExportLog(const QString &result, const QString &error = {});

    std::unique_ptr<QProcess> m_exportProcess;
    std::unique_ptr<ExportProcessSupervisor> m_exportSupervisor;
    std::unique_ptr<QTemporaryFile> m_exportConfig;
    std::unique_ptr<ExportOutputTransaction> m_exportOutputTransaction;
    QByteArray m_exportStdout;
    BoundedProcessOutput m_exportStderr{BoundedProcessOutput::Mode::DiagnosticTail,
                                        ProcessOutputLimits::ffmpegDiagnosticTailBytes};
    QString m_exportCancelPath;
    QString m_exportSupervisionReadyPath;
    QString m_exportManifestPath;
    int m_exportProgress = 0;
    QString m_exportState = QStringLiteral("idle");
    QString m_exportError;
    QVariantMap m_exportMetrics;
    QVariantMap m_exportProgressInfo;
    BoundedDiagnosticLog m_exportDiagnosticLog{1500};
    // Coalesces diagnosticLogChanged: a busy export logs faster than the
    // Very verbose view should re-lay out its text.
    QTimer m_exportDiagnosticNotifier;
    std::unique_ptr<PersistentExportLog> m_persistentExportLog;
    bool m_exportProgressVisible = false;
    bool m_quitAfterExport = false;
};

} // namespace FlappedEar
