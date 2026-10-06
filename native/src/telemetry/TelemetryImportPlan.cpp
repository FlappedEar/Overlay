#include "telemetry/TelemetryImportPlan.h"
#include "telemetry/OutingLaps.h"

#include "telemetry/TelemetryGeometry.h"
#include "telemetry/TelemetrySource.h"

#include <QDir>
#include <QFileInfo>
#include <algorithm>
#include <array>
#include <cmath>

namespace FlappedEar {
namespace {

constexpr int signatureSize = 32;
using GpsSignature = std::array<std::optional<GeoCoordinate>, signatureSize>;

struct TraceEvidence {
    GpsSignature gps{};
    double duration = 0.0;
    bool usable = false;
};

void validateLimits(const TelemetryImportLimits &limits)
{
    const TelemetryImportLimits ceilings;
    if (limits.maximumFiles <= 0 || limits.maximumFiles > ceilings.maximumFiles
        || limits.maximumFileBytes <= 0 || limits.maximumFileBytes > ceilings.maximumFileBytes
        || limits.maximumBatchBytes <= 0 || limits.maximumBatchBytes > ceilings.maximumBatchBytes
        || limits.maximumRetainedChannelSamples <= 0
        || limits.maximumRetainedChannelSamples > ceilings.maximumRetainedChannelSamples) {
        throw std::invalid_argument("Import limits must be positive and no greater than the safety ceilings.");
    }
}

qsizetype channelSamples(const TelemetrySession &session, const qsizetype available,
                        const CancellationCheck &cancelled)
{
    qsizetype count = 0;
    if (!std::isfinite(session.duration) || session.duration < 0.0
        || !std::isfinite(session.startTime)) {
        throw std::runtime_error("Telemetry source has an invalid time range.");
    }
    for (auto channel = session.channels.cbegin(); channel != session.channels.cend(); ++channel) {
        throwIfCancelled(cancelled);
        if (channel->timestamps().size() != channel->values().size()) {
            throw std::runtime_error("Telemetry source has mismatched channel timestamps and values.");
        }
        if (channel->values().size() > available - count) {
            throw ResourceLimitError("Batch decoded-sample limit exceeded; import fewer recordings.");
        }
        count += channel->values().size();
    }
    return count;
}

const TelemetryChannel *gpsChannel(const TelemetrySession &session, const QString &alias)
{
    const auto channel = session.channels.constFind(session.aliases.value(alias, alias));
    if (channel == session.channels.cend() || channel->values().size() < 16
        || channel->timestamps().size() != channel->values().size()) return nullptr;
    return &channel.value();
}

std::optional<double> nearbyRawValue(const TelemetryChannel &channel, const double time)
{
    const auto &times = channel.timestamps();
    const auto next = std::lower_bound(times.cbegin(), times.cend(), time);
    qsizetype index = std::distance(times.cbegin(), next);
    if (index == times.size() || (index > 0 && time - times[index - 1] <= times[index] - time)) --index;
    // Raw samples only: do not interpolate over gaps or inherit overlay holding.
    if (index < 0 || std::abs(times[index] - time) > 0.6
        || !std::isfinite(channel.values()[index])) return std::nullopt;
    return channel.values()[index];
}

GeoCoordinate matchingCoordinate(const TelemetrySession &session, double latitude, double longitude)
{
    // The validated RaceChrono VBO exporter uses VBOX west-positive longitude;
    // RCZ uses east-positive. Normalize only comparison evidence, preserving the
    // existing session/gate coordinate contract and all telemetry values.
    if (session.metadata.value("gpsLongitudeConvention") == "west-positive") longitude = -longitude;
    return {latitude, longitude};
}

TraceEvidence traceEvidence(const TelemetrySession &session, const CancellationCheck &cancelled)
{
    TraceEvidence evidence;
    const auto *latitude = gpsChannel(session, QStringLiteral("latitude"));
    const auto *longitude = gpsChannel(session, QStringLiteral("longitude"));
    if (!latitude || !longitude) return evidence;
    const double start = std::max(latitude->timestamps().first(), longitude->timestamps().first());
    const double end = std::min(latitude->timestamps().last(), longitude->timestamps().last());
    evidence.duration = end - start;
    if (!std::isfinite(start) || !std::isfinite(evidence.duration) || evidence.duration < 10.0) return evidence;
    int valid = 0;
    std::optional<GeoCoordinate> origin;
    double displacement = 0.0;
    for (int i = 0; i < signatureSize; ++i) {
        throwIfCancelled(cancelled);
        const double time = start + evidence.duration * i / (signatureSize - 1);
        const auto lat = nearbyRawValue(*latitude, time);
        const auto lon = nearbyRawValue(*longitude, time);
        if (!lat || !lon) continue;
        const auto coordinate = matchingCoordinate(session, *lat, *lon);
        if (!isValidCoordinate(coordinate)) continue;
        evidence.gps[i] = coordinate;
        ++valid;
        if (!origin) origin = coordinate;
        const auto point = projectCoordinate(coordinate, *origin);
        displacement = std::max(displacement, std::hypot(point.eastMeters, point.northMeters));
    }
    // A stationary paddock trace or sparse GPS is not useful recording evidence.
    evidence.usable = valid >= 29 && displacement >= 50.0;
    return evidence;
}

void findPossibleMatches(TelemetryImportPlan &plan, const CancellationCheck &cancelled)
{
    QVector<TraceEvidence> traces;
    traces.reserve(plan.runs.size());
    for (const auto &run : plan.runs) traces.append(traceEvidence(*run.telemetry, cancelled));
    for (qsizetype first = 0; first < plan.runs.size(); ++first) {
        for (qsizetype second = first + 1; second < plan.runs.size(); ++second) {
            throwIfCancelled(cancelled);
            if (plan.runs[first].format == plan.runs[second].format) continue;
            const auto &a = traces[first];
            const auto &b = traces[second];
            const double durationDifference = std::abs(a.duration - b.duration);
            const double tolerance = std::clamp(std::min(a.duration, b.duration) * 0.01, 1.0, 2.0);
            if (!a.usable || !b.usable || durationDifference > tolerance) continue;
            int compared = 0;
            double maximumSeparation = 0.0;
            const auto firstClock = recordingTimestamp(*plan.runs[first].telemetry);
            const auto secondClock = recordingTimestamp(*plan.runs[second].telemetry);
            const bool sameClock = firstClock && secondClock
                && std::abs(static_cast<long double>(*firstClock) - *secondClock) <= 1000;
            const double shift = sameClock ? static_cast<double>(*secondClock - *firstClock) / 1000.0 : 0;
            for (int i = 0; i < signatureSize; ++i) {
                throwIfCancelled(cancelled);
                auto firstGps = a.gps[i];
                auto secondGps = b.gps[i];
                if (sameClock) {
                    // Compare the same UTC instant, not equal percentages of two
                    // exports whose first/last sample cadences may differ.
                    const double begin = std::max(0.0, shift);
                    const double end = std::min(plan.runs[first].telemetry->duration,
                        shift + plan.runs[second].telemetry->duration);
                    const double time = begin + (end - begin) * i / (signatureSize - 1);
                    const auto coordinate = [](const TelemetrySession &session, double at) -> std::optional<GeoCoordinate> {
                        const auto *lat = gpsChannel(session, "latitude");
                        const auto *lon = gpsChannel(session, "longitude");
                        if (!lat || !lon) return {};
                        const auto latitude = nearbyRawValue(*lat, at);
                        const auto longitude = nearbyRawValue(*lon, at);
                        if (!latitude || !longitude) return {};
                        const auto value = matchingCoordinate(session, *latitude, *longitude);
                        return isValidCoordinate(value) ? std::optional<GeoCoordinate>(value) : std::nullopt;
                    };
                    firstGps = coordinate(*plan.runs[first].telemetry, time);
                    secondGps = coordinate(*plan.runs[second].telemetry, time - shift);
                }
                if (!firstGps || !secondGps) continue;
                const auto point = projectCoordinate(*firstGps, *secondGps);
                maximumSeparation = std::max(maximumSeparation, std::hypot(point.eastMeters, point.northMeters));
                ++compared;
            }
            if (compared < 29 || maximumSeparation > 10.0) continue;
            plan.possibleSameRuns.append({plan.runs[first].id, plan.runs[second].id, compared,
                maximumSeparation, durationDifference,
                QStringLiteral("Similar elapsed GPS traces in VBO and RCZ. Confirm recording date, clock and source grouping; files have not been merged.")});
        }
    }
}

} // namespace

TelemetryImportPlan prepareTelemetryImport(const QStringList &paths,
                                          const TelemetryImportLimits &limits,
                                          const CancellationCheck &cancelled,
                                          const std::function<void(qsizetype, qsizetype)> &progress)
{
    throwIfCancelled(cancelled);
    validateLimits(limits);
    if (paths.size() > limits.maximumFiles) {
        throw ResourceLimitError("Too many files in one import; select a smaller batch.");
    }
    TelemetryImportPlan plan;
    QHash<QByteArray, QString> retainedDigests;
    qint64 inputBytes = 0;
    qsizetype retainedSamples = 0;
    if (progress) progress(0, paths.size());
    for (const auto &path : paths) {
        throwIfCancelled(cancelled);
        TelemetryImportFileResult fileResult;
        fileResult.requestedPath = path;
        try {
            if (path.size() > 4096) throw ResourceLimitError("Telemetry source path is too long.");
            if (!TelemetrySource::supportsPath(path)) {
                throw std::runtime_error("Choose a VBO or RaceChrono RCZ telemetry file.");
            }
            const QFileInfo info(path);
            if (!info.isFile()) throw std::runtime_error("Telemetry source is not an existing regular file.");
            const qint64 size = info.size();
            if (size <= 0 || size > limits.maximumFileBytes) {
                throw ResourceLimitError("Telemetry file is empty or exceeds the per-file import limit.");
            }
            if (size > limits.maximumBatchBytes - inputBytes) {
                throw ResourceLimitError("Batch input-byte limit exceeded; import fewer recordings.");
            }
            // Count attempts, including duplicate and invalid files, to bound IO.
            inputBytes += size;
            // Keep the selected extension for parser dispatch, including links
            // to content stored under a differently named backing file.
            const QString sourcePath = QDir::cleanPath(info.absoluteFilePath());
            const auto digest = TelemetrySource::contentSha256(sourcePath, size, cancelled);
            // A VBO renamed to RCZ must fail that parser, not inherit a ready
            // result from the real VBO just because its bytes are identical.
            const QByteArray digestKey = info.suffix().toLower().toUtf8() + ':' + digest;
            const auto duplicate = retainedDigests.constFind(digestKey);
            if (duplicate != retainedDigests.cend()) {
                fileResult.status = TelemetryImportFileStatus::Duplicate;
                fileResult.runId = duplicate.value();
                fileResult.message = QStringLiteral("Identical file content already present in this batch.");
            } else {
                auto session = TelemetrySource::load(sourcePath, cancelled);
                const qsizetype samples = channelSamples(
                    session, limits.maximumRetainedChannelSamples - retainedSamples, cancelled);
                auto laps = deriveSourceLapSession(session, {}, cancelled);
                if (TelemetrySource::contentSha256(sourcePath, size, cancelled) != digest) {
                    throw std::runtime_error("Telemetry source changed during import; retry with a stable file.");
                }
                TelemetryRunProposal run;
                const QString identity = QString::fromLatin1(digest.toHex());
                run.id = QStringLiteral("run:") + identity;
                run.sourceId = QStringLiteral("sha256:") + identity;
                run.sourcePath = sourcePath;
                run.format = info.suffix().toLower();
                run.contentSha256 = digest;
                // Freeze lazy per-channel cadence statistics before this session is
                // shared as a const object; otherwise concurrent readers (GUI vs. a
                // background geometry/comparison worker) can race on first access.
                freezeCachedStatistics(session, cancelled);
                run.telemetry = std::make_shared<const TelemetrySession>(std::move(session));
                run.laps = std::move(laps);
                retainedSamples += samples;
                retainedDigests.insert(digestKey, run.id);
                fileResult.status = TelemetryImportFileStatus::Ready;
                fileResult.runId = run.id;
                plan.runs.append(std::move(run));
            }
        } catch (const OperationCancelled &) {
            throw;
        } catch (const std::bad_alloc &) {
            // Do not disguise process memory exhaustion as an ordinary bad file.
            throw;
        } catch (const std::exception &error) {
            fileResult.message = QString::fromUtf8(error.what());
        }
        plan.files.append(std::move(fileResult));
        if (progress) progress(plan.files.size(), paths.size());
    }
    findPossibleMatches(plan, cancelled);
    throwIfCancelled(cancelled);
    return plan;
}

QHash<QString, QString> automaticVboPrimaries(const TelemetryImportPlan &plan)
{
    if (plan.runs.size() > 64 || plan.possibleSameRuns.size() > 2016)
        throw ResourceLimitError("Source grouping exceeds the import limit.");
    QHash<QString, const TelemetryRunProposal *> runs;
    QHash<QString, QString> groups;
    for (const auto &run : plan.runs) { runs.insert(run.id, &run); groups.insert(run.id, run.id); }
    QVector<QPair<QString, QString>> matches;
    QHash<QString, int> counts;
    for (const auto &match : plan.possibleSameRuns) {
        const auto *a = runs.value(match.firstRunId);
        const auto *b = runs.value(match.secondRunId);
        if (!a || !b || !a->telemetry || !b->telemetry || a->format == b->format) continue;
        const auto first = recordingTimestamp(*a->telemetry);
        const auto second = recordingTimestamp(*b->telemetry);
        if (!first || !second || std::abs(static_cast<long double>(*first) - *second) > 1000
            || std::abs(a->telemetry->duration - b->telemetry->duration) > 2) continue;
        if (match.comparedGpsSamples < 29 || !std::isfinite(match.maximumSeparationMeters)
            || match.maximumSeparationMeters > 10) continue;
        const auto *primary = a->format == "vbo" ? a : b;
        const auto *alternative = a->format == "rcz" ? a : b;
        if (primary->format != "vbo" || alternative->format != "rcz") continue;
        matches.append({alternative->id, primary->id});
        ++counts[a->id]; ++counts[b->id];
    }
    for (const auto &[alternative, primary] : matches)
        if (counts[alternative] == 1 && counts[primary] == 1) groups[alternative] = primary;
    return groups;
}

} // namespace FlappedEar
