#pragma once

#include <QJsonObject>
#include <QString>
#include <QVariantList>
#include <QtGlobal>

namespace FlappedEar {

// KAN-124: the analysis side's only view of the project document. The
// document owner (today AppController) implements it; AnalysisController reads
// the event project, commits its validated edits (lap exclusions, track
// segments, comparison choices) and checks whether an edit is allowed now,
// without knowing about sources, recovery files, export or the overlay.
class AnalysisDocument {
public:
    virtual ~AnalysisDocument() = default;
    // The current project, including unsaved editor state.
    [[nodiscard]] virtual QJsonObject analysisProject() const = 0;
    // Replaces the project with an edit analysis has already validated, and
    // records it as a user change (dirty state, recovery).
    virtual void commitAnalysisProject(const QJsonObject &project) = 0;
    [[nodiscard]] virtual bool isEventDocument() const = 0;
    [[nodiscard]] virtual QString documentProjectPath() const = 0;
    [[nodiscard]] virtual QString documentIdentity() const = 0;
    [[nodiscard]] virtual quint64 documentRevision() const = 0;
    // Changes whenever the document's sources are replaced; analysis results
    // for an older generation are stale.
    [[nodiscard]] virtual quint64 sourceGeneration() const = 0;
    [[nodiscard]] virtual bool projectLoading() const = 0;
    [[nodiscard]] virtual bool documentBusy() const = 0;
    [[nodiscard]] virtual bool recoveryPending() const = 0;
    [[nodiscard]] virtual bool batchImportPending() const = 0;
    [[nodiscard]] virtual bool destructiveActionPending() const = 0;
    [[nodiscard]] virtual bool dirty() const = 0;
    // True while the document applies its own state (open, undo), when
    // analysis must not record a change of its own.
    [[nodiscard]] virtual bool dirtyTrackingSuppressed() const = 0;
    [[nodiscard]] virtual QString activeRunId() const = 0;
    [[nodiscard]] virtual QVariantList eventRuns() const = 0;
};

} // namespace FlappedEar
