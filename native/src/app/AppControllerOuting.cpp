#include "app/AppController.h"
#include "project/EventProjectCodec.h"
#include "project/ProjectLimits.h"
#include "telemetry/TelemetrySession.h"
#include "telemetry/TelemetrySource.h"

#include <QDateTime>
#include <QTimeZone>
#include <QFileInfo>
#include <QJsonDocument>
#include <QCryptographicHash>
#include <QMap>
#include <QtConcurrent>
#include <algorithm>
#include <cmath>
#include <limits>

namespace FlappedEar {
namespace {
QByteArray sourceDependencyKey(QJsonObject source)
{
    source.remove("name");
    source.remove("inference");
    source.insert("reference", source.value("reference").toObject().value("fingerprint"));
    return QJsonDocument(source).toJson(QJsonDocument::Compact);
}
}

QVariantMap AppController::runMetadata(const QString &runId) const
{
    const auto event = currentProjectObject().value("event").toObject();
    for (const auto &value : event.value("runs").toArray()) {
        const auto run = value.toObject();
        if (run.value("id").toString() != runId) continue;
        QJsonObject metadata{{"name", run.value("name")}};
        for (const auto *key : {"notes", "conditions", "setupChanges"})
            metadata.insert(key, run.contains(key) ? run.value(key) : QJsonValue(QJsonValue::Null));
        const auto token = QCryptographicHash::hash(QJsonDocument(QJsonObject{
            {"documentId", m_documentId}, {"run", run}}).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex();
        metadata.insert("editToken", QString::fromLatin1(token));
        return metadata.toVariantMap();
    }
    return {};
}

bool AppController::updateRunMetadata(const QString &runId, const QString &expectedToken,
    const QString &name, const QString &notes, const QString &conditions, const QString &setupChanges)
{
    if (!EventProjectCodec::isEvent(m_projectTemplate) || projectLoading() || documentBusy()
        || recoveryPending() || m_batchPending
        || m_documentState.pendingAction() != ProjectDocumentState::DestructiveAction::None
        || m_documentState.revision() == std::numeric_limits<quint64>::max()) return false;
    const auto current = runMetadata(runId);
    if (current.isEmpty() || expectedToken.isEmpty() || current.value("editToken").toString() != expectedToken
        || name.trimmed().isEmpty() || name.size() > ProjectLimits::maximumTemplateNameCharacters) return false;
    for (const auto &text : {name, notes, conditions, setupChanges})
        if (text.size() > ProjectLimits::maximumStringCharacters || text.contains(QChar::Null)) return false;
    auto project = currentProjectObject(); auto event = project.value("event").toObject();
    auto runs = event.value("runs").toArray();
    for (qsizetype i = 0; i < runs.size(); ++i) {
        auto run = runs[i].toObject();
        if (run.value("id").toString() != runId) continue;
        const auto before = run;
        run.insert("name", name.trimmed());
        const QJsonObject values{{"notes", notes}, {"conditions", conditions}, {"setupChanges", setupChanges}};
        for (auto it = values.begin(); it != values.end(); ++it) {
            // Opening/saving an unchanged legacy record must not invent metadata.
            if (run.value(it.key()).toString() == it.value().toString()) continue;
            run.insert(it.key(), it.value().toString().trimmed().isEmpty() ? QJsonValue(QJsonValue::Null) : it.value());
        }
        if (run == before) return true;
        runs[i] = run; event.insert("runs", runs); project.insert("event", event);
        QString error;
        if (!ProjectLimits::validateProject(project, &error)) return false;
        m_projectTemplate = project;
        markPersistentChange();
        return true;
    }
    return false;
}

QVariantMap AppController::runTrackConfiguration(const QString &runId) const
{
    for (const auto &value : outingLapSources()) {
        const auto source = value.toObject();
        if (source.value("runId").toString() != runId) continue;
        auto config = source.value("trackConfiguration").toObject();
        config.insert("derivationKey", source.value("derivationKey"));
        const auto cached = m_outingRunCache.value(runId);
        if (cached.dependencyKey == outingRunKey(runId)) {
            config.insert("inferredDirection", cached.inference.route.direction);
            config.insert("inferenceReason", m_outingInferredGroups.reasons.value(runId, cached.inference.reason));
            config.insert("inferenceSupported", cached.inference.supported() && !m_outingInferredGroups.reasons.contains(runId));
        }
        return config.toVariantMap();
    }
    return {};
}

bool AppController::confirmRunTrackConfiguration(const QString &runId, const QString &expectedDerivationKey,
    const QString &layoutId, const QString &direction, const bool applyToMatching)
{
    const auto current = runTrackConfiguration(runId);
    if (current.isEmpty() || expectedDerivationKey.isEmpty()
        || current.value("derivationKey").toString() != expectedDerivationKey) return false;
    QStringList ids{runId};
    if (applyToMatching) {
        const auto cached = m_outingRunCache.value(runId);
        if (cached.dependencyKey != outingRunKey(runId) || !cached.inference.supported()
            || m_outingInferredGroups.reasons.contains(runId)) return false;
        for (auto it = m_outingRunCache.cbegin(); it != m_outingRunCache.cend(); ++it) {
            if (it.key() != runId && it->dependencyKey == outingRunKey(it.key())
                && routesMatch(cached.inference.route, it->inference.route)) ids.append(it.key());
        }
    }
    return setRunTrackConfigurations(ids, layoutId.trimmed(), direction);
}

QVariantMap AppController::outingRanking() const
{
    if (projectLoading() || m_outingLapsLoading || m_outingLapRequestedKey != outingLapKey()
        || m_outingLapGeneration != m_sourceGeneration) return {{"state", "loading"}};
    return m_outingRanking;
}

QVariantMap AppController::outingProgression() const
{
    if (projectLoading() || m_outingLapsLoading || m_outingLapRequestedKey != outingLapKey()
        || m_outingLapGeneration != m_sourceGeneration) return {{"state", "loading"}};
    return m_outingProgression;
}

QVariantList AppController::outingCompatibilityGroups() const
{
    if (projectLoading() || m_outingLapsLoading || m_outingLapRequestedKey != outingLapKey()
        || m_outingLapGeneration != m_sourceGeneration) return {};
    return m_outingCompatibilityGroups;
}

QString AppController::outingComparisonGroupId() const
{
    for (const auto &value : outingCompatibilityGroups())
        if (value.toMap().value("id").toString() == m_outingComparisonGroupId
            && value.toMap().value("available").toBool()) return m_outingComparisonGroupId;
    return {};
}

QString AppController::outingComparisonSelectionState() const
{
    const auto saved = currentProjectObject().value("event").toObject().value("analysisDecisions")
        .toObject().value("comparisonGroupId").toString();
    if (saved.isEmpty()) return outingComparisonGroupId().isEmpty() ? "none" : "automatic";
    if (projectLoading() || m_outingLapsLoading || m_outingLapRequestedKey != outingLapKey()
        || m_outingLapGeneration != m_sourceGeneration) return "loading";
    return outingComparisonGroupId() == saved ? "applied" : "unavailable";
}

bool AppController::selectOutingComparisonGroup(const QString &groupId)
{
    if (!EventProjectCodec::isEvent(m_projectTemplate) || projectLoading() || documentBusy()
        || recoveryPending() || m_batchPending
        || m_documentState.pendingAction() != ProjectDocumentState::DestructiveAction::None
        || m_documentState.revision() == std::numeric_limits<quint64>::max()) return false;
    if (!groupId.isEmpty()) {
        if (m_outingLapsLoading || m_outingLapRequestedKey != outingLapKey()
            || m_outingLapGeneration != m_sourceGeneration) return false;
        bool found = false;
        for (const auto &value : m_outingCompatibilityGroups) {
            const auto group = value.toMap();
            found |= group.value("id").toString() == groupId && group.value("resolved").toBool() && group.value("available").toBool();
        }
        if (!found) return false;
    }
    auto project = currentProjectObject(); auto event = project.value("event").toObject();
    auto decisions = event.value("analysisDecisions").toObject();
    if (decisions.value("comparisonGroupId").toString() == groupId) return true;
    decisions.insert("comparisonGroupId", groupId.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(groupId));
    event.insert("analysisDecisions", decisions); project.insert("event", event);
    if (!ProjectLimits::validateProject(project)) return false;
    m_projectTemplate = project;
    markPersistentChange();
    emit outingLapsChanged();
    return true;
}

void AppController::refreshOutingCompatibility()
{
    if (m_outingLapRequestedKey != outingLapKey() || m_outingLapGeneration != m_sourceGeneration) return;
    m_outingComparisonGroupId = currentProjectObject().value("event").toObject()
        .value("analysisDecisions").toObject().value("comparisonGroupId").toString();
    QHash<QString, QJsonObject> configurations;
    for (const auto &value : outingLapSources()) {
        const auto source = value.toObject();
        const auto runId = source.value("runId").toString();
        configurations.insert(runId, m_outingInferredGroups.configurations.value(runId, source.value("trackConfiguration").toObject()));
    }
    m_outingRunConfigurations = configurations;
    QMap<QString, QVariantMap> groups;
    QHash<QString, QVariantList> membersByGroup, eligibleByGroup;
    for (const auto &value : m_outingLapRows) {
        const auto row = value.toMap(); const auto runId = row.value("runId").toString();
        const auto config = configurations.value(runId);
        const auto resolvedId = lapCompatibilityGroupId(config);
        const auto id = resolvedId.isEmpty() ? "unresolved:" + runId : resolvedId;
        auto &group = groups[id];
        if (group.isEmpty()) group = {{"id", id}, {"resolved", !resolvedId.isEmpty()},
            {"configuration", config.toVariantMap()}, {"runName", row.value("runName")},
            {"members", QVariantList{}}, {"eligibleMembers", QVariantList{}}};
        if (!m_outingStaleRunIds.contains(runId)) group.insert("available", true);
        if (row.value("type") != "LAP") continue;
        membersByGroup[id].append(row.value("reference"));
        if (!resolvedId.isEmpty() && row.value("referenceEligible").toBool() && !m_outingStaleRunIds.contains(runId)) {
            eligibleByGroup[id].append(row.value("reference"));
        }
    }
    if (m_outingComparisonGroupId.isEmpty()) {
        for (auto it = groups.cbegin(); it != groups.cend(); ++it)
            if (it.value().value("resolved").toBool() && it.value().value("available").toBool()) { m_outingComparisonGroupId = it.key(); break; }
    }
    if (!groups.contains(m_outingComparisonGroupId)
        || !groups.value(m_outingComparisonGroupId).value("resolved").toBool()) m_outingComparisonGroupId.clear();
    const auto referenceConfig = groups.value(m_outingComparisonGroupId).value("available").toBool()
        ? QJsonObject::fromVariantMap(groups.value(m_outingComparisonGroupId).value("configuration").toMap()) : QJsonObject{};
    m_outingCompatibilityGroups.clear();
    int number = 0;
    for (auto it = groups.begin(); it != groups.end(); ++it) {
        auto &group = it.value(); const auto config = group.value("configuration").toMap();
        group.insert("members", membersByGroup.value(it.key()));
        group.insert("eligibleMembers", eligibleByGroup.value(it.key()));
        group.insert("lapCount", group.value("members").toList().size());
        group.insert("eligibleLapCount", group.value("eligibleMembers").toList().size());
        const auto label = group.value("resolved").toBool()
            ? QStringLiteral("Group %1 · %2 · %3").arg(++number).arg(config.value("layoutId").toString().startsWith("gps-route-v1:")
                ? QStringLiteral("Detected route") : config.value("layoutId").toString(),
                config.value("direction") == "clockwise" ? QStringLiteral("Clockwise") : QStringLiteral("Counterclockwise"))
            : QStringLiteral("Unresolved · %1").arg(group.value("runName").toString());
        group.insert("label", label);
        group.insert("summary", QStringLiteral("%1 · %2/%3 eligible laps").arg(label)
            .arg(group.value("eligibleLapCount").toLongLong()).arg(group.value("lapCount").toLongLong()));
        m_outingCompatibilityGroups.append(group);
    }
    m_outingRanking = rankOutingLaps(m_outingRawLapRows, m_outingComparisonGroupId, configurations,
        currentProjectObject().value("event").toObject().value("lapExclusions").toArray(), m_outingStaleRunIds).toVariantMap();
    m_outingRanking.insert("groupLabel", groups.value(m_outingComparisonGroupId).value("label"));
    QJsonArray metadata;
    for (const auto &value : currentProjectObject().value("event").toObject().value("runs").toArray()) {
        const auto run = value.toObject();
        QJsonObject item{{"id", run.value("id")}, {"name", run.value("name")},
            {"groupId", lapCompatibilityGroupId(configurations.value(run.value("id").toString()))}};
        for (const auto *field : {"notes", "conditions", "setupChanges"}) item.insert(field, run.value(field));
        metadata.append(item);
    }
    auto progression = summarizeOutingProgression(m_outingRawLapRows, QJsonObject::fromVariantMap(m_outingRanking), metadata);
    auto progressionRuns = progression.value("runs").toArray();
    for (qsizetype i = 0; i < progressionRuns.size(); ++i) {
        auto run = progressionRuns[i].toObject();
        run.insert("clock", run.value("chronologyKnown").toBool()
            ? QDateTime::fromMSecsSinceEpoch(run.value("firstSectionUtcMilliseconds").toString().toLongLong(), QTimeZone::UTC).toString("yyyy-MM-dd HH:mm:ss.zzz")
            : QStringLiteral("Time unavailable · import order"));
        progressionRuns[i] = run;
    }
    progression.insert("runs", progressionRuns);
    m_outingProgression = progression.toVariantMap();
    for (auto &value : m_outingCompatibilityGroups) {
        auto group = value.toMap();
        if (group.value("resolved").toBool()) {
            const auto ranking = rankOutingLaps(m_outingRawLapRows, group.value("id").toString(), configurations,
                currentProjectObject().value("event").toObject().value("lapExclusions").toArray(), m_outingStaleRunIds);
            group.insert("ranking", ranking.toVariantMap());
            group.insert("progression", summarizeOutingProgression(m_outingRawLapRows, ranking, metadata).toVariantMap());
        }
        value = group;
    }
    const auto bestReference = m_outingRanking.value("bestOfDay").toMap().value("reference").toMap();
    QHash<QString, QVariantMap> bestRunReferences;
    for (const auto &value : m_outingRanking.value("runs").toList()) {
        const auto run = value.toMap();
        bestRunReferences.insert(run.value("runId").toString(), run.value("bestLap").toMap().value("reference").toMap());
    }
    for (auto &value : m_outingLapRows) {
        auto row = value.toMap(); const auto runId = row.value("runId").toString();
        const auto config = configurations.value(runId); const auto resolvedId = lapCompatibilityGroupId(config);
        const auto id = resolvedId.isEmpty() ? "unresolved:" + runId : resolvedId;
        const auto issue = row.value("referenceIssue") == "GPS gap" ? LapReferenceIssue::GpsGap
            : row.value("referenceIssue") == "Invalid GPS" ? LapReferenceIssue::InvalidGps : LapReferenceIssue::None;
        auto reasons = lapCompatibilityReasons(config, referenceConfig, issue, row.value("excluded").toBool());
        if (!row.value("layoutIssue").toString().isEmpty()) reasons.append(row.value("layoutIssue").toString());
        if (row.value("type") != "LAP") reasons.append("not-timed-lap");
        if (m_outingStaleRunIds.contains(runId)) reasons.append("stale-source");
        QStringList labels;
        for (const auto &reason : reasons) labels.append(lapCompatibilityReasonText(reason));
        row.insert("compatibilityGroupId", id);
        row.insert("compatibilityGroupLabel", groups.value(id).value("label"));
        row.insert("compatibilityResolved", !resolvedId.isEmpty());
        row.insert("compatibilityReasons", reasons);
        row.insert("compatibilityReasonLabels", labels);
        row.insert("comparisonEligible", !referenceConfig.isEmpty() && reasons.isEmpty());
        row.insert("bestOfDay", !bestReference.isEmpty() && row.value("reference").toMap() == bestReference);
        if (id == m_outingComparisonGroupId) {
            const auto runBest = bestRunReferences.value(runId);
            row.insert("bestOfRun", !runBest.isEmpty() && row.value("reference").toMap() == runBest);
        }
        if (m_outingStaleRunIds.contains(runId)) row.insert("bestOfRun", false);
        value = row;
        if (!m_selectedOutingLap.isEmpty() && m_selectedOutingLap.value("reference") == row.value("reference")) {
            m_selectedOutingLap = row; emit outingLapDetailChanged(); emit outingLapVideoChanged();
        }
    }
}

QJsonObject AppController::activeLapBinding() const
{
    for (auto value : outingLapSources()) {
        auto source = value.toObject();
        if (source.value("runId").toString() != activeRunId()) continue;
        source.insert("sourceRevision", QString::fromLatin1(m_loadedSourceRevision));
        source.remove("reference"); source.remove("name"); source.remove("trackConfiguration");
        return source;
    }
    return {};
}

bool AppController::setOutingLapExcluded(const QVariantMap &referenceMap, bool excluded, const QString &reason)
{
    if (!EventProjectCodec::isEvent(m_projectTemplate) || projectLoading() || documentBusy()
        || recoveryPending() || m_batchPending
        || m_documentState.pendingAction() != ProjectDocumentState::DestructiveAction::None
        || m_documentState.revision() == std::numeric_limits<quint64>::max()) return false;
    const auto reference = QJsonObject::fromVariantMap(referenceMap);
    if (!validLapReference(reference) || reference.value("type") != "LAP") return false;
    if (excluded && (resolveOutingLapReference(referenceMap).value("state").toString() != "resolved"
        || reason.trimmed().isEmpty() || reason.size() > 256 || reason.contains(QChar::Null))) return false;
    auto project = currentProjectObject();
    auto event = project.value("event").toObject();
    QJsonArray exclusions;
    for (const auto &item : event.value("lapExclusions").toArray()) {
        if (excluded && item.toObject().value("reference").toObject() == reference
            && item.toObject().value("reason").toString() == reason.trimmed()) return true;
        if (item.toObject().value("reference").toObject() != reference) exclusions.append(item);
    }
    if (excluded) exclusions.append(QJsonObject{{"reference", reference}, {"reason", reason.trimmed()}});
    if (exclusions == event.value("lapExclusions").toArray()) return true;
    event.insert("lapExclusions", exclusions); project.insert("event", event);
    QString error;
    if (!ProjectLimits::validateProject(project, &error)) return false;
    m_projectTemplate = project;
    markPersistentChange();
    return true;
}

void AppController::refreshLapExclusionPolicy()
{
    const auto event = currentProjectObject().value("event").toObject();
    const auto exclusions = event.value("lapExclusions").toArray();
    applyLapExclusions(m_lapSession, activeLapBinding(), exclusions);
    m_previewRenderContext.setLapSession(m_lapSession);
    emit lapNavigationChanged();
    emit liveValuesChanged();
    if (m_outingLapRequestedKey != outingLapKey() || m_outingLapGeneration != m_sourceGeneration) return;
    QHash<QString, QString> names;
    for (const auto &value : event.value("runs").toArray()) {
        const auto run = value.toObject(); names.insert(run.value("id").toString(), run.value("name").toString());
    }
    for (auto &row : m_outingRawLapRows) row.runName = names.value(row.runId, row.runName);
    const auto reasons = lapExclusionReasons(exclusions);
    QSet<QByteArray> matched;
    QHash<QString, LapSession> runs;
    for (const auto &row : m_outingRawLapRows) {
        if (row.type != LapSectionType::Lap || !row.layoutIssue.isEmpty()) continue;
        TimedLap lap;
        lap.number = row.lapNumber; lap.startTelemetryTime = row.start; lap.endTelemetryTime = row.end;
        lap.durationSeconds = row.end - row.start; lap.referenceIssue = row.referenceIssue;
        lap.userExclusionReason = reasons.value(lapReferenceKey(row.reference));
        if (!lap.userExclusionReason.isEmpty()) matched.insert(lapReferenceKey(row.reference));
        runs[row.runId].timedLaps.append(lap);
    }
    QHash<QString, int> bestNumbers;
    for (auto it = runs.begin(); it != runs.end(); ++it) {
        recomputeLapRanking(it.value());
        if (it->fastestLapIndex) bestNumbers.insert(it.key(), it->timedLaps[*it->fastestLapIndex].number);
    }
    m_outingLapRows.clear();
    m_outingLapMessages.clear();
    for (const auto &message : m_outingSourceMessages)
        m_outingLapMessages.append(message.runId.isEmpty() ? message.text
            : names.value(message.runId) + ": " + message.text);
    const auto unmatched = exclusions.size() - matched.size();
    if (unmatched > 0) m_outingLapMessages.append(QStringLiteral(
        "%1 saved lap exclusion(s) could not be matched to the current recordings; retained without applying.").arg(unmatched));
    for (const auto &row : m_outingRawLapRows) {
        const auto reason = reasons.value(lapReferenceKey(row.reference));
        const QString clock = row.timestampMilliseconds
            ? QDateTime::fromMSecsSinceEpoch(*row.timestampMilliseconds, QTimeZone::UTC).toString("yyyy-MM-dd HH:mm:ss.zzz")
            : QStringLiteral("Time unavailable");
        const QVariantMap item{{"runId", row.runId}, {"runName", row.runName},
            {"type", lapSectionName(row.type)}, {"reference", row.reference.toVariantMap()}, {"lapNumber", row.lapNumber},
            {"startTime", row.start}, {"endTime", row.end}, {"durationSeconds", row.end - row.start}, {"clock", clock},
            {"excluded", !reason.isEmpty()}, {"exclusionReason", reason},
            {"referenceEligible", row.referenceEligible && reason.isEmpty() && row.layoutIssue.isEmpty()},
            {"layoutIssue", row.layoutIssue},
            {"bestOfRun", row.type == LapSectionType::Lap && bestNumbers.value(row.runId, -1) == row.lapNumber},
            {"referenceIssue", row.referenceIssue == LapReferenceIssue::GpsGap ? QStringLiteral("GPS gap")
                : row.referenceIssue == LapReferenceIssue::InvalidGps ? QStringLiteral("Invalid GPS") : QString()},
            {"chronologyKnown", row.timestampMilliseconds.has_value()}};
        m_outingLapRows.append(item);
        if (!m_selectedOutingLap.isEmpty() && m_selectedOutingLap.value("reference") == item.value("reference")) {
            m_selectedOutingLap = item;
            emit outingLapDetailChanged();
            emit outingLapVideoChanged();
        }
    }
    refreshOutingCompatibility();
    emit outingLapsChanged();
}

bool AppController::setRunTrackConfiguration(
    const QString &runId, const QString &layoutId, const QString &direction)
{
    return setRunTrackConfigurations({runId}, layoutId, direction);
}

bool AppController::setRunTrackConfigurations(
    const QStringList &runIds, const QString &layoutId, const QString &direction)
{
    if (!EventProjectCodec::isEvent(m_projectTemplate) || projectLoading() || documentBusy()
        || recoveryPending() || m_batchPending
        || m_documentState.pendingAction() != ProjectDocumentState::DestructiveAction::None
        || m_documentState.revision() == std::numeric_limits<quint64>::max()) return false;
    auto project = currentProjectObject();
    auto event = project.value("event").toObject();
    auto runs = event.value("runs").toArray();
    QSet<QString> found;
    bool changed = false;
    for (qsizetype i = 0; i < runs.size(); ++i) {
        auto run = runs[i].toObject();
        const auto runId = run.value("id").toString();
        if (!runIds.contains(runId)) continue;
        found.insert(runId);
        auto config = EventProjectCodec::trackConfiguration(run);
        config.insert("layoutId", layoutId.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(layoutId));
        config.insert("direction", direction);
        if (config == EventProjectCodec::trackConfiguration(run)) continue;
        run.insert("trackConfiguration", config);
        // Bind a new explicit decision in legacy projects to the verified whole
        // source, without migrating untouched documents on asynchronous load.
        const auto cached = m_outingRunCache.value(runId);
        if (!cached.contentRevision.isEmpty() && cached.dependencyKey == outingRunKey(runId)) {
            auto sources = run.value("sources").toObject(); auto telemetry = sources.value("telemetry").toArray();
            for (qsizetype j = 0; j < telemetry.size(); ++j) {
                auto source = telemetry[j].toObject();
                if (source.value("id") == run.value("primaryTelemetrySourceId")
                    && EventProjectCodec::sourceContentRevision(source).isEmpty()) {
                    source.insert("contentSha256", QString::fromLatin1(cached.contentRevision)); telemetry[j] = source;
                }
            }
            sources.insert("telemetry", telemetry); run.insert("sources", sources);
        }
        runs[i] = run; changed = true;
    }
    if (found.size() != runIds.size()) return false;
    if (!changed) return true;
    event.insert("runs", runs); project.insert("event", event);
    QString error;
    if (!ProjectLimits::validateProject(project, &error)) return false;
    m_projectTemplate = project;
    markPersistentChange();
    return true;
}

QJsonArray AppController::outingLapSources() const
{
    QJsonArray sources;
    const auto project = currentProjectObject();
    for (const auto &value : project.value("event").toObject().value("runs").toArray()) {
        const auto run = value.toObject();
        for (const auto &item : run.value("sources").toObject().value("telemetry").toArray()) {
            const auto source = item.toObject();
            if (source.value("id") != run.value("primaryTelemetrySourceId")) continue;
            sources.append(QJsonObject{{"eventId", project.value("event").toObject().value("id")}, {"runId", run.value("id")}, {"name", run.value("name")},
                {"sourceId", source.value("id")}, {"reference", source.value("reference")},
                {"expectedRevision", QString::fromLatin1(EventProjectCodec::sourceContentRevision(source))},
                {"documentGeneration", QString::number(m_outingDocumentGeneration)},
                {"runGeneration", QString::number(m_outingRunGenerations.value(run.value("id").toString()))},
                {"inference", run.value("trackInference")}, {"inferenceVersion", trackInferenceVersion},
                {"trackConfiguration", EventProjectCodec::trackConfiguration(run)},
                {"derivationKey", QString::fromLatin1(EventProjectCodec::lapDerivationKey(run))}});
        }
    }
    return sources;
}

QByteArray AppController::outingLapKey() const
{
    auto sources = outingLapSources();
    for (qsizetype i = 0; i < sources.size(); ++i) {
        auto source = sources[i].toObject(); source.remove("name"); source.remove("inference"); sources[i] = source;
    }
    return QJsonDocument(QJsonObject{{"document", m_documentId}, {"path", m_documentState.projectPath()},
        {"sources", sources}}).toJson(QJsonDocument::Compact);
}

QByteArray AppController::outingRunKey(const QString &runId) const
{
    for (const auto &value : outingLapSources())
        if (value.toObject().value("runId").toString() == runId) return sourceDependencyKey(value.toObject());
    return {};
}

QSet<QString> AppController::reusableOutingRuns() const
{
    QSet<QString> ids;
    for (const auto &value : outingLapSources()) {
        const auto source = value.toObject(); const auto id = source.value("runId").toString();
        const auto cached = m_outingRunCache.constFind(id);
        if (cached != m_outingRunCache.cend() && cached->dependencyKey == sourceDependencyKey(source)) ids.insert(id);
    }
    return ids;
}

QVariantList AppController::outingLaps() const
{
    if (!outingLapsLoading()) return m_outingLapRows;
    const auto reusable = reusableOutingRuns();
    QVariantList rows;
    for (const auto &value : m_outingLapRows)
        if (reusable.contains(value.toMap().value("runId").toString())) rows.append(value);
    return rows;
}

QVariantMap AppController::outingAnalysisStatus() const
{
    const bool loading = outingLapsLoading();
    const auto reusable = reusableOutingRuns();
    QVariantList runs;
    QStringList notices = loading ? QStringList{} : m_outingLapMessages;
    QString globalError;
    if (!loading) {
        for (const auto &message : m_outingSourceMessages) {
            if (message.runId.isEmpty() && message.state == "error") {
                globalError = message.text;
                notices.removeAll(message.text);
            }
        }
    }
    int sectionCount = 0, readyCount = 0, missingCount = 0, errorCount = 0;
    for (const auto &value : eventRuns()) {
        const auto run = value.toMap();
        const auto id = run.value("id").toString(), name = run.value("name").toString();
        QString state = "empty", message = tr("No recorded sections in this run.");
        int count = 0;
        if (projectLoading() || (loading && !reusable.contains(id))) {
            state = "loading";
            message = tr("Reading recording and detecting laps…");
        } else if (reusable.contains(id)) {
            count = static_cast<int>(m_outingRunCache.value(id).rows.size());
            if (count > 0) { state = "ready"; message = tr("%1 recorded sections available.").arg(count); }
        }
        if (!loading) {
            if (!globalError.isEmpty()) { state = "error"; message = globalError; count = 0; }
            for (const auto &notice : m_outingSourceMessages) {
                if (notice.runId != id || notice.state.isEmpty()) continue;
                state = notice.state; message = notice.text; count = 0;
                notices.removeAll(name + ": " + notice.text);
            }
            if (m_outingStaleRunIds.contains(id)) {
                state = "error"; message = tr("Recording changed; verify or relink its source before analysis."); count = 0;
            }
        }
        sectionCount += count;
        readyCount += state == "ready";
        missingCount += state == "missing-source";
        errorCount += state == "error";
        runs.append(QVariantMap{{"runId", id}, {"runName", name}, {"state", state},
            {"message", message}, {"sectionCount", count}});
    }
    // Partial results remain usable. Ready describes available sections, not
    // ranking eligibility; GPS ambiguity and exclusions have their own policy.
    const QString state = loading ? "loading" : sectionCount > 0 ? "ready"
        : errorCount > 0 || !globalError.isEmpty() ? "error"
        : missingCount > 0 ? "missing-source" : "empty";
    QString message;
    if (state == "loading") message = tr("Updating day results… Available recordings remain inspectable.");
    else if (state == "error") message = tr("Day results unavailable. Review the affected recordings below.");
    else if (state == "missing-source") message = tr("Recordings are missing. Restore their files or relink their sources, then retry.");
    else if (state == "empty") message = tr("No recorded sections available. Add RCZ or VBO recordings; video is optional.");
    else message = tr("%1 of %2 runs available · %3 recorded sections").arg(readyCount).arg(runs.size()).arg(sectionCount);
    return {{"state", state}, {"message", message}, {"runs", runs}, {"notices", notices},
        {"readyRunCount", readyCount}, {"missingRunCount", missingCount}, {"errorRunCount", errorCount},
        {"sectionCount", sectionCount}, {"error", globalError},
        {"partial", sectionCount > 0 && readyCount < runs.size()}};
}

bool AppController::retryOutingAnalysis()
{
    if (outingLapsLoading() || projectLoading() || documentBusy() || recoveryPending()
        || m_batchPending || m_documentState.pendingAction() != ProjectDocumentState::DestructiveAction::None
        || outingLapSources().isEmpty()) return false;
    // Reuse the bounded worker and its full-content checks. Retrying does not
    // select an editor run, alter synchronization or persist an analysis choice.
    m_outingLapRequestedKey.clear();
    refreshOutingLaps();
    return true;
}

void AppController::initializeOutingLaps()
{
    m_outingLapTimer.setSingleShot(true);
    m_outingLapTimer.setInterval(0);
    const auto schedule = [this] { m_outingLapTimer.start(); };
    connect(this, &AppController::documentStateChanged, this, &AppController::refreshLapExclusionPolicy);
    connect(this, &AppController::documentStateChanged, this, schedule);
    connect(this, &AppController::sourceLoadStateChanged, this, schedule);
    // KAN-39: catch-all so outingLapVideoAvailable/outingLapVideoPositionMilliseconds
    // never go stale -- covers active-run switches (documentStateChanged) and video
    // finishing loading/probing (sourceLoadStateChanged), on top of the more specific
    // emits at lap open/close/cursor-move.
    connect(this, &AppController::documentStateChanged, this, &AppController::outingLapVideoChanged);
    connect(this, &AppController::sourceLoadStateChanged, this, &AppController::outingLapVideoChanged);
    connect(&m_outingLapTimer, &QTimer::timeout, this, &AppController::refreshOutingLaps);
    connect(&m_outingLapWatcher, &QFutureWatcher<OutingLapResult>::finished, this, [this] {
        const auto result = m_outingLapWatcher.future().takeResult();
        if (result.cancelled || result.generation != m_sourceGeneration || result.key != outingLapKey()) {
            m_outingLapRequestedKey.clear();
            m_outingLapTimer.start();
            return;
        }
        m_outingStaleRunIds.clear();
        m_outingRunCache = result.runs;
        m_outingInferredGroups = result.groups;
        m_outingRawLapRows = result.rows;
        m_outingSourceMessages = result.messages;
        m_outingLapsLoading = false;
        refreshLapExclusionPolicy();
        if (dirty() && !documentBusy() && !recoveryPending() && !m_suppressDirtyTracking) {
            const auto project = currentProjectObject();
            const auto withInference = projectWithOutingInference(project);
            if (withInference != project) { m_projectTemplate = withInference; markPersistentChange(); }
        }
        if (!m_selectedOutingLap.isEmpty()
            && resolveOutingLapReference(m_selectedOutingLap.value("reference").toMap()).value("state") != "resolved")
            closeOutingLap();
        emit outingLapsChanged();
    });
    // A fresh document has no source-load signal to settle its empty state.
    m_outingLapTimer.start();
}

void AppController::refreshOutingLaps()
{
    const auto key = outingLapKey();
    if (key == m_outingLapRequestedKey && m_sourceGeneration == m_outingLapGeneration) return;
    if (m_outingLapCancellation) m_outingLapCancellation->store(true);
    // Keep unrelated rows and the independently verified open detail. The worker
    // rechecks full content before reusing any cached derivation.
    const auto reusable = reusableOutingRuns();
    m_outingRawLapRows.removeIf([&reusable](const OutingLapRow &row) {
        return !reusable.contains(row.runId);
    });
    m_outingLapRows.removeIf([&reusable](const QVariant &value) {
        const auto runId = value.toMap().value("runId").toString();
        return !reusable.contains(runId);
    });
    m_outingRanking = {{"state", "selection-required"}};
    m_outingProgression = {{"state", "selection-required"}};
    m_outingCompatibilityGroups.clear();
    m_outingSourceMessages.clear();
    m_outingLapMessages.clear();
    const auto sources = outingLapSources();
    m_outingLapsLoading = !sources.isEmpty();
    emit outingLapsChanged();
    // One worker at a time. Its completion schedules the latest source set.
    if (m_outingLapWatcher.isRunning()) return;
    m_outingLapRequestedKey = key;
    m_outingLapGeneration = m_sourceGeneration;
    if (sources.isEmpty()) { emit outingLapsChanged(); return; }
    m_outingLapCancellation = std::make_shared<std::atomic_bool>(false);
    const auto cancellation = m_outingLapCancellation;
    const auto generation = m_sourceGeneration;
    const auto projectPath = m_documentState.projectPath();
    const auto cache = m_outingRunCache;
    m_outingLapWatcher.setFuture(QtConcurrent::run([sources, key, generation, projectPath, cancellation, cache] {
        OutingLapResult result;
        result.key = key;
        result.generation = generation;
        const auto cancelled = [cancellation] { return cancellation->load(); };
        qint64 bytes = 0;
        try {
            if (sources.size() > TelemetryImportLimits{}.maximumFiles)
                throw ResourceLimitError("Too many recordings in this outing.");
            for (qsizetype index = 0; index < sources.size(); ++index) {
                throwIfCancelled(cancelled);
                const auto source = sources[index].toObject();
                const auto runId = source.value("runId").toString();
                QString failureState = "error";
                try {
                    const auto json = source.value("reference").toObject();
                    const ProjectSourceReference reference{json.value("relativePath").toString(),
                        json.value("absolutePath").toString(), json.value("fingerprint").toObject()};
                    const auto path = ProjectSourceReferenceCodec::resolve(reference, projectPath);
                    if (path.isEmpty()) {
                        failureState = "missing-source";
                        throw std::runtime_error("Recording is missing; locate its source to list laps.");
                    }
                    const auto size = QFileInfo(path).size();
                    if (size <= 0 || size > TelemetryImportLimits{}.maximumFileBytes
                        || size > TelemetryImportLimits{}.maximumBatchBytes - bytes)
                        throw ResourceLimitError("Recording exceeds the outing analysis size limit.");
                    bytes += size;
                    const auto contentRevision = TelemetrySource::contentSha256(path, size, cancelled).toHex();
                    const auto expectedRevision = source.value("expectedRevision").toString().toLatin1();
                    if (!expectedRevision.isEmpty() && contentRevision != expectedRevision)
                        throw std::runtime_error("Source identity changed: complete recording content differs; verify/relink this recording.");
                    const auto dependencyKey = sourceDependencyKey(source);
                    auto derived = cache.value(runId);
                    if (derived.dependencyKey == dependencyKey && derived.contentRevision == contentRevision) {
                        if (derived.rows.size() > maximumOutingLapRows - result.rows.size())
                            throw ResourceLimitError("Outing exceeds the 20,000 lap-section limit.");
                        for (auto &row : derived.rows) { row.sourceOrder = index; row.runName = source.value("name").toString(); }
                        throwIfCancelled(cancelled);
                        result.rows.append(derived.rows); result.messages.append(derived.messages);
                        result.runs.insert(runId, std::move(derived));
                        continue;
                    }
                    const auto session = TelemetrySource::load(path, cancelled);
                    throwIfCancelled(cancelled);
                    const auto fingerprint = ProjectSourceReferenceCodec::telemetryFingerprint(path, session);
                    if (ProjectSourceReferenceCodec::compareFingerprints(reference.fingerprint, fingerprint)
                        != SourceFingerprintMatch::Match)
                        throw std::runtime_error("Source identity changed or is unknown; verify/relink this recording.");
                    const auto gateRevision = source.value("trackConfiguration").toObject().value("gateRevision");
                    if (gateRevision.isString() && gateRevision.toString() != timingGateRevision(session, cancelled))
                        throw std::runtime_error("Timing-gate revision changed; verify this recording before analysis.");
                    const auto laps = deriveSourceLapSession(session, {}, cancelled);
                    derived.inference = inferTrack(laps, session.metadata.value("gpsLongitudeConvention") == "west-positive", cancelled);
                    auto rows = outingLapRows(session, laps, source.value("runId").toString(),
                        source.value("name").toString(), index, cancelled);
                    if (rows.size() > maximumOutingLapRows - result.rows.size())
                        throw ResourceLimitError("Outing exceeds the 20,000 lap-section limit.");
                    if (TelemetrySource::contentSha256(path, size, cancelled).toHex() != contentRevision)
                        throw std::runtime_error("Recording changed during lap derivation; reload this source.");
                    for (auto &row : rows) {
                        throwIfCancelled(cancelled);
                        row.reference = makeLapReference(row, source.value("eventId").toString(),
                            source.value("sourceId").toString(), contentRevision,
                            source.value("derivationKey").toString().toLatin1());
                        if (row.reference.isEmpty()) throw std::runtime_error("Cannot identify this lap section.");
                    }
                    derived.dependencyKey = dependencyKey;
                    derived.contentRevision = contentRevision;
                    derived.rows = rows;
                    derived.messages.clear();
                    ++derived.derivationSerial;
                    if (!recordingTimestamp(session)) derived.messages.append(OutingSourceMessage{
                        source.value("runId").toString(), "recording date/time unavailable; listed after chronological records in import order."});
                    if (laps.acceptedPasses.isEmpty()) derived.messages.append(OutingSourceMessage{
                        source.value("runId").toString(), "no reliable start/finish passages; lap type is unknown."});
                    result.rows.append(rows); result.messages.append(derived.messages);
                    result.runs.insert(runId, std::move(derived));
                } catch (const OperationCancelled &) { throw; }
                catch (const std::exception &error) {
                    result.messages.append(OutingSourceMessage{runId, QString::fromUtf8(error.what()), failureState});
                }
            }
            throwIfCancelled(cancelled);
            QHash<QString, TrackInference> inferences;
            QSet<QString> manualRuns;
            auto groupSources = sources;
            for (qsizetype i = 0; i < groupSources.size(); ++i) {
                auto source = groupSources[i].toObject(); const auto id = source.value("runId").toString();
                if (manualTrackConfiguration(source.value("trackConfiguration").toObject())) manualRuns.insert(id);
                if (!result.runs.contains(id)) continue;
                const auto &run = result.runs[id]; inferences.insert(id, run.inference);
                source.insert("expectedRevision", QString::fromLatin1(run.contentRevision)); groupSources[i] = source;
                if (!run.inference.supported() && !manualTrackConfiguration(source.value("trackConfiguration").toObject()))
                    result.messages.append(OutingSourceMessage{id, run.inference.reason});
            }
            result.groups = groupInferredTracks(inferences, groupSources, cancelled);
            for (auto it = result.groups.reasons.cbegin(); it != result.groups.reasons.cend(); ++it)
                if (!manualTrackConfiguration(result.groups.configurations.value(it.key())))
                    result.messages.append(OutingSourceMessage{it.key(), it.value()});
            for (auto &row : result.rows) {
                const auto config = result.groups.configurations.value(row.runId);
                // Manual overrides retain their established eligibility semantics.
                const auto inference = inferences.value(row.runId);
                if (row.type == LapSectionType::Lap && row.referenceEligible && !manualRuns.contains(row.runId)
                    && config.value("layoutId").toString().startsWith("gps-route-v1:")
                    && !inference.matchingLaps.contains(row.lapNumber)) row.layoutIssue = "different-recorded-route";
            }
            sortOutingLaps(result.rows);
        } catch (const OperationCancelled &) { result.cancelled = true; result.rows.clear(); result.runs.clear(); }
        catch (const std::exception &error) {
            result.rows.clear(); result.runs.clear(); result.groups = {};
            result.messages.append(OutingSourceMessage{{}, QString::fromUtf8(error.what()), "error"});
        }
        return result;
    }));
}

QJsonObject AppController::projectWithOutingInference(QJsonObject project) const
{
    auto event = project.value("event").toObject(); auto runs = event.value("runs").toArray();
    for (qsizetype i = 0; i < runs.size(); ++i) {
        auto run = runs[i].toObject(); const auto inference = m_outingInferredGroups.provenance.value(run.value("id").toString());
        if (inference.isEmpty()) continue;
        for (const auto &value : run.value("sources").toObject().value("telemetry").toArray()) {
            const auto source = value.toObject();
            if (source.value("id") == run.value("primaryTelemetrySourceId")
                && EventProjectCodec::sourceContentRevision(source) == inference.value("sourceRevision").toString().toLatin1()
                && EventProjectCodec::trackConfiguration(run).value("gateRevision") == inference.value("gateRevision"))
                run.insert("trackInference", inference);
        }
        runs[i] = run;
    }
    if (!event.isEmpty()) { event.insert("runs", runs); project.insert("event", event); }
    return project;
}

void AppController::initializeOutingLapDetail()
{
    m_outingLapDetailTimer.setSingleShot(true);
    m_outingLapDetailTimer.setInterval(0);
    connect(&m_outingLapDetailTimer, &QTimer::timeout, this, &AppController::loadOutingLapDetail);
    connect(this, &AppController::documentStateChanged, this, &AppController::invalidateOutingLapDetail);
    connect(this, &AppController::sourceLoadStateChanged, this, &AppController::invalidateOutingLapDetail);
    connect(&m_outingLapDetailWatcher, &QFutureWatcher<OutingLapDetailResult>::finished, this, [this] {
        auto result = m_outingLapDetailWatcher.future().takeResult();
        m_outingLapDetailPending = false;
        if (result.request != m_outingLapDetailRequest) {
            if (m_outingLapDetailState == "loading") m_outingLapDetailTimer.start();
            return;
        }
        if (m_selectedOutingLap.isEmpty()
            || m_outingLapDetailKey != outingRunKey(m_selectedOutingLap.value("runId").toString())) { closeOutingLap(); return; }
        m_outingLapDetailSession = std::move(result.session);
        m_outingLapDetailGeometry = std::move(result.geometry);
        m_outingLapTrack = std::move(result.track);
        m_outingLapDetailError = result.error;
        if (result.staleReference) {
            m_outingStaleRunIds.insert(m_selectedOutingLap.value("runId").toString());
            refreshOutingCompatibility();
            emit outingLapsChanged();
        }
        m_outingLapDetailState = m_outingLapDetailSession ? "ready" : "error";
        m_outingLapChannels.clear();
        if (m_outingLapDetailSession) {
            if (m_settings.contains("analysis/lapChannels")) {
                const auto preferred = m_settings.value("analysis/lapChannels").toStringList();
                for (const auto &name : preferred) {
                    if (m_outingLapDetailSession->channels.contains(name) && !m_outingLapChannels.contains(name))
                        m_outingLapChannels.append(name);
                    if (m_outingLapChannels.size() == 4) break;
                }
            } else {
                for (const auto *alias : {"speed", "lateralAcceleration", "longitudinalAcceleration"}) {
                    const auto name = m_outingLapDetailSession->aliases.value(alias, alias);
                    if (m_outingLapDetailSession->channels.contains(name) && !m_outingLapChannels.contains(name))
                        m_outingLapChannels.append(name);
                }
            }
            if (m_outingLapDetailSession->channels.contains(m_outingLapPendingChannel)) {
                m_outingLapChannels.removeAll(m_outingLapPendingChannel);
                m_outingLapChannels.prepend(m_outingLapPendingChannel);
                while (m_outingLapChannels.size() > 4) m_outingLapChannels.removeLast();
            }
        }
        m_outingLapPendingChannel.clear();
        emit outingLapDetailChanged();
        emit outingLapCursorChanged();
        emit outingLapVideoChanged();
    });
}

void AppController::invalidateOutingLapDetail()
{
    if (!m_selectedOutingLap.isEmpty()
        && m_outingLapDetailKey != outingRunKey(m_selectedOutingLap.value("runId").toString())) closeOutingLap();
}

QVariantMap AppController::resolveOutingLapReference(const QVariantMap &value) const
{
    const auto reference = QJsonObject::fromVariantMap(value);
    const auto result = [](const char *state, const char *reason) {
        return QVariantMap{{"state", state}, {"reason", reason}};
    };
    if (!validLapReference(reference)) return result("invalid", "Malformed or unsupported lap reference.");
    if (reference.value("algorithm").toString() != lapReferenceAlgorithm)
        return result("stale", "Lap derivation algorithm changed.");
    const auto project = currentProjectObject();
    const auto event = project.value("event").toObject();
    if (reference.value("eventId") != event.value("id"))
        return result("stale", "Lap reference belongs to another event.");
    QJsonObject run;
    for (const auto &candidate : event.value("runs").toArray())
        if (candidate.toObject().value("id") == reference.value("runId")) run = candidate.toObject();
    if (run.isEmpty()) return result("stale", "Referenced run no longer exists.");
    if (run.value("primaryTelemetrySourceId") != reference.value("sourceId"))
        return result("stale", "Primary telemetry source changed.");
    if (QString::fromLatin1(EventProjectCodec::lapDerivationKey(run)) != reference.value("derivationKey").toString())
        return result("stale", "Source or track/gate configuration changed.");
    if (m_outingStaleRunIds.contains(reference.value("runId").toString()))
        return result("stale", "Recording content changed since the last lap derivation.");
    // A document edit can precede the refresh timer: never search yesterday's rows.
    if (projectLoading() || m_outingLapsLoading || m_outingLapRequestedKey != outingLapKey()
        || m_outingLapGeneration != m_sourceGeneration)
        return result("loading", "Current lap derivation is not ready.");
    int match = -1;
    bool runAvailable = false;
    for (qsizetype i = 0; i < m_outingLapRows.size(); ++i) {
        const auto row = m_outingLapRows[i].toMap();
        runAvailable |= row.value("runId").toString() == reference.value("runId").toString();
        if (QJsonObject::fromVariantMap(row.value("reference").toMap()) != reference) continue;
        if (match >= 0) return result("stale", "Lap reference is ambiguous in this derivation.");
        match = static_cast<int>(i);
    }
    if (match >= 0) return {{"state", "resolved"}, {"index", match}};
    return runAvailable ? result("stale", "Source content or lap boundaries changed.")
                        : result("unavailable", "Referenced recording has no available lap derivation.");
}

bool AppController::selectOutingLapReference(const QVariantMap &reference)
{
    const auto resolved = resolveOutingLapReference(reference);
    return resolved.value("state").toString() == "resolved" && selectOutingLap(resolved.value("index").toInt());
}

bool AppController::openOutingLapChannel(const QVariantMap &reference, const QString &channel)
{
    if (!selectOutingLapReference(reference)) return false;
    // Applied when the lap's recording has loaded; selecting clears it.
    m_outingLapPendingChannel = channel;
    return true;
}

bool AppController::selectOutingLap(int index)
{
    if (index < 0 || index >= m_outingLapRows.size() || m_outingLapsLoading || projectLoading()
        || m_outingLapRequestedKey != outingLapKey() || m_outingLapGeneration != m_sourceGeneration
        || recoveryPending() || m_documentState.pendingAction() != ProjectDocumentState::DestructiveAction::None)
        return false;
    const auto row = m_outingLapRows[index].toMap();
    QJsonObject source;
    for (const auto &value : outingLapSources())
        if (value.toObject().value("runId").toString() == row.value("runId").toString()) source = value.toObject();
    if (source.isEmpty()) return false;
    closeOutingLap();
    m_selectedOutingLap = row;
    m_outingLapDetailSource = source;
    m_outingLapDetailKey = outingRunKey(row.value("runId").toString());
    m_outingLapCursor = row.value("startTime").toDouble();
    m_outingLapDetailState = "loading";
    m_outingLapDetailTimer.start();
    emit outingLapDetailChanged();
    emit outingLapCursorChanged();
    emit outingLapVideoChanged();
    return true;
}

void AppController::closeOutingLap()
{
    ++m_outingLapDetailRequest;
    if (m_outingLapDetailCancellation) m_outingLapDetailCancellation->store(true);
    m_outingLapDetailTimer.stop();
    m_selectedOutingLap.clear();
    m_outingLapDetailSession.reset();
    m_outingLapDetailGeometry = {};
    m_outingLapTrack.clear();
    m_outingLapChannels.clear();
    m_outingLapPendingChannel.clear();
    m_outingLapDetailState = "idle";
    m_outingLapDetailError.clear();
    resetSegmentReview();
    emit outingLapDetailChanged();
    emit outingLapCursorChanged();
    emit outingLapVideoChanged();
}

void AppController::loadOutingLapDetail()
{
    // A rapid second click cancels the current parse and waits for it to finish;
    // it cannot create concurrent parsers with multiplied source-memory budgets.
    if (m_selectedOutingLap.isEmpty() || m_outingLapDetailPending) return;
    const auto source = m_outingLapDetailSource;
    const auto projectPath = m_documentState.projectPath();
    const auto row = m_selectedOutingLap;
    const auto request = m_outingLapDetailRequest;
    m_outingLapDetailCancellation = std::make_shared<std::atomic_bool>(false);
    const auto cancellation = m_outingLapDetailCancellation;
    m_outingLapDetailPending = true;
    m_outingLapDetailWatcher.setFuture(QtConcurrent::run([source, projectPath, row, request, cancellation, cache = m_analysisSourceCache] {
        return readOutingLapDetail(source, projectPath, row, request, cancellation, cache);
    }));
}

AppController::OutingLapDetailResult AppController::readOutingLapDetail(const QJsonObject &source,
    const QString &projectPath, const QVariantMap &row, const quint64 request,
    const std::shared_ptr<std::atomic_bool> &cancellation, const std::shared_ptr<TelemetrySessionCache> &cache,
    const bool deriveReferenceGate)
{
    const auto start = row.value("startTime").toDouble();
    const auto end = row.value("endTime").toDouble();
    const auto lapReference = QJsonObject::fromVariantMap(row.value("reference").toMap());
    OutingLapDetailResult result; result.request = request;
    const auto cancelled = [cancellation] { return cancellation->load(); };
    try {
        throwIfCancelled(cancelled);
        const auto json = source.value("reference").toObject();
        const ProjectSourceReference reference{json.value("relativePath").toString(),
            json.value("absolutePath").toString(), json.value("fingerprint").toObject()};
        const auto path = ProjectSourceReferenceCodec::resolve(reference, projectPath);
        if (path.isEmpty()) throw std::runtime_error("Recording is missing. Relink its source and try again.");
        const auto bytes = QFileInfo(path).size();
        if (bytes <= 0 || bytes > TelemetryImportLimits{}.maximumFileBytes)
            throw ResourceLimitError("Recording exceeds the analysis size limit.");
        const auto contentRevision = TelemetrySource::contentSha256(path, bytes, cancelled).toHex();
        if (!validLapReference(lapReference)
            || contentRevision != lapReference.value("sourceRevision").toString().toLatin1()) {
            result.staleReference = true;
            throw std::runtime_error("Lap reference is stale: recording content changed. Reload this source.");
        }
        const auto cacheKey = QCryptographicHash::hash(QJsonDocument(QJsonObject{
            {"fingerprint", reference.fingerprint}, {"content", QString::fromLatin1(contentRevision)},
            {"derivation", lapReference.value("derivationKey")}, {"algorithm", lapReference.value("algorithm")},
            {"format", QFileInfo(path).suffix().toLower()}}).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256);
        const auto session = cache->load(cacheKey, cancelled,
            [&](qint64 available) { return TelemetrySource::load(path, cancelled, available); },
            [&](const TelemetrySession &candidate) {
                if (TelemetrySource::contentSha256(path, bytes, cancelled).toHex() != contentRevision) {
                    result.staleReference = true;
                    throw std::runtime_error("Lap reference is stale: recording changed while opening it.");
                }
                if (ProjectSourceReferenceCodec::compareFingerprints(reference.fingerprint,
                    ProjectSourceReferenceCodec::telemetryFingerprint(path, candidate)) != SourceFingerprintMatch::Match)
                    throw std::runtime_error("Recording changed. Relink its source before opening this lap.");
            });
        throwIfCancelled(cancelled);
        if (!std::isfinite(start) || !std::isfinite(end) || start < 0 || end <= start || end > session->duration)
            throw std::runtime_error("Lap range is no longer valid for this recording.");
        // Build map bounds only from this section. Keep gaps as separate
        // polylines; the dynamic marker shares the same normalization.
        const auto latitude = session->sampledSegments("latitude", start, end, 2000);
        TelemetrySession mapSession;
        mapSession.metadata.insert("gpsLongitudeConvention", session->metadata.value("gpsLongitudeConvention"));
        mapSession.aliases = {{"latitude", "lat"}, {"longitude", "lon"}};
        mapSession.channels.insert("lat", {}); mapSession.channels.insert("lon", {});
        auto &lat = mapSession.channels["lat"]; auto &lon = mapSession.channels["lon"];
        for (const auto &segment : latitude) {
            for (const auto &point : segment) {
                throwIfCancelled(cancelled);
                const auto longitude = session->valueAt("longitude", point.x());
                if (!longitude) continue;
                lat.values.append(static_cast<float>(point.y()));
                lon.values.append(static_cast<float>(*longitude));
            }
        }
        result.geometry = buildTrackGeometry(mapSession, cancelled);
        result.track = buildTrackSegments(*session, start, end, result.geometry, cancelled);
        if (deriveReferenceGate) {
            // Only the comparison path needs this: a whole-file gate/lap scan
            // to source buildProgressAxis's ingredients, not cheap enough to
            // redo synchronously per slot, so it happens here alongside the
            // source load/verification this worker already does.
            const auto laps = deriveSourceLapSession(*session, {}, cancelled);
            if (laps.selectedStartGate) {
                const int lapNumber = row.value("lapNumber").toInt();
                const auto traceIt = std::find_if(laps.lapTraces.cbegin(), laps.lapTraces.cend(),
                    [lapNumber](const LapTrace &trace) { return trace.lapNumber == lapNumber; });
                if (traceIt != laps.lapTraces.cend()) {
                    result.referenceTrace = *traceIt;
                    result.referenceGate = *laps.selectedStartGate;
                    result.hasReferenceGate = true;
                }
            }
        }
        throwIfCancelled(cancelled);
        result.session = std::move(session);
    } catch (const std::exception &error) {
        result.error = QString::fromUtf8(error.what()); result.track.clear(); result.geometry = {};
    }
    return result;
}

QStringList AppController::outingLapAvailableChannels() const
{
    return m_outingLapDetailSession ? m_outingLapDetailSession->channelNames() : QStringList{};
}

void AppController::setOutingLapChannels(const QStringList &channels)
{
    if (!m_outingLapDetailSession || m_outingLapDetailState != "ready") return;
    QStringList selected;
    for (const auto &name : channels) {
        if (m_outingLapDetailSession->channels.contains(name) && !selected.contains(name))
            selected.append(name);
        if (selected.size() == 4) break;
    }
    // Analysis preferences do not alter the editor document or recording data.
    m_settings.setValue("analysis/lapChannels", selected);
    if (selected == m_outingLapChannels) return;
    m_outingLapChannels = selected;
    emit outingLapDetailChanged();
}

QVariantMap AppController::outingLapSeries(const QString &channel, const int maximumPoints) const
{
    return outingLapSeries(channel, m_selectedOutingLap.value("startTime").toDouble(),
        m_selectedOutingLap.value("endTime").toDouble(), maximumPoints);
}

QVariantMap AppController::outingLapSeries(
    const QString &channel, const double startTime, const double endTime, const int maximumPoints) const
{
    if (!m_outingLapDetailSession || maximumPoints < 2) return {};
    return sessionSeries(*m_outingLapDetailSession, channel, startTime, endTime, maximumPoints);
}

QString AppController::outingLapValueText(const QString &channel) const
{
    const auto value = m_outingLapDetailSession ? m_outingLapDetailSession->valueAt(channel, m_outingLapCursor) : std::nullopt;
    return value ? QString::number(*value, 'f', 2) : QStringLiteral("—");
}

QVariantMap AppController::outingLapTrackPoint() const
{
    if (!m_outingLapDetailSession) return {};
    const auto point = FlappedEar::currentTrackPoint(*m_outingLapDetailSession, m_outingLapCursor, m_outingLapDetailGeometry);
    return point ? QVariantMap{{"x", point->x()}, {"y", point->y()}} : QVariantMap{};
}

void AppController::setOutingLapCursor(double seconds)
{
    if (!m_outingLapDetailSession || !std::isfinite(seconds)) return;
    seconds = std::clamp(seconds, m_selectedOutingLap.value("startTime").toDouble(), m_selectedOutingLap.value("endTime").toDouble());
    if (seconds == m_outingLapCursor) return;
    m_outingLapCursor = seconds;
    emit outingLapCursorChanged();
    emit outingLapVideoChanged();
}

// KAN-39: the open lap's video, through the VideoLink (KAN-124).
bool AppController::outingLapVideoAvailable() const
{
    if (m_selectedOutingLap.isEmpty() || !m_videoLink) return false;
    return m_videoLink->videoPositionForTelemetry(m_selectedOutingLap.value("runId").toString(), m_outingLapCursor).has_value();
}

qint64 AppController::outingLapVideoPositionMilliseconds() const
{
    if (m_selectedOutingLap.isEmpty() || !m_videoLink) return 0;
    return m_videoLink->videoPositionForTelemetry(m_selectedOutingLap.value("runId").toString(), m_outingLapCursor).value_or(0);
}

bool AppController::followOutingLapVideoPosition(const qint64 videoPositionMilliseconds)
{
    if (m_selectedOutingLap.isEmpty() || !m_videoLink) return false;
    const auto telemetryTime = m_videoLink->telemetryForVideoPosition(
        m_selectedOutingLap.value("runId").toString(), videoPositionMilliseconds);
    if (!telemetryTime) return false;
    setOutingLapCursor(*telemetryTime);
    return true;
}

// KAN-124: AppController's VideoLink (the overlay side). The lap's run must be
// the active, loaded one; a lap from another run has no video rather than
// silently switching runs. Out-of-range footage is unavailable, never clamped.
std::optional<qint64> AppController::videoPositionForTelemetry(const QString &runId, const double telemetrySeconds) const
{
    if (m_videoSource.isEmpty() || runId != activeRunId()) return std::nullopt;
    const auto videoTime = FlappedEar::telemetryToVideoTime(telemetrySeconds, m_sync);
    if (!videoTime || *videoTime < 0.0) return std::nullopt;
    const auto milliseconds = qRound64(*videoTime * 1000.0);
    if (milliseconds > previewEndPositionMilliseconds()) return std::nullopt;
    return clampPreviewPositionMilliseconds(milliseconds);
}

std::optional<double> AppController::telemetryForVideoPosition(const QString &runId, const qint64 videoMilliseconds) const
{
    if (runId != activeRunId()) return std::nullopt;
    return FlappedEar::videoToTelemetryTime(videoMilliseconds / 1000.0, m_sync);
}

} // namespace FlappedEar
