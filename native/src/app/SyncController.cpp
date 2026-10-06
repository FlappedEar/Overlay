#include "app/SyncController.h"

#include "app/AppLog.h"
#include "gopro/GoProTelemetrySource.h"
#include "telemetry/SourceOperation.h"

#include <QtConcurrent>

#include <cmath>
#include <utility>

namespace FlappedEar {

SyncController::SyncController(CurrentSources currentSources, QObject *parent)
    : QObject(parent)
    , m_currentSources(std::move(currentSources))
{
    connect(&m_syncWatcher, &QFutureWatcher<AutoSyncResult>::finished, this, &SyncController::finish);
}

void SyncController::finish()
{
    const AutoSyncResult result = m_syncWatcher.result();
    emit runningChanged();
    const Sources current = m_currentSources();
    if (result.generation != current.generation
        || result.syncRevision != m_syncRevision
        || result.videoPath != current.videoPath
        || result.vboPath != current.vboPath) {
        AppLog::warn(QStringLiteral("Stale auto-sync result rejected"));
        return;
    }
    if (!result.success) {
        if (result.cancelled) {
            AppLog::warn(QStringLiteral("Auto-sync cancelled"));
            emit statusMessage(QStringLiteral("Auto sync cancelled."));
            return;
        }
        AppLog::error(QStringLiteral("Auto-sync failed: %1").arg(result.error));
        m_candidate.clear();
        emit candidateChanged();
        emit statusMessage(QStringLiteral("Auto sync failed: %1").arg(result.error));
        return;
    }
    const bool automaticallyApplied = shouldAutoApplySyncCandidate(result.candidate);
    if (automaticallyApplied) {
        setOffset(result.candidate.offset);
        setTimeScale(result.candidate.timeScale);
    }
    m_candidate = {
        {"offset", result.candidate.offset},
        {"timeScale", result.candidate.timeScale},
        {"confidence", result.candidate.confidence},
        {"correlation", result.candidate.diagnostics.correlation},
        {"peakUniqueness", result.candidate.diagnostics.peakUniqueness},
        {"validSamples", result.candidate.diagnostics.validSamples},
        {"coarseOffset", result.candidate.diagnostics.coarseOffset},
        {"packetCount", result.packetCount},
        {"gpsSampleCount", result.gpsSampleCount},
        {"gpsStream", result.gpsStream},
        {"level", candidateLevelName(result.candidate.confidence)},
        {"automaticallyApplied", automaticallyApplied},
        {"canApply", true},
    };
    emit candidateChanged();
    AppLog::info(QStringLiteral("Auto-sync result: offset=%1 s, confidence=%2%, %3")
                     .arg(result.candidate.offset, 0, 'f', 3)
                     .arg(result.candidate.confidence * 100.0, 0, 'f', 0)
                     .arg(automaticallyApplied ? QStringLiteral("applied")
                                               : QStringLiteral("review required")));
    emit statusMessage(QStringLiteral("Auto sync %1: %2 s · correlation %3 · confidence %4% · %5 GPS samples")
                  .arg(automaticallyApplied ? QStringLiteral("applied")
                                            : QStringLiteral("candidate requires review"))
                  .arg(result.candidate.offset, 0, 'f', 3)
                  .arg(result.candidate.diagnostics.correlation, 0, 'f', 3)
                  .arg(result.candidate.confidence * 100.0, 0, 'f', 0)
                  .arg(result.gpsSampleCount));
}

void SyncController::start(const TelemetrySession &telemetry, const Sources &sources)
{
    AppLog::info(QStringLiteral("Auto-sync requested"));
    if (m_syncWatcher.isRunning()) {
        return;
    }
    const quint64 generation = sources.generation;
    const quint64 syncRevision = m_syncRevision;
    const QString normalizedVideoPath = sources.videoPath;
    const QString normalizedVboPath = sources.vboPath;
    m_syncCancellation = std::make_shared<std::atomic_bool>(false);
    const std::shared_ptr<std::atomic_bool> cancellation = m_syncCancellation;
    m_candidate.clear();
    emit candidateChanged();
    emit statusMessage("Indexing GoPro telemetry and matching GPS speed…");
    m_syncWatcher.setFuture(QtConcurrent::run(
        [normalizedVideoPath, normalizedVboPath, telemetry, generation, syncRevision, cancellation] {
        AutoSyncResult result;
        result.generation = generation;
        result.syncRevision = syncRevision;
        result.videoPath = normalizedVideoPath;
        result.vboPath = normalizedVboPath;
        try {
            if (cancellation->load()) {
                result.cancelled = true;
                return result;
            }
            const auto cancelled = [cancellation] { return cancellation->load(); };
            const GoProTelemetryResult videoTelemetry = GoProTelemetrySource::load(
                normalizedVideoPath, cancelled);
            if (cancellation->load()) {
                result.cancelled = true;
                return result;
            }
            result.candidate = TelemetrySyncEngine::synchronize(
                videoTelemetry.session, telemetry, cancelled);
            if (cancellation->load()) {
                result.cancelled = true;
                return result;
            }
            result.packetCount = videoTelemetry.packetCount;
            result.gpsSampleCount = videoTelemetry.session.sampleCount;
            result.gpsStream = videoTelemetry.gpsStream;
            result.success = true;
        } catch (const OperationCancelled &) {
            result.cancelled = true;
            result.error = QStringLiteral("Auto sync was cancelled.");
        } catch (const std::exception &error) {
            result.error = QString::fromUtf8(error.what());
        }
        return result;
    }));
    emit runningChanged();
}

void SyncController::cancel()
{
    if (m_syncCancellation) m_syncCancellation->store(true);
}

void SyncController::restore(const SyncTransform &transform)
{
    m_transform = transform;
    m_candidate.clear();
}

void SyncController::applyCandidate()
{
    if (m_candidate.isEmpty()) {
        return;
    }
    const QVariantMap candidate = m_candidate;
    const double offset = candidate.value("offset").toDouble();
    const double scale = m_candidate.value("timeScale", 1.0).toDouble();
    if (!std::isfinite(offset) || !std::isfinite(scale) || scale <= 0.0) {
        emit statusMessage("Synchronization candidate is invalid and cannot be applied.");
        return;
    }
    setOffset(offset);
    setTimeScale(scale);
    AppLog::info(QStringLiteral("Auto-sync candidate applied: offset=%1 s, scale=%2")
                     .arg(offset, 0, 'f', 3).arg(scale, 0, 'g', 12));
    m_candidate = candidate;
    m_candidate.insert("automaticallyApplied", true);
    m_candidate.insert("appliedManually", true);
    emit candidateChanged();
    emit statusMessage(QStringLiteral("Synchronization candidate applied: %1 s.").arg(offset, 0, 'f', 3));
}

void SyncController::ignoreCandidate()
{
    if (m_candidate.isEmpty()) {
        return;
    }
    AppLog::info(QStringLiteral("Auto-sync candidate rejected"));
    m_candidate.clear();
    emit candidateChanged();
    emit statusMessage("Synchronization candidate ignored; existing timing was retained.");
}

void SyncController::invalidateForTimingEdit()
{
    ++m_syncRevision;
    if (m_syncCancellation) m_syncCancellation->store(true);
    if (!m_candidate.isEmpty()) {
        m_candidate.clear();
        emit candidateChanged();
    }
}

void SyncController::setOffset(const double seconds)
{
    if (!std::isfinite(seconds) || qFuzzyCompare(m_transform.offset, seconds)) {
        return;
    }
    invalidateForTimingEdit();
    m_transform.offset = seconds;
    emit edited();
    emit changed();
}

void SyncController::setTimeScale(const double scale)
{
    if (!std::isfinite(scale) || scale <= 0.0 || qFuzzyCompare(m_transform.timeScale, scale)) {
        return;
    }
    invalidateForTimingEdit();
    m_transform.timeScale = scale;
    emit edited();
    emit changed();
}

QString SyncController::candidateLevelName(const double confidence)
{
    switch (syncConfidenceLevel(confidence)) {
    case SyncConfidenceLevel::High:
        return QStringLiteral("high");
    case SyncConfidenceLevel::Medium:
        return QStringLiteral("medium");
    case SyncConfidenceLevel::Low:
        return QStringLiteral("low");
    }
    return QStringLiteral("low");
}

} // namespace FlappedEar
