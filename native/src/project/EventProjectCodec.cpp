#include "project/EventProjectCodec.h"
#include "project/FormatVersion.h"
#include "telemetry/ChannelFusion.h"
#include "telemetry/OutingLaps.h"
#include "telemetry/TrackInference.h"
#include "telemetry/TrackSegments.h"
#include "telemetry/TrackSegmentReview.h"
#include "project/ProjectLimits.h"
#include "project/AdditionalVideos.h"
#include "project/VideoChapters.h"

#include <QCryptographicHash>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QSet>
#include <cmath>

namespace FlappedEar {
namespace {

bool fail(QString *error, const QString &message)
{
    if (error) *error = message;
    return false;
}

bool validText(const QJsonValue &value, const qsizetype limit)
{
    return value.isString() && !value.toString().trimmed().isEmpty()
        && !value.toString().contains(QChar::Null)
        && value.toString().size() <= limit;
}

bool validReference(const QJsonValue &value)
{
    if (!value.isObject()) return false;
    const QJsonObject reference = value.toObject();
    for (const QString &key : {QStringLiteral("relativePath"), QStringLiteral("absolutePath")}) {
        if (reference.contains(key) && !reference.value(key).isString()) return false;
        if (reference.value(key).toString().contains(QChar::Null)) return false;
    }
    const QString relative = reference.value(QStringLiteral("relativePath")).toString();
    const QString absolute = reference.value(QStringLiteral("absolutePath")).toString();
    if (relative.trimmed().isEmpty() && absolute.trimmed().isEmpty()) return false;
    if (!relative.isEmpty() && QDir::isAbsolutePath(relative)) return false;
    if (!absolute.isEmpty() && !QDir::isAbsolutePath(absolute)) return false;
    if (reference.contains(QStringLiteral("contentSha256"))
        && !ProjectSourceReferenceCodec::isContentSha256(reference.value(QStringLiteral("contentSha256")).toString()))
        return false;
    return !reference.contains(QStringLiteral("fingerprint"))
        || reference.value(QStringLiteral("fingerprint")).isObject();
}

QJsonObject primaryFingerprint(const QJsonObject &run)
{
    for (const auto &value : run.value("sources").toObject().value("telemetry").toArray()) {
        const auto source = value.toObject();
        if (source.value("id") == run.value("primaryTelemetrySourceId"))
            return source.value("reference").toObject().value("fingerprint").toObject();
    }
    return {};
}

bool validConfiguration(const QJsonObject &run)
{
    if (!run.contains("trackConfiguration")) return true;
    if (!run.value("trackConfiguration").isObject()) return false;
    const auto config = run.value("trackConfiguration").toObject();
    const auto layout = config.value("layoutId");
    const auto direction = config.value("direction");
    const auto revision = config.value("gateRevision");
    static const QRegularExpression revisionPattern("^gates-v1:[0-9a-f]{64}\\z"); // \z rejects a trailing newline (KAN-181)
    return (layout.isNull() || validText(layout, ProjectLimits::maximumIdCharacters))
        && direction.isString() && QStringList{"unknown", "clockwise", "counterclockwise"}.contains(direction.toString())
        && (revision.isNull() || (revision.isString() && revisionPattern.match(revision.toString()).hasMatch()))
        && config.value("sourceId") == run.value("primaryTelemetrySourceId")
        && config.value("sourceFingerprint").isObject()
        && config.value("sourceFingerprint").toObject() == primaryFingerprint(run);
}

// KAN-103: an approved source fusion, bound to the exact content of both
// recordings it was reviewed against.
bool validFusion(const QJsonObject &run)
{
    if (!run.contains("fusion")) return true;
    if (!run.value("fusion").isObject()) return false;
    const auto fusion = run.value("fusion").toObject();
    // KAN-170: a newer app's fusion is kept as written and not applied.
    if (FormatVersion::isNewerTag(fusion.value("algorithm"), channelFusionAlgorithm)) return true;
    static const QRegularExpression digest("^[0-9a-f]{64}$");
    const auto alternative = fusion.value("alternativeSourceId").toString();
    if (fusion.value("algorithm").toString() != QLatin1String(channelFusionAlgorithm)
        || !validText(fusion.value("alternativeSourceId"), ProjectLimits::maximumIdCharacters)
        || alternative == run.value("primaryTelemetrySourceId").toString())
        return false;
    bool found = false;
    for (const auto &value : run.value("sources").toObject().value("telemetry").toArray())
        found |= value.toObject().value("id").toString() == alternative;
    if (!found) return false;
    for (const auto *key : {"primarySourceRevision", "alternativeSourceRevision"}) {
        const auto revision = fusion.value(key).toString();
        if (revision.size() != 64 || !digest.match(revision).hasMatch()) return false;
    }
    const auto clock = fusion.value("clock").toObject();
    const auto offset = clock.value("offsetSeconds"), drift = clock.value("driftPpm"), uncertainty = clock.value("uncertaintySeconds");
    if (!fusion.value("clock").isObject() || !offset.isDouble() || !drift.isDouble() || !uncertainty.isDouble()
        || !std::isfinite(offset.toDouble()) || std::abs(offset.toDouble()) > ProjectLimits::maximumFusionOffsetSeconds
        || !std::isfinite(drift.toDouble()) || std::abs(drift.toDouble()) > ProjectLimits::maximumFusionDriftPpm
        || !std::isfinite(uncertainty.toDouble()) || uncertainty.toDouble() < 0.0
        || uncertainty.toDouble() > ProjectLimits::maximumFusionOffsetSeconds)
        return false;
    const auto rules = fusion.value("rules");
    if (!rules.isArray() || rules.toArray().size() > ProjectLimits::maximumFusionRules) return false;
    QSet<QString> keys;
    for (const auto &value : rules.toArray()) {
        const auto rule = value.toObject();
        const auto key = rule.value("key").toString();
        if (!value.isObject() || !validText(rule.value("key"), ProjectLimits::maximumIdCharacters) || keys.contains(key)
            || !QStringList{"primaryOnly", "fillGaps", "preferAlternative"}.contains(rule.value("rule").toString()))
            return false;
        keys.insert(key);
    }
    return true;
}

// KAN-170: a segment or review bound to a newer compatibility identity is
// checked like a current one, with that identity read as an opaque value.
QJsonObject withCurrentReference(QJsonObject object)
{
    static const QString placeholder = QStringLiteral("compatibility-v1:") + QString(64, QLatin1Char('0'));
    if (FormatVersion::isNewerTaggedIdentity(object.value("trackConfigurationReference"), "compatibility-v1"))
        object.insert("trackConfigurationReference", placeholder);
    return object;
}

bool validSegments(const QJsonValue &value)
{
    if (!value.isArray()) return validTrackSegments(value);
    QJsonArray segments;
    for (const auto &item : value.toArray())
        segments.append(item.isObject() ? QJsonValue(withCurrentReference(item.toObject())) : item);
    return validTrackSegments(segments);
}

bool validSegmentReview(const QJsonValue &value)
{
    if (!value.isObject()) return validTrackSegmentReview(value);
    // A newer review version is the newer app's; its shape is not checked.
    if (FormatVersion::isNewerTag(value.toObject().value("version"), trackSegmentReviewAlgorithm)) return true;
    return validTrackSegmentReview(withCurrentReference(value.toObject()));
}

ProjectSourceReference sourceReference(const QJsonObject &object)
{
    const auto digest = object.value(QStringLiteral("contentSha256")).toString();
    return {object.value(QStringLiteral("relativePath")).toString(),
            object.value(QStringLiteral("absolutePath")).toString(),
            object.value(QStringLiteral("fingerprint")).toObject(),
            ProjectSourceReferenceCodec::isContentSha256(digest) ? digest : QString()};
}

QDir referenceDirectory(const QString &projectPath)
{
    const QFileInfo directory(QFileInfo(projectPath).absolutePath());
    const QString canonical = directory.canonicalFilePath();
    return QDir(canonical.isEmpty() ? directory.absoluteFilePath() : canonical);
}

QJsonObject rebaseReference(QJsonObject reference, const QString &oldPath, const QString &newPath)
{
    const QJsonObject known = EventProjectCodec::referenceForSave(sourceReference(reference), oldPath, newPath);
    reference.remove(QStringLiteral("relativePath"));
    reference.remove(QStringLiteral("absolutePath"));
    reference.remove(QStringLiteral("fingerprint"));
    for (auto it = known.begin(); it != known.end(); ++it) reference.insert(it.key(), it.value());
    return reference;
}

// KAN-105: a run's video with its chapters, every reference rebased.
QJsonObject rebaseVideo(QJsonObject video, const QString &oldPath, const QString &newPath)
{
    QJsonArray chapters;
    for (const auto &value : video.value(QStringLiteral("chapters")).toArray())
        chapters.append(rebaseReference(value.toObject(), oldPath, newPath));
    video = rebaseReference(video, oldPath, newPath);
    if (!chapters.isEmpty()) video.insert(QStringLiteral("chapters"), chapters);
    return video;
}

} // namespace

bool EventProjectCodec::isEvent(const QJsonObject &project)
{
    return project.value(QStringLiteral("version")).toDouble() == 3.0;
}

bool EventProjectCodec::validate(const QJsonObject &project, QString *error)
{
    for (const QString &key : {QStringLiteral("sources"), QStringLiteral("sync"),
                              QStringLiteral("videoPath"), QStringLiteral("vboPath")}) {
        if (project.contains(key)) return fail(error, QStringLiteral("Event projects cannot contain root %1.").arg(key));
    }
    const QJsonObject event = project.value(QStringLiteral("event")).toObject();
    const auto decisions = event.value("analysisDecisions");
    if (!decisions.isUndefined()) {
        if (!decisions.isObject()) return fail(error, "Analysis decisions must be an object.");
        const auto group = decisions.toObject().value("comparisonGroupId");
        static const QRegularExpression groupPattern("^compatibility-v1:[0-9a-f]{64}$");
        if (!group.isUndefined() && !group.isNull() && !FormatVersion::isNewerTaggedIdentity(group, "compatibility-v1")
            && (!group.isString() || group.toString().size() != 81 || !groupPattern.match(group.toString()).hasMatch()))
            return fail(error, "Saved comparison group is malformed.");
        const auto savedComparisonSlots = decisions.toObject().value("comparisonSlots");
        if (!savedComparisonSlots.isUndefined()) {
            if (!savedComparisonSlots.isArray() || savedComparisonSlots.toArray().size() != 2)
                return fail(error, "Saved comparison slots must be a pair.");
            for (const auto &item : savedComparisonSlots.toArray()) {
                if (item.isNull()) continue;
                if (!item.isObject() || !validLapReference(item.toObject())
                    || item.toObject().value("type") != "LAP"
                    || item.toObject().value("eventId") != event.value("id"))
                    return fail(error, "Saved comparison slot reference is invalid.");
            }
        }
        const auto savedComparisonRange = decisions.toObject().value("comparisonRange");
        if (!savedComparisonRange.isUndefined() && !savedComparisonRange.isNull()) {
            if (!savedComparisonRange.isObject()) return fail(error, "Saved comparison range must be an object.");
            const auto range = savedComparisonRange.toObject();
            const auto start = range.value("startMeters");
            const auto end = range.value("endMeters");
            if (!start.isDouble() || !end.isDouble()
                || !std::isfinite(start.toDouble()) || !std::isfinite(end.toDouble())
                || start.toDouble() < 0.0 || end.toDouble() <= start.toDouble()
                || end.toDouble() > ProjectLimits::maximumComparisonRangeMeters)
                return fail(error, "Saved comparison range is malformed.");
        }
        const auto savedComparisonChannels = decisions.toObject().value("comparisonChannels");
        if (!savedComparisonChannels.isUndefined() && !savedComparisonChannels.isNull()) {
            if (!savedComparisonChannels.isArray()
                || savedComparisonChannels.toArray().size() > ProjectLimits::maximumComparisonChannels)
                return fail(error, "Saved comparison channels must be a bounded array.");
            QSet<QString> seenChannels;
            for (const auto &item : savedComparisonChannels.toArray()) {
                if (!validText(item, ProjectLimits::maximumIdCharacters) || seenChannels.contains(item.toString()))
                    return fail(error, "Saved comparison channel entry is invalid or duplicated.");
                seenChannels.insert(item.toString());
            }
        }
    }
    if (!validText(event.value(QStringLiteral("id")), ProjectLimits::maximumIdCharacters)
        || !validText(event.value(QStringLiteral("name")), ProjectLimits::maximumTemplateNameCharacters)
        || !validText(event.value(QStringLiteral("activeRunId")), ProjectLimits::maximumIdCharacters)
        || !event.value(QStringLiteral("runs")).isArray()) {
        return fail(error, QStringLiteral("Event metadata is missing or malformed."));
    }
    if (!validLapExclusions(event.value("lapExclusions"), event.value("id").toString()))
        return fail(error, QStringLiteral("Lap exclusions contain an invalid reference, duplicate or reason."));
    const QJsonArray runs = event.value(QStringLiteral("runs")).toArray();
    if (runs.isEmpty() || runs.size() > maximumRuns) return fail(error, QStringLiteral("An event must contain 1–64 runs."));
    QSet<QString> runIds;
    QSet<QString> sourceIds;
    for (const QJsonValue &value : runs) {
        const QJsonObject run = value.toObject();
        const QString id = run.value(QStringLiteral("id")).toString();
        if (!validText(run.value(QStringLiteral("id")), ProjectLimits::maximumIdCharacters)
            || runIds.contains(id)
            || !validText(run.value(QStringLiteral("name")), ProjectLimits::maximumTemplateNameCharacters)
            || !validText(run.value(QStringLiteral("primaryTelemetrySourceId")), ProjectLimits::maximumIdCharacters)) {
            return fail(error, QStringLiteral("Run metadata or identity is invalid."));
        }
        if (run.value("name").toString().contains(QChar::Null))
            return fail(error, QStringLiteral("Run name contains NUL."));
        for (const auto *key : {"notes", "conditions", "setupChanges"}) {
            const auto text = run.value(key);
            if (!text.isUndefined() && !text.isNull() && (!text.isString()
                || text.toString().size() > ProjectLimits::maximumStringCharacters
                || text.toString().contains(QChar::Null)))
                return fail(error, QStringLiteral("Run notes, conditions and setup changes must be bounded text or unknown."));
        }
        runIds.insert(id);
        const QJsonObject sources = run.value(QStringLiteral("sources")).toObject();
        const QJsonValue telemetryValue = sources.value(QStringLiteral("telemetry"));
        const QJsonArray telemetry = telemetryValue.toArray();
        if (!telemetryValue.isArray() || telemetry.isEmpty() || telemetry.size() > maximumSourcesPerRun) {
            return fail(error, QStringLiteral("Each run must contain 1–8 telemetry sources."));
        }
        bool primaryFound = false;
        for (const QJsonValue &sourceValue : telemetry) {
            const QJsonObject source = sourceValue.toObject();
            const QString sourceId = source.value(QStringLiteral("id")).toString();
            if (source.contains("contentSha256")) {
                static const QRegularExpression digestPattern("^[0-9a-f]{64}$");
                if (!source.value("contentSha256").isString() || source.value("contentSha256").toString().size() != 64
                    || !digestPattern.match(source.value("contentSha256").toString()).hasMatch())
                    return fail(error, "Telemetry content identity is malformed.");
            }
            if (!validText(source.value(QStringLiteral("id")), ProjectLimits::maximumIdCharacters)
                || sourceIds.contains(sourceId) || !validReference(source.value(QStringLiteral("reference")))) {
                return fail(error, QStringLiteral("Telemetry source identity or reference is invalid."));
            }
            sourceIds.insert(sourceId);
            primaryFound |= sourceId == run.value(QStringLiteral("primaryTelemetrySourceId")).toString();
        }
        if (!primaryFound || sourceIds.size() > maximumTelemetrySources) {
            return fail(error, QStringLiteral("Primary telemetry source is missing or event source limit exceeded."));
        }
        if (!validConfiguration(run)) {
            return fail(error, QStringLiteral("Track configuration or its primary source binding is invalid."));
        }
        if (!validSegments(run.value("trackSegments"))) {
            return fail(error, QStringLiteral("Track segments are invalid, out of order, or exceed the bound."));
        }
        if (!validSegmentReview(run.value("trackSegmentReview"))) {
            return fail(error, QStringLiteral("Track segment review decisions are invalid or exceed the bound."));
        }
        if (run.contains("trackInference")) {
            const auto inferenceValue = run.value("trackInference"); const auto inference = inferenceValue.toObject();
            static const QRegularExpression digest("^[0-9a-f]{64}$");
            static const QRegularExpression gates("^gates-v1:[0-9a-f]{64}$");
            const auto revision = inference.value("sourceRevision").toString();
            const auto gate = inference.value("gateRevision");
            // KAN-170: a newer inference is kept as written; this build infers afresh.
            const bool newer = inferenceValue.isObject() && FormatVersion::isNewerTag(inference.value("algorithm"), trackInferenceVersion);
            if (!newer && (!inferenceValue.isObject() || !validText(inference.value("algorithm"), 128)
                || revision.size() != 64 || !digest.match(revision).hasMatch()
                || !(gate.isNull() || (gate.toString().size() == 73 && gates.match(gate.toString()).hasMatch()))
                || !validText(inference.value("layoutId"), 128)
                || inference.value("layoutId").toString().size() <= 13
                || !inference.value("layoutId").toString().startsWith("gps-route-v1:")
                || !QStringList{"clockwise", "counterclockwise"}.contains(inference.value("direction").toString())))
                return fail(error, "Track inference provenance is malformed.");
        }
        if (!validFusion(run)) {
            return fail(error, QStringLiteral("Source fusion decision is malformed or not bound to this run's recordings."));
        }
        if (sources.contains(QStringLiteral("video")) && (!validReference(sources.value(QStringLiteral("video")))
                || !VideoChaptersCodec::valid(sources.value(QStringLiteral("video")).toObject()))) {
            return fail(error, QStringLiteral("Run video reference is invalid."));
        }
        if (!AdditionalVideosCodec::valid(sources.value(QStringLiteral("additionalVideos")))
            || !AdditionalVideosCodec::validLayout(run.value(QStringLiteral("videoLayout")))) {
            return fail(error, QStringLiteral("Run additional videos or video layout are invalid."));
        }
        const QJsonObject sync = run.value(QStringLiteral("sync")).toObject();
        const QJsonValue offset = sync.value(QStringLiteral("offset"));
        const QJsonValue scale = sync.value(QStringLiteral("timeScale"));
        if (!offset.isDouble() || !scale.isDouble() || !std::isfinite(offset.toDouble())
            || !std::isfinite(scale.toDouble()) || scale.toDouble() <= 0.0) {
            return fail(error, QStringLiteral("Run synchronization is invalid."));
        }
    }
    if (!runIds.contains(event.value(QStringLiteral("activeRunId")).toString())) {
        return fail(error, QStringLiteral("Active run does not belong to the event."));
    }
    return true;
}

QJsonObject EventProjectCodec::unknownTrackConfiguration(
    const QString &sourceId, const QJsonObject &fingerprint, const QString &gateRevision)
{
    return {{"layoutId", QJsonValue::Null}, {"direction", "unknown"},
        {"gateRevision", gateRevision.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(gateRevision)},
        {"sourceId", sourceId}, {"sourceFingerprint", fingerprint}};
}

QJsonObject EventProjectCodec::trackConfiguration(const QJsonObject &run)
{
    return run.contains("trackConfiguration") ? run.value("trackConfiguration").toObject()
        : unknownTrackConfiguration(run.value("primaryTelemetrySourceId").toString(), primaryFingerprint(run));
}

QJsonObject EventProjectCodec::withReplacedRecording(
    const QJsonObject &eventProject, const QString &runId, const QString &gateRevision)
{
    auto project = eventProject;
    auto event = project.value("event").toObject();
    auto runs = event.value("runs").toArray();
    for (qsizetype i = 0; i < runs.size(); ++i) {
        auto run = runs[i].toObject();
        if (run.value("id").toString() != runId) continue;
        auto config = trackConfiguration(run);
        config.insert("gateRevision", gateRevision.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(gateRevision));
        run.insert("trackConfiguration", config);
        run.remove("trackInference");
        runs[i] = run;
    }
    event.insert("runs", runs);
    project.insert("event", event);
    return project;
}

QByteArray EventProjectCodec::lapDerivationKey(const QJsonObject &run)
{
    return QCryptographicHash::hash(QJsonDocument(QJsonObject{{"version", "lap-derivation-v1"},
        {"runId", run.value("id")}, {"sourceId", run.value("primaryTelemetrySourceId")},
        {"sourceFingerprint", primaryFingerprint(run)}, {"trackConfiguration", trackConfiguration(run)}})
        .toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex();
}

QByteArray EventProjectCodec::sourceContentRevision(const QJsonObject &source)
{
    if (source.contains("contentSha256")) return source.value("contentSha256").toString().toLatin1();
    const auto provenance = source.value("importProvenance").toObject();
    const auto fingerprint = source.value("reference").toObject().value("fingerprint").toObject();
    static const QRegularExpression digestPattern("^[0-9a-f]{64}$");
    const auto digest = provenance.value("sha256").toString();
    return !fingerprint.isEmpty() && provenance.value("fingerprint").toObject() == fingerprint
        && digest.size() == 64 && digestPattern.match(digest).hasMatch() ? digest.toLatin1() : QByteArray{};
}

QJsonObject EventProjectCodec::primaryTelemetryBinding(const QJsonObject &project, const QString &runId)
{
    const auto event = project.value(QStringLiteral("event")).toObject();
    for (const QJsonValue &runValue : event.value(QStringLiteral("runs")).toArray()) {
        const auto run = runValue.toObject();
        if (run.value(QStringLiteral("id")).toString() != runId) continue;
        for (const QJsonValue &sourceValue : run.value(QStringLiteral("sources")).toObject()
                 .value(QStringLiteral("telemetry")).toArray()) {
            const auto source = sourceValue.toObject();
            if (source.value(QStringLiteral("id")) != run.value(QStringLiteral("primaryTelemetrySourceId"))) continue;
            return {{"eventId", event.value(QStringLiteral("id"))}, {"runId", run.value(QStringLiteral("id"))},
                {"sourceId", source.value(QStringLiteral("id"))}, {"reference", source.value(QStringLiteral("reference"))},
                {"expectedRevision", QString::fromLatin1(sourceContentRevision(source))},
                {"derivationKey", QString::fromLatin1(lapDerivationKey(run))}};
        }
        return {};
    }
    return {};
}

QJsonArray EventProjectCodec::outingLapSources(const QJsonObject &project)
{
    QJsonArray sources;
    const auto event = project.value(QStringLiteral("event")).toObject();
    for (const QJsonValue &runValue : event.value(QStringLiteral("runs")).toArray()) {
        const auto run = runValue.toObject();
        const auto telemetry = run.value(QStringLiteral("sources")).toObject().value(QStringLiteral("telemetry")).toArray();
        for (const QJsonValue &item : telemetry) {
            const auto source = item.toObject();
            if (source.value(QStringLiteral("id")) != run.value(QStringLiteral("primaryTelemetrySourceId"))) continue;
            QJsonObject descriptor{{"eventId", event.value(QStringLiteral("id"))}, {"runId", run.value(QStringLiteral("id"))},
                {"name", run.value(QStringLiteral("name"))}, {"sourceId", source.value(QStringLiteral("id"))},
                {"reference", source.value(QStringLiteral("reference"))},
                {"expectedRevision", QString::fromLatin1(sourceContentRevision(source))},
                {"inference", run.value(QStringLiteral("trackInference"))}, {"inferenceVersion", trackInferenceVersion},
                {"trackConfiguration", trackConfiguration(run)},
                {"derivationKey", QString::fromLatin1(lapDerivationKey(run))}};
            // KAN-170: a newer app's fusion is not applied; the primary loads alone.
            if (run.contains(QStringLiteral("fusion"))
                && run.value(QStringLiteral("fusion")).toObject().value(QStringLiteral("algorithm")) == QLatin1String(channelFusionAlgorithm)) {
                auto fusion = run.value(QStringLiteral("fusion")).toObject();
                QJsonObject alternative;
                for (const QJsonValue &other : telemetry)
                    if (other.toObject().value(QStringLiteral("id")) == fusion.value(QStringLiteral("alternativeSourceId")))
                        alternative = other.toObject();
                const auto primaryRevision = sourceContentRevision(source);
                const auto alternativeRevision = sourceContentRevision(alternative);
                if (!primaryRevision.isEmpty()
                    && primaryRevision == fusion.value(QStringLiteral("primarySourceRevision")).toString().toLatin1()
                    && !alternativeRevision.isEmpty()
                    && alternativeRevision == fusion.value(QStringLiteral("alternativeSourceRevision")).toString().toLatin1()) {
                    fusion.insert(QStringLiteral("alternativeReference"), alternative.value(QStringLiteral("reference")));
                    descriptor.insert(QStringLiteral("fusion"), fusion);
                } else {
                    descriptor.insert(QStringLiteral("fusionNeedsRevalidation"), true);
                }
            }
            sources.append(descriptor);
        }
    }
    return sources;
}

QJsonObject EventProjectCodec::editorProjection(const QJsonObject &project)
{
    if (!isEvent(project)) return project;
    QJsonObject editor = project;
    editor.remove(QStringLiteral("event"));
    editor.insert(QStringLiteral("version"), 2);
    const QJsonObject event = project.value(QStringLiteral("event")).toObject();
    for (const QJsonValue &value : event.value(QStringLiteral("runs")).toArray()) {
        const QJsonObject run = value.toObject();
        if (run.value(QStringLiteral("id")) != event.value(QStringLiteral("activeRunId"))) continue;
        const QJsonObject runSources = run.value(QStringLiteral("sources")).toObject();
        QJsonObject sources;
        if (runSources.contains(QStringLiteral("video"))) sources.insert(QStringLiteral("video"), runSources.value(QStringLiteral("video")));
        if (runSources.contains(QStringLiteral("additionalVideos")))
            sources.insert(QStringLiteral("additionalVideos"), runSources.value(QStringLiteral("additionalVideos")));
        for (const QJsonValue &sourceValue : runSources.value(QStringLiteral("telemetry")).toArray()) {
            const QJsonObject source = sourceValue.toObject();
            if (source.value(QStringLiteral("id")) == run.value(QStringLiteral("primaryTelemetrySourceId"))) {
                sources.insert(QStringLiteral("telemetry"), source.value(QStringLiteral("reference")));
            }
        }
        editor.insert(QStringLiteral("sources"), sources);
        editor.insert(QStringLiteral("sync"), run.value(QStringLiteral("sync")));
        if (run.contains(QStringLiteral("videoLayout"))) editor.insert(QStringLiteral("videoLayout"), run.value(QStringLiteral("videoLayout")));
        else editor.remove(QStringLiteral("videoLayout"));
        break;
    }
    return editor;
}

QJsonObject EventProjectCodec::referenceForSave(
    const ProjectSourceReference &reference, const QString &previousProjectPath, const QString &targetProjectPath)
{
    if (reference.isEmpty()) return {};
    QString absolute = ProjectSourceReferenceCodec::resolve(reference, previousProjectPath);
    if (absolute.isEmpty() && !reference.relativePath.isEmpty() && !previousProjectPath.isEmpty()) {
        // Missing files still have a location; never reinterpret it against Save As or cwd.
        absolute = referenceDirectory(previousProjectPath).absoluteFilePath(reference.relativePath);
    }
    if (absolute.isEmpty()) absolute = reference.absolutePath;
    if (absolute.isEmpty()) return ProjectSourceReferenceCodec::toJson(reference, targetProjectPath);
    // Drop the old relative spelling before deriving one in the new document directory.
    return ProjectSourceReferenceCodec::toJson({{}, absolute, reference.fingerprint, reference.contentSha256}, targetProjectPath);
}

QStringList EventProjectCodec::referencedPaths(const QJsonObject &project, const QString &projectPath)
{
    QStringList paths;
    const auto append = [&paths, &projectPath](const QJsonObject &object) {
        const auto reference = sourceReference(object);
        if (!reference.absolutePath.isEmpty()) paths.append(reference.absolutePath);
        if (!reference.relativePath.isEmpty() && !projectPath.isEmpty()) {
            paths.append(referenceDirectory(projectPath).absoluteFilePath(reference.relativePath));
        }
    };
    const auto event = project.value(QStringLiteral("event")).toObject();
    for (const QJsonValue &value : event.value(QStringLiteral("runs")).toArray()) {
        const auto sources = value.toObject().value(QStringLiteral("sources")).toObject();
        append(sources.value(QStringLiteral("video")).toObject());
        for (const QJsonValue &chapter : sources.value(QStringLiteral("video")).toObject().value(QStringLiteral("chapters")).toArray())
            append(chapter.toObject());
        for (const QJsonValue &source : sources.value(QStringLiteral("telemetry")).toArray()) {
            append(source.toObject().value(QStringLiteral("reference")).toObject());
        }
        for (const QJsonValue &video : sources.value(QStringLiteral("additionalVideos")).toArray()) append(video.toObject());
    }
    paths.removeDuplicates();
    return paths;
}

QJsonObject EventProjectCodec::withEditorState(
    const QJsonObject &eventProject, QJsonObject editorProject,
    const QString &previousProjectPath, const QString &targetProjectPath,
    const QByteArray &activeSourceRevision)
{
    QJsonObject event = eventProject.value(QStringLiteral("event")).toObject();
    QJsonArray runs;
    for (const QJsonValue &value : event.value(QStringLiteral("runs")).toArray()) {
        QJsonObject run = value.toObject();
        const bool active = run.value(QStringLiteral("id")) == event.value(QStringLiteral("activeRunId"));
        QJsonObject sources = run.value(QStringLiteral("sources")).toObject();
        QJsonArray telemetry;
        bool contentChanged = false;
        const QJsonObject editorSources = editorProject.value(QStringLiteral("sources")).toObject();
        for (const QJsonValue &sourceValue : sources.value(QStringLiteral("telemetry")).toArray()) {
            QJsonObject source = sourceValue.toObject();
            const bool primary = source.value(QStringLiteral("id")) == run.value(QStringLiteral("primaryTelemetrySourceId"));
            const auto previousRevision = sourceContentRevision(source);
            if (active && primary && !activeSourceRevision.isEmpty()) {
                contentChanged = !previousRevision.isEmpty() && previousRevision != activeSourceRevision;
                // Loading a legacy document is not a schema migration. Record a
                // new binding only on replacement; import already records SHA-256.
                if (contentChanged || source.value("reference").toObject().value("fingerprint")
                    != editorSources.value("telemetry").toObject().value("fingerprint"))
                    source.insert("contentSha256", QString::fromLatin1(activeSourceRevision));
            }
            // KAN-208: an event source keeps its content identity on the
            // source itself, so the editor's copy in the reference is dropped.
            auto editorTelemetry = editorSources.value(QStringLiteral("telemetry")).toObject();
            if (!source.value("reference").toObject().contains(QStringLiteral("contentSha256")))
                editorTelemetry.remove(QStringLiteral("contentSha256"));
            source.insert(QStringLiteral("reference"), active && primary
                ? editorTelemetry
                : rebaseReference(source.value(QStringLiteral("reference")).toObject(), previousProjectPath, targetProjectPath));
            telemetry.append(source);
        }
        sources.insert(QStringLiteral("telemetry"), telemetry);
        if (active) {
            if (editorSources.contains(QStringLiteral("video"))) sources.insert(QStringLiteral("video"), editorSources.value(QStringLiteral("video")));
            else sources.remove(QStringLiteral("video"));
            if (editorSources.contains(QStringLiteral("additionalVideos")))
                sources.insert(QStringLiteral("additionalVideos"), editorSources.value(QStringLiteral("additionalVideos")));
            else sources.remove(QStringLiteral("additionalVideos"));
            run.insert(QStringLiteral("sync"), editorProject.value(QStringLiteral("sync")));
            if (editorProject.contains(QStringLiteral("videoLayout")))
                run.insert(QStringLiteral("videoLayout"), editorProject.value(QStringLiteral("videoLayout")));
            else run.remove(QStringLiteral("videoLayout"));
        } else {
            if (sources.contains(QStringLiteral("video")))
                sources.insert(QStringLiteral("video"), rebaseVideo(sources.value(QStringLiteral("video")).toObject(), previousProjectPath, targetProjectPath));
            if (sources.contains(QStringLiteral("additionalVideos"))) {
                QJsonArray additional;
                for (const auto &video : sources.value(QStringLiteral("additionalVideos")).toArray())
                    additional.append(rebaseReference(video.toObject(), previousProjectPath, targetProjectPath));
                sources.insert(QStringLiteral("additionalVideos"), additional);
            }
        }
        const auto previousFingerprint = primaryFingerprint(run);
        run.insert(QStringLiteral("sources"), sources);
        if (contentChanged || primaryFingerprint(run) != previousFingerprint) {
            // A replacement source cannot inherit asserted layout/gate metadata.
            // A same-content relink/Save As changes paths only and retains it.
            run.insert("trackConfiguration", unknownTrackConfiguration(
                run.value("primaryTelemetrySourceId").toString(), primaryFingerprint(run)));
        }
        runs.append(run);
    }
    event.insert(QStringLiteral("runs"), runs);
    editorProject.insert(QStringLiteral("version"), 3);
    editorProject.insert(QStringLiteral("event"), event);
    editorProject.remove(QStringLiteral("sources"));
    editorProject.remove(QStringLiteral("sync"));
    editorProject.remove(QStringLiteral("videoLayout"));
    editorProject.remove(QStringLiteral("videoPath"));
    editorProject.remove(QStringLiteral("vboPath"));
    return editorProject;
}

} // namespace FlappedEar
