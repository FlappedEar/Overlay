#pragma once

#include "app/AnalysisDocument.h"
#include "app/DocumentHost.h"
#include "project/ProjectDocumentState.h"
#include "project/ProjectRecoveryStore.h"
#include "project/ProjectWriter.h"
#include "telemetry/TelemetryFolderScan.h"
#include "telemetry/TelemetryImportPlan.h"

#include <QFutureWatcher>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QSettings>
#include <QTimer>
#include <QUrl>
#include <QVariant>
#include <atomic>
#include <memory>
#include <optional>

class ProjectTests;
class SourceTests;

namespace FlappedEar {

// KAN-124: the project document -- the saved `.fetproject`, its identity and
// revisions, dirty state, recovery, new/open/save/quit decisions, run
// selection and day import. It knows nothing of video, synchronisation,
// overlays or export: the application adds those through DocumentHost.
// Analysis reads and edits it as its AnalysisDocument. AppController
// forwards its QML API here unchanged.
class DocumentController final : public QObject, public AnalysisDocument {
    Q_OBJECT
    Q_PROPERTY(QUrl projectPath READ projectPath NOTIFY documentStateChanged)
    Q_PROPERTY(QString eventName READ eventName NOTIFY documentStateChanged)
    Q_PROPERTY(QVariantList eventRuns READ eventRuns NOTIFY documentStateChanged)
    Q_PROPERTY(QString activeRunId READ activeRunId NOTIFY documentStateChanged)
    Q_PROPERTY(QString batchImportState READ batchImportState NOTIFY batchImportChanged)
    Q_PROPERTY(QString batchImportError READ batchImportError NOTIFY batchImportChanged)
    Q_PROPERTY(QStringList analysisImportMessages READ analysisImportMessages NOTIFY batchImportChanged)
    Q_PROPERTY(QVariantList batchImportRows READ batchImportRows NOTIFY batchImportChanged)
    Q_PROPERTY(int batchImportProcessed READ batchImportProcessed NOTIFY batchImportChanged)
    Q_PROPERTY(int batchImportTotal READ batchImportTotal NOTIFY batchImportChanged)
    Q_PROPERTY(bool dirty READ dirty NOTIFY documentStateChanged)
    Q_PROPERTY(quint64 lastSavedRevision READ lastSavedRevision NOTIFY documentStateChanged)
    Q_PROPERTY(QString pendingDestructiveAction READ pendingDestructiveAction NOTIFY destructiveActionChanged)
    Q_PROPERTY(bool projectLoading READ projectLoading NOTIFY projectLoadChanged)
    Q_PROPERTY(QString projectLoadStage READ projectLoadStage NOTIFY projectLoadChanged)
    Q_PROPERTY(QString projectLoadError READ projectLoadError NOTIFY projectLoadChanged)
    Q_PROPERTY(bool recoveryPending READ recoveryPending NOTIFY recoveryChanged)
    Q_PROPERTY(bool recoveryDegraded READ recoveryDegraded NOTIFY recoveryChanged)
    Q_PROPERTY(QString recoveryError READ recoveryError NOTIFY recoveryChanged)
    // KAN-90: the pending attach review or primary change: state (checking,
    // review, attaching, switching, error), runId, message, name, evidence.
    Q_PROPERTY(QVariantMap runRecordingReview READ runRecordingReview NOTIFY runRecordingsChanged)

public:
    DocumentController(DocumentHost &host, QString recoveryPath = {},
                       ProjectRecoveryStore::Operations recoveryOperations = {}, QObject *parent = nullptr);
    ~DocumentController() override;

    // Restores the previous session: offers recovery or reopens the last project.
    void startup();
    // The canonical path of an existing file, else the absolute path.
    [[nodiscard]] static QString normalizedSourcePath(const QString &path);

    [[nodiscard]] QUrl projectPath() const;
    [[nodiscard]] QString eventName() const;
    [[nodiscard]] QVariantList eventRuns() const override;
    [[nodiscard]] QString activeRunId() const override;
    [[nodiscard]] bool dirty() const override;
    [[nodiscard]] quint64 lastSavedRevision() const;
    [[nodiscard]] QString pendingDestructiveAction() const;
    [[nodiscard]] bool projectLoading() const override;
    [[nodiscard]] QString projectLoadStage() const;
    [[nodiscard]] QString projectLoadError() const;
    [[nodiscard]] bool recoveryPending() const override;
    [[nodiscard]] bool recoveryDegraded() const;
    [[nodiscard]] QString recoveryError() const;
    [[nodiscard]] QString batchImportState() const { return m_batchState; }
    [[nodiscard]] QString batchImportError() const { return m_batchError; }
    [[nodiscard]] QStringList analysisImportMessages() const { return m_analysisImportMessages; }
    [[nodiscard]] QVariantList batchImportRows() const { return m_batchRows; }
    [[nodiscard]] int batchImportProcessed() const { return m_batchProcessed; }
    [[nodiscard]] int batchImportTotal() const { return m_batchTotal; }

    Q_INVOKABLE bool selectEventRun(const QString &runId);
    Q_INVOKABLE bool beginBatchImport(const QList<QUrl> &urls);
    Q_INVOKABLE bool importAnalysisRuns(const QString &name, const QList<QUrl> &urls);
    // KAN-87: the VBO/RCZ recordings of a folder (and, when asked, its
    // subfolders) through the same review; see scanTelemetryFolder.
    Q_INVOKABLE bool importAnalysisFolder(const QString &name, const QUrl &folder, bool includeSubfolders);
    // KAN-88: dropped (or chosen) files and folders, in any mix; see
    // scanTelemetrySources. Unsupported items are reported, not imported.
    Q_INVOKABLE bool importAnalysisSources(const QString &name, const QList<QUrl> &urls, bool includeSubfolders);
    Q_INVOKABLE void cancelBatchImport();
    Q_INVOKABLE bool confirmBatchImport(const QString &name, bool append, const QVariantList &choices);
    Q_INVOKABLE void requestNewProject();
    Q_INVOKABLE void requestOpenProject(const QUrl &url);
    Q_INVOKABLE void requestQuit();
    Q_INVOKABLE void resolveDestructiveAction(const QString &decision);
    Q_INVOKABLE void cancelPendingDestructiveAction();
    Q_INVOKABLE bool saveCurrentProject();
    Q_INVOKABLE bool saveProject(const QUrl &url);
    Q_INVOKABLE void resolveStartupRecovery(const QString &decision);
    // KAN-90: a run's recordings (sourceId, name, format, primary, available).
    Q_INVOKABLE QVariantList runRecordings(const QString &runId) const;
    // Reads `url` and the run's primary off-thread and offers their match
    // evidence for review; confirmRunRecording rechecks the file and adds it
    // as an alternative (never the primary).
    Q_INVOKABLE bool attachRunRecording(const QString &runId, const QUrl &url);
    Q_INVOKABLE bool confirmRunRecording();
    Q_INVOKABLE void cancelRunRecording();
    // Makes an attached recording the run's primary after verifying it;
    // the run's laps are derived again from it.
    Q_INVOKABLE bool setRunPrimarySource(const QString &runId, const QString &sourceId);
    // KAN-101: describes how an alternative recording's clock lines up with
    // the run's primary (declared and measured offset, drift, uncertainty,
    // evidence). Read-only: nothing is applied or fused. The result arrives
    // as runRecordingReview state "alignment".
    Q_INVOKABLE bool checkRunRecordingAlignment(const QString &runId, const QString &sourceId);
    // KAN-103: review fusing an alternative into the run's analysis: its clock
    // alignment, the resulting channels with coverage, and every conflict. The
    // result is runRecordingReview state "fusionReview"; approving needs an
    // aligned clock and a rule for every conflicting channel, and binds the
    // decision to both recordings' content.
    Q_INVOKABLE bool reviewRunFusion(const QString &runId, const QString &sourceId);
    Q_INVOKABLE bool approveRunFusion(const QVariantMap &rules);
    Q_INVOKABLE bool removeRunFusion(const QString &runId);
    [[nodiscard]] QVariantMap runRecordingReview() const { return m_recordingReview; }

    // For the host's editor state.
    // The stored project as last committed (no editor state applied).
    [[nodiscard]] const QJsonObject &storedProject() const { return m_projectTemplate; }
    // Replaces the stored project without recording a change (the caller
    // records it, or it is the document's own bookkeeping).
    void replaceStoredProject(const QJsonObject &project) { m_projectTemplate = project; }
    // Records a user change: dirty state, notification and recovery.
    void markPersistentChange();
    [[nodiscard]] QJsonObject currentProjectObject(const QString &projectPath = {},
                                                    std::optional<quint64> savedRevision = std::nullopt) const;
    [[nodiscard]] QString documentPath() const { return m_documentState.projectPath(); }
    [[nodiscard]] quint64 nextSourceGeneration() { return ++m_sourceGeneration; }
    [[nodiscard]] bool projectLoadRunning() const { return m_projectLoadWatcher.isRunning(); }
    void cancelProjectLoad();
    [[nodiscard]] bool importRunning() const { return m_batchWatcher.isRunning() || m_folderScanWatcher.isRunning() || m_recordingWatcher.isRunning(); }
    void cancelImport();
    void setProjectLoadState(bool loading, QString stage = {}, QString error = {});
    // While alive, changes are the document's own state, not user edits.
    class DirtySuppression {
    public:
        DirtySuppression(DocumentController &document, bool suppress)
            : m_document(document), m_previous(document.m_suppressDirtyTracking)
        {
            document.m_suppressDirtyTracking = m_previous || suppress;
        }
        ~DirtySuppression() { m_document.m_suppressDirtyTracking = m_previous; }
        DirtySuppression(const DirtySuppression &) = delete;
        DirtySuppression &operator=(const DirtySuppression &) = delete;
    private:
        DocumentController &m_document;
        bool m_previous;
    };

    // AnalysisDocument.
    [[nodiscard]] QJsonObject analysisProject() const override { return currentProjectObject(); }
    void commitAnalysisProject(const QJsonObject &project) override;
    [[nodiscard]] bool isEventDocument() const override;
    [[nodiscard]] QString documentProjectPath() const override { return m_documentState.projectPath(); }
    [[nodiscard]] QString documentIdentity() const override { return m_documentId; }
    [[nodiscard]] quint64 documentRevision() const override { return m_documentState.revision(); }
    [[nodiscard]] quint64 sourceGeneration() const override { return m_sourceGeneration; }
    [[nodiscard]] bool documentBusy() const override { return m_host.documentBusy(); }
    [[nodiscard]] bool batchImportPending() const override { return m_batchPending; }
    [[nodiscard]] bool destructiveActionPending() const override;
    [[nodiscard]] bool dirtyTrackingSuppressed() const override { return m_suppressDirtyTracking; }

signals:
    void batchImportChanged();
    void batchImportCommitted();
    void documentStateChanged();
    void destructiveActionChanged();
    void projectLoadChanged();
    void recoveryChanged();
    void saveAsRequested();
    void quitApproved();
    void runRecordingsChanged();
    // Input, forwarded from the host's signal of the same name.
    void sourceLoadStateChanged();

private:
    friend class ::ProjectTests; // Controlled asynchronous completion in regression tests.
    friend class ::SourceTests;
    struct BatchImportResult {
        std::shared_ptr<TelemetryImportPlan> plan;
        QHash<QString, QJsonObject> fingerprints;
        QSet<QString> existing;
        QJsonObject project;
        QString error;
        bool cancelled = false;
        bool confirmation = false;
        bool append = false;
    };
    [[nodiscard]] bool commitProjectLoad(const ProjectLoadResult &result);
    bool beginProjectLoad(QString projectPath, const QJsonObject &project,
                          bool recovered = false, quint64 recoveredRevision = 0,
                          quint64 recoveredLastSavedRevision = 0,
                          QString recoveredDocumentId = {}, bool runSelection = false);
    void restoreStartupState();
    void scheduleRecoveryWrite();
    void writeRecoverySnapshot();
    bool clearRecovery(const QString &reason);
    bool discardRecovery(const ProjectRecoverySnapshot &snapshot, const QString &reason);
    void clearDiscardTombstoneAfterRecoveryCleanup();
    void retireLegacyDocumentSettings();
    void performClearProject();
    bool performOpenProject(const QUrl &url);
    void beginDestructiveAction(ProjectDocumentState::DestructiveAction action, const QUrl &openUrl = {});
    void performPendingDestructiveAction();
    void initializeBatchImport();
    void invalidateBatchImport();
    [[nodiscard]] bool batchContextMatches() const;
    [[nodiscard]] QByteArray eventSourcesSignature() const;
    void publishBatchRows();
    struct RecordingWork {
        enum class Kind { Attach, Confirm, Primary, Align, FusionReview };
        Kind kind = Kind::Attach;
        QString sourceId;
        QString path;
        QString format;
        QByteArray sha;
        QJsonObject fingerprint;
        QString gateRevision;
        QVariantMap evidence;
        QVariantMap alignment;
        QVariantMap fusionPreview;
        QJsonObject fusionDecision; // clock and both revisions, rules added on approval
        QString error;
        bool cancelled = false;
    };
    void initializeRunRecordings();
    [[nodiscard]] bool recordingEditAllowed() const;
    void setRecordingReview(QVariantMap review);

    DocumentHost &m_host;
    QSettings m_settings;
    QJsonObject m_projectTemplate;
    ProjectWriter m_projectWriter;
    ProjectDocumentState m_documentState;
    ProjectRecoveryStore m_recoveryStore;
    QString m_documentId;
    ProjectRecoverySnapshot m_pendingRecovery;
    QTimer m_recoveryTimer;
    bool m_recoveryPending = false;
    bool m_recoveryDegraded = false;
    QString m_recoveryError;
    QUrl m_pendingOpenProject;
    bool m_suppressDirtyTracking = false;
    quint64 m_sourceGeneration = 0;
    QFutureWatcher<ProjectLoadResult> m_projectLoadWatcher;
    std::shared_ptr<std::atomic_bool> m_projectLoadCancellation;
    bool m_projectLoading = false;
    QString m_projectLoadStage;
    QString m_projectLoadError;
    QFutureWatcher<BatchImportResult> m_batchWatcher;
    QFutureWatcher<TelemetryFolderScan> m_folderScanWatcher;
    QString m_folderImportName;
    QTimer m_batchProgressTimer;
    std::shared_ptr<std::atomic_bool> m_batchCancellation;
    std::shared_ptr<std::atomic_int> m_batchProgress;
    std::shared_ptr<TelemetryImportPlan> m_batchPlan;
    QHash<QString, QJsonObject> m_batchFingerprints;
    QSet<QString> m_batchExisting;
    QVariantList m_batchRows;
    QString m_batchState = QStringLiteral("idle");
    QString m_batchError;
    QString m_batchDocumentId;
    QString m_batchProjectPath;
    quint64 m_batchRevision = 0;
    QByteArray m_batchSources;
    bool m_batchAppendsToEvent = false;
    quint64 m_batchGeneration = 0;
    int m_batchProcessed = 0;
    int m_batchTotal = 0;
    bool m_batchApplying = false;
    bool m_batchPending = false;
    bool m_analysisImportAutomatic = false;
    bool m_analysisImportAppend = false;
    QString m_analysisImportName;
    QStringList m_analysisImportMessages;
    QFutureWatcher<RecordingWork> m_recordingWatcher;
    std::shared_ptr<std::atomic_bool> m_recordingCancellation;
    QVariantMap m_recordingReview;
    RecordingWork m_recordingCandidate;
    QString m_recordingRunId;
    QString m_recordingDocumentId;
    // A recording review depends only on the event's recordings (KAN-150): the
    // day's own analysis bookkeeping, such as a verified track inference, does
    // not make it stale. Applying it builds on the current project.
    QByteArray m_recordingSources;
};

} // namespace FlappedEar
