// KAN-124: loading one recorded section for analysis, moved out of
// AppController into telemetry core so the Telemetry app can reuse it.

#include "telemetry/OutingLapLoader.h"

#include "project/ProjectSourceReference.h"
#include "telemetry/LapTiming.h"
#include "telemetry/OutingLaps.h"
#include "telemetry/SourceOperation.h"
#include "telemetry/TelemetryImportPlan.h"
#include "telemetry/TelemetrySource.h"

#include <QCryptographicHash>
#include <QFileInfo>
#include <QJsonDocument>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace FlappedEar {

OutingLapDetail loadOutingLapDetail(const QJsonObject &source,
    const QString &projectPath, const QVariantMap &row, const quint64 request,
    const std::shared_ptr<std::atomic_bool> &cancellation, const std::shared_ptr<TelemetrySessionCache> &cache,
    const bool deriveReferenceGate)
{
    const auto start = row.value("startTime").toDouble();
    const auto end = row.value("endTime").toDouble();
    const auto lapReference = QJsonObject::fromVariantMap(row.value("reference").toMap());
    OutingLapDetail result; result.request = request;
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

} // namespace FlappedEar
