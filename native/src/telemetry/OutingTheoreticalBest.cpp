// KAN-56/57/60/63/120: the day's theoretical best and everything measured on
// its shared axis (KAN-124: moved from AppController into telemetry core so
// both apps share the worker).

#include "telemetry/OutingTheoreticalBest.h"

#include "telemetry/BrakingMetrics.h"
#include "telemetry/CornerSpeeds.h"
#include "telemetry/ExitMetrics.h"
#include "telemetry/OutingLapLoader.h"
#include "telemetry/SectorTiming.h"
#include "telemetry/SourceOperation.h"
#include "telemetry/TrackSegmentReview.h"
#include "telemetry/TrackSegments.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace FlappedEar {

OutingTheoreticalBest calculateOutingTheoreticalBest(QVector<OutingLapRow> population,
    const QHash<QString, QJsonObject> &sourcesByRunId, const QString &projectPath, const ApprovedSegmentation &approved,
    const QString &canonicalRunId, const QJsonObject &actualBestReference, const quint64 request,
    const std::shared_ptr<std::atomic_bool> &cancellation)
{
    OutingTheoreticalBest result;
    result.canonicalRunId = canonicalRunId;
    const auto cancelled = [cancellation] { return cancellation->load(); };
    try {
        // Grouped by run so the fresh, small cache below only ever needs to
        // hold the run currently being processed: every one of a run's own
        // eligible laps is handled before moving to the next run's file.
        std::sort(population.begin(), population.end(),
            [](const OutingLapRow &a, const OutingLapRow &b) { return a.runId < b.runId; });
        auto cache = std::make_shared<TelemetrySessionCache>();
        const auto rowVariant = [](const OutingLapRow &row) {
            return QVariantMap{{"startTime", row.start}, {"endTime", row.end}, {"lapNumber", row.lapNumber},
                {"reference", row.reference.toVariantMap()}};
        };
        // The axis comes from the canonical run's fastest lap: the lap usually
        // reviewed, so segment bounds line up with the axis they were approved on.
        auto canonical = population.cend();
        for (auto it = population.cbegin(); it != population.cend(); ++it) {
            if (it->runId != canonicalRunId) continue;
            if (canonical == population.cend() || it->end - it->start < canonical->end - canonical->start) canonical = it;
        }
        if (canonical == population.cend()) throw std::runtime_error("The canonical run has no eligible lap in this population.");
        const auto axisSource = loadOutingLapDetail(sourcesByRunId.value(canonicalRunId), projectPath,
            rowVariant(*canonical), request, cancellation, cache, /*deriveReferenceGate=*/true);
        if (!axisSource.session || !axisSource.hasReferenceGate)
            throw std::runtime_error(axisSource.error.isEmpty()
                ? "Could not build a shared track axis from the canonical run." : axisSource.error.toStdString());
        const GeoCoordinate origin = geoMidpoint(axisSource.referenceGate.endpointA, axisSource.referenceGate.endpointB);
        const auto axis = buildProgressAxis(axisSource.referenceTrace, origin, axisSource.referenceGate, cancelled);
        if (!axis.valid) throw std::runtime_error("The shared track axis could not be built from the canonical run's GPS trace.");

        const auto features = computeTrackFeatures(axis, segmentReviewSmoothingMeters);
        QVector<std::pair<QString, std::pair<double, double>>> corners; // id -> [start, end]
        for (const auto &value : approved.segments) {
            const auto segment = value.toObject();
            if (segment.value("type").toString() == trackSegmentTypeName(TrackSegmentType::Corner))
                corners.append({segment.value("id").toString(),
                    {segment.value("startProgressMeters").toDouble(), segment.value("endProgressMeters").toDouble()}});
        }
        QVector<LapSectorTimes> populationTimes;
        std::shared_ptr<const TelemetrySession> currentSession;
        QString currentRunId;
        for (const auto &row : population) {
            throwIfCancelled(cancelled);
            if (row.runId != currentRunId || !currentSession) {
                const auto detail = loadOutingLapDetail(
                    sourcesByRunId.value(row.runId), projectPath, rowVariant(row), request, cancellation, cache,
                    /*deriveReferenceGate=*/false);
                currentSession = detail.session;
                currentRunId = row.runId;
                if (!currentSession) continue; // this lap's recording could not be decoded; skip its contribution
            }
            const auto trace = projectLapTrace(axis, *currentSession, row.start, row.end, cancelled);
            populationTimes.append(
                computeLapSectorTimes(approved, axis.lengthMeters, trace, row.start, row.end, row.reference));
            result.population.append({populationTimes.last(), row.start});
            // KAN-63: the Corner Analyzer's own metrics for every corner of this lap.
            for (const auto &[segmentId, bounds] : corners) {
                throwIfCancelled(cancelled);
                CornerLapObservation observation;
                observation.lapReference = row.reference;
                const auto speeds = computeCornerSpeeds(axis, features, approved, segmentId, trace, *currentSession);
                if (speeds.valid) {
                    observation.apexSpeed = speeds.apex.value;
                    observation.minimumSpeed = speeds.minimum.value;
                    observation.exitSpeed = speeds.exit.value;
                }
                const auto braking = computeBrakingMetrics(axis.lengthMeters, approved, segmentId, trace, *currentSession,
                    row.start, row.end);
                if (braking.valid && braking.brakingPointMeters) {
                    observation.brakingPointMeters = braking.brakingPointMeters;
                    observation.brakingProvenance = braking.provenance;
                }
                const auto exit = computeExitMetrics(axis.lengthMeters, approved, segmentId, trace, *currentSession, row.end);
                if (exit.valid && exit.pickup.progressMeters) {
                    observation.pickupMeters = exit.pickup.progressMeters;
                    observation.pickupProvenance = exit.pickup.provenance;
                }
                // Line at the geometric apex when one exists, else mid-corner
                // (a chain of corners has several apexes).
                const auto [start, end] = bounds;
                const double middle = end >= start ? (start + end) / 2.0
                    : std::fmod(start + (end + axis.lengthMeters - start) / 2.0, axis.lengthMeters);
                const double at = speeds.valid && speeds.apex.value ? speeds.apex.progressMeters : middle;
                if (const auto time = timeAtProgress(trace, at)) {
                    const auto latitude = currentSession->valueAt("latitude", *time);
                    const auto longitude = currentSession->valueAt("longitude", *time);
                    if (latitude && longitude) {
                        const auto local = projectCoordinate({*latitude, *longitude}, axis.origin);
                        observation.lineOffsetMeters = lateralOffsetMeters(axis, at, QPointF(local.eastMeters, local.northMeters));
                    }
                    observation.gpsAccuracyMeters = currentSession->valueAt("accuracy", *time);
                }
                result.cornerObservations[segmentId].append(observation);
            }
            if (!actualBestReference.isEmpty() && row.reference == actualBestReference)
                result.actualBest = populationTimes.last();
        }
        result.best = computeTheoreticalBest(approved, populationTimes);
        result.approved = approved;
        result.axisLengthMeters = axis.lengthMeters;
        result.axis = axis;
    } catch (const OperationCancelled &) {
        result.error = QStringLiteral("Theoretical best calculation was cancelled.");
    } catch (const std::exception &error) {
        result.error = QString::fromUtf8(error.what());
    }
    return result;
}

} // namespace FlappedEar
