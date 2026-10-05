#include "telemetry/TelemetrySyncEngine.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <optional>
#include <stdexcept>

namespace FlappedEar {

SyncConfidenceLevel syncConfidenceLevel(const double confidence)
{
    if (!std::isfinite(confidence) || confidence < 0.0 || confidence > 1.0)
        return SyncConfidenceLevel::Low;
    if (confidence >= kAutomaticSyncConfidenceThreshold) {
        return SyncConfidenceLevel::High;
    }
    // The medium band is useful feedback for a human reviewer, but neither
    // medium nor low candidates change the current transform automatically.
    return confidence >= 0.45 ? SyncConfidenceLevel::Medium : SyncConfidenceLevel::Low;
}

bool shouldAutoApplySyncCandidate(const SyncCandidate &candidate)
{
    return std::isfinite(candidate.offset) && std::isfinite(candidate.timeScale)
        && candidate.timeScale > 0.0
        && syncConfidenceLevel(candidate.confidence) == SyncConfidenceLevel::High;
}

namespace {

void validateSignal(const TelemetryChannel &signal, const CancellationCheck &cancelled)
{
    if (signal.timestamps.size() != signal.values.size() || signal.values.size() < 20)
        throw std::runtime_error("Synchronization requires at least 20 aligned GPS speed samples.");
    if (signal.values.size() > kMaximumSyncSignalSamples)
        throw ResourceLimitError("Synchronization signal exceeds the 1000000-sample limit.");
    for (qsizetype i = 0; i < signal.timestamps.size(); ++i) {
        if ((i & 0xff) == 0) throwIfCancelled(cancelled);
        const double time = signal.timestamps[i];
        if (!std::isfinite(time) || (i && time <= signal.timestamps[i - 1]))
            throw std::runtime_error("Synchronization timestamps must be finite and strictly increasing.");
    }
}

int gridCount(const double span, const double step, const int maximum)
{
    const double intervals = std::floor(span / step);
    if (!std::isfinite(span) || span < 0.0 || !std::isfinite(intervals)
        || intervals >= maximum)
        throw ResourceLimitError("Synchronization grid exceeds the supported time/search budget.");
    return static_cast<int>(intervals) + 1;
}

double gridTime(const double start, const double step, const int index)
{
    const double value = start + index * step;
    if (!std::isfinite(value) || (index && value <= start + (index - 1) * step))
        throw std::runtime_error("Synchronization time grid exceeds supported numeric precision.");
    return value;
}

struct Result {
    double offset = 0.0;
    double score = -1.0;
    int samples = 0;
    // Evidence for a positive correlation over this many samples (Fisher z
    // times sqrt(n - 3)). It ranks the offsets: a near-perfect match over a
    // short overlap is weaker evidence than a strong one over a long overlap.
    double significance = -std::numeric_limits<double>::infinity();
};

double significanceOf(const double score, const int samples)
{
    if (samples <= 3 || !std::isfinite(score)) return -std::numeric_limits<double>::infinity();
    return std::atanh(std::clamp(score, -0.999999, 0.999999)) * std::sqrt(static_cast<double>(samples - 3));
}

double correlation(
    const QVector<double> &a,
    const QVector<double> &b,
    const CancellationCheck &cancelled)
{
    if (a.size() < 2 || a.size() != b.size()) {
        return -1.0;
    }
    const double meanA = std::accumulate(a.cbegin(), a.cend(), 0.0) / a.size();
    const double meanB = std::accumulate(b.cbegin(), b.cend(), 0.0) / b.size();
    double numerator = 0.0;
    double varianceA = 0.0;
    double varianceB = 0.0;
    for (qsizetype index = 0; index < a.size(); ++index) {
        if ((index & 0xfff) == 0) throwIfCancelled(cancelled);
        const double da = a[index] - meanA;
        const double db = b[index] - meanB;
        numerator += da * db;
        varianceA += da * da;
        varianceB += db * db;
    }
    return varianceA > 0.0 && varianceB > 0.0
        ? std::clamp(numerator / std::sqrt(varianceA * varianceB), -1.0, 1.0)
        : -1.0;
}

SyncCandidate calculate(
    const TelemetryChannel &video,
    const TelemetryChannel &telemetry,
    const double searchWindow,
    const double sampleRate,
    const double centerOffset,
    const bool rankBySignificance,
    qint64 &remainingPairs,
    const CancellationCheck &cancelled)
{
    const double step = 1.0 / sampleRate;
    const double firstOffset = centerOffset - searchWindow;
    const double lastOffset = centerOffset + searchWindow;
    if (!std::isfinite(firstOffset) || !std::isfinite(lastOffset))
        throw std::runtime_error("Synchronization offset range is not finite.");
    const int offsets = gridCount(lastOffset - firstOffset + step / 2.0, step, kMaximumSyncOffsets);
    const int samples = gridCount(video.timestamps.constLast() - video.timestamps.constFirst(),
                                  step, kMaximumSyncGridSamples);
    const qint64 pairs = static_cast<qint64>(offsets) * samples;
    if (pairs > remainingPairs)
        throw ResourceLimitError("Synchronization exceeds the 50000000 sample-pair work budget.");
    remainingPairs -= pairs;
    // Validate the grid's precision before allocating or doing correlation work.
    (void) gridTime(firstOffset, step, offsets - 1);
    (void) gridTime(video.timestamps.constFirst(), step, samples - 1);
    QVector<Result> results;
    results.reserve(offsets);
    for (int offsetIndex = 0; offsetIndex < offsets; ++offsetIndex) {
        throwIfCancelled(cancelled);
        const double offset = gridTime(firstOffset, step, offsetIndex);
        QVector<double> a;
        QVector<double> b;
        a.reserve(samples);
        b.reserve(samples);
        for (int sampleIndex = 0; sampleIndex < samples; ++sampleIndex) {
            if ((sampleIndex & 0xff) == 0) throwIfCancelled(cancelled);
            const double time = gridTime(video.timestamps.constFirst(), step, sampleIndex);
            // KAN-157: the shared lookup, so a loss of GPS fix is not bridged
            // into a ramp that correlates.
            const auto av = telemetryValueAt(video, time);
            const auto bv = telemetryValueAt(telemetry, time + offset);
            if (av && bv && std::isfinite(*av) && std::isfinite(*bv)) {
                a.append(*av);
                b.append(*bv);
            }
        }
        const double score = correlation(a, b, cancelled);
        results.append({offset, score, static_cast<int>(a.size()), significanceOf(score, static_cast<int>(a.size()))});
    }
    throwIfCancelled(cancelled);
    // A correlation over a handful of overlapping points can be perfect by
    // accident: offsets without the minimum overlap are ranked only when no
    // offset has it (KAN-146).
    const double minimumSamples = sampleRate * kMinimumSyncOverlapSeconds + 1.0;
    const bool anyLongOverlap = std::any_of(results.cbegin(), results.cend(),
        [minimumSamples](const Result &item) { return item.samples >= minimumSamples; });
    if (anyLongOverlap)
        results.erase(std::remove_if(results.begin(), results.end(),
            [minimumSamples](const Result &item) { return item.samples < minimumSamples; }), results.end());
    // The global search ranks by significance; the local refinement, where
    // the overlap is nearly constant, by correlation alone.
    std::stable_sort(results.begin(), results.end(), [rankBySignificance](const Result &left, const Result &right) {
        return rankBySignificance ? left.significance > right.significance : left.score > right.score;
    });
    const Result best = results.constFirst();
    // The strongest competing offset, at least 5 s away, over the whole range.
    double second = -1.0;
    for (const Result &item : results) {
        if (std::abs(item.offset - best.offset) >= 5.0) {
            second = item.score;
            break;
        }
    }
    const double uniqueness = std::clamp((best.score - second) / 0.25, 0.0, 1.0);
    const double durationScore = std::min(1.0, best.samples / (sampleRate * 20.0));
    const double strength = std::clamp((best.score + 1.0) / 2.0, 0.0, 1.0);
    SyncCandidate candidate;
    const double milliseconds = best.offset * 1000.0;
    if (!std::isfinite(milliseconds))
        throw std::runtime_error("Synchronization offset exceeds millisecond rounding range.");
    candidate.offset = std::round(milliseconds) / 1000.0;
    candidate.confidence = std::round(
        100.0 * strength * (0.35 + 0.4 * uniqueness + 0.25 * durationScore))
        / 100.0;
    // A correlation over a handful of points can be perfect by accident. Require
    // twenty seconds worth of usable resampled overlap before automatic use.
    if (best.samples < minimumSamples || best.score < kMinimumAutomaticSyncCorrelation) {
        candidate.confidence = std::min(candidate.confidence, kAutomaticSyncConfidenceThreshold - 0.01);
    }
    candidate.diagnostics = {best.score, uniqueness, best.samples, sampleRate, 0.0};
    return candidate;
}

} // namespace

SyncCandidate TelemetrySyncEngine::synchronize(
    const TelemetrySession &video,
    const TelemetrySession &telemetry,
    const CancellationCheck &cancelled)
{
    throwIfCancelled(cancelled);
    const auto videoIt = video.channels.constFind(video.aliases.value("speed"));
    const auto telemetryIt = telemetry.channels.constFind(telemetry.aliases.value("speed"));
    if (videoIt == video.channels.cend() || telemetryIt == telemetry.channels.cend()) {
        throw std::runtime_error("GPS speed is not available in both telemetry sources.");
    }
    const TelemetryChannel &videoSpeed = *videoIt;
    const TelemetryChannel &telemetrySpeed = *telemetryIt;
    validateSignal(videoSpeed, cancelled);
    validateSignal(telemetrySpeed, cancelled);
    // Every offset (telemetry = video + offset) that leaves the minimum
    // overlap, in both directions: the camera may start before the logger or
    // stop after it (KAN-146). When either recording is shorter than that,
    // the shorter one must lie fully inside the other.
    const double videoFirst = videoSpeed.timestamps.constFirst(), videoLast = videoSpeed.timestamps.constLast();
    const double telemetryFirst = telemetrySpeed.timestamps.constFirst(), telemetryLast = telemetrySpeed.timestamps.constLast();
    const double requiredOverlap = std::min({kMinimumSyncOverlapSeconds, videoLast - videoFirst, telemetryLast - telemetryFirst});
    double minimum = telemetryFirst - videoLast + requiredOverlap;
    double maximum = telemetryLast - videoFirst - requiredOverlap;
    if (maximum < minimum) std::swap(minimum, maximum);
    if (!std::isfinite(minimum) || !std::isfinite(maximum))
        throw std::runtime_error("Synchronization timestamp differences are not finite.");
    const double center = std::midpoint(minimum, maximum);
    const double window = std::max(1.0, (maximum - minimum) / 2.0);
    qint64 remainingPairs = kMaximumSyncSamplePairs;
    const SyncCandidate coarse = calculate(videoSpeed, telemetrySpeed, window, 1.0, center, true, remainingPairs, cancelled);
    SyncCandidate fine = calculate(videoSpeed, telemetrySpeed, 5.0, 10.0, coarse.offset, false, remainingPairs, cancelled);
    throwIfCancelled(cancelled);
    // Refinement estimates a more precise offset, but cannot erase competing
    // peaks outside its local window or improve the global evidence of uniqueness.
    fine.confidence = std::min(fine.confidence, coarse.confidence);
    fine.diagnostics.peakUniqueness = std::min(
        fine.diagnostics.peakUniqueness, coarse.diagnostics.peakUniqueness);
    fine.diagnostics.coarseOffset = coarse.offset;
    return fine;
}

} // namespace FlappedEar
