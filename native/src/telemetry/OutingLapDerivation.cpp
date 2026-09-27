// Lap sections of every run of an outing (KAN-124: moved from AppController
// into telemetry core so both apps derive laps the same way).

#include "telemetry/OutingLapDerivation.h"

#include "project/ProjectSourceReference.h"
#include "telemetry/LapTiming.h"
#include "telemetry/SourceOperation.h"
#include "telemetry/TelemetryImportPlan.h"
#include "telemetry/TelemetrySource.h"

#include <QFileInfo>
#include <QJsonDocument>
#include <QSet>
#include <stdexcept>

namespace FlappedEar {

QByteArray outingSourceDependencyKey(QJsonObject source)
{
    source.remove("name");
    source.remove("inference");
    source.insert("reference", source.value("reference").toObject().value("fingerprint"));
    // A fused alternative's paths may be rebased on save; its identity is its fingerprint.
    if (source.contains("fusion")) {
        auto fusion = source.value("fusion").toObject();
        fusion.insert("alternativeReference", fusion.value("alternativeReference").toObject().value("fingerprint"));
        source.insert("fusion", fusion);
    }
    return QJsonDocument(source).toJson(QJsonDocument::Compact);
}

OutingLapDerivation deriveOutingLaps(const QJsonArray &sources, const QString &projectPath,
    const QHash<QString, OutingRunDerivation> &cache, const std::shared_ptr<std::atomic_bool> &cancellation)
{
    OutingLapDerivation result;
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
                const auto dependencyKey = outingSourceDependencyKey(source);
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
}

} // namespace FlappedEar
