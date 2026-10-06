#pragma once

#include "project/ProjectSourceReference.h"
#include "telemetry/TelemetrySession.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QtGlobal>

namespace FlappedEar {

// A project read and validated for opening (from disk, recovery, an import or
// a run selection). The editor fields describe the active run's editor state;
// an application without the overlay editor ignores them.
struct ProjectLoadResult {
    bool success = false;
    bool cancelled = false;
    QString projectPath;
    QJsonObject project;
    QJsonArray widgets;
    SyncTransform sync;
    ProjectSourceReference videoReference;
    ProjectSourceReference vboReference;
    QString resolvedVideoPath;
    QString resolvedVboPath;
    QString error;
    quint64 generation = 0;
    quint64 documentRevisionAtStart = 0;
    bool recovered = false;
    bool runSelection = false;
    quint64 recoveredRevision = 0;
    quint64 recoveredLastSavedRevision = 0;
    QString recoveredDocumentId;
};

// KAN-124: what the application adds to the project document. The document
// owns the saved project, its identity and revisions, recovery, open/save and
// import. The host (today AppController, later each app) owns the sources it
// loads and any editor state stored in the project (the active run's video,
// synchronisation, overlay scene and chart channels), and says whether the
// document is busy. FlappedEar Telemetry hosts a document with no editor.
class DocumentHost {
public:
    virtual ~DocumentHost() = default;
    // Cancels and forgets work on the current sources before new ones load;
    // `preserveAnalysis` keeps day-analysis caches (a single source replaced
    // or a run selected). Returns the new source generation.
    virtual quint64 beginSourceGeneration(bool preserveAnalysis) = 0;
    // Whether the project's editor state can be applied; reports why not.
    [[nodiscard]] virtual bool acceptsEditorProject(const QJsonObject &project, const QString &projectPath) = 0;
    // Applies the editor scene first; false (with `error`) refuses the load.
    [[nodiscard]] virtual bool applyEditorScene(const ProjectLoadResult &result, QString *error) = 0;
    // Applies the remaining editor state of a committed load, then (after the
    // document state is updated) announces it.
    virtual void applyEditorProject(const ProjectLoadResult &result) = 0;
    virtual void announceEditorProject() = 0;
    virtual void startEditorSources(const ProjectLoadResult &result) = 0;
    // A project open failed after beginSourceGeneration: restart the current
    // document's source loads that it interrupted (KAN-195).
    virtual void resumeInterruptedSources() = 0;
    // A new, empty document.
    virtual void clearEditor() = 0;
    // The editor state written into `projection` (the active run's editor view).
    [[nodiscard]] virtual QJsonObject withEditorState(QJsonObject projection, const QString &documentPath,
        const QString &targetPath) const = 0;
    // The loaded telemetry's verified content revision, if any.
    [[nodiscard]] virtual QByteArray loadedTelemetryRevision() const = 0;
    // The project with analysis results that belong in a saved document.
    [[nodiscard]] virtual QJsonObject withVerifiedAnalysis(const QJsonObject &project) const = 0;
    virtual void editorProjectSaved(const QJsonObject &project) = 0;
    // A long operation (export) that document changes must not interleave with.
    [[nodiscard]] virtual bool documentBusy() const = 0;
    virtual void showStatus(const QString &status) = 0;
    // An import committed a day to analyse.
    virtual void revealAnalysis() = 0;
};

} // namespace FlappedEar
