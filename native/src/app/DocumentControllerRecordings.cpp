#include "app/DocumentController.h"
#include "project/EventProjectCodec.h"
#include "project/ProjectLimits.h"
#include "project/ProjectSourceReference.h"
#include "telemetry/LapTiming.h"
#include "telemetry/OutingLaps.h"
#include "telemetry/TelemetryImportPlan.h"
#include "telemetry/TelemetrySource.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QUuid>
#include <QtConcurrent>

// KAN-90: a run's recordings. An alternative recording is attached only after
// its match evidence against the run's primary has been reviewed, and the
// primary is changed only explicitly -- there is no automatic format
// preference. Alternatives are kept side by side; nothing is fused.

namespace FlappedEar {
namespace {

ProjectSourceReference referenceOf(const QJsonObject &json)
{
    return {json.value("relativePath").toString(), json.value("absolutePath").toString(),
            json.value("fingerprint").toObject()};
}

QJsonObject runById(const QJsonObject &project, const QString &runId)
{
    for (const auto &value : project.value("event").toObject().value("runs").toArray())
        if (value.toObject().value("id").toString() == runId) return value.toObject();
    return {};
}

} // namespace

QVariantList DocumentController::runRecordings(const QString &runId) const
{
    QVariantList result;
    const auto run = runById(m_projectTemplate, runId);
    const auto primary = run.value("primaryTelemetrySourceId").toString();
    for (const auto &value : run.value("sources").toObject().value("telemetry").toArray()) {
        const auto source = value.toObject();
        const auto reference = referenceOf(source.value("reference").toObject());
        const auto path = ProjectSourceReferenceCodec::resolve(reference, m_documentState.projectPath());
        const auto name = QFileInfo(reference.absolutePath.isEmpty() ? reference.relativePath : reference.absolutePath).fileName();
        const auto format = source.value("importProvenance").toObject().value("format").toString();
        result.append(QVariantMap{{"sourceId", source.value("id").toString()}, {"name", name},
            {"format", format.isEmpty() ? QFileInfo(name).suffix().toUpper() : format.toUpper()},
            {"primary", source.value("id").toString() == primary}, {"available", !path.isEmpty()}});
    }
    return result;
}

bool DocumentController::recordingEditAllowed() const
{
    return EventProjectCodec::isEvent(m_projectTemplate) && !projectLoading() && !m_host.documentBusy()
        && !recoveryPending() && !m_batchPending && !m_recordingWatcher.isRunning()
        && m_documentState.pendingAction() == ProjectDocumentState::DestructiveAction::None
        && m_documentState.revision() != std::numeric_limits<quint64>::max();
}

void DocumentController::setRecordingReview(QVariantMap review)
{
    m_recordingReview = std::move(review);
    emit runRecordingsChanged();
}

bool DocumentController::attachRunRecording(const QString &runId, const QUrl &url)
{
    if (!recordingEditAllowed() || !url.isLocalFile()) return false;
    const auto run = runById(m_projectTemplate, runId);
    if (run.isEmpty()) return false;
    const auto sources = run.value("sources").toObject().value("telemetry").toArray();
    if (sources.size() >= EventProjectCodec::maximumSourcesPerRun) {
        setRecordingReview({{"state", "error"}, {"runId", runId},
            {"message", tr("A run can hold at most %1 recordings.").arg(EventProjectCodec::maximumSourcesPerRun)}});
        return false;
    }
    QString primaryPath;
    QSet<QByteArray> known;
    for (const auto &value : sources) {
        const auto source = value.toObject();
        known.insert(EventProjectCodec::sourceContentRevision(source));
        if (source.value("id") == run.value("primaryTelemetrySourceId"))
            primaryPath = ProjectSourceReferenceCodec::resolve(referenceOf(source.value("reference").toObject()),
                                                               m_documentState.projectPath());
    }
    if (primaryPath.isEmpty()) {
        setRecordingReview({{"state", "error"}, {"runId", runId},
            {"message", tr("The run's primary recording is missing. Relink it first, so the new recording can be compared with it.")}});
        return false;
    }
    m_recordingRunId = runId;
    m_recordingRevision = m_documentState.revision();
    m_recordingDocumentId = m_documentId;
    m_recordingCancellation = std::make_shared<std::atomic_bool>(false);
    const auto cancellation = m_recordingCancellation;
    setRecordingReview({{"state", "checking"}, {"runId", runId}, {"message", tr("Comparing the recording with this run…")}});
    const QString candidatePath = url.toLocalFile();
    m_recordingWatcher.setFuture(QtConcurrent::run([primaryPath, candidatePath, known, cancellation] {
        RecordingWork work;
        work.kind = RecordingWork::Kind::Attach;
        work.path = candidatePath;
        const auto cancelled = [cancellation] { return cancellation->load(); };
        try {
            const auto plan = prepareTelemetryImport({primaryPath, candidatePath}, {}, cancelled);
            const TelemetryRunProposal *primary = nullptr, *candidate = nullptr;
            for (const auto &proposal : plan.runs) {
                if (proposal.sourcePath == primaryPath) primary = &proposal;
                if (proposal.sourcePath == candidatePath) candidate = &proposal;
            }
            for (const auto &file : plan.files)
                if (file.requestedPath == candidatePath && file.status == TelemetryImportFileStatus::Error) {
                    work.error = file.message; return work;
                }
            if (!candidate || known.contains(candidate->contentSha256.toHex())) {
                work.error = QObject::tr("This recording is already in the run."); return work;
            }
            if (!primary) { work.error = QObject::tr("The run's primary recording could not be read."); return work; }
            work.sha = candidate->contentSha256.toHex();
            work.format = candidate->format;
            work.fingerprint = ProjectSourceReferenceCodec::telemetryFingerprint(candidatePath, *candidate->telemetry);
            QVariantMap evidence{{"format", candidate->format.toUpper()}, {"primaryFormat", primary->format.toUpper()}};
            const auto candidateTime = recordingTimestamp(*candidate->telemetry);
            const auto primaryTime = recordingTimestamp(*primary->telemetry);
            if (candidateTime && primaryTime)
                evidence.insert("startDifferenceSeconds", std::abs(*candidateTime - *primaryTime) / 1000.0);
            for (const auto &match : plan.possibleSameRuns) {
                if (!((match.firstRunId == primary->id && match.secondRunId == candidate->id)
                      || (match.firstRunId == candidate->id && match.secondRunId == primary->id))) continue;
                evidence.insert("comparedGpsSamples", match.comparedGpsSamples);
                evidence.insert("maximumSeparationMeters", match.maximumSeparationMeters);
                evidence.insert("gpsDurationDifferenceSeconds", match.gpsDurationDifferenceSeconds);
                evidence.insert("reason", match.reviewReason);
            }
            evidence.insert("matched", evidence.contains("comparedGpsSamples"));
            work.evidence = evidence;
        } catch (const OperationCancelled &) {
            work.cancelled = true;
        } catch (const std::exception &error) {
            work.error = QString::fromUtf8(error.what());
        }
        return work;
    }));
    return true;
}

bool DocumentController::confirmRunRecording()
{
    if (m_recordingReview.value("state") != "review" || m_recordingWatcher.isRunning()) return false;
    if (m_recordingDocumentId != m_documentId || m_recordingRevision != m_documentState.revision()) {
        setRecordingReview({{"state", "error"}, {"runId", m_recordingRunId},
            {"message", tr("The project changed. Choose the recording again.")}});
        return false;
    }
    const auto candidate = m_recordingCandidate;
    m_recordingCancellation = std::make_shared<std::atomic_bool>(false);
    const auto cancellation = m_recordingCancellation;
    setRecordingReview({{"state", "attaching"}, {"runId", m_recordingRunId}, {"message", tr("Checking the recording before adding it…")}});
    m_recordingWatcher.setFuture(QtConcurrent::run([candidate, cancellation] {
        RecordingWork work = candidate;
        work.kind = RecordingWork::Kind::Confirm;
        try {
            const auto size = QFileInfo(candidate.path).size();
            if (TelemetrySource::contentSha256(candidate.path, size, [cancellation] { return cancellation->load(); }).toHex()
                != candidate.sha)
                work.error = QObject::tr("The recording changed since it was reviewed. Choose it again.");
        } catch (const OperationCancelled &) {
            work.cancelled = true;
        } catch (const std::exception &error) {
            work.error = QString::fromUtf8(error.what());
        }
        return work;
    }));
    return true;
}

void DocumentController::cancelRunRecording()
{
    if (m_recordingCancellation) m_recordingCancellation->store(true);
    m_recordingCandidate = {};
    setRecordingReview({});
}

bool DocumentController::setRunPrimarySource(const QString &runId, const QString &sourceId)
{
    if (!recordingEditAllowed()) return false;
    const auto run = runById(m_projectTemplate, runId);
    if (run.isEmpty() || run.value("primaryTelemetrySourceId").toString() == sourceId) return false;
    QJsonObject chosen;
    for (const auto &value : run.value("sources").toObject().value("telemetry").toArray())
        if (value.toObject().value("id").toString() == sourceId) chosen = value.toObject();
    if (chosen.isEmpty()) return false;
    const auto reference = referenceOf(chosen.value("reference").toObject());
    const auto path = ProjectSourceReferenceCodec::resolve(reference, m_documentState.projectPath());
    if (path.isEmpty()) {
        setRecordingReview({{"state", "error"}, {"runId", runId},
            {"message", tr("That recording is missing. Relink it before making it the primary.")}});
        return false;
    }
    m_recordingRunId = runId;
    m_recordingRevision = m_documentState.revision();
    m_recordingDocumentId = m_documentId;
    m_recordingCancellation = std::make_shared<std::atomic_bool>(false);
    const auto cancellation = m_recordingCancellation;
    const auto expectedSha = EventProjectCodec::sourceContentRevision(chosen);
    setRecordingReview({{"state", "switching"}, {"runId", runId}, {"message", tr("Reading the recording…")}});
    m_recordingWatcher.setFuture(QtConcurrent::run([path, sourceId, reference, expectedSha, cancellation] {
        RecordingWork work;
        work.kind = RecordingWork::Kind::Primary;
        work.sourceId = sourceId;
        work.path = path;
        const auto cancelled = [cancellation] { return cancellation->load(); };
        try {
            const auto size = QFileInfo(path).size();
            const auto sha = TelemetrySource::contentSha256(path, size, cancelled).toHex();
            if (!expectedSha.isEmpty() && sha != expectedSha) {
                work.error = QObject::tr("That recording's content changed; relink it before making it the primary.");
                return work;
            }
            const auto session = TelemetrySource::load(path, cancelled);
            if (ProjectSourceReferenceCodec::compareFingerprints(reference.fingerprint,
                    ProjectSourceReferenceCodec::telemetryFingerprint(path, session)) == SourceFingerprintMatch::Mismatch) {
                work.error = QObject::tr("That recording is not the one attached to this run; relink it first.");
                return work;
            }
            work.sha = sha;
            work.gateRevision = timingGateRevision(session, cancelled);
        } catch (const OperationCancelled &) {
            work.cancelled = true;
        } catch (const std::exception &error) {
            work.error = QString::fromUtf8(error.what());
        }
        return work;
    }));
    return true;
}

void DocumentController::initializeRunRecordings()
{
    connect(this, &DocumentController::documentStateChanged, this, [this] {
        // A review is for the document it was made against.
        if (!m_recordingReview.isEmpty() && !m_recordingWatcher.isRunning() && m_recordingReview.value("state") == "review"
            && (m_recordingDocumentId != m_documentId || m_recordingRevision != m_documentState.revision()))
            cancelRunRecording();
    });
    connect(&m_recordingWatcher, &QFutureWatcher<RecordingWork>::finished, this, [this] {
        auto work = m_recordingWatcher.future().takeResult();
        const auto runId = m_recordingRunId;
        if (work.cancelled) { setRecordingReview({}); return; }
        if (m_recordingDocumentId != m_documentId || m_recordingRevision != m_documentState.revision()) {
            setRecordingReview({{"state", "error"}, {"runId", runId},
                {"message", tr("The project changed meanwhile. Try again.")}});
            return;
        }
        if (!work.error.isEmpty()) {
            setRecordingReview({{"state", "error"}, {"runId", runId}, {"message", work.error}});
            return;
        }
        if (work.kind == RecordingWork::Kind::Attach) {
            m_recordingCandidate = work;
            setRecordingReview({{"state", "review"}, {"runId", runId},
                {"name", QFileInfo(work.path).fileName()}, {"evidence", work.evidence}});
            return;
        }
        auto project = currentProjectObject();
        auto event = project.value("event").toObject();
        auto runs = event.value("runs").toArray();
        bool changedActiveRun = false;
        for (qsizetype i = 0; i < runs.size(); ++i) {
            auto run = runs[i].toObject();
            if (run.value("id").toString() != runId) continue;
            auto sources = run.value("sources").toObject();
            auto telemetry = sources.value("telemetry").toArray();
            if (work.kind == RecordingWork::Kind::Confirm) {
                const auto sourceId = QUuid::createUuid().toString(QUuid::WithoutBraces);
                telemetry.append(QJsonObject{{"id", sourceId},
                    {"reference", ProjectSourceReferenceCodec::toJson({{}, work.path, work.fingerprint}, m_documentState.projectPath())},
                    {"contentSha256", QString::fromLatin1(work.sha)},
                    {"importProvenance", QJsonObject{{"sha256", QString::fromLatin1(work.sha)}, {"format", work.format},
                                                     {"fingerprint", work.fingerprint}}}});
                sources.insert("telemetry", telemetry);
                run.insert("sources", sources);
            } else {
                QJsonObject fingerprint;
                for (const auto &value : telemetry)
                    if (value.toObject().value("id").toString() == work.sourceId)
                        fingerprint = value.toObject().value("reference").toObject().value("fingerprint").toObject();
                run.insert("primaryTelemetrySourceId", work.sourceId);
                // A new primary cannot inherit the old one's asserted layout or
                // verified inference; the gates it actually records are kept.
                run.insert("trackConfiguration", EventProjectCodec::unknownTrackConfiguration(work.sourceId, fingerprint, work.gateRevision));
                run.remove("trackInference");
                changedActiveRun = runId == activeRunId();
            }
            runs[i] = run;
        }
        event.insert("runs", runs);
        project.insert("event", event);
        QString error;
        if (!ProjectLimits::validateProject(project, &error)) {
            setRecordingReview({{"state", "error"}, {"runId", runId}, {"message", error}});
            return;
        }
        m_recordingCandidate = {};
        if (changedActiveRun) {
            // The editor holds the active run's primary; reload it through the
            // run-selection path so a save cannot write the old reference back.
            setRecordingReview({});
            static_cast<void>(beginProjectLoad(m_documentState.projectPath(), project, false, 0, 0, {}, true));
        } else {
            m_projectTemplate = project;
            markPersistentChange();
            setRecordingReview({});
        }
        m_host.showStatus(work.kind == RecordingWork::Kind::Confirm
            ? tr("Recording added to the run as an alternative. Save to keep it.")
            : tr("Primary recording changed. Its laps are derived again; results that used the old laps need recomputing. Check this run's video sync."));
        emit runRecordingsChanged();
    });
}

} // namespace FlappedEar
