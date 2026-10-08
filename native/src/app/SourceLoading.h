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
};

struct VideoChapterState {
    ProjectSourceReference reference;
    QString path;
    double durationSeconds = 0.0;
    bool available = false;
    QString problem;                   // why it is a gap
    MediaInfo mediaInfo;               // probed, when available (KAN-106)
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
    bool gapsBlockExport = false;          // a chaptered video with a gap cannot be exported
    QStringList exportPaths;               // the chapters to export as one source; empty if none
    std::optional<MediaInfo> exportInfo;   // their combined probe
    QString exportProblem;                 // why they cannot be combined; empty if they can
};

[[nodiscard]] ChapterDerivation deriveChapters(const QVector<VideoChapterState> &probed);

// Probes the video and fingerprints it. With `chapters`, probes each further
// chapter too and checks it against its saved fingerprint; one that is
// missing, unreadable or no longer the same file is a gap of its saved
// duration (KAN-105).
[[nodiscard]] VideoProbeResult probeVideo(const QString &path, quint64 generation,
    const Cancellation &cancellation, const QJsonObject &expectedFingerprint, bool relink,
    const QVector<VideoChapterInput> &chapters);

// Hashes, parses and analyses the recording (track and laps). The recording
// is hashed again at the end; one that changed meanwhile fails. A recording
// whose hash differs from a non-empty `expectedRevision` is flagged
// `contentMismatch` (KAN-208).
[[nodiscard]] VboLoadResult loadTelemetry(const QString &path, quint64 generation,
    const Cancellation &cancellation, const QByteArray &expectedRevision,
    const QJsonObject &expectedFingerprint, bool relink);

} // namespace SourceLoading

} // namespace FlappedEar
