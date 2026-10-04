#pragma once

#include "project/ProjectSourceReference.h"

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>

namespace FlappedEar {

// v3 owns sources and synchronization per run. The single-run editor projection
// is ephemeral: it must never be serialized alongside the authoritative event.
class EventProjectCodec final {
public:
    static constexpr qsizetype maximumRuns = 64;
    static constexpr qsizetype maximumSourcesPerRun = 8;
    static constexpr qsizetype maximumTelemetrySources = 128;

    [[nodiscard]] static bool isEvent(const QJsonObject &project);
    // Called by ProjectLimits after the common JSON resource checks.
    [[nodiscard]] static bool validate(const QJsonObject &project, QString *error);
    // These adapters accept validated documents; they never open telemetry/media.
    [[nodiscard]] static QJsonObject editorProjection(const QJsonObject &project);
    [[nodiscard]] static QJsonObject withEditorState(
        const QJsonObject &eventProject, QJsonObject editorProject,
        const QString &previousProjectPath, const QString &targetProjectPath,
        const QByteArray &activeSourceRevision = {});
    [[nodiscard]] static QJsonObject referenceForSave(
        const ProjectSourceReference &reference, const QString &previousProjectPath,
        const QString &targetProjectPath);
    // Unknown fields are explicit for new imports; absent legacy configuration
    // reads as unknown without modifying the document just by opening it.
    [[nodiscard]] static QJsonObject unknownTrackConfiguration(
        const QString &sourceId, const QJsonObject &fingerprint, const QString &gateRevision = {});
    [[nodiscard]] static QJsonObject trackConfiguration(const QJsonObject &run);
    // Opaque dependency identity, not a lap reference or compatibility decision.
    // Excludes names, notes, video/sync and portable source paths.
    [[nodiscard]] static QByteArray lapDerivationKey(const QJsonObject &run);
    // Full content identity, including legacy imports whose provenance is still
    // bound to the current fingerprint. Never infer identity from a path.
    [[nodiscard]] static QByteArray sourceContentRevision(const QJsonObject &source);
    // The identity of a run's primary recording read from the document alone:
    // eventId, runId, sourceId, reference, expectedRevision and derivationKey.
    // Empty when the project is not an event or the run has no primary
    // recording. The editor's lap navigation binds to it without analysis
    // state (KAN-166).
    [[nodiscard]] static QJsonObject primaryTelemetryBinding(const QJsonObject &project, const QString &runId);
    // One lap-derivation source per run with a primary recording, in run
    // order: the binding above plus the run name, saved track inference and
    // configuration, and an approved fusion while both recordings are still
    // the content it was reviewed against (KAN-103). deriveOutingLaps reads
    // these; the editor's automatic best lap needs no analysis state (KAN-185).
    [[nodiscard]] static QJsonArray outingLapSources(const QJsonObject &project);
    // All referenced path candidates, including inactive sources, for export protection.
    [[nodiscard]] static QStringList referencedPaths(const QJsonObject &project, const QString &projectPath);
};

} // namespace FlappedEar
