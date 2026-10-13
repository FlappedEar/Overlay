#pragma once

#include "export/MediaProbe.h"
#include "export/MediaTimeline.h"
#include "project/ProjectSourceReference.h"
#include "telemetry/LapTiming.h"
#include "telemetry/TelemetrySession.h"
#include "telemetry/TrackGeometry.h"

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include <atomic>
#include <memory>
#include <optional>

namespace FlappedEar {

// KAN-215: the work of opening a video or a telemetry recording, which runs on
// a background thread, apart from the editor state that receives it. Each
// function is a pure function of its inputs and the file: it never touches the
// controller, honours the cancellation flag between steps and reports every
// failure in the result instead of throwing.
namespace SourceLoading {

// KAN-105: a chapter after the first, as asked for and as found.
struct VideoChapterInput {
    ProjectSourceReference reference;  // empty for a newly chosen file
    QString path;                      // resolved; empty when missing
    double durationSeconds = 0.0;      // saved duration, kept for a gap
    double gapBeforeSeconds = 0.0;     // KAN-262: recorded time without video before it
    // KAN-263: recording time at the clip's media time 0 (BetterDriver); the
    // gap before it is then worked out from the offsets and the probed lengths.
    std::optional<double> recordingOffsetSeconds = std::nullopt;
};

struct VideoChapterState {
    ProjectSourceReference reference;
    QString path;
    double durationSeconds = 0.0;
    bool available = false;
    QString problem;                   // why it is a gap
    MediaInfo mediaInfo;               // probed, when available (KAN-106)
    double gapBeforeSeconds = 0.0;     // KAN-262: recorded time without video before it
    std::optional<double> recordingOffsetSeconds = std::nullopt; // KAN-263, as in VideoChapterInput
};

struct VideoProbeResult {
    bool success = false;
    bool cancelled = false;
    QString path;
    MediaInfo mediaInfo;
    QVector<VideoChapterState> chapters; // all chapters, the first included; empty for one video
    QString error;
    quint64 generation = 0;
    QJsonObject fingerprint;
    QJsonObject expectedFingerprint;
    bool relink = false;
    // KAN-263: the first clip's recording offset; the video's sync when set.
    std::optional<double> recordingOffsetSeconds = std::nullopt;
};

struct VboLoadResult {
    bool success = false;
    bool cancelled = false;
    QString path;
    TelemetrySession session;
    TrackGeometry geometry;
    LapSession lapSession;
    QByteArray contentRevision;
    bool contentMismatch = false;
    QString error;
    quint64 generation = 0;
    QJsonObject fingerprint;
    QJsonObject expectedFingerprint;
    bool relink = false;
};

using Cancellation = std::shared_ptr<std::atomic_bool>;

// What a probed video's chapters mean once committed (KAN-105, KAN-106): the
// chapters that stay (none for one video, or when a duration is unknown), the
// playback timeline, how many are gaps, and the single source export reads.
struct ChapterDerivation {
    QVector<VideoChapterState> chapters;
    MediaTimeline timeline;                // invalid for one video
    bool unusableDurations = false;        // a chapter without a known duration: only the first opens
    int gaps = 0;
    bool gapsBlockExport = false;          // a chaptered video with a missing chapter cannot be exported
    QStringList exportPaths;               // the chapters to export as one source; empty if none
    std::optional<MediaInfo> exportInfo;   // their combined probe
    QString exportProblem;                 // why they cannot be combined; empty if they can
};

[[nodiscard]] ChapterDerivation deriveChapters(const QVector<VideoChapterState> &probed);

// KAN-215: the chapters a saved project names for its video, before anything
// is probed. `pending` keeps them (the first included) as not yet verified, so
// a save before the probe finishes writes them back; `timeline` holds their
// saved durations; `further` are the chapters after the first, each resolved
// against the project, for the probe to check.
struct SavedChapters {
    QVector<VideoChapterState> pending;
    MediaTimeline timeline;                // invalid for one video
    QVector<VideoChapterInput> further;
};

[[nodiscard]] SavedChapters savedChapters(const QJsonObject &video, const QString &projectPath);

// Probes the video and fingerprints it. With `chapters`, probes each further
// chapter too and checks it against its saved fingerprint; one that is
// missing, unreadable or no longer the same file is a gap of its saved
// duration (KAN-105).
[[nodiscard]] VideoProbeResult probeVideo(const QString &path, quint64 generation,
    const Cancellation &cancellation, const QJsonObject &expectedFingerprint, bool relink,
    const QVector<VideoChapterInput> &chapters, std::optional<double> recordingOffsetSeconds = std::nullopt);

// KAN-263: sets each chapter's gapBeforeSeconds from the recording offsets of
// the chapters that have one. The first chapter's offset is the origin of the
// chapter timeline; a later chapter starts at its offset minus that, so the
// gap is that start minus where the previous chapter ended (never below 0:
// clips are not overlapped, and never above the project format's limit).
// Chapters without an offset keep the gap they have.
void applyRecordedGaps(QVector<VideoChapterState> &chapters);

// Hashes, parses and analyses the recording (track and laps). The recording
// is hashed again at the end; one that changed meanwhile fails. A recording
// whose hash differs from a non-empty `expectedRevision` is flagged
// `contentMismatch` (KAN-208).
[[nodiscard]] VboLoadResult loadTelemetry(const QString &path, quint64 generation,
    const Cancellation &cancellation, const QByteArray &expectedRevision,
    const QJsonObject &expectedFingerprint, bool relink);

} // namespace SourceLoading

} // namespace FlappedEar
