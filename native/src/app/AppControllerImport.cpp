#include "app/AppController.h"
#include "project/EventProjectCodec.h"
#include "project/ProjectLimits.h"
#include "telemetry/SourceOperation.h"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QScopedValueRollback>
#include <QSet>
#include <QUuid>
#include <QtConcurrent>
#include <limits>

namespace FlappedEar {
namespace {

QString identity() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }

QByteArray sourceDigest(const QString &path, const CancellationCheck &cancelled)
{
    throwIfCancelled(cancelled);
    QFile file(path);
    if (!QFileInfo(path).isFile() || !file.open(QIODevice::ReadOnly)
        || file.size() <= 0 || file.size() > TelemetryImportLimits{}.maximumFileBytes) {
        throw std::runtime_error("Source is missing, unreadable or exceeds the import limit.");
    }
    const qint64 size = file.size();
    qint64 bytes = 0;
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd()) {
        throwIfCancelled(cancelled);
        const QByteArray block = file.read(64 * 1024);
        bytes += block.size();
        if (block.isEmpty() || bytes > size) throw std::runtime_error("Source changed or could not be read.");
        hash.addData(block);
    }
    if (bytes != size || file.size() != size) throw std::runtime_error("Source changed during validation.");
    return hash.result();
}

QString contentKey(const TelemetryRunProposal &run)
{
    return run.format + ':' + QString::fromLatin1(run.contentSha256.toHex());
}

QSet<QString> existingContent(const QJsonObject &project)
{
    QSet<QString> result;
    const auto event = project.value("event").toObject();
    for (const auto &value : event.value("runs").toArray()) {
        const auto sources = value.toObject().value("sources").toObject();
        for (const auto &item : sources.value("telemetry").toArray()) {
            const auto source = item.toObject();
            const auto provenance = source.value("importProvenance").toObject();
            const auto reference = source.value("reference").toObject();
            // A relink/replacement must not inherit the original content identity.
            if (provenance.value("sha256").toString().size() == 64
                && EventProjectCodec::sourceContentRevision(source) == provenance.value("sha256").toString().toLatin1()
                && provenance.value("fingerprint").isObject()
                && provenance.value("fingerprint") == reference.value("fingerprint")) {
                result.insert(provenance.value("format").toString() + ':' + provenance.value("sha256").toString());
            }
        }
    }
    return result;
}

} // namespace

bool AppController::batchContextMatches() const
{
    return m_batchDocumentId == m_documentId && m_batchRevision == m_documentState.revision()
        && m_batchProjectPath == m_documentState.projectPath()
        && m_batchGeneration == m_sourceGeneration;
}

void AppController::invalidateBatchImport()
{
    if (m_batchApplying || m_batchState == "idle" || m_batchState == "error" || batchContextMatches()) return;
    cancelBatchImport();
    m_batchError = QStringLiteral("Project or sources changed. Select the files again to review a fresh import.");
    if (!m_batchPending) m_batchState = QStringLiteral("error");
    emit batchImportChanged();
}

void AppController::initializeBatchImport()
{
    connect(this, &AppController::documentStateChanged, this, &AppController::invalidateBatchImport);
    connect(this, &AppController::sourceLoadStateChanged, this, &AppController::invalidateBatchImport);
    m_batchProgressTimer.setInterval(100);
    connect(&m_batchProgressTimer, &QTimer::timeout, this, [this] {
        if (m_batchProgress && m_batchProcessed != m_batchProgress->load()) {
            m_batchProcessed = m_batchProgress->load();
            emit batchImportChanged();
        }
    });
    connect(&m_batchWatcher, &QFutureWatcher<BatchImportResult>::finished, this, [this] {
        auto result = m_batchWatcher.future().takeResult();
        m_batchPending = false;
        m_batchProgressTimer.stop();
        if (m_batchProgress) m_batchProcessed = m_batchProgress->load();
        if (m_batchState == "cancelling" || result.cancelled || (m_batchCancellation && m_batchCancellation->load())) {
            m_batchState = m_batchError.isEmpty() ? QStringLiteral("idle") : QStringLiteral("error");
        } else if (!batchContextMatches()) {
            m_batchState = QStringLiteral("error");
            m_batchError = QStringLiteral("Import is stale because the project changed. Select the files again.");
        } else if (!result.error.isEmpty()) {
            m_batchState = result.confirmation ? QStringLiteral("review") : QStringLiteral("error");
            m_batchError = result.error;
        } else if (result.confirmation) {
            const QScopedValueRollback applying(m_batchApplying, true);
            if (documentBusy() || projectLoading() || recoveryPending()
                || m_documentState.pendingAction() != ProjectDocumentState::DestructiveAction::None
                || (!result.append && dirty())) {
                m_batchState = QStringLiteral("review");
                m_batchError = QStringLiteral("Finish the current operation or save your edits before creating a new event.");
            } else {
                bool applied = true;
                if (result.append) {
                    m_projectTemplate = result.project;
                } else {
                    applied = beginProjectLoad({}, result.project);
                }
                if (applied) {
                    markPersistentChange();
                    m_batchPlan.reset();
                    m_batchFingerprints.clear();
                    m_batchRows.clear();
                    m_batchState = QStringLiteral("idle");
                    m_batchError.clear();
                    setAnalysisVisible(true);
                    setStatus(result.append ? QStringLiteral("Runs added to the event. Save to keep them.")
                                            : QStringLiteral("Event created. Save to keep it."));
                    emit batchImportCommitted();
                } else {
                    m_batchState = QStringLiteral("review");
                    m_batchError = QStringLiteral("The event could not be applied; the current document was retained.");
                }
            }
        } else {
            m_batchPlan = std::move(result.plan);
            m_batchFingerprints = std::move(result.fingerprints);
            m_batchExisting = std::move(result.existing);
            m_batchState = QStringLiteral("review");
            publishBatchRows();
            if (m_analysisImportAutomatic) {
                m_analysisImportAutomatic = false;
                QVariantList choices;
                const auto primaryGroups = automaticVboPrimaries(*m_batchPlan);
                for (const auto &value : m_batchRows) {
                    const auto row = value.toMap();
                    const QString id = row.value("proposalId").toString();
                    const bool ready = row.value("status").toString() == "ready";
                    const bool existing = m_analysisImportAppend && row.value("existing").toBool();
                    QString group = primaryGroups.value(id, id);
                    if (m_analysisImportAppend && m_batchExisting.contains(group)) group = id;
                    if (ready) choices.append(QVariantMap{{"proposalId", id}, {"groupId", existing ? QString{} : group}});
                    if (ready && !existing && group != id)
                        m_analysisImportMessages.append(row.value("name").toString()
                            + ": matching RCZ/VBO recording; VBO supplies the lap list and analysis.");
                    if (!ready || existing) {
                        m_analysisImportMessages.append(row.value("name").toString() + ": "
                            + (existing ? QStringLiteral("Already in this outing; skipped.") : row.value("message").toString()));
                    }
                }
                // Reuse the same guarded document transaction and final digest check.
                // Automatic groups require unique dated GPS evidence.
                confirmBatchImport(m_analysisImportName, m_analysisImportAppend, choices);
            }
        }
        if (!m_batchPending) m_analysisImportAutomatic = false;
        emit batchImportChanged();
    });
}

void AppController::cancelBatchImport()
{
    m_analysisImportAutomatic = false;
    m_analysisImportMessages.clear();
    if (m_batchCancellation) m_batchCancellation->store(true);
    m_batchPlan.reset();
    m_batchFingerprints.clear();
    m_batchExisting.clear();
    m_batchRows.clear();
    m_batchError.clear();
    m_batchState = m_batchPending ? QStringLiteral("cancelling") : QStringLiteral("idle");
    if (!m_batchPending) m_batchProgressTimer.stop();
    emit batchImportChanged();
}

bool AppController::importAnalysisRuns(const QString &name, const QList<QUrl> &urls)
{
    if (m_batchPending || documentBusy() || projectLoading() || recoveryPending()
        || m_documentState.pendingAction() != ProjectDocumentState::DestructiveAction::None) return false;
    const bool append = EventProjectCodec::isEvent(m_projectTemplate);
    if (!append && (dirty() || name.trimmed().isEmpty() || name.size() > 160)) {
        cancelBatchImport();
        m_batchState = QStringLiteral("error");
        m_batchError = dirty() ? QStringLiteral("Save your current project before starting an outing.")
                              : QStringLiteral("Enter an outing name (1–160 characters).");
        emit batchImportChanged();
        return false;
    }
    if (!beginBatchImport(urls)) return false;
    m_analysisImportAutomatic = true;
    m_analysisImportAppend = append;
    m_analysisImportName = name.trimmed();
    return true;
}

bool AppController::beginBatchImport(const QList<QUrl> &urls)
{
    if (m_batchPending || documentBusy() || projectLoading() || recoveryPending()
        || m_documentState.pendingAction() != ProjectDocumentState::DestructiveAction::None) return false;
    cancelBatchImport();
    if (urls.isEmpty() || urls.size() > TelemetryImportLimits{}.maximumFiles) {
        m_batchState = QStringLiteral("error");
        m_batchError = QStringLiteral("Select between 1 and 64 telemetry files.");
        emit batchImportChanged();
        return false;
    }
    QStringList paths;
    for (const auto &url : urls) {
        if (!url.isLocalFile() || url.toLocalFile().size() > ProjectLimits::maximumStringCharacters) {
            m_batchState = QStringLiteral("error");
            m_batchError = QStringLiteral("Only bounded local file paths are supported.");
            emit batchImportChanged();
            return false;
        }
        paths.append(url.toLocalFile());
    }
    m_batchDocumentId = m_documentId;
    m_batchProjectPath = m_documentState.projectPath();
    m_batchRevision = m_documentState.revision();
    m_batchGeneration = m_sourceGeneration;
    m_batchProcessed = 0;
    m_batchTotal = static_cast<int>(paths.size());
    m_batchState = QStringLiteral("preparing");
    m_batchCancellation = std::make_shared<std::atomic_bool>(false);
    m_batchProgress = std::make_shared<std::atomic_int>(0);
    const auto cancellation = m_batchCancellation;
    const auto progress = m_batchProgress;
    const auto known = existingContent(currentProjectObject());
    m_batchProgressTimer.start();
    m_batchPending = true;
    emit batchImportChanged();
    m_batchWatcher.setFuture(QtConcurrent::run([paths, cancellation, progress, known] {
        BatchImportResult result;
        const auto cancelled = [cancellation] { return cancellation->load(); };
        try {
            result.plan = std::make_shared<TelemetryImportPlan>(prepareTelemetryImport(paths, {}, cancelled,
                [progress](qsizetype processed, qsizetype) { progress->store(static_cast<int>(processed)); }));
            QSet<QString> unsupportedLinks;
            for (const auto &run : result.plan->runs) {
                throwIfCancelled(cancelled);
                const auto persistedPath = ProjectSourceReferenceCodec::forLoadedSource(run.sourcePath, {}).absolutePath;
                if (QFileInfo(persistedPath).suffix().toLower() != run.format) {
                    unsupportedLinks.insert(run.id);
                    continue;
                }
                if (known.contains(contentKey(run))) result.existing.insert(run.id);
                const auto fingerprint = ProjectSourceReferenceCodec::telemetryFingerprint(run.sourcePath, *run.telemetry);
                if (sourceDigest(run.sourcePath, cancelled) != run.contentSha256) {
                    throw std::runtime_error("A file changed during review preparation. Retry with stable sources.");
                }
                result.fingerprints.insert(run.id, fingerprint);
            }
            for (auto &file : result.plan->files) {
                if (!unsupportedLinks.contains(file.runId)) continue;
                file.status = TelemetryImportFileStatus::Error;
                file.runId.clear();
                file.message = QStringLiteral("The link target has a different format extension. Import a regular copy with the correct VBO/RCZ extension.");
            }
            result.plan->runs.removeIf([&unsupportedLinks](const auto &run) { return unsupportedLinks.contains(run.id); });
            result.plan->possibleSameRuns.removeIf([&unsupportedLinks](const auto &match) {
                return unsupportedLinks.contains(match.firstRunId) || unsupportedLinks.contains(match.secondRunId);
            });
        } catch (const OperationCancelled &) { result.cancelled = true; }
        catch (const std::exception &error) { result.error = QString::fromUtf8(error.what()); }
        return result;
    }));
    return true;
}

void AppController::publishBatchRows()
{
    m_batchRows.clear();
    if (!m_batchPlan) return;
    for (const auto &file : m_batchPlan->files) {
        QVariantMap row{{"path", file.requestedPath}, {"name", QFileInfo(file.requestedPath).fileName()},
                        {"proposalId", file.runId}, {"message", file.message},
                        {"status", file.status == TelemetryImportFileStatus::Ready ? "ready"
                            : file.status == TelemetryImportFileStatus::Duplicate ? "duplicate" : "error"}};
        if (file.status == TelemetryImportFileStatus::Ready) {
            row.insert("existing", m_batchExisting.contains(file.runId));
            for (const auto &run : m_batchPlan->runs) {
                if (run.id != file.runId) continue;
                row.insert("duration", run.telemetry->duration);
                row.insert("laps", run.laps.timedLaps.size());
                row.insert("channels", run.telemetry->channels.size());
                QStringList messages = run.telemetry->warnings.mid(0, 4);
                if (m_batchExisting.contains(run.id)) messages.prepend(QStringLiteral("Already referenced by the current event; skipped when appending."));
                for (const auto &match : m_batchPlan->possibleSameRuns) {
                    if (match.firstRunId != run.id && match.secondRunId != run.id) continue;
                    const QString otherId = match.firstRunId == run.id ? match.secondRunId : match.firstRunId;
                    for (const auto &other : m_batchPlan->runs) if (other.id == otherId) {
                        messages.append(QStringLiteral("Possible same run as %1: %2")
                            .arg(QFileInfo(other.sourcePath).fileName(), match.reviewReason));
                    }
                }
                row.insert("message", messages.join('\n').left(4096));
                break;
            }
        }
        m_batchRows.append(row);
    }
}

bool AppController::confirmBatchImport(const QString &name, const bool append, const QVariantList &choices)
{
    const auto reject = [this](const QString &error) {
        m_batchError = error; emit batchImportChanged(); return false;
    };
    if (m_batchState != "review" || !m_batchPlan || m_batchPending) return false;
    if (!batchContextMatches()) { invalidateBatchImport(); return false; }
    if (documentBusy() || projectLoading() || recoveryPending()
        || m_documentState.pendingAction() != ProjectDocumentState::DestructiveAction::None) return reject("Finish the current operation first.");
    if (!append && dirty()) return reject("Save the current project before creating a new event, or append to this event.");
    if (append && !EventProjectCodec::isEvent(m_projectTemplate)) return reject("Open an event before appending runs.");
    if (m_documentState.revision() == std::numeric_limits<quint64>::max()) return reject("Document revision limit reached.");
    if (!append && (name.trimmed().isEmpty() || name.size() > 160)) return reject("Enter an event name (1–160 characters).");
    if (choices.size() != m_batchPlan->runs.size()) return reject("Review every successfully imported source.");
    QHash<QString, QString> groups;
    QSet<QString> available;
    for (const auto &run : m_batchPlan->runs) available.insert(run.id);
    for (const auto &value : choices) {
        const auto choice = value.toMap();
        const QString id = choice.value("proposalId").toString();
        const QString group = choice.value("groupId").toString();
        if (!available.contains(id) || groups.contains(id) || (!group.isEmpty() && !available.contains(group))) {
            return reject("Invalid or duplicate source choice.");
        }
        groups.insert(id, group);
    }
    QVector<TelemetryRunProposal> selected;
    for (const auto &run : m_batchPlan->runs) {
        const QString group = groups.value(run.id);
        if (group.isEmpty()) continue;
        if (append && (m_batchExisting.contains(run.id) || m_batchExisting.contains(group))) {
            return reject("Already-imported sources must be skipped when appending; group new sources under a new primary.");
        }
        if (groups.value(group) != group) return reject("A grouped source must point directly to a separate primary run, not a skipped or grouped source.");
        selected.append(run);
    }
    if (selected.isEmpty()) return reject("Select at least one new run.");
    QJsonObject project = currentProjectObject();
    QJsonObject event = append ? project.value("event").toObject()
                              : QJsonObject{{"id", identity()}, {"name", name.trimmed()}};
    QJsonArray runs = event.value("runs").toArray();
    // KAN-119: runs are named "Session N" in recording order, continuing after
    // the event's existing runs; a run without a recording clock follows the
    // dated ones in import order. Names are stored, so they never renumber.
    QVector<TelemetryRunProposal> primaries;
    for (const auto &run : selected)
        if (groups.value(run.id) == run.id) primaries.append(run);
    const auto startOf = [](const TelemetryRunProposal &run) {
        return run.telemetry ? recordingTimestamp(*run.telemetry) : std::nullopt;
    };
    std::stable_sort(primaries.begin(), primaries.end(), [&](const TelemetryRunProposal &a, const TelemetryRunProposal &b) {
        const auto left = startOf(a), right = startOf(b);
        if (left.has_value() != right.has_value()) return left.has_value();
        return left && right && *left < *right;
    });
    auto sessionNumber = runs.size();
    for (const auto &primary : primaries) {
        QJsonArray sources;
        QString primarySourceId;
        for (const auto &source : selected) {
            if (groups.value(source.id) != primary.id) continue;
            const QString sourceId = identity();
            if (source.id == primary.id) primarySourceId = sourceId;
            const auto fingerprint = m_batchFingerprints.value(source.id);
            const auto reference = ProjectSourceReferenceCodec::toJson(
                { {}, source.sourcePath, fingerprint }, append ? m_documentState.projectPath() : QString{});
            sources.append(QJsonObject{{"id", sourceId}, {"reference", reference},
                {"contentSha256", QString::fromLatin1(source.contentSha256.toHex())},
                {"importProvenance", QJsonObject{{"sha256", QString::fromLatin1(source.contentSha256.toHex())},
                    {"format", source.format}, {"fingerprint", fingerprint}}}});
        }
        const QString runId = identity();
        const QString runName = QStringLiteral("Session %1").arg(++sessionNumber);
        runs.append(QJsonObject{{"id", runId}, {"name", runName}, {"primaryTelemetrySourceId", primarySourceId},
            {"sources", QJsonObject{{"telemetry", sources}}},
            {"trackConfiguration", EventProjectCodec::unknownTrackConfiguration(primarySourceId,
                m_batchFingerprints.value(primary.id), timingGateRevision(*primary.telemetry))},
            {"sync", QJsonObject{{"offset", 0.0}, {"timeScale", 1.0}}}});
        if (!append && !event.contains("activeRunId")) event.insert("activeRunId", runId);
    }
    event.insert("runs", runs);
    project.insert("event", event);
    project.insert("version", 3);
    for (const QString &key : {QStringLiteral("sources"), QStringLiteral("sync"), QStringLiteral("videoPath"), QStringLiteral("vboPath")}) project.remove(key);
    if (!append) project.insert("documentState", QJsonObject{{"id", identity()}, {"savedRevision", "0"}});
    QString error;
    if (!ProjectLimits::validateProject(project, &error)) return reject(error);
    if (QJsonDocument(project).toJson().size() > ProjectLimits::projectBytes) return reject("The resulting event exceeds the project size limit.");
    m_batchState = QStringLiteral("validating");
    m_batchError.clear();
    m_batchProcessed = 0;
    m_batchTotal = static_cast<int>(selected.size());
    m_batchProgress = std::make_shared<std::atomic_int>(0);
    m_batchCancellation = std::make_shared<std::atomic_bool>(false);
    const auto progress = m_batchProgress;
    const auto cancellation = m_batchCancellation;
    m_batchProgressTimer.start();
    m_batchPending = true;
    emit batchImportChanged();
    m_batchWatcher.setFuture(QtConcurrent::run([selected, project, append, progress, cancellation] {
        BatchImportResult result;
        result.confirmation = true;
        result.append = append;
        result.project = project;
        const auto cancelled = [cancellation] { return cancellation->load(); };
        try {
            for (const auto &source : selected) {
                if (sourceDigest(source.sourcePath, cancelled) != source.contentSha256) {
                    throw std::runtime_error("A reviewed source changed. Cancel and import the files again.");
                }
                progress->fetch_add(1);
            }
            throwIfCancelled(cancelled);
        } catch (const OperationCancelled &) { result.cancelled = true; }
        catch (const std::exception &error) { result.error = QString::fromUtf8(error.what()); }
        return result;
    }));
    return true;
}

} // namespace FlappedEar
