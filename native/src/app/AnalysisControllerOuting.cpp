#include "app/AnalysisController.h"
#include "telemetry/OutingLapDerivation.h"
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
}

QVariantMap AnalysisController::runMetadata(const QString &runId) const
{
    const auto event = m_document.analysisProject().value("event").toObject();
    for (const auto &value : event.value("runs").toArray()) {
        const auto run = value.toObject();
        if (run.value("id").toString() != runId) continue;
        QJsonObject metadata{{"name", run.value("name")}};
        for (const auto *key : {"notes", "conditions", "setupChanges"})
            metadata.insert(key, run.contains(key) ? run.value(key) : QJsonValue(QJsonValue::Null));
        const auto token = QCryptographicHash::hash(QJsonDocument(QJsonObject{
            {"documentId", m_document.documentIdentity()}, {"run", run}}).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex();
        metadata.insert("editToken", QString::fromLatin1(token));
        return metadata.toVariantMap();
    }
    return {};
}

bool AnalysisController::updateRunMetadata(const QString &runId, const QString &expectedToken,
    const QString &name, const QString &notes, const QString &conditions, const QString &setupChanges)
{
    if (!documentEditable()) return false;
    const auto current = runMetadata(runId);
    if (current.isEmpty() || expectedToken.isEmpty() || current.value("editToken").toString() != expectedToken
        || name.trimmed().isEmpty() || name.size() > ProjectLimits::maximumTemplateNameCharacters) return false;
    for (const auto &text : {name, notes, conditions, setupChanges})
        if (text.size() > ProjectLimits::maximumStringCharacters || text.contains(QChar::Null)) return false;
    auto project = m_document.analysisProject(); auto event = project.value("event").toObject();
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
        m_document.commitAnalysisProject(project);
        return true;
    }
    return false;
}

QVariantMap AnalysisController::runTrackConfiguration(const QString &runId) const
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

bool AnalysisController::confirmRunTrackConfiguration(const QString &runId, const QString &expectedDerivationKey,
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

QVariantMap AnalysisController::outingRanking() const
{
    if (m_document.projectLoading() || m_outingLapsLoading || m_outingLapRequestedKey != outingLapKey()
        || m_outingLapGeneration != sourceGeneration()) return {{"state", "loading"}};
    return m_outingRanking;
}

QVariantMap AnalysisController::outingProgression() const
{
    if (m_document.projectLoading() || m_outingLapsLoading || m_outingLapRequestedKey != outingLapKey()
        || m_outingLapGeneration != sourceGeneration()) return {{"state", "loading"}};
    return m_outingProgression;
}

QVariantList AnalysisController::outingCompatibilityGroups() const
{
    if (m_document.projectLoading() || m_outingLapsLoading || m_outingLapRequestedKey != outingLapKey()
        || m_outingLapGeneration != sourceGeneration()) return {};
    return m_outingCompatibilityGroups;
}

QString AnalysisController::outingComparisonGroupId() const
{
    for (const auto &value : outingCompatibilityGroups())
        if (value.toMap().value("id").toString() == m_outingComparisonGroupId
            && value.toMap().value("available").toBool()) return m_outingComparisonGroupId;
    return {};
}

QString AnalysisController::outingComparisonSelectionState() const
{
    const auto saved = m_document.analysisProject().value("event").toObject().value("analysisDecisions")
        .toObject().value("comparisonGroupId").toString();
    if (saved.isEmpty()) return outingComparisonGroupId().isEmpty() ? "none" : "automatic";
    if (m_document.projectLoading() || m_outingLapsLoading || m_outingLapRequestedKey != outingLapKey()
        || m_outingLapGeneration != sourceGeneration()) return "loading";
    return outingComparisonGroupId() == saved ? "applied" : "unavailable";
}

bool AnalysisController::selectOutingComparisonGroup(const QString &groupId)
{
    if (!documentEditable()) return false;
    if (!groupId.isEmpty()) {
        if (m_outingLapsLoading || m_outingLapRequestedKey != outingLapKey()
            || m_outingLapGeneration != sourceGeneration()) return false;
        bool found = false;
        for (const auto &value : m_outingCompatibilityGroups) {
            const auto group = value.toMap();
            found |= group.value("id").toString() == groupId && group.value("resolved").toBool() && group.value("available").toBool();
        }
        if (!found) return false;
    }
    auto project = m_document.analysisProject(); auto event = project.value("event").toObject();
    auto decisions = event.value("analysisDecisions").toObject();
    if (decisions.value("comparisonGroupId").toString() == groupId) return true;
    decisions.insert("comparisonGroupId", groupId.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(groupId));
    event.insert("analysisDecisions", decisions); project.insert("event", event);
    if (!ProjectLimits::validateProject(project)) return false;
    m_document.commitAnalysisProject(project);
    emit outingLapsChanged();
    return true;
}

void AnalysisController::refreshOutingCompatibility()
{
    if (m_outingLapRequestedKey != outingLapKey() || m_outingLapGeneration != sourceGeneration()) return;
    m_outingComparisonGroupId = m_document.analysisProject().value("event").toObject()
        .value("analysisDecisions").toObject().value("comparisonGroupId").toString();
    const auto configurations = outingRunConfigurations(outingLapSources(), m_outingInferredGroups);
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
    // KAN-185: the same choice the editor's automatic best lap makes.
    m_outingComparisonGroupId = outingComparisonGroup(m_outingRawLapRows, configurations,
        m_outingComparisonGroupId, m_outingStaleRunIds);
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
        m_document.analysisProject().value("event").toObject().value("lapExclusions").toArray(), m_outingStaleRunIds).toVariantMap();
    m_outingRanking.insert("groupLabel", groups.value(m_outingComparisonGroupId).value("label"));
    QJsonArray metadata;
    for (const auto &value : m_document.analysisProject().value("event").toObject().value("runs").toArray()) {
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
                m_document.analysisProject().value("event").toObject().value("lapExclusions").toArray(), m_outingStaleRunIds);
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

bool AnalysisController::setOutingLapExcluded(const QVariantMap &referenceMap, bool excluded, const QString &reason)
{
    if (!documentEditable()) return false;
    const auto reference = QJsonObject::fromVariantMap(referenceMap);
    if (!validLapReference(reference) || reference.value("type") != "LAP") return false;
    if (excluded && (resolveOutingLapReference(referenceMap).value("state").toString() != "resolved"
        || reason.trimmed().isEmpty() || reason.size() > 256 || reason.contains(QChar::Null))) return false;
    auto project = m_document.analysisProject();
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
    m_document.commitAnalysisProject(project);
    return true;
}

void AnalysisController::refreshLapExclusionPolicy()
{
    const auto event = m_document.analysisProject().value("event").toObject();
    const auto exclusions = event.value("lapExclusions").toArray();
    emit lapExclusionsRefreshing();
    if (m_outingLapRequestedKey != outingLapKey() || m_outingLapGeneration != sourceGeneration()) return;
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

bool AnalysisController::setRunTrackConfiguration(
    const QString &runId, const QString &layoutId, const QString &direction)
{
    return setRunTrackConfigurations({runId}, layoutId, direction);
}

bool AnalysisController::setRunTrackConfigurations(
    const QStringList &runIds, const QString &layoutId, const QString &direction)
{
    if (!documentEditable()) return false;
    auto project = m_document.analysisProject();
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
    m_document.commitAnalysisProject(project);
    return true;
}

QJsonArray AnalysisController::outingLapSources() const
{
    // The document's sources plus this controller's generations, which force a
    // fresh derivation after sources are replaced or a run is relinked.
    auto sources = EventProjectCodec::outingLapSources(m_document.analysisProject());
    for (qsizetype i = 0; i < sources.size(); ++i) {
        auto source = sources[i].toObject();
        source.insert("documentGeneration", QString::number(m_outingDocumentGeneration));
        source.insert("runGeneration", QString::number(m_outingRunGenerations.value(source.value("runId").toString())));
        sources[i] = source;
    }
    return sources;
}

QByteArray AnalysisController::outingLapKey() const
{
    auto sources = outingLapSources();
    for (qsizetype i = 0; i < sources.size(); ++i) {
        auto source = sources[i].toObject(); source.remove("name"); source.remove("inference"); sources[i] = source;
    }
    return QJsonDocument(QJsonObject{{"document", m_document.documentIdentity()}, {"path", m_document.documentProjectPath()},
        {"sources", sources}}).toJson(QJsonDocument::Compact);
}

QByteArray AnalysisController::outingRunKey(const QString &runId) const
{
    for (const auto &value : outingLapSources())
        if (value.toObject().value("runId").toString() == runId) return outingSourceDependencyKey(value.toObject());
    return {};
}

QSet<QString> AnalysisController::reusableOutingRuns() const
{
    QSet<QString> ids;
    for (const auto &value : outingLapSources()) {
        const auto source = value.toObject(); const auto id = source.value("runId").toString();
        const auto cached = m_outingRunCache.constFind(id);
        if (cached != m_outingRunCache.cend() && cached->dependencyKey == outingSourceDependencyKey(source)) ids.insert(id);
    }
    return ids;
}

QVariantList AnalysisController::outingLaps() const
{
    if (!outingLapsLoading()) return m_outingLapRows;
    const auto reusable = reusableOutingRuns();
    QVariantList rows;
    for (const auto &value : m_outingLapRows)
        if (reusable.contains(value.toMap().value("runId").toString())) rows.append(value);
    return rows;
}

QVariantMap AnalysisController::outingAnalysisStatus() const
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
    for (const auto &value : m_document.eventRuns()) {
        const auto run = value.toMap();
        const auto id = run.value("id").toString(), name = run.value("name").toString();
        QString state = "empty", message = tr("No recorded sections in this run.");
        int count = 0;
        if (m_document.projectLoading() || (loading && !reusable.contains(id))) {
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

bool AnalysisController::retryOutingAnalysis()
{
    if (outingLapsLoading() || m_document.projectLoading() || m_document.documentBusy() || m_document.recoveryPending()
        || m_document.batchImportPending() || m_document.destructiveActionPending()
        || outingLapSources().isEmpty()) return false;
    // Reuse the bounded worker and its full-content checks. Retrying does not
    // select an editor run, alter synchronization or persist an analysis choice.
    m_outingLapRequestedKey.clear();
    refreshOutingLaps();
    return true;
}

void AnalysisController::initializeOutingLaps()
{
    m_outingLapTimer.setSingleShot(true);
    m_outingLapTimer.setInterval(0);
    const auto schedule = [this] { m_outingLapTimer.start(); };
    connect(this, &AnalysisController::documentStateChanged, this, &AnalysisController::refreshLapExclusionPolicy);
    connect(this, &AnalysisController::documentStateChanged, this, schedule);
    connect(this, &AnalysisController::sourceLoadStateChanged, this, schedule);
    // KAN-39: catch-all so outingLapVideoAvailable/outingLapVideoPositionMilliseconds
    // never go stale -- covers active-run switches (documentStateChanged) and video
    // finishing loading/probing (sourceLoadStateChanged), on top of the more specific
    // emits at lap open/close/cursor-move.
    connect(this, &AnalysisController::documentStateChanged, this, &AnalysisController::outingLapVideoChanged);
    connect(this, &AnalysisController::sourceLoadStateChanged, this, &AnalysisController::outingLapVideoChanged);
    connect(&m_outingLapTimer, &QTimer::timeout, this, &AnalysisController::refreshOutingLaps);
    connect(&m_outingLapWatcher, &QFutureWatcher<OutingLapResult>::finished, this, [this] {
        const auto result = m_outingLapWatcher.future().takeResult();
        if (result.cancelled || result.generation != sourceGeneration() || result.key != outingLapKey()) {
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
        if (m_document.dirty() && !m_document.documentBusy() && !m_document.recoveryPending() && !m_document.dirtyTrackingSuppressed()) {
            const auto project = m_document.analysisProject();
            const auto withInference = projectWithOutingInference(project);
            if (withInference != project) { m_document.commitAnalysisProject(withInference); }
        }
        if (!m_selectedOutingLap.isEmpty()
            && resolveOutingLapReference(m_selectedOutingLap.value("reference").toMap()).value("state") != "resolved")
            closeOutingLap();
        emit outingLapsChanged();
    });
    // A fresh document has no source-load signal to settle its empty state.
    m_outingLapTimer.start();
}

void AnalysisController::refreshOutingLaps()
{
    const auto key = outingLapKey();
    if (key == m_outingLapRequestedKey && sourceGeneration() == m_outingLapGeneration) return;
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
    m_outingLapGeneration = sourceGeneration();
    if (sources.isEmpty()) { emit outingLapsChanged(); return; }
    m_outingLapCancellation = std::make_shared<std::atomic_bool>(false);
    const auto cancellation = m_outingLapCancellation;
    const auto generation = sourceGeneration();
    const auto projectPath = m_document.documentProjectPath();
    const auto cache = m_outingRunCache;
    m_outingLapWatcher.setFuture(QtConcurrent::run([sources, key, generation, projectPath, cancellation, cache] {
        OutingLapResult result;
        static_cast<OutingLapDerivation &>(result) = deriveOutingLaps(sources, projectPath, cache, cancellation);
        result.key = key;
        result.generation = generation;
        return result;
    }));
}

QJsonObject AnalysisController::projectWithOutingInference(QJsonObject project) const
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

void AnalysisController::initializeOutingLapDetail()
{
    m_outingLapDetailTimer.setSingleShot(true);
    m_outingLapDetailTimer.setInterval(0);
    connect(&m_outingLapDetailTimer, &QTimer::timeout, this, &AnalysisController::loadOutingLapDetail);
    connect(this, &AnalysisController::documentStateChanged, this, &AnalysisController::invalidateOutingLapDetail);
    connect(this, &AnalysisController::sourceLoadStateChanged, this, &AnalysisController::invalidateOutingLapDetail);
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

void AnalysisController::invalidateOutingLapDetail()
{
    if (!m_selectedOutingLap.isEmpty()
        && m_outingLapDetailKey != outingRunKey(m_selectedOutingLap.value("runId").toString())) closeOutingLap();
}

QVariantMap AnalysisController::resolveOutingLapReference(const QVariantMap &value) const
{
    const auto reference = QJsonObject::fromVariantMap(value);
    const auto result = [](const char *state, const char *reason) {
        return QVariantMap{{"state", state}, {"reason", reason}};
    };
    if (!validLapReference(reference)) return result("invalid", "Malformed or unsupported lap reference.");
    if (reference.value("algorithm").toString() != lapReferenceAlgorithm)
        return result("stale", "Lap derivation algorithm changed.");
    const auto project = m_document.analysisProject();
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
    if (m_document.projectLoading() || m_outingLapsLoading || m_outingLapRequestedKey != outingLapKey()
        || m_outingLapGeneration != sourceGeneration())
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

bool AnalysisController::selectOutingLapReference(const QVariantMap &reference)
{
    const auto resolved = resolveOutingLapReference(reference);
    return resolved.value("state").toString() == "resolved" && selectOutingLap(resolved.value("index").toInt());
}

bool AnalysisController::openOutingLapChannel(const QVariantMap &reference, const QString &channel)
{
    if (!selectOutingLapReference(reference)) return false;
    // Applied when the lap's recording has loaded; selecting clears it.
    m_outingLapPendingChannel = channel;
    return true;
}

bool AnalysisController::selectOutingLap(int index)
{
    if (index < 0 || index >= m_outingLapRows.size() || m_outingLapsLoading || m_document.projectLoading()
        || m_outingLapRequestedKey != outingLapKey() || m_outingLapGeneration != sourceGeneration()
        || m_document.recoveryPending() || m_document.destructiveActionPending())
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

void AnalysisController::closeOutingLap()
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

void AnalysisController::loadOutingLapDetail()
{
    // A rapid second click cancels the current parse and waits for it to finish;
    // it cannot create concurrent parsers with multiplied source-memory budgets.
    if (m_selectedOutingLap.isEmpty() || m_outingLapDetailPending) return;
    const auto source = m_outingLapDetailSource;
    const auto projectPath = m_document.documentProjectPath();
    const auto row = m_selectedOutingLap;
    const auto request = m_outingLapDetailRequest;
    m_outingLapDetailCancellation = std::make_shared<std::atomic_bool>(false);
    const auto cancellation = m_outingLapDetailCancellation;
    m_outingLapDetailPending = true;
    m_outingLapDetailWatcher.setFuture(QtConcurrent::run([source, projectPath, row, request, cancellation, cache = m_analysisSourceCache] {
        return FlappedEar::loadOutingLapDetail(source, projectPath, row, request, cancellation, cache);
    }));
}

QStringList AnalysisController::outingLapAvailableChannels() const
{
    return m_outingLapDetailSession ? m_outingLapDetailSession->channelNames() : QStringList{};
}

void AnalysisController::setOutingLapChannels(const QStringList &channels)
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

QVariantMap AnalysisController::outingLapSeries(const QString &channel, const int maximumPoints) const
{
    return outingLapSeries(channel, m_selectedOutingLap.value("startTime").toDouble(),
        m_selectedOutingLap.value("endTime").toDouble(), maximumPoints);
}

QVariantMap AnalysisController::outingLapSeries(
    const QString &channel, const double startTime, const double endTime, const int maximumPoints) const
{
    if (!m_outingLapDetailSession || maximumPoints < 2) return {};
    return sessionSeries(*m_outingLapDetailSession, channel, startTime, endTime, maximumPoints);
}

QString AnalysisController::outingLapValueText(const QString &channel) const
{
    const auto value = m_outingLapDetailSession ? m_outingLapDetailSession->valueAt(channel, m_outingLapCursor) : std::nullopt;
    return value ? QString::number(*value, 'f', 2) : QStringLiteral("—");
}

QVariantMap AnalysisController::outingLapTrackPoint() const
{
    if (!m_outingLapDetailSession) return {};
    const auto point = FlappedEar::currentTrackPoint(*m_outingLapDetailSession, m_outingLapCursor, m_outingLapDetailGeometry);
    return point ? QVariantMap{{"x", point->x()}, {"y", point->y()}} : QVariantMap{};
}

void AnalysisController::setOutingLapCursor(double seconds)
{
    if (!m_outingLapDetailSession || !std::isfinite(seconds)) return;
    seconds = std::clamp(seconds, m_selectedOutingLap.value("startTime").toDouble(), m_selectedOutingLap.value("endTime").toDouble());
    if (seconds == m_outingLapCursor) return;
    m_outingLapCursor = seconds;
    emit outingLapCursorChanged();
    emit outingLapVideoChanged();
}

// KAN-39: the open lap's video, through the VideoLink (KAN-124).
bool AnalysisController::outingLapVideoAvailable() const
{
    if (m_selectedOutingLap.isEmpty() || !m_videoLink) return false;
    return m_videoLink->videoPositionForTelemetry(m_selectedOutingLap.value("runId").toString(), m_outingLapCursor).has_value();
}

qint64 AnalysisController::outingLapVideoPositionMilliseconds() const
{
    if (m_selectedOutingLap.isEmpty() || !m_videoLink) return 0;
    return m_videoLink->videoPositionForTelemetry(m_selectedOutingLap.value("runId").toString(), m_outingLapCursor).value_or(0);
}

bool AnalysisController::followOutingLapVideoPosition(const qint64 videoPositionMilliseconds)
{
    if (m_selectedOutingLap.isEmpty() || !m_videoLink) return false;
    const auto telemetryTime = m_videoLink->telemetryForVideoPosition(
        m_selectedOutingLap.value("runId").toString(), videoPositionMilliseconds);
    if (!telemetryTime) return false;
    setOutingLapCursor(*telemetryTime);
    return true;
}

} // namespace FlappedEar
