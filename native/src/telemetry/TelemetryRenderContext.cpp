#include "telemetry/TelemetryRenderContext.h"
#include "telemetry/TelemetryGeometry.h"

#include <QtGlobal>
#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>

namespace FlappedEar {

namespace {

struct PresentationPolicy {
    double smoothingSeconds;
    double staleSeconds;
    InterpolationMode interpolation;
};

struct ReferenceMatch {
    double distanceSquared = std::numeric_limits<double>::infinity();
    double elapsedSeconds = 0.0;
};

std::optional<ReferenceMatch> closestReferenceMatch(
    const LapTrace &trace,
    const MetricPoint &current,
    const double expectedElapsedSeconds)
{
    if (trace.points.size() < 2 || !std::isfinite(expectedElapsedSeconds)) return std::nullopt;
    const double searchWindow = std::clamp(trace.durationSeconds * 0.18, 12.0, 30.0);
    // A closed lap revisits the Start coordinates at the Finish. Restricting
    // the spatial search around the expected elapsed time prevents matching
    // the beginning of a lap to its end (and limits work for every frame).
    const double minimumElapsed = std::max(0.0, expectedElapsedSeconds - searchWindow);
    const double maximumElapsed = std::min(
        trace.durationSeconds, expectedElapsedSeconds + searchWindow);
    ReferenceMatch best;
    for (qsizetype index = 1; index < trace.points.size(); ++index) {
        const LapTracePoint &start = trace.points[index - 1];
        const LapTracePoint &end = trace.points[index];
        const double startElapsed = start.telemetryTime - trace.startTelemetryTime;
        const double endElapsed = end.telemetryTime - trace.startTelemetryTime;
        if (endElapsed < minimumElapsed || startElapsed > maximumElapsed) continue;
        const double dx = end.eastMeters - start.eastMeters;
        const double dy = end.northMeters - start.northMeters;
        const double lengthSquared = dx * dx + dy * dy;
        if (!std::isfinite(lengthSquared) || lengthSquared <= 1e-12) continue;
        const double fraction = std::clamp(
            ((current.eastMeters - start.eastMeters) * dx
             + (current.northMeters - start.northMeters) * dy) / lengthSquared,
            0.0, 1.0);
        const double nearestEast = start.eastMeters + dx * fraction;
        const double nearestNorth = start.northMeters + dy * fraction;
        const double eastError = current.eastMeters - nearestEast;
        const double northError = current.northMeters - nearestNorth;
        const double distanceSquared = eastError * eastError + northError * northError;
        if (std::isfinite(distanceSquared) && distanceSquared < best.distanceSquared) {
            best.distanceSquared = distanceSquared;
            best.elapsedSeconds = startElapsed + (endElapsed - startElapsed) * fraction;
        }
    }
    constexpr double MaximumReferenceDistanceMeters = 50.0;
    return std::isfinite(best.distanceSquared)
            && best.distanceSquared <= MaximumReferenceDistanceMeters * MaximumReferenceDistanceMeters
        ? std::optional<ReferenceMatch>(best) : std::nullopt;
}

PresentationPolicy presentationPolicy(const QString &channelName)
{
    const QString name = channelName.toLower();
    if (name.contains(QStringLiteral("throttle")) || name.contains(QStringLiteral("brake")))
        return {0.10, 0.75, InterpolationMode::Linear};
    if (name.contains(QStringLiteral("lateral")) || name.contains(QStringLiteral("longitudinal"))
        || name.contains(QStringLiteral("accel")) || name.contains(QStringLiteral("gforce")))
        return {0.20, 0.75, InterpolationMode::Linear};
    if (name.contains(QStringLiteral("heart")) || name.contains(QStringLiteral("hr"))
        || name.contains(QStringLiteral("bpm")))
        return {0.25, 2.0, InterpolationMode::Linear};
    if (name.contains(QStringLiteral("gear")))
        return {0.0, 0.75, InterpolationMode::Previous};
    if (name.contains(QStringLiteral("speed")) || name.contains(QStringLiteral("rpm")))
        return {0.15, 0.75, InterpolationMode::Linear};
    return {0.15, 0.75, InterpolationMode::Linear};
}

std::optional<double> presentationValueAt(
    const TelemetrySession &session, const QString &channelName, const double time)
{
    const QString resolved = session.aliases.value(channelName, channelName);
    const auto channelIterator = session.channels.constFind(resolved);
    if (channelIterator == session.channels.cend()) return std::nullopt;
    const TelemetryChannel &channel = channelIterator.value();
    if (channel.timestamps().size() != channel.values().size() || channel.timestamps().isEmpty())
        return std::nullopt;

    const PresentationPolicy policy = presentationPolicy(channelName + QLatin1Char(' ') + resolved);
    std::optional<double> presented = session.valueAt(channelName, time, policy.interpolation);
    const auto next = std::lower_bound(channel.timestamps().cbegin(), channel.timestamps().cend(), time);
    if (presented && next != channel.timestamps().cbegin() && next != channel.timestamps().cend()
        && *next != time) {
        const double gap = *next - *(next - 1);
        if (gap > telemetryGapThreshold(channel, policy.staleSeconds)) presented.reset();
    }
    if (!presented) {
        const auto after = std::upper_bound(channel.timestamps().cbegin(), channel.timestamps().cend(), time);
        for (auto iterator = after; iterator != channel.timestamps().cbegin();) {
            --iterator;
            const qsizetype index = std::distance(channel.timestamps().cbegin(), iterator);
            if (time - *iterator > policy.staleSeconds) break;
            if (std::isfinite(channel.values()[index])) {
                presented = channel.values()[index];
                break;
            }
        }
    }
    if (!presented || policy.smoothingSeconds <= 0.0) return presented;

    double weightedTotal = *presented;
    double totalWeight = 1.0;
    const double windowStart = time - policy.smoothingSeconds;
    auto iterator = std::lower_bound(channel.timestamps().cbegin(), channel.timestamps().cend(), windowStart);
    for (; iterator != channel.timestamps().cend() && *iterator <= time; ++iterator) {
        const qsizetype index = std::distance(channel.timestamps().cbegin(), iterator);
        if (qFuzzyCompare(*iterator, time) || !std::isfinite(channel.values()[index])) continue;
        const double weight = std::max(0.0, 1.0 - (time - *iterator) / policy.smoothingSeconds);
        weightedTotal += channel.values()[index] * weight;
        totalWeight += weight;
    }
    const double smoothed = weightedTotal / totalWeight;
    return std::isfinite(smoothed) ? std::optional<double>(smoothed) : std::nullopt;
}

} // namespace

TelemetryRenderContext::TelemetryRenderContext(QObject *parent)
    : QObject(parent)
{
}

double TelemetryRenderContext::time() const { return m_time; }

QVariantList TelemetryRenderContext::trackPoints() const
{
    return m_trackPoints;
}

quint64 TelemetryRenderContext::trackRevision() const { return m_trackRevision; }
quint64 TelemetryRenderContext::trackConversionCount() const { return m_trackConversionCount; }

QVariantMap TelemetryRenderContext::currentTrackPoint() const
{
    if (!m_session || !m_geometry) {
        return {};
    }
    const auto time = videoToTelemetryTime(m_time, m_sync);
    if (!time) return {};
    const auto point = FlappedEar::currentTrackPoint(*m_session, *time, *m_geometry);
    return point ? QVariantMap{{"x", point->x()}, {"y", point->y()}} : QVariantMap();
}

QVariantMap TelemetryRenderContext::fixedLapTiming(const int lapNumber) const
{
    QVariantMap result{{QStringLiteral("state"), QStringLiteral("unavailable")}};
    if (m_lapSession.timedLaps.isEmpty()) return result;
    const TimedLap *best = nullptr;
    for (const auto &lap : m_lapSession.timedLaps)
        if (lap.referenceEligible() && (!best || lap.durationSeconds < best->durationSeconds)) best = &lap;
    const TimedLap *chosen = lapNumber == 0 ? best : nullptr;
    for (const auto &lap : m_lapSession.timedLaps)
        if (lapNumber > 0 && lap.number == lapNumber) chosen = &lap;
    if (!chosen) return result;
    result.insert(QStringLiteral("lapNumber"), chosen->number);
    result.insert(QStringLiteral("durationSeconds"), chosen->durationSeconds);
    result.insert(QStringLiteral("isBest"), best && best->number == chosen->number);
    const auto transformed = videoToTelemetryTime(m_time, m_sync);
    if (!transformed || !std::isfinite(*transformed)) return result;
    // Before the start-line crossing, then running, then held at the final time.
    const double elapsed = *transformed - chosen->startTelemetryTime;
    if (elapsed < 0.0) {
        result.insert(QStringLiteral("state"), QStringLiteral("before"));
        result.insert(QStringLiteral("elapsedSeconds"), 0.0);
    } else if (*transformed < chosen->endTelemetryTime) {
        result.insert(QStringLiteral("state"), QStringLiteral("running"));
        result.insert(QStringLiteral("elapsedSeconds"), elapsed);
    } else {
        result.insert(QStringLiteral("state"), QStringLiteral("finished"));
        result.insert(QStringLiteral("elapsedSeconds"), chosen->durationSeconds);
    }
    return result;
}

QVariantMap TelemetryRenderContext::lapTiming() const
{
    QVariantMap result{{QStringLiteral("available"), false},
                       {QStringLiteral("state"), QStringLiteral("unavailable")}};
    if ((m_lapSession.status != LapSessionStatus::Available
         && m_lapSession.status != LapSessionStatus::InsufficientPasses)
        || m_lapSession.acceptedPasses.isEmpty()) {
        return result;
    }

    const auto transformed = videoToTelemetryTime(m_time, m_sync);
    if (!transformed) return result;
    result.insert(QStringLiteral("available"), true);
    const double currentTime = *transformed;
    const double firstPassTime = m_lapSession.acceptedPasses.constFirst().telemetryTime;
    if (!std::isfinite(currentTime) || currentTime < firstPassTime) {
        result.insert(QStringLiteral("state"), QStringLiteral("waiting"));
        return result;
    }

    const TimedLap *bestCompletedLap = nullptr;
    const auto completedEnd = std::upper_bound(
        m_lapSession.timedLaps.cbegin(), m_lapSession.timedLaps.cend(), currentTime,
        [](const double time, const TimedLap &lap) { return time < lap.endTelemetryTime; });
    if (completedEnd != m_lapSession.timedLaps.cbegin()) {
        const TimedLap &lastLap = *(completedEnd - 1);
        for (auto lap = m_lapSession.timedLaps.cbegin(); lap != completedEnd; ++lap) {
            if (lap->referenceEligible() && (!bestCompletedLap
                || lap->durationSeconds < bestCompletedLap->durationSeconds)) bestCompletedLap = &*lap;
        }
        result.insert(QStringLiteral("lastLapNumber"), lastLap.number);
        result.insert(QStringLiteral("lastLapSeconds"), lastLap.durationSeconds);
        if (bestCompletedLap) {
            result.insert(QStringLiteral("bestLapNumber"), bestCompletedLap->number);
            result.insert(QStringLiteral("bestLapSeconds"), bestCompletedLap->durationSeconds);
            if (lastLap.referenceEligible()) result.insert(QStringLiteral("lastDeltaToBestSeconds"),
                lastLap.durationSeconds - bestCompletedLap->durationSeconds);
            result.insert(QStringLiteral("lastLapIsBest"), lastLap.number == bestCompletedLap->number);
        }
    }

    if (m_session && m_session->duration > 0.0 && currentTime > m_session->duration) {
        result.insert(QStringLiteral("state"), QStringLiteral("finished"));
        return result;
    }

    const auto nextPass = std::upper_bound(
        m_lapSession.acceptedPasses.cbegin(), m_lapSession.acceptedPasses.cend(), currentTime,
        [](const double time, const GatePass &pass) { return time < pass.telemetryTime; });
    const qsizetype passIndex = std::distance(
        m_lapSession.acceptedPasses.cbegin(), nextPass) - 1;
    const GatePass &currentStart = m_lapSession.acceptedPasses[passIndex];
    const double currentElapsed = std::max(0.0, currentTime - currentStart.telemetryTime);
    result.insert(QStringLiteral("state"), QStringLiteral("running"));
    result.insert(QStringLiteral("currentLapNumber"), static_cast<int>(passIndex + 1));
    result.insert(QStringLiteral("currentElapsedSeconds"), currentElapsed);
    const auto currentSpeed = m_session
        ? presentationValueAt(*m_session, QStringLiteral("speed"), currentTime)
        : std::nullopt;
    if (currentSpeed && std::isfinite(*currentSpeed)) {
        result.insert(QStringLiteral("currentSpeedKmh"), *currentSpeed);
    }

    if (bestCompletedLap && m_lapSession.selectedStartGate) {
        const auto referenceTrace = std::find_if(
            m_lapSession.lapTraces.cbegin(), m_lapSession.lapTraces.cend(),
            [bestCompletedLap](const LapTrace &trace) {
                return trace.lapNumber == bestCompletedLap->number;
            });
        const auto latitude = m_session
            ? m_session->valueAt("latitude", currentTime, InterpolationMode::Linear)
            : std::nullopt;
        const auto longitude = m_session
            ? m_session->valueAt("longitude", currentTime, InterpolationMode::Linear)
            : std::nullopt;
        if (referenceTrace != m_lapSession.lapTraces.cend() && latitude && longitude) {
            const GeoCoordinate currentCoordinate{*latitude, *longitude};
            const TimingGate &gate = *m_lapSession.selectedStartGate;
            const GeoCoordinate origin{
                (gate.endpointA.latitudeDegrees + gate.endpointB.latitudeDegrees) / 2.0,
                (gate.endpointA.longitudeDegrees + gate.endpointB.longitudeDegrees) / 2.0};
            if (isValidCoordinate(currentCoordinate) && isValidCoordinate(origin)) {
                const MetricPoint currentPoint = projectCoordinate(currentCoordinate, origin);
                const auto reference = closestReferenceMatch(
                    *referenceTrace, currentPoint,
                    std::clamp(currentElapsed, 0.0, referenceTrace->durationSeconds));
                if (reference) {
                    result.insert(QStringLiteral("liveDeltaSeconds"),
                                  currentElapsed - reference->elapsedSeconds);
                    const auto referenceSpeed = presentationValueAt(
                        *m_session, QStringLiteral("speed"),
                        referenceTrace->startTelemetryTime + reference->elapsedSeconds);
                    if (currentSpeed && referenceSpeed && std::isfinite(*currentSpeed)
                        && std::isfinite(*referenceSpeed)) {
                        result.insert(QStringLiteral("referenceSpeedKmh"), *referenceSpeed);
                        result.insert(QStringLiteral("speedDeltaKmh"),
                                      *currentSpeed - *referenceSpeed);
                    }
                }
            }
        }
    }
    return result;
}

const TelemetrySession *TelemetryRenderContext::session() const { return m_session; }
SyncTransform TelemetryRenderContext::syncTransform() const { return m_sync; }

void TelemetryRenderContext::setSession(const TelemetrySession *session)
{
    // Sessions are immutable once loaded; map their tyre channels once.
    m_tyreChannels = session ? mapTyreChannels(*session) : TyreChannelMap{};
    m_pressureUnits.clear();
    if (m_session == session) {
        return;
    }
    m_session = session;
    emit sourceChanged();
    emit timeChanged();
}

void TelemetryRenderContext::setTrackGeometry(const TrackGeometry *geometry)
{
    m_geometry = geometry;
    m_trackPoints.clear();
    if (m_geometry) {
        m_trackPoints.reserve(m_geometry->points.size());
        for (const QPointF &point : m_geometry->points) {
            m_trackPoints.append(point);
        }
        ++m_trackConversionCount;
    }
    ++m_trackRevision;
    emit trackGeometryChanged();
    emit sourceChanged();
    emit timeChanged();
}

void TelemetryRenderContext::setLapSession(const LapSession &lapSession)
{
    m_lapSession = lapSession;
    emit sourceChanged();
    emit timeChanged();
}

void TelemetryRenderContext::setSyncTransform(const SyncTransform transform)
{
    if (!std::isfinite(transform.offset) || !std::isfinite(transform.timeScale)
        || transform.timeScale <= 0.0
        || (qFuzzyCompare(m_sync.offset, transform.offset)
            && qFuzzyCompare(m_sync.timeScale, transform.timeScale))) {
        return;
    }
    m_sync = transform;
    emit syncTransformChanged();
    emit timeChanged();
}

QVariant TelemetryRenderContext::telemetryValue(const QString &channelName) const
{
    if (!m_session || channelName.isEmpty()) {
        return {};
    }
    const auto time = videoToTelemetryTime(m_time, m_sync);
    if (!time) return {};
    const auto value = presentationValueAt(*m_session, channelName, *time);
    return value ? QVariant(*value) : QVariant();
}

QVariant TelemetryRenderContext::telemetryValueAgo(const QString &channelName, const double secondsAgo) const
{
    if (!m_session || channelName.isEmpty() || !std::isfinite(secondsAgo) || secondsAgo < 0.0) {
        return {};
    }
    const auto time = videoToTelemetryTime(m_time - secondsAgo, m_sync);
    if (!time) return {};
    const auto value = presentationValueAt(*m_session, channelName, *time);
    return value ? QVariant(*value) : QVariant();
}

QString TelemetryRenderContext::valueText(const QString &channelName, const int decimals) const
{
    const QVariant value = telemetryValue(channelName);
    return value.isValid() ? QString::number(value.toDouble(), 'f', qBound(0, decimals, 6))
                           : QStringLiteral("—");
}

QVariantMap TelemetryRenderContext::tyreValues(const QVariantMap &settings) const
{
    QVariantMap result;
    std::array<QString, tyreCornerCount> temperatureChoice;
    std::array<QString, tyreCornerCount> pressureChoice;
    for (int index = 0; index < tyreCornerCount; ++index) {
        const QString code = tyreCornerCode(static_cast<TyreCorner>(index));
        temperatureChoice[index] = settings.value(QStringLiteral("temperatureSource") + code).toString();
        pressureChoice[index] = settings.value(QStringLiteral("pressureSource") + code).toString();
    }
    const TyreChannelMap channels = m_session
        ? withTyreChannelChoices(*m_session, m_tyreChannels, temperatureChoice, pressureChoice, &m_pressureUnits)
        : TyreChannelMap{};
    const bool available = m_session && channels.hasAny();
    result.insert(QStringLiteral("available"), available);
    if (!available) return result;
    const auto time = videoToTelemetryTime(m_time, m_sync);
    QVariantList corners;
    for (int index = 0; index < tyreCornerCount; ++index) {
        const auto corner = static_cast<TyreCorner>(index);
        const TyreReading reading = time ? tyreReadingAt(*m_session, channels, corner, *time) : TyreReading{};
        QVariantMap entry;
        entry.insert(QStringLiteral("corner"), tyreCornerCode(corner));
        entry.insert(QStringLiteral("hasTemperature"), reading.temperatureCelsius.has_value());
        entry.insert(QStringLiteral("hasPressure"), reading.pressureBar.has_value());
        if (reading.temperatureCelsius) entry.insert(QStringLiteral("temperature"), *reading.temperatureCelsius);
        if (reading.pressureBar) entry.insert(QStringLiteral("pressure"), *reading.pressureBar);
        entry.insert(QStringLiteral("pressureSourceUnit"), pressureUnitName(channels.pressureUnit[index]));
        corners.append(entry);
    }
    result.insert(QStringLiteral("corners"), corners);
    return result;
}

bool TelemetryRenderContext::tyreChannelsMissing() const { return m_session && !m_tyreChannels.hasAny(); }

QVariant TelemetryRenderContext::telemetryTime() const
{
    const auto time = videoToTelemetryTime(m_time, m_sync);
    return time ? QVariant(*time) : QVariant();
}

void TelemetryRenderContext::setTime(const double time)
{
    if (!std::isfinite(time) || qFuzzyCompare(m_time, time)) {
        return;
    }
    m_time = time;
    emit timeChanged();
}

} // namespace FlappedEar
