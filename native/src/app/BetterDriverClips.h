#pragma once

#include "telemetry/TelemetrySession.h"

#include <QString>
#include <QVector>

namespace FlappedEar {

// KAN-263: the video clips a BetterDriver recording names, found next to the
// recording. The VBO's `[avi]` section gives the file names
// (`<base><nnnn>.<ext>`, see docs/betterdriver-video.md); this looks for them
// in the recording's own folder and nowhere else, and plans the video to open.
namespace BetterDriverClips {

struct Clip {
    int index = 0;
    QString fileName;
    QString path;                         // absolute; empty when the file is not beside the recording
    QString expectedPath;                 // where it would be: the recording's folder and file name
    double offsetSeconds = 0.0;           // recording time at the clip's media time 0
    double estimatedDurationSeconds = 0.0; // until the clip's last row with video; a missing clip's length
};

struct Plan {
    QVector<Clip> clips;   // from the first clip that exists; later missing clips stay in as gaps
    int found = 0;         // clip files found beside the recording
    int total = 0;         // clips the recording names
    QString problem;       // why no video can be opened; empty when clips can be
};

[[nodiscard]] Plan plan(const RecordingVideoSeries &series, const QString &recordingPath);

} // namespace BetterDriverClips
} // namespace FlappedEar
