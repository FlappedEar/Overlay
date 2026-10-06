#include "telemetry/RecordingAlignment.h"

#include "telemetry/OutingLaps.h"
#include "telemetry/TelemetrySyncEngine.h"

#include <algorithm>
#include <cmath>

namespace FlappedEar {
namespace {

const TelemetryChannel *speedChannel(const TelemetrySession &session)
{
    const auto found = session.channels.constFind(session.aliases.value(QStringLiteral("speed")));
    return found == session.channels.cend() || found->timestamps().isEmpty() ? nullptr : &*found;
}

// A session holding only the speed samples of `channel` in [from, to], on
// the original clock.
TelemetrySession speedSlice(const TelemetryChannel &channel, const double from, const double to)
{
    TelemetryChannel slice;
    slice.name = channel.name;
    slice.unit = channel.unit;
    for (qsizetype index = 0; index < channel.timestamps().size(); ++index) {
        const double time = channel.timestamps()[index];
        if (time < from || time > to) continue;
        slice.appendSample(time, channel.values()[index]);
    }
    TelemetrySession session;
    session.channels.insert(slice.name, slice);
    session.aliases.insert(QStringLiteral("speed"), slice.name);
    return session;
}

} // namespace

RecordingAlignment alignRecordings(const TelemetrySession &primary, const TelemetrySession &candidate,
    const CancellationCheck &cancelled, const RecordingAlignmentOptions &options)
{
    RecordingAlignment result;
    const auto primaryStart = recordingTimestamp(primary), candidateStart = recordingTimestamp(candidate);
    if (primaryStart && candidateStart)
        result.declaredOffset = static_cast<double>(*candidateStart - *primaryStart) / 1000.0;

    const auto *primarySpeed = speedChannel(primary);
    const auto *candidateSpeed = speedChannel(candidate);
    if (!primarySpeed || !candidateSpeed) {
        result.reason = QStringLiteral("noSpeed");
        return result;
    }
    SyncCandidate whole;
    try {
        whole = TelemetrySyncEngine::synchronize(candidate, primary, cancelled);
    } catch (const OperationCancelled &) {
        throw;
    } catch (const std::exception &error) {
        result.reason = QString::fromUtf8(error.what());
        return result;
    }
    result.correlation = whole.diagnostics.correlation;
    result.peakUniqueness = whole.diagnostics.peakUniqueness;
    result.confidence = whole.confidence;
    // The candidate's span that the primary covers under the whole offset.
    const double from = std::max(candidateSpeed->timestamps().constFirst(), primarySpeed->timestamps().constFirst() - whole.offset);
    const double to = std::min(candidateSpeed->timestamps().constLast(), primarySpeed->timestamps().constLast() - whole.offset);
    result.overlapSeconds = std::max(0.0, to - from);
    if (result.overlapSeconds < kMinimumSyncOverlapSeconds) {
        result.reason = QStringLiteral("shortOverlap");
        return result;
    }

    // Windows along the overlap, each matched only near the whole offset so a
    // lap-periodic speed trace cannot match the wrong lap.
    const int count = std::clamp(static_cast<int>(result.overlapSeconds / options.minimumWindowSeconds), 1, options.maximumWindows);
    const double width = result.overlapSeconds / count;
    constexpr double searchSeconds = 10.0;
    for (int index = 0; index < count; ++index) {
        throwIfCancelled(cancelled);
        const double start = from + index * width, end = start + width;
        AlignmentWindow window{(start + end) / 2.0, whole.offset, -1.0, false};
        try {
            const auto part = TelemetrySyncEngine::synchronize(speedSlice(*candidateSpeed, start, end),
                speedSlice(*primarySpeed, start + whole.offset - searchSeconds, end + whole.offset + searchSeconds), cancelled);
            window.offset = part.offset;
            window.correlation = part.diagnostics.correlation;
            window.used = window.correlation >= options.minimumWindowCorrelation
                && std::abs(part.offset - whole.offset) < searchSeconds;
        } catch (const OperationCancelled &) {
            throw;
        } catch (const std::exception &) {
            // Too few samples or no variation in this window: not evidence.
        }
        result.windows.append(window);
        if (window.used) ++result.usedWindows;
    }

    // Offset and drift: a least-squares line through the used windows.
    QVector<const AlignmentWindow *> used;
    for (const auto &window : result.windows) if (window.used) used.append(&window);
    double residual = 0.0;
    if (used.size() >= 3) {
        double meanT = 0.0, meanO = 0.0;
        for (const auto *window : used) { meanT += window->candidateTime; meanO += window->offset; }
        meanT /= used.size(); meanO /= used.size();
        double covariance = 0.0, variance = 0.0;
        for (const auto *window : used) {
            covariance += (window->candidateTime - meanT) * (window->offset - meanO);
            variance += (window->candidateTime - meanT) * (window->candidateTime - meanT);
        }
        const double slope = variance > 0.0 ? covariance / variance : 0.0;
        result.offset = meanO - slope * meanT;
        result.driftPpm = slope * 1e6;
        double squares = 0.0;
        for (const auto *window : used) {
            const double error = window->offset - (*result.offset + slope * window->candidateTime);
            squares += error * error;
        }
        residual = std::sqrt(squares / (used.size() - 2));
        // A drift is reported only when it moves the offset across the
        // windows by clearly more than the offsets can be resolved; below
        // that it would be precision the recordings do not have.
        const double span = used.last()->candidateTime - used.first()->candidateTime;
        if (std::abs(slope) * span < 2.0 * std::max(0.05, residual)) {
            result.offset = meanO;
            result.driftPpm.reset();
            double spread = 0.0;
            for (const auto *window : used) spread += (window->offset - meanO) * (window->offset - meanO);
            residual = std::sqrt(spread / (used.size() - 1));
        }
    } else if (!used.isEmpty()) {
        double sum = 0.0, low = used.first()->offset, high = low;
        for (const auto *window : used) {
            sum += window->offset;
            low = std::min(low, window->offset);
            high = std::max(high, window->offset);
        }
        result.offset = sum / used.size();
        residual = (high - low) / 2.0;
    } else {
        result.offset = whole.offset;
    }
    // The engine resolves offsets on a 10 Hz grid.
    result.uncertaintySeconds = std::max(0.05, residual);

    const auto ambiguous = [&result](const QString &reason) {
        result.status = alignmentAmbiguous;
        result.reason = reason;
        return result;
    };
    if (whole.diagnostics.correlation < options.minimumWindowCorrelation) return ambiguous(QStringLiteral("weakMatch"));
    if (used.size() < 2) return ambiguous(QStringLiteral("tooFewWindows"));
    if (residual > options.maximumResidualSeconds) return ambiguous(QStringLiteral("windowsDisagree"));
    if (result.driftPpm && std::abs(*result.driftPpm) > options.maximumPlausibleDriftPpm)
        return ambiguous(QStringLiteral("implausibleDrift"));
    const bool declaredAgrees = result.declaredOffset
        && std::abs(*result.declaredOffset - *result.offset) <= std::max(options.declaredToleranceSeconds, 3.0 * *result.uncertaintySeconds);
    if (result.declaredOffset && !declaredAgrees) {
        result.status = alignmentConflicting;
        result.reason = QStringLiteral("declaredClockDisagrees");
        return result;
    }
    // Laps repeat: the speed traces often also match one lap away. The match
    // alone decides only when it is unique; otherwise an agreeing declared
    // clock can tell which lap it is, and without one it stays ambiguous.
    if (whole.confidence < kAutomaticSyncConfidenceThreshold) {
        if (!declaredAgrees) return ambiguous(QStringLiteral("repeatedMatch"));
        result.resolvedByDeclaredClock = true;
    }
    result.status = alignmentAligned;
    result.reason.clear();
    return result;
}

} // namespace FlappedEar
