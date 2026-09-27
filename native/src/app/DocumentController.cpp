#include "app/DocumentController.h"
#include "app/AppLog.h"
#include "project/BoundedJsonLoader.h"
#include "project/EventProjectCodec.h"
#include "project/ProjectLimits.h"

#include <QDateTime>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QScopedValueRollback>
#include <QUuid>
#include <QtConcurrent>
#include <cmath>
#include <limits>
#include <utility>

namespace FlappedEar {

namespace {

constexpr int RecoveryWriteDelayMs = 250;
constexpr int RecoveryRetryDelayMs = 5'000;

struct SavedDocumentMetadata final {
    QString id;
    quint64 revision = 0;
};

bool parseSavedDocumentMetadata(const QJsonObject &project, SavedDocumentMetadata *metadata)
{
    const QJsonValue stateValue = project.value(QStringLiteral("documentState"));
    if (!stateValue.isObject()) return false;
    const QJsonObject state = stateValue.toObject();
    const QString id = state.value(QStringLiteral("id")).toString();
    if (id.isEmpty() || id.size() > 128 || !state.value(QStringLiteral("savedRevision")).isString()) {
        return false;
    }
    bool revisionOk = false;
    const quint64 revision = state.value(QStringLiteral("savedRevision")).toString().toULongLong(&revisionOk);
    if (!revisionOk) return false;
    if (metadata) *metadata = {id, revision};
    return true;
}

std::optional<SavedDocumentMetadata> loadSavedDocumentMetadata(const QString &path)
{
    if (path.isEmpty() || !QFileInfo(path).isFile()) return std::nullopt;
    const auto loaded = BoundedJsonLoader::loadFile(
        path, ProjectLimits::projectBytes, QStringLiteral("Saved project"));
    SavedDocumentMetadata metadata;
    QString validationError;
    if (!loaded.success() || !loaded.document.isObject()
        || !ProjectLimits::validateProject(loaded.document.object(), &validationError)
        || !parseSavedDocumentMetadata(loaded.document.object(), &metadata)) {
        return std::nullopt;
    }
    return metadata;
}

enum class RecoveryValidity {
    Valid,
    Stale,
    Invalid,
};

RecoveryValidity recoveryValidity(const ProjectRecoverySnapshot &snapshot,
                                  const QString &rememberedProjectPath)
{
    if (!snapshot.hasLogicalMetadata) return RecoveryValidity::Valid;
    if (snapshot.revision <= snapshot.lastSavedRevision) return RecoveryValidity::Stale;

    QStringList authorityPaths;
    if (!rememberedProjectPath.isEmpty()) authorityPaths.append(rememberedProjectPath);
    if (!snapshot.originalProjectPath.isEmpty()
        && !authorityPaths.contains(snapshot.originalProjectPath)) {
        authorityPaths.append(snapshot.originalProjectPath);
    }
    bool foundAuthority = false;
    for (const QString &path : authorityPaths) {
        const auto metadata = loadSavedDocumentMetadata(path);
        if (!metadata) continue;
        foundAuthority = true;
        if (metadata->id != snapshot.documentId) continue;
        return snapshot.revision <= metadata->revision
            ? RecoveryValidity::Stale : RecoveryValidity::Valid;
    }
    return foundAuthority ? RecoveryValidity::Invalid : RecoveryValidity::Valid;
}

} // namespace

DocumentController::DocumentController(DocumentHost &host, QString recoveryPath,
                                       ProjectRecoveryStore::Operations recoveryOperations, QObject *parent)
    : QObject(parent)
    , m_host(host)
    , m_recoveryStore(std::move(recoveryPath), std::move(recoveryOperations))
{
    m_documentId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_recoveryTimer.setSingleShot(true);
    m_recoveryTimer.setInterval(RecoveryWriteDelayMs);
    connect(&m_recoveryTimer, &QTimer::timeout, this, &DocumentController::writeRecoverySnapshot);
    connect(&m_projectLoadWatcher, &QFutureWatcher<ProjectLoadResult>::finished, this, [this] {
        const ProjectLoadResult result = m_projectLoadWatcher.result();
        if (result.generation != m_sourceGeneration) {
            AppLog::warn(QStringLiteral("Stale project load result rejected: %1")
                             .arg(result.projectPath));
            return;
        }
        if (!result.success) {
            if (result.cancelled) {
                AppLog::warn(QStringLiteral("Project source load cancelled: %1")
                                 .arg(result.projectPath));
                setProjectLoadState(false);
                return;
            }
            AppLog::error(QStringLiteral("Project load failed: %1: %2")
                              .arg(result.projectPath, result.error));
            setProjectLoadState(false, {}, result.error);
            m_host.showStatus(QStringLiteral("Project could not be opened: %1").arg(result.error));
            return;
        }
        if (result.documentRevisionAtStart != m_documentState.revision()) {
            AppLog::warn(QStringLiteral("Stale project load rejected due to document revision: %1")
                             .arg(result.projectPath));
            setProjectLoadState(false, {}, QStringLiteral("document changed while project was loading."));
            m_host.showStatus(QStringLiteral("Project load cancelled because the current document changed."));
            return;
        }
        if (commitProjectLoad(result)) {
            m_host.startEditorSources(result);
        }
    });
    initializeBatchImport();
    initializeRunRecordings();
}

DocumentController::~DocumentController() = default;

QString DocumentController::normalizedSourcePath(const QString &path)
{
    const QFileInfo info(path);
    const QString canonical = info.canonicalFilePath();
    return canonical.isEmpty() ? info.absoluteFilePath() : canonical;
}

void DocumentController::startup()
{
    retireLegacyDocumentSettings();
    restoreStartupState();
}

void DocumentController::cancelProjectLoad()
{
    if (m_projectLoadCancellation) m_projectLoadCancellation->store(true);
}

void DocumentController::cancelImport()
{
    if (m_batchCancellation) m_batchCancellation->store(true);
    if (m_recordingCancellation) m_recordingCancellation->store(true);
}

QUrl DocumentController::projectPath() const
{
    return m_documentState.projectPath().isEmpty()
        ? QUrl() : QUrl::fromLocalFile(m_documentState.projectPath());
}

QString DocumentController::eventName() const
{
    return m_projectTemplate.value(QStringLiteral("event")).toObject().value(QStringLiteral("name")).toString();
}

QString DocumentController::activeRunId() const
{
    return m_projectTemplate.value(QStringLiteral("event")).toObject().value(QStringLiteral("activeRunId")).toString();
}

QVariantList DocumentController::eventRuns() const
{
    QVariantList result;
    const QJsonObject event = m_projectTemplate.value(QStringLiteral("event")).toObject();
    for (const QJsonValue &value : event.value(QStringLiteral("runs")).toArray()) {
        const QJsonObject run = value.toObject();
        result.append(QVariantMap{{QStringLiteral("id"), run.value(QStringLiteral("id")).toString()},
                                  {QStringLiteral("name"), run.value(QStringLiteral("name")).toString()}});
    }
    return result;
}

bool DocumentController::selectEventRun(const QString &runId)
{
    if (!EventProjectCodec::isEvent(m_projectTemplate) || projectLoading() || m_host.documentBusy()
        || recoveryPending() || m_documentState.pendingAction() != ProjectDocumentState::DestructiveAction::None) return false;
    if (runId == activeRunId()) return true;
    if (m_documentState.revision() == std::numeric_limits<quint64>::max()) return false;
    QJsonObject project = currentProjectObject();
    QJsonObject event = project.value(QStringLiteral("event")).toObject();
    bool found = false;
    for (const QJsonValue &value : event.value(QStringLiteral("runs")).toArray()) {
        found |= value.toObject().value(QStringLiteral("id")).toString() == runId;
    }
    if (!found) return false;
    event.insert(QStringLiteral("activeRunId"), runId);
    project.insert(QStringLiteral("event"), event);
    // Commit the complete document only after validation; source-generation cancellation
    // then prevents late results from the previous run from reaching preview/export.
    return beginProjectLoad(m_documentState.projectPath(), project, false, 0, 0, {}, true);
}

bool DocumentController::dirty() const { return m_documentState.dirty(); }

quint64 DocumentController::lastSavedRevision() const { return m_documentState.lastSavedRevision(); }

QString DocumentController::pendingDestructiveAction() const
{
    return ProjectDocumentState::actionName(m_documentState.pendingAction());
}

bool DocumentController::projectLoading() const { return m_projectLoading; }

QString DocumentController::projectLoadStage() const { return m_projectLoadStage; }

QString DocumentController::projectLoadError() const { return m_projectLoadError; }

bool DocumentController::recoveryPending() const { return m_recoveryPending; }

bool DocumentController::recoveryDegraded() const { return m_recoveryDegraded; }

QString DocumentController::recoveryError() const { return m_recoveryError; }

void DocumentController::performClearProject()
{
    const QScopedValueRollback suppressDirty(m_suppressDirtyTracking, true);
    static_cast<void>(m_host.beginSourceGeneration(false));
    // The generation above already settled the load state.
    m_projectTemplate = {};
    m_host.clearEditor();
    m_documentId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_documentState.reset();
    m_pendingOpenProject = QUrl();
    m_settings.remove("project/path");
    m_settings.sync();
    emit documentStateChanged();
    emit destructiveActionChanged();
    m_host.showStatus("New native project created.");
}

void DocumentController::requestNewProject()
{
    AppLog::info(QStringLiteral("New project requested"));
    beginDestructiveAction(ProjectDocumentState::DestructiveAction::NewProject);
}

void DocumentController::requestOpenProject(const QUrl &url)
{
    AppLog::info(QStringLiteral("Open project requested: %1").arg(url.toLocalFile()));
    beginDestructiveAction(ProjectDocumentState::DestructiveAction::OpenProject, url);
}

void DocumentController::requestQuit()
{
    AppLog::info(QStringLiteral("Quit requested"));
    if (m_host.documentBusy()) {
        AppLog::warn(QStringLiteral("Quit request deferred while export is running"));
        return;
    }
    beginDestructiveAction(ProjectDocumentState::DestructiveAction::Quit);
}

void DocumentController::resolveDestructiveAction(const QString &decision)
{
    if (m_documentState.pendingAction() == ProjectDocumentState::DestructiveAction::None) {
        return;
    }
    if (decision == QStringLiteral("cancel")) {
        cancelPendingDestructiveAction();
        return;
    }
    AppLog::info(QStringLiteral("Dirty project decision: %1").arg(decision));
    if (decision == QStringLiteral("discard")) {
        performPendingDestructiveAction();
        return;
    }
    if (decision != QStringLiteral("save")) {
        return;
    }
    if (m_documentState.projectPath().isEmpty()) {
        emit saveAsRequested();
        return;
    }
    saveCurrentProject();
}

void DocumentController::cancelPendingDestructiveAction()
{
    if (m_documentState.pendingAction() == ProjectDocumentState::DestructiveAction::None) {
        return;
    }
    AppLog::info(QStringLiteral("Dirty project decision: cancel"));
    m_documentState.cancelPendingAction();
    m_pendingOpenProject = QUrl();
    emit destructiveActionChanged();
}

bool DocumentController::saveCurrentProject()
{
    if (m_documentState.projectPath().isEmpty()) {
        AppLog::info(QStringLiteral("Project save requested: save as"));
        emit saveAsRequested();
        return false;
    }
    return saveProject(QUrl::fromLocalFile(m_documentState.projectPath()));
}

void DocumentController::resolveStartupRecovery(const QString &decision)
{
    if (!m_recoveryPending || (decision != QStringLiteral("recover")
                               && decision != QStringLiteral("discard"))) {
        return;
    }
    const ProjectRecoverySnapshot snapshot = m_pendingRecovery;
    m_pendingRecovery = {};
    m_recoveryPending = false;
    emit recoveryChanged();
    if (decision == QStringLiteral("recover")) {
        if (!beginProjectLoad(snapshot.originalProjectPath, snapshot.project, true,
                              snapshot.revision, snapshot.lastSavedRevision, snapshot.documentId)) {
            m_pendingRecovery = snapshot;
            m_recoveryPending = true;
            emit recoveryChanged();
        }
        return;
    }
    if (!discardRecovery(snapshot, QStringLiteral("startup discard"))) {
        m_pendingRecovery = snapshot;
        m_recoveryPending = true;
        emit recoveryChanged();
        m_host.showStatus(QStringLiteral("Could not discard recovery data."));
        return;
    }
    AppLog::info(QStringLiteral("Recovery discarded"));
    if (!snapshot.originalProjectPath.isEmpty() && QFileInfo(snapshot.originalProjectPath).isFile()) {
        performOpenProject(QUrl::fromLocalFile(snapshot.originalProjectPath));
    } else {
        performClearProject();
    }
}

bool DocumentController::performOpenProject(const QUrl &url)
{
    if (!url.isLocalFile()) return false;
    const QString projectPath = normalizedSourcePath(url.toLocalFile());
    AppLog::info(QStringLiteral("Project load started: %1").arg(projectPath));
    const quint64 generation = m_host.beginSourceGeneration(false);
    const quint64 documentRevision = m_documentState.revision();
    m_projectLoadCancellation = std::make_shared<std::atomic_bool>(false);
    const std::shared_ptr<std::atomic_bool> cancellation = m_projectLoadCancellation;
    setProjectLoadState(true, QStringLiteral("Validating project"));
    m_projectLoadWatcher.setFuture(QtConcurrent::run([projectPath, generation, documentRevision, cancellation] {
        ProjectLoadResult result;
        result.projectPath = projectPath;
        result.generation = generation;
        result.documentRevisionAtStart = documentRevision;
        if (cancellation->load()) { result.cancelled = true; return result; }
        const auto loaded = BoundedJsonLoader::loadFile(
            projectPath, ProjectLimits::projectBytes, QStringLiteral("Project"));
        if (!loaded.success() || !loaded.document.isObject()) { result.error = loaded.error; return result; }
        const QJsonObject project = loaded.document.object();
        QString validationError;
        if (!ProjectLimits::validateProject(project, &validationError)) { result.error = validationError; return result; }
        if (cancellation->load()) { result.cancelled = true; return result; }
        const QJsonObject editor = EventProjectCodec::editorProjection(project);
        const QJsonObject sync = editor.value(QStringLiteral("sync")).toObject();
        const double offset = sync.value(QStringLiteral("offset")).toDouble();
        const double timeScale = sync.value(QStringLiteral("timeScale")).toDouble(1.0);
        if (!std::isfinite(offset) || !std::isfinite(timeScale) || timeScale <= 0.0) {
            result.error = QStringLiteral("Synchronization state is invalid."); return result;
        }
        const QJsonObject analysis = project.value(QStringLiteral("analysis")).toObject();
        for (const QJsonValue &value : analysis.value(QStringLiteral("channels")).toArray()) {
            if (!value.isString() || value.toString().size() > ProjectLimits::maximumStringCharacters) {
                result.error = QStringLiteral("Analysis channels are malformed."); return result;
            }
            result.analysisChannels.append(value.toString());
        }
        result.success = true;
        result.project = project;
        result.widgets = project.value(QStringLiteral("scene")).toObject().value(QStringLiteral("widgets")).toArray();
        result.sync = {offset, timeScale};
        result.videoReference = ProjectSourceReferenceCodec::fromProject(editor, QStringLiteral("video"), QStringLiteral("videoPath"));
        result.vboReference = ProjectSourceReferenceCodec::fromProject(editor, QStringLiteral("telemetry"), QStringLiteral("vboPath"));
        result.resolvedVideoPath = ProjectSourceReferenceCodec::resolve(result.videoReference, projectPath);
        result.resolvedVboPath = ProjectSourceReferenceCodec::resolve(result.vboReference, projectPath);
        return result;
    }));
    return true;
}

bool DocumentController::beginProjectLoad(
    QString projectPath, const QJsonObject &project, const bool recovered,
    const quint64 recoveredRevision, const quint64 recoveredLastSavedRevision,
    QString recoveredDocumentId, const bool runSelection)
{
    QString validationError;
    if (!ProjectLimits::validateProject(project, &validationError)) {
        AppLog::error(QStringLiteral("Project load failed: %1").arg(validationError));
        m_host.showStatus(QStringLiteral("Project error: %1").arg(validationError));
        return false;
    }
    const QJsonObject scene = project.value("scene").toObject();
    if (!m_host.acceptsEditorProject(project, projectPath)) return false;
    const QJsonObject editor = EventProjectCodec::editorProjection(project);
    const QJsonObject sync = editor.value("sync").toObject();
    const double offset = sync.value("offset").toDouble();
    const double timeScale = sync.value("timeScale").toDouble(1.0);
    if (!std::isfinite(offset) || !std::isfinite(timeScale) || timeScale <= 0.0) {
        AppLog::error(QStringLiteral("Project load failed: invalid synchronization state: %1")
                          .arg(projectPath));
        m_host.showStatus("Project error: synchronization state is invalid.");
        return false;
    }
    const QJsonObject analysis = project.value("analysis").toObject();
    QStringList channels;
    if (analysis.value("channels").isArray()) {
        for (const QJsonValue &value : analysis.value("channels").toArray()) {
            channels.append(value.toString());
        }
    }
    const quint64 generation = m_host.beginSourceGeneration(runSelection);
    ProjectLoadResult result;
    result.success = true;
    result.projectPath = std::move(projectPath);
    result.project = project;
    result.widgets = scene.value(QStringLiteral("widgets")).toArray();
    result.analysisChannels = channels;
    result.sync = {offset, timeScale};
    result.generation = generation;
    result.recovered = recovered;
    result.runSelection = runSelection;
    result.recoveredRevision = recoveredRevision;
    result.recoveredLastSavedRevision = recoveredLastSavedRevision;
    result.recoveredDocumentId = std::move(recoveredDocumentId);
    result.videoReference = ProjectSourceReferenceCodec::fromProject(
        editor, QStringLiteral("video"), QStringLiteral("videoPath"));
    result.vboReference = ProjectSourceReferenceCodec::fromProject(
        editor, QStringLiteral("telemetry"), QStringLiteral("vboPath"));
    result.resolvedVideoPath = ProjectSourceReferenceCodec::resolve(
        result.videoReference, result.projectPath);
    result.resolvedVboPath = ProjectSourceReferenceCodec::resolve(
        result.vboReference, result.projectPath);

    setProjectLoadState(true, QStringLiteral("Applying project"));
    if (!commitProjectLoad(result)) {
        return false;
    }

    m_host.startEditorSources(result);
    return true;
}

void DocumentController::setProjectLoadState(bool loading, QString stage, QString error)
{
    if (m_projectLoading == loading && m_projectLoadStage == stage && m_projectLoadError == error) {
        return;
    }
    m_projectLoading = loading;
    m_projectLoadStage = std::move(stage);
    m_projectLoadError = std::move(error);
    emit projectLoadChanged();
}

bool DocumentController::commitProjectLoad(const ProjectLoadResult &result)
{
    const QScopedValueRollback suppressDirty(m_suppressDirtyTracking, true);
    setProjectLoadState(true, QStringLiteral("Applying project"));
    QString sceneError;
    if (!m_host.applyEditorScene(result, &sceneError)) {
        setProjectLoadState(false, {}, sceneError);
        m_host.showStatus(QStringLiteral("Project could not be opened: %1").arg(sceneError));
        return false;
    }
    m_projectTemplate = result.project;
    m_host.applyEditorProject(result);
    if (!result.projectPath.isEmpty()) {
        m_settings.setValue("project/path", result.projectPath);
    }
    if (result.runSelection) {
        // Selecting a run changes the same document, never its saved identity or clean revision.
        m_documentState.markChanged();
    } else if (result.recovered) {
        m_documentId = result.recoveredDocumentId.isEmpty()
            ? QUuid::createUuid().toString(QUuid::WithoutBraces)
            : result.recoveredDocumentId;
        m_documentState.restoreUnsaved(
            result.projectPath, result.recoveredRevision, result.recoveredLastSavedRevision);
    } else {
        SavedDocumentMetadata metadata;
        if (parseSavedDocumentMetadata(result.project, &metadata)) {
            m_documentId = metadata.id;
            m_documentState.reset(result.projectPath, metadata.revision);
        } else {
            m_documentId = QUuid::createUuid().toString(QUuid::WithoutBraces);
            m_documentState.reset(result.projectPath);
        }
    }
    m_host.announceEditorProject();
    emit documentStateChanged();
    setProjectLoadState(false);
    if (result.runSelection) {
        scheduleRecoveryWrite();
        m_host.showStatus(QStringLiteral("Run selected. Loading its available sources."));
    } else if (result.recovered) {
        AppLog::info(QStringLiteral("Recovery accepted"));
        m_host.showStatus(QStringLiteral("Recovered unsaved changes. Save to keep them."));
    } else {
        AppLog::info(QStringLiteral("Project load succeeded: %1").arg(result.projectPath));
        AppLog::info(QStringLiteral("Saved project restored from disk: %1").arg(result.projectPath));
        m_host.showStatus(QStringLiteral("Project opened: %1").arg(QFileInfo(result.projectPath).fileName()));
    }
    return true;
}

QJsonObject DocumentController::currentProjectObject(
    const QString &projectPath, const std::optional<quint64> savedRevision) const
{
    const bool eventProject = EventProjectCodec::isEvent(m_projectTemplate);
    QJsonObject project = EventProjectCodec::editorProjection(m_projectTemplate);
    project.insert("version", 2);
    project.remove(QStringLiteral("videoPath"));
    project.remove(QStringLiteral("vboPath"));
    const QString targetProjectPath = projectPath.isEmpty()
        ? m_documentState.projectPath() : projectPath;
    project = m_host.withEditorState(project, m_documentState.projectPath(), targetProjectPath);
    QJsonObject documentState = project.value(QStringLiteral("documentState")).toObject();
    documentState.insert(QStringLiteral("id"), m_documentId);
    documentState.insert(QStringLiteral("savedRevision"), QString::number(
        savedRevision.value_or(m_documentState.lastSavedRevision())));
    project.insert(QStringLiteral("documentState"), documentState);
    if (!project.contains("mapSettings")) {
        project.insert("mapSettings", QJsonObject{{"providerId", "none"}});
    }
    if (!project.contains("exportSettings")) {
        project.insert("exportSettings", QJsonObject{{"quality", "high"}});
    }
    project = eventProject
        ? EventProjectCodec::withEditorState(m_projectTemplate, project, m_documentState.projectPath(), targetProjectPath,
            m_host.loadedTelemetryRevision())
        : project;
    return savedRevision ? m_host.withVerifiedAnalysis(project) : project;
}

bool DocumentController::saveProject(const QUrl &url)
{
    QString path = url.toLocalFile();
    if (!path.endsWith(".fetproject", Qt::CaseInsensitive)) {
        path.append(".fetproject");
    }
    AppLog::info(QStringLiteral("Project save requested: %1").arg(path));
    const QJsonObject project = currentProjectObject(path, m_documentState.revision());
    QString validationError;
    if (!ProjectLimits::validateProject(project, &validationError)) {
        AppLog::error(QStringLiteral("Project save rejected: %1").arg(validationError));
        m_host.showStatus(QStringLiteral("Project save error: %1").arg(validationError));
        if (m_documentState.pendingAction() != ProjectDocumentState::DestructiveAction::None) {
            emit destructiveActionChanged();
        }
        return false;
    }
    const QByteArray payload = QJsonDocument(project).toJson(QJsonDocument::Indented);
    if (payload.size() > ProjectLimits::projectBytes) {
        const QString sizeError = QStringLiteral("Project is %1 bytes; the limit is %2 bytes.")
                                      .arg(payload.size())
                                      .arg(ProjectLimits::projectBytes);
        AppLog::error(QStringLiteral("Project save rejected: %1").arg(sizeError));
        m_host.showStatus(QStringLiteral("Project save error: %1").arg(sizeError));
        if (m_documentState.pendingAction() != ProjectDocumentState::DestructiveAction::None) {
            emit destructiveActionChanged();
        }
        return false;
    }
    const ProjectWriter::Result writeResult = m_projectWriter.write(path, payload);
    if (!writeResult.success) {
        AppLog::error(QStringLiteral("Project save failed: %1: %2").arg(path, writeResult.error));
        m_host.showStatus(QStringLiteral("Project save error: %1").arg(writeResult.error));
        if (m_documentState.pendingAction() != ProjectDocumentState::DestructiveAction::None) {
            emit destructiveActionChanged();
        }
        return false;
    }
    m_projectTemplate = project;
    m_host.editorProjectSaved(project);
    m_settings.setValue("project/path", path);
    m_settings.sync();
    m_documentState.markSaved(path);
    m_recoveryTimer.stop();
    clearRecovery(QStringLiteral("successful save"));
    emit documentStateChanged();
    AppLog::info(QStringLiteral("Project save succeeded: %1").arg(path));
    m_host.showStatus(QStringLiteral("Project saved: %1").arg(QFileInfo(path).fileName()));
    if (m_documentState.pendingAction() != ProjectDocumentState::DestructiveAction::None) {
        performPendingDestructiveAction();
    }
    return true;
}

void DocumentController::markPersistentChange()
{
    if (m_suppressDirtyTracking) {
        return;
    }
    m_documentState.markChanged();
    emit documentStateChanged();
    scheduleRecoveryWrite();
}

void DocumentController::scheduleRecoveryWrite()
{
    if (!m_recoveryPending && m_documentState.dirty()) {
        // A normal edit retries immediately; a failed write below schedules a
        // bounded backoff so a broken filesystem cannot cause a busy loop.
        m_recoveryTimer.start(RecoveryWriteDelayMs);
    }
}

void DocumentController::writeRecoverySnapshot()
{
    if (m_recoveryPending || !m_documentState.dirty()) {
        return;
    }
    const ProjectRecoverySnapshot snapshot{
        m_documentState.projectPath(),
        m_documentId,
        m_documentState.revision(),
        m_documentState.lastSavedRevision(),
        QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs),
        currentProjectObject(),
        true,
    };
    QString error;
    if (!m_recoveryStore.write(snapshot, &error)) {
        AppLog::error(QStringLiteral("Recovery snapshot write failed: %1").arg(error));
        const bool changed = !m_recoveryDegraded || m_recoveryError != error;
        m_recoveryDegraded = true;
        m_recoveryError = error;
        if (changed) emit recoveryChanged();
        m_recoveryTimer.start(RecoveryRetryDelayMs);
        return;
    }
    if (m_recoveryDegraded) {
        m_recoveryDegraded = false;
        m_recoveryError.clear();
        emit recoveryChanged();
    }
    AppLog::info(QStringLiteral("Recovery snapshot written"));
}

bool DocumentController::clearRecovery(const QString &reason)
{
    const bool existed = m_recoveryStore.exists();
    QString error;
    if (!m_recoveryStore.clear(&error)) {
        AppLog::error(QStringLiteral("Recovery clear failed: %1").arg(error));
        return false;
    }
    if (existed) {
        AppLog::info(QStringLiteral("Recovery cleared after %1").arg(reason));
    }
    return true;
}

void DocumentController::clearDiscardTombstoneAfterRecoveryCleanup()
{
    QString error;
    if (!m_recoveryStore.clearDiscardTombstone(&error)) {
        AppLog::error(QStringLiteral("Recovery discard tombstone cleanup failed: %1").arg(error));
    }
}

bool DocumentController::discardRecovery(const ProjectRecoverySnapshot &snapshot, const QString &reason)
{
    if (!m_recoveryStore.exists()) return true;
    if (!snapshot.hasLogicalMetadata) return clearRecovery(reason);

    QString tombstoneError;
    const ProjectRecoveryDiscardTombstone tombstone{
        snapshot.documentId, snapshot.revision,
    };
    if (m_recoveryStore.writeDiscardTombstone(tombstone, &tombstoneError)) {
        AppLog::info(QStringLiteral("Recovery discard intent persisted through revision %1")
                         .arg(snapshot.revision));
        if (!clearRecovery(reason)) {
            AppLog::warn(QStringLiteral("Recovery cleanup deferred after durable discard intent"));
            return true;
        }
        clearDiscardTombstoneAfterRecoveryCleanup();
        return true;
    }

    AppLog::error(QStringLiteral("Recovery discard intent persistence failed: %1").arg(tombstoneError));
    return clearRecovery(reason);
}

void DocumentController::retireLegacyDocumentSettings()
{
    m_settings.remove(QStringLiteral("editor/widgets"));
    m_settings.remove(QStringLiteral("sync/offset"));
    m_settings.remove(QStringLiteral("sync/timeScale"));
    m_settings.remove(QStringLiteral("sources/video"));
    m_settings.remove(QStringLiteral("sources/vbo"));
    m_settings.remove(QStringLiteral("analysis/channels"));
    m_settings.remove(QStringLiteral("analysis/visible"));
    m_settings.sync();
}

void DocumentController::restoreStartupState()
{
    ProjectRecoverySnapshot snapshot;
    QString error;
    if (!m_recoveryStore.exists()
        && QFileInfo(m_recoveryStore.discardTombstonePath()).exists()) {
        clearDiscardTombstoneAfterRecoveryCleanup();
    }
    if (m_recoveryStore.exists() && m_recoveryStore.load(&snapshot, &error)) {
        bool discardedByTombstone = false;
        if (snapshot.hasLogicalMetadata) {
            ProjectRecoveryDiscardTombstone tombstone;
            QString tombstoneError;
            if (m_recoveryStore.loadDiscardTombstone(&tombstone, &tombstoneError)) {
                if (tombstone.documentId == snapshot.documentId
                    && snapshot.revision <= tombstone.discardedThroughRevision) {
                    AppLog::info(QStringLiteral("Recovery snapshot suppressed by durable discard intent"));
                    discardedByTombstone = true;
                    if (clearRecovery(QStringLiteral("startup discarded recovery"))) {
                        clearDiscardTombstoneAfterRecoveryCleanup();
                    } else {
                        AppLog::warn(QStringLiteral("Discarded recovery cleanup remains pending"));
                    }
                }
            } else if (QFileInfo(m_recoveryStore.discardTombstonePath()).exists()) {
                AppLog::error(QStringLiteral("Recovery discard tombstone ignored: %1").arg(tombstoneError));
            }
        }
        if (!discardedByTombstone) {
            const RecoveryValidity validity = recoveryValidity(
                snapshot, m_settings.value(QStringLiteral("project/path")).toString());
            if (validity == RecoveryValidity::Stale) {
                AppLog::info(QStringLiteral("Stale recovery snapshot ignored"));
                clearRecovery(QStringLiteral("stale startup recovery"));
            } else if (validity == RecoveryValidity::Invalid) {
                AppLog::error(QStringLiteral("Recovery snapshot ignored: document identity does not match authority"));
            } else {
                m_pendingRecovery = snapshot;
                m_recoveryPending = true;
                m_documentState.reset();
                AppLog::info(QStringLiteral("Recovery detected"));
                m_host.showStatus(QStringLiteral("Unsaved changes are available for recovery."));
                return;
            }
        }
    }
    if (m_recoveryStore.exists() && !error.isEmpty()) {
        AppLog::error(QStringLiteral("Recovery snapshot ignored: %1").arg(error));
    }
    const QString projectPath = m_settings.value(QStringLiteral("project/path")).toString();
    if (projectPath.isEmpty()) {
        m_documentState.reset();
        return;
    }
    if (!QFileInfo(projectPath).isFile()) {
        m_settings.remove(QStringLiteral("project/path"));
        m_settings.sync();
        m_documentState.reset();
        m_host.showStatus(QStringLiteral("The previous project could not be found; a new project was started."));
        return;
    }
    m_documentState.reset();
    performOpenProject(QUrl::fromLocalFile(projectPath));
}

void DocumentController::beginDestructiveAction(
    const ProjectDocumentState::DestructiveAction action, const QUrl &openUrl)
{
    if (action == ProjectDocumentState::DestructiveAction::OpenProject) {
        if (!openUrl.isLocalFile() || openUrl.toLocalFile().isEmpty()) {
            m_host.showStatus("Project error: choose a local .fetproject file.");
            return;
        }
        m_pendingOpenProject = openUrl;
    }
    const auto result = m_documentState.request(action);
    emit destructiveActionChanged();
    if (result == ProjectDocumentState::RequestResult::ContinueImmediately) {
        performPendingDestructiveAction();
    } else {
        AppLog::info(QStringLiteral("Dirty project decision requested for: %1")
                         .arg(ProjectDocumentState::actionName(action)));
    }
}

void DocumentController::performPendingDestructiveAction()
{
    const ProjectDocumentState::DestructiveAction action = m_documentState.takePendingAction();
    const QUrl openUrl = m_pendingOpenProject;
    m_pendingOpenProject = QUrl();
    emit destructiveActionChanged();
    m_recoveryTimer.stop();
    const ProjectRecoverySnapshot snapshot{
        m_documentState.projectPath(), m_documentId, m_documentState.revision(),
        m_documentState.lastSavedRevision(), {}, {}, true,
    };
    if (action != ProjectDocumentState::DestructiveAction::None
        && !discardRecovery(snapshot, QStringLiteral("discarded document state"))) {
        m_host.showStatus(QStringLiteral("Could not discard recovery data; action cancelled."));
        return;
    }
    switch (action) {
    case ProjectDocumentState::DestructiveAction::NewProject:
        performClearProject();
        break;
    case ProjectDocumentState::DestructiveAction::OpenProject:
        performOpenProject(openUrl);
        break;
    case ProjectDocumentState::DestructiveAction::Quit:
        AppLog::info(QStringLiteral("Quit approved"));
        emit quitApproved();
        break;
    case ProjectDocumentState::DestructiveAction::None:
        break;
    }
}

void DocumentController::commitAnalysisProject(const QJsonObject &project)
{
    m_projectTemplate = project;
    markPersistentChange();
}

bool DocumentController::isEventDocument() const
{
    return EventProjectCodec::isEvent(m_projectTemplate);
}

bool DocumentController::destructiveActionPending() const
{
    return m_documentState.pendingAction() != ProjectDocumentState::DestructiveAction::None;
}

} // namespace FlappedEar
