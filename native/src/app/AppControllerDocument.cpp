#include "app/AppController.h"
#include "project/VideoChapters.h"
#include "app/AppLog.h"
#include "project/EventProjectCodec.h"

#include <QJsonArray>

// KAN-124: AppController as DocumentHost -- the active run's editor state
// (sources, synchronisation, overlay scene, chart channels) that the project
// document stores, and the analysis results a saved document carries.

namespace FlappedEar {

void AppController::initializeDocument()
{
    // Document handlers keep their place among this controller's own handlers:
    // import invalidation first, as before.
    connect(this, &AppController::sourceLoadStateChanged, &m_document, &DocumentController::sourceLoadStateChanged);
    connect(&m_document, &DocumentController::documentStateChanged, this, &AppController::documentStateChanged);
    // The editor's laps follow the saved lap exclusions as the document changes.
    connect(&m_document, &DocumentController::documentStateChanged, this, &AppController::applyActiveLapExclusions);
    connect(&m_document, &DocumentController::destructiveActionChanged, this, &AppController::destructiveActionChanged);
    connect(&m_document, &DocumentController::projectLoadChanged, this, &AppController::projectLoadChanged);
    connect(&m_document, &DocumentController::recoveryChanged, this, &AppController::recoveryChanged);
    connect(&m_document, &DocumentController::saveAsRequested, this, &AppController::saveAsRequested);
    connect(&m_document, &DocumentController::quitApproved, this, &AppController::quitApproved);
    // KAN-185: keep a requested best lap current through edits and reloads.
    connect(&m_document, &DocumentController::documentStateChanged, &m_bestLapFinder, &BestLapFinder::documentChanged);
    connect(&m_document, &DocumentController::projectLoadChanged, &m_bestLapFinder, &BestLapFinder::documentChanged);
    connect(&m_document, &DocumentController::sourceLoadStateChanged, &m_bestLapFinder, &BestLapFinder::documentChanged);
    connect(&m_bestLapFinder, &BestLapFinder::resultChanged, this, &AppController::dayBestLapChanged);
}

bool AppController::acceptsEditorProject(const QJsonObject &project, const QString &projectPath)
{
    const QJsonObject scene = project.value("scene").toObject();
    WidgetModel candidateWidgets;
    if (!candidateWidgets.fromJson(scene.value("widgets").toArray())) {
        AppLog::error(QStringLiteral("Project load failed: unsupported or invalid file: %1")
                          .arg(projectPath));
        setStatus("Project error: unsupported or invalid .fetproject file.");
        return false;
    }
    return true;
}

bool AppController::applyEditorScene(const ProjectLoadResult &result, QString *error)
{
    m_sceneNotice.clear();
    if (m_widgetModel.fromJson(result.widgets)) {
        const int retired = m_widgetModel.retiredWidgetsDropped();
        if (retired > 0) {
            AppLog::warn(QStringLiteral("Project load left out %1 widget(s) of retired types: %2")
                             .arg(retired).arg(result.projectPath));
            m_sceneNotice = retired == 1
                ? QStringLiteral("1 widget of a removed type was left out.")
                : QStringLiteral("%1 widgets of removed types were left out.").arg(retired);
        }
        return true;
    }
    AppLog::error(QStringLiteral("Project load failed while applying widget scene: %1")
                      .arg(result.projectPath));
    *error = QStringLiteral("widget scene could not be applied.");
    return false;
}

void AppController::applyEditorProject(const ProjectLoadResult &result)
{
    m_videoReference = result.videoReference;
    m_vboReference = result.vboReference;
    m_videoSource = QUrl();
    // KAN-105: the saved chapters, not yet verified, so a save before (or
    // without) loading the video keeps them; the video probe refreshes them.
    m_videoChapterStates.clear();
    const auto chapters = VideoChaptersCodec::read(
        EventProjectCodec::editorProjection(result.project).value("sources").toObject().value("video").toObject());
    QVector<TimelineChapter> timelineChapters;
    for (const auto &chapter : chapters) {
        m_videoChapterStates.append({chapter.reference, {}, chapter.durationSeconds, false, QStringLiteral("loading")});
        timelineChapters.append({{}, chapter.durationSeconds, false});
    }
    m_videoTimeline = MediaTimeline::fromChapters(timelineChapters);
    m_videoChapterIndex = 0;
    m_exportSourceInfo = {};
    m_videoLoadState = result.videoReference.isEmpty() ? QStringLiteral("idle")
        : result.resolvedVideoPath.isEmpty() ? QStringLiteral("missing")
                                             : QStringLiteral("loading");
    m_telemetryPath.clear();
    m_session.reset();
    m_lapSession = {};
    m_loadedSourceRevision.clear();
    m_trackGeometry = {};
    m_trackPoints = trackPointsFor(m_trackGeometry);
    m_vboLoadState = result.vboReference.isEmpty() ? QStringLiteral("idle")
        : result.resolvedVboPath.isEmpty() ? QStringLiteral("missing")
                                           : QStringLiteral("loading");
    m_previewRenderContext.setSession(nullptr);
    m_previewRenderContext.setTrackGeometry(nullptr);
    m_previewRenderContext.setLapSession({});
    m_sync = result.sync;
    m_previewRenderContext.setSyncTransform(m_sync);
    m_playbackTime = 0.0;
    m_syncCandidate.clear();
    // A .fetproject stores a scene, not template provenance. Retain the picker preference,
    // but never let a newly opened scene overwrite a visible custom template in place.
    if (!result.runSelection) clearActiveTemplate();
}

void AppController::announceEditorProject()
{
    emit videoSourceChanged();
    emit previewMetadataChanged();
    emit telemetryChanged();
    emit lapNavigationChanged();
    emit exportChanged();
    emit playbackTimeChanged();
    emit syncChanged();
    emit syncCandidateChanged();
    emit liveValuesChanged();
    emit sourceLoadStateChanged();
}

void AppController::clearEditor()
{
    m_videoLoadState = QStringLiteral("idle");
    m_vboLoadState = QStringLiteral("idle");
    m_pendingVideoPath.clear();
    m_pendingVboPath.clear();
    m_videoSource = QUrl();
    m_videoReference = {};
    m_videoChapterStates.clear();
    m_videoTimeline = {};
    m_videoChapterIndex = 0;
    m_exportSourceInfo = {};
    m_exportMetrics.clear();
    m_exportDiagnosticLog.clear();
    m_exportProgressInfo.clear();
    m_exportProgressVisible = false;
    m_telemetryPath.clear();
    m_vboReference = {};
    m_session.reset();
    m_lapSession = {};
    m_loadedSourceRevision.clear();
    m_trackGeometry = {};
    m_previewRenderContext.setSession(nullptr);
    m_previewRenderContext.setTrackGeometry(nullptr);
    m_previewRenderContext.setLapSession({});
    m_trackPoints.clear();
    m_playbackTime = 0.0;
    m_sync = {};
    m_syncCandidate.clear();
    clearActiveTemplate();
    m_widgetModel.resetDefaults();
    emit videoSourceChanged();
    emit previewMetadataChanged();
    emit telemetryChanged();
    emit lapNavigationChanged();
    emit playbackTimeChanged();
    emit syncChanged();
    emit syncCandidateChanged();
    emit sourceLoadStateChanged();
    emit liveValuesChanged();
}

QJsonObject AppController::withEditorState(QJsonObject project, const QString &documentPath,
    const QString &targetPath) const
{
    const bool eventProject = EventProjectCodec::isEvent(m_document.storedProject());
    QJsonObject sources = project.value(QStringLiteral("sources")).toObject();
    const QJsonObject video = eventProject
        ? EventProjectCodec::referenceForSave(m_videoReference, documentPath, targetPath)
        : ProjectSourceReferenceCodec::toJson(m_videoReference, targetPath);
    const QJsonObject telemetry = eventProject
        ? EventProjectCodec::referenceForSave(m_vboReference, documentPath, targetPath)
        : ProjectSourceReferenceCodec::toJson(m_vboReference, targetPath);
    const auto overlaySource = [&sources](const QString &key, const QJsonObject &known) {
        QJsonObject source = sources.value(key).toObject();
        source.remove(QStringLiteral("relativePath"));
        source.remove(QStringLiteral("absolutePath"));
        source.remove(QStringLiteral("fingerprint"));
        for (auto it = known.begin(); it != known.end(); ++it) source.insert(it.key(), it.value());
        if (source.isEmpty()) sources.remove(key);
        else sources.insert(key, source);
    };
    overlaySource(QStringLiteral("video"), video);
    overlaySource(QStringLiteral("telemetry"), telemetry);
    // KAN-105: the chapters, written by the same rules as the video itself.
    if (sources.contains(QStringLiteral("video"))) {
        QJsonObject videoSource = sources.value(QStringLiteral("video")).toObject();
        if (m_videoChapterStates.size() > 1) {
            QVector<VideoChapterReference> chapters;
            for (const auto &chapter : m_videoChapterStates) chapters.append({chapter.reference, chapter.durationSeconds});
            chapters.first().reference = m_videoReference;
            videoSource.insert(QStringLiteral("chapters"), VideoChaptersCodec::write(chapters, [&](const ProjectSourceReference &reference) {
                return eventProject ? EventProjectCodec::referenceForSave(reference, documentPath, targetPath)
                                    : ProjectSourceReferenceCodec::toJson(reference, targetPath);
            }));
        } else {
            videoSource.remove(QStringLiteral("chapters"));
        }
        sources.insert(QStringLiteral("video"), videoSource);
    }
    project.insert(QStringLiteral("sources"), sources);
    QJsonObject sync = project.value("sync").toObject();
    sync.insert("offset", m_sync.offset);
    sync.insert("timeScale", m_sync.timeScale);
    project.insert("sync", sync);
    QJsonObject scene = project.value("scene").toObject();
    scene.insert("widgets", m_widgetModel.toJson());
    project.insert("scene", scene);
    // analysis.channels belongs to FlappedEar Telemetry: Overlays saves it as
    // loaded (KAN-166). The old window-visibility flag is not document state.
    if (project.contains(QStringLiteral("analysis"))) {
        QJsonObject analysis = project.value("analysis").toObject();
        analysis.remove(QStringLiteral("visible"));
        project.insert("analysis", analysis);
    }
    return project;
}

QByteArray AppController::loadedTelemetryRevision() const
{
    return m_vboLoadState == "ready" ? m_loadedSourceRevision : QByteArray{};
}

QJsonObject AppController::withVerifiedAnalysis(const QJsonObject &project) const
{
    // Overlays derives no analysis results to add (KAN-166 step 5): saved
    // inference and decisions pass through as loaded.
    return project;
}

void AppController::editorProjectSaved(const QJsonObject &project)
{
    if (!EventProjectCodec::isEvent(project)) return;
    const QJsonObject editor = EventProjectCodec::editorProjection(project);
    m_videoReference = ProjectSourceReferenceCodec::fromProject(editor, QStringLiteral("video"), QStringLiteral("videoPath"));
    m_vboReference = ProjectSourceReferenceCodec::fromProject(editor, QStringLiteral("telemetry"), QStringLiteral("vboPath"));
    const auto chapters = VideoChaptersCodec::read(editor.value("sources").toObject().value("video").toObject());
    if (chapters.size() == m_videoChapterStates.size())
        for (qsizetype index = 0; index < chapters.size(); ++index) m_videoChapterStates[index].reference = chapters[index].reference;
}

} // namespace FlappedEar
